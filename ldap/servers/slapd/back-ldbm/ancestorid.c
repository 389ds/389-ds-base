/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include "back-ldbm.h"
#include "import.h"

static const char *sourcefile = "ancestorid.c";

static int
ancestorid_addordel(
    backend *be,
    dbi_db_t *db,
    ID node_id,
    ID id,
    back_txn *txn,
    struct attrinfo *ai,
    int flags,
    int *allids)
{
    dbi_val_t key = {0};
    char keybuf[24];
    int ret = 0;

    /* Initialize key dbi_val_t */
    dblayer_value_set_buffer(be, &key, keybuf, sizeof(keybuf));
    key.size = PR_snprintf(key.data, key.ulen, "%c%lu",
                           EQ_PREFIX, (u_long)node_id);
    key.size++; /* include the null terminator */

    if (flags & BE_INDEX_ADD) {
#if 1
        slapi_log_err(SLAPI_LOG_TRACE, "ancestorid_addordel", "Insert ancestorid %lu:%lu\n",
                      (u_long)node_id, (u_long)id);
#endif
        ret = idl_insert_key(be, db, &key, id, txn, ai, allids);
    } else {
#if 1
        slapi_log_err(SLAPI_LOG_TRACE, "ancestorid_addordel", "Delete ancestorid %lu:%lu\n",
                      (u_long)node_id, (u_long)id);
#endif
        ret = idl_delete_key(be, db, &key, id, txn, ai);
    }

    if (ret != 0) {
        ldbm_nasty("ancestorid_addordel", sourcefile, 13120, ret);
    }

    return ret;
}

/*
 * Update ancestorid index inserting or deleting depending on flags.
 * The entry ids to be indexed are given by id (a base object)
 * and optionally subtree_idl (descendants of the base object).
 * The ancestorid keys to be updated are derived from nodes
 * in the tree from low up to high. Whether the low and high nodes
 * themselves are updated is given by include_low and include_high.
 */
static int
ldbm_ancestorid_index_update(
    backend *be,
    const Slapi_DN *low,
    const Slapi_DN *high,
    int include_low,
    int include_high,
    ID id,
    IDList *subtree_idl,
    int flags, /* BE_INDEX_ADD, BE_INDEX_DEL */
    back_txn *txn)
{
    dbi_db_t *db = NULL;
    int allids = IDL_INSERT_NORMAL;
    Slapi_DN sdn;
    Slapi_DN nextsdn;
    struct attrinfo *ai = NULL;
    ID node_id, sub_id;
    idl_iterator iter;
    int err = 0, ret = 0;

    slapi_sdn_init(&sdn);
    slapi_sdn_init(&nextsdn);

    /* Open the ancestorid index */
    ainfo_get(be, (char *)LDBM_ANCESTORID_STR, &ai);
    ret = dblayer_get_index_file(be, ai, &db, DBOPEN_CREATE);
    if (ret != 0) {
        ldbm_nasty("ldbm_ancestorid_index_update", sourcefile, 13130, ret);
        goto out;
    }

    slapi_sdn_copy(low, &sdn);

    if (include_low == 0) {
        if (slapi_sdn_compare(&sdn, high) == 0) {
            goto out;
        }
        /* Get the next highest DN */
        slapi_sdn_get_parent(&sdn, &nextsdn);
        slapi_sdn_copy(&nextsdn, &sdn);
    }

    /* Iterate up through the tree */
    do {
        if (slapi_sdn_isempty(&sdn)) {
            break;
        }

        /* Have we reached the high node? */
        if (include_high == 0 && slapi_sdn_compare(&sdn, high) == 0) {
            break;
        }

        /* Get the id for that DN */
        if (entryrdn_get_switch()) { /* subtree-rename: on */
            node_id = 0;
            err = entryrdn_index_read(be, &sdn, &node_id, txn);
            if (err) {
                if (DBI_RC_NOTFOUND != err) {
                    ldbm_nasty("ldbm_ancestorid_index_update", sourcefile, 13141, err);
                    slapi_log_err(SLAPI_LOG_ERR, "ldbm_ancestorid_index_update",
                                  "entryrdn_index_read(%s)\n", slapi_sdn_get_dn(&sdn));
                    ret = err;
                }
                break;
            }
        } else {
            IDList *idl = NULL;
            struct berval ndnv;
            ndnv.bv_val = (void *)slapi_sdn_get_ndn(&sdn);
            ndnv.bv_len = slapi_sdn_get_ndn_len(&sdn);
            err = 0;
            idl = index_read(be, LDBM_ENTRYDN_STR, indextype_EQUALITY, &ndnv, txn, &err);
            if (idl == NULL) {
                if (err != 0 && err != DBI_RC_NOTFOUND) {
                    ldbm_nasty("ldbm_ancestorid_index_update", sourcefile, 13140, err);
                    ret = err;
                }
                break;
            }
            node_id = idl_firstid(idl);
            idl_free(&idl);
        }

        /* Update ancestorid for the base entry */
        ret = ancestorid_addordel(be, db, node_id, id, txn, ai, flags, &allids);
        if (ret != 0)
            break;

        /*
         * If this node was already allids then all higher nodes must already
         * be at allids since the higher nodes must have a greater number
         * of descendants. Therefore no point continuing.
         */
        if (allids == IDL_INSERT_ALLIDS)
            break;

        /* Update ancestorid for any subtree entries */
        if (subtree_idl != NULL && ((flags & BE_INDEX_ADD) || (!ALLIDS(subtree_idl)))) {
            iter = idl_iterator_init(subtree_idl);
            while ((sub_id = idl_iterator_dereference_increment(&iter, subtree_idl)) != NOID) {
                ret = ancestorid_addordel(be, db, node_id, sub_id, txn, ai, flags, &allids);
                if (ret != 0)
                    break;
            }
            if (ret != 0)
                break;
        }

        /* Have we reached the high node? */
        if (slapi_sdn_compare(&sdn, high) == 0) {
            break;
        }

        /* Get the next highest DN */
        slapi_sdn_get_parent(&sdn, &nextsdn);
        slapi_sdn_copy(&nextsdn, &sdn);

    } while (ret == 0);

out:
    slapi_sdn_done(&sdn);
    slapi_sdn_done(&nextsdn);

    /* Release the ancestorid file */
    if (db != NULL) {
        dblayer_release_index_file(be, ai, db);
    }

    return ret;
}

/*
 * Update the ancestorid index for a single entry.
 * This function depends on the integrity of the entrydn index.
 */
int
ldbm_ancestorid_index_entry(
    backend *be,
    struct backentry *e,
    int flags, /* BE_INDEX_ADD, BE_INDEX_DEL */
    back_txn *txn)
{
    int ret = 0;

    ret = ldbm_ancestorid_index_update(be,
                                       slapi_entry_get_sdn_const(e->ep_entry),
                                       slapi_be_getsuffix(be, 0),
                                       0, 1, e->ep_id, NULL, flags, txn);

    return ret;
}

/*
 * Returns <0, 0, >0 according to whether right is a suffix of left,
 * neither is a suffix of the other, or left is a suffix of right.
 * If common is non-null then the common suffix of left and right
 * is returned in *common.
 */
static int
_sdn_suffix_cmp(
    const Slapi_DN *left,
    const Slapi_DN *right,
    Slapi_DN *common)
{
    char **rdns1, **rdns2;
    int count1, count2, i, ret = 0;
    size_t len = 0;
    char *p, *ndnstr;

    rdns1 = slapi_ldap_explode_dn(slapi_sdn_get_ndn(left), 0);
    rdns2 = slapi_ldap_explode_dn(slapi_sdn_get_ndn(right), 0);

    if (NULL == rdns1) {
        if (NULL == rdns2) {
            ret = 0;
        } else {
            ret = 1;
        }
        goto out;
    } else {
        if (NULL == rdns2) {
            ret = -1;
            goto out;
        }
    }

    for (count1 = 0; rdns1[count1] != NULL; count1++)
        ;
    count1--;

    for (count2 = 0; rdns2[count2] != NULL; count2++)
        ;
    count2--;

    while (count1 >= 0 && count2 >= 0) {
        if (strcmp(rdns1[count1], rdns2[count2]) != 0)
            break;
        count1--;
        count2--;
    }

    count1++;
    count2++;

    if (count1 == 0 && count2 == 0) {
        /* equal */
        ret = 0;
    } else if (count1 == 0) {
        /* left is suffix of right */
        ret = 1;
    } else if (count2 == 0) {
        /* right is suffix of left */
        ret = -1;
    } else {
        /* common prefix (possibly root), not left nor right */
        ret = 0;
    }

    /* if caller does not want the common prefix then we're done */
    if (common == NULL)
        goto out;

    /* figure out how much space we need */
    for (i = count1; rdns1[i] != NULL; i++) {
        len += strlen(rdns1[i]) + 1;
    }

    /* write the string */
    p = ndnstr = slapi_ch_calloc(len + 1, sizeof(char));
    for (i = count1; rdns1[i] != NULL; i++) {
        sprintf(p, "%s%s", (p != ndnstr) ? "," : "", rdns1[i]);
        p += strlen(p);
    }

    /* return the DN */
    slapi_sdn_set_dn_passin(common, ndnstr);

    slapi_log_err(SLAPI_LOG_TRACE, "_sdn_suffix_cmp", "Common suffix <%s>\n",
                  slapi_sdn_get_dn(common));

out:
    slapi_ldap_value_free(rdns1);
    slapi_ldap_value_free(rdns2);

    slapi_log_err(SLAPI_LOG_TRACE, "_sdn_suffix_cmp", "(<%s>, <%s>) => %d\n",
                  slapi_sdn_get_dn(left), slapi_sdn_get_dn(right), ret);

    return ret;
}

int
ldbm_ancestorid_move_subtree(
    backend *be,
    const Slapi_DN *olddn,
    const Slapi_DN *newdn,
    ID id,
    IDList *subtree_idl,
    back_txn *txn)
{
    int ret = 0;
    Slapi_DN commonsdn;


    slapi_sdn_init(&commonsdn);
    /* Determine the common ancestor */
    (void)_sdn_suffix_cmp(olddn, newdn, &commonsdn);

    /* Delete from old ancestors */
    ret = ldbm_ancestorid_index_update(be,
                                       olddn,
                                       &commonsdn,
                                       0,
                                       0,
                                       id,
                                       subtree_idl,
                                       BE_INDEX_DEL,
                                       txn);
    if (ret != 0)
        goto out;

    /* Add to new ancestors */
    ret = ldbm_ancestorid_index_update(be,
                                       newdn,
                                       &commonsdn,
                                       0,
                                       0,
                                       id,
                                       subtree_idl,
                                       BE_INDEX_ADD,
                                       txn);

out:
    slapi_sdn_done(&commonsdn);
    return ret;
}

int
ldbm_ancestorid_read_ext(
    backend *be,
    back_txn *txn,
    ID id,
    IDList **idl,
    int allidslimit)
{
    int ret = 0;
    struct berval bv;
    char keybuf[24];

    bv.bv_val = keybuf;
    bv.bv_len = PR_snprintf(keybuf, sizeof(keybuf), "%lu", (u_long)id);

    *idl = index_read_ext_allids(NULL, be, (char *)LDBM_ANCESTORID_STR, indextype_EQUALITY, &bv, txn, &ret, NULL, allidslimit);

    return ret;
}

int
ldbm_ancestorid_read(
    backend *be,
    back_txn *txn,
    ID id,
    IDList **idl)
{
    return ldbm_ancestorid_read_ext(be, txn, id, idl, 0);
}

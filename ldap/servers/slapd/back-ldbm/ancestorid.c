/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include "back-ldbm.h"

static char *sourcefile = "ancestorid";

/* Start of definitions for a simple cache using a hash table */

typedef struct id2idl {
    ID keyid;
    IDList *idl;
    struct id2idl *next;
} id2idl;

static void id2idl_free(id2idl **ididl);
static int id2idl_same_key(const void *ididl, const void *k);

typedef Hashtable id2idl_hash;

#define id2idl_new_hash(size) new_hash(size,HASHLOC(id2idl,next),NULL,id2idl_same_key)
#define id2idl_hash_lookup(ht,key,he) find_hash(ht,key,sizeof(ID),(void**)(he))
#define id2idl_hash_add(ht,key,he,alt) add_hash(ht,key,sizeof(ID),he,(void**)(alt))
#define id2idl_hash_remove(ht,key) remove_hash(ht,key,sizeof(ID))

static void id2idl_hash_destroy(id2idl_hash *ht);

/* End of definitions for a simple cache using a hash table */

static int ldbm_parentid(backend *be, DB_TXN *txn, ID id, ID *ppid);
static int check_cache(id2idl_hash *ht);
static IDList *idl_union_allids(backend *be, struct attrinfo *ai, IDList *a, IDList *b);

static int ldbm_get_nonleaf_ids(backend *be, DB_TXN *txn, IDList **idl)
{
    int ret = 0;
    DB *db    = NULL;
    DBC *dbc  = NULL; 
    DBT key  = {0};
    DBT data = {0};
    struct attrinfo *ai = NULL;
    IDList *nodes = NULL;
    ID id;

    /* Open the parentid index */
    ainfo_get( be, "parentid", &ai );

    /* Open the parentid index file */
    ret = dblayer_get_index_file(be, ai, &db, DBOPEN_CREATE);
    if (ret != 0) {
        ldbm_nasty(sourcefile,13010,ret);
        goto out;
    }

    /* Get a cursor so we can walk through the parentid */
    ret = db->cursor(db,txn,&dbc,0);
    if (ret != 0 ) {
        ldbm_nasty(sourcefile,13020,ret);
        goto out;
    }

    /* For each key which is an equality key */
    do {
        ret = dbc->c_get(dbc,&key,&data,DB_NEXT_NODUP);
        if ((ret == 0) && (*(char*)key.data == EQ_PREFIX)) {
            id = (ID) strtoul((char*)key.data+1, NULL, 10);
            idl_insert(&nodes, id);
        }
    } while (ret == 0);

    /* Check for success */
    if (ret == DB_NOTFOUND) ret = 0;
    if (ret != 0) ldbm_nasty(sourcefile,13030,ret);

 out:
    /* Close the cursor */
    if (dbc != NULL) {
        if (ret == 0) {
            ret = dbc->c_close(dbc);
            if (ret != 0) ldbm_nasty(sourcefile,13040,ret);
        } else {
            (void)dbc->c_close(dbc);
        }
    }

    /* Release the parentid file */
    if (db != NULL) {
        dblayer_release_index_file( be, ai, db );
    }

    /* Return the idlist */
    if (ret == 0) {
        *idl = nodes;
        LDAPDebug(LDAP_DEBUG_TRACE, "found %lu nodes for ancestorid\n", 
                  (u_long)IDL_NIDS(nodes), 0, 0);
    } else {
        idl_free(nodes);
        *idl = NULL;
    }

    return ret;
}

/*
 * XXX: This function creates ancestorid index, which is a sort of hack.
 *      This function handles idl directly, 
 *      which should have been implemented in the idl file(s).
 *      When the idl code would be updated in the future,
 *      this function may also get affected.
 *      (see also bug#: 605535)
 *
 * Construct the ancestorid index. Requirements:
 * - The backend is read only.
 * - The parentid index is accurate.
 * - Non-leaf entries have IDs less than their descendants
 *   (guaranteed after a database import but not after a subtree move)
 *
 */
int ldbm_ancestorid_create_index(backend *be)
{
    int ret = 0;
    DB *db_pid    = NULL;
    DB *db_aid    = NULL;
    DBT key  = {0};
    DB_TXN *txn = NULL;
    struct attrinfo *ai_pid = NULL;
    struct attrinfo *ai_aid = NULL;
    char keybuf[24];
    IDList *nodes = NULL;
    IDList *children = NULL, *descendants = NULL;
    NIDS nids;
    ID id, parentid;
    id2idl_hash *ht = NULL;
    id2idl *ididl;

    /*
     * We need to iterate depth-first through the non-leaf nodes
     * in the tree amassing an idlist of descendant ids for each node.
     * We would prefer to go through the parentid keys just once from 
     * highest id to lowest id but the btree ordering is by string
     * rather than number. So we go through the parentid keys in btree
     * order first of all to create an idlist of all the non-leaf nodes.
     * Then we can use the idlist to iterate through parentid in the
     * correct order.
     */

    LDAPDebug(LDAP_DEBUG_TRACE, "Creating ancestorid index\n", 0,0,0);

    /* Get the non-leaf node IDs */
    ret = ldbm_get_nonleaf_ids(be, txn, &nodes);
    if (ret != 0) return ret;

    /* Get the ancestorid index */
    ainfo_get(be, "ancestorid", &ai_aid);

    /* Prevent any other use of the index */
    ai_aid->ai_indexmask |= INDEX_OFFLINE;

    /* Open the ancestorid index file */
    ret = dblayer_get_index_file(be, ai_aid, &db_aid, DBOPEN_CREATE);
    if (ret != 0) {
        ldbm_nasty(sourcefile,13050,ret);
        goto out;
    }

    /* Maybe nothing to do */
    if (nodes == NULL || nodes->b_nids == 0) {
        LDAPDebug(LDAP_DEBUG_ANY, "Nothing to do to build ancestorid index\n",
                  0, 0, 0);
        goto out;
    }

    /* Create an ancestorid cache */
    ht = id2idl_new_hash(nodes->b_nids);

    /* Get the parentid index */
    ainfo_get( be, "parentid", &ai_pid );

    /* Open the parentid index file */
    ret = dblayer_get_index_file(be, ai_pid, &db_pid, DBOPEN_CREATE);
    if (ret != 0) {
        ldbm_nasty(sourcefile,13060,ret);
        goto out;
    }

    /* Initialize key DBT */
    key.data = keybuf;
    key.ulen = sizeof(keybuf);
    key.flags = DB_DBT_USERMEM;

    /* Iterate from highest to lowest ID */
    nids = nodes->b_nids;
    do {

        nids--;
        id = nodes->b_ids[nids];

        /* Get immediate children from parentid index */
        key.size = PR_snprintf(key.data, key.ulen, "%c%lu", 
                               EQ_PREFIX, (u_long)id);
        key.size++;             /* include the null terminator */
        ret = NEW_IDL_NO_ALLID;
        children = idl_fetch(be, db_pid, &key, txn, ai_pid, &ret);
        if (ret != 0) {
            ldbm_nasty(sourcefile,13070,ret);
            break;
        }

        /* Insert into ancestorid for this node */
        if (id2idl_hash_lookup(ht, &id, &ididl)) {
            descendants = idl_union_allids(be, ai_aid, ididl->idl, children);
            idl_free(children);
            if (id2idl_hash_remove(ht, &id) == 0) {
                LDAPDebug(LDAP_DEBUG_ANY, "ancestorid hash_remove failed\n", 0,0,0);
            } else {
                id2idl_free(&ididl);
            }
        } else {
            descendants = children;
        }
        ret = idl_store_block(be, db_aid, &key, descendants, txn, ai_aid);
        if (ret != 0) break;

        /* Get parentid for this entry */
        ret = ldbm_parentid(be, txn, id, &parentid);
        if (ret != 0) {
            idl_free(descendants);
            break;
        }

        /* A suffix entry does not have a parent */
        if (parentid == NOID) {
            idl_free(descendants);
            continue;
        }

        /* Insert into ancestorid for this node's parent */
        if (id2idl_hash_lookup(ht, &parentid, &ididl)) {
            IDList *idl = idl_union_allids(be, ai_aid, ididl->idl, descendants);
            idl_free(descendants);
            idl_free(ididl->idl);
            ididl->idl = idl;
        } else {
            ididl = (id2idl*)slapi_ch_calloc(1,sizeof(id2idl));
            ididl->keyid = parentid;
            ididl->idl = descendants;
            if (id2idl_hash_add(ht, &parentid, ididl, NULL) == 0) {
                LDAPDebug(LDAP_DEBUG_ANY, "ancestorid hash_add failed\n", 0,0,0);
            }
        }

    } while (nids > 0);

    if (ret != 0) {
        goto out;
    }

    /* We're expecting the cache to be empty */
    ret = check_cache(ht);

 out:
    if (ret == 0) {
        LDAPDebug(LDAP_DEBUG_TRACE, "Created ancestorid index\n", 0,0,0);
    } else {
        LDAPDebug(LDAP_DEBUG_ANY, "Failed to create ancestorid index\n", 0,0,0);
    }

    /* Destroy the cache */
    id2idl_hash_destroy(ht);

    /* Free any leftover idlists */
    idl_free(nodes);

    /* Release the parentid file */
    if (db_pid != NULL) {
        dblayer_release_index_file( be, ai_pid, db_pid );
    }

    /* Release the ancestorid file */
    if (db_aid != NULL) {
        dblayer_release_index_file( be, ai_aid, db_aid );
    }

    /* Enable the index */
    if (ret == 0) {
        ai_aid->ai_indexmask &= ~INDEX_OFFLINE;
    }

    return ret;
}

/*
 * Get parentid of an id by reading the operational attr from id2entry.
 */
static int ldbm_parentid(backend *be, DB_TXN *txn, ID id, ID *ppid)
{
    int ret = 0;
    DB *db = NULL;
    DBT	key = {0};
    DBT data = {0};
    ID stored_id;
    char *p;

    /* Open the id2entry file */
    ret = dblayer_get_id2entry(be, &db);
    if (ret != 0) {
        ldbm_nasty(sourcefile,13100,ret);
        goto out;
    }

    /* Initialize key and data DBTs */
    id_internal_to_stored(id, (char *)&stored_id);
    key.data = (char *)&stored_id;
    key.size = sizeof(stored_id);
    key.flags = DB_DBT_USERMEM;
    data.flags = DB_DBT_MALLOC;

    /* Read id2entry */
    ret = db->get(db, txn, &key, &data, 0);
    if (ret != 0) {
        ldbm_nasty(sourcefile,13110,ret);
        goto out;
    }

    /* Extract the parentid value */
#define PARENTID_STR "\nparentid:"
    p = strstr(data.data, PARENTID_STR);
    if (p == NULL) {
        *ppid = NOID;
        goto out;
    }
    *ppid = strtoul(p + strlen(PARENTID_STR), NULL, 10);

 out:
    /* Free the entry value */
    if (data.data != NULL) free(data.data);

    /* Release the id2entry file */
    if (db != NULL) {
        dblayer_release_id2entry(be, db);
    }
    return ret;
}

static void id2idl_free(id2idl **ididl)
{
    idl_free((*ididl)->idl);
    slapi_ch_free((void**)ididl);
}

static int id2idl_same_key(const void *ididl, const void *k)
{
    return (((id2idl *)ididl)->keyid == *(ID *)k);
}

static int check_cache(id2idl_hash *ht)
{
    id2idl *e;
    u_long i, found = 0;
    int ret = 0;

    if (ht == NULL) return 0;

    for (i = 0; i < ht->size; i++) {
	e = (id2idl *)ht->slot[i];
        while (e) {
            found++;
            e = e->next;
        }
    }

    if (found > 0) {
        LDAPDebug(LDAP_DEBUG_ANY, "ERROR: parentid index is not complete (%lu extra keys in ancestorid cache)\n", found,0,0);
        ret = -1;
    }

    return ret;
}

static void id2idl_hash_destroy(id2idl_hash *ht)
{
    u_long i;
    id2idl *e, *next;

    if (ht == NULL) return;

    for (i = 0; i < ht->size; i++) {
	e = (id2idl *)ht->slot[i];
        while (e) {
            next = e->next;
            id2idl_free(&e);
            e = next;
        }
    }
    slapi_ch_free((void **)&ht);
}

/*
 * idl_union_allids - return a union b
 * takes attr index allids setting into account
 */
static IDList *idl_union_allids(backend *be, struct attrinfo *ai, IDList *a, IDList *b)
{
    if (!idl_get_idl_new()) {
        if (a != NULL && b != NULL) {
            if (ALLIDS( a ) || ALLIDS( b ) || 
                (IDL_NIDS(a) + IDL_NIDS(b) > idl_get_allidslimit(ai))) {
                return( idl_allids( be ) );
            }
        }
    }
    return idl_union(be, a, b);
}

static int ancestorid_addordel(
    backend *be, 
    DB* db, 
    ID node_id, 
    ID id, 
    DB_TXN *txn, 
    struct attrinfo *ai,
    int flags,
    int *allids
)
{
    DBT key = {0};
    char keybuf[24];
    int ret = 0;

    /* Initialize key DBT */
    key.data = keybuf;
    key.ulen = sizeof(keybuf);
    key.flags = DB_DBT_USERMEM;
    key.size = PR_snprintf(key.data, key.ulen, "%c%lu", 
                           EQ_PREFIX, (u_long)node_id);
    key.size++;             /* include the null terminator */

    if (flags & BE_INDEX_ADD) {
#if 1
        LDAPDebug(LDAP_DEBUG_TRACE, "insert ancestorid %lu:%lu\n", 
                  (u_long)node_id, (u_long)id, 0);
#endif
        ret = idl_insert_key(be, db, &key, id, txn, ai, allids);
    } else {
#if 1
        LDAPDebug(LDAP_DEBUG_TRACE, "delete ancestorid %lu:%lu\n", 
                  (u_long)node_id, (u_long)id, 0);
#endif
        ret = idl_delete_key(be, db, &key, id, txn, ai);
    }

    if (ret != 0) {
        ldbm_nasty(sourcefile,13120,ret);
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
static int ldbm_ancestorid_index_update(
    backend		*be,
    const Slapi_DN	*low,
    const Slapi_DN	*high,
    int			include_low,
    int			include_high,
    ID			id,
    IDList		*subtree_idl,
    int			flags,  /* BE_INDEX_ADD, BE_INDEX_DEL */
    back_txn		*txn
)
{
    DB *db = NULL;
    int allids = IDL_INSERT_NORMAL;
    Slapi_DN dn = {0};
    Slapi_DN nextdn = {0};
    struct attrinfo *ai = NULL;
    struct berval ndnv;
    ID node_id, sub_id;
    IDList *idl;
    idl_iterator iter;
    int err = 0, ret = 0;
    DB_TXN *db_txn = txn != NULL ? txn->back_txn_txn : NULL;

    /* Open the ancestorid index */
    ainfo_get(be, "ancestorid", &ai);
    ret = dblayer_get_index_file(be, ai, &db, DBOPEN_CREATE);
    if (ret != 0) {
        ldbm_nasty(sourcefile,13130,ret);
        goto out;
    }

    slapi_sdn_copy(low, &dn);

    if (include_low == 0) {
        if (slapi_sdn_compare(&dn, high) == 0) {
            goto out;
        }
        /* Get the next highest DN */
        slapi_sdn_get_parent(&dn, &nextdn);
        slapi_sdn_copy(&nextdn, &dn);
    }

    /* Iterate up through the tree */
    do {
        if (slapi_sdn_isempty(&dn)) {
            break;
        }

        /* Have we reached the high node? */
        if (include_high == 0 && slapi_sdn_compare(&dn, high) == 0) {
            break;
        }

        /* Get the id for that DN */
	ndnv.bv_val = (void*)slapi_sdn_get_ndn(&dn);
	ndnv.bv_len = slapi_sdn_get_ndn_len(&dn);
        err = 0;
        idl = index_read(be, "entrydn", indextype_EQUALITY, &ndnv, txn, &err);
        if (idl == NULL) {
            if (err != 0 && err != DB_NOTFOUND) {
                ldbm_nasty(sourcefile,13140,ret);
                ret = err;
            }
            break;
        }
        node_id = idl_firstid(idl);
        idl_free(idl);

        /* Update ancestorid for the base entry */
        ret = ancestorid_addordel(be, db, node_id, id, db_txn, ai, flags, &allids);
        if (ret != 0) break;

        /* 
         * If this node was already allids then all higher nodes must already 
         * be at allids since the higher nodes must have a greater number
         * of descendants. Therefore no point continuing.
         */
        if (allids == IDL_INSERT_ALLIDS) break;

        /* Update ancestorid for any subtree entries */
        if (subtree_idl != NULL && ((flags & BE_INDEX_ADD) || (!ALLIDS(subtree_idl)))) {
            iter = idl_iterator_init(subtree_idl);
            while ((sub_id = idl_iterator_dereference_increment(&iter, subtree_idl)) != NOID) {
                ret = ancestorid_addordel(be, db, node_id, sub_id, db_txn, ai, flags, &allids);
                if (ret != 0) break;
            }
            if (ret != 0) break;
        }

        /* Have we reached the high node? */
        if (slapi_sdn_compare(&dn, high) == 0) {
            break;
        }

        /* Get the next highest DN */
        slapi_sdn_get_parent(&dn, &nextdn);
        slapi_sdn_copy(&nextdn, &dn);

    } while (ret == 0);

 out:
    slapi_sdn_done(&dn);
    slapi_sdn_done(&nextdn);

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
int ldbm_ancestorid_index_entry(
    backend		*be,
    struct backentry	*e,
    int			flags,  /* BE_INDEX_ADD, BE_INDEX_DEL */
    back_txn		*txn
)
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
int slapi_sdn_suffix_cmp(
    const Slapi_DN *left, 
    const Slapi_DN *right, 
    Slapi_DN *common
)
{
    char **rdns1, **rdns2;
    int count1, count2, i, ret = 0;
    size_t len = 0;
    char *p, *ndnstr;

    rdns1 = ldap_explode_dn(slapi_sdn_get_ndn(left), 0);
    rdns2 = ldap_explode_dn(slapi_sdn_get_ndn(right), 0);

    for(count1 = 0; rdns1[count1]!=NULL; count1++){
    }
    count1--;

    for(count2 = 0; rdns2[count2]!=NULL; count2++){
    }
    count2--;

    while (count1 >= 0 && count2 >= 0) {
        if (strcmp(rdns1[count1], rdns2[count2]) != 0) break;
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
    if (common == NULL) goto out;

    /* figure out how much space we need */
    for (i = count1; rdns1[i] != NULL; i++) {
        len += strlen(rdns1[i]) + 1;
    }

    /* write the string */
    p = ndnstr = slapi_ch_calloc(len+1,sizeof(char));
    for (i = count1; rdns1[i] != NULL; i++) {
        sprintf(p, "%s%s", (p != ndnstr) ? "," : "", rdns1[i]);
        p += strlen(p);
    }

    /* return the DN */
    slapi_sdn_set_dn_passin(common, ndnstr);

    LDAPDebug(LDAP_DEBUG_TRACE, "common suffix <%s>\n",
              slapi_sdn_get_dn(common), 0, 0);

 out:
    ldap_value_free(rdns1);
    ldap_value_free(rdns2);

    LDAPDebug(LDAP_DEBUG_TRACE, "slapi_sdn_suffix_cmp(<%s>, <%s>) => %d\n",
              slapi_sdn_get_dn(left), slapi_sdn_get_dn(right), ret);

    return ret;
}

int ldbm_ancestorid_move_subtree(
    backend		*be,
    const Slapi_DN	*olddn,
    const Slapi_DN	*newdn,
    ID			id,
    IDList		*subtree_idl,
    back_txn		*txn
)
{
    int ret = 0;
    Slapi_DN commondn = {0};

    /* Determine the common ancestor */
    (void)slapi_sdn_suffix_cmp(olddn, newdn, &commondn);

    /* Delete from old ancestors */
    ret = ldbm_ancestorid_index_update(be, 
                                       olddn,
                                       &commondn,
                                       0,
                                       0,
                                       id,
                                       subtree_idl,
                                       BE_INDEX_DEL,
                                       txn);
    if (ret != 0) goto out;

    /* Add to new ancestors */
    ret = ldbm_ancestorid_index_update(be, 
                                       newdn,
                                       &commondn,
                                       0,
                                       0,
                                       id,
                                       subtree_idl,
                                       BE_INDEX_ADD,
                                       txn);

 out:
    slapi_sdn_done(&commondn);
    return ret;
}

int ldbm_ancestorid_read(
    backend		*be,
    back_txn		*txn,
    ID			id,
    IDList		**idl
)
{
    int ret = 0;
    struct berval bv;
    char keybuf[24];

    bv.bv_val = keybuf;
    bv.bv_len = PR_snprintf(keybuf, sizeof(keybuf), "%lu", (u_long)id);

    *idl = index_read(be, "ancestorid", indextype_EQUALITY, &bv, txn, &ret);

    return ret;
}


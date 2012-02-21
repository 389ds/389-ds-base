/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#if defined(DEBUG)
/* #define LDAP_DEBUG_ENTRYRDN 1 -- very verbose */
#define ENTRYRDN_DEBUG 1
#endif

/* ldbm_entryrdn.c - module to access entry rdn index */

#include "back-ldbm.h"

static int entryrdn_switch = 0;
static int entryrdn_noancestorid = 0;

#ifdef ENTRYRDN_DEBUG
#define ASSERT(_x) do { \
    if (!(_x)) { \
        LDAPDebug(LDAP_DEBUG_ANY, "BAD ASSERTION at %s/%d: %s\n", \
        __FILE__, __LINE__, #_x); \
        *(char *)0L = 23; \
    } \
} while (0)
#else
#define ASSERT(_x) ;
#endif

#define ENTRYRDN_TAG "entryrdn-index"

#define RDN_INDEX_SELF 'S'
#define RDN_INDEX_CHILD 'C'
#define RDN_INDEX_PARENT 'P'

#define RDN_BULK_FETCH_BUFFER_SIZE (size_t)8*1024 /* DBLAYER_INDEX_PAGESIZE */
#define RDN_STRINGID_LEN 64

typedef struct _rdn_elem {
    char rdn_elem_id[sizeof(ID)];
    char rdn_elem_nrdn_len[2]; /* ushort; length including '\0' */
    char rdn_elem_rdn_len[2];  /* ushort; length including '\0' */
    char rdn_elem_nrdn_rdn[1]; /* "normalized rdn" '\0' "rdn" '\0' */
} rdn_elem;

#define RDN_ADDR(elem) \
    ((elem)->rdn_elem_nrdn_rdn + \
     sizeushort_stored_to_internal((elem)->rdn_elem_nrdn_len))

#define TMPID 0 /* Used for the fake ID */

/* RDN(s) which can be added even if no suffix exists in the entryrdn index */
const char *rdn_exceptions[] = {
    "nsuniqueid=ffffffff-ffffffff-ffffffff-ffffffff",
    NULL
};

/* helper functions */
static rdn_elem *_entryrdn_new_rdn_elem(backend *be, ID id, Slapi_RDN *srdn, size_t *length);
static void _entryrdn_dup_rdn_elem(const void *raw, rdn_elem **new);
static size_t _entryrdn_rdn_elem_size(rdn_elem *elem);
#ifdef LDAP_DEBUG_ENTRYRDN
static void _entryrdn_dump_rdn_elem(rdn_elem *elem);
#endif
static int _entryrdn_open_index(backend *be, struct attrinfo **ai, DB **dbp);
#if 0 /* not used */
static char *_entryrdn_encrypt_key(backend *be, const char *key, struct attrinfo *ai);
static char *_entryrdn_decrypt_key(backend *be, const char *key, struct attrinfo *ai);
#endif
static int _entryrdn_get_elem(DBC *cursor, DBT *key, DBT *data, const char *comp_key, rdn_elem **elem);
static int _entryrdn_get_tombstone_elem(DBC *cursor, Slapi_RDN *srdn, DBT *key, const char *comp_key, rdn_elem **elem);
static int _entryrdn_put_data(DBC *cursor, DBT *key, DBT *data, char type);
static int _entryrdn_del_data(DBC *cursor,  DBT *key, DBT *data);
static int _entryrdn_insert_key(backend *be, DBC *cursor, Slapi_RDN *srdn, ID id, DB_TXN *db_txn);
static int _entryrdn_insert_key_elems(backend *be, DBC *cursor, Slapi_RDN *srdn, DBT *key, rdn_elem *elem, rdn_elem *childelem, size_t childelemlen, DB_TXN *db_txn);
static int _entryrdn_delete_key(backend *be, DBC *cursor, Slapi_RDN *srdn, ID id, DB_TXN *db_txn);
static int _entryrdn_index_read(backend *be, DBC *cursor, Slapi_RDN *srdn, rdn_elem **elem, rdn_elem **parentelem, rdn_elem ***childelems, int flags, DB_TXN *db_txn);
static int _entryrdn_append_childidl(DBC *cursor, const char *nrdn, ID id, IDList **affectedidl);
static void _entryrdn_cursor_print_error(char *fn, void *key, size_t need, size_t actual, int rc);

static int entryrdn_warning_on_encryption = 1;

/*
 * This function sets the integer value val to entryrdn_switch. 
 * If val is non-zero, the entryrdn index is used and moving subtree 
 * and/or renaming an RDN which has children is enabled.  
 * If val is zero, the entrydn index is used.
 */
void
entryrdn_set_switch(int val)
{
    entryrdn_switch = val;
    return;
}

/*
 * This function gets the value of entry_switch.  
 * All the entryrdn related codes are supposed to be in the 
 * if (entryrdn_get_switch()) clauses.
 */
int
entryrdn_get_switch()
{
    return entryrdn_switch;
}

/*
 * Note: nsslapd-noancestorid never be "on" unless nsslapd-subtree-rename-switch
 * is on.
 */
void
entryrdn_set_noancestorid(int val)
{
    if (entryrdn_switch) {
        entryrdn_noancestorid = val;
    } else {
        entryrdn_noancestorid = 0;
    }
    return;
}

int
entryrdn_get_noancestorid()
{
    if (entryrdn_switch) {
        return entryrdn_noancestorid;
    } else {
        return 0;
    }
}

/*
 * Rules:
 * NULL comes before anything else.
 * Otherwise, strcmp(elem_a->rdn_elem_nrdn_rdn - elem_b->rdn_elem_nrdn_rdn) is
 * returned.
 */
int
entryrdn_compare_dups(DB *db, const DBT *a, const DBT *b)
{   
    rdn_elem *elem_a = NULL;
    rdn_elem *elem_b = NULL;
    int delta = 0;

    if (NULL == a) {
        if (NULL == b) {
            return 0;
        } else {
            return -1;
        }
    } else if (NULL == b) {
        return 1;
    }

    elem_a = (rdn_elem *)a->data;
    elem_b = (rdn_elem *)b->data;

    delta = strcmp((char *)elem_a->rdn_elem_nrdn_rdn,
                   (char *)elem_b->rdn_elem_nrdn_rdn);

    return delta;
}

/*
 * Add/Delete an entry 'e' to/from the entryrdn index
 */
int
entryrdn_index_entry(backend *be,
                     struct backentry *e,
                     int flags, /* BE_INDEX_ADD or BE_INDEX_DEL */
                     back_txn *txn)
{
    int rc = -1;
    struct attrinfo *ai = NULL;
    DB *db = NULL;
    DBC *cursor = NULL;
    DB_TXN *db_txn = (txn != NULL) ? txn->back_txn_txn : NULL;
    const Slapi_DN *sdn = NULL;
    Slapi_RDN *srdn = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "--> entryrdn_index_entry\n");
    if (NULL == be || NULL == e) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                    "entryrdn_index_entry: Param error: Empty %s\n",
                    NULL==be?"backend":NULL==e?"entry":"unknown");
        return rc;
    }
    /* Open the entryrdn index */
    rc = _entryrdn_open_index(be, &ai, &db);
    if (rc || (NULL == db)) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "entryrdn_index_entry: Opening the index failed: "
                        "%s(%d)\n",
                        rc<0?dblayer_strerror(rc):"Invalid parameter", rc);
        return rc;
    }

    srdn = slapi_entry_get_srdn(e->ep_entry);
    if (NULL == slapi_rdn_get_rdn(srdn)) {
        sdn = slapi_entry_get_sdn_const(e->ep_entry);
        if (NULL == sdn) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "entryrdn_index_entry: Empty dn\n");
            goto bail;
        }
        rc = slapi_rdn_init_all_sdn(srdn, sdn);
        if (rc < 0) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                            "entryrdn_index_entry: Failed to convert "
                            "%s to Slapi_RDN\n", slapi_sdn_get_dn(sdn));
            rc = LDAP_INVALID_DN_SYNTAX;
            goto bail;
        } else if (rc > 0) {
            slapi_log_error(SLAPI_LOG_BACKLDBM, ENTRYRDN_TAG,
                            "entryrdn_index_entry: %s does not belong to "
                            "the db\n", slapi_sdn_get_dn(sdn));
            rc = DB_NOTFOUND;
            goto bail;
        }
    }

    /* Make a cursor */
    rc = db->cursor(db, db_txn, &cursor, 0);
    if (rc) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                "entryrdn_index_entry: Failed to make a cursor: %s(%d)\n",
                dblayer_strerror(rc), rc);
        cursor = NULL;
        goto bail;
    }

    if (flags & BE_INDEX_ADD) {
        rc = _entryrdn_insert_key(be, cursor, srdn, e->ep_id, db_txn);
    } else if (flags & BE_INDEX_DEL) {
        rc = _entryrdn_delete_key(be, cursor, srdn, e->ep_id, db_txn);
        if (DB_NOTFOUND == rc) {
            rc = 0;
        }
    }

bail:
    /* Close the cursor */
    if (cursor) {
        int myrc = cursor->c_close(cursor);
        if (0 != myrc) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                  "entryrdn_index_entry: Failed to close cursor: %s(%d)\n",
                  dblayer_strerror(rc), rc);
        }
    }
    if (db) {
        dblayer_release_index_file(be, ai, db);
    }

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "<-- entryrdn_index_entry\n");
    return rc;
}

/*
 * input: Full DN in Slapi_RDN rdn
 * output: ID
 *
 * return values:  0 -- success
 *                -1 -- error
 *                      param error (broken rdn, failed to get index file)
 *                      Otherwise -- (DB errors)
 */
int
entryrdn_index_read(backend *be,
                        const Slapi_DN *sdn,
                        ID *id,
                        back_txn *txn)
{
    return entryrdn_index_read_ext(be, sdn, id, 0/*flags*/, txn);
}

int
entryrdn_index_read_ext(backend *be,
                        const Slapi_DN *sdn,
                        ID *id,
                        int flags,
                        back_txn *txn)
{
    int rc = -1;
    struct attrinfo *ai = NULL;
    Slapi_RDN srdn = {0};
    DB *db = NULL;
    DB_TXN *db_txn = (txn != NULL) ? txn->back_txn_txn : NULL;
    DBC *cursor = NULL;
    rdn_elem *elem = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "--> entryrdn_index_read\n");

    if (NULL == be || NULL == sdn || NULL == id) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                    "entryrdn_index_read: Param error: Empty %s\n",
                    NULL==be?"backend":NULL==sdn?"DN":
                    NULL==id?"id container":"unknown");
        goto bail;
    }

    *id = 0;

    rc = slapi_rdn_init_all_sdn(&srdn, sdn);
    if (rc < 0) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "entryrdn_index_read: Param error: Failed to convert "
                        "%s to Slapi_RDN\n", slapi_sdn_get_dn(sdn));
        rc = LDAP_INVALID_DN_SYNTAX;
        goto bail;
    } else if (rc > 0) {
        slapi_log_error(SLAPI_LOG_BACKLDBM, ENTRYRDN_TAG,
                        "entryrdn_index_read: %s does not belong to the db\n",
                        slapi_sdn_get_dn(sdn));
        rc = DB_NOTFOUND;
        goto bail;
    }

    /* Open the entryrdn index */
    rc = _entryrdn_open_index(be, &ai, &db);
    if (rc || (NULL == db)) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "entryrdn_index_read: Opening the index failed: "
                        "%s(%d)\n",
                        rc<0?dblayer_strerror(rc):"Invalid parameter", rc);
        db = NULL;
        goto bail;
    }

    /* Make a cursor */
    rc = db->cursor(db, db_txn, &cursor, 0);
    if (rc) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                "entryrdn_index_read: Failed to make a cursor: %s(%d)\n",
                dblayer_strerror(rc), rc);
        cursor = NULL;
        goto bail;
    }

    rc = _entryrdn_index_read(be, cursor, &srdn, &elem, NULL, NULL,
                              flags, db_txn);
    if (rc) {
        goto bail;
    }
    *id = id_stored_to_internal(elem->rdn_elem_id);

bail:
    /* Close the cursor */
    if (cursor) {
        int myrc = cursor->c_close(cursor);
        if (0 != myrc) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                            "entryrdn_index_read: Failed to close cursor: "
                            "%s(%d)\n", dblayer_strerror(rc), rc);
        }
    }
    if (db) {
        dblayer_release_index_file(be, ai, db);
    }
    slapi_rdn_done(&srdn);
    slapi_ch_free((void **)&elem);
    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "<-- entryrdn_index_read\n");
    return rc;
}

/*
 * rename oldsdn <rdn>,<old superior> to <new rdn>,<new superior>
 *
 * This function renames and/or moves the given subtree.  
 * The second argument ''oldsdn'' is the DN to be moved/renamed. 
 * In the modrdn operation, the value of newrdn is set to this third argument 
 * newsrdn.  If the new RDN is not the same as the leaf RDN in the original 
 * DN oldsdn, the original RDN is renamed to the new RDN.  
 * If the newsuperior is set in the modrdn operation, the value is set to the 
 * fourth argument newsupsdn.  If the value is non-zero, the original leaf RDN 
 * is moved under the new superior relinking the parent and child links.
 */
int
entryrdn_rename_subtree(backend *be,
                        const Slapi_DN *oldsdn,
                        Slapi_RDN *newsrdn,        /* new rdn */
                        const Slapi_DN *newsupsdn, /* new superior dn */
                        ID id,
                        back_txn *txn)
{
    int rc = -1;
    struct attrinfo *ai = NULL;
    DB *db = NULL;
    DBC *cursor = NULL;
    DB_TXN *db_txn = (txn != NULL) ? txn->back_txn_txn : NULL;
    Slapi_RDN oldsrdn = {0};
    Slapi_RDN supsrdn = {0};
    Slapi_RDN newsupsrdn = {0};
    const char *nrdn = NULL; /* normalized rdn */
    int rdnidx = -1;
    char *keybuf = NULL;
    DBT key;
    DBT renamedata;
    rdn_elem *targetelem = NULL;
    rdn_elem *newelem = NULL;
    rdn_elem *newsupelem = NULL;
    rdn_elem *oldsupelem = NULL;
    rdn_elem **childelems = NULL;
    rdn_elem **cep = NULL;
    size_t targetelemlen = 0;
    size_t newelemlen = 0;
    size_t newsupelemlen = 0;
    size_t oldsupelemlen = 0;
    const Slapi_DN *mynewsupsdn = NULL;
    Slapi_RDN *mynewsrdn = NULL;
    ID targetid = 0;

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "--> entryrdn_rename_subtree\n");

    if (NULL == be || NULL == oldsdn || 0 == id) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                "entryrdn_rename_subtree: Param error: Empty %s\n",
                NULL==be?"backend":NULL==oldsdn?"old dn":
                (NULL==newsrdn&&NULL==newsupsdn)?"new dn and new superior":
                0==id?"id":"unknown");
        goto bail;
    }

    rc = slapi_rdn_init_all_sdn(&oldsrdn, oldsdn);
    if (rc < 0) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "entryrdn_rename_subtree: Failed to convert olddn "
                        "\"%s\" to Slapi_RDN\n", slapi_sdn_get_dn(oldsdn));
        rc = LDAP_INVALID_DN_SYNTAX;
        goto bail;
    } else if (rc > 0) {
        slapi_log_error(SLAPI_LOG_BACKLDBM, ENTRYRDN_TAG,
                        "entryrdn_rename_subtree: %s does not belong to "
                        "the db\n", slapi_sdn_get_dn(oldsdn));
        rc = DB_NOTFOUND;
        goto bail;
    }

    /* newsupsdn is given and DN value is set in it. */
    if (newsupsdn && slapi_sdn_get_dn(newsupsdn)) {
        mynewsupsdn = newsupsdn;
    }
    /* newsrdn is given and RDN value is set in it. */
    if (newsrdn && slapi_rdn_get_rdn(newsrdn)) {
        /* if the new RDN value is identical to the old RDN,
         * we don't have to do "rename" */
        if (strcmp(slapi_rdn_get_nrdn(newsrdn), slapi_rdn_get_nrdn(&oldsrdn))) {
            /* did not match; let's rename it */
            mynewsrdn = newsrdn;
        }
    }
    if (NULL == mynewsrdn && NULL == mynewsupsdn) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "entryrdn_rename_subtree: No new superior is given "
                        "and new rdn %s is identical to the original\n",
                        slapi_rdn_get_rdn(&oldsrdn));
        goto bail;
    }

    /* Checking the contents of oldsrdn */
    rdnidx = slapi_rdn_get_last_ext(&oldsrdn, &nrdn, FLAG_ALL_NRDNS);
    if (rdnidx < 0 || NULL == nrdn) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "entryrdn_rename_subtree: Empty RDN\n");
        goto bail;
    } else if (0 == rdnidx) {
        if (mynewsupsdn) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                            "entryrdn_move_subtree: Moving suffix \"%s\" is "
                            "not alloweds\n", nrdn);
            goto bail;
        } else {
            /* newsupsdn == NULL, so newsrdn is not */
            slapi_log_error(SLAPI_LOG_BACKLDBM, ENTRYRDN_TAG,
                        "entryrdn_rename_subtree: Renaming suffix %s to %s\n",
                        nrdn, slapi_rdn_get_nrdn((Slapi_RDN *)mynewsrdn));
        }
    }

    /* Open the entryrdn index */
    rc = _entryrdn_open_index(be, &ai, &db);
    if (rc || (NULL == db)) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "entryrdn_rename_subtree: Opening the index failed: "
                        "%s(%d)\n",
                        rc<0?dblayer_strerror(rc):"Invalid parameter", rc);
        db = NULL;
        return rc;
    }

    /* Make a cursor */
    rc = db->cursor(db, db_txn, &cursor, 0);
    if (rc) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                "entryrdn_rename_subtree: Failed to make a cursor: %s(%d)\n",
                dblayer_strerror(rc), rc);
        cursor = NULL;
        goto bail;
    }

    /* prepare the element for the newly renamed rdn, if any. */
    if (mynewsrdn) {
        newelem = _entryrdn_new_rdn_elem(be, id, mynewsrdn, &newelemlen);
        if (NULL == newelem) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                            "entryrdn_rename_subtree: Failed to generate "
                            "a new elem: id: %d, rdn: %s\n", 
                            id, slapi_rdn_get_rdn(mynewsrdn));
            goto bail;
        }
    }

    /* Get the new superior elem, if any. */
    if (mynewsupsdn) {
        rc = slapi_rdn_init_all_sdn(&newsupsrdn, mynewsupsdn);
        if (rc < 0) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                            "entryrdn_rename_subtree: Failed to convert "
                            "new superior \"%s\" to Slapi_RDN\n", 
                            slapi_sdn_get_dn(mynewsupsdn));
            rc = LDAP_INVALID_DN_SYNTAX;
            goto bail;
        } else if (rc > 0) {
            slapi_log_error(SLAPI_LOG_BACKLDBM, ENTRYRDN_TAG,
                            "entryrdn_rename_subtree: %s does not belong "
                            "to the db\n", slapi_sdn_get_dn(mynewsupsdn));
            rc = DB_NOTFOUND;
            goto bail;
        }

        rc = _entryrdn_index_read(be, cursor, &newsupsrdn, &newsupelem,
                                  NULL, NULL, 0/*flags*/, db_txn);
        if (rc) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                            "entryrdn_rename_subtree: Failed to read "
                            "the element of new superior \"%s\" (%d)\n", 
                            slapi_sdn_get_dn(mynewsupsdn), rc);
            goto bail;
        }
        newsupelemlen = _entryrdn_rdn_elem_size(newsupelem);
    }

    if (mynewsrdn) {
        rc = _entryrdn_index_read(be, cursor, &oldsrdn, &targetelem,
                                  &oldsupelem, &childelems, 0/*flags*/, db_txn);
    } else {
        rc = _entryrdn_index_read(be, cursor, &oldsrdn, &targetelem,
                                  &oldsupelem, NULL, 0/*flags*/, db_txn);
    }
    if (rc || NULL == targetelem) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                            "entryrdn_rename_subtree: Failed to read "
                            "the target element \"%s\" (%d)\n", 
                            slapi_sdn_get_dn(oldsdn), rc);
        goto bail;
    }
    targetid = id_stored_to_internal(targetelem->rdn_elem_id);
    targetelemlen = _entryrdn_rdn_elem_size(targetelem);
    if (oldsupelem) {
        oldsupelemlen = _entryrdn_rdn_elem_size(oldsupelem);
    }

    /* 1) rename targetelem */
    /* 2) update targetelem's child link, if renaming the target */
    if (mynewsrdn) {
        /* remove the old elem; (1) rename targetelem */
        keybuf = slapi_ch_smprintf("%u", targetid);
        key.data = keybuf;
        key.size = key.ulen = strlen(keybuf) + 1;
        key.flags = DB_DBT_USERMEM;    

        memset(&renamedata, 0, sizeof(renamedata));
        renamedata.ulen = renamedata.size = targetelemlen;
        renamedata.data = (void *)targetelem;
        renamedata.flags = DB_DBT_USERMEM;
        rc = _entryrdn_del_data(cursor, &key, &renamedata);
        if (rc) {
            goto bail;
        }
        if (childelems) {
            slapi_ch_free_string(&keybuf);
            keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, targetid);
            key.data = keybuf;
            key.size = key.ulen = strlen(keybuf) + 1;
            key.flags = DB_DBT_USERMEM;    
            /* remove the old elem; (2) update targetelem's child link */
            for (cep = childelems; cep && *cep; cep++) {
                memset(&renamedata, 0, sizeof(renamedata));
                renamedata.ulen = renamedata.size =
                                  _entryrdn_rdn_elem_size(*cep);
                renamedata.data = (void *)(*cep);
                renamedata.flags = DB_DBT_USERMEM;
                rc = _entryrdn_del_data(cursor, &key, &renamedata);
                if (rc) {
                    goto bail;
                }
            }
        }

        /* add the new elem */
        slapi_ch_free_string(&keybuf);
        keybuf = slapi_ch_smprintf("%u", id);
        key.data = keybuf;
        key.size = key.ulen = strlen(keybuf) + 1;
        key.flags = DB_DBT_USERMEM;    

        memset(&renamedata, 0, sizeof(renamedata));
        renamedata.ulen = renamedata.size = newelemlen;
        renamedata.data = (void *)newelem;
        renamedata.flags = DB_DBT_USERMEM;
        rc = _entryrdn_put_data(cursor, &key, &renamedata, RDN_INDEX_SELF);
        if (rc) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                                "entryrdn_rename_subtree: Adding %s failed; "
                                "%s(%d)\n", keybuf, dblayer_strerror(rc), rc);
            goto bail;
        }
        if (childelems) {
            slapi_ch_free_string(&keybuf);
            keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, id);
            key.data = keybuf;
            key.size = key.ulen = strlen(keybuf) + 1;
            key.flags = DB_DBT_USERMEM;    
            /* add the new elem; (2) update targetelem's child link */
            for (cep = childelems; cep && *cep; cep++) {
                memset(&renamedata, 0, sizeof(renamedata));
                renamedata.ulen = renamedata.size =
                                  _entryrdn_rdn_elem_size(*cep);
                renamedata.data = (void *)(*cep);
                renamedata.flags = DB_DBT_USERMEM;
                rc = _entryrdn_put_data(cursor, &key,
                                        &renamedata, RDN_INDEX_CHILD);
                if (rc) {
                    goto bail;
                }
            }
        }
    }
    /* 3) update targetelem's parent link, if any */
    if (oldsupelem) {
        slapi_ch_free_string(&keybuf);
        keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_PARENT, targetid);
        key.data = keybuf;
        key.size = key.ulen = strlen(keybuf) + 1;
        key.flags = DB_DBT_USERMEM;    

        memset(&renamedata, 0, sizeof(renamedata));
        renamedata.ulen = renamedata.size = oldsupelemlen;
        renamedata.data = (void *)oldsupelem;
        renamedata.flags = DB_DBT_USERMEM;
        rc = _entryrdn_del_data(cursor, &key, &renamedata);
        if (rc) {
            goto bail;
        }

        /* add the new elem */
        if (mynewsrdn) {
            slapi_ch_free_string(&keybuf);
            key.flags = DB_DBT_USERMEM;    
            keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_PARENT, id);
            key.data = keybuf;
            key.size = key.ulen = strlen(keybuf) + 1;

            memset(&renamedata, 0, sizeof(renamedata));
            renamedata.flags = DB_DBT_USERMEM;
            if (mynewsupsdn) {
                renamedata.ulen = renamedata.size = newsupelemlen;
                renamedata.data = (void *)newsupelem;
            } else {
                renamedata.ulen = renamedata.size = oldsupelemlen;
                renamedata.data = (void *)oldsupelem;
            }
        } else {
            if (mynewsupsdn) {
                renamedata.ulen = renamedata.size = newsupelemlen;
                renamedata.data = (void *)newsupelem;
            } else {
                /* never comes here */
                rc = -1;
                goto bail;
            }
        }
        rc = _entryrdn_put_data(cursor, &key, &renamedata, RDN_INDEX_PARENT);
        if (rc) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                                             "entryrdn_rename_subtree: Adding "
                                             "%s failed; %s(%d)\n",
                                             keybuf, dblayer_strerror(rc), rc);
            goto bail;
        }
    }

    /* 4) update targetelem's children's parent link, if renaming the target */
    if (mynewsrdn) {
        for (cep = childelems; cep && *cep; cep++) {
            /* remove the old elem */
            slapi_ch_free_string(&keybuf);
            keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_PARENT,
                                    id_stored_to_internal((*cep)->rdn_elem_id));
            key.data = keybuf;
            key.size = key.ulen = strlen(keybuf) + 1;
            key.flags = DB_DBT_USERMEM;    
    
            memset(&renamedata, 0, sizeof(renamedata));
            renamedata.ulen = renamedata.size = targetelemlen;
            renamedata.data = (void *)targetelem;
            renamedata.flags = DB_DBT_USERMEM;
            rc = _entryrdn_del_data(cursor, &key, &renamedata);
            if (rc) {
                goto bail;
            }
    
            /* add the new elem */
            memset(&renamedata, 0, sizeof(renamedata));
            renamedata.ulen = renamedata.size = newelemlen;
            renamedata.data = (void *)newelem;
            renamedata.flags = DB_DBT_USERMEM;
            rc = _entryrdn_put_data(cursor, &key, &renamedata, RDN_INDEX_SELF);
            if (rc) {
                slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                                  "entryrdn_rename_subtree: Adding %s failed; "
                                  "%s(%d)\n", keybuf, dblayer_strerror(rc), rc);
                goto bail;
            }
        }
    }

    /* 5) update parentelem's child link (except renaming the suffix) */
    if (oldsupelem) {
        /* remove the old elem */
        slapi_ch_free_string(&keybuf);
        keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD,
                                id_stored_to_internal(oldsupelem->rdn_elem_id));
        key.data = keybuf;
        key.size = key.ulen = strlen(keybuf) + 1;
        key.flags = DB_DBT_USERMEM;    
    
        memset(&renamedata, 0, sizeof(renamedata));
        renamedata.ulen = renamedata.size = targetelemlen;
        renamedata.data = (void *)targetelem;
        renamedata.flags = DB_DBT_USERMEM;
        rc = _entryrdn_del_data(cursor, &key, &renamedata);
        if (rc) {
            goto bail;
        }

        /* add the new elem */
        if (mynewsupsdn) {
            slapi_ch_free_string(&keybuf);
            keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD,
                                id_stored_to_internal(newsupelem->rdn_elem_id));
            key.data = keybuf;
            key.size = key.ulen = strlen(keybuf) + 1;
            key.flags = DB_DBT_USERMEM;    

            memset(&renamedata, 0, sizeof(renamedata));
            renamedata.flags = DB_DBT_USERMEM;
            if (mynewsrdn) {
                renamedata.ulen = renamedata.size = newelemlen;
                renamedata.data = (void *)newelem;
            } else {
                renamedata.ulen = renamedata.size = targetelemlen;
                renamedata.data = (void *)targetelem;
            }
        } else {
            if (mynewsrdn) {
                memset(&renamedata, 0, sizeof(renamedata));
                renamedata.ulen = renamedata.size = newelemlen;
                renamedata.data = (void *)newelem;
                renamedata.flags = DB_DBT_USERMEM;
            } else {
                /* never comes here */
                rc = -1;
                goto bail;
            }
        }
        rc = _entryrdn_put_data(cursor, &key, &renamedata, RDN_INDEX_CHILD);
        if (rc) {
            goto bail;
        }
    }

bail:
    slapi_ch_free_string(&keybuf);
    slapi_ch_free((void **)&targetelem);
    slapi_ch_free((void **)&newelem);
    slapi_ch_free((void **)&newsupelem);
    slapi_ch_free((void **)&oldsupelem);
    slapi_rdn_done(&oldsrdn);
    slapi_rdn_done(&supsrdn);
    slapi_rdn_done(&newsupsrdn);
    if (childelems) {
        for (cep = childelems; *cep; cep++) {
            slapi_ch_free((void **)cep);
        }
        slapi_ch_free((void **)&childelems);
    }

    /* Close the cursor */
    if (cursor) {
        int myrc = cursor->c_close(cursor);
        if (0 != myrc) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                  "entryrdn_rename_subtree: Failed to close cursor: %s(%d)\n",
                  dblayer_strerror(rc), rc);
        }
    }
    if (db) {
        dblayer_release_index_file(be, ai, db);
    }
    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "<-- entryrdn_rename_subtree\n");
    return rc;
}

/*
 * Get the IDList of direct childen and indirect subordinates
 * OUTPUT: subordinates
 */
int
entryrdn_get_subordinates(backend *be,
                          const Slapi_DN *sdn,
                          ID id,
                          IDList **subordinates,
                          back_txn *txn)
{
    int rc = -1;
    struct attrinfo *ai = NULL;
    DB *db = NULL;
    DBC *cursor = NULL;
    DB_TXN *db_txn = (txn != NULL) ? txn->back_txn_txn : NULL;
    Slapi_RDN srdn = {0};
    const char *nrdn = NULL; /* normalized rdn */
    int rdnidx = -1;
    char *keybuf = NULL;
    rdn_elem *elem = NULL;
    rdn_elem **childelems = NULL;
    rdn_elem **cep = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "--> entryrdn_get_subordinates\n");

    if (NULL == be || NULL == sdn || 0 == id) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                "entryrdn_get_subordinates: Param error: Empty %s\n",
                NULL==be?"backend":NULL==sdn?"dn":0==id?"id":"unknown");
        goto bail;
    }
    if (subordinates) {
        *subordinates = NULL;
    } else {
        rc = 0;
        goto bail;
    }

    rc = slapi_rdn_init_all_sdn(&srdn, sdn);
    if (rc) {
        if (rc < 0) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                            "entryrdn_get_subordinates: Failed to convert "
                            "\"%s\" to Slapi_RDN\n", slapi_sdn_get_dn(sdn));
            rc = LDAP_INVALID_DN_SYNTAX;
        } else if (rc > 0) {
            slapi_log_error(SLAPI_LOG_BACKLDBM, ENTRYRDN_TAG,
                            "entryrdn_get_subordinates: %s does not belong to "
                            "the db\n", slapi_sdn_get_dn(sdn));
            rc = DB_NOTFOUND;
        }
        goto bail;
    }

    /* check the given dn/srdn */
    rdnidx = slapi_rdn_get_last_ext(&srdn, &nrdn, FLAG_ALL_NRDNS);
    if (rdnidx < 0 || NULL == nrdn) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "entryrdn_get_subordinates: Empty RDN\n");
        goto bail;
    } 

    /* Open the entryrdn index */
    rc = _entryrdn_open_index(be, &ai, &db);
    if (rc || (NULL == db)) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "entryrdn_get_subordinates: Opening the index failed: "
                        "%s(%d)\n",
                        rc<0?dblayer_strerror(rc):"Invalid parameter", rc);
        db = NULL;
        goto bail;
    }

    /* Make a cursor */
    rc = db->cursor(db, db_txn, &cursor, 0);
    if (rc) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                "entryrdn_get_subordinates: Failed to make a cursor: %s(%d)\n",
                dblayer_strerror(rc), rc);
        cursor = NULL;
        goto bail;
    }

    rc = _entryrdn_index_read(be, cursor, &srdn, &elem,
                              NULL, &childelems, 0/*flags*/, db_txn);

    for (cep = childelems; cep && *cep; cep++) {
        ID childid = id_stored_to_internal((*cep)->rdn_elem_id);
        /* set direct children to the idlist */
        rc = idl_append_extend(subordinates, childid);
        if (rc) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                            "entryrdn_get_subordinates: Appending %d to idl "
                            "for direct children failed (%d)\n", childid, rc);
            goto bail;
        }

        /* set indirect subordinates to the idlist */
        rc = _entryrdn_append_childidl(cursor, (*cep)->rdn_elem_nrdn_rdn,
                                       childid, subordinates);
        if (rc) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                            "entryrdn_get_subordinates: Appending %d to idl "
                            "for indirect children failed (%d)\n",
                            childid, rc);
            goto bail;
        }
    }
    
bail:
    if (rc && subordinates && *subordinates) {
        idl_free(*subordinates);
    }
    slapi_ch_free_string(&keybuf);
    slapi_ch_free((void **)&elem);
    slapi_rdn_done(&srdn);
    if (childelems) {
        for (cep = childelems; *cep; cep++) {
            slapi_ch_free((void **)cep);
        }
        slapi_ch_free((void **)&childelems);
    }

    /* Close the cursor */
    if (cursor) {
        int myrc = cursor->c_close(cursor);
        if (0 != myrc) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                  "entryrdn_get_subordinates: Failed to close cursor: %s(%d)\n",
                  dblayer_strerror(rc), rc);
        }
    }
    if (db) {
        dblayer_release_index_file(be, ai, db);
    }
    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "<-- entryrdn_get_subordinates\n");
    return rc;
}

/*
 * Input: (rdn, id)
 * Output: dn
 *
 * caller is responsible to release *dn
 */
int
entryrdn_lookup_dn(backend *be,
                   const char *rdn,
                   ID id,
                   char **dn,
                   back_txn *txn)
{
    int rc = -1;
    struct attrinfo *ai = NULL;
    DB *db = NULL;
    DBC *cursor = NULL;
    DB_TXN *db_txn = (txn != NULL) ? txn->back_txn_txn : NULL;
    DBT key, data;
    char *keybuf = NULL;
    Slapi_RDN *srdn = NULL;
    char *orignrdn = NULL;
    char *nrdn = NULL;
    size_t nrdn_len = 0;
    ID workid = id; /* starting from the given id */
    rdn_elem *elem = NULL;
    int maybesuffix = 0;

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "--> entryrdn_lookup_dn\n");

    if (NULL == be || NULL == rdn || 0 == id || NULL == dn) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                    "entryrdn_lookup_dn: Param error: Empty %s\n",
                    NULL==be?"backend":NULL==rdn?"rdn":0==id?"id":
                    NULL==dn?"dn container":"unknown");
        return rc;
    }

    *dn = NULL;
    /* Open the entryrdn index */
    rc = _entryrdn_open_index(be, &ai, &db);
    if (rc || (NULL == db)) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "entryrdn_lookup_dn: Opening the index failed: "
                        "%s(%d)\n",
                        rc<0?dblayer_strerror(rc):"Invalid parameter", rc);
        return rc;
    }

    memset(&data, 0, sizeof(data));
    /* Make a cursor */
    rc = db->cursor(db, db_txn, &cursor, 0);
    if (rc) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                "entryrdn_lookup_dn: Failed to make a cursor: %s(%d)\n",
                dblayer_strerror(rc), rc);
        cursor = NULL;
        goto bail;
    }
    srdn = slapi_rdn_new_all_dn(rdn);
    orignrdn = slapi_ch_strdup(rdn);
    rc = slapi_dn_normalize_case_ext(orignrdn, 0, &nrdn, &nrdn_len);
    if (rc < 0) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                "entryrdn_get_parent: Failed to normalize %s\n", rdn);
        goto bail;
    }
    if (rc == 0) { /* orignrdn is passed in */
        *(nrdn + nrdn_len) = '\0';
    } else {
        slapi_ch_free_string(&orignrdn);
    }

    /* Setting the bulk fetch buffer */
    data.flags = DB_DBT_MALLOC;

    do {
        /* Setting up a key for the node to get its parent */
        slapi_ch_free_string(&keybuf);
        keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_PARENT, workid);
        key.data = keybuf;
        key.size = key.ulen = strlen(keybuf) + 1;
        key.flags = DB_DBT_USERMEM;    
    
        /* Position cursor at the matching key */
retry_get0:
        rc = cursor->c_get(cursor, &key, &data, DB_SET);
        if (rc) {
            if (DB_LOCK_DEADLOCK == rc) {
                /* try again */
                goto retry_get0;
            } else if (DB_NOTFOUND == rc) { /* could be a suffix or
                                               note: no parent for suffix */
                slapi_ch_free_string(&keybuf);
                keybuf = slapi_ch_smprintf("%s", nrdn);
                key.data = keybuf;
                key.size = key.ulen = strlen(keybuf) + 1;
                key.flags = DB_DBT_USERMEM;    
retry_get1:
                rc = cursor->c_get(cursor, &key, &data, DB_SET);
                if (rc) {
                    if (DB_LOCK_DEADLOCK == rc) {
                        /* try again */
                        goto retry_get1;
                    } else if (DB_NOTFOUND != rc) {
                        _entryrdn_cursor_print_error("entryrdn_lookup_dn",
                                            key.data, data.size, data.ulen, rc);
                    }
                    goto bail;
                }
                maybesuffix = 1;
            } else {
                _entryrdn_cursor_print_error("entryrdn_lookup_dn",
                                            key.data, data.size, data.ulen, rc);
                goto bail;
            }
        }
    
        /* Iterate over the duplicates to get the direct child's ID */
        workid = 0;
        if (maybesuffix) {
            /* it is a suffix, indeed.  done. */
            /* generate sdn to return */
            slapi_rdn_get_dn(srdn, dn);
            rc = 0;
            goto bail;
        }
        /* found a parent (there should be just one parent :) */
        elem = (rdn_elem *)data.data;
#ifdef LDAP_DEBUG_ENTRYRDN
        _entryrdn_dump_rdn_elem(elem);
#endif
        slapi_ch_free_string(&nrdn);
        nrdn = slapi_ch_strdup(elem->rdn_elem_nrdn_rdn);
        workid = id_stored_to_internal(elem->rdn_elem_id);
        /* 1 is byref, and the dup'ed rdn is freed with srdn */
        slapi_rdn_add_rdn_to_all_rdns(srdn, slapi_ch_strdup(RDN_ADDR(elem)), 1);
        slapi_ch_free(&data.data);
    } while (workid);

    if (0 == workid) {
        rc = -1;
    }

bail:
    slapi_ch_free(&data.data);
    /* Close the cursor */
    if (cursor) {
        int myrc = cursor->c_close(cursor);
        if (0 != myrc) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                  "entryrdn_lookup_dn: Failed to close cursor: %s(%d)\n",
                  dblayer_strerror(myrc), myrc);
        }
    }
    /* it is guaranteed that db is not NULL. */
    dblayer_release_index_file(be, ai, db);
    slapi_rdn_free(&srdn);
    slapi_ch_free_string(&nrdn);
    slapi_ch_free_string(&keybuf);
    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "<-- entryrdn_lookup_dn\n");
    return rc;
}

/*
 * Input: (rdn, id)
 * Output: (prdn, pid)
 *
 * If Input is a suffix, the Output is also a suffix.
 * If the rc is DB_NOTFOUND, the index is empty.
 * caller is responsible to release *prdn
 */
int
entryrdn_get_parent(backend *be,
                    const char *rdn,
                    ID id,
                    char **prdn,
                    ID *pid,
                    back_txn *txn)
{
    int rc = -1;
    struct attrinfo *ai = NULL;
    DB *db = NULL;
    DBC *cursor = NULL;
    DB_TXN *db_txn = (txn != NULL) ? txn->back_txn_txn : NULL;
    DBT key, data;
    char *keybuf = NULL;
    char *orignrdn = NULL;
    char *nrdn = NULL;
    size_t nrdn_len = 0;
    rdn_elem *elem = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "--> entryrdn_get_parent\n");

    /* Initialize data */
    memset(&data, 0, sizeof(data));

    if (NULL == be || NULL == rdn || 0 == id || NULL == prdn || NULL == pid) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                    "entryrdn_get_parent: Param error: Empty %s\n",
                    NULL==be?"backend":NULL==rdn?"rdn":0==id?"id":
                    NULL==rdn?"rdn container":
                    NULL==pid?"pid":"unknown");
        return rc;
    }
    *prdn = NULL;
    *pid = 0;

    /* Open the entryrdn index */
    rc = _entryrdn_open_index(be, &ai, &db);
    if (rc || (NULL == db)) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "entryrdn_get_parent: Opening the index failed: "
                        "%s(%d)\n",
                        rc<0?dblayer_strerror(rc):"Invalid parameter", rc);
        return rc;
    }

    /* Make a cursor */
    rc = db->cursor(db, db_txn, &cursor, 0);
    if (rc) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                "entryrdn_get_parent: Failed to make a cursor: %s(%d)\n",
                dblayer_strerror(rc), rc);
        cursor = NULL;
        goto bail;
    }
    orignrdn = slapi_ch_strdup(rdn);
    rc = slapi_dn_normalize_case_ext(orignrdn, 0, &nrdn, &nrdn_len);
    if (rc < 0) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                "entryrdn_get_parent: Failed to normalize %s\n", rdn);
        goto bail;
    }
    if (rc == 0) { /* orignrdn is passed in */
        *(nrdn + nrdn_len) = '\0';
    } else {
        slapi_ch_free_string(&orignrdn);
    }

    data.flags = DB_DBT_MALLOC;

    /* Setting up a key for the node to get its parent */
    slapi_ch_free_string(&keybuf);
    keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_PARENT, id);
    key.data = keybuf;
    key.size = key.ulen = strlen(keybuf) + 1;
    key.flags = DB_DBT_USERMEM;    
    
    /* Position cursor at the matching key */
retry_get0:
    rc = cursor->c_get(cursor, &key, &data, DB_SET);
    if (rc) {
        if (DB_LOCK_DEADLOCK == rc) {
            /* try again */
            goto retry_get0;
        } else if (DB_NOTFOUND == rc) { /* could be a suffix
                                           note: no parent for suffix */
            slapi_ch_free_string(&keybuf);
            keybuf = slapi_ch_smprintf("%s", nrdn);
            key.data = keybuf;
            key.size = key.ulen = strlen(keybuf) + 1;
            key.flags = DB_DBT_USERMEM;    
retry_get1:
            rc = cursor->c_get(cursor, &key, &data, DB_SET);
            if (rc) {
                if (DB_LOCK_DEADLOCK == rc) {
                    /* try again */
                    goto retry_get1;
                } else if (DB_NOTFOUND != rc) {
                    _entryrdn_cursor_print_error("entryrdn_get_parent",
                                            key.data, data.size, data.ulen, rc);
                }
            }
        } else {
            _entryrdn_cursor_print_error("entryrdn_get_parent",
                                         key.data, data.size, data.ulen, rc);
        }
        goto bail;
    }
    
    elem = (rdn_elem *)data.data;
#ifdef LDAP_DEBUG_ENTRYRDN
    _entryrdn_dump_rdn_elem(elem);
#endif
    *pid = id_stored_to_internal(elem->rdn_elem_id);
    *prdn = slapi_ch_strdup(RDN_ADDR(elem));
bail:
    slapi_ch_free_string(&nrdn);
    slapi_ch_free_string(&keybuf);
    slapi_ch_free((void **)&data.data);
    /* Close the cursor */
    if (cursor) {
        int myrc = cursor->c_close(cursor);
        if (0 != myrc) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                  "entryrdn_get_parent: Failed to close cursor: %s(%d)\n",
                  dblayer_strerror(rc), rc);
        }
    }
    /* it is guaranteed that db is not NULL. */
    dblayer_release_index_file(be, ai, db);
    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "<-- entryrdn_get_parent\n");
    return rc;
}

/* helper functions */
/*
 * Input:
 *   id -- ID of the entry specified with srdn
 *   srdn -- should store the target entry's rdn
 * Output:
 *   Return value: new rdn_elem
 *   length -- length of the new rdn_elem
 */
static rdn_elem *
_entryrdn_new_rdn_elem(backend *be, 
                       ID id,
                       Slapi_RDN *srdn,
                       size_t *length)
{
    const char *rdn = NULL;
    const char *nrdn = NULL;
    size_t rdn_len = 0;
    size_t nrdn_len = 0;
    rdn_elem *re = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "--> _entryrdn_new_rdn_elem\n");
    if (NULL == srdn || NULL == be) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                                "_entryrdn_new_rdn_elem: Empty %s\n",
                                NULL==srdn?"RDN":NULL==be?"backend":"unknown");
        *length = 0;
        return NULL;
    }

    rdn = slapi_rdn_get_rdn(srdn);
    nrdn = slapi_rdn_get_nrdn(srdn);

    if (NULL == rdn || NULL == nrdn) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_new_rdn_elem: Empty rdn (%s) or "
                        "normalized rdn (%s)\n", rdn?rdn:"",
                        nrdn?nrdn:"");
        *length = 0;
        return NULL;
    }
    /* If necessary, encrypt this index key */
    rdn_len = strlen(rdn) + 1;
    nrdn_len = strlen(nrdn) + 1;
    *length = sizeof(rdn_elem) + rdn_len + nrdn_len;
    re = (rdn_elem *)slapi_ch_malloc(*length);
    id_internal_to_stored(id, re->rdn_elem_id);
    sizeushort_internal_to_stored(nrdn_len, re->rdn_elem_nrdn_len);
    sizeushort_internal_to_stored(rdn_len, re->rdn_elem_rdn_len);
    PL_strncpyz(re->rdn_elem_nrdn_rdn, nrdn, nrdn_len);
    PL_strncpyz(RDN_ADDR(re), rdn, rdn_len);

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "<-- _entryrdn_new_rdn_elem\n");
    return re;
}

static void
_entryrdn_dup_rdn_elem(const void *raw, rdn_elem **new)
{
    rdn_elem *orig = (rdn_elem *)raw;
    size_t elem_len = _entryrdn_rdn_elem_size(orig);
    *new = (rdn_elem *)slapi_ch_malloc(elem_len);
    memcpy(*new, raw, elem_len);
}

static size_t
_entryrdn_rdn_elem_size(rdn_elem *elem)
{
    size_t len = sizeof(rdn_elem);
    len += sizeushort_stored_to_internal(elem->rdn_elem_rdn_len) +
           sizeushort_stored_to_internal(elem->rdn_elem_nrdn_len);
    return len;
}

#ifdef LDAP_DEBUG_ENTRYRDN
static void
_entryrdn_dump_rdn_elem(rdn_elem *elem)
{
    if (NULL == elem) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG, "RDN ELEMENT: empty\n");
        return;
    }
    slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG, "RDN ELEMENT:\n");
    slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG, "    ID: %u\n",
                    id_stored_to_internal(elem->rdn_elem_id));
    slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG, "    RDN: \"%s\"\n",
                    RDN_ADDR(elem));
    slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG, "    RDN length: %u\n",
                    sizeushort_stored_to_internal(elem->rdn_elem_rdn_len));
    slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG, "    Normalized RDN: \"%s\"\n",
                    elem->rdn_elem_nrdn_rdn);
    slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG, "    Normalized RDN length: %u\n",
                    sizeushort_stored_to_internal(elem->rdn_elem_nrdn_len));
    return;
}
#endif

static int
_entryrdn_open_index(backend *be, struct attrinfo **ai, DB **dbp)
{
    int rc = -1;
    ldbm_instance *inst = NULL;

    if (NULL == be || NULL == ai || NULL == dbp) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_open_index: Param error: Empty %s\n",
                        NULL==be?"be":NULL==ai?"attrinfo container":
                        NULL==dbp?"db container":"unknown");
        goto bail;
    }
    *ai = NULL;
    *dbp = NULL;
    /* Open the entryrdn index */
    ainfo_get(be, LDBM_ENTRYRDN_STR, ai);
    if (NULL == *ai) {
        rc = ENODATA;
        goto bail;
    }
    inst = (ldbm_instance *)be->be_instance_info;
    if ((*ai)->ai_attrcrypt && entryrdn_warning_on_encryption) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "Encrypting entryrdn is not supported.  "
                        "Ignoring the configuration entry \"dn: "
                        "cn=entryrdn, cn=encrypted attributes, cn=<backend>, "
                        "cn=%s, cn=plugins, cn=config\"\n",
                        inst->inst_li->li_plugin->plg_name);

        entryrdn_warning_on_encryption = 0;
    }
    rc = dblayer_get_index_file(be, *ai, dbp, DBOPEN_CREATE);
bail:
    return rc;
}

#if 0 /* not used */
/* 
 * We don't support attribute encryption for entryrdn.
 * Since there is no way to encrypt RDN in the main db id2entry,
 * encrypting/decrypting entryrdn does not add any benefit to the server.
 */ 
static berval *
_entryrdn_encrypt_key(backend *be, const char *key, struct attrinfo *ai)
{
    int rc = 0;
    struct berval val = {0};
    struct berval *encrypted_val = NULL;
    char *encrypted = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "--> _entryrdn_encrypt_key\n");

    if (NULL == key) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG, "Empty key\n");
        goto bail;
    }
    if (NULL == be || NULL == key || NULL == ai) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                    "_entryrdn_encrypt_key: Param error: Empty %s\n",
                    NULL==be?"be":NULL==key?"key":
                    NULL==ai?"attrinfo":"unknown");
        goto bail;
    }
    val.bv_val = (void *)key;
    val.bv_len = strlen(key);
    rc = attrcrypt_encrypt_index_key(be, ai, &val, &encrypted_val);
    if (NULL == encrypted_val) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "Failed to encrypt index key for %s\n", key);
    }
bail:
    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "<-- _entryrdn_encrypt_key\n");
    return encrypted_val;
}

static char *
_entryrdn_decrypt_key(backend *be, const char *key, struct attrinfo *ai)
{
    int rc = 0;
    struct berval val = {0};
    struct berval *decrypted_val = NULL;
    char *decrypted = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "--> _entryrdn_decrypt_key\n");

    if (NULL == key) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG, "Empty key\n");
        goto bail;
    }
    if (NULL == be || NULL == key || NULL == ai) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                    "_entryrdn_encrypt_key: Param error: Empty %s\n",
                    NULL==be?"be":NULL==key?"key":
                    NULL==ai?"attrinfo":"unknown");
        goto bail;
    }
    val.bv_val = (void *)key;
    val.bv_len = strlen(key);
    rc = attrcrypt_decrypt_index_key(be, ai, &val, &decrypted_val);
    if (decrypted_val) {
        /* null terminated string */
        decrypted = slapi_ch_strdup(decrypted_val->bv_val);
        ber_bvfree(decrypted_val);
        goto bail;
    }
    slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
            "Failed to decrypt index key for %s\n", key);

bail:
    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "<-- _detryrdn_encrypt_key\n");
    return decrypted;
}
#endif

/* Notes:
 * 1) data->data must be located in the data area (not in the stack).
 *    If c_get reallocate the memory, the given data is freed.
 * 2) output elem returns data->data regardless of the result (success|failure)
 */
static int
_entryrdn_get_elem(DBC *cursor,
                   DBT *key,
                   DBT *data,
                   const char *comp_key,
                   rdn_elem **elem)
{
    int rc = 0;
    void *ptr = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG, "--> _entryrdn_get_elem\n");
    if (NULL == cursor || NULL == key || NULL == data || NULL == elem ||
        NULL == comp_key) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                    "_entryrdn_get_elem: Param error: Empty %s\n",
                    NULL==cursor?"cursor":NULL==key?"key":
                    NULL==data?"data":NULL==elem?"elem container":
                    NULL==comp_key?"key to compare":"unknown");
        goto bail;
    }
    /* Position cursor at the matching key */
    ptr = data->data;
retry_get:
    rc = cursor->c_get(cursor, key, data, DB_GET_BOTH_RANGE);
    *elem = (rdn_elem *)data->data;
    if (rc) {
        if (DB_LOCK_DEADLOCK == rc) {
            /* try again */
            goto retry_get;
        } else if (DB_BUFFER_SMALL == rc) {
            /* try again */
            data->flags = DB_DBT_MALLOC;
            goto retry_get;
        } else if (DB_NOTFOUND != rc) {
            _entryrdn_cursor_print_error("_entryrdn_get_elem",
                                         key->data, data->size, data->ulen, rc);
        }
        goto bail;
    } 
    if (0 != strcmp(comp_key, (char *)(*elem)->rdn_elem_nrdn_rdn)) {
        /* the exact element was not found */
        if ((DB_DBT_MALLOC == data->flags) && (ptr != data->data)) {
            /* free the memory allocated in c_get when it returns an error */
            slapi_ch_free(&data->data);
            data->data = ptr;
            *elem = (rdn_elem *)data->data;
        }
        rc = DB_NOTFOUND;
        goto bail;
    }
    if ((0 == rc) && (DB_DBT_MALLOC == data->flags) && (ptr != data->data)) {
        /* the given data->data has been replaced by c_get */
        slapi_ch_free(&ptr);
    }
bail:
    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG, "<-- _entryrdn_get_elem\n");
    return rc;
}

static int
_entryrdn_get_tombstone_elem(DBC *cursor,
                             Slapi_RDN *srdn,
                             DBT *key,
                             const char *comp_key,
                             rdn_elem **elem)
{
    int rc = 0;
    DBT data;
    rdn_elem *childelem = NULL;
    char buffer[RDN_BULK_FETCH_BUFFER_SIZE]; 

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG, 
                    "--> _entryrdn_get_tombstone_elem\n");
    if (NULL == cursor || NULL == srdn || NULL == key || NULL == elem ||
        NULL == comp_key) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                    "_entryrdn_get_tombstone_elem: Param error: Empty %s\n",
                    NULL==cursor?"cursor":NULL==key?"key":
                    NULL==srdn?"srdn":NULL==elem?"elem container":
                    NULL==comp_key?"key to compare":"unknown");
        goto bail;
    }
    *elem = NULL;

    /* get the child elems */
    /* Setting the bulk fetch buffer */
    memset(&data, 0, sizeof(data));
    data.ulen = sizeof(buffer);
    data.size = sizeof(buffer);
    data.data = buffer;
    data.flags = DB_DBT_USERMEM;

retry_get0:
    rc = cursor->c_get(cursor, key, &data, DB_SET|DB_MULTIPLE);
    if (DB_LOCK_DEADLOCK == rc) {
        /* try again */
        goto retry_get0;
    } else if (DB_NOTFOUND == rc) {
        rc = 0; /* Child not found is ok */
        goto bail;
    } else if (rc) {
        _entryrdn_cursor_print_error("_entryrdn_get_tombstone_elem",
                                     key->data, data.size, data.ulen, rc);
        goto bail;
    }
        
    do {
        DBT dataret;
        void *ptr;
        char *childnrdn = NULL;
        char *comma = NULL;

        DB_MULTIPLE_INIT(ptr, &data);
        do {
            memset(&dataret, 0, sizeof(dataret));
            DB_MULTIPLE_NEXT(ptr, &data, dataret.data, dataret.size);
            if (NULL == dataret.data || NULL == ptr) {
                break;
            }
            childelem = (rdn_elem *)dataret.data;
            childnrdn = (char *)childelem->rdn_elem_nrdn_rdn;
            comma = strchr(childnrdn, ',');
            if (NULL == comma) { /* No comma; This node is not a tombstone */
                continue;
            }
            if (strncasecmp(childnrdn, SLAPI_ATTR_UNIQUEID,
                            sizeof(SLAPI_ATTR_UNIQUEID) - 1)) {
                /* Does not start w/ UNIQUEID; not a tombstone */
                continue;
            }
            if (0 == strcmp(comma + 1, slapi_rdn_get_nrdn(srdn))) {
                /* found and done */
                _entryrdn_dup_rdn_elem((const void *)dataret.data, elem);
                goto bail;
            }
            if (0 == strncmp(childnrdn, slapi_rdn_get_nrdn(srdn),
                             comma - childnrdn)) {
                /* found and done */
                _entryrdn_dup_rdn_elem((const void *)dataret.data, elem);
                goto bail;
            }
        } while (NULL != dataret.data && NULL != ptr);
retry_get1:
        rc = cursor->c_get(cursor, key, &data, DB_NEXT_DUP|DB_MULTIPLE);
        if (DB_LOCK_DEADLOCK == rc) {
            /* try again */
            goto retry_get1;
        } else if (DB_NOTFOUND == rc) {
            rc = 0;
            goto bail; /* done */
        } else if (rc) {
            _entryrdn_cursor_print_error("_entryrdn_get_tombstone_elem",
                                         key->data, data.size, data.ulen, rc);
            goto bail;
        }
    } while (0 == rc);

bail:
    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                    "<-- _entryrdn_get_tombstone_elem\n");
    return rc;
}

static int
_entryrdn_put_data(DBC *cursor, DBT *key, DBT *data, char type)
{
    int rc = -1;

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "--> _entryrdn_put_data\n");
    if (NULL == cursor || NULL == key || NULL == data) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                    "_entryrdn_put_data: Param error: Empty %s\n",
                    NULL==cursor?"cursor":NULL==key?"key":
                    NULL==data?"data":"unknown");
        goto bail;
    }
    /* insert it */
    rc = cursor->c_put(cursor, key, data, DB_NODUPDATA);
    if (rc) {
        if (DB_KEYEXIST == rc) {
            /* this is okay */
            slapi_log_error(SLAPI_LOG_BACKLDBM, ENTRYRDN_TAG,
                            "_entryrdn_put_data: The same key (%s) and the "
                            "data exists in index\n",
                            (char *)key->data);
        } else {
            char *keyword = NULL;
            if (type == RDN_INDEX_CHILD) {
                keyword = "child";
            } else if (type == RDN_INDEX_PARENT) {
                keyword = "parent";
            } else {
                keyword = "self";
            }
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                            "_entryrdn_put_data: Adding the %s link (%s) "
                            "failed: %s (%d)\n", keyword, (char *)key->data,
                            dblayer_strerror(rc), rc);
        }
    }
bail:
    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG, "<-- _entryrdn_put_data\n");
    return rc;
}

static int
_entryrdn_del_data(DBC *cursor,  DBT *key, DBT *data)
{
    int rc = -1;

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "--> _entryrdn_del_data\n");
    if (NULL == cursor || NULL == key || NULL == data) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                    "_entryrdn_del_data: Param error: Empty %s\n",
                    NULL==cursor?"cursor":NULL==key?"key":
                    NULL==data?"data":"unknown");
        goto bail;
    }
retry_get:
    rc = cursor->c_get(cursor, key, data, DB_GET_BOTH);
    if (rc) {
        if (DB_LOCK_DEADLOCK == rc) {
            /* try again */
            goto retry_get;
        } else if (DB_NOTFOUND == rc) {
            rc = 0; /* not found is ok */
        } else {
            _entryrdn_cursor_print_error("_entryrdn_del_data",
                                         key->data, data->size, data->ulen, rc);
        }
    } else {
        /* We found it, so delete it */
        rc = cursor->c_del(cursor, 0);
        if (rc) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                                "_entryrdn_del_data: Deleting %s failed; "
                                "%s(%d)\n", (char *)key->data,
                                dblayer_strerror(rc), rc);
        }
    }
bail:
    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "<-- _entryrdn_del_data\n");
    return rc;
}

/* Child is a Leaf RDN to be added */
static int
_entryrdn_insert_key_elems(backend *be,
                     DBC *cursor,
                     Slapi_RDN *srdn,
                     DBT *key,
                     rdn_elem *parentelem,
                     rdn_elem *elem,
                     size_t elemlen,
                     DB_TXN *db_txn)
{
    /* We found a place to add RDN. */
    DBT adddata;
    char *keybuf = NULL;
    size_t len = 0;
    int rc = 0;
    ID myid = 0;

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "--> _entryrdn_insert_key_elems\n");

    if (NULL == be || NULL == cursor || NULL == srdn ||
        NULL == key || NULL == parentelem || NULL == elem) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                    "_entryrdn_insert_key_elem: Param error: Empty %s\n",
                    NULL==be?"backend":NULL==cursor?"cursor":NULL==srdn?"RDN":
                    NULL==key?"key":NULL==parentelem?"parent element":
                    NULL==elem?"target element":"unknown");
        goto bail;
    }
#ifdef LDAP_DEBUG_ENTRYRDN
    _entryrdn_dump_rdn_elem(elem);
#endif
    memset(&adddata, 0, sizeof(adddata));
    adddata.ulen = adddata.size = elemlen;
    adddata.data = (void *)elem;
    adddata.flags = DB_DBT_USERMEM;

    /* adding RDN to the child key */
    rc = _entryrdn_put_data(cursor, key, &adddata, RDN_INDEX_CHILD);
    keybuf = key->data;
    if (rc) { /* failed */
        goto bail;
    }

    myid = id_stored_to_internal(elem->rdn_elem_id);

    /* adding RDN to the self key */
    slapi_ch_free_string(&keybuf);
    /* Generate a key for self rdn */
    /* E.g., 222 */
    keybuf = slapi_ch_smprintf("%u", myid);
    key->data = keybuf;
    key->size = key->ulen = strlen(keybuf) + 1;
    key->flags = DB_DBT_USERMEM;    

    rc = _entryrdn_put_data(cursor, key, &adddata, RDN_INDEX_SELF);
    if (rc) { /* failed */
        goto bail;
    }

    /* adding RDN to the parent key */
    slapi_ch_free_string(&keybuf);
    /* Generate a key for parent rdn */
    /* E.g., P222 */
    keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_PARENT, myid);
    key->data = keybuf;
    key->size = key->ulen = strlen(keybuf) + 1;
    key->flags = DB_DBT_USERMEM;    

    memset(&adddata, 0, sizeof(adddata));
    len = _entryrdn_rdn_elem_size(parentelem);
    adddata.ulen = adddata.size = len;
    adddata.data = (void *)parentelem;
    adddata.flags = DB_DBT_USERMEM;
    /* adding RDN to the self key */
    rc = _entryrdn_put_data(cursor, key, &adddata, RDN_INDEX_PARENT);
    /* Succeeded or failed, it's done. */
bail:
    slapi_ch_free_string(&keybuf);
    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "<-- _entryrdn_insert_key_elems\n");
    return rc;
}

/*
 * Helper function to replace a temporary id assigned to suffix id.
 */
static int
_entryrdn_replace_suffix_id(DBC *cursor, DBT *key, DBT *adddata,
                            ID id, const char *normsuffix)
{
    int rc = 0;
    char *keybuf = NULL;
    char *realkeybuf = NULL;
    DBT realkey;
    char buffer[RDN_BULK_FETCH_BUFFER_SIZE]; 
    DBT data;
    DBT moddata;
    rdn_elem **childelems = NULL;
    rdn_elem **cep = NULL;
    rdn_elem *childelem = NULL;
    size_t childnum = 4;
    size_t curr_childnum = 0;

    /* temporary id added for the non exisiting suffix */
    /* Let's replace it with the real entry ID */
    /* SELF */
    rc = cursor->c_put(cursor, key, adddata, DB_CURRENT);
    if (rc) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_replace_suffix_id: Adding suffix %s failed: "
                        "%s (%d)\n", normsuffix, dblayer_strerror(rc), rc);
        goto bail;
    }

    /*
     * Fixing Child link:
     * key: C0:Suffix --> C<realID>:Suffix
     */
    /* E.g., C1 */
    keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, TMPID);
    key->data = keybuf;
    key->size = key->ulen = strlen(keybuf) + 1;
    key->flags = DB_DBT_USERMEM;    

    /* Setting the bulk fetch buffer */
    memset(&data, 0, sizeof(data));
    data.ulen = sizeof(buffer);
    data.size = sizeof(buffer);
    data.data = buffer;
    data.flags = DB_DBT_USERMEM;

    realkeybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, id);
    realkey.data = realkeybuf;
    realkey.size = realkey.ulen = strlen(realkeybuf) + 1;
    realkey.flags = DB_DBT_USERMEM;    

    memset(&moddata, 0, sizeof(moddata));
    moddata.flags = DB_DBT_USERMEM;
retry_get0:
    rc = cursor->c_get(cursor, key, &data, DB_SET|DB_MULTIPLE);
    if (DB_LOCK_DEADLOCK == rc) {
        /* try again */
        goto retry_get0;
    } else if (DB_NOTFOUND == rc) {
        _entryrdn_cursor_print_error("_entryrdn_replace_suffix_id",
                                     key->data, data.size, data.ulen, rc);
        goto bail;
    } else if (rc) {
        _entryrdn_cursor_print_error("_entryrdn_replace_suffix_id",
                                     key->data, data.size, data.ulen, rc);
        goto bail;
    }
    childelems = (rdn_elem **)slapi_ch_calloc(childnum, sizeof(rdn_elem *));
    do {
        DBT dataret;
        void *ptr;
        DB_MULTIPLE_INIT(ptr, &data);
        do {
            memset(&dataret, 0, sizeof(dataret));
            DB_MULTIPLE_NEXT(ptr, &data, dataret.data, dataret.size);
            if (NULL == dataret.data || NULL == ptr) {
                break;
            }
            _entryrdn_dup_rdn_elem((const void *)dataret.data, &childelem);
            moddata.data = childelem;
            moddata.ulen = moddata.size = _entryrdn_rdn_elem_size(childelem);
            /* Delete it first */
            rc = _entryrdn_del_data(cursor, key, &moddata);
            if (rc) {
                goto bail0;
            }
            /* Add it back */
            rc = _entryrdn_put_data(cursor, &realkey, &moddata, 
                                                RDN_INDEX_CHILD);
            if (curr_childnum + 1 == childnum) {
                childnum *= 2;
                childelems = (rdn_elem **)slapi_ch_realloc((char *)childelems,
                                                sizeof(rdn_elem *) * childnum);
                memset(childelems + curr_childnum, 0,
                       sizeof(rdn_elem *) * (childnum - curr_childnum));
            }
            childelems[curr_childnum++] = childelem;
            /* We don't access the address with this variable any more */
            childelem = NULL;
        } while (NULL != dataret.data && NULL != ptr);
retry_get1:
        rc = cursor->c_get(cursor, key, &data, DB_NEXT_DUP|DB_MULTIPLE);
        if (DB_LOCK_DEADLOCK == rc) {
            /* try again */
            goto retry_get1;
        } else if (DB_NOTFOUND == rc) {
            rc = 0;
            break; /* done */
        } else if (rc) {
            _entryrdn_cursor_print_error("_entryrdn_replace_suffix_id",
                                         key->data, data.size, data.ulen, rc);
            goto bail0;
        }
    } while (0 == rc);

    /*
     * Fixing Children's parent link:
     * key:  P<childID>:<childRDN>  --> P<childID>:<childRDN>
     * data: 0                      --> <realID>
     */
    for (cep = childelems; cep && *cep; cep++) {
        rdn_elem *pelem = NULL;
        slapi_ch_free_string(&keybuf);
        /* E.g., P1 */
        keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_PARENT, 
                                   id_stored_to_internal((*cep)->rdn_elem_id));
        key->data = keybuf;
        key->size = key->ulen = strlen(keybuf) + 1;
        key->flags = DB_DBT_USERMEM;    

        memset(&moddata, 0, sizeof(moddata));
        moddata.flags = DB_DBT_MALLOC;

        /* Position cursor at the matching key */
retry_get2:
        rc = cursor->c_get(cursor, key, &moddata, DB_SET);
        if (rc) {
            if (DB_LOCK_DEADLOCK == rc) {
                /* try again */
                goto retry_get2;
            } else if (rc) {
                _entryrdn_cursor_print_error("_entryrdn_replace_suffix_id",
                                           key->data, data.size, data.ulen, rc);
                goto bail0;
            }
        }
        pelem = (rdn_elem *)moddata.data;
        if (TMPID == id_stored_to_internal(pelem->rdn_elem_id)) {
            /* the parent id is TMPID;
             * replace it with the given id */
            id_internal_to_stored(id, pelem->rdn_elem_id);
            rc = cursor->c_put(cursor, key, &moddata, DB_CURRENT);
            if (rc) {
                slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                                "_entryrdn_replace_suffix_id: "
                                "Fixing the parent link (%s) failed: %s (%d)\n",
                                keybuf, dblayer_strerror(rc), rc);
                goto bail0;
            }
        }
        slapi_ch_free((void **)&moddata.data);
    } /* for (cep = childelems; cep && *cep; cep++) */
bail0:
    for (cep = childelems; cep && *cep; cep++) {
        slapi_ch_free((void **)cep);
    }
    slapi_ch_free((void **)&childelems);
bail:
    slapi_ch_free_string(&keybuf);
    slapi_ch_free_string(&realkeybuf);
    return rc;
}

/*
 * This function starts from the suffix following the child links to the bottom.
 * If the target leaf node does not exist, the nodes (the child link of the 
 * parent node and the self link) are added.
 */
static int
_entryrdn_insert_key(backend *be,
                     DBC *cursor,
                     Slapi_RDN *srdn,
                     ID id, 
                     DB_TXN *db_txn)
{
    int rc = -1;
    size_t len = 0;
    const char *nrdn = NULL; /* normalized rdn */
    const char *childnrdn = NULL; /* normalized child rdn */
    int rdnidx = -1;
    char *keybuf = NULL;
    DBT key, data;
    ID workid = 0;
    rdn_elem *elem = NULL;
    rdn_elem *childelem = NULL;
    rdn_elem *parentelem = NULL;
    rdn_elem *tmpelem = NULL;
    Slapi_RDN *tmpsrdn = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "--> _entryrdn_insert_key\n");

    if (NULL == be || NULL == cursor || NULL == srdn || 0 == id) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                    "_entryrdn_insert_key: Param error: Empty %s\n",
                    NULL==be?"backend":NULL==cursor?"cursor":NULL==srdn?"RDN":
                    0==id?"id":"unknown");
        goto bail;
    }

    /* get the top normalized rdn */
    rdnidx = slapi_rdn_get_last_ext(srdn, &nrdn, FLAG_ALL_NRDNS);
    if (rdnidx < 0 || NULL == nrdn) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_insert_key: Empty RDN\n");
        goto bail;
    }

    /* Setting up a key for suffix */
    key.data = (void *)nrdn;
    key.size = key.ulen = strlen(nrdn) + 1;
    key.flags = DB_DBT_USERMEM;    

    if (0 == rdnidx) { /* "0 == rdnidx" means adding suffix */
        /* adding suffix RDN to the self key */
        DBT adddata;
        elem = _entryrdn_new_rdn_elem(be, id, srdn, &len);
        if (NULL == elem) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                           "_entryrdn_insert_key: Failed to generate an elem: "
                           "id: %d, rdn: %s\n", 
                           id, slapi_rdn_get_rdn(srdn));
            goto bail;
        }
#ifdef LDAP_DEBUG_ENTRYRDN
        _entryrdn_dump_rdn_elem(elem);
#endif

        memset(&adddata, 0, sizeof(adddata));
        adddata.ulen = adddata.size = len;
        adddata.data = (void *)elem;
        adddata.flags = DB_DBT_USERMEM;

        rc = _entryrdn_put_data(cursor, &key, &adddata, RDN_INDEX_SELF);
        if (DB_KEYEXIST == rc) {
            DBT existdata;
            rdn_elem *existelem = NULL;
            ID tmpid;
            memset(&existdata, 0, sizeof(existdata));
            existdata.flags = DB_DBT_MALLOC;
            rc = cursor->c_get(cursor, &key, &existdata, DB_SET);
            if (rc) {
                slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                                "_entryrdn_insert_key: Get existing suffix %s "
                                "failed: %s (%d)\n",
                                nrdn, dblayer_strerror(rc), rc);
                goto bail;
            }
            existelem = (rdn_elem *)existdata.data;
            tmpid = id_stored_to_internal(existelem->rdn_elem_id);
            slapi_ch_free((void **)&existelem);
            if (TMPID == tmpid) {
                rc = _entryrdn_replace_suffix_id(cursor, &key, &adddata, 
                                                 id, nrdn);
                if (rc) {
                    goto bail;
                }
            } /* if (TMPID == tmpid) */
            rc = 0;
        } /* if (DB_KEYEXIST == rc) */
        slapi_log_error(SLAPI_LOG_BACKLDBM, ENTRYRDN_TAG,
                        "_entryrdn_insert_key: Suffix %s added: %d\n", 
                        nrdn, rc);
        goto bail; /* succeeded or failed, it's done */
    }

    /* (0 < rdnidx) */
    /* get id of the suffix */
    tmpsrdn = NULL;
    /* tmpsrdn == suffix'es srdn */
    rc = slapi_rdn_partial_dup(srdn, &tmpsrdn, rdnidx);
    if (rc) {
        char *dn  = NULL;
        slapi_rdn_get_dn(srdn, &dn);
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_insert_key: partial dup of %s (idx %d) "
                        "failed (%d)\n", dn, rdnidx, rc);
        slapi_ch_free_string(&dn);
        goto bail;
    }
    elem = _entryrdn_new_rdn_elem(be, 0 /*fake id*/, tmpsrdn, &len);
    if (NULL == elem) {
        char *dn  = NULL;
        slapi_rdn_get_dn(tmpsrdn, &dn);
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_insert_key: Failed to generate a new elem: "
                        "dn: %s\n", dn);
        slapi_ch_free_string(&dn);
        goto bail;
    }

    memset(&data, 0, sizeof(data));
    data.ulen = data.size = len;
    data.data = elem;
    data.flags = DB_DBT_USERMEM;

    /* getting the suffix element */
    rc = _entryrdn_get_elem(cursor, &key, &data, nrdn, &elem); 
    if (rc) {
        const char *myrdn = slapi_rdn_get_nrdn(srdn);
        const char *ep = NULL;
        int isexception = 0;
        /* Check the RDN is in the exception list */
        for (ep = *rdn_exceptions; ep && *ep; ep++) {
            if (!strcmp(ep, myrdn)) {
                isexception = 1;
            }
        }

        if (isexception) {
            /* adding suffix RDN to the self key */
            DBT adddata;
            /* suffix ID = 0: fake ID to be replaced with the real one when
             * it's really added. */
            ID suffixid = TMPID;
            slapi_ch_free((void **)&elem);
            elem = _entryrdn_new_rdn_elem(be, suffixid, tmpsrdn, &len);
            if (NULL == elem) {
                slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                           "_entryrdn_insert_key: Failed to generate an elem: "
                           "id: %d, rdn: %s\n", 
                           suffixid, slapi_rdn_get_rdn(tmpsrdn));
                goto bail;
            }
#ifdef LDAP_DEBUG_ENTRYRDN
            _entryrdn_dump_rdn_elem(elem);
#endif
            memset(&adddata, 0, sizeof(adddata));
            adddata.ulen = adddata.size = len;
            adddata.data = (void *)elem;
            adddata.flags = DB_DBT_USERMEM;

            rc = _entryrdn_put_data(cursor, &key, &adddata, RDN_INDEX_SELF);
            slapi_log_error(SLAPI_LOG_BACKLDBM, ENTRYRDN_TAG,
                        "_entryrdn_insert_key: Suffix %s added: %d\n", 
                        slapi_rdn_get_rdn(tmpsrdn), rc);
        } else {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                            "_entryrdn_insert_key: Suffix \"%s\" not found: "
                            "%s(%d)\n", nrdn, dblayer_strerror(rc), rc);
            goto bail;
        }
    }
    slapi_rdn_free(&tmpsrdn);

    /* workid: ID of suffix */
    workid = id_stored_to_internal(elem->rdn_elem_id);
    parentelem = elem;
    elem = NULL;

    do {
        slapi_ch_free_string(&keybuf);

        /* Check the direct child in the RDN array, first */
        rdnidx = slapi_rdn_get_prev_ext(srdn, rdnidx,
                                        &childnrdn, FLAG_ALL_NRDNS);
        if ((rdnidx < 0) || (NULL == childnrdn)) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                            "_entryrdn_insert_key: RDN list \"%s\" is broken: "
                            "idx(%d)\n", slapi_rdn_get_rdn(srdn), rdnidx);
            goto bail;
        }
        /* Generate a key for child tree */
        /* E.g., C1 */
        keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, workid);
        key.data = keybuf;
        key.size = key.ulen = strlen(keybuf) + 1;
        key.flags = DB_DBT_USERMEM;    

        tmpsrdn = srdn;
        if (0 < rdnidx) {
            rc = slapi_rdn_partial_dup(srdn, &tmpsrdn, rdnidx);
            if (rc) {
                char *dn  = NULL;
                slapi_rdn_get_dn(srdn, &dn);
                slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                                "_entryrdn_insert_key: partial dup of %s "
                                "(idx %d) failed (%d)\n", dn, rdnidx, rc);
                slapi_ch_free_string(&dn);
                goto bail;
            }
        }
        elem = _entryrdn_new_rdn_elem(be, 0 /*fake id*/, tmpsrdn, &len);
        if (NULL == elem) {
            char *dn  = NULL;
            slapi_rdn_get_dn(tmpsrdn, &dn);
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_insert_key: Failed to generate a new elem: "
                        "dn: %s\n", dn);
            slapi_ch_free_string(&dn);
            goto bail;
        }

        _entryrdn_dup_rdn_elem((const void *)elem, &tmpelem);
        memset(&data, 0, sizeof(data));
        data.ulen = data.size = len;
        data.data = tmpelem;
        data.flags = DB_DBT_USERMEM;
        /* getting the child element */

        rc = _entryrdn_get_elem(cursor, &key, &data, childnrdn, &tmpelem);
        if (rc) {
            slapi_ch_free((void **)&tmpelem);
            if (DB_NOTFOUND == rc) {
                /* if 0 == rdnidx, Child is a Leaf RDN to be added */
                if (0 == rdnidx) {
                    /* keybuf (C#) is consumed in _entryrdn_insert_key_elems */
                    /* set id to the elem to be added */
                    id_internal_to_stored(id, elem->rdn_elem_id);
                    rc = _entryrdn_insert_key_elems(be, cursor, srdn, &key,
                                                 parentelem, elem, len, db_txn);
                    keybuf = NULL;
                    goto bail;
                    /* done */
                } else {
                    ID currid = 0;
                    /*
                     * In DIT cn=A,ou=B,o=C, cn=A and ou=B are removed and
                     * turned to tombstone entries.  We need to support both:
                     *   nsuniqueid=...,cn=A,ou=B,o=C and
                     *   nsuniqueid=...,cn=A,nsuniqueid=...,ou=B,o=C
                     * The former appears when cn=A is deleted;
                     * the latter appears when the entryrdn is reindexed.
                     * The former is taken care in _entryrdn_get_tombstone_elem;
                     * the else clause to skip "nsuniqueid" is needed for the
                     * latter case.
                     */
                    rc = _entryrdn_get_tombstone_elem(cursor, tmpsrdn, &key,
                                                          childnrdn, &tmpelem);
                    if (rc) {
                        char *dn  = NULL;
                        slapi_rdn_get_dn(tmpsrdn, &dn);
                        if (DB_NOTFOUND == rc) {
                            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                                "_entryrdn_insert_key: Node \"%s\" not found: "
                                "%s(%d)\n", dn, dblayer_strerror(rc), rc);
                        } else {
                            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                                "_entryrdn_insert_key: Getting \"%s\" failed: "
                                "%s(%d)\n", dn, dblayer_strerror(rc), rc);
                        }
                        slapi_ch_free_string(&dn);
                        goto bail;
                    }
                    /* Node is a tombstone. */
                    if (tmpelem) {
                        currid = id_stored_to_internal(tmpelem->rdn_elem_id);
                        nrdn = childnrdn;
                        workid = currid;
                        slapi_ch_free((void **)&parentelem);
                        parentelem = tmpelem;
                        slapi_ch_free((void **)&elem);
                    }
                }
            } else {
                char *dn  = NULL;
                slapi_rdn_get_dn(tmpsrdn, &dn);
                slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                            "_entryrdn_insert_key: Suffix \"%s\" not found: "
                            "%s(%d)\n", nrdn, dblayer_strerror(rc), rc);
                slapi_ch_free_string(&dn);
                goto bail;
            }
        } else { /* rc == 0; succeeded to get an element */
            ID currid = 0;
            slapi_ch_free((void **)&elem);
            elem = tmpelem;
            currid = id_stored_to_internal(elem->rdn_elem_id);
            if (0 == rdnidx) { /* Child is a Leaf RDN to be added */
                if (currid == id) {
                    /* already in the file */
                    /* do nothing and return. */
                    rc = 0;
                    slapi_log_error(SLAPI_LOG_BACKLDBM, ENTRYRDN_TAG,
                                    "_entryrdn_insert_key: ID %d is already "
                                    "in the index. NOOP.\n", currid);
                } else { /* different id, error return */
                    char *dn  = NULL;
                    int tmprc = slapi_rdn_get_dn(srdn, &dn);
                    slapi_log_error(SLAPI_LOG_FATAL,
                                ENTRYRDN_TAG,
                                "_entryrdn_insert_key: Same DN (%s: %s) "
                                "is already in the %s file with different ID "
                                "%d.  Expected ID is %d.\n", 
                                tmprc?"rdn":"dn", tmprc?childnrdn:dn,
                                LDBM_ENTRYRDN_STR, currid, id);
                    slapi_ch_free_string(&dn);
                    /* returning special error code for the upgrade */
                    rc = LDBM_ERROR_FOUND_DUPDN;
                }
                goto bail;
            } else { /* if (0 != rdnidx) */
                nrdn = childnrdn;
                workid = currid;
                slapi_ch_free((void **)&parentelem);
                parentelem = elem;
                elem = NULL;
            }
        }
        if (tmpsrdn != srdn) {
            slapi_rdn_free(&tmpsrdn);
        }
    } while (rdnidx >= 0 && workid > 0);
bail:
    if (tmpsrdn != srdn) {
        slapi_rdn_free(&tmpsrdn);
    }
    slapi_ch_free_string(&keybuf);
    slapi_ch_free((void **)&elem);
    slapi_ch_free((void **)&parentelem);
    slapi_ch_free((void **)&childelem);
    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "<-- _entryrdn_insert_key\n");
    return rc;
}

/*
 * This function checks the existence of the target self link (key ID:RDN; 
 * value ID,RDN,normalized RDN). If it exists and it does not have child links,
 * then it deletes the parent's child link and the self link. 
 */
static int
_entryrdn_delete_key(backend *be,
                     DBC *cursor,
                     Slapi_RDN *srdn,
                     ID id, 
                     DB_TXN *db_txn)
{
    int rc = -1;
    size_t len = 0;
    const char *nrdn = NULL; /* normalized rdn */
    const char *suffix = NULL; /* normalized suffix */
    char *parentnrdn = NULL; /* normalized parent rdn */
    const char *selfnrdn = NULL; /* normalized parent rdn */
    int rdnidx = -1;
    int lastidx = -1;
    char *keybuf = NULL;
    DBT key, data;
    ID workid = 0;
    rdn_elem *elem = NULL;
    int issuffix = 0;
    Slapi_RDN *tmpsrdn = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "--> _entryrdn_delete_key\n");

    if (NULL == be || NULL == cursor || NULL == srdn || 0 == id) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                    "_entryrdn_delete_key: Param error: Empty %s\n",
                    NULL==be?"backend":NULL==cursor?"cursor":NULL==srdn?"RDN":
                    0==id?"ID":"unknown");
        goto bail;
    }

    /* get the bottom normalized rdn (target to delete) */
    rdnidx = slapi_rdn_get_first_ext(srdn, &nrdn, FLAG_ALL_NRDNS);
    /* rdnidx is supposed to be 0 */
    if (rdnidx < 0 || NULL == nrdn) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_delete_key: Empty RDN\n");
        goto bail;
    }
    lastidx = slapi_rdn_get_last_ext(srdn, &suffix, FLAG_ALL_NRDNS);
    if (0 == lastidx) {
        issuffix = 1;
        selfnrdn = suffix;
    } else if (lastidx < 0 || NULL == suffix) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_delete_key: Empty suffix\n");
        goto bail;
    }

    /* check if the target element has a child or not */
    keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, id);
    key.data = (void *)keybuf;
    key.size = key.ulen = strlen(nrdn) + 1;
    key.flags = DB_DBT_USERMEM;    

    memset(&data, 0, sizeof(data));
    data.flags = DB_DBT_MALLOC;

retry_get0:
    rc = cursor->c_get(cursor, &key, &data, DB_SET);
    if (rc) {
        if (DB_LOCK_DEADLOCK == rc) {
            /* try again */
            goto retry_get0;
        } else if (DB_NOTFOUND != rc) {
            _entryrdn_cursor_print_error("_entryrdn_delete_key",
                                         key.data, data.size, data.ulen, rc);
            goto bail;
        }
    } else {
        slapi_ch_free(&data.data);
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_delete_key: Failed to remove %s; "
                        "has children\n", nrdn);
        rc = -1;
        goto bail;
    }
    workid = id;

    do {
        slapi_ch_free_string(&keybuf);
        slapi_ch_free((void **)&elem);
        tmpsrdn = srdn;
        if (NULL == parentnrdn && NULL == selfnrdn) {
            /* First, deleting parent link */
            /* E.g., P10 */
            keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_PARENT, workid);
            rc = slapi_rdn_partial_dup(srdn, &tmpsrdn, 1);
            if (rc) {
                char *dn  = NULL;
                slapi_rdn_get_dn(srdn, &dn);
                slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                            "_entryrdn_delete_key: partial dup of %s (idx %d) "
                            "failed (%d)\n", dn, 1, rc);
                slapi_ch_free_string(&dn);
                goto bail;
            }
            elem = _entryrdn_new_rdn_elem(be, 0 /*fake id*/, tmpsrdn, &len);
            if (NULL == elem) {
                char *dn  = NULL;
                slapi_rdn_get_dn(tmpsrdn, &dn);
                slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_delete_key: Failed to generate a parent "
                        "elem: dn: %s\n", dn);
                slapi_ch_free_string(&dn);
                slapi_rdn_free(&tmpsrdn);
                goto bail;
            }
        } else if (parentnrdn) {
            /* Then, the child link from the parent */
            keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, workid);
            elem = _entryrdn_new_rdn_elem(be, id, srdn, &len);
            if (NULL == elem) {
                char *dn  = NULL;
                slapi_rdn_get_dn(srdn, &dn);
                slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_delete_key: Failed to generate a parent's "
                        "child elem: dn: %s\n", dn);
                slapi_ch_free_string(&dn);
                goto bail;
            }
        } else if (selfnrdn) {
            /* Then, deleting the self elem */
            if (issuffix) {
                keybuf = slapi_ch_smprintf("%s", selfnrdn);
            } else {
                keybuf = slapi_ch_smprintf("%u", workid);
            }
            elem = _entryrdn_new_rdn_elem(be, id, srdn, &len);
            if (NULL == elem) {
                char *dn  = NULL;
                slapi_rdn_get_dn(srdn, &dn);
                slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_delete_key: Failed to generate a target "
                        "elem: dn: %s\n", dn);
                slapi_ch_free_string(&dn);
                goto bail;
            }
        }
        key.data = keybuf;
        key.size = key.ulen = strlen(keybuf) + 1;
        key.flags = DB_DBT_USERMEM;    

        memset(&data, 0, sizeof(data));
        data.ulen = data.size = len;
        data.data = elem;
        data.flags = DB_DBT_USERMEM;

        /* Position cursor at the matching key */
        rc = _entryrdn_get_elem(cursor, &key, &data,
                                slapi_rdn_get_nrdn(tmpsrdn), &elem); 
        if (tmpsrdn != srdn) {
            slapi_rdn_free(&tmpsrdn);
        }
        if (rc) {
            if (DB_NOTFOUND == rc) {
                slapi_log_error(SLAPI_LOG_BACKLDBM, ENTRYRDN_TAG,
                           "_entryrdn_delete_key: No parent link %s\n", keybuf);
                goto bail;
            } else {
                /* There's no parent or positioning at parent failed */
                _entryrdn_cursor_print_error("_entryrdn_delete_key",
                                            key.data, data.size, data.ulen, rc);
                goto bail;
            }
        }

        if (NULL == parentnrdn && NULL == selfnrdn) {
            /* First, deleting parent link */
#ifdef LDAP_DEBUG_ENTRYRDN
            _entryrdn_dump_rdn_elem(elem);
#endif
            parentnrdn = slapi_ch_strdup(elem->rdn_elem_nrdn_rdn);
            workid = id_stored_to_internal(elem->rdn_elem_id);

            /* deleteing the parent link */
            /* the cursor is set at the parent link by _entryrdn_get_elem */
            rc = cursor->c_del(cursor, 0);
            if (rc && DB_NOTFOUND != rc) {
                slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                                "_entryrdn_delete_key: Deleting %s failed; "
                                "%s(%d)\n", (char *)key.data,
                                dblayer_strerror(rc), rc);
                goto bail;
            }
        } else if (parentnrdn) {
#ifdef LDAP_DEBUG_ENTRYRDN
            _entryrdn_dump_rdn_elem(elem);
#endif
            slapi_ch_free_string(&parentnrdn);
            /* deleteing the parent's child link */
            /* the cursor is set at the parent link by _entryrdn_get_elem */
            rc = cursor->c_del(cursor, 0);
            if (rc && DB_NOTFOUND != rc) {
                slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                                "_entryrdn_delete_key: Deleting %s failed; "
                                "%s(%d)\n", (char *)key.data,
                                dblayer_strerror(rc), rc);
                goto bail;
            }
            selfnrdn = nrdn;
            workid = id;
        } else if (selfnrdn) {
#ifdef LDAP_DEBUG_ENTRYRDN
            _entryrdn_dump_rdn_elem(elem);
#endif
            /* deleteing the self link */
            /* the cursor is set at the parent link by _entryrdn_get_elem */
            rc = cursor->c_del(cursor, 0);
            if (rc && DB_NOTFOUND != rc) {
                slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                                "_entryrdn_delete_key: Deleting %s failed; "
                                "%s(%d)\n", (char *)key.data,
                                dblayer_strerror(rc), rc);
                goto bail;
            }
            goto bail; /* done */
        }
    } while (workid);

bail:
    slapi_ch_free_string(&keybuf);
    slapi_ch_free((void **)&elem);
    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "<-- _entryrdn_delete_key\n");
    return rc;
}

static int
_entryrdn_index_read(backend *be,
                     DBC *cursor,
                     Slapi_RDN *srdn,
                     rdn_elem **elem,
                     rdn_elem **parentelem,
                     rdn_elem ***childelems,
                     int flags,
                     DB_TXN *db_txn)
{
    int rc = -1;
    size_t len = 0;
    ID id;
    const char *nrdn = NULL; /* normalized rdn */
    const char *childnrdn = NULL; /* normalized rdn */
    int rdnidx = -1;
    char *keybuf = NULL;
    DBT key, data;
    size_t childnum = 32;
    size_t curr_childnum = 0;
    Slapi_RDN *tmpsrdn = NULL;
    rdn_elem *tmpelem = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "--> _entryrdn_index_read\n");
    if (NULL == be || NULL == cursor ||
        NULL == srdn || NULL == elem) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                    "_entryrdn_index_read: Param error: Empty %s\n",
                    NULL==be?"backend":NULL==cursor?"cursor":NULL==srdn?"RDN":
                    NULL==elem?"elem container":"unknown");
        goto bail;
    }

    *elem = NULL;
    if (parentelem) {
        *parentelem = NULL;
    }
    if (childelems) {
        *childelems = NULL;
    }
    /* get the top normalized rdn (normalized suffix) */
    rdnidx = slapi_rdn_get_last_ext(srdn, &nrdn, FLAG_ALL_NRDNS);
    if (rdnidx < 0 || NULL == nrdn) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_index_read: Empty RDN (Suffix)\n");
        goto bail;
    }
    /* Setting up a key for suffix */
    keybuf = slapi_ch_smprintf("%s", nrdn);
    key.data = keybuf;
    key.size = key.ulen = strlen(keybuf) + 1;
    key.flags = DB_DBT_USERMEM;    

    /* get id of the suffix */
    tmpsrdn = NULL;
    /* tmpsrdn == suffix'es srdn */
    rc = slapi_rdn_partial_dup(srdn, &tmpsrdn, rdnidx);
    if (rc) {
        char *dn  = NULL;
        slapi_rdn_get_dn(srdn, &dn);
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_index_read: partial dup of %s (idx %d) "
                        "failed (%d)\n", dn, rdnidx, rc);
        slapi_ch_free_string(&dn);
        goto bail;
    }
    *elem = _entryrdn_new_rdn_elem(be, 0 /*fake id*/, tmpsrdn, &len);
    if (NULL == *elem) {
        char *dn  = NULL;
        slapi_rdn_get_dn(tmpsrdn, &dn);
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_index_read: Failed to generate a new elem: "
                        "dn: %s\n", dn);
        slapi_ch_free_string(&dn);
        slapi_rdn_free(&tmpsrdn);
        goto bail;
    }

    memset(&data, 0, sizeof(data));
    data.ulen = data.size = len;
    data.data = *elem;
    data.flags = DB_DBT_USERMEM;

    /* getting the suffix element */
    rc = _entryrdn_get_elem(cursor, &key, &data, nrdn, elem); 
    if (rc || NULL == *elem) {
        slapi_ch_free((void **)elem);
        if (flags & TOMBSTONE_INCLUDED) {
            /* Node might be a tombstone. */
            rc = _entryrdn_get_tombstone_elem(cursor, tmpsrdn, 
                                              &key, nrdn, elem);
        }
        if (rc || NULL == *elem) {
            slapi_log_error(SLAPI_LOG_BACKLDBM, ENTRYRDN_TAG,
                            "_entryrdn_index_read: Suffix \"%s\" not found: "
                            "%s(%d)\n", nrdn, dblayer_strerror(rc), rc);
            rc = DB_NOTFOUND;
            slapi_rdn_free(&tmpsrdn);
            goto bail;
        }
    }
    slapi_rdn_free(&tmpsrdn);
    /* workid: ID of suffix */
    id = id_stored_to_internal((*elem)->rdn_elem_id);

    do {
        slapi_ch_free_string(&keybuf);

        /* Check the direct child in the RDN array, first */
        childnrdn = NULL;
        rdnidx = slapi_rdn_get_prev_ext(srdn, rdnidx,
                                        &childnrdn, FLAG_ALL_NRDNS);
        if (0 > rdnidx) {
            if (childelems) {
                break; /* get the child elems */
            } else {
                /* We got the targetelem.
                 * And we don't have to gather childelems, so we can return. */
#ifdef LDAP_DEBUG_ENTRYRDN
                char *dn = NULL;
                slapi_rdn_get_dn(srdn, &dn);
                slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                                "_entryrdn_index_read: done; DN %s => ID %d\n",
                                dn, id);
                slapi_ch_free_string(&dn);
#endif
                goto bail;
            }
        }
        /* 0 <= rdnidx */
        tmpsrdn = srdn;
        if (0 < rdnidx) {
            rc = slapi_rdn_partial_dup(srdn, &tmpsrdn, rdnidx);
            if (rc) {
                char *dn  = NULL;
                slapi_rdn_get_dn(srdn, &dn);
                slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                                "_entryrdn_delete_key: partial dup of %s "
                                "(idx %d) failed (%d)\n", dn, rdnidx, rc);
                slapi_ch_free_string(&dn);
                goto bail;
            }
        }
        tmpelem = _entryrdn_new_rdn_elem(be, 0 /*fake id*/, tmpsrdn, &len);
        if (NULL == tmpelem) {
            char *dn  = NULL;
            slapi_rdn_get_dn(tmpsrdn, &dn);
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_index_read: Failed to generate a new elem: "
                        "dn: %s\n", dn);
            slapi_ch_free_string(&dn);
            if (tmpsrdn != srdn) {
                slapi_rdn_free(&tmpsrdn);
            }
            goto bail;
        }

        /* Generate a key for child tree */
        /* E.g., C1 */
        keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, id);
        key.data = keybuf;
        key.size = key.ulen = strlen(keybuf) + 1;
        key.flags = DB_DBT_USERMEM;    
    
        memset(&data, 0, sizeof(data));
        data.ulen = data.size = len;
        data.data = tmpelem;
        data.flags = DB_DBT_USERMEM;

        /* Position cursor at the matching key */
        rc = _entryrdn_get_elem(cursor, &key, &data, childnrdn, &tmpelem); 
        if (rc) {
            slapi_ch_free((void **)&tmpelem);
            if (flags & TOMBSTONE_INCLUDED) {
                /* Node might be a tombstone */
                /*
                 * In DIT cn=A,ou=B,o=C, cn=A and ou=B are removed and
                 * turned to tombstone entries.  We need to support both:
                 *   nsuniqueid=...,cn=A,ou=B,o=C and
                 *   nsuniqueid=...,cn=A,nsuniqueid=...,ou=B,o=C
                 */
                rc = _entryrdn_get_tombstone_elem(cursor, tmpsrdn, &key, 
                                                  childnrdn, &tmpelem);
                if (rc || (NULL == tmpelem)) {
                    slapi_ch_free((void **)&tmpelem);
                    slapi_log_error(SLAPI_LOG_BACKLDBM, ENTRYRDN_TAG,
                                "_entryrdn_index_read: Child link \"%s\" of "
                                "key \"%s\" not found: %s(%d)\n",
                                childnrdn, keybuf, dblayer_strerror(rc), rc);
                    rc = DB_NOTFOUND;
                    if (tmpsrdn != srdn) {
                        slapi_rdn_free(&tmpsrdn);
                    }
                    goto bail;
                }
            } else {
                slapi_ch_free((void **)&tmpelem);
                slapi_log_error(SLAPI_LOG_BACKLDBM, ENTRYRDN_TAG,
                                "_entryrdn_index_read: Child link \"%s\" of "
                                "key \"%s\" not found: %s(%d)\n",
                                childnrdn, keybuf, dblayer_strerror(rc), rc);
                rc = DB_NOTFOUND;
                if (tmpsrdn != srdn) {
                        slapi_rdn_free(&tmpsrdn);
                }
                goto bail;
            }
        }
        if (tmpsrdn != srdn) {
            slapi_rdn_free(&tmpsrdn);
        }
#ifdef LDAP_DEBUG_ENTRYRDN
        _entryrdn_dump_rdn_elem(tmpelem);
#endif
        if (parentelem) {
            slapi_ch_free((void **)parentelem);
            *parentelem = *elem;
        } else {
            slapi_ch_free((void **)elem);
        }
        *elem = tmpelem;
#ifdef LDAP_DEBUG_ENTRYRDN
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_index_read: %s matched normalized child "
                        "rdn %s\n", (*elem)->rdn_elem_nrdn_rdn, childnrdn);
#endif
        id = id_stored_to_internal((*elem)->rdn_elem_id);
        nrdn = childnrdn;
    
        if (0 == id) {
            slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                           "_entryrdn_index_read: Child %s of %s not found\n", 
                           childnrdn, nrdn);
            break;
        }
    } while (rdnidx >= 0);

    /* get the child elems */
    if (childelems) {
        char buffer[RDN_BULK_FETCH_BUFFER_SIZE]; 

        slapi_ch_free_string(&keybuf);
        keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, id);
        key.data = keybuf;
        key.size = key.ulen = strlen(keybuf) + 1;
        key.flags = DB_DBT_USERMEM;    

        /* Setting the bulk fetch buffer */
        memset(&data, 0, sizeof(data));
        data.ulen = sizeof(buffer);
        data.size = sizeof(buffer);
        data.data = buffer;
        data.flags = DB_DBT_USERMEM;

retry_get0:
        rc = cursor->c_get(cursor, &key, &data, DB_SET|DB_MULTIPLE);
        if (DB_LOCK_DEADLOCK == rc) {
            /* try again */
            goto retry_get0;
        } else if (DB_NOTFOUND == rc) {
            rc = 0; /* Child not found is ok */
            goto bail;
        } else if (rc) {
            _entryrdn_cursor_print_error("_entryrdn_index_read",
                                            key.data, data.size, data.ulen, rc);
            goto bail;
        }
        
        *childelems = (rdn_elem **)slapi_ch_calloc(childnum,
                                                   sizeof(rdn_elem *));
        do {
            rdn_elem *childelem = NULL;
            DBT dataret;
            void *ptr;
            DB_MULTIPLE_INIT(ptr, &data);
            do {
                memset(&dataret, 0, sizeof(dataret));
                DB_MULTIPLE_NEXT(ptr, &data, dataret.data, dataret.size);
                if (NULL == dataret.data || NULL == ptr) {
                    break;
                }
                _entryrdn_dup_rdn_elem((const void *)dataret.data, &childelem);

                if (curr_childnum + 1 == childnum) {
                    childnum *= 2;
                    *childelems =
                              (rdn_elem **)slapi_ch_realloc((char *)*childelems,
                                                 sizeof(rdn_elem *) * childnum);
                    memset(*childelems + curr_childnum, 0,
                               sizeof(rdn_elem *) * (childnum - curr_childnum));
                }
                (*childelems)[curr_childnum++] = childelem;
            } while (NULL != dataret.data && NULL != ptr);
retry_get1:
            rc = cursor->c_get(cursor, &key, &data, DB_NEXT_DUP|DB_MULTIPLE);
            if (DB_LOCK_DEADLOCK == rc) {
                /* try again */
                goto retry_get1;
            } else if (DB_NOTFOUND == rc) {
                rc = 0;
                goto bail; /* done */
            } else if (rc) {
                _entryrdn_cursor_print_error("_entryrdn_index_read",
                                            key.data, data.size, data.ulen, rc);
                goto bail;
            }
        } while (0 == rc);
    }

bail:
    if (childelems && *childelems && 0 == curr_childnum) {
        slapi_ch_free((void **)childelems);
    }
    slapi_ch_free_string(&keybuf);
    slapi_log_error(SLAPI_LOG_TRACE, ENTRYRDN_TAG,
                                     "<-- _entryrdn_index_read\n");
    return rc;
}

static int
_entryrdn_append_childidl(DBC *cursor,
                          const char *nrdn,
                          ID id,
                          IDList **affectedidl)
{
    /* E.g., C5 */
    char *keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, id);
    DBT key, data;
    char buffer[RDN_BULK_FETCH_BUFFER_SIZE]; 
    int rc = 0;

    key.data = keybuf;
    key.size = key.ulen = strlen(keybuf) + 1;
    key.flags = DB_DBT_USERMEM;    

    /* Setting the bulk fetch buffer */
    memset(&data, 0, sizeof(data));
    data.ulen = sizeof(buffer);
    data.size = sizeof(buffer);
    data.data = buffer;
    data.flags = DB_DBT_USERMEM;

    /* Position cursor at the matching key */
retry_get0:
    rc = cursor->c_get(cursor, &key, &data, DB_SET|DB_MULTIPLE);
    if (rc) {
        if (DB_LOCK_DEADLOCK == rc) {
            /* try again */
            goto retry_get0;
        } else if (DB_NOTFOUND == rc) {
            rc = 0; /* okay not to have children */
        } else {
            _entryrdn_cursor_print_error("_entryrdn_append_childidl",
                                          key.data, data.size, data.ulen, rc);
        }
        goto bail;
    }
    
    /* Iterate over the duplicates to get the direct child's ID */
    do {
        rdn_elem *myelem = NULL;
        DBT dataret;
        void *ptr;
        DB_MULTIPLE_INIT(ptr, &data);
        do {
            ID myid = 0;
            myelem = NULL;
            memset(&dataret, 0, sizeof(dataret));
            DB_MULTIPLE_NEXT(ptr, &data, dataret.data, dataret.size);
            if (NULL == dataret.data || NULL == ptr) {
                break;
            }
            myelem = (rdn_elem *)dataret.data;
            myid = id_stored_to_internal(myelem->rdn_elem_id);
            rc = idl_append_extend(affectedidl, myid);
            if (rc) {
                slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "_entryrdn_append_childidl: Appending %d to "
                        "affectedidl failed (%d)\n", myid, rc);
                goto bail;
            }
            rc = _entryrdn_append_childidl(cursor,
                                        (const char *)myelem->rdn_elem_nrdn_rdn,
                                        myid, affectedidl);
            if (rc) {
                goto bail;
            }
        } while (NULL != dataret.data && NULL != ptr);
retry_get1:
        rc = cursor->c_get(cursor, &key, &data, DB_NEXT_DUP|DB_MULTIPLE);
        if (rc) {
            if (DB_LOCK_DEADLOCK == rc) {
                /* try again */
                goto retry_get1;
            } else if (DB_NOTFOUND == rc) {
                rc = 0; /* okay not to have children */
            } else {
                _entryrdn_cursor_print_error("_entryrdn_append_childidl",
                                           key.data, data.size, data.ulen, rc);
            }
            goto bail;
        }
    } while (0 == rc);

bail:
    slapi_ch_free_string(&keybuf);
    return rc;
}

static void
_entryrdn_cursor_print_error(char *fn, void *key,
                             size_t need, size_t actual, int rc)
{
    if (DB_BUFFER_SMALL == rc) {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "%s: Entryrdn index is corrupt; data item for key %s "
                        "is too large for the buffer need=%lu actual=%lu)\n",
                        fn, (char *)key, need, actual);
    } else {
        slapi_log_error(SLAPI_LOG_FATAL, ENTRYRDN_TAG,
                        "%s: Failed to position cursor at "
                        "the key: %s: %s(%d)\n",
                        fn, (char *)key, dblayer_strerror(rc), rc);
    }
}

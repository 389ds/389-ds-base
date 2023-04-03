/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
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
#define ASSERT(_x)                                                                                \
    do {                                                                                          \
        if (!(_x)) {                                                                              \
            slapi_log_err(SLAPI_LOG_DEBUG, "ldbm_entryrdn ASSERT - BAD ASSERTION at %s/%d: %s\n", \
                          __FILE__, __LINE__, #_x);                                               \
            *(char *)0L = 23;                                                                     \
        }                                                                                         \
    } while (0)
#else
#define ASSERT(_x) ;
#endif

#ifdef LDAP_DEBUG_ENTRYRDN
#define _ENTRYRDN_DUMP_RDN_ELEM(elem) _entryrdn_dump_rdn_elem(__LINE__, #elem, elem)
#define _ENTRYRDN_DUMP_RDN_ELEM_ARRAY(elem) { \
            if (elem) { \
                char buff[50]; \
                int i; \
                for (i=0; elem[i]; i++) { \
                    sprintf(buff, "%s[%d]", #elem, i); \
                    _entryrdn_dump_rdn_elem(__LINE__, buff, elem[i]); \
                } \
            } else { \
                _ENTRYRDN_DUMP_RDN_ELEM((rdn_elem*)elem); \
            } \
        }
#else
#define _ENTRYRDN_DUMP_RDN_ELEM(elem)
#define _ENTRYRDN_DUMP_RDN_ELEM_ARRAY(elem)
#endif

#define ENTRYRDN_LOGLEVEL(rc) \
    (((rc) == DBI_RC_RETRY) ? SLAPI_LOG_BACKLDBM : SLAPI_LOG_ERR)

#define ENTRYRDN_DELAY                                            \
    {                                                             \
        PRIntervalTime interval;                                  \
        interval = PR_MillisecondsToInterval(slapi_rand() % 100); \
        DS_Sleep(interval);                                       \
    }

#define RDN_INDEX_SELF 'S'
#define RDN_INDEX_CHILD 'C'
#define RDN_INDEX_PARENT 'P'

#define RDN_BULK_FETCH_BUFFER_SIZE (size_t)8 * 1024 /* DBLAYER_INDEX_PAGESIZE */
#define RDN_STRINGID_LEN 64

typedef struct _rdn_elem
{
    char rdn_elem_id[sizeof(ID)];
    char rdn_elem_nrdn_len[2]; /* ushort; length including '\0' */
    char rdn_elem_rdn_len[2];  /* ushort; length including '\0' */
    char rdn_elem_nrdn_rdn[1]; /* "normalized rdn" '\0' "rdn" '\0' */
} rdn_elem;

#define RDN_ADDR(elem)           \
    ((elem)->rdn_elem_nrdn_rdn + \
     sizeushort_stored_to_internal((elem)->rdn_elem_nrdn_len))

#define TMPID 0 /* Used for the fake ID */

/* RDN(s) which can be added even if no suffix exists in the entryrdn index */
const char *rdn_exceptions[] = {
    "nsuniqueid=ffffffff-ffffffff-ffffffff-ffffffff",
    NULL};

/* helper functions */
static rdn_elem *_entryrdn_new_rdn_elem(backend *be, ID id, Slapi_RDN *srdn, size_t *length);
static void _entryrdn_dup_rdn_elem(const void *raw, rdn_elem **new);
static size_t _entryrdn_rdn_elem_size(rdn_elem *elem);
#ifdef LDAP_DEBUG_ENTRYRDN
static void _entryrdn_dump_rdn_elem(int lineno, const char*name, rdn_elem *elem);
#endif
static int _entryrdn_open_index(backend *be, struct attrinfo **ai, dbi_db_t **dbp);
static int _entryrdn_get_elem(dbi_cursor_t *cursor, dbi_val_t *key, dbi_val_t *data, const char *comp_key, rdn_elem **elem, dbi_txn_t *db_txn);
static int _entryrdn_get_tombstone_elem(dbi_cursor_t *cursor, Slapi_RDN *srdn, dbi_val_t *key, const char *comp_key, rdn_elem **elem, dbi_txn_t *db_txn);
static int _entryrdn_put_data(dbi_cursor_t *cursor, dbi_val_t *key, dbi_val_t *data, char type, dbi_txn_t *db_txn);
static int _entryrdn_del_data(dbi_cursor_t *cursor, dbi_val_t *key, dbi_val_t *data, dbi_txn_t *db_txn);
static int _entryrdn_insert_key_elems(backend *be, dbi_cursor_t *cursor, Slapi_RDN *srdn, dbi_val_t *key, rdn_elem *elem, rdn_elem *childelem, size_t childelemlen, dbi_txn_t *db_txn);
static int _entryrdn_index_read(backend *be, dbi_cursor_t *cursor, Slapi_RDN *srdn, rdn_elem **elem, rdn_elem **parentelem, rdn_elem ***childelems, int flags, dbi_txn_t *db_txn);
static int _entryrdn_append_childidl(dbi_cursor_t *cursor, const char *nrdn, ID id, IDList **affectedidl, dbi_txn_t *db_txn);
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

    if (entryrdn_switch) { /* entryrdn on */
        /* Don't store entrydn in the db */
        set_attr_to_protected_list(SLAPI_ATTR_ENTRYDN, 0);
    } else { /* entryrdn off */
        /* Store entrydn in the db */
        set_attr_to_protected_list(SLAPI_ATTR_ENTRYDN, 1);
    }

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
    dbi_db_t *db = NULL;
    dbi_cursor_t cursor = {0};
    dbi_txn_t *db_txn = (txn != NULL) ? txn->back_txn_txn : NULL;
    const Slapi_DN *sdn = NULL;
    Slapi_RDN *srdn = NULL;
    int db_retry = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_index_entry",
                  "--> entryrdn_index_entry\n");
    if (NULL == be || NULL == e) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_index_entry",
                      "Param error: Empty %s\n", NULL == be ? "backend" : NULL == e ? "entry" : "unknown");
        return rc;
    }
    /* Open the entryrdn index */
    rc = _entryrdn_open_index(be, &ai, &db);
    if (rc || (NULL == db)) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_index_entry",
                      "Opening the index failed: %s(%d)\n",
                      rc < 0 ? dblayer_strerror(rc) : "Invalid parameter", rc);
        return rc;
    }

    srdn = slapi_entry_get_srdn(e->ep_entry);
    if (NULL == slapi_rdn_get_rdn(srdn)) {
        sdn = slapi_entry_get_sdn_const(e->ep_entry);
        if (NULL == sdn) {
            slapi_log_err(SLAPI_LOG_ERR, "entryrdn_index_entry",
                          "Empty dn\n");
            goto bail;
        }
        rc = slapi_rdn_init_all_sdn(srdn, sdn);
        if (rc < 0) {
            slapi_log_err(SLAPI_LOG_ERR, "entryrdn_index_entry",
                          "Failed to convert %s to Slapi_RDN\n", slapi_sdn_get_dn(sdn));
            rc = LDAP_INVALID_DN_SYNTAX;
            goto bail;
        } else if (rc > 0) {
            slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_index_entry",
                          "%s does not belong to the db\n", slapi_sdn_get_dn(sdn));
            rc = DBI_RC_NOTFOUND;
            goto bail;
        }
    }

    /* Make a cursor */
    for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
        rc = dblayer_new_cursor(be, db, db_txn, &cursor);
        if (rc) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_index_entry",
                          "Failed to make a cursor: %s(%d)\n", dblayer_strerror(rc), rc);
            if ((DBI_RC_RETRY == rc) && !db_txn) {
                ENTRYRDN_DELAY;
                continue;
            }
            goto bail;
        } else {
            break; /* success */
        }
    }
    if (RETRY_TIMES == db_retry) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_index_entry",
                      "Cursor open failed after [%d] retries\n", db_retry);
        rc = DBI_RC_RETRY;
        goto bail;
    }

    if (flags & BE_INDEX_ADD) {
        rc = entryrdn_insert_key(be, &cursor, srdn, e->ep_id, txn);
    } else if (flags & BE_INDEX_DEL) {
        rc = entryrdn_delete_key(be, &cursor, srdn, e->ep_id, txn);
        if (DBI_RC_NOTFOUND == rc) {
            rc = 0;
        }
    }

bail:
    /* Close the cursor */
    for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
        int myrc = dblayer_cursor_op(&cursor, DBI_OP_CLOSE, NULL, NULL);
        if (0 != myrc) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(myrc), "entryrdn_index_entry",
                          "Failed to close cursor: %s(%d)\n",
                          dblayer_strerror(myrc), myrc);
            if ((DBI_RC_RETRY == myrc) && !db_txn) {
                ENTRYRDN_DELAY;
                continue;
            }
            if (!rc) {
                /* if cursor close returns DEADLOCK, we must bubble that up
                   to the higher layers for retries */
                rc = myrc;
                break;
            }
        } else {
            break; /* success */
        }
    }
    if (RETRY_TIMES == db_retry) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_index_entry",
                      "Cursor close failed after [%d] retries\n", db_retry);
        rc = DBI_RC_RETRY;
    }
    if (db) {
        dblayer_release_index_file(be, ai, db);
    }

    slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_index_entry",
                  "<-- entryrdn_index_entry\n");
    return rc;
}

/* Convert internal values to rdnentry dbi record */
void *entryrdn_encode_data(backend *be, size_t *rdn_elem_len, ID id, const char *nrdn, const char *rdn)
{
    size_t rdn_len = 0;
    size_t nrdn_len = 0;
    rdn_elem *re = NULL;

    /* If necessary, encrypt this index key */
    rdn_len = strlen(rdn) + 1;
    nrdn_len = strlen(nrdn) + 1;
    *rdn_elem_len = sizeof(rdn_elem) + rdn_len + nrdn_len;
    re = (rdn_elem *)slapi_ch_malloc(*rdn_elem_len);
    id_internal_to_stored(id, re->rdn_elem_id);
    sizeushort_internal_to_stored(nrdn_len, re->rdn_elem_nrdn_len);
    sizeushort_internal_to_stored(rdn_len, re->rdn_elem_rdn_len);
    PL_strncpyz(re->rdn_elem_nrdn_rdn, nrdn, nrdn_len);
    PL_strncpyz(RDN_ADDR(re), rdn, rdn_len);
    return re;
}

/* Convert raw data from rdnentry dbi record to internal values. */
void
entryrdn_decode_data(backend *be, void *rdn_elem, ID *id, int *nrdnlen, char **nrdn, int *rdnlen, char **rdn)
{
    /* 'be' is unused for now may will be useful if we code entryrdn encryption */
    /* warning 'be' may be null (when coming from dbscan) */
    struct _rdn_elem *elem = rdn_elem;
    int len = sizeushort_stored_to_internal(elem->rdn_elem_nrdn_len);

    if (id) {
        *id = id_stored_to_internal(elem->rdn_elem_id);
    }
    if (nrdnlen) {
        *nrdnlen = len;
    }
    if (rdnlen) {
        *rdnlen = sizeushort_stored_to_internal(elem->rdn_elem_rdn_len);
    }
    if (nrdn) {
        *nrdn = elem->rdn_elem_nrdn_rdn;
    }
    if (rdn) {
        *rdn = &elem->rdn_elem_nrdn_rdn[len];
    }
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
    return entryrdn_index_read_ext(be, sdn, id, 0 /*flags*/, txn);
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
    dbi_db_t *db = NULL;
    dbi_txn_t *db_txn = (txn != NULL) ? txn->back_txn_txn : NULL;
    dbi_cursor_t cursor = {0};
    rdn_elem *elem = NULL;
    int db_retry = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_index_read",
                  "--> entryrdn_index_read\n");

    if (NULL == be || NULL == sdn || NULL == id) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_index_read_ext",
                      "Param error: Empty %s\n",
                      NULL == be ? "backend" : NULL == sdn ? "DN" : NULL == id ? "id container" : "unknown");
        goto bail;
    }

    *id = 0;

    rc = slapi_rdn_init_all_sdn(&srdn, sdn);
    if (rc < 0) {
        slapi_log_err(SLAPI_LOG_BACKLDBM, "entryrdn_index_read_ext",
                      "Param error: Failed to convert %s to Slapi_RDN\n",
                      slapi_sdn_get_dn(sdn));
        rc = LDAP_INVALID_DN_SYNTAX;
        goto bail;
    } else if (rc > 0) {
        slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_index_read_ext",
                      "%s does not belong to the db\n", slapi_sdn_get_dn(sdn));
        rc = DBI_RC_NOTFOUND;
        goto bail;
    }

    /* Open the entryrdn index */
    rc = _entryrdn_open_index(be, &ai, &db);
    if (rc || (NULL == db)) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_index_read_ext",
                      "Opening the index failed: %s(%d)\n",
                      rc < 0 ? dblayer_strerror(rc) : "Invalid parameter", rc);
        db = NULL;
        goto bail;
    }

    /* Make a cursor */
    for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
        rc = dblayer_new_cursor(be, db, db_txn, &cursor);
        if (rc) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_index_read_ext",
                          "Failed to make a cursor: %s(%d)\n", dblayer_strerror(rc), rc);
            if ((DBI_RC_RETRY == rc) && !db_txn) {
                ENTRYRDN_DELAY;
                continue;
            }
            goto bail;
        } else {
            break; /* success */
        }
    }
    if (RETRY_TIMES == db_retry) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_index_read_ext",
                      "Failed to make a cursor after [%d] retries\n", db_retry);
        rc = DBI_RC_RETRY;
        goto bail;
    }

    rc = _entryrdn_index_read(be, &cursor, &srdn, &elem, NULL, NULL,
                              flags, db_txn);
    if (rc) {
        goto bail;
    }
    *id = id_stored_to_internal(elem->rdn_elem_id);

bail:
    /* Close the cursor */
    for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
        int myrc = dblayer_cursor_op(&cursor, DBI_OP_CLOSE, NULL, NULL);
        if (0 != myrc) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(myrc), "entryrdn_index_read_ext",
                          "Failed to close cursor: %s(%d)\n", dblayer_strerror(myrc), myrc);
            if ((DBI_RC_RETRY == myrc) && !db_txn) {
                ENTRYRDN_DELAY;
                continue;
            }
            if (!rc) {
                /* if cursor close returns DEADLOCK, we must bubble that up
                   to the higher layers for retries */
                rc = myrc;
                break;
            }
        } else {
            break; /* success */
        }
    }
    if (RETRY_TIMES == db_retry) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_index_read_ext",
                      "Failed to close cursor after [%d] retries\n", db_retry);
        rc = rc ? rc : DBI_RC_RETRY;
    }
    if (db) {
        dblayer_release_index_file(be, ai, db);
    }
    slapi_rdn_done(&srdn);
    slapi_ch_free((void **)&elem);
    slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_index_read",
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
                        back_txn *txn,
                        int flags)
{
    int rc = -1;
    struct attrinfo *ai = NULL;
    dbi_db_t *db = NULL;
    dbi_cursor_t cursor = {0};
    dbi_txn_t *db_txn = (txn != NULL) ? txn->back_txn_txn : NULL;
    Slapi_RDN oldsrdn = {0};
    Slapi_RDN supsrdn = {0};
    Slapi_RDN newsupsrdn = {0};
    const char *nrdn = NULL; /* normalized rdn */
    int rdnidx = -1;
    char *keybuf = NULL;
    dbi_val_t key = {0};
    dbi_val_t renamedata = {0};
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
    int db_retry = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_rename_subtree",
                  "--> entryrdn_rename_subtree\n");

    if (NULL == be || NULL == oldsdn || 0 == id) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_rename_subtree",
                      "Param error: Empty %s\n",
                      NULL == be ? "backend" : NULL == oldsdn ? "old dn" : (NULL == newsrdn && NULL == newsupsdn) ? "new dn and new superior" : 0 == id ? "id" : "unknown");
        goto bail;
    }

    rc = slapi_rdn_init_all_sdn_ext(&oldsrdn, oldsdn, flags);
    if (rc < 0) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_rename_subtree",
                      "Failed to convert olddn \"%s\" to Slapi_RDN\n",
                      slapi_sdn_get_dn(oldsdn));
        rc = LDAP_INVALID_DN_SYNTAX;
        goto bail;
    } else if (rc > 0) {
        slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_rename_subtree",
                      "%s does not belong to the db\n", slapi_sdn_get_dn(oldsdn));
        rc = DBI_RC_NOTFOUND;
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
        /* Don't miss the case changes, too. */
        if (strcmp(slapi_rdn_get_rdn(newsrdn), slapi_rdn_get_rdn(&oldsrdn))) {
            /* did not match; let's rename it */
            mynewsrdn = newsrdn;
        }
    }
    if (NULL == mynewsrdn && NULL == mynewsupsdn) {
        /* E.g., rename dn: cn=ABC    DEF,... --> cn=ABC DEF,... */
        slapi_log_err(SLAPI_LOG_BACKLDBM, "entryrdn_rename_subtree",
                      "No new superior is given "
                      "and new rdn %s is identical to the original\n",
                      slapi_rdn_get_rdn(&oldsrdn));
        goto bail;
    }

    /* Checking the contents of oldsrdn */
    rdnidx = slapi_rdn_get_last_ext(&oldsrdn, &nrdn, FLAG_ALL_NRDNS);
    if (rdnidx < 0 || NULL == nrdn) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_rename_subtree",
                      "Empty RDN\n");
        goto bail;
    } else if (0 == rdnidx) {
        if (mynewsupsdn) {
            slapi_log_err(SLAPI_LOG_ERR, "entryrdn_rename_subtree",
                          "Moving suffix \"%s\" is not alloweds\n", nrdn);
            goto bail;
        } else {
            /* newsupsdn == NULL, so newsrdn is not */
            slapi_log_err(SLAPI_LOG_BACKLDBM, "entryrdn_rename_subtree",
                          "Renaming suffix %s to %s\n",
                          nrdn, slapi_rdn_get_nrdn((Slapi_RDN *)mynewsrdn));
        }
    }

    /* Open the entryrdn index */
    rc = _entryrdn_open_index(be, &ai, &db);
    if (rc || (NULL == db)) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_rename_subtree",
                      "Opening the index failed: %s(%d)\n",
                      rc < 0 ? dblayer_strerror(rc) : "Invalid parameter", rc);
        db = NULL;
        return rc;
    }

    /* Make a cursor */
    for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
        rc = dblayer_new_cursor(be, db, db_txn, &cursor);
        if (rc) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_rename_subtree",
                          "Failed to make a cursor: %s(%d)\n", dblayer_strerror(rc), rc);
            if ((DBI_RC_RETRY == rc) && !db_txn) {
                ENTRYRDN_DELAY;
                continue;
            }
            goto bail;
        } else {
            break; /* success */
        }
    }
    if (RETRY_TIMES == db_retry) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_rename_subtree",
                      "Create cursor failed after [%d] retries\n",
                      db_retry);
        rc = DBI_RC_RETRY;
        goto bail;
    }

    /* prepare the element for the newly renamed rdn, if any. */
    if (mynewsrdn) {
        newelem = _entryrdn_new_rdn_elem(be, id, mynewsrdn, &newelemlen);
        if (NULL == newelem) {
            slapi_log_err(SLAPI_LOG_ERR, "entryrdn_rename_subtree",
                          "Failed to generate a new elem: id: %d, rdn: %s\n",
                          id, slapi_rdn_get_rdn(mynewsrdn));
            goto bail;
        }
    }

    /* Get the new superior elem, if any. */
    if (mynewsupsdn) {
        rc = slapi_rdn_init_all_sdn(&newsupsrdn, mynewsupsdn);
        if (rc < 0) {
            slapi_log_err(SLAPI_LOG_ERR, "entryrdn_rename_subtree",
                          "Failed to convert new superior \"%s\" to Slapi_RDN\n",
                          slapi_sdn_get_dn(mynewsupsdn));
            rc = LDAP_INVALID_DN_SYNTAX;
            goto bail;
        } else if (rc > 0) {
            slapi_log_err(SLAPI_LOG_BACKLDBM, "entryrdn_rename_subtree",
                          "%s does not belong to the db\n", slapi_sdn_get_dn(mynewsupsdn));
            rc = DBI_RC_NOTFOUND;
            goto bail;
        }

        rc = _entryrdn_index_read(be, &cursor, &newsupsrdn, &newsupelem,
                                  NULL, NULL, 0 /*flags*/, db_txn);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "entryrdn_rename_subtree",
                          "Failed to read the element of new superior \"%s\" (%d)\n",
                          slapi_sdn_get_dn(mynewsupsdn), rc);
            goto bail;
        }
        newsupelemlen = _entryrdn_rdn_elem_size(newsupelem);
    }

    if (mynewsrdn) {
        rc = _entryrdn_index_read(be, &cursor, &oldsrdn, &targetelem,
                                  &oldsupelem, &childelems, 0 /*flags*/, db_txn);
    } else {
        rc = _entryrdn_index_read(be, &cursor, &oldsrdn, &targetelem,
                                  &oldsupelem, NULL, 0 /*flags*/, db_txn);
    }
    _ENTRYRDN_DUMP_RDN_ELEM(targetelem);
    _ENTRYRDN_DUMP_RDN_ELEM(oldsupelem);
    _ENTRYRDN_DUMP_RDN_ELEM_ARRAY(childelems);
    if (rc || NULL == targetelem) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_rename_subtree",
                      "Failed to read the target element \"%s\" (%d)\n",
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
        dblayer_value_set(be, &key, keybuf, strlen(keybuf) + 1);

        dblayer_value_set_buffer(be, &renamedata, targetelem, targetelemlen);
        rc = _entryrdn_del_data(&cursor, &key, &renamedata, db_txn);
        if (rc) {
            goto bail;
        }
        if (childelems) {
            keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, targetid);
            dblayer_value_set(be, &key, keybuf, strlen(keybuf) + 1);
            /* remove the old elem; (2) update targetelem's child link */
            for (cep = childelems; cep && *cep; cep++) {
                dblayer_value_set_buffer(be, &renamedata, *cep, _entryrdn_rdn_elem_size(*cep));
                rc = _entryrdn_del_data(&cursor, &key, &renamedata, db_txn);
                if (rc) {
                    goto bail;
                }
            }
        }

        /* add the new elem */
        keybuf = slapi_ch_smprintf("%u", id);
        dblayer_value_set(be, &key, keybuf, strlen(keybuf) + 1);
        rc = _entryrdn_put_data(&cursor, &key, &renamedata, RDN_INDEX_SELF, db_txn);
        if (rc && (DBI_RC_KEYEXIST != rc)) { /* failed && ignore already exists */
            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_rename_subtree",
                          "Adding %s failed; %s(%d)\n", keybuf, dblayer_strerror(rc), rc);
            goto bail;
        }
        if (childelems) {
            keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, id);
            dblayer_value_set(be, &key, keybuf, strlen(keybuf) + 1);
            /* add the new elem; (2) update targetelem's child link */
            for (cep = childelems; cep && *cep; cep++) {
                dblayer_value_set_buffer(be, &renamedata, *cep, _entryrdn_rdn_elem_size(*cep));
                rc = _entryrdn_put_data(&cursor, &key,
                                        &renamedata, RDN_INDEX_CHILD, db_txn);
                if (rc && (DBI_RC_KEYEXIST != rc)) { /* failed && ignore already exists */
                    goto bail;
                }
            }
        }
    }
    /* 3) update targetelem's parent link, if any */
    if (oldsupelem) {
        keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_PARENT, targetid);
        dblayer_value_set(be, &key, keybuf, strlen(keybuf) + 1);
        dblayer_value_set_buffer(be, &renamedata, oldsupelem, oldsupelemlen);
        rc = _entryrdn_del_data(&cursor, &key, &renamedata, db_txn);
        if (rc) {
            goto bail;
        }

        /* add the new elem */
        if (mynewsrdn) {
            keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_PARENT, id);
            dblayer_value_set(be, &key, keybuf, strlen(keybuf) + 1);
            if (mynewsupsdn) {
                dblayer_value_set_buffer(be, &renamedata, newsupelem, newsupelemlen);
            } else {
                dblayer_value_set_buffer(be, &renamedata, oldsupelem, oldsupelemlen);
            }
        } else {
            dblayer_value_set_buffer(be, &renamedata, newsupelem, newsupelemlen);
        }
        rc = _entryrdn_put_data(&cursor, &key, &renamedata, RDN_INDEX_PARENT, db_txn);
        if (rc && (DBI_RC_KEYEXIST != rc)) { /* failed && ignore already exists */
            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_rename_subtree",
                          "Adding %s failed; %s(%d)\n", keybuf, dblayer_strerror(rc), rc);
            goto bail;
        }
    }

    /* 4) update targetelem's children's parent link, if renaming the target */
    if (mynewsrdn) {
        for (cep = childelems; cep && *cep; cep++) {
            /* remove the old elem */
            keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_PARENT,
                                       id_stored_to_internal((*cep)->rdn_elem_id));
            dblayer_value_set(be, &key, keybuf, strlen(keybuf) + 1);
            dblayer_value_set_buffer(be, &renamedata, targetelem, targetelemlen);
            rc = _entryrdn_del_data(&cursor, &key, &renamedata, db_txn);
            if (rc) {
                goto bail;
            }

            /* add the new elem */
            dblayer_value_set_buffer(be, &renamedata, newelem, newelemlen);
            rc = _entryrdn_put_data(&cursor, &key, &renamedata, RDN_INDEX_SELF, db_txn);
            if (rc && (DBI_RC_KEYEXIST != rc)) { /* failed && ignore already exists */
                slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_rename_subtree",
                              "Adding %s failed; %s(%d)\n", keybuf, dblayer_strerror(rc), rc);
                goto bail;
            }
        }
    }

    /* 5) update parentelem's child link (except renaming the suffix) */
    if (oldsupelem) {
        /* remove the old elem */
        keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD,
                                   id_stored_to_internal(oldsupelem->rdn_elem_id));
        dblayer_value_set(be, &key, keybuf, strlen(keybuf) + 1);
        dblayer_value_set_buffer(be, &renamedata, targetelem, targetelemlen);
        rc = _entryrdn_del_data(&cursor, &key, &renamedata, db_txn);
        if (rc) {
            goto bail;
        }

        /* add the new elem */
        if (mynewsupsdn) {
            keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD,
                                       id_stored_to_internal(newsupelem->rdn_elem_id));
            dblayer_value_set(be, &key, keybuf, strlen(keybuf) + 1);
            if (mynewsrdn) {
                dblayer_value_set_buffer(be, &renamedata, newelem, newelemlen);
            } else {
                dblayer_value_set_buffer(be, &renamedata, targetelem, targetelemlen);
            }
        } else {
            dblayer_value_set_buffer(be, &renamedata, newelem, newelemlen);
        }
        rc = _entryrdn_put_data(&cursor, &key, &renamedata, RDN_INDEX_CHILD, db_txn);
        if (rc && (DBI_RC_KEYEXIST != rc)) { /* failed && ignore already exists */
            goto bail;
        }
    }

bail:
    dblayer_value_free(be, &key);
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
    for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
        int myrc = dblayer_cursor_op(&cursor, DBI_OP_CLOSE, NULL, NULL);
        if (0 != myrc) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(myrc), "entryrdn_rename_subtree",
                          "Failed to close cursor: %s(%d)\n", dblayer_strerror(myrc), myrc);
            if ((DBI_RC_RETRY == myrc) && !db_txn) {
                ENTRYRDN_DELAY;
                continue;
            }
            if (!rc) {
                /* if cursor close returns DEADLOCK, we must bubble that up
                   to the higher layers for retries */
                rc = myrc;
                break;
            }
        } else {
            break; /* success */
        }
    }
    if (RETRY_TIMES == db_retry) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_rename_subtree",
                      "Failed to close cursor after [%d] retries.\n", db_retry);
        rc = rc ? rc : DBI_RC_RETRY;
    }
    if (db) {
        dblayer_release_index_file(be, ai, db);
    }
    slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_rename_subtree",
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
                          back_txn *txn,
                          int flags)
{
    int rc = -1;
    struct attrinfo *ai = NULL;
    dbi_db_t *db = NULL;
    dbi_cursor_t cursor = {0};
    dbi_txn_t *db_txn = (txn != NULL) ? txn->back_txn_txn : NULL;
    Slapi_RDN srdn = {0};
    const char *nrdn = NULL; /* normalized rdn */
    int rdnidx = -1;
    rdn_elem *elem = NULL;
    rdn_elem **childelems = NULL;
    rdn_elem **cep = NULL;
    int db_retry = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_get_subordinates",
                  "--> entryrdn_get_subordinates\n");

    if (NULL == be || NULL == sdn || 0 == id) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_get_subordinates",
                      "Param error: Empty %s\n",
                      NULL == be ? "backend" : NULL == sdn ? "dn" : 0 == id ? "id" : "unknown");
        goto bail;
    }
    if (subordinates) {
        *subordinates = NULL;
    } else {
        rc = 0;
        goto bail;
    }

    rc = slapi_rdn_init_all_sdn_ext(&srdn, sdn, flags);
    if (rc) {
        if (rc < 0) {
            slapi_log_err(SLAPI_LOG_ERR, "entryrdn_get_subordinates",
                          "Failed to convert \"%s\" to Slapi_RDN\n", slapi_sdn_get_dn(sdn));
            rc = LDAP_INVALID_DN_SYNTAX;
        } else if (rc > 0) {
              slapi_log_err(SLAPI_LOG_ERR, "entryrdn_get_subordinates",
                          "Failed to convert \"%s\" to Slapi_RDN\n", slapi_sdn_get_dn(sdn));
          slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_get_subordinates",
                          "%s does not belong to the db\n", slapi_sdn_get_dn(sdn));
            rc = DBI_RC_NOTFOUND;
        }
        goto bail;
    }

    /* check the given dn/srdn */
    rdnidx = slapi_rdn_get_last_ext(&srdn, &nrdn, FLAG_ALL_NRDNS);
    if (rdnidx < 0 || NULL == nrdn) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_get_subordinates",
                      "Empty RDN\n");
        goto bail;
    }

    /* Open the entryrdn index */
    rc = _entryrdn_open_index(be, &ai, &db);
    if (rc || (NULL == db)) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_get_subordinates",
                      "Opening the index failed: %s(%d)\n",
                      rc < 0 ? dblayer_strerror(rc) : "Invalid parameter", rc);
        db = NULL;
        goto bail;
    }

    /* Make a cursor */
    for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
        rc = dblayer_new_cursor(be, db, db_txn, &cursor);
        if (rc) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_get_subordinates",
                          "Failed to make a cursor: %s(%d)\n", dblayer_strerror(rc), rc);
            if ((DBI_RC_RETRY == rc) && !db_txn) {
                ENTRYRDN_DELAY;
                continue;
            }
            goto bail;
        } else {
            break; /* success */
        }
    }
    if (RETRY_TIMES == db_retry) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_get_subordinates",
                      "Failed to make a cursor after [%d] retries\n", db_retry);
        rc = DBI_RC_RETRY;
        goto bail;
    }

    rc = _entryrdn_index_read(be, &cursor, &srdn, &elem,
                              NULL, &childelems, 0 /*flags*/, db_txn);
    if ((rc == DBI_RC_RETRY) && db_txn) {
        goto bail;
    }

    for (cep = childelems; cep && *cep; cep++) {
        ID childid = id_stored_to_internal((*cep)->rdn_elem_id);
        /* set direct children to the idlist */
        rc = idl_append_extend(subordinates, childid);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "entryrdn_get_subordinates",
                          "Appending %d to idl for direct children failed (%d)\n",
                          childid, rc);
            goto bail;
        }

        /* set indirect subordinates to the idlist */
        rc = _entryrdn_append_childidl(&cursor, (*cep)->rdn_elem_nrdn_rdn,
                                       childid, subordinates, db_txn);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "entryrdn_get_subordinates",
                          "Appending %d to idl for indirect children failed (%d)\n",
                          childid, rc);
            goto bail;
        }
    }

bail:
    if (rc && subordinates && *subordinates) {
        idl_free(subordinates);
    }
    slapi_ch_free((void **)&elem);
    slapi_rdn_done(&srdn);
    if (childelems) {
        for (cep = childelems; *cep; cep++) {
            slapi_ch_free((void **)cep);
        }
        slapi_ch_free((void **)&childelems);
    }

    /* Close the cursor */
    for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
        int myrc = dblayer_cursor_op(&cursor, DBI_OP_CLOSE, NULL, NULL);
        if (0 != myrc) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(myrc), "entryrdn_get_subordinates",
                          "Failed to close cursor: %s(%d)\n", dblayer_strerror(myrc), myrc);
            if ((DBI_RC_RETRY == myrc) && !db_txn) {
                ENTRYRDN_DELAY;
                continue;
            }
            if (!rc) {
                /* if cursor close returns DEADLOCK, we must bubble that up
                   to the higher layers for retries */
                rc = myrc;
                break;
            }
        } else {
            break; /* success */
        }
    }
    if (RETRY_TIMES == db_retry) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_get_subordinates",
                      "Failed to close cursor after [%d] retries\n", db_retry);
        rc = rc ? rc : DBI_RC_RETRY;
        goto bail;
    }
    if (db) {
        dblayer_release_index_file(be, ai, db);
    }
    slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_get_subordinates",
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
                   Slapi_RDN **psrdn,
                   back_txn *txn)
{
    int rc = -1;
    struct attrinfo *ai = NULL;
    dbi_db_t *db = NULL;
    dbi_cursor_t cursor = {0};
    dbi_txn_t *db_txn = (txn != NULL) ? txn->back_txn_txn : NULL;
    dbi_val_t key = {0};
    dbi_val_t data = {0};
    char *keybuf = NULL;
    Slapi_RDN *srdn = NULL;
    char *orignrdn = NULL;
    char *nrdn = NULL;
    size_t nrdn_len = 0;
    ID workid = id; /* starting from the given id */
    rdn_elem *elem = NULL;
    int maybesuffix = 0;
    int db_retry = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_lookup_dn",
                  "--> entryrdn_lookup_dn\n");

    if (NULL == be || NULL == rdn || 0 == id || NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_lookup_dn",
                      "Param error: Empty %s\n",
                      NULL == be ? "backend" : NULL == rdn ? "rdn" : 0 == id ? "id" : NULL == dn ? "dn container" : "unknown");
        return rc;
    }

    *dn = NULL;
    if (psrdn)
        *psrdn = NULL;
    /* Open the entryrdn index */
    rc = _entryrdn_open_index(be, &ai, &db);
    if (rc || (NULL == db)) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_lookup_dn",
                      "Opening the index failed: %s(%d)\n",
                      rc < 0 ? dblayer_strerror(rc) : "Invalid parameter", rc);
        return rc;
    }

    /* Make a cursor */
    for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
        rc = dblayer_new_cursor(be, db, db_txn, &cursor);
        if (rc) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_lookup_dn",
                          "Failed to make a cursor: %s(%d)\n", dblayer_strerror(rc), rc);
            if (DBI_RC_RETRY == rc) {
#ifdef FIX_TXN_DEADLOCKS
#error if txn != NULL, have to retry the entire transaction
#endif
                ENTRYRDN_DELAY;
                continue;
            }
            goto bail;
        } else {
            break; /* success */
        }
    }
    srdn = slapi_rdn_new_all_dn(rdn);
    orignrdn = slapi_ch_strdup(rdn);
    rc = slapi_dn_normalize_case_ext(orignrdn, 0, &nrdn, &nrdn_len);
    if (rc < 0) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_lookup_dn",
                      "Failed to normalize %s\n", rdn);
        goto bail;
    }
    if (rc == 0) { /* orignrdn is passed in */
        *(nrdn + nrdn_len) = '\0';
    } else {
        slapi_ch_free_string(&orignrdn);
    }

    /* Setting the bulk fetch buffer */
    dblayer_value_free(be, &data);
    dblayer_value_init(be, &data);

    do {
        /* Setting up a key for the node to get its parent */
        keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_PARENT, workid);
        dblayer_value_set(be, &key, keybuf, strlen(keybuf) + 1);

    /* Position cursor at the matching key */
    retry_get0:
        rc = dblayer_cursor_op(&cursor, DBI_OP_MOVE_TO_KEY, &key, &data);
        if (rc) {
            if (DBI_RC_RETRY == rc) {
                if (db_txn) {
                    slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_lookup_dn",
                                  "Cursor got deadlock while under txn -> failure\n");
                    goto bail;
                } else {
                    /* try again */
                    slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_lookup_dn",
                                  "Cursor deadlocked, trying again.\n");
                    goto retry_get0;
                }
            } else if (DBI_RC_NOTFOUND == rc) { /* could be a suffix or
                                               note: no parent for suffix */
                keybuf = slapi_ch_smprintf("%s", nrdn);
                dblayer_value_set(be, &key, keybuf, strlen(keybuf) + 1);
            retry_get1:
                rc = dblayer_cursor_op(&cursor, DBI_OP_MOVE_TO_KEY, &key, &data);
                if (rc) {
                    if (DBI_RC_RETRY == rc) {
                        if (db_txn) {
                            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_lookup_dn",
                                          "Cursor get deadlock while under txn -> failure\n");
                        } else {
                            /* try again */
                            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_lookup_dn",
                                          "Cursor deadlockrf, trying again.\n");
                            goto retry_get1;
                        }
                    } else if (DBI_RC_NOTFOUND != rc) {
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
        _ENTRYRDN_DUMP_RDN_ELEM(elem);
        slapi_ch_free_string(&nrdn);
        nrdn = slapi_ch_strdup(elem->rdn_elem_nrdn_rdn);
        workid = id_stored_to_internal(elem->rdn_elem_id);
        /* 1 is byref, and the dup'ed rdn is freed with srdn */
        slapi_rdn_add_rdn_to_all_rdns(srdn, slapi_ch_strdup(RDN_ADDR(elem)), 1);
        dblayer_value_free(be, &data);
        dblayer_value_init(be, &data);
    } while (workid);

    if (0 == workid) {
        rc = -1;
    }

bail:
    dblayer_value_free(be, &data);
    dblayer_value_free(be, &key);
    /* Close the cursor */
    for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
        int myrc = dblayer_cursor_op(&cursor, DBI_OP_CLOSE, NULL, NULL);
        if (0 != myrc) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(myrc), "entryrdn_lookup_dn",
                          "Failed to close cursor: %s(%d)\n", dblayer_strerror(myrc), myrc);
            if (DBI_RC_RETRY == myrc) {
#ifdef FIX_TXN_DEADLOCKS
#error if txn != NULL, have to retry the entire transaction
#endif
                ENTRYRDN_DELAY;
                continue;
            }
            if (!rc) {
                /* if cursor close returns DEADLOCK, we must bubble that up
                   to the higher layers for retries */
                rc = myrc;
                break;
            }
        } else {
            break; /* success */
        }
    }
    /* it is guaranteed that db is not NULL. */
    dblayer_release_index_file(be, ai, db);
    if (psrdn) {
        *psrdn = srdn;
    } else {
        slapi_rdn_free(&srdn);
    }
    slapi_ch_free_string(&nrdn);
    slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_lookup_dn",
                  "<-- entryrdn_lookup_dn\n");
    return rc;
}

/*
 * Input: (rdn, id)
 * Output: (prdn, pid)
 *
 * If Input is a suffix, the Output is also a suffix.
 * If the rc is DBI_RC_NOTFOUND, the index is empty.
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
    dbi_db_t *db = NULL;
    dbi_cursor_t cursor = {0};
    dbi_txn_t *db_txn = (txn != NULL) ? txn->back_txn_txn : NULL;
    dbi_val_t key = {0};
    dbi_val_t data = {0};
    char *keybuf = NULL;
    char *orignrdn = NULL;
    char *nrdn = NULL;
    size_t nrdn_len = 0;
    rdn_elem *elem = NULL;
    int db_retry = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_get_parent",
                  "--> entryrdn_get_parent\n");

    /* Initialize data */
    memset(&data, 0, sizeof(data));

    if (NULL == be || NULL == rdn || 0 == id || NULL == prdn || NULL == pid) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_get_parent",
                      "Param error: Empty %s\n",
                      NULL == be ? "backend" : NULL == rdn ? "rdn" : 0 == id ? "id" : NULL == rdn ? "rdn container" : NULL == pid ? "pid" : "unknown");
        return rc;
    }
    *prdn = NULL;
    *pid = 0;

    /* Open the entryrdn index */
    rc = _entryrdn_open_index(be, &ai, &db);
    if (rc || (NULL == db)) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_get_parent",
                      "Opening the index failed: %s(%d)\n",
                      rc < 0 ? dblayer_strerror(rc) : "Invalid parameter", rc);
        return rc;
    }

    /* Make a cursor */
    for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
        rc = dblayer_new_cursor(be, db, db_txn, &cursor);
        if (rc) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_get_parent",
                          "Failed to make a cursor: %s(%d)\n", dblayer_strerror(rc), rc);
            if (DBI_RC_RETRY == rc) {
#ifdef FIX_TXN_DEADLOCKS
#error if txn != NULL, have to retry the entire transaction
#endif
                ENTRYRDN_DELAY;
                continue;
            }
            goto bail;
        } else {
            break; /* success */
        }
    }
    orignrdn = slapi_ch_strdup(rdn);
    rc = slapi_dn_normalize_case_ext(orignrdn, 0, &nrdn, &nrdn_len);
    if (rc < 0) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_get_parent",
                      "Failed to normalize %s\n", rdn);
        goto bail;
    }
    if (rc == 0) { /* orignrdn is passed in */
        *(nrdn + nrdn_len) = '\0';
    } else {
        slapi_ch_free_string(&orignrdn);
    }

    dblayer_value_init(be, &data);

    /* Setting up a key for the node to get its parent */
    keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_PARENT, id);
    dblayer_value_set(be, &key, keybuf, strlen(keybuf) + 1);

/* Position cursor at the matching key */
retry_get0:
    rc = dblayer_cursor_op(&cursor, DBI_OP_MOVE_TO_KEY, &key, &data);
    if (rc) {
        if (DBI_RC_RETRY == rc) {
            if (db_txn) {
                slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_get_parent",
                              "Cursor get deadlock while under txn -> failure\n");
            } else {
                slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_get_parent",
                              "Cursor deadlocked, trying again.\n");
                /* try again */
                goto retry_get0;
            }
        } else if (DBI_RC_NOTFOUND == rc) { /* could be a suffix
                                           note: no parent for suffix */
            keybuf = slapi_ch_smprintf("%s", nrdn);
            dblayer_value_set(be, &key, keybuf, strlen(keybuf) + 1);
        retry_get1:
            rc = dblayer_cursor_op(&cursor, DBI_OP_MOVE_TO_KEY, &key, &data);
            if (rc) {
                if (DBI_RC_RETRY == rc) {
                    if (db_txn) {
                        slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_get_parent",
                                      "Cursor get deadlock while under txn -> failure\n");
                    } else {
                        /* try again */
                        slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_get_parent",
                                      "Cursor deadlocked, trying again.\n");
                        goto retry_get1;
                    }
                } else if (DBI_RC_NOTFOUND != rc) {
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
    _ENTRYRDN_DUMP_RDN_ELEM(elem);
    *pid = id_stored_to_internal(elem->rdn_elem_id);
    *prdn = slapi_ch_strdup(RDN_ADDR(elem));
bail:
    slapi_ch_free_string(&nrdn);
    dblayer_value_free(be, &key);
    dblayer_value_free(be, &data);
    /* Close the cursor */
    for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
        int myrc = dblayer_cursor_op(&cursor, DBI_OP_CLOSE, NULL, NULL);
        if (0 != myrc) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(myrc), "entryrdn_get_parent",
                          "Failed to close cursor: %s(%d)\n", dblayer_strerror(myrc), myrc);
            if (DBI_RC_RETRY == myrc) {
#ifdef FIX_TXN_DEADLOCKS
#error if txn != NULL, have to retry the entire transaction
#endif
                ENTRYRDN_DELAY;
                continue;
            }
            if (!rc) {
                /* if cursor close returns DEADLOCK, we must bubble that up
                   to the higher layers for retries */
                rc = myrc;
                break;
            }
        } else {
            break; /* success */
        }
    }
    /* it is guaranteed that db is not NULL. */
    dblayer_release_index_file(be, ai, db);
    slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_get_parent",
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
    rdn_elem *re = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "_entryrdn_new_rdn_elem",
                  "--> _entryrdn_new_rdn_elem\n");
    if (NULL == srdn || NULL == be) {
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_new_rdn_elem",
                      "Empty %s\n", NULL == srdn ? "RDN" : NULL == be ? "backend" : "unknown");
        *length = 0;
        return NULL;
    }

    rdn = slapi_rdn_get_rdn(srdn);
    nrdn = slapi_rdn_get_nrdn(srdn);

    if (NULL == rdn || NULL == nrdn) {
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_new_rdn_elem",
                      "Empty rdn (%s) or normalized rdn (%s)\n", rdn ? rdn : "",
                      nrdn ? nrdn : "");
        *length = 0;
        return NULL;
    }
    re = entryrdn_encode_data(be, length, id, nrdn, rdn);

    slapi_log_err(SLAPI_LOG_TRACE, "_entryrdn_new_rdn_elem",
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
_entryrdn_dump_rdn_elem(int lineno, const char *name, rdn_elem *elem)
{
    slapi_log_err(SLAPI_LOG_DEBUG, "_entryrdn_dump_rdn_elem", "line %d: RDN ELEMENT %s : 0x%lx\n", lineno, name, (long)elem);
    if (NULL == elem) {
        slapi_log_err(SLAPI_LOG_DEBUG, "_entryrdn_dump_rdn_elem", "RDN ELEMENT: empty\n");
        return;
    }
    slapi_log_err(SLAPI_LOG_DEBUG, "_entryrdn_dump_rdn_elem", "    ID: %u\n",
                  id_stored_to_internal(elem->rdn_elem_id));
    slapi_log_err(SLAPI_LOG_DEBUG, "_entryrdn_dump_rdn_elem", "    RDN: \"%s\"\n",
                  RDN_ADDR(elem));
    slapi_log_err(SLAPI_LOG_DEBUG, "_entryrdn_dump_rdn_elem", "    RDN length: %lu\n",
                  sizeushort_stored_to_internal(elem->rdn_elem_rdn_len));
    slapi_log_err(SLAPI_LOG_DEBUG, "_entryrdn_dump_rdn_elem", "    Normalized RDN: \"%s\"\n",
                  elem->rdn_elem_nrdn_rdn);
    slapi_log_err(SLAPI_LOG_DEBUG, "_entryrdn_dump_rdn_elem", "    Normalized RDN length: %lu\n",
                  sizeushort_stored_to_internal(elem->rdn_elem_nrdn_len));
    return;
}
#endif

static int
_entryrdn_open_index(backend *be, struct attrinfo **ai, dbi_db_t **dbp)
{
    int rc = -1;
    ldbm_instance *inst = NULL;
    int dbopen_flags = DBOPEN_CREATE;

    if (NULL == be || NULL == ai || NULL == dbp) {
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_open_index",
                      "Param error: Empty %s\n",
                      NULL == be ? "be" : NULL == ai ? "attrinfo container" : NULL == dbp ? "db container" : "unknown");
        goto bail;
    }
    *ai = NULL;
    *dbp = NULL;
    /* Open the entryrdn index */
    ainfo_get(be, LDBM_ENTRYRDN_STR, ai);
    if (NULL == *ai) {
        /*
         * ENODATA exists on linux, but not other platforms. Change to -1, as
         * all callers to this function only ever check != 0.
         */
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_open_index", "EntryRDN str for attrinfo is null, unable to proceed.\n");
        rc = -1;
        goto bail;
    }
    inst = (ldbm_instance *)be->be_instance_info;
    if ((*ai)->ai_attrcrypt && entryrdn_warning_on_encryption) {
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_open_index",
                      "Encrypting entryrdn is not supported.  "
                      "Ignoring the configuration entry \"dn: "
                      "cn=entryrdn, cn=encrypted attributes, cn=<backend>, "
                      "cn=%s, cn=plugins, cn=config\"\n",
                      inst->inst_li->li_plugin->plg_name);

        entryrdn_warning_on_encryption = 0;
    }
    if (slapi_be_is_flag_set(be, SLAPI_BE_FLAG_POST_IMPORT)) {
        /* We are doing an import so the db instance is probably flagged as dirty */
        dbopen_flags |= DBOPEN_ALLOW_DIRTY;
    }
    rc = dblayer_get_index_file(be, *ai, dbp, dbopen_flags);
bail:
    return rc;
}

/* Notes:
 * 1) data->data must be located in the data area (not in the stack).
 *    If c_get reallocate the memory, the given data is freed.
 * 2) output elem returns data->data regardless of the result (success|failure)
 */
static int
_entryrdn_get_elem(dbi_cursor_t *cursor,
                   dbi_val_t *key,
                   dbi_val_t *data,
                   const char *comp_key,
                   rdn_elem **elem,
                   dbi_txn_t *db_txn)
{
    int rc = 0;

    if (NULL == cursor || NULL == key || NULL == data || NULL == elem ||
        NULL == comp_key) {
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_get_elem",
                      "Param error: Empty %s\n",
                      NULL == cursor ? "cursor" : NULL == key ? "key" : NULL == data ? "data" : NULL == elem ? "elem container" : NULL == comp_key ? "key to compare" : "unknown");
        return DBI_RC_INVALID;
    }
    slapi_log_err(SLAPI_LOG_TRACE, "_entryrdn_get_elem", "--> _entryrdn_get_elem (key=%s)\n", (char*)(key->data));
    /* Position cursor at the matching key */
    *elem = NULL;
retry_get:
    rc = dblayer_cursor_op(cursor, DBI_OP_MOVE_NEAR_DATA, key, data);
    *elem = (rdn_elem *)data->data;
    dblayer_value_init(cursor->be, data);

    if (rc) {
        if (DBI_RC_RETRY == rc) {
            if (db_txn) {
                slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "_entryrdn_get_elem",
                              "Cursor get deadlock while under txn -> failure\n");
            } else {
                slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "_entryrdn_get_elem",
                              "Cursor deadlocked, trying again.\n");
                /* try again */
                goto retry_get;
            }
        } else if (DBI_RC_BUFFER_SMALL == rc) {
            /* Could not happen any more because data can be realloced */
            PR_ASSERT(0);
            /* try again */
            goto retry_get;
        } else if (DBI_RC_NOTFOUND != rc) {
            _entryrdn_cursor_print_error("_entryrdn_get_elem",
                                         key->data, data->size, data->ulen, rc);
        }
        goto bail;
    }
    if (0 != strcmp(comp_key, (char *)(*elem)->rdn_elem_nrdn_rdn)) {
        /* the exact element was not found */
        rc = DBI_RC_NOTFOUND;
        goto bail;
    }
bail:
    if (*elem) {
        slapi_log_err(SLAPI_LOG_TRACE, "_entryrdn_get_elem", "<-- _entryrdn_get_elem (*elem rdn=%s)\n",
                      RDN_ADDR(*elem));
    } else {
        slapi_log_err(SLAPI_LOG_TRACE, "_entryrdn_get_elem", "<-- _entryrdn_get_elem (*elem NULL)\n");
    }
    return rc;
}

static int
_entryrdn_get_tombstone_elem(dbi_cursor_t *cursor,
                             Slapi_RDN *srdn,
                             dbi_val_t *key,
                             const char *comp_key,
                             rdn_elem **elem,
                             dbi_txn_t *db_txn)
{
    int rc = 0;
    dbi_bulk_t data = {0};
    rdn_elem *childelem = NULL;
    char buffer[RDN_BULK_FETCH_BUFFER_SIZE];
    backend *be;

    if (NULL == cursor || NULL == srdn || NULL == key || NULL == elem ||
        NULL == comp_key) {
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_get_tombstone_elem",
                      "Param error: Empty %s\n",
                      NULL == cursor ? "cursor" : NULL == key ? "key" : NULL == srdn ? "srdn" : NULL == elem ? "elem container" : NULL == comp_key ? "key to compare" : "unknown");
        return DBI_RC_INVALID;
    }
    be = cursor->be;
    slapi_log_err(SLAPI_LOG_TRACE, "_entryrdn_get_tombstone_elem",
                  "--> _entryrdn_get_tombstone_elem\n");
    *elem = NULL;

    /* get the child elems */
    /* Setting the bulk fetch buffer */
    dblayer_bulk_set_buffer(be, &data, buffer, sizeof(buffer), DBI_VF_BULK_DATA);

retry_get0:
    rc = dblayer_cursor_bulkop(cursor, DBI_OP_MOVE_TO_KEY, key, &data);
    if (DBI_RC_RETRY == rc) {
        if (db_txn) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "_entryrdn_get_tombstone_elem",
                          "Cursor get deadlock while under txn -> failure\n");
            goto bail;
        } else {
            /* try again */
            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "_entryrdn_get_tombstone_elem",
                          "Cursor deadlocked, trying again.\n");
            goto retry_get0;
        }

    } else if (DBI_RC_NOTFOUND == rc) {
        rc = 0; /* Child not found is ok */
        goto bail;
    } else if (rc) {
        _entryrdn_cursor_print_error("_entryrdn_get_tombstone_elem",
                                     key->data, data.v.size, data.v.ulen, rc);
        goto bail;
    }

    do {
        dbi_val_t dataret = {0};
        char *childnrdn = NULL;
        char *comma = NULL;

        dblayer_value_init(be, &dataret);
        for (dblayer_bulk_start(&data); DBI_RC_SUCCESS == dblayer_bulk_nextdata(&data, &dataret);) {
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
        }
    retry_get1:
        rc = dblayer_cursor_bulkop(cursor, DBI_OP_NEXT_DATA, key, &data);
        if (DBI_RC_RETRY == rc) {
            if (db_txn) {
                slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "_entryrdn_get_tombstone_elem",
                              "Cursor get deadlock while under txn -> failure\n");
                goto bail;
            } else {
                /* try again */
                slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "_entryrdn_get_tombstone_elem",
                              "Cursor deadlocked, trying again.\n");
                goto retry_get1;
            }
        } else if (DBI_RC_NOTFOUND == rc) {
            rc = 0;
            goto bail; /* done */
        } else if (rc) {
            _entryrdn_cursor_print_error("_entryrdn_get_tombstone_elem",
                                         key->data, data.v.size, data.v.ulen, rc);
            goto bail;
        }
    } while (0 == rc);

bail:
    slapi_log_err(SLAPI_LOG_TRACE, "_entryrdn_get_tombstone_elem",
                  "<-- _entryrdn_get_tombstone_elem\n");
    return rc;
}

static int
_entryrdn_put_data(dbi_cursor_t *cursor, dbi_val_t *key, dbi_val_t *data, char type, dbi_txn_t *db_txn)
{
    int rc = -1;
    int db_retry = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "_entryrdn_put_data",
                  "--> _entryrdn_put_data\n");
    if (NULL == cursor || NULL == key || NULL == data) {
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_put_data",
                      "Param error: Empty %s\n",
                      NULL == cursor ? "cursor" : NULL == key ? "key" : NULL == data ? "data" : "unknown");
        goto bail;
    }
    /* insert it */
    for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
        rc = dblayer_cursor_op(cursor, DBI_OP_ADD, key, data);
        if (rc) {
            if (DBI_RC_KEYEXIST == rc) {
                /* this is okay, but need to return DBI_RC_KEYEXIST to caller */
                slapi_log_err(SLAPI_LOG_BACKLDBM, "_entryrdn_put_data",
                              "The same key (%s) and the data exists in index\n",
                              (char *)key->data);
                break;
            } else {
                char *keyword = NULL;
                if (type == RDN_INDEX_CHILD) {
                    keyword = "child";
                } else if (type == RDN_INDEX_PARENT) {
                    keyword = "parent";
                } else {
                    keyword = "self";
                }
                slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "_entryrdn_put_data",
                              "Adding the %s link (%s) failed: %s (%d)\n", keyword, (char *)key->data,
                              dblayer_strerror(rc), rc);
                if ((DBI_RC_RETRY == rc) && !db_txn) {
                    ENTRYRDN_DELAY;
                    continue;
                }
                goto bail;
            }
        } else {
            break; /* success */
        }
    }
    if (RETRY_TIMES == db_retry) {
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_put_data",
                      "Cursor put operation failed after [%d] retries\n",
                      db_retry);
        rc = DBI_RC_RETRY;
    }
bail:
    slapi_log_err(SLAPI_LOG_TRACE, "_entryrdn_put_data", "<-- _entryrdn_put_data\n");
    return rc;
}

static int
_entryrdn_del_data(dbi_cursor_t *cursor, dbi_val_t *key, dbi_val_t *data, dbi_txn_t *db_txn)
{
    int rc = -1;
    int db_retry = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "_entryrdn_del_data",
                  "--> _entryrdn_del_data\n");
    if (NULL == cursor || NULL == key || NULL == data) {
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_del_data",
                      "Param error: Empty %s\n",
                      NULL == cursor ? "cursor" : NULL == key ? "key" : NULL == data ? "data" : "unknown");
        goto bail;
    }

    for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
        rc = dblayer_cursor_op(cursor, DBI_OP_MOVE_TO_DATA, key, data);
        if (rc) {
            if ((DBI_RC_RETRY == rc) && !db_txn) {
                slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "_entryrdn_del_data",
                              "Cursor deadlocked, trying again.\n");
                /* try again */
            } else if (DBI_RC_NOTFOUND == rc) {
                rc = 0; /* not found is ok */
                goto bail;
            } else {
                _entryrdn_cursor_print_error("_entryrdn_del_data",
                                             key->data, data->size, data->ulen, rc);
                goto bail;
            }
        } else {
            break; /* found it */
        }
    }
    if (RETRY_TIMES == db_retry) {
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_del_data",
                      "Cursor get failed after [%d] retries\n",
                      db_retry);
        rc = DBI_RC_RETRY;
        goto bail;
    }

    /* We found it, so delete it */
    for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
        rc = dblayer_cursor_op(cursor, DBI_OP_DEL, NULL, NULL);
        if (rc) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "_entryrdn_del_data",
                          "Deleting %s failed; %s(%d)\n", (char *)key->data,
                          dblayer_strerror(rc), rc);
            if ((DBI_RC_RETRY == rc) && !db_txn) {
                ENTRYRDN_DELAY;
                continue;
            }
            goto bail;
        } else {
            break; /* success */
        }
    }
    if (RETRY_TIMES == db_retry) {
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_del_data",
                      "Cursor del failed after [%d] retries\n",
                      db_retry);
        rc = DBI_RC_RETRY;
    }
bail:
    slapi_log_err(SLAPI_LOG_TRACE, "_entryrdn_del_data",
                  "<-- _entryrdn_del_data\n");
    return rc;
}

/* Child is a Leaf RDN to be added */
static int
_entryrdn_insert_key_elems(backend *be,
                           dbi_cursor_t *cursor,
                           Slapi_RDN *srdn,
                           dbi_val_t *key,
                           rdn_elem *parentelem,
                           rdn_elem *elem,
                           size_t elemlen,
                           dbi_txn_t *db_txn)
{
    /* We found a place to add RDN. */
    dbi_val_t adddata = {0};
    char *keybuf = NULL;
    size_t len = 0;
    int rc = 0;
    ID myid = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "_entryrdn_insert_key_elems",
                  "--> _entryrdn_insert_key_elems\n");

    if (NULL == be || NULL == cursor || NULL == srdn ||
        NULL == key || NULL == parentelem || NULL == elem) {
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_insert_key_elems",
                      "Param error: Empty %s\n",
                      NULL == be ? "backend" : NULL == cursor ? "cursor" : NULL == srdn ? "RDN" : NULL == key ? "key" : NULL == parentelem ? "parent element" : NULL == elem ? "target element" : "unknown");
        goto bail;
    }
    _ENTRYRDN_DUMP_RDN_ELEM(elem);
    dblayer_value_set_buffer(be, &adddata, elem, elemlen);

    /* adding RDN to the child key */
    rc = _entryrdn_put_data(cursor, key, &adddata, RDN_INDEX_CHILD, db_txn);
    if (rc && (DBI_RC_KEYEXIST != rc)) { /* failed && ignore already exists */
        goto bail;
    }

    myid = id_stored_to_internal(elem->rdn_elem_id);

    /* adding RDN to the self key */
    keybuf = slapi_ch_smprintf("%u", myid);
    dblayer_value_set(be, key, keybuf, strlen(keybuf) + 1);

    rc = _entryrdn_put_data(cursor, key, &adddata, RDN_INDEX_SELF, db_txn);
    if (rc && (DBI_RC_KEYEXIST != rc)) { /* failed && ignore already exists */
        goto bail;
    }

    /* adding RDN to the parent key */
    keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_PARENT, myid);
    dblayer_value_set(be, key, keybuf, strlen(keybuf) + 1);

    _ENTRYRDN_DUMP_RDN_ELEM(parentelem);
    len = _entryrdn_rdn_elem_size(parentelem);
    dblayer_value_set_buffer(be, &adddata, parentelem, len);
    /* adding RDN to the self key */
    rc = _entryrdn_put_data(cursor, key, &adddata, RDN_INDEX_PARENT, db_txn);
    if (DBI_RC_KEYEXIST == rc) { /* failed && ignore already exists */
        rc = 0;
    }
/* Succeeded or failed, it's done. */
bail:
    dblayer_value_free(be, key);
    slapi_log_err(SLAPI_LOG_TRACE, "_entryrdn_insert_key_elems",
                  "<-- _entryrdn_insert_key_elems\n");
    return rc;
}

/*
 * Helper function to replace a temporary id assigned to suffix id.
 */
static int
_entryrdn_replace_suffix_id(dbi_cursor_t *cursor, dbi_val_t *key, dbi_val_t *adddata, ID id, const char *normsuffix, dbi_txn_t *db_txn)
{
    int rc = 0;
    char *keybuf = NULL;
    char *realkeybuf = NULL;
    dbi_val_t realkey = {0};
    char buffer[RDN_BULK_FETCH_BUFFER_SIZE];
    dbi_bulk_t data = {0};
    dbi_val_t moddata = {0};
    rdn_elem **childelems = NULL;
    rdn_elem **cep = NULL;
    rdn_elem *childelem = NULL;
    size_t childnum = 4;
    size_t curr_childnum = 0;
    int db_retry = 0;
    backend *be = cursor->be;

    /* temporary id added for the non exisiting suffix */
    /* Let's replace it with the real entry ID */
    /* SELF */
    for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
        rc = dblayer_cursor_op(cursor, DBI_OP_REPLACE, key, adddata);
        if (rc) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(rc),
                          "_entryrdn_replace_suffix_id",
                          "Adding suffix %s failed: %s (%d)\n",
                          normsuffix, dblayer_strerror(rc), rc);
            if ((DBI_RC_RETRY == rc) && !db_txn) {
                ENTRYRDN_DELAY;
                continue;
            }
            goto bail;
        } else {
            break; /* success */
        }
    }
    if (RETRY_TIMES == db_retry) {
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_replace_suffix_id",
                      "Cursor put failed after [%d] retries\n",
                      db_retry);
        rc = DBI_RC_RETRY;
        goto bail;
    }

    /*
     * Fixing Child link:
     * key: C0:Suffix --> C<realID>:Suffix
     */
    /* E.g., C1 */
    keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, TMPID);
    dblayer_value_set(be, key, keybuf, strlen(keybuf) + 1);

    /* Setting the bulk fetch buffer */
    dblayer_bulk_set_buffer(be, &data, buffer, sizeof(buffer), DBI_VF_BULK_DATA);

    realkeybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, id);
    dblayer_value_set_buffer(be, &realkey, realkeybuf, strlen(realkeybuf) + 1);
    dblayer_value_init(be, &moddata);

    for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
        rc = dblayer_cursor_bulkop(cursor, DBI_OP_MOVE_TO_KEY, key, &data);
        if ((DBI_RC_RETRY == rc) && !db_txn) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "_entryrdn_replace_suffix_id",
                          "Cursor get deadlocked, trying again.\n");
            /* try again */
            ENTRYRDN_DELAY;
        } else if (rc) {
            _entryrdn_cursor_print_error("_entryrdn_replace_suffix_id",
                                         key->data, data.v.size, data.v.ulen, rc);
            goto bail;
        } else {
            break; /* found */
        }
    }
    if (RETRY_TIMES == db_retry) {
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_replace_suffix_id",
                      "Cursor get1 failed after [%d] retries\n",
                      db_retry);
        rc = DBI_RC_RETRY;
        goto bail;
    }
    childelems = (rdn_elem **)slapi_ch_calloc(childnum, sizeof(rdn_elem *));
    do {
        dbi_val_t dataret = {0};
        dblayer_value_init(be, &dataret);
        for(dblayer_bulk_start(&data); DBI_RC_SUCCESS == dblayer_bulk_nextdata(&data, &dataret);) {
            _entryrdn_dup_rdn_elem((const void *)dataret.data, &childelem);
            dblayer_value_set_buffer(be, &moddata, childelem, _entryrdn_rdn_elem_size(childelem));
            /* Delete it first */
            rc = _entryrdn_del_data(cursor, key, &moddata, db_txn);
            if (rc) {
                goto bail0;
            }
            /* Add it back */
            rc = _entryrdn_put_data(cursor, &realkey, &moddata,
                                    RDN_INDEX_CHILD, db_txn);
            if (rc && (DBI_RC_KEYEXIST != rc)) { /* failed && ignore already exists */
                goto bail0;
            }
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
        }

        for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
            rc = dblayer_cursor_bulkop(cursor, DBI_OP_NEXT_DATA, key, &data);
            if ((DBI_RC_RETRY == rc) && !db_txn) {
                slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "_entryrdn_replace_suffix_id",
                              "Retry cursor get deadlock\n");
                /* try again */
                ENTRYRDN_DELAY;
            } else if (!rc || (DBI_RC_NOTFOUND == rc)) {
                break; /* done */
            } else {
                _entryrdn_cursor_print_error("_entryrdn_replace_suffix_id",
                                             key->data, data.v.size, data.v.ulen, rc);
                goto bail0;
            }
        }
        if (RETRY_TIMES == db_retry) {
            slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_replace_suffix_id",
                          "Cursor get2 failed after [%d] retries\n",
                          db_retry);
            rc = DBI_RC_RETRY;
            goto bail0;
        }
        if (DBI_RC_NOTFOUND == rc) {
            rc = 0; /* ok */
            break;  /* we're done */
        }
    } while (0 == rc);

    /*
     * Fixing Children's parent link:
     * key:  P<childID>:<childRDN>  --> P<childID>:<childRDN>
     * data: 0                      --> <realID>
     */
    for (cep = childelems; cep && *cep; cep++) {
        rdn_elem *pelem = NULL;
        /* E.g., P1 */
        keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_PARENT,
                                   id_stored_to_internal((*cep)->rdn_elem_id));
        dblayer_value_set(be, key, keybuf, strlen(keybuf) + 1);
        dblayer_value_init(be, &moddata);

        /* Position cursor at the matching key */
        for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
            rc = dblayer_cursor_op(cursor, DBI_OP_MOVE_TO_KEY, key, &moddata);
            if (rc) {
                if ((DBI_RC_RETRY == rc) && !db_txn) {
                    slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "_entryrdn_replace_suffix_id",
                                  "Retry2 cursor get deadlock\n");
                    ENTRYRDN_DELAY;
                } else {
                    _entryrdn_cursor_print_error("_entryrdn_replace_suffix_id",
                                                 key->data, data.v.size, data.v.ulen, rc);
                    goto bail0;
                }
            } else {
                break;
            }
        }
        if (RETRY_TIMES == db_retry) {
            slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_replace_suffix_id",
                          "Cursor get3 failed after [%d] retries\n",
                          db_retry);
            rc = DBI_RC_RETRY;
            goto bail0;
        }
        pelem = (rdn_elem *)moddata.data;
        if (TMPID == id_stored_to_internal(pelem->rdn_elem_id)) {
            /* the parent id is TMPID;
             * replace it with the given id */
            id_internal_to_stored(id, pelem->rdn_elem_id);
            for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
                rc = dblayer_cursor_op(cursor, DBI_OP_REPLACE, key, &moddata);
                if (rc) {
                    slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "_entryrdn_replace_suffix_id",
                                  "Fixing the parent link (%s) failed: %s (%d)\n",
                                  keybuf, dblayer_strerror(rc), rc);
                    if ((DBI_RC_RETRY == rc) && !db_txn) {
                        ENTRYRDN_DELAY;
                        continue;
                    }
                    goto bail0;
                } else {
                    break; /* success */
                }
            }
            if (RETRY_TIMES == db_retry) {
                slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_replace_suffix_id",
                              "Cursor put failed after [%d] retries\n", db_retry);
                rc = DBI_RC_RETRY;
                goto bail0;
            }
        }
        dblayer_value_free(be, &moddata);
    } /* for (cep = childelems; cep && *cep; cep++) */
bail0:
    for (cep = childelems; cep && *cep; cep++) {
        slapi_ch_free((void **)cep);
    }
    slapi_ch_free((void **)&childelems);
bail:
    dblayer_value_free(be, key);
    dblayer_value_free(be, &moddata);
    return rc;
}

/*
 * This function starts from the suffix following the child links to the bottom.
 * If the target leaf node does not exist, the nodes (the child link of the
 * parent node and the self link) are added.
 */
int
entryrdn_insert_key(backend *be,
                     dbi_cursor_t *cursor,
                     Slapi_RDN *srdn,
                     ID id,
                     back_txn *txn)
{
    dbi_txn_t *db_txn = (txn != NULL) ? txn->back_txn_txn : NULL;
    int rc = -1;
    size_t len = 0;
    const char *nrdn = NULL;      /* normalized rdn */
    const char *childnrdn = NULL; /* normalized child rdn */
    int rdnidx = -1;
    char *keybuf = NULL;
    dbi_val_t key = {0};
    dbi_val_t data = {0};
    ID workid = 0;
    rdn_elem *elem = NULL;
    rdn_elem *childelem = NULL;
    rdn_elem *parentelem = NULL;
    rdn_elem *tmpelem = NULL;
    Slapi_RDN *tmpsrdn = NULL;
    int db_retry = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_insert_key",
                  "--> _entryrdn_insert_key\n");

    if (NULL == be || NULL == cursor || NULL == srdn || 0 == id) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_insert_key",
                      "Param error: Empty %s\n",
                      NULL == be ? "backend" : NULL == cursor ? "cursor" : NULL == srdn ? "RDN" : 0 == id ? "id" : "unknown");
        goto bail;
    }

    if (txn && txn->back_special_handling_fn) {
        /* Ignore the cursor while handling import/index tasks (anyway foreman thread is associated with read/only txn)
         * we have better to have a entryrdn cursor in the import writer thread and use it
         */
        dblayer_value_set_buffer(be, &key, srdn, sizeof (Slapi_RDN));
        dblayer_value_set_buffer(be, &data, &id, sizeof id);
        return txn->back_special_handling_fn(be, BTXNACT_ENTRYRDN_ADD, NULL, &key, &data, txn);
    }

    /* get the top normalized rdn */
    rdnidx = slapi_rdn_get_last_ext(srdn, &nrdn, FLAG_ALL_NRDNS);
    if (rdnidx < 0 || NULL == nrdn) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_insert_key",
                      "Empty RDN\n");
        goto bail;
    }

    /* Setting up a key for suffix */
    dblayer_value_set_buffer(be, &key, (void*)nrdn, strlen(nrdn) + 1);

    if (0 == rdnidx) { /* "0 == rdnidx" means adding suffix */
        /* adding suffix RDN to the self key */
        dbi_val_t adddata = {0};
        elem = _entryrdn_new_rdn_elem(be, id, srdn, &len);
        if (NULL == elem) {
            slapi_log_err(SLAPI_LOG_ERR, "entryrdn_insert_key",
                          "Failed to generate an elem: "
                          "id: %d, rdn: %s\n",
                          id, slapi_rdn_get_rdn(srdn));
            goto bail;
        }
        _ENTRYRDN_DUMP_RDN_ELEM(elem);

        len = _entryrdn_rdn_elem_size(elem);
        dblayer_value_set_buffer(be, &adddata, elem, len);

        rc = _entryrdn_put_data(cursor, &key, &adddata, RDN_INDEX_SELF, db_txn);
        if (DBI_RC_KEYEXIST == rc) {
            dbi_val_t existdata = {0};
            rdn_elem *existelem = NULL;
            ID tmpid;
            dblayer_value_init(be, &existdata);
            for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
                rc = dblayer_cursor_op(cursor, DBI_OP_MOVE_TO_KEY, &key, &existdata);
                if (rc) {
                    slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_insert_key",
                                  "Get existing suffix %s failed: %s (%d)\n",
                                  nrdn, dblayer_strerror(rc), rc);
                    if ((DBI_RC_RETRY == rc) && !db_txn) {
                        ENTRYRDN_DELAY;
                        continue;
                    }
                    goto bail;
                } else {
                    break; /* success */
                }
            }
            if (RETRY_TIMES == db_retry) {
                slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_insert_key",
                              "Cursor get failed after [%d] retries\n", db_retry);
                rc = DBI_RC_RETRY;
                goto bail;
            }
            existelem = (rdn_elem *)existdata.data;
            tmpid = id_stored_to_internal(existelem->rdn_elem_id);
            dblayer_value_free(be, &existdata);
            if (TMPID == tmpid) {
                rc = _entryrdn_replace_suffix_id(cursor, &key, &adddata,
                                                 id, nrdn, db_txn);
                if (rc) {
                    goto bail;
                }
            } /* if (TMPID == tmpid) */
            rc = 0;
        } /* if (DBI_RC_KEYEXIST == rc) */
        slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_insert_key",
                      "Suffix %s added: %d\n",
                      nrdn, rc);
        goto bail; /* succeeded or failed, it's done */
    }

    /* (0 < rdnidx) */
    /* get id of the suffix */
    tmpsrdn = NULL;
    /* tmpsrdn == suffix'es srdn */
    rc = slapi_rdn_partial_dup(srdn, &tmpsrdn, rdnidx);
    if (rc) {
        char *dn = NULL;
        slapi_rdn_get_dn(srdn, &dn);
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_insert_key",
                      "Partial dup of %s (idx %d) failed (%d)\n", dn, rdnidx, rc);
        slapi_ch_free_string(&dn);
        goto bail;
    }
    elem = _entryrdn_new_rdn_elem(be, TMPID, tmpsrdn, &len);
    if (NULL == elem) {
        char *dn = NULL;
        slapi_rdn_get_dn(tmpsrdn, &dn);
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_insert_key",
                      "Failed to generate a new elem: dn: %s\n", dn);
        slapi_ch_free_string(&dn);
        goto bail;
    }

    dblayer_value_set_buffer(be, &data, elem, len);

    /* getting the suffix element */
    rc = _entryrdn_get_elem(cursor, &key, &data, nrdn, &elem, db_txn);
    if (rc) {
        const char *myrdn = slapi_rdn_get_nrdn(srdn);
        const char **ep = NULL;
        int isexception = 0;

        if ((rc == DBI_RC_RETRY) && db_txn) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_insert_key",
                          "Suffix \"%s\" cursor get fails: %s(%d)\n", nrdn, dblayer_strerror(rc), rc);
            goto bail;
        }
        /* Check the RDN is in the exception list */
        for (ep = rdn_exceptions; ep && *ep; ep++) {
            if (!strcmp(*ep, myrdn)) {
                isexception = 1;
                break;
            }
        }

        if (isexception) {
            /* adding suffix RDN to the self key */
            dbi_val_t adddata = {0};
            /* suffix ID = 0: fake ID to be replaced with the real one when
             * it's really added. */
            ID suffixid = TMPID;
            slapi_ch_free((void **)&elem);
            elem = _entryrdn_new_rdn_elem(be, suffixid, tmpsrdn, &len);
            if (NULL == elem) {
                slapi_log_err(SLAPI_LOG_ERR, "entryrdn_insert_key",
                              "Failed to generate an elem: id: %d, rdn: %s\n",
                              suffixid, slapi_rdn_get_rdn(tmpsrdn));
                goto bail;
            }
            _ENTRYRDN_DUMP_RDN_ELEM(elem);

            dblayer_value_set_buffer(be, &adddata, elem, len);
            rc = _entryrdn_put_data(cursor, &key, &adddata, RDN_INDEX_SELF, db_txn);
            slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_insert_key",
                          "Suffix %s added: %d\n", slapi_rdn_get_rdn(tmpsrdn), rc);
#ifdef FIX_TXN_DEADLOCKS
#error no checking for rc here? - what if rc is deadlock?  should bail?
#endif
        } else {
            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_insert_key",
                          "Suffix \"%s\" not found: %s(%d)\n", nrdn, dblayer_strerror(rc), rc);
            goto bail;
        }
    }
    slapi_rdn_free(&tmpsrdn);

    /* workid: ID of suffix */
    workid = id_stored_to_internal(elem->rdn_elem_id);
    parentelem = elem;
    _ENTRYRDN_DUMP_RDN_ELEM(parentelem);
    elem = NULL;

    do {
        /* Check the direct child in the RDN array, first */
        rdnidx = slapi_rdn_get_prev_ext(srdn, rdnidx,
                                        &childnrdn, FLAG_ALL_NRDNS);
        if ((rdnidx < 0) || (NULL == childnrdn)) {
            slapi_log_err(SLAPI_LOG_ERR, "entryrdn_insert_key",
                          "RDN list \"%s\" is broken: idx(%d)\n",
                          slapi_rdn_get_rdn(srdn), rdnidx);
            goto bail;
        }
        /* Generate a key for child tree */
        /* E.g., C1 */
        keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, workid);
        dblayer_value_set(be, &key, keybuf, strlen(keybuf) + 1);

        tmpsrdn = srdn;
        if (0 < rdnidx) {
            rc = slapi_rdn_partial_dup(srdn, &tmpsrdn, rdnidx);
            if (rc) {
                char *dn = NULL;
                slapi_rdn_get_dn(srdn, &dn);
                slapi_log_err(SLAPI_LOG_ERR, "entryrdn_insert_key",
                              "Partial dup of %s (idx %d) failed (%d)\n", dn, rdnidx, rc);
                slapi_ch_free_string(&dn);
                goto bail;
            }
        }
        elem = _entryrdn_new_rdn_elem(be, TMPID, tmpsrdn, &len);
        if (NULL == elem) {
            char *dn = NULL;
            slapi_rdn_get_dn(tmpsrdn, &dn);
            slapi_log_err(SLAPI_LOG_ERR, "entryrdn_insert_key",
                          "Failed to generate a new elem: dn: %s\n", dn);
            slapi_ch_free_string(&dn);
            goto bail;
        }

        _entryrdn_dup_rdn_elem((const void *)elem, &tmpelem);
        _ENTRYRDN_DUMP_RDN_ELEM(tmpelem);

        dblayer_value_set(be, &data, tmpelem, len);
        tmpelem = NULL;

        /* getting the child element */

        rc = _entryrdn_get_elem(cursor, &key, &data, childnrdn, &tmpelem, db_txn);
        if (rc) {
            slapi_ch_free((void **)&tmpelem);
            if ((rc == DBI_RC_RETRY) && db_txn) {
                slapi_log_err(ENTRYRDN_LOGLEVEL(rc),
                              "entryrdn_insert_key",
                              "Suffix \"%s\" cursor get fails: %s(%d)\n",
                              childnrdn, dblayer_strerror(rc), rc);
                goto bail;
            } else if (DBI_RC_NOTFOUND == rc) {
                /* if 0 == rdnidx, Child is a Leaf RDN to be added */
                if (0 == rdnidx) {
                    /* set id to the elem to be added */
                    id_internal_to_stored(id, elem->rdn_elem_id);
                    rc = _entryrdn_insert_key_elems(be, cursor, srdn, &key,
                                                    parentelem, elem, len, db_txn);
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
                                                      childnrdn, &tmpelem, db_txn);
                    if (rc) {
                        char *dn = NULL;
                        slapi_rdn_get_dn(tmpsrdn, &dn);
                        if (DBI_RC_NOTFOUND == rc) {
                            slapi_log_err(SLAPI_LOG_ERR, "entryrdn_insert_key",
                                          "Node \"%s\" not found: %s(%d)\n", dn, dblayer_strerror(rc), rc);
                        } else {
                            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_insert_key",
                                          "Getting \"%s\" failed: %s(%d)\n", dn, dblayer_strerror(rc), rc);
                        }
                        slapi_ch_free((void **)&tmpelem);
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
                        _ENTRYRDN_DUMP_RDN_ELEM(parentelem);
                    }
                }
            } else {
                char *dn = NULL;
                slapi_rdn_get_dn(tmpsrdn, &dn);
                slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_insert_key",
                              "Suffix \"%s\" not found: %s(%d)\n", nrdn, dblayer_strerror(rc), rc);
                slapi_ch_free_string(&dn);
                goto bail;
            }
        } else { /* rc == 0; succeeded to get an element */
            ID currid = 0;
            slapi_ch_free((void **)&elem);
            elem = tmpelem;
            tmpelem = NULL;
            _ENTRYRDN_DUMP_RDN_ELEM(elem);
            currid = id_stored_to_internal(elem->rdn_elem_id);
            if (0 == rdnidx) { /* Child is a Leaf RDN to be added */
                if (currid == id) {
                    /* already in the file */
                    /* do nothing and return. */
                    rc = 0;
                    slapi_log_err(SLAPI_LOG_BACKLDBM, "entryrdn_insert_key",
                                  "ID %d is already in the index. NOOP.\n", currid);
                } else { /* different id, error return */
                    char *dn = NULL;
                    int tmprc = slapi_rdn_get_dn(srdn, &dn);
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "entryrdn_insert_key",
                                  "Same DN (%s: %s) is already in the %s file with different ID "
                                  "%d.  Expected ID is %d.\n",
                                  tmprc ? "rdn" : "dn", tmprc ? childnrdn : dn,
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
                _ENTRYRDN_DUMP_RDN_ELEM(parentelem);
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
    _ENTRYRDN_DUMP_RDN_ELEM(elem);
    _ENTRYRDN_DUMP_RDN_ELEM(parentelem);
    _ENTRYRDN_DUMP_RDN_ELEM(childelem);
    slapi_ch_free((void **)&elem);
    slapi_ch_free((void **)&parentelem);
    slapi_ch_free((void **)&childelem);
    slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_insert_key",
                  "<-- entrydn_insert_key\n");
    dblayer_value_free(be, &key);
    dblayer_value_free(be, &data);
    return rc;
}

/*
 * This function checks the existence of the target self link (key ID:RDN;
 * value ID,RDN,normalized RDN). If it exists and it does not have child links,
 * then it deletes the parent's child link and the self link.
 */
int
entryrdn_delete_key(backend *be,
                     dbi_cursor_t *cursor,
                     Slapi_RDN *srdn,
                     ID id,
                     back_txn *txn)
{
    dbi_txn_t *db_txn = (txn != NULL) ? txn->back_txn_txn : NULL;
    int rc = -1;
    size_t len = 0;
    const char *nrdn = NULL;     /* normalized rdn */
    const char *suffix = NULL;   /* normalized suffix */
    char *parentnrdn = NULL;     /* normalized parent rdn */
    const char *selfnrdn = NULL; /* normalized parent rdn */
    int rdnidx = -1;
    int lastidx = -1;
    char *keybuf = NULL;
    dbi_val_t key = {0};
    dbi_val_t data = {0};
    dbi_bulk_t bulkdata = {0};
    ID workid = 0;
    rdn_elem *elem = NULL;
    int issuffix = 0;
    Slapi_RDN *tmpsrdn = NULL;
    int db_retry = 0;
    int done = 0;
    char buffer[RDN_BULK_FETCH_BUFFER_SIZE];

    slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_delete_key",
                  "--> entryrdn_delete_key\n");

    if (NULL == be || NULL == cursor || NULL == srdn || 0 == id) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_delete_key",
                      "Param error: Empty %s\n",
                      NULL == be ? "backend" : NULL == cursor ? "cursor" : NULL == srdn ? "RDN" : 0 == id ? "ID" : "unknown");
        goto bail;
    }

    if (txn && txn->back_special_handling_fn) {
        /* Ignore the cursor while handling import/index tasks (anyway foreman thread is associated with read/only txn)
         * we have better to have a entryrdn cursor in the import writer thread and use it
         */
        dblayer_value_set_buffer(be, &key, srdn, sizeof (Slapi_RDN));
        dblayer_value_set_buffer(be, &data, &id, sizeof id);
        return txn->back_special_handling_fn(be, BTXNACT_ENTRYRDN_DEL, NULL, &key, &data, txn);
    }


    /* get the bottom normalized rdn (target to delete) */
    rdnidx = slapi_rdn_get_first_ext(srdn, &nrdn, FLAG_ALL_NRDNS);
    /* rdnidx is supposed to be 0 */
    if (rdnidx < 0 || NULL == nrdn) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_delete_key",
                      "Empty RDN\n");
        goto bail;
    }
    lastidx = slapi_rdn_get_last_ext(srdn, &suffix, FLAG_ALL_NRDNS);
    if (0 == lastidx) {
        issuffix = 1;
        selfnrdn = suffix;
    } else if (lastidx < 0 || NULL == suffix) {
        slapi_log_err(SLAPI_LOG_ERR, "entryrdn_delete_key",
                      "Empty suffix\n");
        goto bail;
    }

    /* check if the target element has a child or not */
    keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, id);
    dblayer_value_set(be, &key, keybuf, strlen(keybuf) + 1);
    keybuf = NULL;

    /* Setting the bulk fetch buffer */
    dblayer_bulk_set_buffer(be, &bulkdata, buffer, sizeof(buffer), DBI_VF_BULK_DATA);

    done = 0;
    while (!done) {
        rc = dblayer_cursor_bulkop(cursor, DBI_OP_MOVE_TO_KEY, &key, &bulkdata);
        if (DBI_RC_RETRY == rc) {
            if (db_txn) {
                slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_delete_key",
                              "Cursor get deadlock while under txn -> failure\n");
                goto bail;
            } else {
                slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_delete_key",
                              "Cursor get deadlock, trying again.\n");
                /* try again */
                continue;
            }
        } else if (DBI_RC_NOTFOUND == rc) {
            /* no children; ok */
            done = 1;
            continue;
        } else if (rc) {
            _entryrdn_cursor_print_error("entryrdn_delete_key",
                                         key.data, bulkdata.v.size, bulkdata.v.ulen, rc);
            goto bail;
        }

        do {
            rdn_elem *childelem = NULL;
            dbi_val_t dataret = {0};
            dblayer_value_init(be, &dataret);
            for (dblayer_bulk_start(&bulkdata); DBI_RC_SUCCESS == dblayer_bulk_nextdata(&bulkdata, &dataret);) {
                childelem = (rdn_elem *)dataret.data;
                if (!slapi_is_special_rdn(childelem->rdn_elem_nrdn_rdn, RDN_IS_TOMBSTONE) &&
                    !strcasestr(childelem->rdn_elem_nrdn_rdn, "cenotaphid")) {
                    /* there's at least one live child */
                    slapi_log_err(SLAPI_LOG_ERR, "entryrdn_delete_key",
                                  "Failed to remove %s; has a child %s\n", nrdn,
                                  (char *)childelem->rdn_elem_nrdn_rdn);
                    rc = -1;
                    goto bail;
                }
            }
        retry_get:
            rc = dblayer_cursor_bulkop(cursor, DBI_OP_NEXT_DATA, &key, &bulkdata);
            if (DBI_RC_RETRY == rc) {
                if (db_txn) {
                    slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_delete_key",
                                  "Cursor get deadlock while under txn -> failure\n");
                    goto bail;
                } else {
                    slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_delete_key",
                                  "Cursor get deadlocked, trying again\n");
                    /* try again */
                    goto retry_get;
                }
            } else if (DBI_RC_NOTFOUND == rc) {
                rc = 0;
                done = 1;
                break;
            } else if (rc) {
                _entryrdn_cursor_print_error("entryrdn_delete_key",
                                             key.data, bulkdata.v.size, bulkdata.v.ulen, rc);
                goto bail;
            }
        } while (0 == rc);
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
                char *dn = NULL;
                slapi_rdn_get_dn(srdn, &dn);
                slapi_log_err(SLAPI_LOG_ERR, "entryrdn_delete_key",
                              "Partial dup of %s (idx %d) failed (%d)\n", dn, 1, rc);
                slapi_ch_free_string(&dn);
                goto bail;
            }
            elem = _entryrdn_new_rdn_elem(be, TMPID, tmpsrdn, &len);
            if (NULL == elem) {
                char *dn = NULL;
                slapi_rdn_get_dn(tmpsrdn, &dn);
                slapi_log_err(SLAPI_LOG_ERR, "entryrdn_delete_key",
                              "Failed to generate a parent elem: dn: %s\n", dn);
                slapi_ch_free_string(&dn);
                slapi_rdn_free(&tmpsrdn);
                goto bail;
            }
        } else if (parentnrdn) {
            /* Then, the child link from the parent */
            keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, workid);
            elem = _entryrdn_new_rdn_elem(be, id, srdn, &len);
            if (NULL == elem) {
                char *dn = NULL;
                slapi_rdn_get_dn(srdn, &dn);
                slapi_log_err(SLAPI_LOG_ERR, "entryrdn_delete_key",
                              "Failed to generate a parent's child elem: dn: %s\n", dn);
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
                char *dn = NULL;
                slapi_rdn_get_dn(srdn, &dn);
                slapi_log_err(SLAPI_LOG_ERR, "entryrdn_delete_key",
                              "Failed to generate a target elem: dn: %s\n", dn);
                slapi_ch_free_string(&dn);
                goto bail;
            }
        }
        dblayer_value_set(be, &key, keybuf, strlen(keybuf) + 1);
        dblayer_value_protect_data(be, &key);
        dblayer_value_set(be, &data, elem, len);
        elem = NULL;

        /* Position cursor at the matching key */
        rc = _entryrdn_get_elem(cursor, &key, &data,
                                slapi_rdn_get_nrdn(tmpsrdn), &elem, db_txn);
        if (tmpsrdn != srdn) {
            slapi_rdn_free(&tmpsrdn);
        }
        if (rc) {
            if (DBI_RC_NOTFOUND == rc) {
                slapi_log_err(SLAPI_LOG_BACKLDBM, "entryrdn_delete_key",
                              "No parent link %s\n", keybuf);
                goto bail;
            } else {
                /* There's no parent or positioning at parent failed */
                _entryrdn_cursor_print_error("entryrdn_delete_key",
                                             key.data, data.size, data.ulen, rc);
                goto bail;
            }
        }

        if (NULL == parentnrdn && NULL == selfnrdn) {
/* First, deleting parent link */
            _ENTRYRDN_DUMP_RDN_ELEM(elem);
            parentnrdn = slapi_ch_strdup(elem->rdn_elem_nrdn_rdn);
            workid = id_stored_to_internal(elem->rdn_elem_id);

            /* deleteing the parent link */
            /* the cursor is set at the parent link by _entryrdn_get_elem */
            for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
                rc = dblayer_cursor_op(cursor, DBI_OP_DEL, NULL, NULL);
                if (rc && (DBI_RC_NOTFOUND != rc)) {
                    slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_delete_key",
                                  "Deleting %s failed; %s(%d)\n", (char *)key.data,
                                  dblayer_strerror(rc), rc);
                    if ((DBI_RC_RETRY == rc) && !db_txn) {
                        ENTRYRDN_DELAY; /* sleep for a bit then retry immediately */
                        continue;
                    }
                    goto bail; /* if deadlock and txn, have to abort entire txn */
                } else {
                    break; /* success */
                }
            }
            if (RETRY_TIMES == db_retry) {
                slapi_log_err(SLAPI_LOG_ERR, "entryrdn_delete_key",
                              "Delete parent link failed after [%d] retries\n",
                              db_retry);
                rc = DBI_RC_RETRY;
                goto bail;
            }
        } else if (parentnrdn) {
            _ENTRYRDN_DUMP_RDN_ELEM(elem);
            slapi_ch_free_string(&parentnrdn);
            /* deleteing the parent's child link */
            /* the cursor is set at the parent link by _entryrdn_get_elem */
            for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
                rc = dblayer_cursor_op(cursor, DBI_OP_DEL, NULL, NULL);
                if (rc && (DBI_RC_NOTFOUND != rc)) {
                    slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_delete_key",
                                  "Deleting %s failed; %s(%d)\n", (char *)key.data,
                                  dblayer_strerror(rc), rc);
                    if ((DBI_RC_RETRY == rc) && !db_txn) {
                        ENTRYRDN_DELAY;
                        continue;
                    }
                    goto bail; /* if deadlock and txn, have to abort entire txn */
                } else {
                    break; /* success */
                }
            }
            if (RETRY_TIMES == db_retry) {
                slapi_log_err(SLAPI_LOG_ERR, "entryrdn_delete_key",
                              "Delete parent's child link failed after [%d] retries\n",
                              db_retry);
                rc = DBI_RC_RETRY;
                goto bail;
            }
            selfnrdn = nrdn;
            workid = id;
        } else if (selfnrdn) {
            _ENTRYRDN_DUMP_RDN_ELEM(elem);
            /* deleteing the self link */
            /* the cursor is set at the parent link by _entryrdn_get_elem */
            for (db_retry = 0; db_retry < RETRY_TIMES; db_retry++) {
                rc = dblayer_cursor_op(cursor, DBI_OP_DEL, NULL, NULL);
                if (rc && (DBI_RC_NOTFOUND != rc)) {
                    slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "entryrdn_delete_key",
                                  "Deleting %s failed: %s(%d)\n", (char *)key.data,
                                  dblayer_strerror(rc), rc);
                    if ((DBI_RC_RETRY == rc) && !db_txn) {
                        ENTRYRDN_DELAY;
                        continue;
                    }
                    goto bail; /* if deadlock and txn, have to abort entire txn */
                } else {
                    break; /* success */
                }
            }
            if (RETRY_TIMES == db_retry) {
                slapi_log_err(SLAPI_LOG_ERR, "entryrdn_delete_key",
                              "Delete self link failed after [%d] retries\n",
                              db_retry);
                rc = DBI_RC_RETRY;
            }
            goto bail; /* done */
        }
    } while (workid);

bail:
    slapi_ch_free_string(&parentnrdn);
    dblayer_value_free(be, &key);
    dblayer_value_free(be, &data);
    slapi_ch_free((void **)&elem);
    slapi_ch_free_string(&keybuf);
    slapi_log_err(SLAPI_LOG_TRACE, "entryrdn_delete_key",
                  "<-- entryrdn_delete_key\n");
    return rc;
}

static int
_entryrdn_index_read(backend *be,
                     dbi_cursor_t *cursor,
                     Slapi_RDN *srdn,
                     rdn_elem **elem,
                     rdn_elem **parentelem,
                     rdn_elem ***childelems,
                     int flags,
                     dbi_txn_t *db_txn)
{
    int rc = -1;
    size_t len = 0;
    ID id;
    const char *nrdn = NULL;      /* normalized rdn */
    const char *childnrdn = NULL; /* normalized rdn */
    int rdnidx = -1;
    char *keybuf = NULL;
    dbi_val_t key = {0};
    dbi_val_t data = {0};
    dbi_bulk_t bulkdata = {0};
    size_t childnum = 32;
    size_t curr_childnum = 0;
    Slapi_RDN *tmpsrdn = NULL;
    rdn_elem *tmpelem = NULL;

    if (NULL == be || NULL == cursor ||
        NULL == srdn || NULL == elem) {
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_index_read",
                      "Param error: Empty %s\n",
                      NULL == be ? "backend" : NULL == cursor ? "cursor" : NULL == srdn ? "RDN" : NULL == elem ? "elem container" : "unknown");
        return DBI_RC_INVALID;
    }
    slapi_log_err(SLAPI_LOG_TRACE, "_entryrdn_index_read",
                  "--> _entryrdn_index_read (rdn=%s)\n", srdn->rdn);

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
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_index_read",
                      "Empty RDN (Suffix)\n");
        goto bail;
    }
    /* Setting up a key for suffix */
    keybuf = slapi_ch_smprintf("%s", nrdn);
    dblayer_value_set_buffer(be, &key, keybuf, strlen(keybuf) + 1);

    /* get id of the suffix */
    tmpsrdn = NULL;
    /* tmpsrdn == suffix'es srdn */
    rc = slapi_rdn_partial_dup(srdn, &tmpsrdn, rdnidx);
    if (rc) {
        char *dn = NULL;
        slapi_rdn_get_dn(srdn, &dn);
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_index_read",
                      "Partial dup of %s (idx %d) failed (%d)\n",
                      dn, rdnidx, rc);
        slapi_ch_free_string(&dn);
        goto bail;
    }
    *elem = _entryrdn_new_rdn_elem(be, TMPID, tmpsrdn, &len);
    if (NULL == *elem) {
        char *dn = NULL;
        slapi_rdn_get_dn(tmpsrdn, &dn);
        slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_index_read",
                      "Failed to generate a new elem: dn: %s\n", dn);
        slapi_ch_free_string(&dn);
        slapi_rdn_free(&tmpsrdn);
        goto bail;
    }

    dblayer_value_set(be, &data, *elem, len);

    /* getting the suffix element */
    rc = _entryrdn_get_elem(cursor, &key, &data, nrdn, elem, db_txn);
    if (rc || NULL == *elem) {
        slapi_ch_free((void **)elem);
        if ((rc == DBI_RC_RETRY) && db_txn) {
            slapi_log_err(SLAPI_LOG_BACKLDBM, "_entryrdn_index_read",
                          "Suffix \"%s\" cursor get fails: %s(%d)\n",
                          nrdn, dblayer_strerror(rc), rc);
            slapi_rdn_free(&tmpsrdn);
            goto bail;
        }
        if (flags & TOMBSTONE_INCLUDED) {
            /* Node might be a tombstone. */
            rc = _entryrdn_get_tombstone_elem(cursor, tmpsrdn,
                                              &key, nrdn, elem, db_txn);
            _ENTRYRDN_DUMP_RDN_ELEM_ARRAY(elem);
            rdnidx--; /* consider nsuniqueid=..,<RDN> one RDN */
        }
        if (rc || NULL == *elem) {
            slapi_log_err(SLAPI_LOG_BACKLDBM, "_entryrdn_index_read",
                          "Suffix \"%s\" not found: %s(%d)\n",
                          nrdn, dblayer_strerror(rc), rc);
            rc = DBI_RC_NOTFOUND;
            slapi_rdn_free(&tmpsrdn);
            goto bail;
        }
    }
    _ENTRYRDN_DUMP_RDN_ELEM(*elem);
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
                slapi_log_err(SLAPI_LOG_DEBUG, "_entryrdn_index_read",
                              "Done; DN %s => ID %d\n",
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
                char *dn = NULL;
                slapi_rdn_get_dn(srdn, &dn);
                slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_index_read",
                              "Partial dup of %s (idx %d) failed (%d)\n",
                              dn, rdnidx, rc);
                slapi_ch_free_string(&dn);
                goto bail;
            }
        }
        tmpelem = _entryrdn_new_rdn_elem(be, TMPID, tmpsrdn, &len);
        if (NULL == tmpelem) {
            char *dn = NULL;
            slapi_rdn_get_dn(tmpsrdn, &dn);
            slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_index_read",
                          "Failed to generate a new elem: dn: %s\n", dn);
            slapi_ch_free_string(&dn);
            if (tmpsrdn != srdn) {
                slapi_rdn_free(&tmpsrdn);
            }
            goto bail;
        }

        /* Generate a key for child tree */
        /* E.g., C1 */
        keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, id);
        dblayer_value_set_buffer(be, &key, keybuf, strlen(keybuf) + 1);
        dblayer_value_set(be, &data, tmpelem, len);

        /* Position cursor at the matching key */
        rc = _entryrdn_get_elem(cursor, &key, &data, childnrdn, &tmpelem, db_txn);
        if (rc) {
            slapi_ch_free((void **)&tmpelem);
            if ((rc == DBI_RC_RETRY) && db_txn) {
                slapi_log_err(SLAPI_LOG_BACKLDBM, "_entryrdn_index_read",
                              "Suffix \"%s\" cursor get fails: "
                              "%s(%d)\n",
                              nrdn, dblayer_strerror(rc), rc);
                if (tmpsrdn != srdn) {
                    slapi_rdn_free(&tmpsrdn);
                }
                goto bail;
            }
            if (flags & TOMBSTONE_INCLUDED) {
                /* Node might be a tombstone */
                /*
                 * In DIT cn=A,ou=B,o=C, cn=A and ou=B are removed and
                 * turned to tombstone entries.  We need to support both:
                 *   nsuniqueid=...,cn=A,ou=B,o=C and
                 *   nsuniqueid=...,cn=A,nsuniqueid=...,ou=B,o=C
                 */
                rc = _entryrdn_get_tombstone_elem(cursor, tmpsrdn, &key,
                                                  childnrdn, &tmpelem, db_txn);
                if (rc || (NULL == tmpelem)) {
                    slapi_ch_free((void **)&tmpelem);
                    if (DBI_RC_NOTFOUND != rc) {
                        slapi_log_err(SLAPI_LOG_BACKLDBM, "_entryrdn_index_read",
                                      "Child link \"%s\" of "
                                      "key \"%s\" not found: %s(%d)\n",
                                      childnrdn, keybuf, dblayer_strerror(rc), rc);
                        rc = DBI_RC_NOTFOUND;
                    }
                    if (tmpsrdn != srdn) {
                        slapi_rdn_free(&tmpsrdn);
                    }
                    goto bail;
                }
                rdnidx--; /* consider nsuniqueid=..,<RDN> one RDN */
            } else {
                slapi_ch_free((void **)&tmpelem);
                if (DBI_RC_NOTFOUND != rc) {
                    slapi_log_err(SLAPI_LOG_BACKLDBM, "_entryrdn_index_read",
                                  "Child link \"%s\" of key \"%s\" not found: %s(%d)\n",
                                  childnrdn, keybuf, dblayer_strerror(rc), rc);
                    rc = DBI_RC_NOTFOUND;
                }
                if (tmpsrdn != srdn) {
                    slapi_rdn_free(&tmpsrdn);
                }
                goto bail;
            }
        }
        if (tmpsrdn != srdn) {
            slapi_rdn_free(&tmpsrdn);
        }
        _ENTRYRDN_DUMP_RDN_ELEM(tmpelem);
        if (parentelem) {
            slapi_ch_free((void **)parentelem);
            *parentelem = *elem;
            _ENTRYRDN_DUMP_RDN_ELEM(*parentelem);
        } else {
            slapi_ch_free((void **)elem);
        }
        *elem = tmpelem;
#ifdef LDAP_DEBUG_ENTRYRDN
        slapi_log_err(SLAPI_LOG_DEBUG, "_entryrdn_index_read",
                      "%s matched normalized child rdn %s\n",
                      (*elem)->rdn_elem_nrdn_rdn, childnrdn);
#endif
        id = id_stored_to_internal((*elem)->rdn_elem_id);
        nrdn = childnrdn;

        if (0 == id) {
            slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_index_read",
                          "Child %s of %s not found\n", childnrdn, nrdn);
            break;
        }
    } while (rdnidx >= 0);

    /* get the child elems */
    if (childelems) {
        char buffer[RDN_BULK_FETCH_BUFFER_SIZE];

        slapi_ch_free_string(&keybuf);
        keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, id);
        dblayer_value_set_buffer(be, &key, keybuf, strlen(keybuf) + 1);

        /* Setting the bulk fetch buffer */
        dblayer_bulk_set_buffer(be, &bulkdata, buffer, sizeof(buffer), DBI_VF_BULK_DATA);

    retry_get0:
        rc = dblayer_cursor_bulkop(cursor, DBI_OP_MOVE_TO_KEY, &key, &bulkdata);
        if (DBI_RC_RETRY == rc) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "_entryrdn_index_read",
                          "Cursor get deadlock\n");
            if (db_txn) {
                goto bail;
            } else {
                /* try again */
                goto retry_get0;
            }
        } else if (DBI_RC_NOTFOUND == rc) {
            rc = 0; /* Child not found is ok */
            goto bail;
        } else if (rc) {
            _entryrdn_cursor_print_error("_entryrdn_index_read",
                                         key.data, bulkdata.v.size, bulkdata.v.ulen, rc);
            goto bail;
        }

        *childelems = (rdn_elem **)slapi_ch_calloc(childnum,
                                                   sizeof(rdn_elem *));
        do {
            rdn_elem *childelem = NULL;
            dbi_val_t dataret = {0};
            for (dblayer_bulk_start(&bulkdata); DBI_RC_SUCCESS == dblayer_bulk_nextdata(&bulkdata, &dataret);) {
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
            }
        retry_get1:
            rc = dblayer_cursor_bulkop(cursor, DBI_OP_NEXT_DATA, &key, &bulkdata);
            if (DBI_RC_RETRY == rc) {
                slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "_entryrdn_index_read",
                              "Retry cursor get deadlock\n");

                if (db_txn) {
                    goto bail;
                } else {
                    /* try again */
                    goto retry_get1;
                }
            } else if (DBI_RC_NOTFOUND == rc) {
                rc = 0;
                goto bail; /* done */
            } else if (rc) {
                _entryrdn_cursor_print_error("_entryrdn_index_read",
                                             key.data, bulkdata.v.size, bulkdata.v.ulen, rc);
                goto bail;
            }
        } while (0 == rc);
    }

bail:
    if (childelems && *childelems && 0 == curr_childnum) {
        slapi_ch_free((void **)childelems);
    }
    slapi_ch_free_string(&keybuf);
    dblayer_value_free(be, &data);
    slapi_log_err(SLAPI_LOG_TRACE, "_entryrdn_index_read",
                  "<-- _entryrdn_index_read (rc=%d)\n", rc);
    return rc;
}

static int
_entryrdn_append_childidl(dbi_cursor_t *cursor,
                          const char *nrdn __attribute__((unused)),
                          ID id,
                          IDList **affectedidl,
                          dbi_txn_t *db_txn)
{
    /* E.g., C5 */
    char *keybuf = slapi_ch_smprintf("%c%u", RDN_INDEX_CHILD, id);
    dbi_val_t key = {0};
    dbi_bulk_t data = {0};
    char buffer[RDN_BULK_FETCH_BUFFER_SIZE];
    int rc = 0;
    backend *be = cursor->be;

    dblayer_value_set(be, &key, keybuf, strlen(keybuf) + 1);
    /* Setting the bulk fetch buffer */
    dblayer_bulk_set_buffer(be, &data, buffer, sizeof(buffer), DBI_VF_BULK_DATA);

/* Position cursor at the matching key */
retry_get0:
    rc = dblayer_cursor_bulkop(cursor, DBI_OP_MOVE_TO_KEY, &key, &data);
    if (rc) {
        if (DBI_RC_RETRY == rc) {
            slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "_entryrdn_append_childidl",
                          "Cursor get deadlock\n");
            if (db_txn) {
                goto bail;
            } else {
                /* try again */
                goto retry_get0;
            }
        } else if (DBI_RC_NOTFOUND == rc) {
            rc = 0; /* okay not to have children */
        } else {
            _entryrdn_cursor_print_error("_entryrdn_append_childidl",
                                         key.data, data.v.size, data.v.ulen, rc);
        }
        goto bail;
    }

    /* Iterate over the duplicates to get the direct child's ID */
    do {
        rdn_elem *myelem = NULL;
        dbi_val_t dataret = {0};
        for (dblayer_bulk_start(&data); DBI_RC_SUCCESS == dblayer_bulk_nextdata(&data, &dataret); ) {
            ID myid = 0;
            myelem = (rdn_elem *)dataret.data;
            myid = id_stored_to_internal(myelem->rdn_elem_id);
            rc = idl_append_extend(affectedidl, myid);
            if (rc) {
                slapi_log_err(SLAPI_LOG_ERR, "_entryrdn_append_childidl",
                              "Appending %d to affected idl failed (%d)\n", myid, rc);
                goto bail;
            }
            rc = _entryrdn_append_childidl(cursor,
                                           (const char *)myelem->rdn_elem_nrdn_rdn,
                                           myid, affectedidl, db_txn);
            if (rc) {
                goto bail;
            }
        }
    retry_get1:
        rc = dblayer_cursor_bulkop(cursor, DBI_OP_NEXT_DATA, &key, &data);
        if (rc) {
            if (DBI_RC_RETRY == rc) {
                slapi_log_err(ENTRYRDN_LOGLEVEL(rc), "_entryrdn_append_childidl",
                              "Retry cursor get deadlock\n");
                if (db_txn) {
                    goto bail;
                } else {
                    /* try again */
                    goto retry_get1;
                }
            } else if (DBI_RC_NOTFOUND == rc) {
                rc = 0; /* okay not to have children */
            } else {
                _entryrdn_cursor_print_error("_entryrdn_append_childidl",
                                             key.data, data.v.size, data.v.ulen, rc);
            }
            goto bail;
        }
    } while (0 == rc);

bail:
    dblayer_value_free(be, &key);
    return rc;
}

static void
_entryrdn_cursor_print_error(char *fn, void *key, size_t need, size_t actual, int rc)
{
    if (DBI_RC_BUFFER_SMALL == rc) {
        slapi_log_err(SLAPI_LOG_ERR, NULL,
                      "%s - Entryrdn index is corrupt; data item for key %s "
                      "is too large for the buffer need=%lu actual=%lu)\n",
                      fn, (char *)key, (long unsigned int)need, (long unsigned int)actual);
    } else {
        slapi_log_err(SLAPI_LOG_ERR, NULL,
                      "%s - Failed to position cursor at "
                      "the key: %s: %s(%d)\n",
                      fn, (char *)key, dblayer_strerror(rc), rc);
    }
}

int
entryrdn_compare_rdn_elem(const void *elem_a, const void *elem_b)
{
    const rdn_elem *a = elem_a;
    const rdn_elem *b = elem_b;
    return strcmp((char *)a->rdn_elem_nrdn_rdn, (char *)b->rdn_elem_nrdn_rdn);
}


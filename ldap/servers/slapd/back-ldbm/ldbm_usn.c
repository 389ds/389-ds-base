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

#include "back-ldbm.h"

static int usn_get_last_usn(Slapi_Backend *be, PRUint64 *last_usn);

/*
 * USN counter part in the backend
 * - If usn is enabled,
 * -   For each backend,
 * -     Get the last USN index key
 * -     Initialize the slapi counter with the next USN (last USN + 1)
 *
 * dn: cn=entryusn,cn=default indexes,cn=config,cn=ldbm database,cn=plugins,cn=
 *  config
 * objectClass: top
 * objectClass: nsIndex
 * cn: entryusn
 * nsSystemIndex: true
 * nsIndexType: eq
 */

void
ldbm_usn_init(struct ldbminfo *li)
{
    Slapi_DN *sdn = NULL;
    void *node = NULL;
    int rc = 0;
    Slapi_Backend *be = NULL;
    PRUint64 last_usn = 0;
    PRUint64 global_last_usn = INITIALUSN;
    int isglobal = config_get_entryusn_global();
    int isfirst = 1;

    /* if USN is not enabled, return immediately */
    if (!plugin_enabled("USN", li->li_identity)) {
        goto bail;
    }

    /* Search each namingContext in turn */
    for (sdn = slapi_get_first_suffix(&node, 0); sdn != NULL;
         sdn = slapi_get_next_suffix_ext(&node, 0)) {
        be = slapi_mapping_tree_find_backend_for_sdn(sdn);
        rc = usn_get_last_usn(be, &last_usn);
        if (0 == rc) { /* only when the last usn is available */
            slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_usn_init",
                          "backend: %s%s\n", be->be_name,
                          isglobal ? " (global mode)" : "");
            if (isglobal) {
                if (isfirst) {
                    li->li_global_usn_counter = slapi_counter_new();
                    isfirst = 0;
                }
                /* share one counter */
                be->be_usn_counter = li->li_global_usn_counter;
                /* Initialize global_last_usn;
                 * Set the largest last_usn among backends */
                if ((global_last_usn == INITIALUSN) ||
                    ((last_usn != INITIALUSN) && (global_last_usn < last_usn))) {
                    global_last_usn = last_usn;
                }
                slapi_counter_set_value(be->be_usn_counter, global_last_usn);
                /* stores next usn */
                slapi_counter_increment(be->be_usn_counter);
            } else {
                be->be_usn_counter = slapi_counter_new();
                slapi_counter_set_value(be->be_usn_counter, last_usn);
                /* stores next usn */
                slapi_counter_increment(be->be_usn_counter);
            }
        }
    }
bail:
    return;
}

/*
 * usn_ge_last_usn: get the last USN from the entryusn equality index
 */
static int
usn_get_last_usn(Slapi_Backend *be, PRUint64 *last_usn)
{
    struct attrinfo *ai = NULL;
    int rc = -1;
    DB *db = NULL;
    DBC *dbc = NULL;
    DBT key; /* For the last usn */
    DBT value;
    PRInt64 signed_last_usn;

    if ((NULL == be) || (NULL == last_usn)) {
        return rc;
    }

    memset(&key, 0, sizeof(key));
    memset(&value, 0, sizeof(key));

    *last_usn = INITIALUSN; /* to start from 0 */

    /* Open the entryusn index */
    ainfo_get(be, SLAPI_ATTR_ENTRYUSN, &ai);

    /* Open the entryusn index file */
    rc = dblayer_get_index_file(be, ai, &db, DBOPEN_CREATE);
    if (0 != rc) {
        /* entryusn.db# is missing; it would be the first time. */
        slapi_log_err(SLAPI_LOG_ERR, "usn_get_last_usn",
                      "Failed to open the entryusn index: %d; Creating it...\n", rc);
        goto bail;
    }

    /* Get a cursor */
    rc = db->cursor(db, NULL, &dbc, 0);
    if (0 != rc) {
        slapi_log_err(SLAPI_LOG_ERR, "usn_get_last_usn",
                      "Failed to create a cursor: %d", rc);
        goto bail;
    }

    key.flags = DB_DBT_MALLOC;
    value.flags = DB_DBT_MALLOC;
    rc = dbc->c_get(dbc, &key, &value, DB_LAST);
    if ((0 == rc) && key.data) {
        char *p = (char *)key.data;
        while ((0 == rc) && ('=' != *p)) { /* get the last elem of equality */
            slapi_ch_free(&(key.data));
            slapi_ch_free(&(value.data));
            rc = dbc->c_get(dbc, &key, &value, DB_PREV);
            p = (char *)key.data;
        }
        if (0 == rc) {
            signed_last_usn = strtoll(++p, (char **)NULL, 0); /* key.data: =num */
            if (signed_last_usn > SIGNEDINITIALUSN) {
                *last_usn = signed_last_usn;
            }
        }
    } else if (DB_NOTFOUND == rc) {
        /* if empty, it's okay.  This is just a beginning. */
        rc = 0;
    }
    slapi_ch_free(&(key.data));
    slapi_ch_free(&(value.data));

bail:
    if (dbc) {
        dbc->c_close(dbc);
    }
    if (db) {
        dblayer_release_index_file(be, ai, db);
    }
    return rc;
}

/*
 * Whether USN is enabled or not is checked with be_usn_counter.
 */
int
ldbm_usn_enabled(Slapi_Backend *be)
{
    return (NULL != be->be_usn_counter);
}

/*
 * set last usn to the USN slapi_counter in backend
 */
int
ldbm_set_last_usn(Slapi_Backend *be)
{
    PRUint64 last_usn = 0;
    int isglobal = config_get_entryusn_global();
    int rc = -1;

    if (NULL == be) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_set_last_usn",
                      "Empty backend\n");
        return rc;
    }

    if (isglobal) {
        struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
        /* destroy old counter, if any */
        slapi_counter_destroy(&(li->li_global_usn_counter));
        ldbm_usn_init(li);
    } else {
        slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_set_last_usn",
                      "backend: %s\n", be->be_name);
        rc = usn_get_last_usn(be, &last_usn);
        if (0 == rc) { /* only when the last usn is available */
            /* destroy old counter, if any */
            slapi_counter_destroy(&(be->be_usn_counter));
            be->be_usn_counter = slapi_counter_new();
            slapi_counter_set_value(be->be_usn_counter, last_usn);
            slapi_counter_increment(be->be_usn_counter); /* stores next usn */
        }
    }

    return rc;
}

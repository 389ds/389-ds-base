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
ldbm_usn_init(struct ldbminfo  *li)
{
    Slapi_DN *sdn = NULL;
    void *node = NULL;
    const char *base = NULL;
    int rc = 0;
    Slapi_Backend *be = NULL;
    PRUint64 last_usn = 0;

    /* if USN is not enabled, return immediately */
    if (!plugin_enabled("USN", li->li_identity)) {
        goto bail;
    }

    /* Search each namingContext in turn */
    for ( sdn = slapi_get_first_suffix( &node, 0 ); sdn != NULL;
          sdn = slapi_get_next_suffix( &node, 0 )) {
        base = slapi_sdn_get_dn( sdn );
        be = slapi_mapping_tree_find_backend_for_sdn(sdn);
        slapi_log_error(SLAPI_LOG_TRACE, "ldbm_usn_init",
                        "backend: %s\n", be->be_name);
        rc = usn_get_last_usn(be, &last_usn);
        if (0 == rc) { /* only when the last usn is available */
            be->be_usn_counter = slapi_counter_new();
            slapi_counter_set_value(be->be_usn_counter, last_usn);
            slapi_counter_increment(be->be_usn_counter); /* stores next usn */
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
    DBT key;              /* For the last usn */
    DBT value;

    if (NULL == last_usn) {
        return rc;
    }

    memset(&key, 0, sizeof(key));
    memset(&value, 0, sizeof(key));

    *last_usn = -1; /* to start from 0 */

    /* Open the entryusn index */
    ainfo_get(be, SLAPI_ATTR_ENTRYUSN, &ai);

    /* Open the entryusn index file */
    rc = dblayer_get_index_file(be, ai, &db, DBOPEN_CREATE);
    if (0 != rc) {
        /* entryusn.db# is missing; it would be the first time. */
        slapi_log_error(SLAPI_LOG_FATAL, "usn_get_last_usn", 
                        "failed to open the entryusn index: %d", rc);
        goto bail;
    }

    /* Get a cursor */
    rc = db->cursor(db, NULL, &dbc, 0);
    if (0 != rc) {
        slapi_log_error(SLAPI_LOG_FATAL, "usn_get_last_usn", 
                        "failed to create a cursor: %d", rc);
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
            *last_usn = strtoll(++p, (char **)NULL, 0); /* key.data: =num */
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
    int rc = usn_get_last_usn(be, &last_usn);

    if (0 == rc) { /* only when the last usn is available */
        /* destroy old counter, if any */
        slapi_counter_destroy(&(be->be_usn_counter));
        be->be_usn_counter = slapi_counter_new();
        slapi_counter_set_value(be->be_usn_counter, last_usn);
        slapi_counter_increment(be->be_usn_counter); /* stores next usn */
    }

    return rc;
}

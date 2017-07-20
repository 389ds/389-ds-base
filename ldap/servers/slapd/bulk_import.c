/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * Copyright (c) 1995 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

/* The functions in this file allow plugins to perform bulk import of data
   comming over ldap connection. Note that the code will not work if
   there is now active connection since import state is stored in the
   connection extension */

#include "slap.h"

/* forward declarations */
static int process_bulk_import_op(Slapi_PBlock *pb, int state, Slapi_Entry *e);

/* This function initiates bulk import. The pblock must contain
   SLAPI_LDIF2DB_GENERATE_UNIQUEID -- currently always set to TIME_BASED
   SLAPI_CONNECTION -- connection over which bulk import is coming
   SLAPI_BACKEND -- the backend being imported
   or
   SLAPI_TARGET_SDN that contains root of the imported area.
   The function returns LDAP_SUCCESS or LDAP error code
*/

int
slapi_start_bulk_import(Slapi_PBlock *pb)
{
    return (process_bulk_import_op(pb, SLAPI_BI_STATE_START, NULL));
}

/* This function stops bulk import. The pblock must contain
   SLAPI_CONNECTION -- connection over which bulk import is coming
   SLAPI_BACKEND -- the backend being imported
   or
   SLAPI_TARGET_SDN that contains root of the imported area.
   The function returns LDAP_SUCCESS or LDAP error code
*/
int
slapi_stop_bulk_import(Slapi_PBlock *pb)
{
    return (process_bulk_import_op(pb, SLAPI_BI_STATE_DONE, NULL));
}

/* This function adds an entry to the bulk import. The pblock must contain
   SLAPI_CONNECTION -- connection over which bulk import is coming
   SLAPI_BACKEND -- optional backend pointer; if missing computed based on entry dn
   The function returns LDAP_SUCCESS or LDAP error code
*/
int
slapi_import_entry(Slapi_PBlock *pb, Slapi_Entry *e)
{
    return (process_bulk_import_op(pb, SLAPI_BI_STATE_ADD, e));
}

static int
process_bulk_import_op(Slapi_PBlock *pb, int state, Slapi_Entry *e)
{
    int rc;
    Slapi_Backend *be = NULL;
    Slapi_DN *target_sdn = NULL;

    if (pb == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "process_bulk_import_op", "NULL pblock\n");
        return LDAP_OPERATIONS_ERROR;
    }

    if (state == SLAPI_BI_STATE_ADD && e == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "process_bulk_import_op", "NULL entry\n");
        return LDAP_OPERATIONS_ERROR;
    }

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (be == NULL) {
        /* try to get dn to select backend */
        if (e) {
            target_sdn = slapi_entry_get_sdn(e);
        } else {
            slapi_pblock_get(pb, SLAPI_TARGET_SDN, &target_sdn);
            if (NULL == target_sdn) {
                slapi_log_err(SLAPI_LOG_ERR, "process_bulk_import_op",
                              "NULL target sdn\n");
                return LDAP_OPERATIONS_ERROR;
            }
        }
        be = slapi_be_select(target_sdn);

        if (be) {
            if (state == SLAPI_BI_STATE_START && (!slapi_be_issuffix(be, target_sdn))) {
                slapi_log_err(SLAPI_LOG_ERR, "process_bulk_import_op",
                              "Wrong backend suffix\n");
                return LDAP_OPERATIONS_ERROR;
            }
            slapi_pblock_set(pb, SLAPI_BACKEND, be);
        } else {
            slapi_log_err(SLAPI_LOG_ERR, "process_bulk_import_op", "NULL backend\n");
            return LDAP_OPERATIONS_ERROR;
        }
    }

    if (be->be_wire_import == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "process_bulk_import_op",
                      "Bulk import is not supported by this (%s) backend\n",
                      be->be_type);
        return LDAP_NOT_SUPPORTED;
    }

    /* set required parameters */
    slapi_pblock_set(pb, SLAPI_BULK_IMPORT_STATE, &state);
    if (e)
        slapi_pblock_set(pb, SLAPI_BULK_IMPORT_ENTRY, e);

    rc = be->be_wire_import(pb);
    if (rc != 0) {
        /* The caller will free the entry (e), so we just
         * leave it alone here. */
        slapi_log_err(SLAPI_LOG_ERR, "process_bulk_import_op",
                      "Failed; error = %d\n", rc);
        return LDAP_OPERATIONS_ERROR;
    }
    if (state == SLAPI_BI_STATE_DONE) {
        slapi_be_set_flag(be, SLAPI_BE_FLAG_POST_IMPORT);
    }

    return LDAP_SUCCESS;
}

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

#include "cb.h"

/*
 * Perform an abandon operation
 *
 * Returns:
 *   0  - success
 *   <0 - fail
 *
 */

int
chaining_back_abandon(Slapi_PBlock *pb __attribute__((unused)))
{
    /*
     * Abandon forwarded to the farm server for scoped
     * searches only. Done in cb_search.c
     */
    return 0;
}

int
cb_check_forward_abandon(cb_backend_instance *cb, Slapi_PBlock *pb, LDAP *ld, int msgid)
{

    int rc;
    LDAPControl **ctrls = NULL;

    if (slapi_op_abandoned(pb)) {

        if ((rc = cb_forward_operation(pb)) != LDAP_SUCCESS) {
            return 0;
        }

        if ((rc = cb_update_controls(pb, ld, &ctrls, CB_UPDATE_CONTROLS_ISABANDON)) != LDAP_SUCCESS) {
            if (NULL != ctrls)
                ldap_controls_free(ctrls);
            return 0;
        }
        rc = ldap_abandon_ext(ld, msgid, ctrls, NULL);
        cb_release_op_connection(cb->pool, ld, CB_LDAP_CONN_ERROR(rc));
        if (NULL != ctrls)
            ldap_controls_free(ctrls);
        return 1;
    }
    return 0;
}

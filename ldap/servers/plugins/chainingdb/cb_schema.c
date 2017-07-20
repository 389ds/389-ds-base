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

void
cb_eliminate_illegal_attributes(cb_backend_instance *inst, Slapi_Entry *e)
{

    /* get rid of illegal attributes before sending op to the   */
    /* farm server.    (Add)                                */

    int rc, j;
    Slapi_Attr *attr = NULL;
    char *tobefreed = NULL;

    if (inst->illegal_attributes != NULL) { /* Unlikely to happen */

        slapi_rwlock_wrlock(inst->rwl_config_lock);

        for (j = 0; inst->illegal_attributes[j]; j++) {
            char *aType = NULL;
            rc = slapi_entry_first_attr(e, &attr);
            while (rc == 0) {
                if (tobefreed) {
                    slapi_entry_attr_delete(e, tobefreed);
                    tobefreed = NULL;
                }
                slapi_attr_get_type(attr, &aType);
                if (aType && slapi_attr_types_equivalent(inst->illegal_attributes[j], aType)) {
                    tobefreed = aType;
                    slapi_log_err(SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                                  "attribute <%s> not forwarded.\n", aType);
                }
                rc = slapi_entry_next_attr(e, attr, &attr);
            }
            if (tobefreed) {
                slapi_entry_attr_delete(e, tobefreed);
                tobefreed = NULL;
            }
        }

        slapi_rwlock_unlock(inst->rwl_config_lock);
    }
}

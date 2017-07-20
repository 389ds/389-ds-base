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

int
chainingdb_start(Slapi_PBlock *pb)
{

    cb_backend *cb;

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &cb);

    if (cb->started) {
        /* We may be called multiple times due to */
        /* plugin dependency resolution           */
        return 0;
    }

    /*
    ** Reads in any configuration information held in the dse for the
    ** chaining plugin. Create dse entries used to configure the
    ** chaining plugin if they don't exist. Registers plugins to maintain
    ** those dse entries.
    */

    cb_config_load_dse_info(pb);

    /* Register new LDAPv3 controls supported by the chaining backend */

    slapi_register_supported_control(CB_LDAP_CONTROL_CHAIN_SERVER,
                                     SLAPI_OPERATION_SEARCH | SLAPI_OPERATION_COMPARE | SLAPI_OPERATION_ADD | SLAPI_OPERATION_DELETE | SLAPI_OPERATION_MODIFY | SLAPI_OPERATION_MODDN);

    /* register to be notified when backend state changes */
    slapi_register_backend_state_change((void *)cb_be_state_change,
                                        cb_be_state_change);

    cb->started = 1;
    return 0;
}

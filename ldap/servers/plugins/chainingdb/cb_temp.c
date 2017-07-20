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
** Temp wrappers until the appropriate functions
** are implemented in the slapi interface
*/

cb_backend_instance *
cb_get_instance(Slapi_Backend *be)
{
    return (cb_backend_instance *)slapi_be_get_instance_info(be);
}

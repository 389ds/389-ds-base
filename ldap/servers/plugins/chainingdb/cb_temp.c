/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include "cb.h"

/*
** Temp wrappers until the appropriate functions
** are implemented in the slapi interface
*/

cb_backend_instance * cb_get_instance(Slapi_Backend * be) {
	return (cb_backend_instance *)slapi_be_get_instance_info(be);
}

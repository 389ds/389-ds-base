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
** cLeanup a chaining backend instance
*/

int
cb_back_cleanup(Slapi_PBlock *pb __attribute__((unused)))
{

    /*
    ** Connections have been closed in cb_back_close()
    ** For now, don't do more
    */

    return 0;
}

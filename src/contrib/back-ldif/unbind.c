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
 *  File: unbind.c
 *
 *  Functions:
 *
 *      ldif_back_unbind() - ldap ldif back-end unbind routine
 *
 */
#include "back-ldif.h"

/*
 *  Function: ldif_back_unbind
 *
 *  Returns: returns 0
 *
 *  Description: performs an ldap unbind.
 */
int
ldif_back_unbind(Slapi_PBlock *pb)
{
    return (0);
}

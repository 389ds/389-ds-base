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

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "slap.h"


int
type_to_ACCESS_bit(char *p __attribute__((unused)))
{
    return 0;
}

void *
PT_Lock(PRLock *x_mutex __attribute__((unused)))
{
    return NULL;
}

int
lcache_init(LDAP *ld __attribute__((unused)), void *arg __attribute__((unused)))
{
    slapi_log_err(SLAPI_LOG_ERR,
                  "lcache_init", "Shouldn't have been called\n");
    return -1;
}

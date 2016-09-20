/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "slap.h"


int type_to_ACCESS_bit( char *p )
{
	return 0;
}

void *PT_Lock( PRLock *x_mutex )
{
	return NULL;
}

int lcache_init(LDAP *ld, void *arg)
{
	LDAPDebug(LDAP_DEBUG_ERR, 
		"lcache_init: Shouldn't have been called\n", 0,0,0);
	return -1;
}

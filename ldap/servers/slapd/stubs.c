/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* Needed because not all functions are currently defined for server3_branch */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "slap.h"

#if defined( XP_WIN32 ) /* PK*/
void *dlsym(void *a, char *b) 
{
	return 0;
}
#endif

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
	LDAPDebug(LDAP_DEBUG_ANY, 
		"lcache_init: Shouldn't have been called\n", 0,0,0);
	return -1;
}

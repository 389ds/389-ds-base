/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* stubrepl.c - stubs of functions required for the evil stuff in the tools directory  */

#include "slap.h"

int connection_type = -1;

void
ps_service_persistent_searches( Slapi_Entry *e, Slapi_Entry *eprev, int chgtype, int chgnum )
{
}

void
ps_wakeup_all( void )
{
}

int
slapd_ssl_init()
{
	return( -1 );
}

int
slapd_ssl_init2(PRFileDesc **fd, int startTLS)
{
	return( -1 );
}

void
connection_abandon_operations( Connection *conn )
{
}

void
disconnect_server( Connection *conn, int opconnid, int opid, PRErrorCode reason, PRInt32 error )
{
}

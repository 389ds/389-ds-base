/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* init.c - initialize various things */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/time.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif
#include "slap.h"
#include "fe.h"
#if defined( MACOS ) || defined( DOS ) || defined( _WIN32 ) || defined( NEED_BSDREGEX )
#include "regex.h"
#endif

void
slapd_init()
{
#ifdef _WIN32
	WSADATA	wsadata;
	int	err;

	if( err = WSAStartup(0x0101, &wsadata ) != 0 ) {
		LDAPDebug( LDAP_DEBUG_ANY,
		    "Windows Sockets initialization failed, error %d (%s)\n",
		    err, slapd_system_strerror( err ), 0 );
		exit( 1 );
	}
#endif /* _WIN32 */

	ops_mutex = PR_NewLock();
	num_conns_mutex = PR_NewLock();
	g_set_num_sent_mutex( PR_NewLock() );
	g_set_current_conn_count_mutex( PR_NewLock() );
	slapd_re_init();

	if ( ops_mutex == NULL ||
	    num_conns_mutex == NULL ||
	    g_get_num_sent_mutex() == NULL ||
	    g_get_current_conn_count_mutex() == NULL )
	{
		LDAPDebug( LDAP_DEBUG_ANY,
		    "init: PR_NewLock failed\n", 0, 0, 0 );
		exit( -1 );
	}

#ifndef HAVE_TIME_R
	if ((time_func_mutex = PR_NewLock()) == NULL ) {
                LDAPDebug( LDAP_DEBUG_ANY,
                    "init: PR_NewLock failed\n", 0, 0, 0 );
                exit(-1);
        }

#endif /* HAVE_TIME_R */
}

/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/************************************************************

 testgetip.c

 This source file provides an example of a pre-operation plug-in
 function that gets the IP address of the client and the IP 
 address of the server.

 testgetip logs this information to the server error log.

 To test this plug-in function, stop the server, edit the dse.ldif file
 (in the <server_root>/slapd-<server_id>/config directory)
 and add the following lines before restarting the server :

 dn: cn=Test GetIP,cn=plugins,cn=config
 objectClass: top
 objectClass: nsSlapdPlugin
 objectClass: extensibleObject
 cn: Test GetIP
 nsslapd-pluginPath: <server_root>/plugins/slapd/slapi/examples/libtest-plugin.so
 nsslapd-pluginInitfunc: testgetip_init
 nsslapd-pluginType: preoperation
 nsslapd-pluginEnabled: on
 nsslapd-plugin-depends-on-type: database
 nsslapd-pluginId: test-getip

 ************************************************************/

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include "slapi-plugin.h"
#include "nspr.h"

Slapi_PluginDesc getippdesc = { "test-getip", "Netscape", "0.5",
	"sample pre-operation plugin" };

static char *netaddr2str( PRNetAddr *addrp, char *buf, size_t buflen );

int
testgetip( Slapi_PBlock *pb )
{
	void		*conn;
	PRNetAddr	client_addr, server_addr;
	char		addrbuf[ 512 ], *addrstr;

	/*
	 * Don't do anything for internal operations (NULL connection).
	 */
	if ( slapi_pblock_get( pb, SLAPI_CONNECTION, &conn ) != 0 ||
		( conn == NULL )) {
			return( 0 );
	}

	/*
	 * Get the client's IP address and log it
	 */
	if ( slapi_pblock_get( pb, SLAPI_CONN_CLIENTNETADDR, &client_addr )
	    != 0 || ( client_addr.raw.family == 0 )) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "testgetip",
		    "Could not get client IP.\n" );
	} else if (( addrstr = netaddr2str( &client_addr, addrbuf,
	    sizeof(addrbuf))) != NULL ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "testgetip",
		    "Client's IP address is %s\n", addrstr );
	}

	/*
	 * Get the destination (server) IP address and log it
	 */
	if ( slapi_pblock_get( pb, SLAPI_CONN_SERVERNETADDR, &server_addr )
	    != 0 || ( server_addr.raw.family == 0 )) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "testgetip",
		    "Could not get server IP.\n" );
	} else if (( addrstr = netaddr2str( &server_addr, addrbuf,
	    sizeof(addrbuf))) != NULL ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "testgetip",
		    "Client sent request to server IP %s\n", addrstr );
	}

	return( 0 );
}

#ifdef _WIN32
__declspec(dllexport)
#endif
int
testgetip_init( Slapi_PBlock *pb )
{
	/* Register the pre-operation plug-in function. */
	if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    SLAPI_PLUGIN_VERSION_01 ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&getippdesc ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_SEARCH_FN,
	    (void *) testgetip ) != 0 ) { 
		slapi_log_error( SLAPI_LOG_FATAL, "testgetip_init",
			"Failed to set version and functions.\n" );
		return( -1 );
	}

	slapi_log_error( SLAPI_LOG_PLUGIN, "testgetip_init",
		"Registered preop plugins.\n" );
	return( 0 );
}


/*
 * Utility function to convert a PRNetAddr to a human readable string.
 */
static char *
netaddr2str( PRNetAddr *addrp, char *buf, size_t buflen )
{
	char	*addrstr;

	*buf = '\0';
	if ( PR_NetAddrToString( addrp, buf, buflen ) != PR_SUCCESS ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "testgetip",
		    "PR_NetAddrToString failed.\n" );
		return( NULL );
	}

	/* skip past leading ::ffff: if IPv4 address */
	if ( strlen( buf ) > 7 && strncmp( buf, "::ffff:", 7 ) == 0 ) {
		addrstr = buf + 7;
	} else {
		addrstr = buf;
	}

	return( addrstr );
}

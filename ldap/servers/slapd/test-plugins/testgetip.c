/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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

Slapi_PluginDesc getippdesc = { "test-getip", "Fedora Project", "7.1 SP3",
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

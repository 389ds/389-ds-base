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

 testextendedop.c

 This source file provides an example of a plug-in function 
 that implements an extended operation.  The plug-in function
 is called by the server if an LDAP client request contains
 the OID "1.2.3.4" (which identifies this operation).

 To test this plug-in function, you need to write an LDAP
 v3 client that can send requests for extended operations.
 You can use the Netscape Directory SDK for C 3.0 or the 
 Netscape Directory SDK for Java 3.0 to build these clients.  
 (These SDKs are available from DevEdge Online at 
 http://developer.netscape.com/tech/directory/)

 The LDAP client should send an extended operation request
 with the OID 1.2.3.4. To verify that the operation completed
 successfully, your client should check the OID and value
 returned in the LDAP response.

 To test this plug-in function, stop the server, edit the dse.ldif file
 (in the <server_root>/slapd-<server_id>/config directory)
 and add the following lines before restarting the server :

 dn: cn=Test ExtendedOp,cn=plugins,cn=config
 objectClass: top
 objectClass: nsSlapdPlugin
 objectClass: extensibleObject
 cn: Test ExtendedOp
 nsslapd-pluginPath: <server_root>/plugins/slapd/slapi/examples/libtest-plugin.so
 nsslapd-pluginInitfunc: testexop_init
 nsslapd-pluginType: extendedop
 nsslapd-pluginEnabled: on
 nsslapd-plugin-depends-on-type: database
 nsslapd-pluginId: test-extendedop
 nsslapd-pluginarg0: 1.2.3.4

 ************************************************************/

#include <stdio.h>
#include <string.h>
#include "slapi-plugin.h"

/* OID of the extended operation handled by this plug-in */
#define MY_OID	"1.2.3.4"

Slapi_PluginDesc expdesc = { "test-extendedop", "Fedora Project", "7.1 SP3",
	"sample extended operation plugin" };


/* Extended operation plug-in */
int
testexop_babs( Slapi_PBlock *pb )
{
	char		*oid;
	struct berval	*bval;
	char		*retval, *msg;
	struct berval	retbval;

	/* Get the OID and the value included in the request */
	if ( slapi_pblock_get( pb, SLAPI_EXT_OP_REQ_OID, &oid ) != 0 ||
	    slapi_pblock_get( pb, SLAPI_EXT_OP_REQ_VALUE, &bval ) != 0 ) {
		msg = "Could not get OID and value from request.";
		slapi_log_error( SLAPI_LOG_PLUGIN, "testexop_babs", "%s\n",
			 msg );
		slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL,
			 msg, 0, NULL );
		return( SLAPI_PLUGIN_EXTENDED_SENT_RESULT );
	} else {
	    slapi_log_error( SLAPI_LOG_PLUGIN, "testexop_babs", 
			"Received extended operation request with OID %s\n",
			oid );
	    slapi_log_error( SLAPI_LOG_PLUGIN, "testexop_babs",
			"Value from client: %s\n", bval->bv_val );
	}

	/* Set up the value that you want returned to the client.
	   In this case, it's just the value sent from the client,
	   preceded by the string "Value from client: "  */

	msg = "Value from client: ";
	retval = ( char * )slapi_ch_malloc( bval->bv_len + strlen( msg ) + 1 );
	sprintf( retval, "%s%s", msg, bval->bv_val );
	retbval.bv_val = retval;
	retbval.bv_len = strlen( retbval.bv_val );

	/* Prepare to return the OID and value back to the client.
	   Note that if you want, you can return a different OID to
	   the client (for example, if you want to use the OID as 
	   an indicator of something). */
	if ( slapi_pblock_set( pb, SLAPI_EXT_OP_RET_OID, "5.6.7.8" ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_EXT_OP_RET_VALUE, &retbval ) != 0 ) {
		slapi_ch_free( ( void ** ) &retval );
		msg = "Could not set return values";
		slapi_log_error( SLAPI_LOG_PLUGIN, "testexop_babs", "%s\n",
			    msg );
		slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL,
			    msg, 0, NULL );
		return( SLAPI_PLUGIN_EXTENDED_SENT_RESULT );
	}

	/* Send the response (containing the OID and value you set)
	   back to the client. */
	slapi_send_ldap_result( pb, LDAP_SUCCESS, NULL,
	    "operation babs successful!", 0, NULL );
	slapi_log_error( SLAPI_LOG_PLUGIN, "testexop_babs", 
		"OID sent to client: %s\n", "5.6.7.8" );
	slapi_log_error( SLAPI_LOG_PLUGIN, "testexop_babs",
		"Value sent to client: %s\n", retval );
		
	/* Free any memory allocated by this plug-in. */
	slapi_ch_free( ( void ** ) &retval );

	/* Let front end know we sent the result */
	return( SLAPI_PLUGIN_EXTENDED_SENT_RESULT );
}

/* Initialization function */
#ifdef _WIN32
__declspec(dllexport)
#endif
int
testexop_init( Slapi_PBlock *pb )
{
	char	**argv;
	char	*oid;
	char	**oidlist, **namelist;

	/* Get the arguments appended to the plugin extendedop directive
	   in the plugin entry.  The first argument 
	   (after the standard arguments for the directive) should 
	   contain the OID of the extended op.
	*/ 

	if ( slapi_pblock_get( pb, SLAPI_PLUGIN_ARGV, &argv ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN,
		    "testexop_init", "Could not get argv\n" );
		return( -1 );
	}

	/* Compare the OID specified in the configuration file
	   against the OID supported by this plug-in function. */

	if ( argv == NULL || strcmp( argv[0], MY_OID ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN,
		    "testexop_init", "OID is missing or is not %s\n", MY_OID );
		return( -1 );
	} else {
		oid = slapi_ch_strdup( argv[0] );
		slapi_log_error( SLAPI_LOG_PLUGIN, "testexop_init",
			"Registering plug-in for extended op %s.\n", oid );
	}

	oidlist = (char **) slapi_ch_malloc( 2 * sizeof( char * ) );
	oidlist[0] = oid;
	oidlist[1] = NULL;
	namelist = (char **) slapi_ch_malloc( 2 * sizeof( char * ) );
	namelist[0] = "test extended op";
	namelist[1] = NULL;

	/* Register the plug-in function as an extended operation
	   plug-in function that handles the operation identified by
	   OID 1.2.3.4.  Also specify the version of the server 
	   plug-in */ 
	if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
		SLAPI_PLUGIN_VERSION_01 ) != 0 || 
	     slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
		(void *)&expdesc ) != 0 ||
	     slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_FN,
		(void *) testexop_babs ) != 0 ||
	     slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_OIDLIST, oidlist ) ||
	     slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_NAMELIST, namelist ) != 0 ) {

		slapi_log_error( SLAPI_LOG_PLUGIN, "testexop_init",
			"Failed to set plug-in version, function, and OID.\n" );
		return( -1 );
	}

	return( 0 );
}


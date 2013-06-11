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
 * Copyright (C) 2013 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/**
 * LDAP whoami extended operation plug-in
 */

#include <stdio.h>
#include <string.h>
#include "slapi-plugin.h"

#define PLUGIN_NAME              "whoami-plugin"
#define PLUGIN_DESC              "whoami extended operation plugin"
#define WHOAMI_EXOP_REQUEST_OID  "1.3.6.1.4.1.4203.1.11.3"

static Slapi_PluginDesc expdesc = { PLUGIN_NAME, VENDOR, DS_PACKAGE_VERSION, PLUGIN_DESC };

static char *whoami_exop_oid_list[] = { WHOAMI_EXOP_REQUEST_OID, NULL };

int whoami_init( Slapi_PBlock *pb );
int whoami_exop( Slapi_PBlock *pb );

/* Extended operation function */

int whoami_exop( Slapi_PBlock *pb )
{
	struct berval *bval;
	struct berval retbval;

	Slapi_DN *dn = NULL;
	char *client_dn = NULL;
	char *fdn = NULL;
	char *oid = NULL;
	/* Get the OID and the value included in the request */
	
	if ( slapi_pblock_get( pb, SLAPI_EXT_OP_REQ_OID, &oid ) != 0 )
   	{
		slapi_log_error( SLAPI_LOG_FATAL, PLUGIN_NAME, "Could not get OID from request\n" );
		slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, "Could not get OID from request\n", 0, NULL );
		return( SLAPI_PLUGIN_EXTENDED_SENT_RESULT );
	} 
	
	if ( slapi_pblock_get( pb, SLAPI_EXT_OP_REQ_VALUE, &bval ) != 0 || bval->bv_val != NULL )
        {
                slapi_log_error( SLAPI_LOG_FATAL, PLUGIN_NAME, "Could not get correct request value from request\n" );
                slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, "Could not get correct request value from request\n", 0, NULL );
                return( SLAPI_PLUGIN_EXTENDED_SENT_RESULT );
        }

	/* Fetch the client dn */

	if( slapi_pblock_get( pb, SLAPI_CONN_DN, &client_dn ) !=0 )
	{
		slapi_log_error( SLAPI_LOG_FATAL, PLUGIN_NAME, "Could not get client_dn" );
		slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, "Could not get client_dn\n", 0, NULL );
		return( SLAPI_PLUGIN_EXTENDED_SENT_RESULT );
	}

	if( client_dn == NULL )
	{
		retbval.bv_val = NULL;
        	retbval.bv_len = 0;
	}
	else
	{
		fdn = slapi_ch_smprintf( "dn: %s", client_dn );
		retbval.bv_val = fdn;
		retbval.bv_len = strlen( retbval.bv_val );
	}

	/* Set the return value in pblock */

	if ( slapi_pblock_set( pb, SLAPI_EXT_OP_RET_OID, NULL ) != 0 || slapi_pblock_set( pb, SLAPI_EXT_OP_RET_VALUE, &retbval ) != 0 )
	{
		slapi_log_error( SLAPI_LOG_FATAL, PLUGIN_NAME, "Could not set return values" );
                slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, "Could not set return values", 0, NULL );
		slapi_ch_free_string( &client_dn );
        	slapi_ch_free_string( &fdn );
		return( SLAPI_PLUGIN_EXTENDED_SENT_RESULT );
	}

	/* Send the response back to client */
	
	slapi_send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 0, NULL );
	
	/* Free any memory allocated by this plug-in. */ 
	
	slapi_ch_free_string( &client_dn );
	slapi_ch_free_string( &fdn );
	return( SLAPI_PLUGIN_EXTENDED_SENT_RESULT );
}

/* Initialization function */

int whoami_init( Slapi_PBlock *pb )
{

	/* Register the plugin function as an extended operation plugin function */

	if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_03 ) != 0 || slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, ( void * )&expdesc ) != 0 || slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_FN, ( void * )whoami_exop ) != 0 || slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_OIDLIST, ( void * )whoami_exop_oid_list ) !=0 )
	{
		slapi_log_error( SLAPI_LOG_FATAL, PLUGIN_NAME, "whoami_init: Failed to register plug-in.\n" );
		return( -1 );
	}

	return( 0 );
}


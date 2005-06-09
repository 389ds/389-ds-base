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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/******** testdatainterop.c ******************* 

 This source file provides an example of a plug-in function
 that implements an datainteroprability functionality.
 The plug-in function is called by the server 
 when the server is configured to use the null dn.
 meaning dn: 
 The server uses the null dn or the root suffix opnly when
 the configuration for the server has the following
 node in the dse.ldif of the server instance
 (in the <server_root>/slapd-<server_id>/config directory).

 dn: cn="",cn=mapping tree,cn=config
 objectClass: top 
 objectClass: extensibleObject
 objectClass: nsMappingTree
 cn: ""
 nsslapd-state: container

 The plugin below is a pre-operation plugin which 
 provides alternate functionality for the LDAP operations
 of search, modify, add etc. that are targeted at the root-suffix 
 or the null-dn to be serviced by an alternate data source or
 alternate access methods allowing datainteroperability.

 The example below creates a berkely db and modifies or adds data
 to the db demonstarting the use of an alternate data source seperate
 from the Directory Server. Also, the results of a search operation
 are completely in the control of the pre-operation plugin. In this 
 example a fake entry is returned to express the functionality 

 
 To test this plug-in function, stop the server, edit the dse.ldif file
 (in the <server_root>/slapd-<server_id>/config directory)
 and add the following lines before restarting the server :

 dn: cn="",cn=mapping tree,cn=config
 objectClass: top 
 objectClass: extensibleObject
 objectClass: nsMappingTree
 cn: ""
 nsslapd-state: container


 dn: cn=datainterop,cn=plugins,cn=config
 objectClass: top
 objectClass: nsSlapdPlugin
 cn: datainterop 
 nsslapd-pluginPath: <server-root>/plugins/slapd/slapi/examples/libtest-plugin.so
 nsslapd-pluginInitfunc: nullsuffix_init
 nsslapd-pluginType: preoperation
 nsslapd-pluginEnabled: on
 nsslapd-pluginId: nullsuffix-preop
 nsslapd-pluginVersion: 7.1
 nsslapd-pluginVendor: Fedora Project
 nsslapd-pluginDescription: sample pre-operation null suffix plugin

 ******************************************/

#include <stdio.h>
#include <string.h>
#include "slapi-plugin.h"

/*
 * Macros.
 */
#define PLUGIN_NAME	"nullsuffix-preop"

#define PLUGIN_OPERATION_HANDLED    1	
#define PLUGIN_OPERATION_IGNORED    0	

#define SEARCH_SCOPE_ANY	(-1)



/*
 * Static variables.
 */
static Slapi_PluginDesc plugindesc = { PLUGIN_NAME, "Fedora Project", "7.1",
	"sample pre-operation null suffix plugin" };

static Slapi_ComponentId *plugin_id = NULL;


/*
 * Function prototypes.
 */
static int nullsuffix_search( Slapi_PBlock *pb );
static int nullsuffix_add( Slapi_PBlock *pb );
static int nullsuffix_close( Slapi_PBlock *pb );
static int nullsuffix_modify( Slapi_PBlock *pb );
static int nullsuffix_delete( Slapi_PBlock *pb );
static int nullsuffix_modrdn( Slapi_PBlock *pb );
static int nullsuffix_bind( Slapi_PBlock *pb );


/*
 * Initialization function.
 */
#ifdef _WIN32
__declspec(dllexport)
#endif
int
nullsuffix_init( Slapi_PBlock *pb )
{
	int		i;

	slapi_log_error( SLAPI_LOG_PLUGIN, PLUGIN_NAME, "nullsuffix_init\n" );

	/* retrieve plugin identity to later pass to internal operations */
	if ( slapi_pblock_get( pb, SLAPI_PLUGIN_IDENTITY, &plugin_id ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, PLUGIN_NAME,
			"unable to get SLAPI_PLUGIN_IDENTITY\n" );
		return -1;
	}

	/* register the pre-operation search function, etc. */
	if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01
				) != 0
				|| slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, 
					(void *)&plugindesc ) != 0
				|| slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_SEARCH_FN, 
					(void *)nullsuffix_search ) != 0
				|| slapi_pblock_set( pb, SLAPI_PLUGIN_CLOSE_FN, 
					(void *)nullsuffix_close ) != 0 
				|| slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_ADD_FN,
				    (void *)nullsuffix_add) != 0
				|| slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_MODIFY_FN,
				    (void *)nullsuffix_modify) != 0
				|| slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_DELETE_FN,
				    (void *)nullsuffix_delete) != 0
				|| slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_BIND_FN,
				    (void *)nullsuffix_bind) != 0
				|| slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_MODRDN_FN,
				    (void *)nullsuffix_modrdn) != 0) {
		slapi_log_error( SLAPI_LOG_PLUGIN, PLUGIN_NAME,
				"failed to set version and function\n" );
		return -1;
	}

  

	return 0;
}

static int
nullsuffix_bind( Slapi_PBlock *pb )
{
	if( slapi_op_reserved(pb) ){
		return PLUGIN_OPERATION_IGNORED;
	}
	slapi_log_error( SLAPI_LOG_PLUGIN, PLUGIN_NAME, "nullsuffix_bind\n" );
	send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 0, NULL );
	return PLUGIN_OPERATION_HANDLED;

}

static int
nullsuffix_add( Slapi_PBlock *pb )
{
	char *dn;
	if( slapi_op_reserved(pb) ){
		return PLUGIN_OPERATION_IGNORED;
	}
	slapi_log_error( SLAPI_LOG_PLUGIN, PLUGIN_NAME, "nullsuffix_add\n" );
	slapi_pblock_get( pb, SLAPI_ADD_TARGET, &dn );
	db_put_dn(dn);
	send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 0, NULL );
	return PLUGIN_OPERATION_HANDLED;
}

static int
nullsuffix_modify( Slapi_PBlock *pb )
{
	Slapi_Entry *entry;
	int i;
        int j;
	char *dn;
	if( slapi_op_reserved(pb) ){
		return PLUGIN_OPERATION_IGNORED;
	}
	slapi_pblock_get( pb, SLAPI_MODIFY_TARGET, &dn );
	slapi_pblock_get( pb, SLAPI_ENTRY_PRE_OP, &entry);  
	db_put_dn(dn);
	send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 0, NULL );
	slapi_log_error( SLAPI_LOG_PLUGIN, PLUGIN_NAME, "nullsuffix_modify\n" );
	return PLUGIN_OPERATION_HANDLED;

}

static int
nullsuffix_delete( Slapi_PBlock *pb )
{
	if( slapi_op_reserved(pb) ){
		return PLUGIN_OPERATION_IGNORED;
	}
	slapi_log_error( SLAPI_LOG_PLUGIN, PLUGIN_NAME, "nullsuffix_delete\n" );
	send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 0, NULL );
	return PLUGIN_OPERATION_HANDLED;
}

static int
nullsuffix_modrdn( Slapi_PBlock *pb )
{
	if( slapi_op_reserved(pb) ){
		return PLUGIN_OPERATION_IGNORED;
	}
	slapi_log_error( SLAPI_LOG_PLUGIN, PLUGIN_NAME, "nullsuffix_modrdn\n" );
	send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 0, NULL );
	return PLUGIN_OPERATION_HANDLED;
}

static int
nullsuffix_search( Slapi_PBlock *pb )
{
	char			*dn_base, **attrs, *newStr;
	int				scope, sizelimit, timelimit, deref, attrsonly;
	Slapi_Filter	*filter;
	Slapi_DN		*sdn_base;
	int				ldaperr = LDAP_SUCCESS;	/* optimistic */
	int				nentries = 0;	/* entry count */
	int				i;
	Slapi_Operation	*op;
	Slapi_Entry		*e;

	const char *entrystr =
		"dn:cn=Joe Smith,o=Example\n"
		"objectClass: top\n"
		"objectClass: person\n"
		"objectClass: organizationalPerson\n"
		"objectClass: inetOrgPerson\n"
		"cn:Joe Smith\n"
		"sn:Smith\n"
		"uid:jsmith\n"
		"mail:jsmith@example.com\n";
	
	slapi_log_error( SLAPI_LOG_PLUGIN, PLUGIN_NAME, "nullsuffix_search\n" );
	if( slapi_op_reserved(pb) ){
		return PLUGIN_OPERATION_IGNORED;
	}

	/* get essential search parameters */
	if ( slapi_pblock_get( pb, SLAPI_SEARCH_TARGET, &dn_base ) != 0 ||
			slapi_pblock_get( pb, SLAPI_SEARCH_SCOPE, &scope ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, PLUGIN_NAME,
				"could not get base DN and scope search parameters\n" );
	}
	if ( dn_base == NULL ) {
		dn_base = "";
	}
	sdn_base = slapi_sdn_new_dn_byval( dn_base );
	slapi_pblock_get(pb, SLAPI_OPERATION, &op);

	/* get remaining search parameters */
	if ( slapi_pblock_get( pb, SLAPI_SEARCH_DEREF, &deref ) != 0 ||
			slapi_pblock_get( pb, SLAPI_SEARCH_SIZELIMIT, &sizelimit ) != 0 ||
			slapi_pblock_get( pb, SLAPI_SEARCH_TIMELIMIT, &timelimit ) != 0 ||
			slapi_pblock_get( pb, SLAPI_SEARCH_FILTER, &filter ) != 0 ||
			slapi_pblock_get( pb, SLAPI_SEARCH_ATTRS, &attrs ) != 0 ||
			slapi_pblock_get( pb, SLAPI_SEARCH_ATTRSONLY, &attrsonly ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, PLUGIN_NAME,
				"could not get remaining search parameters\n" );
	}

	if ( slapi_pblock_get( pb, SLAPI_OPERATION, &op ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, PLUGIN_NAME,
				"could not get operation\n" );
	} else {
		 slapi_operation_set_flag(op, SLAPI_OP_FLAG_NO_ACCESS_CHECK  );
	}

	/* create a fake entry and send it along */
	newStr = slapi_ch_strdup( entrystr );
	if ( NULL == ( e = slapi_str2entry( newStr,
				SLAPI_STR2ENTRY_ADDRDNVALS
				| SLAPI_STR2ENTRY_EXPAND_OBJECTCLASSES ))) {
		slapi_log_error( SLAPI_LOG_PLUGIN, PLUGIN_NAME,
				"nullsuffix_search: slapi_str2entry() failed\n" );
	} else {
		slapi_send_ldap_search_entry( pb, e, NULL /* controls */,
				attrs, attrsonly );
		++nentries;
		slapi_entry_free( e );
	}

	slapi_send_ldap_result( pb, ldaperr, NULL, "kilroy was here",
			nentries, NULL );
	slapi_log_error( SLAPI_LOG_PLUGIN, PLUGIN_NAME, "nullsuffix_search:"
			" handled search based at %s with scope %d; ldaperr=%d\n",
			dn_base, scope, ldaperr );

	slapi_ch_free_string(&newStr);
	slapi_sdn_free(&sdn_base);

	return PLUGIN_OPERATION_HANDLED;
}


/*
 * Shutdown function.
 */
static int
nullsuffix_close( Slapi_PBlock *pb )
{
	slapi_log_error( SLAPI_LOG_PLUGIN, PLUGIN_NAME, "nullsuffix_close\n" );
	return 0;
}

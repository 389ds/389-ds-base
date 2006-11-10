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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


/*
 * plugin_acl.c - routines for calling access control plugins
 */

#include "slap.h"

static int
acl_default_access ( Slapi_PBlock *pb , Slapi_Entry *e, int access)
{

	int				isRoot, rootdse, accessCheckDisabled;
	int				 rc;

	slapi_pblock_get ( pb, SLAPI_REQUESTOR_ISROOT, &isRoot);
	if ( isRoot )  return LDAP_SUCCESS;	

	rc = slapi_pblock_get ( pb, SLAPI_PLUGIN_DB_NO_ACL, &accessCheckDisabled );
    if (  rc != -1 && accessCheckDisabled ) return LDAP_SUCCESS;

	rootdse = slapi_is_rootdse ( slapi_entry_get_ndn ( e ) );
    if (  rootdse && (access & (SLAPI_ACL_READ | SLAPI_ACL_SEARCH) ) )
		return LDAP_SUCCESS;

	return LDAP_INSUFFICIENT_ACCESS;
}

int
plugin_call_acl_plugin ( Slapi_PBlock *pb, Slapi_Entry *e, char **attrs, 
			 struct berval *val, int access , int flags, char **errbuf)
{
	struct slapdplugin	*p;
	int			rc = LDAP_INSUFFICIENT_ACCESS;
	int			aclplugin_initialized = 0;
	Operation	*operation;

	slapi_pblock_get (pb, SLAPI_OPERATION, &operation);

	/* we don't perform acl check for internal operations  and if the plugin has set it not to be checked */
	if (operation_is_flag_set(operation, SLAPI_OP_FLAG_NO_ACCESS_CHECK|OP_FLAG_INTERNAL|OP_FLAG_REPLICATED|OP_FLAG_LEGACY_REPLICATION_DN))
		return LDAP_SUCCESS;
	
	/* call the global plugins first and then the backend specific */
	for ( p = get_plugin_list(PLUGIN_LIST_ACL); p != NULL; p = p->plg_next ) {
		if (plugin_invoke_plugin_sdn (p, SLAPI_PLUGIN_ACL_ALLOW_ACCESS, pb, 
									  (Slapi_DN*)slapi_entry_get_sdn_const (e))){
			aclplugin_initialized = 1;
			rc = (*p->plg_acl_access_allowed)(pb, e, attrs, val, access, flags, errbuf);
			if ( rc != LDAP_SUCCESS ) break;
		}
	}

	if (! aclplugin_initialized ) {
		rc = acl_default_access ( pb, e, access);
	}
	return rc;
}

int 
plugin_call_acl_mods_access ( Slapi_PBlock *pb, Slapi_Entry *e, LDAPMod **mods, char **errbuf )
{

	struct slapdplugin	*p;
	int			aclplugin_initialized = 0;
	int			rc = LDAP_INSUFFICIENT_ACCESS;
	Operation	*operation;

	slapi_pblock_get (pb, SLAPI_OPERATION, &operation);

	/* we don't perform acl check for internal operations  and if the plugin has set it not to be checked */
	if (operation_is_flag_set(operation, SLAPI_OP_FLAG_NO_ACCESS_CHECK|OP_FLAG_INTERNAL|OP_FLAG_REPLICATED|OP_FLAG_LEGACY_REPLICATION_DN))
		return LDAP_SUCCESS;
	
	/* call the global plugins first and then the backend specific */
	for ( p = get_plugin_list(PLUGIN_LIST_ACL); p != NULL; p = p->plg_next ) {
		if (plugin_invoke_plugin_sdn (p, SLAPI_PLUGIN_ACL_MODS_ALLOWED, pb, 
									  (Slapi_DN*)slapi_entry_get_sdn_const (e))){
			aclplugin_initialized = 1;
			rc = (*p->plg_acl_mods_allowed)( pb, e, mods, errbuf );
			if ( rc != LDAP_SUCCESS ) break;
		}
	}
	if (! aclplugin_initialized ) {
		rc = acl_default_access ( pb, e, SLAPI_ACL_WRITE);
	}
	return rc;
}

/* This plugin should be called immediatly after the changes have been comitted */
/* This function is now fully executed for internal and replicated ops. */
int 
plugin_call_acl_mods_update ( Slapi_PBlock *pb, int optype )
{
	struct slapdplugin	*p;
	char 				*dn;
	int					rc = 0;
   	void				*change = NULL;
   	Slapi_Entry			*te = NULL;
    Slapi_DN			sdn;
	Operation			*operation;

	slapi_pblock_get (pb, SLAPI_OPERATION, &operation);

	(void)slapi_pblock_get( pb, SLAPI_TARGET_DN, &dn );

	switch ( optype ) {
 	  case SLAPI_OPERATION_MODIFY:
		(void)slapi_pblock_get( pb, SLAPI_MODIFY_MODS, &change );
		break;
	  case SLAPI_OPERATION_ADD:
		(void)slapi_pblock_get( pb, SLAPI_ADD_ENTRY, &change );
		te = (Slapi_Entry *)change;
		if(!slapi_sdn_isempty(slapi_entry_get_sdn(te)))
		{
		    dn= (char*)slapi_sdn_get_ndn(slapi_entry_get_sdn(te)); /* jcm - Had to cast away const */
		}
		break;
    	  case SLAPI_OPERATION_MODRDN:
		(void)slapi_pblock_get( pb, SLAPI_MODRDN_NEWRDN, &change );
		break;
    	}
	
	slapi_sdn_init_dn_byref (&sdn, dn);
	/* call the global plugins first and then the backend specific */
	for ( p = get_plugin_list(PLUGIN_LIST_ACL); p != NULL; p = p->plg_next ) {
		if (plugin_invoke_plugin_sdn (p, SLAPI_PLUGIN_ACL_MODS_UPDATE, pb, &sdn)){
			rc = (*p->plg_acl_mods_update)(pb, optype, dn, change );
			if ( rc != LDAP_SUCCESS ) break;
		}
	}

	slapi_sdn_done (&sdn);
	return rc;
}

int
plugin_call_acl_verify_syntax ( Slapi_PBlock *pb, Slapi_Entry *e, char **errbuf )
{

	struct slapdplugin	*p;
	int			rc = 0;
	int			plugin_called = 0;
	Operation	*operation;

	slapi_pblock_get (pb, SLAPI_OPERATION, &operation);

	/* we don't perform acl check for internal operations  and if the plugin has set it not to be checked */
	if (operation_is_flag_set(operation, SLAPI_OP_FLAG_NO_ACCESS_CHECK|OP_FLAG_INTERNAL|OP_FLAG_REPLICATED|OP_FLAG_LEGACY_REPLICATION_DN))
		return LDAP_SUCCESS;

	/* call the global plugins first and then the backend specific */
	for ( p = get_plugin_list(PLUGIN_LIST_ACL); p != NULL; p = p->plg_next ) {
		if (plugin_invoke_plugin_sdn (p, SLAPI_PLUGIN_ACL_SYNTAX_CHECK, pb, 
									  (Slapi_DN*)slapi_entry_get_sdn_const (e))){
			plugin_called = 1;
			rc = (*p->plg_acl_syntax_check)( e, errbuf );
			if ( rc != LDAP_SUCCESS ) break;
		}
	}

	if ( !plugin_called ) {
		LDAPDebug ( LDAP_DEBUG_ANY, "The ACL plugin is not initialized. The aci syntax cannot be verified\n",0,0,0);
	}
	return rc;
}

int slapi_access_allowed( Slapi_PBlock *pb, Slapi_Entry *e, char *attr,
        		  struct berval *val, int access ) 
{
	char	*attrs[2] = { NULL, NULL };

	attrs[0] = attr;
	return ( plugin_call_acl_plugin ( pb, e, attrs, val, access, ACLPLUGIN_ACCESS_DEFAULT, NULL ) );
}

int slapi_acl_check_mods( Slapi_PBlock *pb, Slapi_Entry *e, LDAPMod **mods, char **errbuf )
{

	return ( plugin_call_acl_mods_access ( pb, e, mods, errbuf ) );


}

int slapi_acl_verify_aci_syntax (Slapi_PBlock *pb, Slapi_Entry *e, char **errbuf) 
{
	return ( plugin_call_acl_verify_syntax ( pb, e, errbuf ) );
}

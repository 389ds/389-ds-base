/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

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

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
/*
 * There are 3 ACL PLUGIN points
 * PREOP, POSTOP and ACL plugin
 *
 */
#include "acl.h"
#include "dirver.h"
#include <dirlite_strings.h> /* PLUGIN_MAGIC_VENDOR_STR */

static Slapi_PluginDesc pdesc = { "acl", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT, "acl access check plugin" };
char *plugin_name = ACL_PLUGIN_NAME;

/* Prototypes */

static int aclplugin_preop_search ( Slapi_PBlock *pb );
static int aclplugin_preop_modify ( Slapi_PBlock *pb );
static int aclplugin_preop_common ( Slapi_PBlock *pb );

/*******************************************************************************
 *  ACL PLUGIN Architecture
 *
 *	There are 3 registered plugins:
 *
 *	1) PREOP ACL Plugin
 *		The preop plugin does all the initialization. It allocate the ACL
 *		PBlock and copies stuff from the connection if it needs to.
 *	
 *	2) POSTOP ACL Plugin
 *		The Postop plugin cleans up the ACL PBlock. It copies Back to the
 *		connection struct. The Postop bind & Unbind  cleans up the 
 *		ACL CBlock ( struct hanging from conn struct ).
 *
 *	3) ACCESSCONTROL Plugin
 *		Main module which does the access check. There are 5 entry point
 *		from this plugin 
 *		a) Initilize the ACL system i.e read all the ACLs and generate the
 *		   the ACL List.
 *		b) Check for ACI syntax.
 *		c) Check for normal access.
 *		d) Check for access to a mod request.
 *		e) Update the in-memory ACL List.
 *
 *******************************************************************************/

/*******************************************************************************
 * PREOP
 *******************************************************************************/

/* Plugin identity is passed by the server in the plugin init function and must 
   be supplied by the plugin to all internal operations it initiates
 */
void* g_acl_preop_plugin_identity;

int
acl_preopInit (Slapi_PBlock *pb)
{
	int rc = 0;
	
	/* save plugin identity to later pass to internal operations */
	rc = slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &g_acl_preop_plugin_identity);

	/* Declare plugin version */
	rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01);

    	/* Provide descriptive information */
	rc |=  slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void*)&pdesc);

	/* Register functions */
	rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_SEARCH_FN, (void*)aclplugin_preop_search);
	rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_COMPARE_FN, (void*)aclplugin_preop_search);
	rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_ADD_FN,    (void*)aclplugin_preop_modify);
	rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_MODIFY_FN, (void*)aclplugin_preop_modify);
	rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_MODRDN_FN, (void*)aclplugin_preop_modify);
	rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_DELETE_FN, (void*)aclplugin_preop_modify);

#if 0
	/*
	 * XXXmcs: In order to support access control checking from
	 * extended operations, we need a SLAPI_PLUGIN_PRE_EXTENDED_FN hook.
	 * But today no such entry point exists.
	 */
	rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_EXTENDED_FN, (void*)aclplugin_preop_modify);
#endif


        slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "<= acl_preop_Init %d\n", rc, 0, 0 );
        return( rc );
}

/* For preop search we do two things:
 * 1) based on the search base, we preselect the acls.
 * 2) also get hold of a acl_pblock for use 
 */
static int
aclplugin_preop_search ( Slapi_PBlock *pb )
{
	int 		scope;
	char		*base = NULL;
	int			optype;
	int			isRoot;
	int			rc = 0;
			
	TNF_PROBE_0_DEBUG(aclplugin_preop_search_start ,"ACL","");

	slapi_pblock_get ( pb, SLAPI_OPERATION_TYPE, &optype );
	slapi_pblock_get ( pb, SLAPI_REQUESTOR_ISROOT, &isRoot );

	if ( isRoot ) {
		TNF_PROBE_1_DEBUG(aclplugin_preop_search_end ,"ACL","",
							tnf_string,isroot,"");
		return rc;
	}

	slapi_pblock_get( pb, SLAPI_SEARCH_TARGET, &base );
	/* For anonymous client  doing search nothing needs to be set up */
	if ( optype == SLAPI_OPERATION_SEARCH && aclanom_is_client_anonymous ( pb )  &&
			! slapi_dn_issuffix( base, "cn=monitor") ) {
				TNF_PROBE_1_DEBUG(aclplugin_preop_search_end ,"ACL","",
									tnf_string,anon,"");
		return rc;
	}

	if ( 0 == ( rc = aclplugin_preop_common( pb ))) {
		slapi_pblock_get( pb, SLAPI_SEARCH_SCOPE, &scope );
		acllist_init_scan ( pb, scope, base );
	}

	TNF_PROBE_0_DEBUG(aclplugin_preop_search_end ,"ACL","");

	return rc;
}

/* 
 * For rest of the opertion type, we get a hold of the acl
 * private Block.
 */
static int
aclplugin_preop_modify ( Slapi_PBlock *pb )
{
	/*
	 * Note: since we don't keep the anom profile for modifies, we have to go
	 * through the regular process to check the access.
	*/
	return aclplugin_preop_common( pb );
}

/*
 * Common function that is called by aclplugin_preop_search() and
 * aclplugin_preop_modify().
 *
 * Return values:
 *	0 - all is well; proceed.
 *  1 - fatal error; result has been sent to client.
 */ 
static int
aclplugin_preop_common( Slapi_PBlock *pb )
{
	char		*proxy_dn;	/* id being assumed */
	char		*dn;		/* proxy master */
	char		*errtext = NULL;
	int			lderr;
	Acl_PBlock	*aclpb;

	TNF_PROBE_0_DEBUG(aclplugin_preop_common_start ,"ACL","");

	aclpb = acl_get_aclpb ( pb, ACLPB_BINDDN_PBLOCK );

	/*
	 * The following mallocs memory for proxy_dn, but not the dn.
	 * The proxy_dn is the id being assumed, while dn
	 * is the "proxy master".
	*/
	proxy_dn = NULL;
	if ( LDAP_SUCCESS != ( lderr = acl_get_proxyauth_dn( pb, &proxy_dn,
			&errtext ))) {
		/*
		 * Fatal error -- send a result to the client and arrange to skip
		 * any further processing.
		 */
		slapi_send_ldap_result( pb, lderr, NULL, errtext, 0, NULL );
		TNF_PROBE_1_DEBUG(aclplugin_preop_common_end ,"ACL","",
						tnf_string,proxid_error,"");

		return 1;	/* skip any further processing */
	}
	slapi_pblock_get ( pb, SLAPI_REQUESTOR_DN, &dn );


	/*
	 * The dn is copied into the aclpb during initialization.
	*/
	if ( proxy_dn) {
		TNF_PROBE_0_DEBUG(proxyacpb_init_start,"ACL","");

		slapi_log_error( SLAPI_LOG_ACL, plugin_name,
				"proxied authorization dn is (%s)\n", proxy_dn );
		acl_init_aclpb ( pb, aclpb, proxy_dn, 1 );
		aclpb = acl_new_proxy_aclpb (pb );
		acl_init_aclpb ( pb, aclpb, dn, 0 );
		slapi_ch_free ( (void **) &proxy_dn );
		
		TNF_PROBE_0_DEBUG(proxyacpb_init_end,"ACL","");
 
	} else {
		TNF_PROBE_0_DEBUG(aclpb_init_start,"ACL","");
		acl_init_aclpb ( pb, aclpb, dn, 1 );
		TNF_PROBE_0_DEBUG(aclpb_init_end,"ACL","");

	}

	TNF_PROBE_0_DEBUG(aclplugin_preop_common_end ,"ACL","");

	return 0;
}

/*******************************************************************************
 * POSTOP
 *******************************************************************************/

/*******************************************************************************
 * ACCESSCONTROL PLUGIN
 *******************************************************************************/

void* g_acl_plugin_identity;

/* For now, the acl component is implemented as 2 different plugins */
/* Return the right plugin identity 				    */
void * aclplugin_get_identity(int plug) {
        if (plug == ACL_PLUGIN_IDENTITY)
                return g_acl_plugin_identity;
        if (plug == ACL_PREOP_PLUGIN_IDENTITY)
                return g_acl_preop_plugin_identity;
        return NULL;
}

int
aclplugin_init (Slapi_PBlock *pb )
{

	int rc = 0; /* OK */
	rc = aclinit_main ( pb );

	return  rc;

}
int
aclplugin_stop ( Slapi_PBlock *pb )
{
	int rc = 0; /* OK */

	/* nothing to be done now */
	return  rc;
}

int
acl_init( Slapi_PBlock *pb )
{
        int     rc =0;

        slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "=> acl_init\n", 0, 0, 0 );

        if  ( 0 != acl_init_ext() ) {
		    slapi_log_error ( SLAPI_LOG_FATAL, plugin_name,
			    "Unable to initialize the extensions\n");
		    return 1;
	    }

		/* save plugin identity to later pass to internal operations */
		rc = slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &g_acl_plugin_identity);

        rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
            (void *) SLAPI_PLUGIN_VERSION_01 );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
            (void *)&pdesc );

		rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_START_FN, (void *) aclplugin_init );
		rc = slapi_pblock_set( pb, SLAPI_PLUGIN_CLOSE_FN, (void *) aclplugin_stop );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_ACL_SYNTAX_CHECK, 
            (void *) acl_verify_aci_syntax );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_ACL_ALLOW_ACCESS,
            (void *) acl_access_allowed_main );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_ACL_MODS_ALLOWED,
            (void *) acl_check_mods );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_ACL_MODS_UPDATE,
            (void *) acl_modified );

        slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "<= acl_init %d\n", rc, 0, 0 );
        return( rc );
}

/*
 *
 * acl_access_allowed_main
 *	Main interface to the plugin. Calls different access check functions
 *	based on the flag.
 *
 *	
 *  Returns:
 *	LDAP_SUCCESS			-- access is granted
 *	LDAP_INSUFFICIENT_ACCESS	-- access denied
 *	<other ldap error>		-- ex: opererations error
 *
 */
int
acl_access_allowed_main ( Slapi_PBlock *pb, Slapi_Entry *e, char **attrs, 
			  struct berval *val, int access , int flags, char **errbuf)
{
	int	rc =0;
	char	*attr = NULL;

	TNF_PROBE_0_DEBUG(acl_access_allowed_main_start,"ACL","");

	if (attrs && *attrs) attr = attrs[0];

	if (ACLPLUGIN_ACCESS_READ_ON_ENTRY == flags)
		rc = acl_read_access_allowed_on_entry ( pb, e, attrs, access);
	else if ( ACLPLUGIN_ACCESS_READ_ON_ATTR == flags)
		rc = acl_read_access_allowed_on_attr ( pb, e, attr, val, access);
	else if ( ACLPLUGIN_ACCESS_READ_ON_VLV == flags)
		rc =  acl_access_allowed_disjoint_resource ( pb, e, attr, val, access);
	else if ( ACLPLUGIN_ACCESS_MODRDN == flags)
		rc =  acl_access_allowed_modrdn ( pb, e, attr, val, access);
	else if ( ACLPLUGIN_ACCESS_GET_EFFECTIVE_RIGHTS == flags)
		rc =  acl_get_effective_rights ( pb, e, attrs, val, access, errbuf );
	else
		rc = acl_access_allowed ( pb, e, attr, val, access);

	/* generate the appropriate error message */
	if ( ( rc != LDAP_SUCCESS ) && errbuf && 
		 ( ACLPLUGIN_ACCESS_GET_EFFECTIVE_RIGHTS != flags ) &&
		 ( access & ( SLAPI_ACL_WRITE | SLAPI_ACL_ADD | SLAPI_ACL_DELETE ))) {

		char	*edn  = slapi_entry_get_dn ( e );

		acl_gen_err_msg(access, edn, attr, errbuf);
	}
	
	TNF_PROBE_0_DEBUG(acl_access_allowed_main_end,"ACL","");

	return rc;
}
#ifdef _WIN32

int *module_ldap_debug = 0;
void plugin_init_debug_level ( int *level_ptr )
{
	module_ldap_debug = level_ptr;
}
#endif


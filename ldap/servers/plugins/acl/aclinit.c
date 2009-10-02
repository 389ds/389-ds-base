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

#include    "acl.h"

static int 		__aclinit__RegisterLases(void);
static int 		__aclinit__RegisterAttributes(void);
static int		__aclinit_handler(Slapi_Entry *e, void *callback_data);

/***************************************************************************
*
* aclinit_main()
*	Main routine which is called at the server boot up time. 
*
*	1) Reads all the ACI entries from the database and creates
*	   the ACL list.
*   2) Registers all the LASes and the GetAttrs supported by the DS.
*	3) Generates anonymous profiles.
*   4) Registers proxy control
* 	5) Creates aclpb pool 
*
* Input:
*	None.
*
* Returns:
*	0		-- no error
*	1		-- Error
*
* Error Handling:
*	If any error found during the ACL generation, error is logged.
*
**************************************************************************/
static int acl_initialized = 0;
int
aclinit_main()
{
	Slapi_PBlock		*pb;
	int					rv;
	Slapi_DN			*sdn;
	void 				*node;

	if (acl_initialized) {
		/* There is no need to do anything more */
		return 0;
	}

	/* Initialize the LIBACCESS ACL library */
	if (ACL_Init() != 0) {
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
			 "ACL Library Initialization failed\n");
		return 1;
	}
	
	/* register all the LASes supported by the DS */
	if (ACL_ERR == __aclinit__RegisterLases()) {
		/* Error is already logged */
		return 1;
	}

	/* Register all the Attrs */
	if (ACL_ERR == __aclinit__RegisterAttributes()) {
		/* Error is already logged */
		return 1;
	}

	/*
	 * Register to get backend state changes so we can add/remove
	 * acis from backends that come up and go down.
	*/

	slapi_register_backend_state_change((void *) NULL, acl_be_state_change_fnc);
	

	/* register the extensions */
	/* ONREPL Moved to the acl_init function because extensions
       need to be registered before any operations are issued
    if  ( 0 != acl_init_ext() ) {
		slapi_log_error ( SLAPI_LOG_FATAL, plugin_name,
			"Unable to initialize the extensions\n");
		return 1;
	} */

	/* create the mutex array */
	if ( 0 != aclext_alloc_lockarray ( ) ) {
		slapi_log_error ( SLAPI_LOG_FATAL, plugin_name,
			"Unable to create the mutext array\n");
		return 1;
	}

    /* Allocate the pool */
	if ( 0 != acl_create_aclpb_pool () ) {
		slapi_log_error ( SLAPI_LOG_FATAL, plugin_name,
			"Unable to create the acl private pool\n");
		return 1;
	}

	/*
	 * Now read all the ACLs from all the backends and put it
	 * in a list
	 */
	/* initialize the ACLLIST sub-system */
	if ( 0 != (rv = acllist_init ( ))) {
		slapi_log_error ( SLAPI_LOG_FATAL, plugin_name,
				"Unable to initialize the plugin:%d\n", rv );
		return 1;
	}

	/* Initialize the anonymous profile i.e., generate it */
	rv = aclanom_init ();

	pb = slapi_pblock_new();
	
	/*
	 * search for the aci_attr_type attributes of all entries.
	 *
	 * slapi_get_fist_suffix() and slapi_get_next_suffix() do not return the 
	 * rootdse entry so we search for acis in there explicitly here.
	*/

	sdn = slapi_sdn_new_dn_byval("");
	slapi_log_error ( SLAPI_LOG_ACL, plugin_name,
				"Searching for all acis(scope base) at suffix ''\n");
	aclinit_search_and_update_aci ( 0,		/* thisbeonly */
										sdn,	/* base */
										NULL,	/* be name*/
										LDAP_SCOPE_BASE, ACL_ADD_ACIS,
										DO_TAKE_ACLCACHE_WRITELOCK);
	slapi_sdn_free(&sdn);	

	sdn = slapi_get_first_suffix( &node, 1 );
	while (sdn)
	{
		slapi_log_error ( SLAPI_LOG_ACL, plugin_name,
				"Searching for all acis(scope subtree) at suffix '%s'\n", 
					slapi_sdn_get_dn(sdn) );
		aclinit_search_and_update_aci ( 0,		/* thisbeonly */
										sdn,	/* base */
										NULL,	/* be name*/
										LDAP_SCOPE_SUBTREE, ACL_ADD_ACIS,
										DO_TAKE_ACLCACHE_WRITELOCK);
		sdn = slapi_get_next_suffix( &node, 1 );
	}

	/* Initialize it. */
	acl_initialized = 1;

	/* generate the signatures */
	acl_set_aclsignature ( aclutil_gen_signature ( 100 ) );

	/* Initialize the user-group cache */
	rv = aclgroup_init ( );

	aclanom_gen_anomProfile (DO_TAKE_ACLCACHE_READLOCK);

	/* Register both of the proxied authorization controls (version 1 and 2) */
	slapi_register_supported_control( LDAP_CONTROL_PROXYAUTH,
			SLAPI_OPERATION_SEARCH | SLAPI_OPERATION_COMPARE
			| SLAPI_OPERATION_ADD | SLAPI_OPERATION_DELETE
			| SLAPI_OPERATION_MODIFY | SLAPI_OPERATION_MODDN
			| SLAPI_OPERATION_EXTENDED );
	slapi_register_supported_control( LDAP_CONTROL_PROXIEDAUTH,
			SLAPI_OPERATION_SEARCH | SLAPI_OPERATION_COMPARE
			| SLAPI_OPERATION_ADD | SLAPI_OPERATION_DELETE
			| SLAPI_OPERATION_MODIFY | SLAPI_OPERATION_MODDN
			| SLAPI_OPERATION_EXTENDED );

	slapi_pblock_destroy ( pb );
	return 0;
}
/*
 * This routine is the one that scans for acis and either adds them
 * to the internal cache (op==ACL_ADD_ACIS) or deletes them
 * (op==ACL_REMOVE_ACIS).
 *
 * If thisbeonly is 0 the search
 * is conducted on the base with the specifed scope and be_name is ignored.
 * This is used at startup time where we iterate over all suffixes, searching
 * for all the acis in the DIT to load the ACL cache.
 *
 * If thisbeonly is 1 then then a be_name must be specified.
 * In this case we will  search in that backend ONLY.
 * This is used in the case where a backend is turned on and off--in this
 * case we only want to add/remove the acis in that particular backend and
 * not for example in any backends below that one.
*/

int
aclinit_search_and_update_aci ( int thisbeonly, const Slapi_DN *base,
								char *be_name, int scope, int op,
								acl_lock_flag_t lock_flag )
{
	char				*attrs[2] = { "aci", NULL };
	 /* Tell __aclinit_handler whether it's an add or a delete */
	Slapi_PBlock 	*aPb;
	LDAPControl		**ctrls=NULL;
	struct berval	*bval;
	aclinit_handler_callback_data_t call_back_data;

	PR_ASSERT( lock_flag == DONT_TAKE_ACLCACHE_WRITELOCK ||
				lock_flag == DO_TAKE_ACLCACHE_WRITELOCK);

	if ( thisbeonly && be_name == NULL) {
		slapi_log_error ( SLAPI_LOG_FATAL, plugin_name, 
						"Error: This  be_name must be specified.\n");
		return -1;
	}


	/*
	 * We need to explicitly request (objectclass=ldapsubentry)
	 * in order to get all the subentry acis too.
	 * Note that subentries can be added under subentries (although its not
	 * recommended) so that
	 * there may be non-trivial acis under a subentry.
	*/ 

	/* Use new search internal API                 */
	/* and never retrieve aci from a remote server */
	aPb = slapi_pblock_new ();
		
	/*
	 * Set up the control to say "Only get acis from this Backend--
	 * there may be more backends under this one.
	*/

	if ( thisbeonly ) {		
		
		bval = (struct berval *)slapi_ch_malloc(sizeof(struct berval));
		bval->bv_len = strlen(be_name) + 1;
		bval->bv_val = slapi_ch_strdup(be_name);

		ctrls = (LDAPControl **)slapi_ch_calloc( 2, sizeof(LDAPControl *));
		ctrls[0] = NULL;
		ctrls[1] = NULL;
	
		slapi_build_control_from_berval(
										MTN_CONTROL_USE_ONE_BACKEND_OID,
                						bval,
										1 /* is critical */, 
										ctrls);

	}	

	slapi_search_internal_set_pb (	aPb,
					slapi_sdn_get_dn(base),
					scope,
					"(|(aci=*)(objectclass=ldapsubentry))",
					attrs,
					0 /* attrsonly */,
					ctrls /* controls: SLAPI_ARGCONTROLS */,
					NULL /* uniqueid */,
					aclplugin_get_identity (ACL_PLUGIN_IDENTITY),
					SLAPI_OP_FLAG_NEVER_CHAIN /* actions : get local aci only */);
	
	if (thisbeonly) {
		slapi_pblock_set(aPb, SLAPI_REQCONTROLS, ctrls);
	}

	call_back_data.op = op;
	call_back_data.retCode = 0;
	call_back_data.lock_flag = lock_flag;

	slapi_search_internal_callback_pb(aPb,
					  &call_back_data /* callback_data */,
					  NULL/* result_callback */,
					  __aclinit_handler,
					  NULL /* referral_callback */);

	if (thisbeonly) {				
		slapi_ch_free((void **)&bval);				
	}	

	/*
	 * This frees the control oid, the bv_val and the control itself and the 
	 * ctrls array mem by caling ldap_controls_free()--so we
	 * don't need to do it ourselves.
	*/
	slapi_pblock_destroy (aPb);
	
	return call_back_data.retCode; 

}

/***************************************************************************
*
* __aclinit_handler
*
*	For each entry, finds if there is any ACL in thet entry. If there is
*	then the ACL is processed and stored in the ACL LIST.
*
*
* Input:
*
*
* Returns:
*	None.
*
* Error Handling:
*	If any error found during the ACL generation, the ACL is
*	logged.  Also, set in the callback_data so that caller can act upon it.
*
**************************************************************************/
static int
__aclinit_handler ( Slapi_Entry *e, void *callback_data)	
{
    Slapi_Attr 		*attr;
	aclinit_handler_callback_data_t *call_back_data = 
		(aclinit_handler_callback_data_t*)callback_data;	
	Slapi_DN			*e_sdn;
	int					rv;
	Slapi_Value 		*sval=NULL;

	call_back_data->retCode = 0;		 /* assume success--if there's an error we overwrite it */
    if (e != NULL) {		

		e_sdn = slapi_entry_get_sdn ( e );	

		/*
	 	 * Take the write lock around all the mods--so that
	 	 * other operations will see the acicache either before the whole mod
		 * or after but not, as it was before, during the mod.
		 * This is in line with the LDAP concept of the operation
		 * on the whole entry being the atomic unit.
	 	 * 
		*/
		
		if ( call_back_data->op == ACL_ADD_ACIS ) {
			slapi_log_error ( SLAPI_LOG_ACL, plugin_name,
				"Adding acis for entry '%s'\n", slapi_sdn_get_dn(e_sdn));
			slapi_entry_attr_find ( e, aci_attr_type, &attr );

			if ( attr ) {
				
				const struct berval	*attrValue;				
				
				int i;
				if ( call_back_data->lock_flag == DO_TAKE_ACLCACHE_WRITELOCK) {
					acllist_acicache_WRITE_LOCK();
				}
				i= slapi_attr_first_value ( attr, &sval );
				while(i != -1) {
		        	attrValue = slapi_value_get_berval(sval);									
					
						if ( 0 != (rv=acllist_insert_aci_needsLock (e_sdn, attrValue))) {
							aclutil_print_err(rv, e_sdn, attrValue, NULL); 

							/* We got an error; Log it  and then march along */
							slapi_log_error ( SLAPI_LOG_FATAL, plugin_name, 
									  "Error: This  (%s) ACL will not be considered for evaluation"
									  " because of syntax errors.\n", 
									  attrValue->bv_val ? attrValue->bv_val: "NULL");
							call_back_data->retCode = rv;
						}				
					i= slapi_attr_next_value( attr, i, &sval );
				}/* while */
				if ( call_back_data->lock_flag == DO_TAKE_ACLCACHE_WRITELOCK) {
					acllist_acicache_WRITE_UNLOCK();
				}
			}
		} else if (call_back_data->op == ACL_REMOVE_ACIS) {

			/* Here we are deleting the acis. */
				slapi_log_error ( SLAPI_LOG_ACL, plugin_name, "Removing acis\n");
				if ( call_back_data->lock_flag == DO_TAKE_ACLCACHE_WRITELOCK) {
					acllist_acicache_WRITE_LOCK();
				}	
				if ( 0 != (rv=acllist_remove_aci_needsLock(e_sdn, NULL))) {
					aclutil_print_err(rv, e_sdn, NULL, NULL); 

					/* We got an error; Log it  and then march along */
					slapi_log_error ( SLAPI_LOG_FATAL, plugin_name, 
									  "Error: ACls not deleted from %s\n",
                                      slapi_sdn_get_dn(e_sdn));
					call_back_data->retCode = rv;
				}
				if ( call_back_data->lock_flag == DO_TAKE_ACLCACHE_WRITELOCK) {
					acllist_acicache_WRITE_UNLOCK();
				}
		}
		
	}

	/*
	 * If we get here it's success.
	 * The call_back_data->error is the error code that counts as it's the
	 * one that the original caller will see--this routine is called off a callbacl.
	*/
	
    return ACL_FALSE;	/* "local" error code--it's 0 */
}
/***************************************************************************
*
* __acl__RegisterAttributes
*
*	Register all the attributes supported by the DS.
*
* Input:
*	None.
*
* Returns:
*	ACL_OK		- No error
*	ACL_ERR		- in case of errror
*
* Error Handling:
*	None.
*
**************************************************************************/
static int
__aclinit__RegisterAttributes(void)
{

	ACLMethod_t	methodinfo;
	NSErr_t		errp;
	int		rv;

	memset (&errp, 0, sizeof(NSErr_t));
	
	rv = ACL_MethodRegister(&errp, DS_METHOD, &methodinfo);
	if (rv < 0) {
		acl_print_acllib_err(&errp, NULL);
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name, 
			  "Unable to Register the methods\n");
		return ACL_ERR;
	}
	rv = ACL_MethodSetDefault (&errp,  methodinfo);
	if (rv < 0) {
		acl_print_acllib_err(&errp, NULL);
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name, 
			  "Unable to Set the default method\n");
		return ACL_ERR;
	}
        rv = ACL_AttrGetterRegister(&errp, ACL_ATTR_IP, DS_LASIpGetter,
				methodinfo, ACL_DBTYPE_ANY, ACL_AT_FRONT, NULL);
	if (rv < 0) {
		acl_print_acllib_err(&errp, NULL);
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name, 
			  "Unable to Register Attr ip\n");
		return ACL_ERR;
	}
        rv = ACL_AttrGetterRegister(&errp, ACL_ATTR_DNS, DS_LASDnsGetter,
				methodinfo, ACL_DBTYPE_ANY, ACL_AT_FRONT, NULL);
	if (rv < 0) {
		acl_print_acllib_err(&errp, NULL);
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name, 
			  "Unable to Register Attr dns\n");
		return ACL_ERR;
	}
	return ACL_OK;
}

/***************************************************************************
*
* __acl__RegisterLases
*	Register all the LASes supported by the DS.
*
*	The DS doesnot support user/group. We have defined our own LAS
*	so that we can display/print an error when the LAS is invoked.
* Input:
*	None.
*
* Returns:
*	ACL_OK		- No error
*	ACL_ERR		- in case of errror
*
* Error Handling:
*	None.
*
**************************************************************************/
static int
__aclinit__RegisterLases(void)
{

	if (ACL_LasRegister(NULL, DS_LAS_USER, (LASEvalFunc_t) DS_LASUserEval, 
				(LASFlushFunc_t) NULL) <  0) {
		slapi_log_error (SLAPI_LOG_FATAL, plugin_name,
				"Unable to register USER Las\n");
		return ACL_ERR;
	}
	if (ACL_LasRegister(NULL, DS_LAS_GROUP, (LASEvalFunc_t) DS_LASGroupEval, 
				(LASFlushFunc_t) NULL) <  0) {
		slapi_log_error (SLAPI_LOG_FATAL, plugin_name,
				"Unable to register GROUP Las\n");
		return ACL_ERR;
	}
	if (ACL_LasRegister(NULL, DS_LAS_GROUPDN, (LASEvalFunc_t)DS_LASGroupDnEval, 
				(LASFlushFunc_t)NULL) < 0) {
		slapi_log_error (SLAPI_LOG_FATAL, plugin_name,
				"Unable to register GROUPDN Las\n");
		return ACL_ERR;
	}
	if (ACL_LasRegister(NULL, DS_LAS_ROLEDN, (LASEvalFunc_t)DS_LASRoleDnEval, 
				(LASFlushFunc_t)NULL) < 0) {
		slapi_log_error (SLAPI_LOG_FATAL, plugin_name,
				"Unable to register ROLEDN Las\n");
		return ACL_ERR;
	}
	if (ACL_LasRegister(NULL, DS_LAS_USERDN, (LASEvalFunc_t)DS_LASUserDnEval, 
				(LASFlushFunc_t)NULL) < 0) {
		slapi_log_error (SLAPI_LOG_FATAL, plugin_name,
				"Unable to register USERDN Las\n");
		return ACL_ERR;
	}
	if (ACL_LasRegister(NULL, DS_LAS_USERDNATTR, 
				(LASEvalFunc_t)DS_LASUserDnAttrEval, 
				(LASFlushFunc_t)NULL) < 0) {
		slapi_log_error (SLAPI_LOG_FATAL, plugin_name,
				"Unable to register USERDNATTR Las\n");
		return ACL_ERR;
	}
	if (ACL_LasRegister(NULL, DS_LAS_AUTHMETHOD, 
				(LASEvalFunc_t)DS_LASAuthMethodEval, 
				(LASFlushFunc_t)NULL) < 0) {
		slapi_log_error (SLAPI_LOG_FATAL, plugin_name,
			"Unable to register CLIENTAUTHTYPE Las\n");
		return ACL_ERR;
	}
	if (ACL_LasRegister(NULL, DS_LAS_GROUPDNATTR,
				(LASEvalFunc_t)DS_LASGroupDnAttrEval,
				(LASFlushFunc_t)NULL) < 0) {
		slapi_log_error (SLAPI_LOG_FATAL, plugin_name,
				"Unable to register GROUPDNATTR Las\n");
		return ACL_ERR;
	}
	if (ACL_LasRegister(NULL, DS_LAS_USERATTR,
				(LASEvalFunc_t)DS_LASUserAttrEval,
				(LASFlushFunc_t)NULL) < 0) {
		slapi_log_error (SLAPI_LOG_FATAL, plugin_name,
				"Unable to register USERATTR Las\n");
		return ACL_ERR;
	}
	if (ACL_LasRegister(NULL, DS_LAS_SSF,
				(LASEvalFunc_t)DS_LASSSFEval,
				(LASFlushFunc_t)NULL) < 0) {
		slapi_log_error (SLAPI_LOG_FATAL, plugin_name,
			"Unable to register SSF Las\n");
		return ACL_ERR;
	}
	return ACL_OK;
}

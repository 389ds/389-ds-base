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

#include	"acl.h"

/****************************************************************************
*
* acl.c
*
*
*	This file contains the functions related to Access Control List	(ACL)
*	checking. The ACL checking is based on the ONE ACL design implemented
*	in the Web server 2.0. For more	information on the ACL design look
*	into the barracuda home	page.
*
*
******************************************************************************/
 

/****************************************************************************/
/*	Globals. Must be protected by Mutex.				    */
/****************************************************************************/
/* Signatures to see if	things have changed */
static	short	acl_signature =	0;

/****************************************************************************/
/* Defines, Constants, ande Declarations				    */
/****************************************************************************/
static char *ds_map_generic[2] = { NULL, NULL };

/****************************************************************************/
/* prototypes								    */
/****************************************************************************/
static	int	acl__resource_match_aci(struct acl_pblock *aclpb, aci_t	*aci ,
								int skip_attrEval, int *a_matched);
static	int acl__TestRights(Acl_PBlock *aclpb,int access, char **right,
						char ** map_generic, aclResultReason_t *result_reason);
static int 	acl__scan_for_acis(struct acl_pblock *aclpb, int *err);
static void	acl__reset_cached_result (struct acl_pblock *aclpb );
static int 	acl__scan_match_handles ( struct acl_pblock *aclpb, int type);
static int 	acl__attr_cached_result (struct acl_pblock *aclpb, char *attr, int access );
static int 	acl__match_handlesFromCache (struct acl_pblock *aclpb, char *attr, int access);
static int	acl__get_attrEval ( struct acl_pblock *aclpb, char *attr );
static int	acl__recompute_acl (Acl_PBlock *aclpb, AclAttrEval *a_eval,
									int	access,	int	aciIndex);
static void	__acl_set_aclIndex_inResult ( Acl_PBlock *aclpb,
												int access, int	index );
static int	acl__make_filter_test_entry ( Slapi_Entry **entry,
									char *attr_type, struct	berval *attr_val);
static int	acl__test_filter ( Slapi_Entry *entry, struct slapi_filter *f,
							 int filter_sense);
static void print_access_control_summary( char * source,
									int ret_val, char *clientDn,
									struct	acl_pblock	*aclpb,
									char *right,
									char *attr,
									const char *edn,
									aclResultReason_t *acl_reason);
static int check_rdn_access( Slapi_PBlock *pb,Slapi_Entry *e, char * newrdn,
						int access);


/*
 * Check the rdn permissions for this entry:
 * 	require: write access to the entry, write (add) access to the new
 * naming attribute, write (del) access to the old naming attribute if
 * deleteoldrdn set.
 *
 * Valid only for the modrdn operation.
*/
int
acl_access_allowed_modrdn(
	Slapi_PBlock	    *pb,
	Slapi_Entry	    *e,			/* The Slapi_Entry */
	char				*attr,		/* Attribute of	the entry */
	struct berval	    *val,		/* value of attr. NOT USED */
	int		    access		/* requested access rights */
	)
{
	int retCode ;
	char *newrdn, *oldrdn;
	int deleteoldrdn = 0;

	/*
	 * First check write permission on the entry--this is actually
	 * specially for modrdn.
	 */
	retCode = acl_access_allowed ( pb, e, NULL /* attr */, NULL /* val */,
									SLAPI_ACL_WRITE);

	if ( retCode != LDAP_SUCCESS ) {
		slapi_log_error( SLAPI_LOG_ACL, plugin_name,
							"modrdn:write permission to entry not allowed\n");
		return(retCode);
	}

	/* Now get the new rdn attribute name and value */

	slapi_pblock_get( pb, SLAPI_MODRDN_TARGET, &oldrdn );
    slapi_pblock_get( pb, SLAPI_MODRDN_NEWRDN, &newrdn );

	/* Check can add the new naming attribute */
	retCode = check_rdn_access( pb, e, newrdn, ACLPB_SLAPI_ACL_WRITE_ADD) ;
	if ( retCode != LDAP_SUCCESS ) {
		slapi_log_error( SLAPI_LOG_ACL, plugin_name,
			"modrdn:write permission to add new naming attribute not allowed\n");
		return(retCode);
	}

	/* Check can delete the new naming attribute--if required */
	slapi_pblock_get( pb, SLAPI_MODRDN_DELOLDRDN, &deleteoldrdn );
	if ( deleteoldrdn ) {
		retCode = check_rdn_access( pb, e, oldrdn, ACLPB_SLAPI_ACL_WRITE_DEL) ;
		if ( retCode != LDAP_SUCCESS ) {
			slapi_log_error( SLAPI_LOG_ACL, plugin_name,
				"modrdn:write permission to delete old naming attribute not allowed\n");
			return(retCode);
		}
	}

	return(retCode);

}
/*
 * Test if have access to make the first rdn of dn in entry e.
*/
 
static int check_rdn_access( Slapi_PBlock *pb, Slapi_Entry *e, char *dn,
						int access) {
	
	char **dns;
	char **rdns;
	int retCode = LDAP_INSUFFICIENT_ACCESS;
	int i;

	if ( (dns = ldap_explode_dn( dn, 0 )) != NULL ) {

		if ( (rdns = ldap_explode_rdn( dns[0], 0 )) != NULL ) {
	
			for ( i = 0; rdns[i] != NULL; i++ ) {
				char *type;			
				struct berval bv;
			
				if ( slapi_rdn2typeval( rdns[i], &type, &bv ) != 0 ) {
        			char ebuf[ BUFSIZ ];
        			slapi_log_error( SLAPI_LOG_ACL, plugin_name,
							"modrdn: rdn2typeval (%s) failed\n",
							 escape_string( rdns[i], ebuf ));
					retCode = LDAP_INSUFFICIENT_ACCESS;
					break;            	
				} else {
					if ( (retCode = acl_access_allowed ( pb, e, type /* attr */,
													&bv /* val */,
													access)) != LDAP_SUCCESS) {
						break;
					}
				}    			
			}
			slapi_ldap_value_free( rdns );
		}
		slapi_ldap_value_free( dns );
	}

	return(retCode);
}

/***************************************************************************
*
* acl_access_allowed
*	Determines if access to	the resource is	allowed	or not.
*
* Input:
*					
*
* Returns:
*
*	Returns	success/Denied/error condition
*
*	LDAP_SUCCESS			-- access allowed
*	LDAP_INSUFFICIENT_ACCESS	-- access denied
*
*	Errors returned:
*
*	Some of	the definition of the return values used copied	from
*	"ldap.h" for convienience.
*		LDAP_OPERATIONS_ERROR
*		LDAP_PROTOCOL_ERROR
*		LDAP_UNWILLING_TO_PERFORM
*
*
* Error	Handling:
*		Returned error code.
**************************************************************************/
int
acl_access_allowed(
	Slapi_PBlock	    *pb,
	Slapi_Entry	    *e,			/* The Slapi_Entry */
	char				*attr,		/* Attribute of	the entry */
	struct berval	    *val,		/* value of attr */
	int		    access		/* requested access rights */
	)
{
	char				*n_edn;		/*  Normalized DN of the entry */
	int					rv;
	int					err;
	int					ret_val;
	char				*right;
	struct	acl_pblock	*aclpb = NULL;
	AclAttrEval			*c_attrEval = NULL;
	int					got_reader_locked = 0;
	int					deallocate_attrEval = 0;
	char				ebuf [ BUFSIZ ];
	char				*clientDn;
	Slapi_DN			*e_sdn;
	Slapi_Operation		*op = NULL;
	aclResultReason_t	decision_reason;
	int					loglevel;

	loglevel = slapi_is_loglevel_set(SLAPI_LOG_ACL) ? SLAPI_LOG_ACL : SLAPI_LOG_ACLSUMMARY;
	slapi_pblock_get(pb, SLAPI_OPERATION, &op); /* for logging */

	TNF_PROBE_1_DEBUG(acl_access_allowed_start,"ACL","",
						tnf_int,access,access);

	decision_reason.deciding_aci = NULL;
	decision_reason.reason = ACL_REASON_NONE;

	/**
	 * First, if the acl private write/delete on attribute right
	 * is requested, turn this into	SLAPI_ACL_WRITE
	 * and record the original value.
     * Need to make sure that these rights do not clash	with the SLAPI
     * public rights.  This should be easy as the requested rights
     * in the aclpb are	stored in the bottom byte of aclpb_res_type,
     * so if we	keep the ACL private bits here too we make sure
     * not to clash.
     *
	*/

	if ( access & (ACLPB_SLAPI_ACL_WRITE_ADD | ACLPB_SLAPI_ACL_WRITE_DEL) )	{
		access |= SLAPI_ACL_WRITE;
	}

	n_edn =	slapi_entry_get_ndn ( e	);
	e_sdn =	slapi_entry_get_sdn ( e	);

	/* Check if this is a write operation and the database is readonly */
	/* No one, even	the rootdn should be allowed to	write to the database */
	/* jcm:	ReadOnly only applies to the public backends, the private ones */
	/* (the	DSEs) should still be writable for configuration. */
	if ( access & (	SLAPI_ACL_WRITE	| SLAPI_ACL_ADD	 | SLAPI_ACL_DELETE )) {
		int			be_readonly, privateBackend;
		Slapi_Backend		*be;

		slapi_pblock_get ( pb, SLAPI_BE_READONLY, &be_readonly );
		slapi_pblock_get ( pb, SLAPI_BACKEND, &be );
		privateBackend = slapi_be_private ( be );

		 if (  !privateBackend && (be_readonly ||  slapi_config_get_readonly () )){
			slapi_log_error	(loglevel, plugin_name,
				"conn=%" NSPRIu64 " op=%d (main): Deny %s on entry(%s)"
				": readonly backend\n",
				 op->o_connid, op->o_opid,
				acl_access2str(access),
				escape_string_with_punctuation(n_edn,ebuf));
			return LDAP_UNWILLING_TO_PERFORM;
		}
	}
	
	/* Check for things we need to skip */
	TNF_PROBE_0_DEBUG(acl_skipaccess_start,"ACL","");
	if (  acl_skip_access_check ( pb, e )) {
		slapi_log_error	(loglevel,	plugin_name,
				"conn=%" NSPRIu64 " op=%d (main): Allow %s on entry(%s)"
				": root user\n",
				op->o_connid, op->o_opid,
				acl_access2str(access),
				escape_string_with_punctuation(n_edn,ebuf));
		return(LDAP_SUCCESS);
	}
	TNF_PROBE_0_DEBUG(acl_skipaccess_end,"ACL","");


	/* Get the bindDN */
	slapi_pblock_get ( pb, SLAPI_REQUESTOR_DN, &clientDn );

	/* Initialize aclpb */
	aclplugin_preop_common( pb );

	/* get the right acl pblock  to	work with */
	if ( access & SLAPI_ACL_PROXY )
		aclpb =	acl_get_aclpb (	pb, ACLPB_PROXYDN_PBLOCK );
	else
		aclpb =	acl_get_aclpb (	pb, ACLPB_BINDDN_PBLOCK	);

	if ( !aclpb ) {
		slapi_log_error	( SLAPI_LOG_FATAL, plugin_name,	 "Missing aclpb	1 \n" );
		ret_val	= LDAP_OPERATIONS_ERROR;
		goto cleanup_and_ret;
	}

	/* check if aclpb is initialized or not	*/
	TNF_PROBE_0_DEBUG(acl_aclpbinit_start,"ACL","");
	acl_init_aclpb ( pb, aclpb, clientDn, 0	);
	TNF_PROBE_0_DEBUG(acl_aclpbinit_end,"ACL","");

	/* Here	we mean	if "I am trying	to add/delete "myself" to a group, etc." We
	 * basically just want to see if the value matches the DN of the user that
	 * we're checking access for */
	if (val &&  (access & SLAPI_ACL_WRITE) && (val->bv_len > 0)) {
		Slapi_Attr *sa = slapi_attr_new();
		char *oid = NULL;

		slapi_attr_init(sa, attr);
		slapi_attr_get_syntax_oid_copy(sa, &oid);
  
		/* We only want to perform this check if the attribute is
		 * defined using the DN or Name And Optional UID syntaxes. */
		if (oid && ((strcasecmp(oid, DN_SYNTAX_OID) == 0) ||
		            (strcasecmp(oid, NAMEANDOPTIONALUID_SYNTAX_OID) == 0))) { 
			/* should use slapi_sdn_compare() but that'a an extra malloc/free */
			char *dn_val_to_write = slapi_create_dn_string("%s", val->bv_val);
			if ( dn_val_to_write && aclpb->aclpb_authorization_sdn && 
					slapi_utf8casecmp((ACLUCHP)dn_val_to_write, (ACLUCHP)
					slapi_sdn_get_ndn(aclpb->aclpb_authorization_sdn)) == 0) { 
				access |= SLAPI_ACL_SELF;
			} 
	
			slapi_ch_free_string(&dn_val_to_write);
		}

		slapi_ch_free_string(&oid);
		slapi_attr_free(&sa);
	}

	/* Convert access to string of rights eg SLAPI_ACL_ADD->"add". */
	if ((right= acl_access2str(access)) == NULL) {
		/* ERROR: unknown rights */
		slapi_log_error(SLAPI_LOG_ACL, plugin_name,
				"acl_access_allowed unknown rights:%d\n", access);

		ret_val	= LDAP_OPERATIONS_ERROR;
		goto cleanup_and_ret;
	}

	
	/*
	 * Am I	a anonymous dude ? then we can use our anonymous profile
	 * We don't require the aclpb to have been initialized for anom stuff
	 *
	*/
	TNF_PROBE_0_DEBUG(acl_anon_test_start,"ACL","");
	if ( (access & (SLAPI_ACL_SEARCH | SLAPI_ACL_READ )) &&
										(clientDn && *clientDn == '\0')) {
		aclanom_get_suffix_info(e, aclpb);
		ret_val	= aclanom_match_profile	( pb, aclpb, e, attr, access );
		if (ret_val != -1 ) {
			if (ret_val == LDAP_SUCCESS ) {
				decision_reason.reason = ACL_REASON_ANON_ALLOWED;
			} else if (ret_val == LDAP_INSUFFICIENT_ACCESS) {
				decision_reason.reason = ACL_REASON_ANON_DENIED;
			}
			goto cleanup_and_ret;
		}
	}
	TNF_PROBE_0_DEBUG(acl_anon_test_end,"ACL","");
	
    /* copy the	value into the aclpb for later checking	by the value acl code */

    aclpb->aclpb_curr_attrVal =	val;

	if (!(aclpb->aclpb_state & ACLPB_SEARCH_BASED_ON_LIST) &&
		(access	& SLAPI_ACL_SEARCH)) {
		/* We are evaluating SEARCH right for the entry. After that
		** we will eval	the READ right.	We need	to refresh  the
		** list	of acls	selected for evaluation	for the	entry.
		** Initialize the array	so that	we indicate nothing has	been
		** selected.
		*/
		aclpb->aclpb_handles_index[0] =	-1;
		/* access is not allowed on entry for search -- it's for 
		** read only.
		*/
		aclpb->aclpb_state &= ~ACLPB_ACCESS_ALLOWED_ON_ENTRY;
	}

	/* set that this is a new entry */
	aclpb->aclpb_res_type |= ACLPB_NEW_ENTRY;
	aclpb->aclpb_access = 0;
	aclpb->aclpb_access |= access; 

	/*
	 * stub the Slapi_Entry info  first time and only it has changed
	 * or if the pblock is a psearch pblock--in this case the lifetime
	 * of entries associated with psearches is such that we cannot cache
	 * pointers to them--we must always start afresh (see psearch.c).
	*/ 
	slapi_pblock_get( pb, SLAPI_OPERATION, &op);
	if ( operation_is_flag_set(op, OP_FLAG_PS) ||
		 (aclpb->aclpb_curr_entry_sdn == NULL) ||
		 (slapi_sdn_compare ( aclpb->aclpb_curr_entry_sdn, e_sdn) != 0) ||
		 (aclpb->aclpb_curr_entry != e) /* cannot trust the cached entry */ ) {
		TNF_PROBE_0_DEBUG(acl_entry_first_touch_start,"ACL","");

		slapi_log_error(loglevel, plugin_name,
			"#### conn=%" NSPRIu64 " op=%d binddn=\"%s\"\n",
			op->o_connid, op->o_opid, clientDn);
		aclpb->aclpb_stat_total_entries++;

		if (!(access & SLAPI_ACL_PROXY) &&
							!( aclpb->aclpb_state & ACLPB_DONOT_EVALUATE_PROXY )) {
				Acl_PBlock *proxy_pb;

			proxy_pb = acl_get_aclpb( pb, ACLPB_PROXYDN_PBLOCK );
			if (proxy_pb) {
				TNF_PROBE_0_DEBUG(acl_access_allowed_proxy_start,"ACL","");
				ret_val = acl_access_allowed( pb, e, attr, val, SLAPI_ACL_PROXY );
				TNF_PROBE_0_DEBUG(acl_access_allowed_proxy_end,"ACL","");

				if (ret_val != LDAP_SUCCESS) goto cleanup_and_ret;
			}
		}
		if ( access & SLAPI_ACL_SEARCH)  {
			aclpb->aclpb_num_entries++;

			if ( aclpb->aclpb_num_entries == 1) {
				aclpb->aclpb_state |= ACLPB_COPY_EVALCONTEXT;
			} else if ( aclpb->aclpb_state & ACLPB_COPY_EVALCONTEXT ) {	
				/* We need to copy the evalContext */
				acl_copyEval_context ( aclpb, &aclpb->aclpb_curr_entryEval_context,
							&aclpb->aclpb_prev_entryEval_context, 0 );
				aclpb->aclpb_state &= ~ACLPB_COPY_EVALCONTEXT;
			}
			acl_clean_aclEval_context ( &aclpb->aclpb_curr_entryEval_context, 1 /*scrub */);
		}

		/* reset the cached result based on the scope */
		acl__reset_cached_result (aclpb );

		/* Find all the candidate aci's that apply by scanning up the DIT tree from edn. */
		
		TNF_PROBE_0_DEBUG(acl_aciscan_start,"ACL","");
		slapi_sdn_done ( aclpb->aclpb_curr_entry_sdn );
		slapi_sdn_set_dn_byval ( aclpb->aclpb_curr_entry_sdn, n_edn );
		acllist_aciscan_update_scan ( aclpb, n_edn ); 
		TNF_PROBE_0_DEBUG(acl_aciscan_end,"ACL","");

		/* Keep the ptr to the current entry */
		aclpb->aclpb_curr_entry =  (Slapi_Entry *) e;

		/* Get the attr info */
		deallocate_attrEval = acl__get_attrEval ( aclpb, attr );

		aclutil_print_resource ( aclpb, right,  attr, clientDn );    
        
        /*
         * Used to be PListInitProp(aclpb->aclpb_proplist, 0,
         *	 						DS_ATTR_ENTRY, e, 0);
		 *
         * The difference is that PListInitProp() allocates a new property
         * every time it's called, overwriting the old name in the PList hash
         * table, but not freeing the original property.
         * Now, we just create the property at aclpb_malloc() time and
         * Assign a new value each time.
        */       
        
	 	rv = PListAssignValue(aclpb->aclpb_proplist, 
				   DS_ATTR_ENTRY, e, 0);
                                                 
		if (rv < 0) {
			slapi_log_error(SLAPI_LOG_ACL, plugin_name, 
	    	  		"Unable to set the Slapi_Entry in the Plist\n");
			ret_val = LDAP_OPERATIONS_ERROR;
			goto cleanup_and_ret;
		}
		
		TNF_PROBE_0_DEBUG(acl_entry_first_touch_end,"ACL","");

	} else {
		/* we are processing the same entry but for a different
		** attribute. If access is already allowed on that, entry, then 
		** it's not a new entry anymore. It's the same old one.
		*/
	
		TNF_PROBE_0_DEBUG(acl_entry_subs_touch_start,"ACL","");
	
		aclpb->aclpb_res_type &= ~ACLPB_NEW_ENTRY;

		/* Get the attr info */
		deallocate_attrEval = acl__get_attrEval ( aclpb, attr );

		TNF_PROBE_0_DEBUG(acl_entry_subs_touch_end,"ACL","");

	}

	/* get a lock for the reader */
	acllist_acicache_READ_LOCK();
	got_reader_locked = 1;

	/*
	** Check if we can use any cached information to determine 
	** access to this resource
	*/
	if (  (access & SLAPI_ACL_SEARCH) &&
	 (ret_val =  acl__match_handlesFromCache ( aclpb , attr, access))  != -1) {
		/* means got a result: allowed or not*/

		if (ret_val == LDAP_SUCCESS ) {
				decision_reason.reason = ACL_REASON_EVALCONTEXT_CACHED_ALLOW;
		} else if (ret_val == LDAP_INSUFFICIENT_ACCESS) {
				decision_reason.reason =
					ACL_REASON_EVALCONTEXT_CACHED_NOT_ALLOWED;
		}
		goto cleanup_and_ret;
	}
	
	/*
	** Now we have all the information about the resource. Now we need to 
	** figure out if there are any ACLs which can be applied.
	** If no ACLs are there, then it's a DENY as default.
	*/
	if (!(acl__scan_for_acis(aclpb, &err))) {

		/* We might have accessed the ACL first time which could
		** have caused syntax error.
		*/
		if ( err == ACL_ONEACL_TEXT_ERR) 
			ret_val = LDAP_INVALID_SYNTAX;
		else {
			ret_val = LDAP_INSUFFICIENT_ACCESS;
			decision_reason.reason = ACL_REASON_NO_MATCHED_RESOURCE_ALLOWS;			
		}
		goto cleanup_and_ret;
	}

	slapi_log_error( SLAPI_LOG_ACL, plugin_name,
		   "Processed attr:%s for entry:%s\n", attr ? attr : "NULL",
		   ACL_ESCAPE_STRING_WITH_PUNCTUATION ( n_edn, ebuf));

	/*
	** Now evaluate the rights. 
	** This is what we have been waiting for.
	** The return value should be ACL_RES_DENY or ACL_RES_ALLOW.
	*/
	rv = acl__TestRights(aclpb, access, &right, ds_map_generic,
												&decision_reason);
	if ( rv != ACL_RES_ALLOW && (0 == strcasecmp ( right, "selfwrite") ) ) {
		/* If I am adding myself to a group, we don't need selfwrite always,
		** write priv is good enough. Since libaccess doesn't provide me a nice
		** way to evaluate OR rights, I have to try again with wite priv.
		** bug: 339051
		*/
		right = access_str_write;
		rv = acl__TestRights(aclpb, access, &right, ds_map_generic,
													&decision_reason);
    }

	if (rv == ACL_RES_ALLOW) {
		ret_val = LDAP_SUCCESS;
	} else {
		ret_val = LDAP_INSUFFICIENT_ACCESS;
	} 

cleanup_and_ret:

	TNF_PROBE_0_DEBUG(acl_cleanup_start,"ACL","");

	/* I am ready to get out. */
	if ( got_reader_locked ) acllist_acicache_READ_UNLOCK();

	/* Store the status of the evaluation for this attr */
	if (  aclpb && (c_attrEval = aclpb->aclpb_curr_attrEval )) {
		if ( deallocate_attrEval ) {
			/* In this case we are not caching the result as 
			** we have too many attrs. we have malloced space. 
			** Get rid of it.
			*/
			slapi_ch_free ( (void **) &c_attrEval->attrEval_name );
			slapi_ch_free ( (void **) &c_attrEval );
		} else if (ret_val == LDAP_SUCCESS ) {
			if ( access & SLAPI_ACL_SEARCH )
				c_attrEval->attrEval_s_status |= ACL_ATTREVAL_SUCCESS;
			else if ( access & SLAPI_ACL_READ )
				c_attrEval->attrEval_r_status |= ACL_ATTREVAL_SUCCESS;
			else
				c_attrEval->attrEval_r_status |= ACL_ATTREVAL_INVALID;
		} else {
			if ( access & SLAPI_ACL_SEARCH )
				c_attrEval->attrEval_s_status |= ACL_ATTREVAL_FAIL;
			else if ( access & SLAPI_ACL_READ )
				c_attrEval->attrEval_r_status |= ACL_ATTREVAL_FAIL;
			else
				c_attrEval->attrEval_r_status |= ACL_ATTREVAL_INVALID;
		}
	}

	if ( aclpb ) aclpb->aclpb_curr_attrEval = NULL;

	print_access_control_summary( "main", ret_val, clientDn, aclpb, right,
									(attr ? attr : "NULL"),
							escape_string_with_punctuation (n_edn, ebuf),
							&decision_reason);
	TNF_PROBE_0_DEBUG(acl_cleanup_end,"ACL","");
	
	TNF_PROBE_0_DEBUG(acl_access_allowed_end,"ACL","");

	return(ret_val);
	
}

static void print_access_control_summary( char *source, int ret_val, char *clientDn,
									struct	acl_pblock	*aclpb,
									char *right,
									char *attr,
									const char *edn,
									aclResultReason_t *acl_reason)
{
	struct codebook {
		int   code;
		char *text;
	};

	static struct codebook reasonbook[] = {
		{ACL_REASON_NO_ALLOWS,					"no allow acis"},
		{ACL_REASON_RESULT_CACHED_DENY,			"cached deny"},
		{ACL_REASON_RESULT_CACHED_ALLOW,		"cached allow"},
		{ACL_REASON_EVALUATED_ALLOW,			"allowed"},
		{ACL_REASON_EVALUATED_DENY,				"denied"},
		{ACL_REASON_NO_MATCHED_RESOURCE_ALLOWS,	"no aci matched the resource"},
		{ACL_REASON_NO_MATCHED_SUBJECT_ALLOWS,	"no aci matched the subject"},
		{ACL_REASON_ANON_ALLOWED,				"allow anyone aci matched anon user"},
		{ACL_REASON_ANON_DENIED,				"no matching anyone aci for anon user"},
		{ACL_REASON_EVALCONTEXT_CACHED_ALLOW,	"cached context/parent allow"},
		{ACL_REASON_EVALCONTEXT_CACHED_NOT_ALLOWED,	"cached context/parent deny"},
		{ACL_REASON_EVALCONTEXT_CACHED_ATTR_STAR_ALLOW,	"cached context/parent allow any attr"},
		{ACL_REASON_NONE,						"error occurred"},
	};

	char *anon = "anonymous";
	char *null_user = "NULL";	/* bizare case */ 
	char *real_user = NULL;
	char *proxy_user = NULL;	
	char *access_allowed_string = "Allow";
	char *access_not_allowed_string = "Deny";
	char *access_error_string = "access_error";
	char *access_status = NULL;
	char *access_reason_none = "no reason available";
	char *access_reason = access_reason_none;	
	char acl_info[ BUFSIZ ];
	Slapi_Operation *op = NULL;
	int loglevel; 
	int	i;

	loglevel = slapi_is_loglevel_set(SLAPI_LOG_ACL) ? SLAPI_LOG_ACL : SLAPI_LOG_ACLSUMMARY;

	if ( !slapi_is_loglevel_set(loglevel) ) {
		return;
	}

	slapi_pblock_get(aclpb->aclpb_pblock, SLAPI_OPERATION, &op); /* for logging */

	if (ret_val == LDAP_INSUFFICIENT_ACCESS) {
		access_status = access_not_allowed_string;
	} else if ( ret_val == LDAP_SUCCESS) {
		access_status = access_allowed_string;
	} else { /* some kind of error */
		access_status = access_error_string;
	}

	/* decode the reason */
	for (i = 0; i < sizeof(reasonbook) / sizeof(struct codebook); i++) {
		if ( acl_reason->reason == reasonbook[i].code ) {
			access_reason = reasonbook[i].text;
			break;
		}
	}

	/* get the acl */
	acl_info[0] = '\0';
	if (acl_reason->deciding_aci) {
		if (acl_reason->reason == ACL_REASON_RESULT_CACHED_DENY ||
			acl_reason->reason == ACL_REASON_RESULT_CACHED_ALLOW) {
			/* acl is in cache. Its detail must have been printed before.
			 * So no need to print out acl detail this time.
			 */
			PR_snprintf( &acl_info[0], BUFSIZ, "%s by aci(%d)",
				access_reason,
				acl_reason->deciding_aci->aci_index);
		} 
		else {
			PR_snprintf( &acl_info[0], BUFSIZ, "%s by aci(%d): aciname=%s, acidn=\"%s\"",
				access_reason,
				acl_reason->deciding_aci->aci_index,
				acl_reason->deciding_aci->aclName,
				slapi_sdn_get_ndn (acl_reason->deciding_aci->aci_sdn) );
		}
	} 

	/* Say who was denied access */

	if (clientDn) {
		if (clientDn[0] == '\0') {
			/* anon */
			real_user = anon;
		} else {
			real_user = clientDn;
		}
	} else {
		real_user = null_user;
	}
			
	/* Is there a proxy */

	if ( aclpb != NULL && aclpb->aclpb_proxy != NULL) {

		if ( aclpb->aclpb_authorization_sdn != NULL ) {

				proxy_user = 
					(char *)(slapi_sdn_get_ndn(aclpb->aclpb_authorization_sdn)?
					slapi_sdn_get_ndn(aclpb->aclpb_authorization_sdn):
					null_user);

				slapi_log_error(loglevel, plugin_name, 
		"conn=%" NSPRIu64 " op=%d (%s): %s %s on entry(%s).attr(%s) to proxy (%s)"
		": %s\n",
				op->o_connid, op->o_opid,
				source,
				access_status,
				right, 
				edn,
				attr ? attr: "NULL",
				proxy_user,
				acl_info[0] ? acl_info : access_reason);									
		} else {
					proxy_user = null_user;
					slapi_log_error(loglevel, plugin_name, 
		"conn=%" NSPRIu64 " op=%d (%s): %s %s on entry(%s).attr(%s) to proxy (%s)"
		": %s\n",
				op->o_connid, op->o_opid,
				source,
				access_status,
				right, 
				edn,
				attr ? attr: "NULL",
				proxy_user,
				acl_info[0] ? acl_info : access_reason);								
		}
	} else{
		slapi_log_error(loglevel, plugin_name, 
			"conn=%" NSPRIu64 " op=%d (%s): %s %s on entry(%s).attr(%s) to %s"
			": %s\n",
				op->o_connid, op->o_opid,
				source,
				access_status,
				right, 
				edn,
				attr ? attr: "NULL",
				real_user,
				acl_info[0] ? acl_info : access_reason);									
	}
	

}
/***************************************************************************
*
* acl_read_access_allowed_on_entry 
*	check read access control on the given entry.
*
*  Only used during seearch to test for read access on the entry.
*  (Could be generalized).
* 
*	attrs is the list of requested attributes passed with the search.
*	If the entry has no attributes (weird case) then the routine survives.
*
* Input:
*
*
* Returns:
* 	LDAP_SUCCESS			- access allowed
*	LDAP_INSUFFICIENT_ACCESS	- access denied
*
* Error Handling:
*	None.
*
**************************************************************************/
int
acl_read_access_allowed_on_entry (
	Slapi_PBlock	   *pb,
	Slapi_Entry        *e,			/* The Slapi_Entry */	
	char		   **attrs,
	int                access		/* access rights */
	)
{

	struct	acl_pblock	*aclpb; 
	Slapi_Attr			*currAttr;
	Slapi_Attr			*nextAttr;
	int					len;
	int					attr_index = -1;
	char				*attr_type = NULL;
	int					rv, isRoot;
	char				*clientDn;
	unsigned long		flags;
	aclResultReason_t	decision_reason;
	int loglevel;

	loglevel = slapi_is_loglevel_set(SLAPI_LOG_ACL) ? SLAPI_LOG_ACL : SLAPI_LOG_ACLSUMMARY;

	TNF_PROBE_0_DEBUG(acl_read_access_allowed_on_entry_start ,"ACL","");

	decision_reason.deciding_aci = NULL;
	decision_reason.reason = ACL_REASON_NONE;

	slapi_pblock_get ( pb, SLAPI_REQUESTOR_ISROOT, &isRoot );

	/* 
	** If it's the root, or acl is off or the entry is a rootdse, 
	** Then you have the privilege to read it.
	*/
	if ( acl_skip_access_check ( pb, e ) ) {
		char   *n_edn =  slapi_entry_get_ndn ( e );
		char	ebuf [ BUFSIZ ];
		slapi_log_error (SLAPI_LOG_ACL, plugin_name, 
			  "Root access (%s) allowed on entry(%s)\n",
			   acl_access2str(access), 
			   ACL_ESCAPE_STRING_WITH_PUNCTUATION (n_edn, ebuf));
		TNF_PROBE_1_DEBUG(acl_read_access_allowed_on_entry_end ,"ACL","",
							tnf_string,skip_access,"");
		return LDAP_SUCCESS;
	}		

	aclpb = acl_get_aclpb ( pb, ACLPB_BINDDN_PBLOCK );
	if ( !aclpb ) {
		slapi_log_error ( SLAPI_LOG_FATAL, plugin_name,  "Missing aclpb 2 \n" );
		TNF_PROBE_1_DEBUG(acl_read_access_allowed_on_entry_end ,"ACL","",
							tnf_string,end,"aclpb error");
		return LDAP_OPERATIONS_ERROR;
	}

	/*
	 * Am I	a anonymous dude ? then	we can use our anonympous profile
	 * We don't require the aclpb to have been initialized for anom stuff
	 *
	*/
	slapi_pblock_get ( pb, SLAPI_REQUESTOR_DN, &clientDn );
	if ( clientDn  && *clientDn == '\0' ) {
		int ret_val;
		ret_val =  aclanom_match_profile ( pb, aclpb, e, NULL, SLAPI_ACL_READ );
		TNF_PROBE_1_DEBUG(acl_read_access_allowed_on_entry_end ,"ACL","",
					tnf_string,end,"anon");

		if (ret_val != -1 ) return ret_val;
	}

	aclpb->aclpb_state &= ~ACLPB_RESET_MASK;
	if (aclpb->aclpb_state & ACLPB_MATCHES_ALL_ACLS ) {
		int	ret_val;
		ret_val = acl__attr_cached_result (aclpb, NULL, SLAPI_ACL_READ);
		if (ret_val != -1 ) {
			/* print summary if loglevel set */			
			if ( slapi_is_loglevel_set(loglevel) ) {
				char *n_edn;
				n_edn =  slapi_entry_get_ndn ( e );
				if ( ret_val == LDAP_SUCCESS) {
					decision_reason.reason =
							ACL_REASON_EVALCONTEXT_CACHED_ALLOW;
				} else {
					decision_reason.reason =
									ACL_REASON_EVALCONTEXT_CACHED_NOT_ALLOWED;
				}
				/*
				 * pass NULL as the attr as this routine is concerned with
				 * access at the entry level.
				*/
				print_access_control_summary( "on entry",
											ret_val, clientDn, aclpb,
											acl_access2str(SLAPI_ACL_READ),
											NULL, n_edn,
											&decision_reason);
			}
			TNF_PROBE_1_DEBUG(acl_read_access_allowed_on_entry_end ,"ACL","",
							tnf_string,eval_context_cached,"");

			return ret_val;
		}
	}

	/*
	 * Currently do not use this code--it results in confusing
	 * behaviour..see 529905
	*/	
#ifdef DETERMINE_ACCESS_BASED_ON_REQUESTED_ATTRIBUTES

	/* Do we have access to the entry by virtue of 
	** having access to an attr. Before that, let's find out which attrs
	** the user want. If the user has specified certain attributes, then
	** we check aginst that set of attributes.
	*/
	if (!((aclpb->aclpb_state & ACLPB_USER_WANTS_ALL_ATTRS) || 
		(aclpb->aclpb_state & ACLPB_USER_SPECIFIED_ATTARS))) {
		int i;
		if (attrs == NULL) {
			aclpb->aclpb_state |= ACLPB_USER_WANTS_ALL_ATTRS;
		} else {
			for ( i = 0; attrs != NULL && attrs[i] != NULL; i++ ) {
				if ( strcmp( LDAP_ALL_USER_ATTRS, attrs[i] ) == 0 ) {
					aclpb->aclpb_state |= ACLPB_USER_WANTS_ALL_ATTRS;
					break;
				}
			}
		}

		if (!(aclpb->aclpb_state & ACLPB_USER_WANTS_ALL_ATTRS)) {
	  		for (i = 0; attrs != NULL && attrs[i] != NULL; i++ ) {
				if ( !slapi_entry_attr_find ( e,  attrs[i], &currAttr ) ) {
					aclpb->aclpb_state |= ACLPB_USER_SPECIFIED_ATTARS;
					break;
				}
	   		}
		}
	} /* end of all user test*/


	/* 
	** If user has specified a list of attrs, might as well use it
	** to determine access control.
	*/
	currAttr = NULL;
	attr_index = -1;
	if ( aclpb->aclpb_state & ACLPB_USER_SPECIFIED_ATTARS) {
		attr_index = 0;
		attr_type = attrs[attr_index++];
	} else { 
		/* Skip the operational attributes  -- if there are any in the front */
		slapi_entry_first_attr ( e, &currAttr );
		if (currAttr != NULL) {
			slapi_attr_get_flags ( currAttr,  &flags );
			while  ( flags & SLAPI_ATTR_FLAG_OPATTR ) {
				flags = 0;
				rv = slapi_entry_next_attr ( e, currAttr, &nextAttr );
				if  (  !rv )  slapi_attr_get_flags ( nextAttr,  &flags );
				currAttr = nextAttr;
			}		

			/* Get the attr type */
			if ( currAttr ) slapi_attr_get_type ( currAttr , &attr_type );
		}
	}

#endif /*DETERMINE_ACCESS_BASED_ON_REQUESTED_ATTRIBUTES*/

#ifndef DETERMINE_ACCESS_BASED_ON_REQUESTED_ATTRIBUTES

	/*
	 * Here look at each attribute in the entry and see if
	 * we have read access to it--if we do
	 * and we are not denied access to the entry then this
	 * is taken as implying access to the entry.
	*/
	slapi_entry_first_attr ( e, &currAttr );
	if (currAttr != NULL) {
		slapi_attr_get_type ( currAttr , &attr_type );
	}
#endif
	aclpb->aclpb_state |= ACLPB_EVALUATING_FIRST_ATTR;

	while (attr_type) {
		if (acl_access_allowed (pb, e,attr_type, NULL, 
				    SLAPI_ACL_READ) == LDAP_SUCCESS) {
			/*
			** We found a rule which requires us to test access
			** to the entry.
			*/
			if ( aclpb->aclpb_state & ACLPB_FOUND_A_ENTRY_TEST_RULE){
				/* Do I have access on the entry  itself */
				if (acl_access_allowed (pb, e, NULL, 
						NULL, access) != LDAP_SUCCESS) {
					/* How was I denied ? 
					** I could be denied on a DENY rule or because 
					** there is no allow rule. If it's a DENY from
					** a DENY rule, then we don't have access to 
					** the entry ( nice trick to get in )
					*/
					if ( aclpb->aclpb_state & 
							ACLPB_EXECUTING_DENY_HANDLES)
						return LDAP_INSUFFICIENT_ACCESS;
					
					/* The other case is I don't have an
					** explicit allow rule -- which is fine.
					** Since, I am already here, it means that I have
					** an implicit allow to the entry.	
					*/
				}
			}
			aclpb->aclpb_state &= ~ACLPB_EVALUATING_FIRST_ATTR;

			/*
			** If we are not sending all the attrs, then we must
			** make sure that we have right on a attr that we are 
			** sending
			*/
			len = strlen(attr_type);
			if ( len > ACLPB_MAX_ATTR_LEN) {
				slapi_ch_free ( (void **) &aclpb->aclpb_Evalattr);
				aclpb->aclpb_Evalattr = slapi_ch_malloc(len);
			}
			PL_strncpyz (aclpb->aclpb_Evalattr, attr_type, len);
			if ( attr_index >= 0 ) {
				/*
				 * access was granted to one of the user specified attributes
				 * which was found in the entry and that attribute is
				 * now in aclpb_Evalattr
				*/
				aclpb->aclpb_state |= 
					ACLPB_ACCESS_ALLOWED_USERATTR;
			} else {
				/*
				 * Access was granted to _an_ attribute in the entry and that
				 * attribute is now in aclpb_Evalattr
				*/
				aclpb->aclpb_state |= 
					ACLPB_ACCESS_ALLOWED_ON_A_ATTR;
			}
			TNF_PROBE_1_DEBUG(acl_read_access_allowed_on_entry_end , "ACL","",
						tnf_string,called_access_allowed,"");

			return LDAP_SUCCESS;
		} else {
			/* try the next one */
			attr_type = NULL;
			if (attr_index >= 0) { 
				attr_type = attrs[attr_index++];
			} else {
				rv = slapi_entry_next_attr ( e, currAttr, &nextAttr );
				if ( rv != 0 ) break;
				currAttr = nextAttr;
				slapi_attr_get_flags ( currAttr,  &flags );
				while  ( flags & SLAPI_ATTR_FLAG_OPATTR ) {
					flags = 0;
					rv = slapi_entry_next_attr ( e, currAttr, &nextAttr );
					if  (  !rv )  slapi_attr_get_flags ( nextAttr,  &flags );
					currAttr = nextAttr;
				}
				/* Get the attr type */
				if ( currAttr ) slapi_attr_get_type ( currAttr , &attr_type );
			}
		}
	}

	/* 
	** That means. we have searched thru all the attrs and found
	** access is denied on all attrs.
	**
	** If there were no attributes in the entry at all (can have
	** such entries thrown up by the b/e, then we do
	** not have such an implied access.
	*/
	aclpb->aclpb_state |= ACLPB_ACCESS_DENIED_ON_ALL_ATTRS;
	aclpb->aclpb_state &= ~ACLPB_EVALUATING_FIRST_ATTR;
	TNF_PROBE_0_DEBUG(acl_read_access_allowed_on_entry_end ,"ACL","");

	return LDAP_INSUFFICIENT_ACCESS;
}

/***************************************************************************
*
* acl_read_access_allowed_on_attr 
*	check access control on the given attr.
*
* Only used during search to test for read access to an attr.
* (Could be generalized)
*
* Input:
*
*
* Returns:
* 	LDAP_SUCCESS			- access allowed
*	LDAP_INSUFFICIENT_ACCESS	- access denied
*
* Error Handling:
*	None.
*
**************************************************************************/
int
acl_read_access_allowed_on_attr (
	Slapi_PBlock		*pb,
	Slapi_Entry			*e,		/* The Slapi_Entry */
	char				*attr,		/* Attribute of the entry */
	struct berval 		*val,		/* value of attr. NOT USED */
	int					access		/* access rights */
	)
{

	struct	acl_pblock	*aclpb =  NULL;
	char				ebuf [ BUFSIZ ];
	char				*clientDn = NULL;
	char				*n_edn;
	aclResultReason_t	decision_reason;
	int 				ret_val = -1;
	int					loglevel;

	loglevel = slapi_is_loglevel_set(SLAPI_LOG_ACL) ? SLAPI_LOG_ACL : SLAPI_LOG_ACLSUMMARY;

	TNF_PROBE_0_DEBUG(acl_read_access_allowed_on_attr_start ,"ACL","");

	decision_reason.deciding_aci = NULL;
	decision_reason.reason = ACL_REASON_NONE;

	/* I am here, because I have access to the entry */

	n_edn =  slapi_entry_get_ndn ( e );

	/* If it's the root or acl is off or rootdse, he has all the priv */
	if ( acl_skip_access_check ( pb, e ) ) {
		char	ebuf [ BUFSIZ ];
		slapi_log_error (SLAPI_LOG_ACL, plugin_name, 
			  "Root access (%s) allowed on entry(%s)\n",
			   acl_access2str(access), 
			   ACL_ESCAPE_STRING_WITH_PUNCTUATION (n_edn, ebuf));
		TNF_PROBE_1_DEBUG(acl_read_access_allowed_on_attr_end ,"ACL","",
							tnf_string,skip_aclcheck,"");

		return LDAP_SUCCESS;
	}	

	aclpb = acl_get_aclpb ( pb, ACLPB_BINDDN_PBLOCK );
	if ( !aclpb ) {
		slapi_log_error ( SLAPI_LOG_FATAL, plugin_name,  "Missing aclpb 3 \n" );
		TNF_PROBE_1_DEBUG(acl_read_access_allowed_on_attr_end ,"ACL","",
							tnf_string,aclpb_error,"");

		return LDAP_OPERATIONS_ERROR;
	}
	
	/*
	 * Am I	a anonymous dude ? then	we can use our anonympous profile
	 * We don't require the aclpb to have been initialized for anom stuff
	 *
	*/
	slapi_pblock_get (pb, SLAPI_REQUESTOR_DN ,&clientDn );
	if ( clientDn && *clientDn == '\0' ) {		
		ret_val =  aclanom_match_profile ( pb, aclpb, e, attr, 
						SLAPI_ACL_READ );
		TNF_PROBE_1_DEBUG(acl_read_access_allowed_on_attr_end ,"ACL","",
							tnf_string,anon_decision,"");
		if (ret_val != -1 ) return ret_val;
	}

	/* Then I must have a access to the entry. */
	aclpb->aclpb_state |= ACLPB_ACCESS_ALLOWED_ON_ENTRY;

	if ( aclpb->aclpb_state & ACLPB_MATCHES_ALL_ACLS ) {		

		ret_val = acl__attr_cached_result (aclpb, attr, SLAPI_ACL_READ);
		if (ret_val != -1 ) {
			slapi_log_error(SLAPI_LOG_ACL, plugin_name, 
				 "MATCHED HANDLE:dn:%s attr: %s val:%d\n", 
				ACL_ESCAPE_STRING_WITH_PUNCTUATION (n_edn, ebuf), attr,
																ret_val );
			if ( ret_val == LDAP_SUCCESS) {
				decision_reason.reason =
						ACL_REASON_EVALCONTEXT_CACHED_ALLOW;
			} else {
				decision_reason.reason =
							ACL_REASON_EVALCONTEXT_CACHED_NOT_ALLOWED;
			}		
			goto acl_access_allowed_on_attr_Exit;
		 } else  {
			aclpb->aclpb_state |= ACLPB_COPY_EVALCONTEXT;
		}
	}

	if (aclpb->aclpb_state & ACLPB_ACCESS_DENIED_ON_ALL_ATTRS) {
		/* access is denied on all the attributes */
		TNF_PROBE_1_DEBUG(acl_read_access_allowed_on_attr_end ,"ACL","",
							tnf_string,deny_all_attrs,"");

		return LDAP_INSUFFICIENT_ACCESS;
	} 

	/* do I have access to all the entries by virtue of having aci 
	** rules with targetattr ="*". If yes, then allow access to
	** rest of the attributes.
	*/
	if (aclpb->aclpb_state & ACLPB_ATTR_STAR_MATCHED) {
		slapi_log_error(SLAPI_LOG_ACL, plugin_name, 
		 	  "STAR Access allowed on attr:%s; entry:%s \n",
		 	   attr, ACL_ESCAPE_STRING_WITH_PUNCTUATION (n_edn, ebuf));
		decision_reason.reason =
						ACL_REASON_EVALCONTEXT_CACHED_ATTR_STAR_ALLOW;
		ret_val = LDAP_SUCCESS;
		goto acl_access_allowed_on_attr_Exit;

	}

	if (aclpb->aclpb_state & ACLPB_ACCESS_ALLOWED_ON_A_ATTR) {

		/* access is allowed on that attr.
		** for example:  Slapi_Entry: cn, sn. phone, uid, passwd, address
		** We found that access is allowed on phone. That means the
		**  -- access is denied on cn, sn
		**  -- access  is allowed on phone
		**  -- Don't know about the rest. Need to evaluate.
		*/

		if ( slapi_attr_type_cmp (attr, aclpb->aclpb_Evalattr, 1) == 0) {
			/* from now on we need to evaluate access on
			** rest of the attrs.
			*/
			aclpb->aclpb_state &= ~ACLPB_ACCESS_ALLOWED_ON_A_ATTR;
			TNF_PROBE_1_DEBUG(acl_read_access_allowed_on_attr_end ,"ACL","",
								tnf_string,aclp_Evalattr1,"");

			return LDAP_SUCCESS;
		} else {
			/*
			 * Here, the attr that implied access to the entry (aclpb_Evalattr),
			 *  is not
			 * the one we currently want evaluated--so
			 * we need to evaluate access to attr--so fall through.
			*/	 					
		}

	}  else if (aclpb->aclpb_state & ACLPB_ACCESS_ALLOWED_USERATTR) {
		/* Only skip evaluation on the user attr on which we have
		** evaluated before.
		*/
		if ( slapi_attr_type_cmp (attr, aclpb->aclpb_Evalattr, 1) == 0) {
			aclpb->aclpb_state &= ~ACLPB_ACCESS_ALLOWED_USERATTR;
			TNF_PROBE_1_DEBUG(acl_read_access_allowed_on_attr_end ,"ACL","",
								tnf_string,aclp_Evalattr2,"");
			return LDAP_SUCCESS;
		}
	}

	/* we need to evaluate the access on this attr */
	return ( acl_access_allowed(pb, e, attr, val, access) );

	/* This exit point prints a summary and returns ret_val */
acl_access_allowed_on_attr_Exit:

	/* print summary if loglevel set */			
	if ( slapi_is_loglevel_set(loglevel) ) {
		
		print_access_control_summary( "on attr",
										ret_val, clientDn, aclpb,
										acl_access2str(SLAPI_ACL_READ),
										attr, n_edn,											&decision_reason);
	}
	TNF_PROBE_0_DEBUG(acl_read_access_allowed_on_attr_end ,"ACL","");

	return(ret_val);
}
/***************************************************************************
*
* acl_check_mods 
*	 check access control on the given entry to see if
* 	it allows the given modifications by the user associated with op.
*
*
* Input:
*
*
* Returns:
* 	LDAP_SUCCESS			- mods allowed ok
*	<err>				- same return value as acl_access_allowed()
*
* Error Handling:
*	None.
*
**************************************************************************/
int
acl_check_mods(
    Slapi_PBlock	*pb,
    Slapi_Entry	*e,
    LDAPMod	**mods,
    char	**errbuf
)
{
	int 			i;
	int				rv, accessCheckDisabled;
	int				lastmod = 0;
	Slapi_Attr 		*attr = NULL;
	char			*n_edn;
	Slapi_Backend	*be = NULL;
	Slapi_DN		*e_sdn;
	Acl_PBlock		*aclpb = acl_get_aclpb ( pb, ACLPB_PROXYDN_PBLOCK );
	LDAPMod			*mod;
	Slapi_Mods		smods;

	rv = slapi_pblock_get ( pb, SLAPI_PLUGIN_DB_NO_ACL, &accessCheckDisabled );
    if ( rv != -1 && accessCheckDisabled ) return LDAP_SUCCESS;

	 if ( NULL == aclpb )
		aclpb = acl_get_aclpb ( pb, ACLPB_BINDDN_PBLOCK );

	n_edn = slapi_entry_get_ndn ( e );
	e_sdn = slapi_entry_get_sdn ( e );

	slapi_mods_init_byref(&smods,mods);
	
	for (mod = slapi_mods_get_first_mod(&smods);
		 mod != NULL;
		 mod = slapi_mods_get_next_mod(&smods)) {
		switch (mod->mod_op & ~LDAP_MOD_BVALUES ) {

		   case LDAP_MOD_DELETE:
			if (mod->mod_bvalues != NULL ) {
				break;
			}
			
			/*
			 * Here, check that we have the right to delete all 
			 * the values of the attribute in the entry.
			*/

		   case LDAP_MOD_REPLACE:
			if ( !lastmod ) {
			    if (be == NULL) { 
				if (slapi_pblock_get( pb, SLAPI_BACKEND, &be )) {
				    be = NULL;
				}
			    }
			    if (be != NULL)
				slapi_pblock_get ( pb, SLAPI_BE_LASTMOD, &lastmod );
			}
			if (lastmod &&
			    (strcmp (mod->mod_type, "modifiersname")== 0 ||
			     strcmp (mod->mod_type, "modifytimestamp")== 0)) {
				continue; 
			}

			slapi_entry_attr_find (e, mod->mod_type, &attr);
			if ( attr != NULL) {
				Slapi_Value *sval=NULL;
				const struct berval *attrVal=NULL;
				int k= slapi_attr_first_value(attr,&sval);
				while(k != -1) {
					attrVal = slapi_value_get_berval(sval);
					rv = slapi_access_allowed (pb, e,
						    	     mod->mod_type, 
						    	     (struct berval *)attrVal, /* XXXggood had to cast away const - BAD */
							  		ACLPB_SLAPI_ACL_WRITE_DEL); /* was SLAPI_ACL_WRITE */
					if ( rv != LDAP_SUCCESS) {
						acl_gen_err_msg (
							SLAPI_ACL_WRITE,
							n_edn, 
							mod->mod_type,
							errbuf);
						/* Cleanup */
						slapi_mods_done(&smods);
						return(rv);
					}
					k= slapi_attr_next_value(attr, k, &sval);
				}
			}
			else {
					rv = slapi_access_allowed (pb, e,
						    	     mod->mod_type, 
						    	     NULL,
							  		ACLPB_SLAPI_ACL_WRITE_DEL); /* was SLAPI_ACL_WRITE */
					if ( rv != LDAP_SUCCESS) {
						acl_gen_err_msg (
							SLAPI_ACL_WRITE,
							n_edn, 
							mod->mod_type,
							errbuf);
						/* Cleanup */
						slapi_mods_done(&smods);
						return(rv);
					}
			}
			break;

		   default:
			break;
		} /* switch */

		/* 
         * Check that we have add/delete writes on the specific values
		 * we are trying to add.
        */
        
		if ( aclpb && aclpb->aclpb_curr_entry_sdn )
			slapi_sdn_done ( aclpb->aclpb_curr_entry_sdn );

		if ( mod->mod_bvalues != NULL ) {

			/*
			 * Here, there are specific values specified.
			 * For add and replace--we need add rights for these values.
			 * For delete we need delete rights for these values.
			*/

			for ( i = 0; mod->mod_bvalues[i] != NULL; i++ ) {

				if (SLAPI_IS_MOD_ADD(mod->mod_op) ||
					 SLAPI_IS_MOD_REPLACE(mod->mod_op)) {

						rv = acl_access_allowed (pb,e,
						     mod->mod_type, 
						     mod->mod_bvalues[i],
                        	ACLPB_SLAPI_ACL_WRITE_ADD); /*was SLAPI_ACL_WRITE*/
				} else if (SLAPI_IS_MOD_DELETE(mod->mod_op)) {
						rv = acl_access_allowed (pb,e,
						     mod->mod_type, 
						     mod->mod_bvalues[i],
                        	ACLPB_SLAPI_ACL_WRITE_DEL); /*was SLAPI_ACL_WRITE*/
				} else {
						rv = LDAP_INSUFFICIENT_ACCESS;
				}		
				     
				if ( rv != LDAP_SUCCESS ) {
					acl_gen_err_msg (
						SLAPI_ACL_WRITE,
						n_edn, 
						mod->mod_type,
						errbuf);
					/* Cleanup */
					slapi_mods_done(&smods);
					return rv;
				}
				/* Need to check for all the values because
				** we may be modifying a "self<right>" value.
				*/

				/* Are we adding/replacing a aci attribute
				** value. In that case, we need to make
				** sure that the new value has thr right 
				** syntax
				*/
				if (strcmp(mod->mod_type, 
					   aci_attr_type) == 0) {
					if ( 0 != (rv = acl_verify_syntax( e_sdn,
						      mod->mod_bvalues[i]))) {
						aclutil_print_err(rv, e_sdn, 
							mod->mod_bvalues[i],
							errbuf);
						/* Cleanup */
						slapi_mods_done(&smods);
						return LDAP_INVALID_SYNTAX;
				   	}
				}
			} /* for */
		}
	} /* end of big for */
	/* Cleanup */
	slapi_mods_done(&smods);
	return( LDAP_SUCCESS );
}
/***************************************************************************
*
* acl_modified
*	Modifies ( removed, add, changes) the ACI LIST. 
*
* Input:
*	int 	*optype		- op code
*	char 	*dn		- DN of the entry
*	void 	*change		- The change struct which contais the 
*				- change value
*
* Returns:
*	None.
*
* Error Handling:
*	None.
*
**************************************************************************/
extern void
acl_modified (Slapi_PBlock *pb, int optype, char *n_dn, void *change)
{
	struct  berval	**bvalue;
	char			**value;
	int				rv=0;		/* returned value */
	char*          	new_RDN;
	char*          	parent_DN;
	char*          	new_DN;
	LDAPMod			**mods;
	struct	berval	b;
	int				j;
	Slapi_Attr 		*attr = NULL;
	Slapi_Entry		*e = NULL;
	char			ebuf [ BUFSIZ];
	Slapi_DN		*e_sdn;
	aclUserGroup	*ugroup = NULL;
	
	e_sdn = slapi_sdn_new_ndn_byval ( n_dn );
	/* Before we proceed, Let's first check if we are changing any groups.
	** If we are, then we need to change the signature
	*/
	switch ( optype ) {
	case SLAPI_OPERATION_MODIFY:
	case SLAPI_OPERATION_DELETE:
		slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, (void*)&e);
		break;
	case SLAPI_OPERATION_ADD:
		e = (Slapi_Entry *)change;
		break;
	}

	/* e can be null for RDN */
	if ( e ) slapi_entry_attr_find( e, "objectclass", &attr);

	if ( attr ) {
		int	group_change = 0;
		Slapi_Value *sval=NULL;
		const struct berval *attrVal;
		int i;

		i= slapi_attr_first_value ( attr,&sval );
		while(i != -1) {
		        attrVal = slapi_value_get_berval ( sval );
			if ( (strcasecmp (attrVal->bv_val, "groupOfNames") == 0 )  ||
				(strcasecmp (attrVal->bv_val, "groupOfUniqueNames") == 0 )  ||
				(strcasecmp (attrVal->bv_val, "groupOfCertificates") == 0 )  ||
				(strcasecmp (attrVal->bv_val, "groupOfURLs") == 0 ) ) {
				group_change= 1;
				if ( optype == SLAPI_OPERATION_MODIFY ) {
					Slapi_Attr	*a = NULL;
					int 			rv;
					rv = slapi_entry_attr_find ( e, "uniqueMember", &a);
					if ( rv != 0 ) break;
					rv = slapi_entry_attr_find ( e, "Member", &a );
					if ( rv != 0 ) break;
					rv = slapi_entry_attr_find ( e, "MemberURL", &a );
					if ( rv != 0 ) break;
					/* That means we are not changing the member
					** list, so it's okay to let go this 
					** change
					*/
					group_change = 0;
				} 
				break;
			}
			i= slapi_attr_next_value ( attr, i, &sval );
		}

		/*
		** We can do better here XXX, i.e invalidate the cache for users who
		** use this group. for now just do the whole thing.
		*/
		if ( group_change )  {
			slapi_log_error(SLAPI_LOG_ACL, plugin_name,
			"Group Change: Invalidating entire UserGroup Cache %s\n",
			ACL_ESCAPE_STRING_WITH_PUNCTUATION(n_dn, ebuf));
			aclg_regen_group_signature();
			if (  (optype == SLAPI_OPERATION_MODIFY) || (optype == SLAPI_OPERATION_DELETE ) ) {
				/* Then we need to invalidate the acl signature also */
				acl_signature = aclutil_gen_signature ( acl_signature );
			}
		}
	}

	/*
	 * Here if the target entry is in the group cache
	 * as a user then, as it's being changed it may move out of any dynamic
	 * groups it belongs to.
	 * Just remove it for now--can do better XXX by checking to see if
	 * it really needs to be removed by testing to see if he's
	 * still in th group after the change--but this requires keeping
	 * the memberURL of the group  which we don't currently do.
	 * Also, if we keep
	 * the attributes that are being used in dynamic
	 * groups then we could only remove the user if a sensitive
	 * attribute was being modified (rather than scanning the whole user cache
	 * all the time).  Also could do a hash lookup.
	 *
	 * aclg_find_userGroup() incs a refcnt so we can still refer to ugroup.
	 * aclg_markUgroupForRemoval() decs it and marks it for removal
	 * , so after that you cannot refer to ugroup.
	 *
	*/

	if ( (ugroup = aclg_find_userGroup(n_dn)) != NULL) {	
		/*
		 * Mark this for deletion next time round--try to impact
		 * this mainline code as little as possible.
		*/
		slapi_log_error(SLAPI_LOG_ACL, plugin_name,
			"Marking entry %s for removal from ACL user Group Cache\n",
			ACL_ESCAPE_STRING_WITH_PUNCTUATION(n_dn, ebuf));
		aclg_markUgroupForRemoval (ugroup);
	}

	/*
	 * Take the write lock around all the mods--so that
	 * other operations will see the acicache either before the whole mod
	 * or after but not, as it was before, during the mod.
	 * This is in line with the LDAP concept of the operation
	 * on the whole entry being the atomic unit.
	 * 
	*/
	
	switch(optype) {
	   case SLAPI_OPERATION_DELETE:
		/* In this case we have already checked if the user has 
		** right to delete the entry. Part of delete of entry is 
		** remove all the ACLs also.
		*/
		
		acllist_acicache_WRITE_LOCK();
		rv = acllist_remove_aci_needsLock(e_sdn, NULL);
		acllist_acicache_WRITE_UNLOCK();

		break;
	   case SLAPI_OPERATION_ADD:
		slapi_entry_attr_find ( (Slapi_Entry *) change, aci_attr_type, &attr );

		if ( attr ) {
			Slapi_Value 	*sval=NULL;
			const struct berval 	*attrVal;
			int i;

			acllist_acicache_WRITE_LOCK();
			i= slapi_attr_first_value ( attr,&sval );
			while ( i != -1 ) {
			        attrVal = slapi_value_get_berval(sval);
				rv= acllist_insert_aci_needsLock(e_sdn, attrVal );
				if (rv <= ACL_ERR) 
					aclutil_print_err(rv, e_sdn, attrVal, NULL);
				/* Print the aci list */
				i= slapi_attr_next_value ( attr, i, &sval );
			}
			acllist_acicache_WRITE_UNLOCK();		
		}
		break;

	   case SLAPI_OPERATION_MODIFY:
	   {
		int got_write_lock = 0;

		mods = (LDAPMod **) change;
		
		for (j=0;  mods[j] != NULL; j++) {
			if (strcasecmp(mods[j]->mod_type, aci_attr_type) == 0) {

				/* Got an aci to mod in this list of mods, so
				 * take the acicache lock for the whole list of mods,
				 * remembering to free it below.
				*/
				if ( !got_write_lock) {
					acllist_acicache_WRITE_LOCK();
					got_write_lock = 1;
				}

			   switch (mods[j]->mod_op & ~LDAP_MOD_BVALUES) {
			   case LDAP_MOD_REPLACE:
				/* First remove the item */
				rv = acllist_remove_aci_needsLock(e_sdn, NULL);
				
				/* now fall thru to add the new one */
			   case LDAP_MOD_ADD:
				/* Add the new aci */
				if (mods[j]->mod_op & LDAP_MOD_BVALUES) {
					bvalue = mods[j]->mod_bvalues;	
					if (bvalue == NULL)
						break;
					for (; *bvalue != NULL; ++bvalue) {
						rv=acllist_insert_aci_needsLock( e_sdn, *bvalue);
						if (rv <= ACL_ERR) { 
						    aclutil_print_err(rv, e_sdn,
								   *bvalue, NULL);
						}
					}
				} else {
					value = mods[j]->mod_values;
					if (value == NULL)
						break;
					for (; *value != NULL; ++value) {
						b.bv_len = strlen (*value);
						b.bv_val = *value;
						rv=acllist_insert_aci_needsLock( e_sdn, &b);
						if (rv <= ACL_ERR) {
						    aclutil_print_err(rv, e_sdn,
								   &b, NULL);
						}
					}
				}
				break;
			   case LDAP_MOD_DELETE:
				if (mods[j]->mod_op & LDAP_MOD_BVALUES) {
					bvalue = mods[j]->mod_bvalues;
					if (bvalue == NULL || *bvalue == NULL) {
						 rv = acllist_remove_aci_needsLock( e_sdn, NULL);
					} else {
						for (; *bvalue != NULL; ++bvalue)
						   acllist_remove_aci_needsLock( e_sdn, *bvalue);
					}
				} else {
					value = mods[j]->mod_values;
					if (value == NULL || *value == NULL)  {
					   acllist_remove_aci_needsLock( e_sdn,NULL);
					} else {
						for (; *value != NULL; ++value) {
							b.bv_len = strlen (*value);
							b.bv_val = *value;
							acllist_remove_aci_needsLock( e_sdn, &b);
						}
					}

				}
				break;
				
			   default:
				break;
			}/* modtype switch */
		   }/* attrtype is aci */
		} /* end of for */
		if ( got_write_lock ) {
			acllist_acicache_WRITE_UNLOCK();		
			got_write_lock = 0;
		}

		break;
	   }/* case op is modify*/

	   case SLAPI_OPERATION_MODRDN:

		new_RDN = (char*) change;
		slapi_log_error (SLAPI_LOG_ACL, plugin_name, 
			   "acl_modified (MODRDN %s => \"%s\"\n", 
			   ACL_ESCAPE_STRING_WITH_PUNCTUATION (n_dn, ebuf), new_RDN);

		/* compute new_DN: */
		parent_DN = slapi_dn_parent (n_dn);
		if (parent_DN == NULL) {
			new_DN = new_RDN;
		} else {
			new_DN = slapi_create_dn_string("%s,%s", new_RDN, parent_DN);
		}

		/* Change the acls */
		acllist_acicache_WRITE_LOCK();		
		acllist_moddn_aci_needsLock ( e_sdn, new_DN );
		acllist_acicache_WRITE_UNLOCK();		

		/* deallocat the parent_DN */
		if (parent_DN != NULL)  {
			slapi_ch_free ( (void **) &new_DN );
			slapi_ch_free ( (void **) &parent_DN );
		}
		break;

	   default:
		/* print ERROR */
		break;
	} /*optype switch */
		
	slapi_sdn_free ( &e_sdn );	

}
/***************************************************************************
*
* acl__scan_for_acis
*	Scan the list and picup the correct acls for evaluation.
*
* Input:
*	Acl_PBlock	*aclpb		- Main ACL pblock
*	int			*err;		- Any error status
* Returns:
*	num_handles			- Number of handles matched to the
*					- resource + 1.
* Error Handling:
*	None.
*
**************************************************************************/
static int  
acl__scan_for_acis(Acl_PBlock *aclpb, int *err)
{
	aci_t			*aci;
	NSErr_t			errp;
	int				attr_matched;
	int				deny_handle;
	int				allow_handle;
	int				gen_allow_handle = ACI_MAX_ELEVEL+1;
	int				gen_deny_handle = ACI_MAX_ELEVEL+1;
	int				i;
	PRUint32		cookie;
	
	TNF_PROBE_0_DEBUG(acl__scan_for_acis_start,"ACL","");

	/* 
	** Determine if we are traversing via the list Vs. we have our own 
	** generated list
	*/
	if ( aclpb->aclpb_state & ACLPB_SEARCH_BASED_ON_LIST ||
			aclpb->aclpb_handles_index[0] != -1 ) {
			int kk = 0;
			while ( kk < ACLPB_MAX_SELECTED_ACLS && aclpb->aclpb_handles_index[kk] != -1 ) {
				slapi_log_error(SLAPI_LOG_ACL, plugin_name, "Using ACL Cointainer:%d for evaluation\n", kk);
				kk++;
			}
	}
		
	memset (&errp, 0, sizeof(NSErr_t));
	*err = ACL_FALSE;
	aclpb->aclpb_num_deny_handles = -1;
	aclpb->aclpb_num_allow_handles = -1;
	for (i=0; i <= ACI_MAX_ELEVEL; i++) {
		aclpb->aclpb_deny_handles [i] = NULL;
		aclpb->aclpb_allow_handles [i] = NULL;
	}
		
	/* Check the signature. If it has changed, start fresh */
	if ( aclpb->aclpb_signature != acl_signature ) {
		slapi_log_error (SLAPI_LOG_ACL, plugin_name, 
				"Restart the scan -- due to acl changes\n");
		acllist_init_scan ( aclpb->aclpb_pblock, LDAP_SCOPE_BASE, NULL );
	}

	attr_matched = ACL_FALSE;
	deny_handle = 0;
	allow_handle = 0;
	i = 0;

	aclpb->aclpb_stat_acllist_scanned++;
	aci = acllist_get_first_aci ( aclpb, &cookie );

	while( aci ) {
		if (acl__resource_match_aci(aclpb, aci, 0, &attr_matched)) {
			/* Generate the ACL list handle  */
			if (aci->aci_handle == NULL) {
				aci = acllist_get_next_aci ( aclpb, aci, &cookie );
				continue;
			}
			aclutil_print_aci (aci, acl_access2str (aclpb->aclpb_access));

			if (aci->aci_type & ACI_HAS_DENY_RULE) {
				if (aclpb->aclpb_deny_handles[aci->aci_elevel] == NULL ) {
					aclpb->aclpb_deny_handles[aci->aci_elevel] = aci;
				} else {
				   if ((gen_deny_handle + ACI_DEFAULT_ELEVEL + 1) == 
					aclpb->aclpb_deny_handles_size ) {
					int num = ACLPB_INCR_LIST_HANDLES +
						aclpb->aclpb_deny_handles_size;
					/* allocate more space */
					aclpb->aclpb_deny_handles =  
						(aci_t **) 
						  slapi_ch_realloc (
						       (void *) aclpb->aclpb_deny_handles,
						       num * sizeof (aci_t *));
					aclpb->aclpb_deny_handles_size = num;
				   }	
				   aclpb->aclpb_deny_handles [gen_deny_handle] = aci;
				   gen_deny_handle++;
				}
				deny_handle++;
			}
			/* 
			** It's possible that a single acl is in both the camps i.e
			** It has a allow and a deny rule 
			** In that case we keep the same acl in both the camps.
			*/ 
			if (aci->aci_type & ACI_HAS_ALLOW_RULE) {
				if (aclpb->aclpb_allow_handles[aci->aci_elevel] == NULL ) {
					aclpb->aclpb_allow_handles[aci->aci_elevel] = aci;
				} else {
				   if ((gen_allow_handle + ACI_DEFAULT_ELEVEL + 1) == 
					aclpb->aclpb_allow_handles_size) {
					/* allocate more space */
					int num = ACLPB_INCR_LIST_HANDLES +
						aclpb->aclpb_allow_handles_size;

					aclpb->aclpb_allow_handles =  
						(aci_t **) 
						  slapi_ch_realloc (
						       (void *) aclpb->aclpb_allow_handles,
						       num * sizeof (aci_t *));
					aclpb->aclpb_allow_handles_size = num;
				   }	
				   aclpb->aclpb_allow_handles [gen_allow_handle] = aci;
				   gen_allow_handle++;
				}
				allow_handle++;
			}
		}
		aci = acllist_get_next_aci ( aclpb, aci, &cookie );
	} /* end of while */

	/* make the last one a null */
	aclpb->aclpb_deny_handles [gen_deny_handle] = NULL;
	aclpb->aclpb_allow_handles [gen_allow_handle] = NULL;

	/* specify how many we found */
	aclpb->aclpb_num_deny_handles = deny_handle;
	aclpb->aclpb_num_allow_handles = allow_handle;

	slapi_log_error(SLAPI_LOG_ACL, plugin_name, "Num of ALLOW Handles:%d, DENY handles:%d\n", 
		  aclpb->aclpb_num_allow_handles, aclpb->aclpb_num_deny_handles);

	TNF_PROBE_0_DEBUG(acl__scan_for_acis_end,"ACL","");

	return(allow_handle + deny_handle);
}

/***************************************************************************
*
* acl__resource_match_aci
*
*	This compares the ACI for the given resource and determines if
*	the ACL applies to the resource or not.
*
*	For read/search operation, we collect all the possible acls which
*	will apply, We will be using this list  for future acl evaluation
*	for that entry.
*
* Input:
*	struct acl_pblock	*aclpb - Main acl private block
*	aci_t		*aci		- The ACI item
*	int		skip_attrEval	- DOn't check for attrs
*	int		*a_matched	- Attribute matched 
*
* Returns:
*
*	ACL_TRUE			- Yep, This  ACL is applicable to the
*					- the resource.
*	ACL_FALSE			- No it is not.
*
* Error Handling:
*	None.
*
**************************************************************************/
#define ACL_RIGHTS_TARGETATTR_NOT_NEEDED  ( SLAPI_ACL_ADD | SLAPI_ACL_DELETE | SLAPI_ACL_PROXY)
static int
acl__resource_match_aci( Acl_PBlock *aclpb, aci_t *aci, int skip_attrEval, int *a_matched)
{

	struct slapi_filter 	*f;			/* filter */
	int						rv;			/* return value */
	int						matches;
	int						attr_matched;
	int						attr_matched_in_targetattrfilters = 0;
	int						dn_matched;
	char					*res_attr;
	int						aci_right = 0;
	int						res_right = 0;
	int						star_matched = ACL_FALSE;
	int						num_attrs = 0;
	AclAttrEval				*c_attrEval = NULL;
	const	char			*res_ndn = NULL;
	const	char			*aci_ndn = NULL;
	char					*matched_val = NULL;	
	int						add_matched_val_to_ht = 0;
	char 					res_right_str[128];

	TNF_PROBE_0_DEBUG(acl__resource_match_aci_start,"ACL","");

	aclpb->aclpb_stat_aclres_matched++;

	/* Assume that resource matches */
	matches  = ACL_TRUE;

	/* Figure out if the acl has the correct rights or not */
	aci_right = aci->aci_access;
	res_right = aclpb->aclpb_access;
	if (!(aci_right & res_right)) {
		/* If we are looking for read/search and the acl has read/search
		** then go further because if targets match we may keep that
		** acl in  the entry cache list.
		*/
		if (!((res_right & (SLAPI_ACL_SEARCH | SLAPI_ACL_READ)) &&
			(aci_right & (SLAPI_ACL_SEARCH | SLAPI_ACL_READ))))
			matches = ACL_FALSE;
			goto acl__resource_match_aci_EXIT;			
	}

	
	/* first Let's see if the entry is under the subtree where the
	** ACL resides. We can't let somebody affect a target beyond the 
	** scope of where the ACL resides
	** Example: ACL is located in "ou=engineering, o=ace industry, c=us
	** but if the target is "o=ace industry, c=us", then we are in trouble.
	**
	** If the aci is in the rootdse and the entry is not, then we do not
	** match--ie. acis in the rootdse do NOT apply below...for the moment.
	** 
	*/
	res_ndn = slapi_sdn_get_ndn ( aclpb->aclpb_curr_entry_sdn );
	aci_ndn = slapi_sdn_get_ndn ( aci->aci_sdn );
	if (!slapi_sdn_issuffix(aclpb->aclpb_curr_entry_sdn, aci->aci_sdn)
		|| (!slapi_is_rootdse(res_ndn) && slapi_is_rootdse(aci_ndn)) ) {

		/* cant' poke around */
		matches = ACL_FALSE;
		goto acl__resource_match_aci_EXIT;			
	}
	
	/*
	** We have a single ACI which we need to find if it applies to
	** the resource or not.
	*/
	if ((aci->aci_type & ACI_TARGET_DN) && (aclpb->aclpb_curr_entry_sdn)) {
		char		*avaType;
		struct berval	*avaValue;		

		f = aci->target; 
		dn_matched = ACL_TRUE;
		slapi_filter_get_ava ( f, &avaType, &avaValue );
		
		if (!slapi_dn_issuffix( res_ndn, avaValue->bv_val)) {
			dn_matched = ACL_FALSE;
		}
		if (aci->aci_type & ACI_TARGET_NOT) {
			matches = (dn_matched ? ACL_FALSE : ACL_TRUE);
		} else {
			matches = (dn_matched ? ACL_TRUE: ACL_FALSE);
		}
	}
	
	/* No need to look further */
	if (matches == ACL_FALSE) {
		goto acl__resource_match_aci_EXIT;	
	}

	if (aci->aci_type & ACI_TARGET_PATTERN) {
		
		f = aci->target; 
		dn_matched = ACL_TRUE;
	
		if ((rv = acl_match_substring(f, (char *)res_ndn, 0 /* match suffux */)) != ACL_TRUE) {
			dn_matched = ACL_FALSE;
			if(rv == ACL_ERR) {
				slapi_log_error(SLAPI_LOG_ACL, plugin_name, 
					"acl__resource_match_aci:pattern err\n");
				matches = ACL_FALSE;
				goto acl__resource_match_aci_EXIT;	
			}
		}
		if (aci->aci_type & ACI_TARGET_NOT) {
			matches = (dn_matched ? ACL_FALSE : ACL_TRUE);
		} else {
			matches = (dn_matched ? ACL_TRUE: ACL_FALSE);
		}
	}

	/* No need to look further */
	if (matches == ACL_FALSE) {
		goto acl__resource_match_aci_EXIT;	
	}

	/*
	 * Is it a (target="ldap://cn=*,($dn),o=sun.com") kind of thing.
	*/
	if (aci->aci_type & ACI_TARGET_MACRO_DN) {
		/*
		 * See if the ($dn) component matches the string and
		 * retrieve the matched substring for later use
		 * in the userdn.
		 * The macro string is a function of the dn only, so if the
		 * entry is the same one don't recalculate it--
		 * this flag only works for search right now, could
		 * also optimise for mods by making it work for mods.
		*/
			
		if ( (aclpb->aclpb_res_type & ACLPB_NEW_ENTRY) == 0 ) {
			/*
			 * Here same entry so just look up the matched value,
			 * calculated from the targetdn and stored judiciously there
			 */
			matched_val = (char *)acl_ht_lookup( aclpb->aclpb_macro_ht,
										(PLHashNumber)aci->aci_index);
		}
		if ( matched_val == NULL &&
			(aclpb->aclpb_res_type & (ACLPB_NEW_ENTRY | ACLPB_EFFECTIVE_RIGHTS))) {

			slapi_log_error (SLAPI_LOG_ACL, plugin_name,
				"Evaluating macro aci(%d)%s for resource %s\n",
				aci->aci_index, aci->aclName,
				aclutil__access_str(res_right, res_right_str));
			matched_val = acl_match_macro_in_target( res_ndn,
												aci->aci_macro->match_this,
												aci->aci_macro->macro_ptr);
			add_matched_val_to_ht = 1; /* may need to add matched value to ht */
		}
		if (matched_val == NULL) {
			dn_matched = ACL_FALSE;
		} else {
			dn_matched = ACL_TRUE;
		}
														
		if (aci->aci_type & ACI_TARGET_NOT) {
			matches = (dn_matched ? ACL_FALSE : ACL_TRUE);
		} else {
			matches = (dn_matched ? ACL_TRUE: ACL_FALSE);
		}
		
		if ( add_matched_val_to_ht ) {
			if ( matches == ACL_TRUE && matched_val ) {
				/*
				 * matched_val may be needed later for matching on
				 * other targets or on the subject--so optimistically
				 * put it in the hash table.
				 * If, at the end of this routine, we
				 * find that after all the resource did not match then
				 * that's ok--the value is freed at aclpb cleanup time.
				 * If there is already an entry for this aci in this
				 * aclpb then remove it--it's an old value for a
				 * different entry.
				*/

				acl_ht_add_and_freeOld(aclpb->aclpb_macro_ht,
									(PLHashNumber)aci->aci_index,
									matched_val);
				slapi_log_error (SLAPI_LOG_ACL, plugin_name,
					"-- Added aci(%d) and matched value (%s) to macro ht\n",
					aci->aci_index, matched_val);
				acl_ht_display_ht(aclpb->aclpb_macro_ht);
			} else {
				slapi_ch_free((void **)&matched_val);
				if (matches == ACL_FALSE) {
					slapi_log_error (SLAPI_LOG_ACL, plugin_name,
						"Evaluated ACL_FALSE\n");
				}
			}
		}
	} /* MACRO_DN */

	/* No need to look further */
	if (matches == ACL_FALSE) {
		goto acl__resource_match_aci_EXIT;	
	}

	/* 
	** Here, if there's a targetfilter field, see if it matches.
	**
	** The commented out code below was an erroneous attempt to skip
	** this test.  It is wrong because: 1. you need to store
	** whether the last test matched or not (you cannot just assume it did)
	** and 2. It may not be the same aci, so the previous matched
	** value is a function of the aci.
	** May be interesting to build such a cache...but no evidence for
	** for that right now. See Bug 383424.
	**
	** 
	**   && ((aclpb->aclpb_state & ACLPB_SEARCH_BASED_ON_LIST) ||
	**	(aclpb->aclpb_res_type & ACLPB_NEW_ENTRY))
	*/
	if (aci->aci_type & ACI_TARGET_FILTER ) {
		int 	filter_matched = ACL_TRUE;

		/*
		 * Check for macros.
		 * For targetfilter we need to fake the lasinfo structure--it's
		 * created "naturally" for subjects but not targets.
		*/
		

		if ( aci->aci_type &  ACI_TARGET_FILTER_MACRO_DN) {			
				
			lasInfo *lasinfo = NULL;
			
			lasinfo = (lasInfo*) slapi_ch_malloc( sizeof(lasInfo) );

			lasinfo->aclpb = aclpb;
			lasinfo->resourceEntry = aclpb->aclpb_curr_entry;
			aclpb->aclpb_curr_aci = aci;
			filter_matched = aclutil_evaluate_macro( aci->targetFilterStr,
													lasinfo,
													ACL_EVAL_TARGET_FILTER);
			slapi_ch_free((void**)&lasinfo);
		} else {


			if (slapi_vattr_filter_test(NULL, aclpb->aclpb_curr_entry, 
				aci->targetFilter,
				0 /*don't do acess chk*/)!= 0) {
				filter_matched = ACL_FALSE;
			}

		}

		/* If it's a logical value we can do logic on it...otherwise we do not match */
		if ( filter_matched == ACL_TRUE || filter_matched == ACL_FALSE) {
			if (aci->aci_type & ACI_TARGET_FILTER_NOT) {
				matches = (filter_matched == ACL_TRUE ? ACL_FALSE : ACL_TRUE);
			} else {
				matches = (filter_matched == ACL_TRUE ? ACL_TRUE: ACL_FALSE);
			}
		} else {
			matches = ACL_FALSE;
			slapi_log_error( SLAPI_LOG_ACL, plugin_name,
				"Returning UNDEFINED for targetfilter evaluation.\n");
		}

		if (matches == ACL_FALSE) {
			goto acl__resource_match_aci_EXIT;	
		}
	}

	/*
	 * Check to see if we need to evaluate any targetattrfilters.
	 * They look as follows:
	 * (targetattrfilters="add=sn:(sn=rob) && gn:(gn!=byrne),
	 *					   del=sn:(sn=rob) && gn:(gn=byrne)")
	 *
	 * For ADD/DELETE:
	 * If theres's a targetattrfilter then each add/del filter
	 * that applies to an attribute in the entry, must be satisfied
	 * by each value of the attribute in the entry.
	 *
	 * For MODIFY:
	 *	If there's a targetattrfilter then the add/del filter
	 * must be satisfied by the attribute to be added/deleted.
	 * (MODIFY acl is evaluated one value at a time).
	 * 
	 *
	*/

	if (((aclpb->aclpb_access & SLAPI_ACL_ADD) &&
		 (aci->aci_type & ACI_TARGET_ATTR_ADD_FILTERS) )||
		((aclpb->aclpb_access & SLAPI_ACL_DELETE) && 
		 (aci->aci_type & ACI_TARGET_ATTR_DEL_FILTERS) ) ) {
		
			Targetattrfilter **attrFilterArray;

			Targetattrfilter	*attrFilter = NULL;

			Slapi_Attr	*attr_ptr = NULL;
			Slapi_Value *sval;
			const struct berval *attrVal;
			int k;
			int done;


			if (aclpb->aclpb_access & SLAPI_ACL_ADD &&
				aci->aci_type & ACI_TARGET_ATTR_ADD_FILTERS) {

				attrFilterArray = aci->targetAttrAddFilters;

			} else if (aclpb->aclpb_access & SLAPI_ACL_DELETE && 
						aci->aci_type & ACI_TARGET_ATTR_DEL_FILTERS) {
				
				attrFilterArray = aci->targetAttrDelFilters;

			}

			attr_matched = ACL_TRUE;
			num_attrs = 0;

			while (attrFilterArray[num_attrs] && attr_matched) {
				attrFilter = attrFilterArray[num_attrs];

				/* 
				 * If this filter applies to an attribute in the entry,
				 * apply it to the entry.
				 * Otherwise just ignore it.
				 * 
				*/

				if (slapi_entry_attr_find ( aclpb->aclpb_curr_entry,
										   attrFilter->attr_str,
										   &attr_ptr) == 0)  {

					/*
					 * This is an applicable filter.
					 *  The filter is to be appplied to the entry being added
					 * or deleted.
					 * The filter needs to be satisfied by _each_ occurence
					 * of the attribute in the entry--otherwise you
					 * could satisfy the filter and then put loads of other
					 * values in on the back of it.
				 	*/

					sval=NULL;
					attrVal=NULL;
					k= slapi_attr_first_value(attr_ptr,&sval);
					done = 0;
					while(k != -1 && !done) {
						attrVal = slapi_value_get_berval(sval);

						if ( acl__make_filter_test_entry(
                            	&aclpb->aclpb_filter_test_entry,
								attrFilter->attr_str,
								(struct berval *)attrVal) == LDAP_SUCCESS ) {                      
				
							attr_matched= acl__test_filter( 
                            	    	aclpb->aclpb_filter_test_entry,
						   	   			attrFilter->filter,
						   	   			1 /* Do filter sense evaluation below */
							 			);
							done = !attr_matched;
							slapi_entry_free( aclpb->aclpb_filter_test_entry );			
						}                                           
						
						k= slapi_attr_next_value(attr_ptr, k, &sval);
					}/* while */

					/*
					 * Here, we applied an applicable filter to the entry.
					 * So if attr_matched is ACL_TRUE then every value
					 * of the attribute in the entry satisfied the filter.
					 * Otherwise, attr_matched is ACL_FALSE and not every 
					 * value satisfied the filter, so we will teminate the
					 * scan of the filter list.					
					*/

				}

				num_attrs++;
			} /* while */

		/*
		 * Here, we've applied all the applicable filters to the entry.
		 * Each one must have been satisfied by all the values of the attribute.
		 * The result of this is stored in attr_matched.
		*/		

#if 0		
		/*
		 * Don't support a notion of "add != " or "del != "
		 * at the moment.
		 * To do this, need to test whether it's an add test or del test
		 * then if it's add and ACI_TARGET_ATTR_ADD_FILTERS_NOT then
		 * flip the bit.  Same for del.
		*/

		if (aci->aci_type & ACI_TARGET_ATTR_DEL_FILTERS_NOT) {
			matches = (matches ? ACL_FALSE : ACL_TRUE);
		} else {
			matches = (matches ? ACL_TRUE: ACL_FALSE);
		}
#endif

		/* No need to look further */
		if (attr_matched == ACL_FALSE) {
			matches = ACL_FALSE;
			goto acl__resource_match_aci_EXIT;	
		}

	} else  if ( ((aclpb->aclpb_access & ACLPB_SLAPI_ACL_WRITE_ADD) &&
                  (aci->aci_type & ACI_TARGET_ATTR_ADD_FILTERS)) ||
                 ((aclpb->aclpb_access & ACLPB_SLAPI_ACL_WRITE_DEL) &&
                  (aci->aci_type & ACI_TARGET_ATTR_DEL_FILTERS)) ) {

            
		/*     
		 * Here, it's a modify add/del and we have attr filters.  
		 * So, we need to scan the add/del filter list to find the filter
		 * that applies to the current attribute.
		 * Then the (attribute,value) pair being added/deleted better
		 * match that filter.
		 *
		 *
		 */

		Targetattrfilter **attrFilterArray = NULL;
		Targetattrfilter	*attrFilter;
		int found = 0;

		if ((aclpb->aclpb_access & ACLPB_SLAPI_ACL_WRITE_ADD) &&
			(aci->aci_type & ACI_TARGET_ATTR_ADD_FILTERS)) {

			attrFilterArray = aci->targetAttrAddFilters;

		} else if ((aclpb->aclpb_access & ACLPB_SLAPI_ACL_WRITE_DEL) && 
				   (aci->aci_type & ACI_TARGET_ATTR_DEL_FILTERS)) {
				
			attrFilterArray = aci->targetAttrDelFilters;

		}
		

		/*
		 * Scan this filter list for an applicable filter.
		 */

		found = 0;
		num_attrs = 0;

		while (attrFilterArray[num_attrs] && !found) {
				attrFilter = attrFilterArray[num_attrs];

			/* If this filter applies to the attribute, stop. */
			if ((aclpb->aclpb_curr_attrEval) &&
				slapi_attr_type_cmp ( aclpb->aclpb_curr_attrEval->attrEval_name,
									  attrFilter->attr_str, 1) == 0) {
				found = 1;
			}
			num_attrs++;
		}

		/*
		 * Here, if found an applicable filter, then apply the filter to the 
		 * (attr,val) pair.
		 * Otherwise, ignore the targetattrfilters.
		*/

		if (found) {

			if ( acl__make_filter_test_entry(
                            	&aclpb->aclpb_filter_test_entry,
								aclpb->aclpb_curr_attrEval->attrEval_name,
								aclpb->aclpb_curr_attrVal) == LDAP_SUCCESS ) {                      
				
				attr_matched= acl__test_filter(aclpb->aclpb_filter_test_entry,
										attrFilter->filter,
										1 /* Do filter sense evaluation below */
										);                            
				slapi_entry_free( aclpb->aclpb_filter_test_entry );			
			}           

			/* No need to look further */
			if (attr_matched == ACL_FALSE) {
				matches = attr_matched;
				goto acl__resource_match_aci_EXIT;
			}

			/*
			 * Here this attribute appeared and was matched in a
			 * targetattrfilters list, so record this fact so we do
			 * not have to scan the targetattr list for the attribute.
			*/
		
			attr_matched_in_targetattrfilters = 1;
		}                                
	} /* targetvaluefilters */ 
	

	/* There are 3 cases  by which acis are selected.
	** 1) By scanning the whole list and picking based on the resource.
	** 2) By picking a subset of the list which will be used for the whole
	**    acl evaluation. 
	** 3) A finer granularity, i.e, a selected list of acls which will be
	** used for only that entry's evaluation.
	*/
	if ( !(skip_attrEval) && (aclpb->aclpb_state & ACLPB_SEARCH_BASED_ON_ENTRY_LIST) &&
		(res_right & SLAPI_ACL_SEARCH) && 
		((aci->aci_access & SLAPI_ACL_READ) || (aci->aci_access & SLAPI_ACL_SEARCH))) {
		int	kk=0;

		while ( kk < ACLPB_MAX_SELECTED_ACLS && aclpb->aclpb_handles_index[kk] >=0 ) kk++;
		if (kk >= ACLPB_MAX_SELECTED_ACLS)  {
			aclpb->aclpb_state &= ~ACLPB_SEARCH_BASED_ON_ENTRY_LIST;
		} else {
			aclpb->aclpb_handles_index[kk++] = aci->aci_index;
			aclpb->aclpb_handles_index[kk] = -1;
		}
	}


	/* If we are suppose to skip attr eval, then let's skip it */
	if ( (aclpb->aclpb_access & SLAPI_ACL_SEARCH ) && ( ! skip_attrEval ) &&
		( aclpb->aclpb_res_type &  ACLPB_NEW_ENTRY )) {
		aclEvalContext		*c_evalContext = &aclpb->aclpb_curr_entryEval_context;
		int			nhandle = c_evalContext->acle_numof_tmatched_handles;

		if ( nhandle < ACLPB_MAX_SELECTED_ACLS) {
		   c_evalContext->acle_handles_matched_target[nhandle] = aci->aci_index;
		   c_evalContext->acle_numof_tmatched_handles++;
		}
	}

	if ( skip_attrEval ) {
		goto acl__resource_match_aci_EXIT;
	}

	/* We need to check again because we don't want to select this handle 
	** if the right doesn't match for now.
	*/
	if (!(aci_right & res_right)) {
		matches = ACL_FALSE;
		goto acl__resource_match_aci_EXIT;
	}

    /*
     * Here if the request is one that requires matching 
     * on a targetattr then do it here.
     * If we have already matched an attribute in the targetattrfitlers list
     * then we do not require a match in the targetattr so we can skip it.
     * The operations that require targetattr are SLAPI_ACL_COMPARE,
     * SLAPI_ACL_SEARCH, SLAPI_ACL_READ and SLAPI_ACL_WRITE, as long as
     * c_attrEval is non-null (otherwise it's a modrdn op which
     * does not require the targetattr list).	  
     *
	 * rbyrneXXX if we had a proper permission for modrdn eg SLAPI_ACL_MODRDN
	 * then we would not need this crappy way of telling it was a MODRDN
	 * request ie. SLAPI_ACL_WRITE && !(c_attrEval).
    */
    
	c_attrEval = aclpb->aclpb_curr_attrEval;
    
    /*
     * If we've already matched on targattrfilter then do not
     * bother to look at the attrlist.
    */
    
    if (!attr_matched_in_targetattrfilters) {                
            
	/* match target attr */
	if ((c_attrEval) && 
		(aci->aci_type & ACI_TARGET_ATTR))	{
			/* there is a target ATTR */
			Targetattr	**attrArray = aci->targetAttr;
			Targetattr	*attr = NULL;

			res_attr = c_attrEval->attrEval_name;
			attr_matched = ACL_FALSE;
			star_matched = ACL_FALSE;
			num_attrs = 0;

			while (attrArray[num_attrs] && !attr_matched) {
				attr = attrArray[num_attrs];           
	            if (attr->attr_type & ACL_ATTR_STRING) { 
					if (slapi_attr_type_cmp ( res_attr, 
					      		attr->u.attr_str, 1) == 0) {
						attr_matched = ACL_TRUE;
						*a_matched = ACL_TRUE;
					} 
				} else if (attr->attr_type & ACL_ATTR_FILTER) {
					if (ACL_TRUE == acl_match_substring (
								attr->u.attr_filter, 
								res_attr, 1)) {
						attr_matched = ACL_TRUE;
						*a_matched = ACL_TRUE;
					}
				} else if (attr->attr_type & ACL_ATTR_STAR) {
					attr_matched = ACL_TRUE;
					*a_matched = ACL_TRUE;
					star_matched = ACL_TRUE;
				} 
				num_attrs++;
			}

			if (aci->aci_type & ACI_TARGET_ATTR_NOT) {
				matches = (attr_matched ? ACL_FALSE : ACL_TRUE);
			} else {
				matches = (attr_matched ? ACL_TRUE: ACL_FALSE);
			}


			aclpb->aclpb_state &= ~ACLPB_ATTR_STAR_MATCHED;
			/* figure out how it matched, i.e star matched */
			if (matches && star_matched && num_attrs == 1 &&
				!(aclpb->aclpb_state & ACLPB_FOUND_ATTR_RULE))
				aclpb->aclpb_state |= ACLPB_ATTR_STAR_MATCHED;
			else {
				/* we are here means that there is a specific 
				** attr in the rule for this resource.
				** We need to avoid this case
				** Rule 1: (targetattr = "uid")
				** Rule 2: (targetattr = "*")
				** we cannot use STAR optimization
				*/
				aclpb->aclpb_state |=  ACLPB_FOUND_ATTR_RULE;
				aclpb->aclpb_state &=  ~ACLPB_ATTR_STAR_MATCHED;
			}
	} else if ( (c_attrEval) ||
		    (aci->aci_type & ACI_TARGET_ATTR)) {
		if ((aci_right & ACL_RIGHTS_TARGETATTR_NOT_NEEDED) &&
		    (aclpb->aclpb_access & ACL_RIGHTS_TARGETATTR_NOT_NEEDED)) {
			/* 
			** Targetattr rule doesn't  make any sense
			** in this case. So select this rule
			** default: matches = ACL_TRUE;
			*/
			;
		} else if (aci_right & SLAPI_ACL_WRITE && 
			  (aci->aci_type & ACI_TARGET_ATTR) &&
			  !(c_attrEval)) {
			/* We need to handle modrdn operation.  Modrdn doesn't 
			** change any attrs but changes the RDN and so (attr=NULL).
			** Here we found an acl which has a targetattr but
			** the resource doesn't need one. In that case, we should
			** consider this acl.
			** default: matches = ACL_TRUE;
			*/
			;
		} else {
			matches = ACL_FALSE;
		}
	}
	}/* !attr_matched_in_targetattrfilters */
   
	/* 
	** Here we are testing if we find a entry test rule (which should
	** be rare). In that case, just remember it. An entry test rule
	** doesn't have "(targetattr)".
	*/
	if (aclpb && (aclpb->aclpb_state & ACLPB_EVALUATING_FIRST_ATTR) &&
		(!(aci->aci_type & ACI_TARGET_ATTR))) {
		aclpb->aclpb_state |= ACLPB_FOUND_A_ENTRY_TEST_RULE;
	}

	/*
	 * Generic exit point for this routine:
	 * matches is ACL_TRUE if the aci matches the target of the resource,
	 * ACL_FALSE othrewise.
	 * Apologies for the goto--this is a retro-fitted exit point.
	*/
acl__resource_match_aci_EXIT:

	/*
	 * For macro acis, there may be a partial macro string
	 * placed in the aclpb_macro_ht
	 * even if the aci did not finally match.
	 * All the partial strings will be freed at aclpb
	 * cleanup time.
	*/

	TNF_PROBE_0_DEBUG(acl__resource_match_aci_end,"ACL","");

	return (matches);
}
/* Macro to determine if the cached result is valid or not. */
#define ACL_CACHED_RESULT_VALID( result) 				\
	(((result & ACLPB_CACHE_READ_RES_ALLOW) &&			\
		(result & ACLPB_CACHE_READ_RES_SKIP)) ||		\
	    ((result & ACLPB_CACHE_SEARCH_RES_ALLOW) &&			\
		(result & ACLPB_CACHE_SEARCH_RES_SKIP))) ? 0 : 1
/***************************************************************************
*
* acl__TestRights
*
*	Test the rights and find out if access is allowed or not.
*
*	Processing Alogorithm:
*	
*	First, process the DENY rules one by one. If the user is not explicitly
*	denied, then check if the user is allowed by processing the ALLOW handles
*	one by one.
*	The result of the evaluation is cached. Exceptions are
*         -- If an acl happens to be in both DENY and ALLOW camp.
*	  -- Only interested for READ/SEARCH right.
*
* Input:
* 	struct acl_pblock 	*aclpb - main acl private block
*	int					access	- The access bits
*	char				**right		- The right we are looking for
*	char				** generic	- Generic rights
*
* Returns:
*
*	ACL_RES_ALLOW			- Access allowed
*	ACL_RES_DENY			- Access denied
*	err				- error condition
*
* Error Handling:
*	None.
*
**************************************************************************/
static int
acl__TestRights(Acl_PBlock *aclpb,int access, char **right, char ** map_generic,
				aclResultReason_t *result_reason)
{
	ACLEvalHandle_t		*acleval;
	int			rights_rv = ACL_RES_DENY;
	int			rv, i,j, k;
	int				index;
	char			*deny = NULL;
	char			*deny_generic = NULL;
	char			*acl_tag;
	int			expr_num;
	char			*testRights[2];
	aci_t			*aci;
	int			numHandles = 0;
	
	TNF_PROBE_0_DEBUG(acl__TestRights_start,"ACL","");

	/* record the aci and reason for access decision */
	result_reason->deciding_aci = NULL;
	result_reason->reason = ACL_REASON_NONE;

	/* If we don't have any ALLLOW handles, it's DENY by default */
	if (aclpb->aclpb_num_allow_handles <= 0) {
		result_reason->deciding_aci = NULL;
		result_reason->reason = ACL_REASON_NO_MATCHED_RESOURCE_ALLOWS;

		TNF_PROBE_1_DEBUG(acl__TestRights_end,"ACL","",
							tnf_string,no_allows,"");	

		return ACL_RES_DENY;
	}

	/* Get the ACL evaluation Context */
 	acleval =  aclpb->aclpb_acleval;

	testRights[0] = *right;
	testRights[1] = '\0';
	
	/* 
	** START PROCESSING DENY HANDLES
	** Process each handle at a time. Do not concatenate the handles or else
	** all the context information will be build again and we will pay a
	** lot of penalty. The context is built the first time the handle is
	** processed.
	**
	** First we set the default to INVALID so that if rules are not matched, then
	** we get INVALID and if a rule matched, the we get DENY.
	*/
	aclpb->aclpb_state &= ~ACLPB_EXECUTING_ALLOW_HANDLES;
	aclpb->aclpb_state |= ACLPB_EXECUTING_DENY_HANDLES;
	ACL_SetDefaultResult (NULL, acleval, ACL_RES_INVALID);

	numHandles = ACI_MAX_ELEVEL + aclpb->aclpb_num_deny_handles;
	for (i=0, k=0; i < numHandles && k < aclpb->aclpb_num_deny_handles ; ++i) {
		int skip_eval = 0;

		/* 
		** If the handle has been evaluated before, we can
		** cache the result.
		*/
		if (((aci = aclpb->aclpb_deny_handles[i]) == NULL) && (i <= ACI_MAX_ELEVEL))
			continue;
		k++;
		index = aci->aci_index;
		slapi_log_error(SLAPI_LOG_ACL, plugin_name,
			"Evaluating DENY aci(%d) \"%s\"\n", index, aci->aclName);

		if (access  & ( SLAPI_ACL_SEARCH | SLAPI_ACL_READ)) {

			/*
			 * aclpb->aclpb_cache_result[0..aclpb->aclpb_last_cache_result] is
			 * a cache of info about whether applicable acis
			 * allowed, did_not_allow or denied access
			*/			
			for (j =0; j < aclpb->aclpb_last_cache_result; j++) {
				if (index == aclpb->aclpb_cache_result[j].aci_index) {
					short  result;

					result = aclpb->aclpb_cache_result[j].result;
					if ( result <= 0) break;
					if (!ACL_CACHED_RESULT_VALID(result)) {
						/* something is wrong. Need to evaluate */
						aclpb->aclpb_cache_result[j].result = -1;
						break;
					}
					/*
					** We have a valid cached result. Let's see if we 
					** have what we need.
					*/
					if (access & SLAPI_ACL_SEARCH) {
						if ( result & ACLPB_CACHE_SEARCH_RES_DENY){
							slapi_log_error(SLAPI_LOG_ACL, plugin_name, 
						           "DENY:Found SEARCH DENY in cache\n");
							__acl_set_aclIndex_inResult ( aclpb, access, index );
							result_reason->deciding_aci = aci;
							result_reason->reason = ACL_REASON_RESULT_CACHED_DENY;
							TNF_PROBE_1_DEBUG(acl__TestRights_end,"ACL","",
								tnf_string,cached_deny,"");
							return ACL_RES_DENY;
						} else if ((result & ACLPB_CACHE_SEARCH_RES_SKIP) ||
							   (result & ACLPB_CACHE_SEARCH_RES_ALLOW)) { 
					  	    slapi_log_error(SLAPI_LOG_ACL, plugin_name, 
							     "DENY:Found SEARCH SKIP in cache\n");
							skip_eval = 1;
							break;
						} else {
							break;
						}
					} else {	/* must be READ */
						if (result & ACLPB_CACHE_READ_RES_DENY) {
							slapi_log_error(SLAPI_LOG_ACL, plugin_name, 
								  "DENY:Found READ DENY in cache\n");
							__acl_set_aclIndex_inResult ( aclpb, access, index );
							result_reason->deciding_aci = aci;
							result_reason->reason = ACL_REASON_RESULT_CACHED_DENY;
							TNF_PROBE_1_DEBUG(acl__TestRights_end,"ACL","",
								tnf_string,cached_deny,"");
							return ACL_RES_DENY;
						} else if ( result & ACLPB_CACHE_READ_RES_SKIP) {
							slapi_log_error(SLAPI_LOG_ACL, plugin_name, 
								  "DENY:Found READ SKIP in cache\n");
							skip_eval = 1;
							break;
						} else {
							break;
						}
					}
				}
			}
		}
		if (skip_eval) {
			skip_eval = 0;
			continue;
		}

		rv = ACL_EvalSetACL(NULL, acleval, aci->aci_handle);
		if ( rv < 0) {
			slapi_log_error(SLAPI_LOG_ACL, plugin_name,
				"acl__TestRights:Unable to set the DENY acllist\n");
			continue;
		}
		/* 
		** Now we have all the information we need. We need to call
		** the ONE ACL to test the rights.
		** return value: ACL_RES_DENY, ACL_RES_ALLOW,  error codes 
		*/
		aclpb->aclpb_curr_aci = aci;
		rights_rv = ACL_EvalTestRights (NULL, acleval, testRights, 
						map_generic, &deny, 
						&deny_generic,
						&acl_tag, &expr_num);

		slapi_log_error( SLAPI_LOG_ACL, plugin_name, "Processed:%d DENY handles Result:%d\n",index, rights_rv);

		if (rights_rv   == ACL_RES_FAIL) {
				result_reason->deciding_aci = aci;
				result_reason->reason = ACL_REASON_NONE;
				TNF_PROBE_1_DEBUG(acl__TestRights_end,"ACL","",
								tnf_string,evaled_deny,"");
				return ACL_RES_DENY;
		}

		/* have we executed an ATTR RULE */
		if ( aci->aci_ruleType & ACI_ATTR_RULES )
			aclpb->aclpb_state |= ACLPB_ATTR_RULE_EVALUATED;

		if (access & (SLAPI_ACL_SEARCH | SLAPI_ACL_READ)) {

			for (j=0; j <aclpb->aclpb_last_cache_result; ++j) {
				if (index == aclpb->aclpb_cache_result[j].aci_index) {
					break;
				}
			}

			if ( j < aclpb->aclpb_last_cache_result)  {
				/* already in cache */
			} else if ( j < ACLPB_MAX_CACHE_RESULTS ) {
				/* j == aclpb->aclpb_last_cache_result  &&
					j < ACLPB_MAX_CACHE_RESULTS */
				aclpb->aclpb_last_cache_result++;
				aclpb->aclpb_cache_result[j].aci_index = index;
				aclpb->aclpb_cache_result[j].aci_ruleType = aci->aci_ruleType; 

			} else {  /* cache overflow */
				if (  rights_rv == ACL_RES_DENY) {
					result_reason->deciding_aci = aci;
					result_reason->reason = ACL_REASON_EVALUATED_DENY;
					TNF_PROBE_1_DEBUG(acl__TestRights_end,"ACL","",
								tnf_string,evaled_deny,"");
					return ACL_RES_DENY;
				} else {
					continue;
				}
			}
		
			__acl_set_aclIndex_inResult ( aclpb, access, index );
			if (rights_rv == ACL_RES_DENY) {
				if (access & SLAPI_ACL_SEARCH)  {
					aclpb->aclpb_cache_result[j].result |= 
							ACLPB_CACHE_SEARCH_RES_DENY;
				} else  {		/* MUST BE READ */
					aclpb->aclpb_cache_result[j].result |= 
							ACLPB_CACHE_READ_RES_DENY;
				}
				/* We are done  -- return */
				result_reason->deciding_aci = aci;
				result_reason->reason = ACL_REASON_EVALUATED_DENY;
				TNF_PROBE_1_DEBUG(acl__TestRights_end,"ACL","",
								tnf_string,evaled_deny,"");
				return ACL_RES_DENY;
			} else if (rights_rv == ACL_RES_ALLOW) {
				/* This will happen, of we have an acl with both deny and allow 
				** Since we may not have finished all the deny acl, go thru all
				** of them. We will use this cached result when we evaluate this
				** handle in the context of allow handles.
				*/
				if (access & SLAPI_ACL_SEARCH)  {
					aclpb->aclpb_cache_result[j].result |= 
							ACLPB_CACHE_SEARCH_RES_ALLOW;
				} else  {
					aclpb->aclpb_cache_result[j].result |= 
							ACLPB_CACHE_READ_RES_ALLOW;
				}

			} else {
				if (access & SLAPI_ACL_SEARCH)  {
					aclpb->aclpb_cache_result[j].result |= 
							ACLPB_CACHE_SEARCH_RES_SKIP;
				} else  {
					aclpb->aclpb_cache_result[j].result |= 
							ACLPB_CACHE_READ_RES_SKIP;
				}
				continue;
			}
		} else {
			if ( rights_rv == ACL_RES_DENY ) {
				result_reason->deciding_aci = aci;
				result_reason->reason = ACL_REASON_EVALUATED_DENY;
				TNF_PROBE_1_DEBUG(acl__TestRights_end,"ACL","",
								tnf_string,evaled_deny,"");
				return ACL_RES_DENY;
			}
		}
	}


	/*
	** START PROCESSING ALLOW HANDLES.
	** Process each handle at a time. Do not concatenate the handles or else
	** all the context information will be build again and we will pay a
	** lot of penalty. The context is built the first time the handle is
	** processed.
	**
	** First we set the default to INVALID so that if rules are not matched, then
	** we get INVALID and if a rule matched, the we get ALLOW.
	*/ 
	aclpb->aclpb_state &= ~ACLPB_EXECUTING_DENY_HANDLES;
	aclpb->aclpb_state |= ACLPB_EXECUTING_ALLOW_HANDLES;
	ACL_SetDefaultResult (NULL, acleval, ACL_RES_INVALID);
	numHandles = ACI_MAX_ELEVEL + aclpb->aclpb_num_allow_handles;
	for (i=0, k=0; i < numHandles && k < aclpb->aclpb_num_allow_handles ; ++i) {
		int	skip_eval = 0;
		/* 
		** If the handle has been evaluated before, we can
		** cache the result.
		*/
		aci = aclpb->aclpb_allow_handles[i];
		if (((aci = aclpb->aclpb_allow_handles[i]) == NULL) && (i <= ACI_MAX_ELEVEL))
			continue;
		k++;
		index = aci->aci_index;
		slapi_log_error(SLAPI_LOG_ACL, plugin_name,
			"%d. Evaluating ALLOW aci(%d) \"%s\"\n", k, index, aci->aclName);

		if (access  & ( SLAPI_ACL_SEARCH | SLAPI_ACL_READ)) {

			/*
			 * aclpb->aclpb_cache_result[0..aclpb->aclpb_last_cache_result] is
			 * a cache of info about whether applicable acis
			 * allowed, did_not_allow or denied access
			*/			

			for (j =0; j < aclpb->aclpb_last_cache_result; j++) {
				if (index == aclpb->aclpb_cache_result[j].aci_index) {
					short  result;
					result = aclpb->aclpb_cache_result[j].result;
					if ( result <= 0) break;

					if (!ACL_CACHED_RESULT_VALID(result)) {
						/* something is wrong. Need to evaluate */
						aclpb->aclpb_cache_result[j].result = -1;
						break;
					}

					/*
					** We have a valid cached result. Let's see if we 
					** have what we need.
					*/
					if (access & SLAPI_ACL_SEARCH) {
						if (result & ACLPB_CACHE_SEARCH_RES_ALLOW) {
							slapi_log_error(SLAPI_LOG_ACL, plugin_name, 
							   "Found SEARCH ALLOW in cache\n");
							__acl_set_aclIndex_inResult ( aclpb, access, index );
							result_reason->deciding_aci = aci;
							result_reason->reason = ACL_REASON_RESULT_CACHED_ALLOW;
							TNF_PROBE_1_DEBUG(acl__TestRights_end,"ACL","",
								tnf_string,cached_allow,"");
							return ACL_RES_ALLOW;
						} else if ( result & ACLPB_CACHE_SEARCH_RES_SKIP) {
							slapi_log_error(SLAPI_LOG_ACL, plugin_name, 
							   "Found SEARCH SKIP in cache\n");
							skip_eval = 1;
							break;
						} else {
							/* need to evaluate */
							break;
						}
					} else {
						if ( result & ACLPB_CACHE_READ_RES_ALLOW) {
							slapi_log_error(SLAPI_LOG_ACL, plugin_name, 
							   "Found READ ALLOW in cache\n");
							__acl_set_aclIndex_inResult ( aclpb, access, index );
							result_reason->deciding_aci = aci;
							result_reason->reason = ACL_REASON_RESULT_CACHED_ALLOW;
							TNF_PROBE_1_DEBUG(acl__TestRights_end,"ACL","",
								tnf_string,cached_allow,"");
							return ACL_RES_ALLOW;
						} else if ( result & ACLPB_CACHE_READ_RES_SKIP) {
							slapi_log_error(SLAPI_LOG_ACL, plugin_name, 
							   "Found READ SKIP in cache\n");
							skip_eval = 1;
							break;
						} else {
							break;
						}
					}
				}
			}
		}
		if ( skip_eval) {
			skip_eval = 0;
			continue;
		}

		TNF_PROBE_0_DEBUG(acl__libaccess_start,"ACL","");
		rv = ACL_EvalSetACL(NULL, acleval, aci->aci_handle);
		if ( rv < 0) {
			slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
				"acl__TestRights:Unable to set the acllist\n");
			continue;
		}
		/* 
		** Now we have all the information we need. We need to call
		** the ONE ACL to test the rights.
		** return value: ACL_RES_DENY, ACL_RES_ALLOW,  error codes 
		*/
		aclpb->aclpb_curr_aci = aci;
		rights_rv = ACL_EvalTestRights (NULL, acleval, testRights, 
						map_generic, &deny, 
						&deny_generic,
						&acl_tag, &expr_num);
		TNF_PROBE_0_DEBUG(acl__libaccess_end,"ACL","");

		if (aci->aci_ruleType & ACI_ATTR_RULES)
			aclpb->aclpb_state |= ACLPB_ATTR_RULE_EVALUATED;

		if (access & (SLAPI_ACL_SEARCH | SLAPI_ACL_READ))  {

			for (j=0; j <aclpb->aclpb_last_cache_result; ++j) {
				if (index == aclpb->aclpb_cache_result[j].aci_index) {
					break;
				}
			}

			if ( j < aclpb->aclpb_last_cache_result)  {
				/* already in cache */
			} else if ( j < ACLPB_MAX_CACHE_RESULTS ) {
				/* j == aclpb->aclpb_last_cache_result  &&
					j < ACLPB_MAX_CACHE_RESULTS */
				aclpb->aclpb_last_cache_result++;
				aclpb->aclpb_cache_result[j].aci_index = index;
				aclpb->aclpb_cache_result[j].aci_ruleType = aci->aci_ruleType;
			} else {  /* cache overflow */
				slapi_log_error (SLAPI_LOG_FATAL, "acl__TestRights", "cache overflown\n");
				if (  rights_rv == ACL_RES_ALLOW) {
					result_reason->deciding_aci = aci;
					result_reason->reason = ACL_REASON_EVALUATED_ALLOW;
					TNF_PROBE_1_DEBUG(acl__TestRights_end,"ACL","",
								tnf_string,evaled_allow,"");
					return ACL_RES_ALLOW;
				} else {
					continue;
				}
			}

			__acl_set_aclIndex_inResult ( aclpb, access, index );
			if (rights_rv == ACL_RES_ALLOW) {
				if (access & SLAPI_ACL_SEARCH)  {
					aclpb->aclpb_cache_result[j].result |= 
							ACLPB_CACHE_SEARCH_RES_ALLOW;
				} else  {		/* must be READ */
					aclpb->aclpb_cache_result[j].result |= 
							ACLPB_CACHE_READ_RES_ALLOW;
				}
				
				/* We are done  -- return */
				result_reason->deciding_aci = aci;
				result_reason->reason = ACL_REASON_EVALUATED_ALLOW;
				TNF_PROBE_1_DEBUG(acl__TestRights_end,"ACL","",
								tnf_string,evaled_allow,"");
				return ACL_RES_ALLOW;

			} else {
				if (access & SLAPI_ACL_SEARCH)  {
					aclpb->aclpb_cache_result[j].result |= 
							ACLPB_CACHE_SEARCH_RES_SKIP;
				} else  {
					aclpb->aclpb_cache_result[j].result |= 
							ACLPB_CACHE_READ_RES_SKIP;
				}
				continue;
			}
		} else {
			if ( rights_rv == ACL_RES_ALLOW ) {
				result_reason->deciding_aci = aci;
				result_reason->reason = ACL_REASON_EVALUATED_ALLOW;
				TNF_PROBE_1_DEBUG(acl__TestRights_end,"ACL","",
								tnf_string,evaled_allow,"");
				return ACL_RES_ALLOW;
			}
		}
	}/* for */
	result_reason->deciding_aci = aci;
	result_reason->reason = ACL_REASON_NO_MATCHED_SUBJECT_ALLOWS;

	TNF_PROBE_0_DEBUG(acl__TestRights_end,"ACL","");	

	return (ACL_RES_DENY);
}
/***************************************************************************
*
* acl_match_substring
*
*	Compare the input string to the patteren in the filter
*
* Input:
*	struct slapi_filter	*f	- Filter which has the patteren
*	char		*str		- String to compare
*	int		exact_match	- 1; match the pattern exactly
*					- 0; match the pattern as a suffix
*
* Returns:
*	ACL_TRUE		- The sting matches with the patteren
*	ACL_FALSE		- No it doesn't
*	ACL_ERR			- Error
*
* Error Handling:
*	None.
*
**************************************************************************/
int
acl_match_substring ( Slapi_Filter *f, char *str, int exact_match)
{
	int 		i, rc, len;
	char 		*p = NULL;
	char		*end, *realval, *tmp;
	char 		pat[BUFSIZ];
	char 		buf[BUFSIZ];
	char		*type, *initial, *final;
	char		**any;
	Slapi_Regex	*re = NULL;
	const char  *re_result = NULL;

	if ( 0 != slapi_filter_get_subfilt ( f, &type, &initial, &any, &final ) ) {
		return (ACL_FALSE);
	}

	/* convert the input to lower. */
	for (p = str; *p; p++)
		*p = TOLOWER ( *p );

	/* construct a regular expression corresponding to the filter: */
	pat[0] = '\0';
	p = pat;
	end = pat + sizeof(pat) - 2; /* leave room for null */


	if ( initial != NULL) {
		strcpy (p, "^");
		p = strchr (p, '\0');

		/* 2 * in case every char is special */
		if (p + 2 * strlen ( initial ) > end) {
			slapi_log_error (SLAPI_LOG_ACL, plugin_name, 
				"not enough pattern space\n");

			return (ACL_ERR);
		}

		if (!exact_match) {
			strcpy (p, ".*");
			p = strchr (p, '\0');
		}
		acl_strcpy_special (p, initial);
		p = strchr (p, '\0');
	}

	if ( any != NULL) {
		for (i = 0;  any && any[i] != NULL; i++) {
			/* ".*" + value */
			if (p + 2 * strlen ( any[i]) + 2 > end) {
				slapi_log_error (SLAPI_LOG_ACL, plugin_name,
					"not enough pattern space\n");
				return (ACL_ERR);
			}

			strcpy (p, ".*");
			p = strchr (p, '\0');
			acl_strcpy_special (p, any[i]);
			p = strchr (p, '\0');
		}
	}


	if ( final != NULL) {
		/* ".*" + value */
		if (p + 2 * strlen ( final ) + 2 > end) {
			slapi_log_error (SLAPI_LOG_ACL, plugin_name, 
				"not enough pattern space\n");
			return (ACL_ERR);
		}
	
		strcpy (p, ".*");
		p = strchr (p, '\0');
		acl_strcpy_special (p, final);
		p = strchr (p, '\0');
		strcpy (p, "$");
	}

	/* see if regex matches with the input string */
	tmp = NULL;
	len = strlen(str);

	if (len < sizeof(buf)) {
		strcpy (buf, str);
		realval = buf;
	} else {
		tmp = (char*) slapi_ch_malloc (len + 1);
		strcpy (tmp, str);
		realval = tmp;
	}

	/* What we have built is a regular pattaren expression.
	** Now we will compile the pattern and compare wth the string to
	** see if the input string matches with the patteren or not.
	*/
	re = slapi_re_comp( pat, &re_result );
	if (NULL == re) {
		slapi_log_error (SLAPI_LOG_ACL, plugin_name, 
			"acl_match_substring:re_comp failed (%s)\n", re_result?re_result:"unknown");
		return (ACL_ERR);
	}

	/* slapi_re_exec() returns 1 if the string p1 matches the last compiled
	** regular expression, 0 if the string p1 failed to match 
	*/
	rc = slapi_re_exec( re, realval, -1 /* no timelimit */ );

	slapi_re_free(re);
	slapi_ch_free ( (void **) &tmp );

	if (rc == 1) {
		return ACL_TRUE;
	} else {
		return ACL_FALSE;
	}
}
/***************************************************************************
*
* acl__reset_cached_result
*
* Go thru the cached result and invlalidate the cached evaluation results for
* rules which can only be cached based on the scope. 
* If we have scope ACI_CACHE_RESULT_PER_ENTRY, then we need to invalidate the
* cacched reult whenever we hit a new entry.
*
* Returns: 
* 	Null
*
**************************************************************************/
static void 
acl__reset_cached_result (struct acl_pblock *aclpb )
{

	int		j;

	for (j =0; j < aclpb->aclpb_last_cache_result; j++) {
		/* Unless we have to clear the result, move on */
		if (!( aclpb->aclpb_cache_result[j].aci_ruleType & ACI_CACHE_RESULT_PER_ENTRY))
			continue;
		aclpb->aclpb_cache_result[j].result = 0;
	}
}
/*
 * acl_access_allowed_disjoint_resource
 * 
 *	This is an internal module which can be used to verify
 *	access to a resource which may not be inside the search scope.
 *
 * Returns:
 *	- Same return val as acl_access_allowed().
 */
int
acl_access_allowed_disjoint_resource(
        Slapi_PBlock        *pb,
        Slapi_Entry         *e, 		/* The Slapi_Entry */
        char                *attr,		/* Attribute of the entry */
        struct berval       *val,		/* value of attr. NOT USED */
        int                 access		/* access rights */
	)
{

	int		   rv;
	struct acl_pblock  *aclpb = NULL;

	aclpb = acl_get_aclpb ( pb, ACLPB_BINDDN_PBLOCK );
	/*
	** It's possible that we already have a context of ACLs. 
	** However once in a while, we need to test 
	** access to a resource which (like vlv, schema) which falls 
	** outside the search base. In that case, we need to  go 
	** thru all the ACLs and not depend upon the acls which we have
	** gathered.
	*/

	/* If you have the right to use the resource, then we don't need to check for
	** proxy right on that resource.
	*/
	if (aclpb) 
		aclpb->aclpb_state |= ( ACLPB_DONOT_USE_CONTEXT_ACLS| ACLPB_DONOT_EVALUATE_PROXY );

	rv = acl_access_allowed(pb, e, attr, val, access);

	if (aclpb) aclpb->aclpb_state &= ~ACLPB_DONOT_USE_CONTEXT_ACLS;
	if (aclpb ) aclpb->aclpb_state &= ~ACLPB_DONOT_EVALUATE_PROXY;

	return rv;
}

/*
 * acl__attr_cached_result
 *    Loops thru the cached result and determines if we can use the cached value.
 *
 * 	Inputs:
 *		Slapi_pblock	*aclpb		- acl private block
 *		char		*attr			- attribute name
 *		int		access				- access type
 * 	Returns:
 *		LDAP_SUCCESS: 				- access is granted
 *		LDAP_INSUFFICIENT_ACCESS	- access denied
 *		ACL_ERR						- no cached info about this attr.
 *									- or the attr had multiple result and so
 *									- we can't determine the outcome.
 *
 */
static int
acl__attr_cached_result (struct acl_pblock *aclpb, char *attr, int access )
{

	int					i, rc;
	aclEvalContext		*c_evalContext;

	if ( !(access & ( SLAPI_ACL_SEARCH | SLAPI_ACL_READ) ))
		return ACL_ERR;

	if (aclpb->aclpb_state & ACLPB_HAS_ACLCB_EVALCONTEXT ) {
		c_evalContext = &aclpb->aclpb_prev_opEval_context;
		slapi_log_error ( SLAPI_LOG_ACL, plugin_name, 
			    "acl__attr_cached_result:Using Context: ACLPB_ACLCB\n" );
	} else {
		c_evalContext = &aclpb->aclpb_prev_entryEval_context;
		slapi_log_error ( SLAPI_LOG_ACL, plugin_name, 
			    "acl__attr_cached_result:Using Context: ACLPB_PREV\n" );
	}

	if ( attr == NULL ) {
		int		eval_read = 0;
		/*
		**  Do I have access to at least one attribute, then I have 
		** access to the  entry.
		*/
		for (i=0; i < c_evalContext->acle_numof_attrs; i++ ) {
			AclAttrEval     *a_eval = &c_evalContext->acle_attrEval[i];

			if (  (access & SLAPI_ACL_READ ) && a_eval->attrEval_r_status  && 
				a_eval->attrEval_r_status < ACL_ATTREVAL_DETERMINISTIC ) {
				eval_read++;
				if ( a_eval->attrEval_r_status & ACL_ATTREVAL_SUCCESS) 
						return LDAP_SUCCESS;
				/* rbyrne: recompute if we have to.
				 * How does this cached result get turned off for
				 * attr style acis which acannot be cached becuase entry
				 * can result in a diff value.
				*/				
				else if ( a_eval->attrEval_r_status & ACL_ATTREVAL_RECOMPUTE ) {
					rc = acl__recompute_acl ( aclpb, a_eval, access,
											a_eval->attrEval_r_aciIndex);
					if ( rc != ACL_ERR ) {
						acl_copyEval_context ( aclpb, c_evalContext,
										&aclpb->aclpb_curr_entryEval_context, 1);
					}
					if ( rc == LDAP_SUCCESS) {
						return LDAP_SUCCESS;
					}
				}
			}
		}/* for */
		/*
		 * If we have scanned the whole list without success then
		 *  we are not granting access to this entry through access
		 * to an attribute in the list--however this does not mean
		 * that we do not have access to the entry via another attribute
		 * not already in the list, so return -1 meaning
		 * "don't know".
		*/
		return(ACL_ERR);
#if 0
		if ( eval_read )
			return LDAP_INSUFFICIENT_ACCESS;
		else
			return ACL_ERR;
#endif
	}

	for (i=0; i < c_evalContext->acle_numof_attrs; i++ ) {
		AclAttrEval     *a_eval = &c_evalContext->acle_attrEval[i];

		if ( a_eval == NULL ) continue;

		if (strcasecmp ( attr, a_eval->attrEval_name ) == 0 ) {
			if ( access & SLAPI_ACL_SEARCH ) {
				if (a_eval->attrEval_s_status < ACL_ATTREVAL_DETERMINISTIC ) {
					if ( a_eval->attrEval_s_status & ACL_ATTREVAL_SUCCESS)
						return LDAP_SUCCESS;
					else if ( a_eval->attrEval_s_status & ACL_ATTREVAL_FAIL)
						return LDAP_INSUFFICIENT_ACCESS;
					else if ( a_eval->attrEval_s_status & ACL_ATTREVAL_RECOMPUTE ) {
							rc = acl__recompute_acl ( aclpb, a_eval, access,
												a_eval->attrEval_s_aciIndex);
							if ( rc != ACL_ERR ) {
								acl_copyEval_context ( aclpb, c_evalContext,
										&aclpb->aclpb_curr_entryEval_context, 1);
							}
					} else
						return ACL_ERR;
				} else {
					/*  This means that for the same attribute and same type of
					** access, we had different results at different time.
					** Since we are not caching per object, we can't
					** determine exactly. So, can't touch this
					*/
					return ACL_ERR;
				}
			} else {
				if (a_eval->attrEval_r_status < ACL_ATTREVAL_DETERMINISTIC ) {
					if ( a_eval->attrEval_r_status & ACL_ATTREVAL_SUCCESS)
						return LDAP_SUCCESS;
					else if ( a_eval->attrEval_r_status & ACL_ATTREVAL_FAIL)
						return LDAP_INSUFFICIENT_ACCESS;
					else if ( a_eval->attrEval_r_status & ACL_ATTREVAL_RECOMPUTE ) {
							rc = acl__recompute_acl ( aclpb, a_eval, access,
											a_eval->attrEval_r_aciIndex);
							if ( rc != ACL_ERR ) {
								acl_copyEval_context ( aclpb, c_evalContext,
										&aclpb->aclpb_curr_entryEval_context, 1);
							}
					} else
						return ACL_ERR;
				} else {
					/* Look above for explanation */
					return ACL_ERR;
				}
			}
		}
	}
	return ACL_ERR;
}

/*
 * Had to do this juggling of casting to make 
 * both Nt & unix compiler happy.
 */
static  int 
acl__cmp(const void *a, const void *b)
{
	short *i = (short *) a;
	short *j = (short *) b;

	if ( (short) *i > (short) *j )
		return (1);
	if ( (short)*i < (short) *j)
		return (-1);
	return (0);
}

/*
 * acl__scan_match_handles
 *	Go thru the ACL list and determine if the list of acls selected matches
 *	what we have in the cache.
 *
 *	Inputs:
 *		Acl_PBlock *pb		- Main pblock ( blacvk hole)
 *		int	     type		- Which context to look on
 *
 *	Returns:
 *		0				- matches all the acl handles
 *		ACL_ERR			- sorry; no match
 *
 * 	ASSUMPTION: A READER LOCK ON ACL LIST
 */
static int
acl__scan_match_handles ( Acl_PBlock *aclpb, int type)
{


	int				matched = 0;
	aci_t			*aci = NULL;
	int				index;
	PRUint32		cookie;
	aclEvalContext	*c_evalContext = NULL;

	if (type  == ACLPB_EVALCONTEXT_PREV ) {
		c_evalContext = &aclpb->aclpb_prev_entryEval_context;
	} else if ( type == ACLPB_EVALCONTEXT_ACLCB ){
		c_evalContext = &aclpb->aclpb_prev_opEval_context;
	} else {
		return ACL_ERR;
	}
		

	if ( !c_evalContext->acle_numof_tmatched_handles )
		return ACL_ERR;

	aclpb->aclpb_stat_acllist_scanned++;
	aci = acllist_get_first_aci ( aclpb, &cookie );

	while ( aci ) {
		index = aci->aci_index;
		if (acl__resource_match_aci(aclpb, aci, 1 /* skip attr matching */, NULL )) {
			int	j;
			int	s_matched = matched;

			/* We have a sorted list of handles that matched the target */
			
			for (j=0; j < c_evalContext->acle_numof_tmatched_handles ; j++ ) {
				if ( c_evalContext->acle_handles_matched_target[j]  > index )
					
					break;
				else if ( index == c_evalContext->acle_handles_matched_target[j] ) {
					int		jj;
					matched++;

					/* See if this is a ATTR rule that matched -- in that case we have
					** to nullify the cached result
					 */
					if ( aci->aci_ruleType & ACI_ATTR_RULES ) {
						slapi_log_error (  SLAPI_LOG_ACL, plugin_name, 
							"Found an attr Rule [Name:%s Index:%d\n", aci->aclName, 
							aci->aci_index );
						for ( jj =0; jj < c_evalContext->acle_numof_attrs; jj++ ) {
							AclAttrEval     *a_eval = &c_evalContext->acle_attrEval[jj];
							if ( a_eval->attrEval_r_aciIndex == aci->aci_index )
								a_eval->attrEval_r_status = ACL_ATTREVAL_RECOMPUTE;
							if ( a_eval->attrEval_s_aciIndex == aci->aci_index )
								a_eval->attrEval_s_status = ACL_ATTREVAL_RECOMPUTE;
						}
					}
					break;
				}
			}
			if ( s_matched == matched ) return ACL_ERR;
		}
		aci = acllist_get_next_aci ( aclpb, aci, &cookie );
	}
	if ( matched == c_evalContext->acle_numof_tmatched_handles )
		return 0;

	return ACL_ERR;
}
/*
 * acl_copyEval_context
 *	Copy the context info which include attr info and handles.
 *
 *	Inputs
 *		struct acl_pblock *aclpb	- acl private main block
 *		aclEvalContext	*src		- src context
 *		aclEvalContext	*dest		- dest context
 *	Returns:
 *		None.
 *
 */
void
acl_copyEval_context ( struct acl_pblock *aclpb, aclEvalContext *src, 
			aclEvalContext *dest , int copy_attr_only )
{

	int		d_slot, i;

	/* Do a CLEAN copy we have nothing or else do an incremental copy.*/
	if ( src->acle_numof_attrs < 1 )
		return;

	/* Copy the attr info */
	if ( dest->acle_numof_attrs < 1 )
		acl_clean_aclEval_context (  dest, 0 /*clean */ );

	d_slot = dest->acle_numof_attrs;
	for (i=0; i < src->acle_numof_attrs; i++ ) {
		int	j;
		int	attr_exists = 0;
		int	dd_slot = d_slot;

		if ( aclpb && (i == 0) ) aclpb->aclpb_stat_num_copycontext++;

		if (  src->acle_attrEval[i].attrEval_r_status == 0 &&
			src->acle_attrEval[i].attrEval_s_status == 0  )
			continue;

		for ( j = 0; j < dest->acle_numof_attrs; j++ ) {
			if ( strcasecmp ( src->acle_attrEval[i].attrEval_name,
					  dest->acle_attrEval[j].attrEval_name ) == 0 ) {
				/* We have it. skip it. */
				attr_exists = 1;
				dd_slot = j;
				break;
			}
		}
		if ( !attr_exists ) {
			if ( dd_slot >= ACLPB_MAX_ATTRS -1 )
				break;

			if ( aclpb) aclpb->aclpb_stat_num_copy_attrs++;

			if ( dest->acle_attrEval[dd_slot].attrEval_name )
				slapi_ch_free ( (void **) &dest->acle_attrEval[dd_slot].attrEval_name );

			dest->acle_attrEval[dd_slot].attrEval_name  = 
				slapi_ch_strdup ( src->acle_attrEval[i].attrEval_name );
		}
		/* Copy the result status and the aci index */
		dest->acle_attrEval[dd_slot].attrEval_r_status = 
				src->acle_attrEval[i].attrEval_r_status;
		dest->acle_attrEval[dd_slot].attrEval_r_aciIndex = 
				src->acle_attrEval[i].attrEval_r_aciIndex;
		dest->acle_attrEval[dd_slot].attrEval_s_status = 
				src->acle_attrEval[i].attrEval_s_status;
		dest->acle_attrEval[dd_slot].attrEval_s_aciIndex = 
				src->acle_attrEval[i].attrEval_s_aciIndex;

		if (!attr_exists ) d_slot++;
	}

	dest->acle_numof_attrs = d_slot;
	dest->acle_attrEval[d_slot].attrEval_name  =  NULL;

	if ( copy_attr_only )
		return;

	/* First sort the arrays which keeps the acl index numbers */
	qsort ( (char *) src->acle_handles_matched_target, 
			(size_t)src->acle_numof_tmatched_handles, sizeof( int ), acl__cmp );

	for (i=0; i < src->acle_numof_tmatched_handles; i++ )  {
		dest->acle_handles_matched_target[i]  =
				src->acle_handles_matched_target[i];
	}

	if ( src->acle_numof_tmatched_handles ) {
		dest->acle_numof_tmatched_handles = src->acle_numof_tmatched_handles;
		if ( aclpb) aclpb->aclpb_stat_num_tmatched_acls = src->acle_numof_tmatched_handles;
	}
}

/*
 * acl_clean_aclEval_context
 *	Clean the eval context
 *
 *	Inputs:
 *		aclEvalContext *clean_me	- COntext to be cleaned
 *		int		clean_type	-  0: clean, 1 scrub
 *
 */

void
acl_clean_aclEval_context ( aclEvalContext *clean_me, int scrub_only )
{
	int		i;

	/* Copy the attr info */
	for (i=0; i < clean_me->acle_numof_attrs; i++ ) {

		char	*a_name = clean_me->acle_attrEval[i].attrEval_name;
		if ( a_name  && !scrub_only) {
			slapi_ch_free ( (void **)  &a_name );
			clean_me->acle_attrEval[i].attrEval_name = NULL;
		}
		clean_me->acle_attrEval[i].attrEval_r_status = 0;
		clean_me->acle_attrEval[i].attrEval_s_status = 0;
		clean_me->acle_attrEval[i].attrEval_r_aciIndex = 0;
		clean_me->acle_attrEval[i].attrEval_s_aciIndex = 0;
	}

	if ( !scrub_only ) clean_me->acle_numof_attrs = 0;
	clean_me->acle_numof_tmatched_handles = 0;
}
/*
 * acl__match_handlesFromCache
 *
 *	We have 2 cacheed information
 *	1) cached info from the previous operation
 *	2) cached info from the prev entry evaluation
 *
 *	What we are doing here is going thru all the acls and see if the same
 *	set of acls apply to this resource or not. If it does, then do we have
 * 	a cached info for the attr. If we don't for both of the cases then we need 
 *	to evaluate all over again.
 *
 *	Inputs:
 *		struct	acl_pblock	 - ACL private block;
 *		char *attr		 - Attribute name
 *		int	access		 - acces type
 *
 *	returns:
 *		LDAP_SUCCESS (0)	 - The same acls apply and we have 
 *					   access ALLOWED on the attr
 *		LDAP_INSUFFICIENT_ACCESS - The same acls apply and we have 
 *					   access DENIED on the attr
 *		-1			 - Acls doesn't match or we don't have
 *					   cached info for this attr.
 *
 *	ASSUMPTIONS: A reader lock has been obtained for the acl list.
 */
static int
acl__match_handlesFromCache (  Acl_PBlock *aclpb, char *attr, int access)
{

	aclEvalContext		*c_evalContext = NULL;
	int			context_type = 0;
	int	ret_val = -1;	/* it doen't match by default */

	/* Before we proceed, find out if we have evaluated any ATTR RULE. If we have
	** then we can't use any caching mechanism
	*/

	if ( aclpb->aclpb_state & ACLPB_HAS_ACLCB_EVALCONTEXT ) {
		context_type = ACLPB_EVALCONTEXT_ACLCB;
		c_evalContext = &aclpb->aclpb_prev_opEval_context;
	 } else {
		context_type =  ACLPB_EVALCONTEXT_PREV;
		c_evalContext = &aclpb->aclpb_prev_entryEval_context;
	}


 	if ( aclpb->aclpb_res_type & (ACLPB_NEW_ENTRY | ACLPB_EFFECTIVE_RIGHTS) ) { 
		aclpb->aclpb_state |= ACLPB_MATCHES_ALL_ACLS;
		ret_val =  acl__scan_match_handles ( aclpb, context_type );
		if (  -1 == ret_val ) {
			aclpb->aclpb_state &= ~ACLPB_MATCHES_ALL_ACLS;
			aclpb->aclpb_state |= ACLPB_UPD_ACLCB_CACHE;
			/* Did not match */
			if ( context_type == ACLPB_HAS_ACLCB_EVALCONTEXT ) {
				aclpb->aclpb_state &= ~ACLPB_HAS_ACLCB_EVALCONTEXT;
			} else {
				aclpb->aclpb_state |= ACLPB_COPY_EVALCONTEXT;
				c_evalContext->acle_numof_tmatched_handles = 0;
			}
		}
	}
	if ( aclpb->aclpb_state & ACLPB_MATCHES_ALL_ACLS ) {
		/* See if we have a cached result for this attr */
		ret_val = acl__attr_cached_result (aclpb, attr, access);

		/* It's not in the ACLCB context but we might have it in the 
		** current/prev context. Take a look at it. we might have evaluated
		** this attribute already.
		*/
		if ( (-1 == ret_val ) &&
			( aclpb->aclpb_state & ACLPB_HAS_ACLCB_EVALCONTEXT )) {
			aclpb->aclpb_state &= ~ACLPB_HAS_ACLCB_EVALCONTEXT ;
			ret_val = acl__attr_cached_result (aclpb, attr, access);
			aclpb->aclpb_state |= ACLPB_HAS_ACLCB_EVALCONTEXT ;

			/* We need to do an incremental update */
			if  ( !ret_val ) aclpb->aclpb_state |= ACLPB_INCR_ACLCB_CACHE;
		}
	}
	return ret_val;
}
/*
 * acl__get_attrEval
 * 	Get the atteval from the current context and hold the ptr in aclpb.
 * 	If we have too many attrs, then allocate a new one. In that case
 *	we let the caller know about that so that it will be deallocated.
 *
 * 	Returns:
 *		int 		- 0: The context was indexed. So, no allocations.
 *				- 1; context was allocated - deallocate it.
 */
static int
acl__get_attrEval ( struct acl_pblock *aclpb, char *attr )
{

	int					j;
	aclEvalContext		*c_ContextEval = &aclpb->aclpb_curr_entryEval_context;
	int					deallocate_attrEval = 0;
	AclAttrEval			*c_attrEval = NULL;

	if ( !attr ) return deallocate_attrEval;

	aclpb->aclpb_curr_attrEval = NULL;

	/* Go thru and see if we have the attr already */
	for (j=0; j < c_ContextEval->acle_numof_attrs; j++) {
		c_attrEval = &c_ContextEval->acle_attrEval[j];
			
		if ( c_attrEval && 
			slapi_attr_type_cmp ( c_attrEval->attrEval_name, attr, 1) == 0 ) {
			aclpb->aclpb_curr_attrEval = c_attrEval;
			break;
		}
	}

	if ( !aclpb->aclpb_curr_attrEval) {
		if ( c_ContextEval->acle_numof_attrs == ACLPB_MAX_ATTRS -1 ) {
			/* Too many attrs. create a temp one  */
			c_attrEval =  (AclAttrEval * ) slapi_ch_calloc ( 1, sizeof ( AclAttrEval ) );
			deallocate_attrEval =1;
		} else {
			c_attrEval = &c_ContextEval->acle_attrEval[c_ContextEval->acle_numof_attrs++];
			c_attrEval->attrEval_r_status = 0;
			c_attrEval->attrEval_s_status = 0;
			c_attrEval->attrEval_r_aciIndex = 0;
			c_attrEval->attrEval_s_aciIndex = 0;
		}
		/* clean it before use */
		c_attrEval->attrEval_name = slapi_ch_strdup ( attr );
		aclpb->aclpb_curr_attrEval = c_attrEval;
	}
	return deallocate_attrEval;
}
/*
 * acl_skip_access_check
 *
 * 	See if we need to go thru the ACL check or not. We don't need to if I am root
 * 	or internal operation or ...
 *
 * returns:
 *	ACL_TRUE		- Yes; skip the ACL check
 *	ACL_FALSE		- No; you have to go thru ACL check
 * 
 */
int
acl_skip_access_check ( Slapi_PBlock *pb,  Slapi_Entry *e )
{
	int				rv, isRoot, accessCheckDisabled;
	void			*conn = NULL;
	Slapi_Backend	*be;   

	slapi_pblock_get ( pb, SLAPI_REQUESTOR_ISROOT, &isRoot );
	if ( isRoot ) return ACL_TRUE;

	/* See  if this is local request */
	slapi_pblock_get ( pb, SLAPI_CONNECTION, &conn);

	if ( NULL == conn ) return  ACL_TRUE;

	/*
	 * Turn on access checking in the rootdse--this code used
	 * to skip the access check.
	 * 
	 *  check if the entry is the RootDSE entry
	if ( e   ) {
		char	* edn = slapi_entry_get_ndn ( e );
		if  ( slapi_is_rootdse ( edn ) ) return ACL_TRUE;
	}
	*/

	/* GB : when referrals are directly set in the mappin tree
	 * we can reach this code without a backend in the pblock
	 * in such a case, allow access for now	
	 * we may want to reconsider this is NULL DSE implementation happens
	 */
	rv = slapi_pblock_get ( pb, SLAPI_BACKEND, &be );
	if (be == NULL)
		return ACL_TRUE;
			
	rv = slapi_pblock_get ( pb, SLAPI_PLUGIN_DB_NO_ACL, &accessCheckDisabled );
    if ( rv != -1 && accessCheckDisabled ) return ACL_TRUE;

	return ACL_FALSE;
}
short
acl_get_aclsignature ()
{
	return acl_signature;
}
void
acl_set_aclsignature ( short value)
{
	acl_signature = value;
}
void
acl_regen_aclsignature ()
{
	acl_signature = aclutil_gen_signature ( acl_signature );
}
	

/*
*
* Assumptions:
*	1) Called for read/search right.
*/
static int
acl__recompute_acl (  	Acl_PBlock 		*aclpb,  
						AclAttrEval		*a_eval,
						int				access,
						int				aciIndex
					)
{


	char		*unused_str1, *unused_str2;
	char		*acl_tag, *testRight[2];
	int			j, expr_num;
	int			result_status, cache_result;
	PRUint32	cookie;
	aci_t		*aci;


	PR_ASSERT ( aciIndex >= 0 );
	PR_ASSERT ( a_eval != NULL );
	PR_ASSERT (aclpb != NULL );


	/* We might have evaluated this acl just now, check it there first */

	for ( j =0; j < aclpb->aclpb_last_cache_result; j++) {
		if (aciIndex == aclpb->aclpb_cache_result[j].aci_index) {
			short  result;
			result_status =ACL_RES_INVALID; 

			result = aclpb->aclpb_cache_result[j].result;
			if ( result <= 0) break;
			if (!ACL_CACHED_RESULT_VALID(result)) {
				/* something is wrong. Need to evaluate */
				aclpb->aclpb_cache_result[j].result = -1;
				break;
			}
			

			/*
			** We have a valid cached result. Let's see if we
			** have what we need.
			*/
			if ((result & ACLPB_CACHE_SEARCH_RES_ALLOW) ||
				(result & ACLPB_CACHE_READ_RES_ALLOW) )
				result_status = ACL_RES_ALLOW;
			else if ((result & ACLPB_CACHE_SEARCH_RES_DENY) ||
				(result & ACLPB_CACHE_READ_RES_DENY) )
				result_status = ACL_RES_DENY;

		}
	} /* end of for */

	if ( result_status != ACL_RES_INVALID ) {
		goto set_result_status;
	}

	slapi_log_error ( SLAPI_LOG_ACL, plugin_name,
			"Recomputing the ACL Index:%d for entry:%s\n",
			aciIndex, slapi_entry_get_ndn ( aclpb->aclpb_curr_entry) );
				
	/* First find this one ACL and then evaluate it. */

	
	aci = acllist_get_first_aci ( aclpb, &cookie );
	while ( aci && aci->aci_index != aciIndex ) {
		aci = acllist_get_next_aci ( aclpb, aci, &cookie );
	}

	if (NULL == aci)
		return -1;


	ACL_SetDefaultResult (NULL, aclpb->aclpb_acleval, ACL_RES_INVALID);
	ACL_EvalSetACL(NULL, aclpb->aclpb_acleval, aci->aci_handle);

	testRight[0] = acl_access2str ( access );
	testRight[1] = '\0';
	aclpb->aclpb_curr_aci = aci;
	result_status = ACL_EvalTestRights (NULL, aclpb->aclpb_acleval, testRight,
							ds_map_generic, &unused_str1,
							&unused_str2,
 							&acl_tag, &expr_num);

	cache_result = 0;
	if ( result_status == ACL_RES_DENY && aci->aci_type & ACI_HAS_DENY_RULE ) {
		if ( access & SLAPI_ACL_SEARCH)
			cache_result = ACLPB_CACHE_SEARCH_RES_DENY;
		else
			cache_result = ACLPB_CACHE_READ_RES_DENY;
	} else if ( result_status == ACL_RES_ALLOW && aci->aci_type & ACI_HAS_ALLOW_RULE ) {
		if ( access & SLAPI_ACL_SEARCH)
			cache_result = ACLPB_CACHE_SEARCH_RES_ALLOW;
		else
			cache_result = ACLPB_CACHE_READ_RES_ALLOW;

	} else {
		result_status = -1;
	}

	/* Now we need to put the cached result in the aclpb */

	for (j=0; j <aclpb->aclpb_last_cache_result; ++j) {
		if (aciIndex == aclpb->aclpb_cache_result[j].aci_index) {
			break;
		}
	}

	if ( j < aclpb->aclpb_last_cache_result)  {
		/* already in cache */
	} else if ( j < ACLPB_MAX_CACHE_RESULTS-1) {
        /* rbyrneXXX: make this same as other last_cache_result code! */
		j =  ++aclpb->aclpb_last_cache_result;
		aclpb->aclpb_cache_result[j].aci_index = aci->aci_index;
		aclpb->aclpb_cache_result[j].aci_ruleType = aci->aci_ruleType; 

	} else {  /*  No more space */
		goto set_result_status;
	}

	/* Add the cached result status */
	aclpb->aclpb_cache_result[j].result |= cache_result;



set_result_status:
	if (result_status == ACL_RES_ALLOW) {
		if (access & SLAPI_ACL_SEARCH)
			/*wrong bit maskes were being used here--
				a_eval->attrEval_s_status = ACLPB_CACHE_SEARCH_RES_ALLOW;*/
			a_eval->attrEval_s_status = ACL_ATTREVAL_SUCCESS;
		else
			a_eval->attrEval_r_status = ACL_ATTREVAL_SUCCESS;

	} else if ( result_status == ACL_RES_DENY) {
		if (access & SLAPI_ACL_SEARCH)
			a_eval->attrEval_s_status = ACL_ATTREVAL_FAIL;
		else
			a_eval->attrEval_r_status = ACL_ATTREVAL_FAIL;
	} else {
		/* Here, set it to recompute--try again later */
		if (access & SLAPI_ACL_SEARCH)
			a_eval->attrEval_s_status = ACL_ATTREVAL_RECOMPUTE;
		else
			a_eval->attrEval_r_status = ACL_ATTREVAL_RECOMPUTE;
		result_status = -1;
	}

	return result_status;
}

static void 
__acl_set_aclIndex_inResult ( Acl_PBlock *aclpb, int access, int index )
{
	AclAttrEval	*c_attrEval = aclpb->aclpb_curr_attrEval;
	
	if ( c_attrEval )  {
		if ( access & SLAPI_ACL_SEARCH )
			c_attrEval->attrEval_s_aciIndex = index;
		else if ( access & SLAPI_ACL_READ )
			c_attrEval->attrEval_r_aciIndex = index;
	}
}

/* 
 * If filter_sense is true then return (entry satisfies f).
 * Otherwise, return !(entry satisfies f)
*/

static int
acl__test_filter ( Slapi_Entry *entry, struct slapi_filter *f, int filter_sense) {
		int 	filter_matched;

		/* slapi_vattr_filter_test() returns 0 for match */

		filter_matched = !slapi_vattr_filter_test(NULL, entry, 
					    					f,
					    					0 /*don't do acess chk*/);
		
		if (filter_sense) {
			return(filter_matched ? ACL_TRUE : ACL_FALSE);
		} else {
			return(filter_matched ? ACL_FALSE: ACL_TRUE);
		}
}  

/*
 * Make an entry consisting of attr_type and attr_val and put
 * a pointer to it in *entry.
 * We will use this afterwards to test for against a filter.
*/

static int
acl__make_filter_test_entry ( Slapi_Entry **entry,  char *attr_type,
							  struct berval *attr_val) {

	struct berval *vals_array[2];
    
    vals_array[0] = attr_val;
    vals_array[1] = NULL;

	*entry = slapi_entry_alloc();
	slapi_entry_init(*entry, NULL, NULL);

	return (slapi_entry_add_values( *entry, (const char *)attr_type,
            							(struct berval**)&vals_array[0] ));
										
}

/*********************************************************************************/
/*		E	N	D						*/
/*********************************************************************************/

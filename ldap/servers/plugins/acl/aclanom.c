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

#include 	"acl.h"

/************************************************************************
Anonymous profile 
**************************************************************************/

struct  anom_targetacl {
	int				anom_type;				/* defines for anom types same as aci_type */
	int				anom_access;
    Slapi_DN		*anom_target;			/* target of the ACL */
	Slapi_Filter	*anom_filter;			/* targetfilter part */
	char			**anom_targetAttrs;		/* list of attrs */
};


struct anom_profile {
	short				anom_signature;
	short				anom_numacls;
	struct anom_targetacl anom_targetinfo[ACL_ANOM_MAX_ACL];
};

static struct anom_profile *acl_anom_profile = NULL;

static PRRWLock *anom_rwlock = NULL;
#define ANOM_LOCK_READ()     PR_RWLock_Rlock  (anom_rwlock )
#define ANOM_UNLOCK_READ()   PR_RWLock_Unlock (anom_rwlock )
#define ANOM_LOCK_WRITE()    PR_RWLock_Wlock  (anom_rwlock )
#define ANOM_UNLOCK_WRITE()  PR_RWLock_Unlock (anom_rwlock )


static void 	__aclanom__del_profile ();

/*
 * aclanom_init ();
 *	Generate a profile for the anonymous user.  We can use this profile
 *	later to determine what resources the client is allowed to.
 *
 * Dependency:
 * 		Before calling this, it is assumed that all the ACLs have been read
 *		and parsed. 
 *
 *		We will go thru all the ACL and pick the ANYONE ACL and generate the anom 
 *		profile.
 *
 */
int
aclanom_init ()
{

	acl_anom_profile = (struct anom_profile * )
                slapi_ch_calloc (1, sizeof ( struct anom_profile ) );

	if (( anom_rwlock = PR_NewRWLock( PR_RWLOCK_RANK_NONE,"ANOM LOCK") ) == NULL ) {
		slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
				"Failed in getting the ANOM rwlock\n" );
		return 1;
	}
	return 0;
}

/*
 * Depending on the context, this routine may need to take the
 * acicache read lock.
*/
void
aclanom_gen_anomProfile (acl_lock_flag_t lock_flag)
{
	aci_t					*aci = NULL;
	int						i;
	Targetattr				**srcattrArray;
	Targetattr				*attr;
	struct	anom_profile	*a_profile;
	PRUint32				cookie;

	PR_ASSERT( lock_flag == DO_TAKE_ACLCACHE_READLOCK ||
				lock_flag == DONT_TAKE_ACLCACHE_READLOCK);

	/*
	 * This routine requires two locks:
	 * the one for the global cache in acllist_acicache_READ_LOCK() and
	 * the one for the anom profile.
	 * They _must_ be taken in the order presented here or there
	 * is a deadlock scenario with acllist_remove_aci_needsLock() which
	 * takes them is this order.
	*/

	if ( lock_flag == DO_TAKE_ACLCACHE_READLOCK ) {
		acllist_acicache_READ_LOCK();
	}
	ANOM_LOCK_WRITE  ();
	a_profile = acl_anom_profile;

	if ( (!acl_get_aclsignature()) || ( !a_profile) ||
			(a_profile->anom_signature ==  acl_get_aclsignature()) ) {
		ANOM_UNLOCK_WRITE ();
		if ( lock_flag == DO_TAKE_ACLCACHE_READLOCK ) {
			acllist_acicache_READ_UNLOCK();
		}
		return;
	}

	/* D0 we have one already. If we do, then clean it up */
	__aclanom__del_profile();

	/* We have a new signature now */
	a_profile->anom_signature =  acl_get_aclsignature();

	slapi_log_error(SLAPI_LOG_ACL, plugin_name, "GENERATING ANOM USER PROFILE\n");
	/*
	** Go thru the ACL list and find all the ACLs  which apply to the 
	** anonymous user i.e anyone. we can generate a profile for that.
	** We will llok at the simple case i.e it matches 
	** cases not handled:
	**  1) When there is a mix if rule types ( allows & denies )
	**
	*/

	aci = acllist_get_first_aci ( NULL, &cookie );	
	while ( aci ) {
		int			a_numacl;
		struct slapi_filter	*f;
		char			**destattrArray;


		/* 
		 * We must not have a  rule like:  deny ( all )  userdn != "xyz" 
		 * or groupdn !=
		*/
		if ( (aci->aci_type &  ACI_HAS_DENY_RULE) &&
			( (aci->aci_type & ACI_CONTAIN_NOT_USERDN ) ||
			  (aci->aci_type & ACI_CONTAIN_NOT_GROUPDN)	||
				(aci->aci_type & ACI_CONTAIN_NOT_ROLEDN)) ){
			slapi_log_error(SLAPI_LOG_ACL, plugin_name, 
				"CANCELLING ANOM USER PROFILE BECAUSE OF DENY RULE\n");
			goto cleanup;
		}

		/* Must be a anyone rule */
		if ( aci->aci_elevel != ACI_ELEVEL_USERDN_ANYONE ) {
			aci =  acllist_get_next_aci ( NULL, aci, &cookie);
			continue;
		}
		if (! (aci->aci_access &  ( SLAPI_ACL_READ | SLAPI_ACL_SEARCH)) ) {
			aci =  acllist_get_next_aci ( NULL, aci, &cookie);
			continue;
		}
		/* If the rule has anything other than userdn = "ldap:///anyone"
		** let's not consider complex rules - let's make this lean.
		*/
		if ( aci->aci_ruleType & ~ACI_USERDN_RULE ){
			slapi_log_error(SLAPI_LOG_ACL, plugin_name, 
				"CANCELLING ANOM USER PROFILE BECAUSE OF COMPLEX RULE\n");
			goto cleanup;
		}

		/* Must not be a or have a 
		** 1 ) DENY RULE   2) targetfilter
		** 3) no target pattern ( skip monitor acl  )
		*/
		if ( aci->aci_type & ( ACI_HAS_DENY_RULE  | ACI_TARGET_PATTERN |
					ACI_TARGET_NOT | ACI_TARGET_FILTER_NOT )) {
			const char	*dn = slapi_sdn_get_dn ( aci->aci_sdn );

			/* see if this is a monitor acl */
			if (( strcasecmp ( dn, "cn=monitor") == 0 )  ||
			    ( strcasecmp ( dn, "cn=monitor,cn=ldbm") == 0 )) {
				aci =  acllist_get_next_aci ( NULL, aci, &cookie);
				continue;
			} else {
				/* clean up before leaving */
				slapi_log_error(SLAPI_LOG_ACL, plugin_name, 
					"CANCELLING ANOM USER PROFILE 1\n");
				goto cleanup;
			}

		}

		/* Now we have an ALLOW ACL which applies to anyone */
		a_numacl = a_profile->anom_numacls++;

		if ( a_profile->anom_numacls == ACL_ANOM_MAX_ACL ) {
			slapi_log_error(SLAPI_LOG_ACL, plugin_name, "CANCELLING ANOM USER PROFILE 2\n");
			goto cleanup;
		}

		if ( (f = aci->target) != NULL ) {
			char            *avaType;
			struct berval   *avaValue;
			slapi_filter_get_ava ( f, &avaType, &avaValue );

			a_profile->anom_targetinfo[a_numacl].anom_target = 
						slapi_sdn_new_dn_byval ( avaValue->bv_val );
		} else {
			a_profile->anom_targetinfo[a_numacl].anom_target = 
						slapi_sdn_dup ( aci->aci_sdn );
		}

		a_profile->anom_targetinfo[a_numacl].anom_filter =  NULL;
		if ( aci->targetFilterStr ) {
			a_profile->anom_targetinfo[a_numacl].anom_filter =  slapi_str2filter ( aci->targetFilterStr );
			if (NULL == a_profile->anom_targetinfo[a_numacl].anom_filter) {
				const char	*dn = slapi_sdn_get_dn ( aci->aci_sdn );
				slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
								"Error: invalid filter [%s] in anonymous aci in entry [%s]\n",
								aci->targetFilterStr, dn);
				goto cleanup;
			}
		}				

		i = 0;
		srcattrArray = aci->targetAttr;
		while ( srcattrArray[i])
			i++;

		a_profile->anom_targetinfo[a_numacl].anom_targetAttrs = 
					(char **) slapi_ch_calloc ( 1, (i+1) * sizeof(char *));

		srcattrArray = aci->targetAttr;
		destattrArray = a_profile->anom_targetinfo[a_numacl].anom_targetAttrs;

		i = 0;
		while ( srcattrArray[i] ) {
			attr = srcattrArray[i];
			if ( attr->attr_type & ACL_ATTR_FILTER ) {
				/* Do'nt want to support these kind now */
				destattrArray[i] = NULL;
				/* clean up before leaving */
				__aclanom__del_profile ();
				slapi_log_error(SLAPI_LOG_ACL, plugin_name, 
					"CANCELLING ANOM USER PROFILE 3\n");
				goto cleanup;
			}

			destattrArray[i] = slapi_ch_strdup ( attr->u.attr_str );
			i++;
		}	

		destattrArray[i] = NULL;

		aclutil_print_aci ( aci, "anom" );	
		/*  Here we are storing att the info from the acls. However
		** we are only interested in a few things like ACI_TARGETATTR_NOT.
		*/
		a_profile->anom_targetinfo[a_numacl].anom_type = aci->aci_type;
		a_profile->anom_targetinfo[a_numacl].anom_access = aci->aci_access;
		
		aci =  acllist_get_next_aci ( NULL, aci, &cookie);
	}

	ANOM_UNLOCK_WRITE ();
	if ( lock_flag == DO_TAKE_ACLCACHE_READLOCK ) {
		acllist_acicache_READ_UNLOCK();
	}
	return;

cleanup:
	__aclanom__del_profile ();
	ANOM_UNLOCK_WRITE ();
	if ( lock_flag == DO_TAKE_ACLCACHE_READLOCK ) {
		acllist_acicache_READ_UNLOCK();
	}
}


void
aclanom_invalidateProfile ()
{
	ANOM_LOCK_WRITE();
	if ( acl_anom_profile && acl_anom_profile->anom_numacls )
		acl_anom_profile->anom_signature = 0;
	ANOM_UNLOCK_WRITE();


}

/*
 * __aclanom_del_profile
 *
 *	Cleanup the anonymous user's profile we have.
 * 
 *	ASSUMPTION: A WRITE LOCK HAS BEEN OBTAINED
 *
 */
static void
__aclanom__del_profile (void)
{
	int						i;
	struct	anom_profile	*a_profile;


	if ( (a_profile = acl_anom_profile) == NULL ) {
		return;
	}

	for ( i=0; i < a_profile->anom_numacls; i++ ) {
		int	j = 0;
		char	**destArray = a_profile->anom_targetinfo[i].anom_targetAttrs;

		/* Deallocate target */
		slapi_sdn_free ( &a_profile->anom_targetinfo[i].anom_target );
		
		/* Deallocate filter */
		if ( a_profile->anom_targetinfo[i].anom_filter )
			slapi_filter_free ( a_profile->anom_targetinfo[i].anom_filter, 1 );

		/* Deallocate attrs */
		if ( destArray ) {
			while ( destArray[j] ) {
				slapi_ch_free ( (void **) &destArray[j] );
				j++;
			}
			slapi_ch_free ( (void **) &destArray );
		}
		a_profile->anom_targetinfo[i].anom_targetAttrs = NULL;
		a_profile->anom_targetinfo[i].anom_type = 0;
        a_profile->anom_targetinfo[i].anom_access = 0;
	}
	a_profile->anom_numacls = 0;

	/* Don't clean the signatue */
}

/*
 * This routine sets up a "context" for evaluation of access control
 * on a given entry for an anonymous user.
 * It just factors out the scope and targetfilter info into a list
 * of indices of the global anom profile list, that apply to this
 * entry, and stores them in the aclpb.
 * It's use relies on the way that access control is checked in the mailine search
 * code in the core server, namely: check filter, check entry, then check each
 * attribute.  So, we call this in acl_access_allowed() before calling
 * aclanom_match_profile()--therafter, aclanom_match_profile() uses the
 * context to evaluate access to the entry and attributes.
 * 
 * If there are no anom profiles, or the anom profiles get cancelled
 * due to complex anon acis, then that's OK, aclanom_match_profile()
 * returns -1 and the mainline acl code kicks in.
 *
 * The lifetime of this context info is the time it takes to check
 * access control for all parts of this entry (filter, entry, attributes).
 * So, if for an example an entry changes and a given anom profile entry
 * no longer applies, we will not notice until the next round of access
 * control checking on the entry--this is acceptable.
 * 
 * The gain on doing this factoring in the following type of search
 * was approx 6%:
 * anon bind, 20 threads, exact match, ~20 attributes returned, 
 * (searchrate & DirectoryMark).
 * 
*/
void
aclanom_get_suffix_info(Slapi_Entry *e,
							struct acl_pblock *aclpb ) {
	int i;
	char     *ndn = NULL;
	Slapi_DN    *e_sdn;
	const char    *aci_ndn;
	struct scoped_entry_anominfo *s_e_anominfo =
			&aclpb->aclpb_scoped_entry_anominfo;

	ANOM_LOCK_READ ();
		
	s_e_anominfo->anom_e_nummatched=0;

	ndn = slapi_entry_get_ndn ( e ) ;
	e_sdn= slapi_entry_get_sdn ( e ) ;
	for (i=acl_anom_profile->anom_numacls-1; i >= 0; i-- ) {
		aci_ndn = slapi_sdn_get_ndn (acl_anom_profile->anom_targetinfo[i].anom_target);
		if (!slapi_sdn_issuffix(e_sdn,acl_anom_profile->anom_targetinfo[i].anom_target)
				|| (!slapi_is_rootdse(ndn) && slapi_is_rootdse(aci_ndn)))
						continue;
		if ( acl_anom_profile->anom_targetinfo[i].anom_filter ) {
			if ( slapi_vattr_filter_test( aclpb->aclpb_pblock, e,
                               		acl_anom_profile->anom_targetinfo[i].anom_filter, 
					0 /*don't do acess chk*/)  != 0)
				continue;
		}
		s_e_anominfo->anom_e_targetInfo[s_e_anominfo->anom_e_nummatched]=i;
		s_e_anominfo->anom_e_nummatched++;
	}
	ANOM_UNLOCK_READ (); 
}


/*
 * aclanom_match_profile
 *	Look at the anonymous profile and see if we can use it or not.
 *
 *
 *	Inputs:
 *		Slapi_Pblock			- The Pblock
 *		Slapi_Entry *e			- The entry for which we are asking permission.
 *		char *attr			- Attribute name
 *		int  access			- access type
 *
 *	Return:
 *		LDAP_SUCCESS ( 0 )		- acess is allowed.
 *		LDAP_INSUFFICIENT_ACCESS (50 )  - access denied.
 *		-1			        - didn't match the targets
 *
 * Assumptions:
 * 	The caller of this module has to make sure that the client is 
 *	an anonymous client.
 */
int
aclanom_match_profile (Slapi_PBlock *pb, struct acl_pblock *aclpb, Slapi_Entry *e,
						char *attr, int access ) 
{

	struct	anom_profile	*a_profile;
	int						result, i, k;
	char					**destArray;
	int						tmatched = 0;
	char					ebuf[ BUFSIZ ];
	int						loglevel;
	struct scoped_entry_anominfo *s_e_anominfo =
			&aclpb->aclpb_scoped_entry_anominfo;

	loglevel = slapi_is_loglevel_set(SLAPI_LOG_ACL) ? SLAPI_LOG_ACL : SLAPI_LOG_ACLSUMMARY;

	/* WE are only interested for READ/SEARCH  */
	if (  !(access & ( SLAPI_ACL_SEARCH | SLAPI_ACL_READ)) )
		return -1;

	/* If we are here means, the client is doing a anonymous read/search */
	if ((a_profile = acl_anom_profile) == NULL ) {
		return -1;
	}		

	ANOM_LOCK_READ ();
	/* Check the signature first */
	if ( a_profile->anom_signature != acl_get_aclsignature () ) {
		/* Need to regenrate the signature.
	 	 * Need a WRITE lock to generate the anom profile  -
	 	 * which is obtained in acl__gen_anom_user_profile (). Since
	 	 * I don't have upgrade lock -- I have to do this way.
	 	 */
		ANOM_UNLOCK_READ ();
		aclanom_gen_anomProfile (DO_TAKE_ACLCACHE_READLOCK);
		aclanom_get_suffix_info(e, aclpb );
		ANOM_LOCK_READ ();
	}

	/* doing this early saves use a malloc/free/normalize cost */
	if ( !a_profile->anom_numacls ) {
		ANOM_UNLOCK_READ ();
		return -1;
	}

	result = LDAP_INSUFFICIENT_ACCESS;

	for ( k=0; k<s_e_anominfo->anom_e_nummatched; k++ ) {
		short	matched = 0;
		short	j = 0;	
	
		i = s_e_anominfo->anom_e_targetInfo[k];
	
		/* Check for right */
		if ( !(a_profile->anom_targetinfo[i].anom_access & access) )
			continue;
		
		/*
		 * XXX rbyrne Don't really understand the role of this
		 * but not causing any obvious bugs...get back to it.
		*/		
		tmatched++;
		
		if ( attr == NULL ) {
			result = LDAP_SUCCESS;
			break;
		}

		destArray = a_profile->anom_targetinfo[i].anom_targetAttrs;
		while ( destArray[j] ) {
			if ( strcasecmp ( destArray[j], "*") == 0 ||
				slapi_attr_type_cmp ( attr, destArray[j], 1 ) == 0 ) {
				matched = 1;
				break;
			}
			j++;
		}
		
		if ( a_profile->anom_targetinfo[i].anom_type  & ACI_TARGET_ATTR_NOT )
			result = matched ? LDAP_INSUFFICIENT_ACCESS : LDAP_SUCCESS;
		else 
			result = matched ? LDAP_SUCCESS : LDAP_INSUFFICIENT_ACCESS;
	
		if ( result == LDAP_SUCCESS )
			break;
	} /* for */

	if ( slapi_is_loglevel_set(loglevel) ) {
		char					*ndn = NULL;
		Slapi_Operation			*op = NULL;

		ndn = slapi_entry_get_ndn ( e ) ;
		slapi_pblock_get(pb, SLAPI_OPERATION, &op);

		if ( result == LDAP_SUCCESS) {
			const char				*aci_ndn;
			aci_ndn = slapi_sdn_get_ndn (acl_anom_profile->anom_targetinfo[i].anom_target);

			slapi_log_error(loglevel, plugin_name, 
				"conn=%" NSPRIu64 " op=%d: Allow access on entry(%s).attr(%s) to anonymous: acidn=\"%s\"\n",
				op->o_connid, op->o_opid,
				escape_string_with_punctuation(ndn, ebuf),
				attr ? attr:"NULL",
				escape_string_with_punctuation(aci_ndn, ebuf));
		} else {
			slapi_log_error(loglevel, plugin_name,
				"conn=%" NSPRIu64 " op=%d: Deny access on entry(%s).attr(%s) to anonymous\n",
		op->o_connid, op->o_opid,
                escape_string_with_punctuation(ndn, ebuf), attr ? attr:"NULL" );
		}
	}	

	ANOM_UNLOCK_READ ();
	if ( tmatched == 0) 
		return -1;
	else 
		return result;

}
int
aclanom_is_client_anonymous ( Slapi_PBlock *pb )
{
	char		*clientDn;


	slapi_pblock_get ( pb, SLAPI_REQUESTOR_DN, &clientDn );
	if (acl_anom_profile->anom_numacls  && 
			acl_anom_profile->anom_signature  &&  
			(( NULL == clientDn) || (clientDn && *clientDn == '\0')) )
		return 1;

	return 0;
}


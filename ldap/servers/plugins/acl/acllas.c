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

	
#include	<ipfstruct.h>
#include 	"acl.h"

/*
	A word on this file:

	The various routines here implement each component of the subject of an aci
	eg. "groupdn", "userdn","roledn", "userattr" etc.
	They are responsible for evaluating each individual keyword not for doing
	the boolean combination of these keywords, nor for combining multiple 
	allow()/deny() statements--that's libaccess's job.
	For example, for "groupdn", DS_LASGroupDnEval might have to evaluate 
	something like this: 

	"groupdn = "ldap:///cn=G1,o=sun.com || ldap:///cn=G2,o=sun.com"

	The "=" here may be "!=" as well and these routines take care of the
	comparator.

	These rotuines get called via acl__TestRights(), which calls
	ACL_EvalTestRights() a libaccess routine (the immediately calling routine is
	ACLEvalAce() in oneeval.cpp).

	They should return LAS_EVAL_TRUE, if that keyword component evaluates to
	TRUE, LAS_EVAL_FALSE if it evaluates to FALSE and LAS_EVAL_FAIL if an
	error occurrs during evaluation. Note that once any component of a subject
	returns LAS_EVAL_FAIL, the evaluation in libaccess stops and the whole
	subject does not match and that aci is not applied.
*/

/*

	A word on three-valued logic:

	In general when you do boolean combination of terms some of which
	may evaluate to UNDEFINED then you need to define what the combination 
	means.
	So, for example libaccess implements a scheme which once UNDEFINED
	is returned for a term, it bales out of the
	evaluation and the whole expression evaluates to UNDEFINED.
	In this case the aci will not apply.
	On the other hand LDAP filters (cf. rfc2251 4.5.1) say that for OR,
	an expression will
	evaluate to TRUE if any term is TRUE, even if some terms are UNDEFINED. 	
	Other off the cuff options might be to redefine UNDEFINED to be FALSE,
	or TRUE.

	Which is best ? 

	Well it probably depends on exactly what is to decided based on the
	evaluation of the logical expression.  However, the final suggestion is
	almost certainly
	bad--you are unlikely to want to take an action based on an undefined
	result and
	defining UNDEFINED to be either TRUE or FALSE may result in the overall
	expression
	returning TRUE--a security hole.  The only case this might work is if you
	are dealing with restricted
	expressions eg. terms may only be AND'ed togther--in this case defining
	UNDEFINED to be FALSE would guarantee a result of FALSE.

	The libaccess approach of returning UNDEFINED once an UNDEFINED is
	encountered during
	evaluation is not too bad--at least it guarantees that no aci will apply
	based on an 
	undefined value.  However, with an aci like this "...allow(all) A or B"
	where A returned UNDEFINED, you might be disappointed not to receive the
	rights if it was B that
	was granting you the rights and evaluation of A, which has nothing to do
	with you, returns UNDEFINED.  In the case of an aci like 
	"...deny(all) A or B" then the same
	situation is arguably a security hole. Note that this scheme also makes
	the final result
	dependent on the evaluation order and so if the evaluation engine does
	anything fancy internally (eg. reordering the terms in an OR so that fast
	to evaluate ones came first) then
	this would need to be documented so that a user (or a tool) could look at
	the external syntax and figure out the result of the evaluation.
	Also it breaks commutivity and De Morgans law.

	The LDAP filter scheme is starting to look good--it solves the problems of
	the
	libaccess approach, makes the final result of an expression independent of
	the evaluation order and
	gives you back commutivity of OR and AND. De Morgans is still broken, but
	that's because of the asymmetry of behaviour of UNDEFINED with OR and AND.
	
	So...?

	For acis, in general it can look like this:

	"...allow(rights)(LogicalCombinationofBindRule);
		deny(LogicalCombinationOfBindRule)...." 

	A BindRule is one of the "userdn", "groupdn" or "userattr" things and it
	can look like this:

	"groupdn = "ldap:///cn=G1,o=sun.com || ldap:///cn=G2,o=sun.com"

    The "=" here may be "!=" as well and these routines take care of the
	comparator.

	For "userattr" keywords a mutilvalued attribute amounts a logical OR of the
	individual values.  There is also a logical OR over the different levels
	as specified by the "parent" keyword. 
	
	In fact there are three levels of logical combination:
	
	1.	In the aclplugin:
		The "||" and "!=" combinator for BindRule keywords like userdn and
		groupdn.
		The fact that for the "userattr" keyword, a mutilvalued attribute is
		evaluated as "||".  Same for the different levels.
	2.	In libaccess:
		The logical combination of BindRules.
	3.	In libaccess:
		The evaluation of multiple BindRules seperated by ";", which means OR.		

	The LDAP filter three-valued logic SHOULD be applied to each level but
	here's the way it works right now:

	1.  At this level it depends....

		DS_LASIpGetter			- get attr for IP -
			returns ip address or LAS_EVAL_FAIL for error.
			no logical combination.
		DS_LASDnsGetter			- get attr for DNS-
			 returns dns name or LAS_EVAL_FAIL for error
			no logical combination.
		DS_LASUserDnEval		- LAS Evaluation for USERDN		- 	
			three-valued logic
			logical combination: || and !=
		DS_LASGroupDnEval		- LAS Evaluation for GROUPDN	-
			three-valued logic
			logical combination: || and !=
		DS_LASRoleDnEval		- LAS Evaluation for ROLEDN		-
			three-valued logic
			logical combination: || and !=
		DS_LASUserDnAttrEval	- LAS Evaluation for USERDNATTR -
			three-valued logic  
			logical combination || (over specified attribute values and
			parent keyword levels), !=
		DS_LASAuthMethodEval	- LAS Evaluation for AUTHMETHOD -
			three-valued logic ( logical combinations: !=)
		DS_LASGroupDnAttrEval	- LAS Evaluation for GROUPDNATTR -
			three-valued logic  
			logical combination || (over specified attribute values and
			parent keyword levels), !=
		DS_LASUserAttrEval		- LAS Evaluation for USERATTR -
			USER, GROUPDN and ROLEDN as above.
			LDAPURL -- three-valued logic (logical combinations: || over
						specified attribute vales, !=)
			attrname#attrvalue -- three-valued logic, logical combination:!=

	2. The libaccess scheme applies at this level.
	3. The LDAP filter three-valued logic applies at this level.

	Example of realistic, non-bizarre things that cause evaluation of a
	BindRule to be undefined are exceeding some resource limits (nesting level,
	lookthrough limit) in group membership evaluation, or trying to get ADD
	permission from the "userattr" keyword at "parent" level 0.	
	Note that not everything that might be construed as an error needs to be
	taken as UNDEFINED.  For example, things like not finding a user or an
	attribute in an entry can be defined away as TRUE or FALSE.  eg. in an
	LDAP filter (cn=rob) applied to an entry where cn is not present is FALSE,
	not UNDEFINED.  Similarly, if the number of levels in a parent keyword
	exceeds the allowed limit, we just ignore the rest--though this
	is a syntax error which should be detected at parse time.
	

*/

/* To get around warning: declared in ldapserver/lib/ldaputil/ldaputili.h */
extern int ldapu_member_certificate_match (void* cert, const char* desc);

/****************************************************************************/
/* Defines, Constants, ande Declarations                                    */
/****************************************************************************/
static char* const   	type_objectClass = "objectclass";
static char* const 	filter_groups = "(|(objectclass=groupOfNames) (objectclass=groupOfUniqueNames)(objectclass=groupOfCertificates)(objectclass=groupOfURLs))";
static char* const	type_member = "member";
static char* const	type_uniquemember = "uniquemember";
static char* const	type_memberURL = "memberURL";
static char* const	type_memberCert = "memberCertificateDescription";

/* cache strategy for groups */
#define ACLLAS_CACHE_MEMBER_GROUPS		0x1
#define ACLLAS_CACHE_NOT_MEMBER_GROUPS	0x2
#define ACLLAS_CACHE_ALL_GROUPS			0x3

/****************************************************************************/
/* prototypes                                                               */
/****************************************************************************/
static int		acllas__handle_group_entry(Slapi_Entry *, void *);
static int 		acllas__user_ismember_of_group(struct acl_pblock *aclpb,
							char* groupDN,
							char* clientDN,
							int   cache_status,
							CERTCertificate *clientCert);
static int acllas__user_has_role( struct acl_pblock *aclpb,
									Slapi_DN *roleDN, Slapi_DN *clientDn);
static int 		acllas__add_allgroups (Slapi_Entry* e, void *callback_data);
static int 		acllas__eval_memberGroupDnAttr (char *attrName, 
							Slapi_Entry *e,
							char *n_clientdn, 
							struct acl_pblock *aclpb);
static int 		acllas__verify_client (Slapi_Entry* e, void *callback_data);
static int 		acllas__verify_ldapurl (Slapi_Entry* e, void *callback_data);
static char* 		acllas__dn_parent( char *dn, int level);
static int 		acllas__get_members (Slapi_Entry* e, void *callback_data);
static int 		acllas__client_match_URL (struct acl_pblock *aclpb,
						   char *n_dn, char *url );
static int 		acllas__handle_client_search (Slapi_Entry *e, void *callback_data);
static int 		__acllas_setup ( NSErr_t *errp, char *attr_name, CmpOp_t comparator, int allow_range,
						char *attr_pattern, int *cachable, void **LAS_cookie,
        				PList_t subject, PList_t resource, PList_t auth_info,
        				PList_t global_auth, char *lasType, char *lasName, lasInfo *linfo);
int
aclutil_evaluate_macro( char * user, lasInfo *lasinfo,
						acl_eval_types evalType );
static int
acllas_eval_one_user( struct acl_pblock *aclpb,
						char * clientDN, char *userKeyword);
static int
acllas_eval_one_group(char *group, lasInfo *lasinfo);
static int
acllas_eval_one_role(char *role, lasInfo *lasinfo);
static char **
acllas_replace_dn_macro( char *rule, char *matched_val, lasInfo *lasinfo);
static char **
acllas_replace_attr_macro( char *rule, lasInfo *lasinfo);
static int 
acllas_eval_one_target_filter( char * str, Slapi_Entry *e);

/****************************************************************************/

int
DS_LASIpGetter(NSErr_t *errp, PList_t subject, PList_t resource, PList_t
 		auth_info, PList_t global_auth, void *arg)
{

	struct acl_pblock	*aclpb = NULL;
	IPAddr_t        	ip=0;
	PRNetAddr		client_praddr;
	struct in_addr		client_addr;
	int			rv;


	rv = ACL_GetAttribute(errp, DS_PROP_ACLPB, (void **)&aclpb,
				subject, resource, auth_info, global_auth);
	if ( rv != LAS_EVAL_TRUE  || ( NULL == aclpb )) {
		acl_print_acllib_err(errp, NULL);
		slapi_log_error( SLAPI_LOG_ACL, plugin_name,
			"DS_LASIpGetter:Unable to get the ACLPB(%d)\n", rv);
		return LAS_EVAL_FAIL;
	}

	if ( slapi_pblock_get( aclpb->aclpb_pblock, SLAPI_CONN_CLIENTNETADDR,
														&client_praddr ) != 0 ) {
                slapi_log_error( SLAPI_LOG_FATAL, plugin_name, "Could not get client IP.\n" );
                return( LAS_EVAL_FAIL );
        }

	if ( !PR_IsNetAddrType(&client_praddr, PR_IpAddrV4Mapped) ) {
	        slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
				 "Client address is IPv6. ACLs only support IPv4 addresses so far.\n");
		return( LAS_EVAL_FAIL );
	}
	        	
	client_addr.s_addr = client_praddr.ipv6.ip.pr_s6_addr32[3];

	ip = (IPAddr_t) ntohl( client_addr.s_addr );
	rv = PListInitProp(subject, 0, ACL_ATTR_IP, (void *)ip, NULL);
	
	slapi_log_error( SLAPI_LOG_ACL, plugin_name,
        "Returning client ip address '%s'\n",
        (slapi_is_loglevel_set(SLAPI_LOG_ACL) ?  inet_ntoa(client_addr) : ""));

	return LAS_EVAL_TRUE;

}

/* 
 * This is called from the libaccess code when it needs to find a dns name.
 * It's called from ACL_GetAttribute() when it finds that ACL_ATTR_DNS is
 * not already part of the proplist.
 * 
*/

int 
DS_LASDnsGetter(NSErr_t *errp, PList_t subject, PList_t resource, PList_t
           auth_info, PList_t global_auth, void *arg)
{
	struct acl_pblock	*aclpb = NULL;
	PRNetAddr		client_praddr;
	PRHostEnt		*hp;
	char			*dnsName = NULL;
	int			rv;
	struct berval		**clientDns;


	rv = ACL_GetAttribute(errp, DS_PROP_ACLPB, (void **)&aclpb,
				subject, resource, auth_info, global_auth);
	if ( rv != LAS_EVAL_TRUE  || ( NULL == aclpb )) {
		acl_print_acllib_err(errp, NULL);
		slapi_log_error( SLAPI_LOG_ACL, plugin_name,
			"DS_LASDnsGetter:Unable to get the ACLPB(%d)\n", rv);
		return LAS_EVAL_FAIL;
	}
	
	if ( slapi_pblock_get( aclpb->aclpb_pblock, SLAPI_CLIENT_DNS, &clientDns ) != 0 ) {
                slapi_log_error( SLAPI_LOG_FATAL, plugin_name, "Could not get client IP.\n" );
                return( LAS_EVAL_FAIL );
	}

	/*
	 * If the client hostname has already been put into the pblock then
	 * use that.  Otherwise we work it out and add it ourselves.
	 * This info is connection-lifetime so with multiple operaitons on the same
	 * connection we will only do the calculation once.
	 *
	 * rbyrneXXX surely this code would be better in connection.c so
	 * the name would be just there waiting for us, and everyone else.
	 *
	*/

	if ( clientDns && clientDns[0] != NULL && clientDns[0]->bv_val ) {
		dnsName = clientDns[0]->bv_val;
	} else {
		struct	berval		**dnsList;
		char    		buf[PR_NETDB_BUF_SIZE];

		if ( slapi_pblock_get( aclpb->aclpb_pblock, SLAPI_CONN_CLIENTNETADDR, &client_praddr ) != 0 ) {

				slapi_log_error( SLAPI_LOG_FATAL, plugin_name, "Could not get client IP.\n" );
               return( LAS_EVAL_FAIL );
        	}
		hp = (PRHostEnt *)slapi_ch_malloc( sizeof(PRHostEnt) );
		if ( PR_GetHostByAddr( &(client_praddr), (char *)buf, sizeof(buf), hp ) == PR_SUCCESS ) {
			if ( hp->h_name != NULL ) {
				dnsList = (struct berval**) 
						slapi_ch_calloc (1, sizeof(struct berval*) * (1 + 1));
				*dnsList = (struct berval*) 
						slapi_ch_calloc ( 1, sizeof(struct berval));				
                dnsName = (*dnsList)->bv_val = slapi_ch_strdup( hp->h_name );
				(*dnsList)->bv_len = strlen ( (*dnsList)->bv_val );
				slapi_pblock_set( aclpb->aclpb_pblock, SLAPI_CLIENT_DNS, &dnsList );
			}
		} 
		slapi_ch_free( (void **)&hp );
	}

	if ( NULL == dnsName ) return LAS_EVAL_FAIL;

	rv = PListInitProp(subject, 0, ACL_ATTR_DNS, dnsName, NULL);
        if (rv < 0) {
                slapi_log_error ( SLAPI_LOG_ACL, plugin_name, 
				"DS_LASDnsGetter:Couldn't set the DNS property(%d)\n", rv );
                return LAS_EVAL_FAIL;
        }
	slapi_log_error ( SLAPI_LOG_ACL, plugin_name, "DNS name: %s\n", dnsName );
	return LAS_EVAL_TRUE;

}
/***************************************************************************/
/* New LASes								   */
/* 									   */
/* 1. user, groups. -- stubs to report errors. Not supported.		   */
/* 2. userdn								   */
/* 3. groupdn								   */
/* 4. userdnattr							   */
/* 5. authmethod							   */
/* 6. groupdnattr							   */
/* 7. roledn							*/
/* 									   */
/* 									   */
/***************************************************************************/
int 
DS_LASUserEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator, 
		char *attr_pattern, int *cachable, void **LAS_cookie, 
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth)
{
	slapi_log_error(SLAPI_LOG_FATAL, plugin_name, 
			"User LAS is not supported in the ACL\n");

	return LAS_EVAL_INVALID;
}

int 
DS_LASGroupEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator, 
		char *attr_pattern, int *cachable, void **LAS_cookie, 
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth)
{
	slapi_log_error(SLAPI_LOG_FATAL, plugin_name, 
			"Group LAS is not supported in the ACL\n");

	return LAS_EVAL_INVALID;
}

/***************************************************************************
*
* DS_LASUserDnEval
*	Evaluate the "userdn" LAS. See if the user has rights.
*
* Input:
*	attr_name	The string "userdn" - in lower case.
*	comparator	CMP_OP_EQ or CMP_OP_NE only
*	attr_pattern	A comma-separated list of users
*	cachable	Always set to FALSE.
*	subject		Subject property list
*	resource        Resource property list
*	auth_info	Authentication info, if any
*
* Returns:
*	retcode	        The usual LAS return codes.
*
* Error Handling:
*	None.
*
**************************************************************************/
int 
DS_LASUserDnEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator, 
		char *attr_pattern, int *cachable, void **LAS_cookie, 
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth)
{

	char		*users = NULL;
	char		*s_user, *user = NULL;
	char		*ptr = NULL;
	char		*end_dn = NULL;
	char		*n_edn = NULL;
	char		*parent_dn = NULL;
	int			matched;
	int			rc;
	short		len;
	const size_t 	LDAP_URL_prefix_len = strlen(LDAP_URL_prefix);
	const size_t 	LDAPS_URL_prefix_len = strlen(LDAPS_URL_prefix);
	lasInfo			lasinfo;
	int			got_undefined = 0;

	if ( 0 !=  (rc = __acllas_setup (errp, attr_name, comparator, 0, /* Don't allow range comparators */
									attr_pattern,cachable,LAS_cookie,
									subject, resource, auth_info,global_auth,
									DS_LAS_USERDN, "DS_LASUserDnEval", &lasinfo )) ) {
		return LAS_EVAL_FAIL;
	}

 	users = slapi_ch_strdup(attr_pattern);
	user = users;
	matched = ACL_FALSE;

	/* check if the clientdn is one of the users */
	while(user != 0 && *user != 0 && matched != ACL_TRUE ) {

		/* ignore leading whitespace */
		while(ldap_utf8isspace(user)) 
			LDAP_UTF8INC(user);

		/* Now we must see the userdn in the following
		** formats:
		**
		** The following formats are supported:
		** 
		**  1. The DN itself: 
		**    	allow (read)  userdn = "ldap:///cn=prasanta, ..." 
		**
		**  2. keyword SELF: 
		**    	allow (write)  
		**         userdn = "ldap:///self"
		**
		**  3. Pattern: 
		**    	deny (read) userdn = "ldap:///cn=*, o=netscape, c = us";
		**
		**  4. Anonymous user
		**    	deny (read, write)    userdn = "ldap:///anyone"
		**
		**  5. All users (All authenticated users)
		**    	allow (search)  **   	   userdn = "ldap:///all"
		**  6. parent "ldap:///parent"
		**  7. Synamic users using the URL
		** 
		**
		** DNs must be separated by "||". Ex:
		** allow (read)
		** userdn = "ldap:///DN1 || ldap:///DN2" 
		*/
	
		/* The DN is now "ldap:///DN" 
		** remove the "ldap:///" part
		*/
		if (strncasecmp (user, LDAP_URL_prefix, LDAP_URL_prefix_len) == 0) {
			s_user = user;
			user += LDAP_URL_prefix_len;
		} else if (strncasecmp (user, LDAPS_URL_prefix, LDAPS_URL_prefix_len) == 0) {
			s_user = user;
			user += LDAPS_URL_prefix_len;
		} else {
			char ebuf[ BUFSIZ ];
			slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
			 	"DS_LASUserDnEval:Syntax error(%s)\n", 
				 escape_string_with_punctuation( user, ebuf  ));
			return LAS_EVAL_FAIL;
		}

		/* Now we have the starting point of the "userdn" */
		if ((end_dn = strstr(user, "||")) != NULL) {
			auto char *t = end_dn;
			LDAP_UTF8INC(end_dn);
			LDAP_UTF8INC(end_dn);
			*t = 0;
		}

		/* Now user is a null terminated string */

		if (*user) {
			while(ldap_utf8isspace(user)) 
				LDAP_UTF8INC(user);
			/* ignore trailing whitespace */
			len = strlen(user);
			ptr = user+len-1;
			while(ptr >= user && ldap_utf8isspace(ptr)) {
				*ptr = '\0';
				LDAP_UTF8DEC(ptr);
			}
		}

		/* 
		** Check , if the user is a anonymous user. In that case
		** We must find the rule "ldap:///anyone"
		*/
		if (lasinfo.anomUser) {
			if (strcasecmp(user, "anyone") == 0 ) {
				/* matches  -- anonymous user */
				matched = ACL_TRUE;
				break;
			} 
		} else {
			/* URL format */
			
			if ((strstr (user, ACL_RULE_MACRO_DN_KEY) != NULL) ||
				(strstr (user, ACL_RULE_MACRO_DN_LEVELS_KEY) != NULL) ||
				(strstr (user, ACL_RULE_MACRO_ATTR_KEY) != NULL)) {			
				
				matched = aclutil_evaluate_macro( s_user, &lasinfo,
													ACL_EVAL_USER);
				if (matched == ACL_TRUE) {
					break;
				}										
				
			} else if (strchr (user, '?') != NULL) {
				/* URL format */
				if (acllas__client_match_URL ( lasinfo.aclpb, lasinfo.clientDn, 
							     s_user) == ACL_TRUE) {
					matched = ACL_TRUE;
					break;
				}
			} else if (strcasecmp(user, "anyone") == 0 ) {
				/* Anyone means anyone in the world */
				matched = ACL_TRUE;
				break;
			} else if (strcasecmp(user, "self") == 0) {
				if (n_edn == NULL) {
					n_edn = slapi_entry_get_ndn (  lasinfo.resourceEntry );
				}
				if (slapi_utf8casecmp((ACLUCHP)lasinfo.clientDn, (ACLUCHP)n_edn) == 0)
					matched = ACL_TRUE;
				break;
			} else if (strcasecmp(user, "parent") == 0) {
				if (n_edn == NULL) {
					n_edn = slapi_entry_get_ndn ( lasinfo.resourceEntry );
				}
				/* get the parent */
				parent_dn = slapi_dn_parent(n_edn);
				if (parent_dn && 
					slapi_utf8casecmp ((ACLUCHP)lasinfo.clientDn, (ACLUCHP)parent_dn) == 0)
					matched = ACL_TRUE;

				if (parent_dn) slapi_ch_free ( (void **) &parent_dn );
				break;
			} else if (strcasecmp(user, "all") == 0) {
				/* matches  -- */
				matched = ACL_TRUE;
				break;
			} else if (strchr(user, '*')) {
				char	line[200];
				char *lineptr = &line[0];
				char *newline = NULL;
				int lenu = 0;
				Slapi_Filter	*f = NULL;
				char		*tt;
				int		filterChoice;

				/* 
				** what we are doing is faking the str2simple()
				** function with a "userdn = "user")
				*/
				for (tt = user; *tt; tt++)
					*tt = TOLOWER ( *tt );

				if ((lenu = strlen(user)) > 190) { /* 200 - 9 for "(userdn=%s)" */
					newline = slapi_ch_malloc(lenu + 10);
					lineptr = newline;
				}

				sprintf (lineptr, "(userdn=%s)", user);
				if ((f = slapi_str2filter (lineptr)) == NULL) {
					if (newline) slapi_ch_free((void **) &newline);
					/* try the next one */
					break;
				}
				if (newline) slapi_ch_free((void **) &newline);
				filterChoice = slapi_filter_get_choice ( f );

				if (( filterChoice != LDAP_FILTER_SUBSTRINGS) &&
			    		( filterChoice != LDAP_FILTER_PRESENT)) {
			   		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
			    			 "DS_LASUserDnEval:Error in gen. filter(%s)\n", user);
				}
				if ((rc = acl_match_substring( f, 
							       lasinfo.clientDn,
							       1 /*exact match */)
							     ) == ACL_TRUE) {
					matched = ACL_TRUE;
					slapi_filter_free(f,1);
					break;
				}
				if (rc == ACL_ERR) {
			   		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
			    			"DS_LASUserDnEval:Error in matching patteren(%s)\n",
			     			user);
				}
				slapi_filter_free(f,1);
			} else {
				/* Must be a simple dn then */
				char *normed = NULL;
				size_t dnlen = 0;
				rc = slapi_dn_normalize_ext(user, 0, &normed, &dnlen);
				if (rc == 0) { /* user passed in; not terminated */
					*(normed + dnlen) = '\0';
				} else if (rc < 0) { /* normalization failed, user the original */
					normed = user;
				}
				rc = slapi_utf8casecmp((ACLUCHP)lasinfo.clientDn, (ACLUCHP)normed);
				if (normed != user) {
					slapi_ch_free_string(&normed);
				}
				if (0 == rc) {
					matched = ACL_TRUE;
					break;
				}
			}
		}

		if ( matched == ACL_DONT_KNOW ) {
			/* record this but keep going--maybe another user will evaluate to TRUE */
			got_undefined = 1;
		}
		/* Nothing matched -- try the next DN */
		user = end_dn;
	} /* end of while */

	slapi_ch_free ( (void **) &users);

	/*
	 * If no terms were undefined, then evaluate as normal.
	 * If there was an undefined term, but another one was TRUE, then we also evaluate
	 * as normal.  Otherwise, the whole expression is UNDEFINED.
	*/
	if ( matched == ACL_TRUE || !got_undefined ) {
		if (comparator == CMP_OP_EQ) {
			rc = (matched == ACL_TRUE  ? LAS_EVAL_TRUE : LAS_EVAL_FALSE);
		} else {
			rc = (matched == ACL_TRUE ? LAS_EVAL_FALSE : LAS_EVAL_TRUE);
		}
	} else {
		rc = LAS_EVAL_FAIL;
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
			"Returning UNDEFINED for userdn evaluation.\n");
	} 

	return rc;
}

/***************************************************************************
*
* DS_LASGroupDnEval
*
*
* Input:
*	attr_name	The string "userdn" - in lower case.
*	comparator	CMP_OP_EQ or CMP_OP_NE only
*	attr_pattern	A comma-separated list of users
*	cachable	Always set to FALSE.
*	subject		Subject property list
*	resource        Resource property list
*	auth_info	Authentication info, if any
*
* Returns:
*	retcode		The usual LAS return code
*			If the client is in any of the groups mentioned this groupdn keywrod
*			then returns LAS_EVAL_TRUE, if he's not in any LAS_EVAL_FALSE.
*			If any of the membership evaluations fail, then it goes on to evaluate the
*			others.
*
* Error Handling:
*	None.
*
**************************************************************************/
int 
DS_LASGroupDnEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator, 
		char *attr_pattern, int *cachable, void **LAS_cookie, 
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth)
{

	char			*groups;
	char			*groupNameOrig;
	char			*groupName;
	char			*ptr;
	char			*end_dn;
	int				matched;
	int				rc;
	int				len;
	const size_t 	LDAP_URL_prefix_len = strlen(LDAP_URL_prefix);
	int				any_group = 0;
	lasInfo			lasinfo;
	int 			got_undefined = 0;

	/* the setup should not fail under normal operation */
	if ( 0 !=  (rc = __acllas_setup (errp, attr_name, comparator, 0, /* Don't allow range comparators */
									attr_pattern,cachable,LAS_cookie,
									subject, resource, auth_info,global_auth,
									DS_LAS_GROUPDN, "DS_LASGroupDnEval", &lasinfo )) ) {
		return LAS_EVAL_FAIL;
	}

 	groups = slapi_ch_strdup(attr_pattern);
	groupNameOrig = groupName = groups;
	matched = ACL_FALSE;

	/* check if the groupdn is one of the users */
	while(groupName != 0 && *groupName != 0 && matched != ACL_TRUE) {

		/* ignore leading whitespace */
		while(ldap_utf8isspace(groupName)) 
			LDAP_UTF8INC(groupName);

		/* 
		** The syntax allowed for the groupdn is
		**
		** Example:
		**  groupdn = "ldap:///dn1 ||  ldap:///dn2";
		**
		*/
	
		if (strncasecmp (groupName, LDAP_URL_prefix,
				 LDAP_URL_prefix_len) == 0) {
			groupName += LDAP_URL_prefix_len;
		} else {
			char ebuf[ BUFSIZ ];
			slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
				  "DS_LASGroupDnEval:Syntax error(%s)\n",
				   escape_string_with_punctuation( groupName, ebuf ));
		}
	
		/* Now we have the starting point of the "groupdn" */
		if ((end_dn = strstr(groupName, "||")) != NULL) {
			auto char *t = end_dn;
			LDAP_UTF8INC(end_dn);
			LDAP_UTF8INC(end_dn);
			/* removing trailing spaces */
			LDAP_UTF8DEC(t);
			while (' ' == *t || '\t' == *t) {
				LDAP_UTF8DEC(t);
			}
			LDAP_UTF8INC(t);
			*t = '\0';
			/* removing beginning spaces */
			while (' ' == *end_dn || '\t' == *end_dn) {
				LDAP_UTF8INC(end_dn);
			}
		}

		if (*groupName) {
			while(ldap_utf8isspace(groupName)) 
				LDAP_UTF8INC(groupName);
			/* ignore trailing whitespace */
			len = strlen(groupName);
			ptr = groupName+len-1;
			while(ptr >= groupName && ldap_utf8isspace(ptr)) {
				*ptr = '\0';
				LDAP_UTF8DEC(ptr);
			}
		}

		/* 
		** Now we have the DN of the group. Evaluate the "clientdn"
		** and see if the user is a member of the group.
		*/
		if (0 == (strcasecmp(groupName, "anyone"))) {
			any_group = 1;
		}

		if (any_group) {
			/* anyone in the world */
			matched = ACL_TRUE;
			break;
		} else if ( lasinfo.anomUser && 
				(lasinfo.aclpb->aclpb_clientcert == NULL) && (!any_group)) {
			slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
					"Group not evaluated(%s)\n", groupName);
			break;
		} else {			
			if ((strstr (groupName, ACL_RULE_MACRO_DN_KEY) != NULL) ||
				(strstr (groupName, ACL_RULE_MACRO_DN_LEVELS_KEY) != NULL) ||
				(strstr (groupName, ACL_RULE_MACRO_ATTR_KEY) != NULL)) {			
				matched = aclutil_evaluate_macro( groupName, &lasinfo,
													ACL_EVAL_GROUP);
				slapi_log_error ( SLAPI_LOG_ACL, plugin_name,
						"DS_LASGroupDnEval: Param group name:%s\n",
						groupName);
			} else {
				LDAPURLDesc		*ludp = NULL;
				int             urlerr = 0;
				int				rval;
				Slapi_PBlock	*myPb = NULL;
				Slapi_Entry		**grpentries = NULL;

				/* Groupdn is full ldapurl? */
				if ((0 == (urlerr = slapi_ldap_url_parse(groupNameOrig, &ludp, 0, NULL))) &&
				    NULL != ludp->lud_dn &&
					-1 != ludp->lud_scope &&
					NULL != ludp->lud_filter) {
					/* Yes, it is full ldapurl; Let's run the search */
					myPb = slapi_pblock_new ();
					slapi_search_internal_set_pb(
								myPb,
								ludp->lud_dn,
								ludp->lud_scope,
								ludp->lud_filter,
								NULL,
								0,
								NULL /* controls */,
								NULL /* uniqueid */,
								aclplugin_get_identity (ACL_PLUGIN_IDENTITY),
								0 );	
					slapi_search_internal_pb(myPb);
					slapi_pblock_get(myPb, SLAPI_PLUGIN_INTOP_RESULT, &rval);
					if (rval == LDAP_SUCCESS) {
						Slapi_Entry		**ep;
						slapi_pblock_get(myPb,
								SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &grpentries);
						if ((grpentries != NULL) && (grpentries[0] != NULL)) {
							char *edn = NULL;
							for (ep = grpentries; *ep; ep++) {
								/* groups having ACI */
								edn = slapi_entry_get_ndn(*ep);
								matched = acllas_eval_one_group(edn, &lasinfo);
								if (ACL_TRUE == matched) {
									break; /* matched ! */
								}
							}
						}
					}
					slapi_free_search_results_internal(myPb);
					slapi_pblock_destroy (myPb);
	
				} else {
					if (urlerr) {
						slapi_log_error ( SLAPI_LOG_ACL, plugin_name,
										  "DS_LASGroupDnEval: Groupname [%s] not a valid ldap url: %d (%s)\n",
										  groupNameOrig, urlerr, slapi_urlparse_err2string(urlerr));
					}
					/* normal evaluation */
					matched = acllas_eval_one_group( groupName, &lasinfo );
				}
				if ( ludp ) {
					ldap_free_urldesc( ludp );
				}
			}
			
			if ( matched == ACL_TRUE ) {
				break;
			} else if ( matched == ACL_DONT_KNOW ) {
				/* record this but keep going--maybe another group will evaluate to TRUE */
				got_undefined = 1;
			}
		}
		/* Nothing matched -- try the next DN */
		groupNameOrig = groupName = end_dn;

	} /* end of while */

	/*
	 * If no terms were undefined, then evaluate as normal.
	 * If there was an undefined term, but another one was TRUE, then we also evaluate
	 * as normal.  Otherwise, the whole expression is UNDEFINED.
	*/
	if ( matched == ACL_TRUE || !got_undefined ) {
		if (comparator == CMP_OP_EQ) {
			rc = (matched == ACL_TRUE  ? LAS_EVAL_TRUE : LAS_EVAL_FALSE);
		} else {
			rc = (matched == ACL_TRUE ? LAS_EVAL_FALSE : LAS_EVAL_TRUE);
		}
	} else {
		rc = LAS_EVAL_FAIL;
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
			"Returning UNDEFINED for groupdn evaluation.\n");
	} 

	slapi_ch_free ((void**) &groups);
	return rc;
}
/***************************************************************************
*
* DS_LASRoleDnEval
*
*
* Input:
*	attr_name	The string "roledn" - in lower case.
*	comparator	CMP_OP_EQ or CMP_OP_NE only
*	attr_pattern	A "||" sperated list of roles
*	cachable	Always set to FALSE.
*	subject		Subject property list
*	resource        Resource property list
*	auth_info	Authentication info, if any
*
* Returns:
*	retcode	        The usual LAS return codes.
*
* Error Handling:
*	None.
*
**************************************************************************/
int 
DS_LASRoleDnEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator, 
		char *attr_pattern, int *cachable, void **LAS_cookie, 
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth)
{

	char			*roles;
	char			*role;
	char			*ptr;
	char			*end_dn;
	int				matched;
	int				rc;
	int				len;
	const size_t 	LDAP_URL_prefix_len = strlen(LDAP_URL_prefix);
	int				any_role = 0;
	lasInfo			lasinfo;
	int				got_undefined = 0;

	if ( 0 !=  (rc = __acllas_setup (errp, attr_name, comparator, 0, /* Don't allow range comparators */
									attr_pattern,cachable,LAS_cookie,
									subject, resource, auth_info,global_auth,
									DS_LAS_ROLEDN, "DS_LASRoleDnEval",
									&lasinfo )) ) {
		return LAS_EVAL_FALSE;
	}


 	roles = slapi_ch_strdup(attr_pattern);
	role = roles;
	matched = ACL_FALSE;

	/* check if the roledn is one of the users */
	while(role != 0 && *role != 0 && matched != ACL_TRUE) {

		/* ignore leading whitespace */
		while(ldap_utf8isspace(role)) 
			LDAP_UTF8INC(role);

		/* 
		** The syntax allowed for the roledn is
		**
		** Example:
		**  roledn = "ldap:///roledn1 ||  ldap:///roledn2";
		**
		*/
	
		if (strncasecmp (role, LDAP_URL_prefix,
				 LDAP_URL_prefix_len) == 0) {
			role += LDAP_URL_prefix_len;
		} else {
			char ebuf[ BUFSIZ ];
			slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
				  "DS_LASRoleDnEval:Syntax error(%s)\n",
				   escape_string_with_punctuation( role, ebuf ));
		}
	
		/* Now we have the starting point of the "roledn" */
		if ((end_dn = strstr(role, "||")) != NULL) {
			auto char *t = end_dn;
			LDAP_UTF8INC(end_dn);
			LDAP_UTF8INC(end_dn);
			*t = 0;
		}


		if (*role) {
			while(ldap_utf8isspace(role)) 
				LDAP_UTF8INC(role);
			/* ignore trailing whitespace */
			len = strlen(role);
			ptr = role+len-1;
			while(ptr >= role && ldap_utf8isspace(ptr)) {
				*ptr = '\0';
				LDAP_UTF8DEC(ptr);
			}
		}

		/* 
		** Now we have the DN of the role. Evaluate the "clientdn"
		** and see if the user has this role.
		*/
		if (0 == (strcasecmp(role, "anyone"))) {
			any_role = 1;
		}

		if (any_role) {
			/* anyone in the world */
			matched = ACL_TRUE;
			break;
		} else if ( lasinfo.anomUser && 
				(lasinfo.aclpb->aclpb_clientcert == NULL) && (!any_role)) {
			slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
					"Role not evaluated(%s) for anon user\n", role);
			break;
		} else {

			/* Take care of param strings */
			if ((strstr (role, ACL_RULE_MACRO_DN_KEY) != NULL) ||
				(strstr (role, ACL_RULE_MACRO_DN_LEVELS_KEY) != NULL) ||
				(strstr (role, ACL_RULE_MACRO_ATTR_KEY) != NULL)) {			
				
				matched = aclutil_evaluate_macro( role, &lasinfo,
													ACL_EVAL_ROLE);
				slapi_log_error ( SLAPI_LOG_ACL, plugin_name,
						"DS_LASRoleDnEval: Param role name:%s\n",
						role);
			} else {/* normal evaluation */

				matched = acllas_eval_one_role( role, &lasinfo);

			}
			
			if ( matched == ACL_TRUE ) {
				break;
			} else if ( matched == ACL_DONT_KNOW ) {
				/* record this but keep going--maybe another role will evaluate to TRUE */
				got_undefined = 1;
			}
		}
		/* Nothing matched -- try the next DN */
		role = end_dn;

	} /* end of while */

	/*
	 * If no terms were undefined, then evaluate as normal.
	 * If there was an undefined term, but another one was TRUE, then we also evaluate
	 * as normal.  Otherwise, the whole expression is UNDEFINED.
	*/
	if ( matched == ACL_TRUE || !got_undefined ) {
		if (comparator == CMP_OP_EQ) {
			rc = (matched == ACL_TRUE  ? LAS_EVAL_TRUE : LAS_EVAL_FALSE);
		} else {
			rc = (matched == ACL_TRUE ? LAS_EVAL_FALSE : LAS_EVAL_TRUE);
		}
	} else {
		rc = LAS_EVAL_FAIL;
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
			"Returning UNDEFINED for roledn evaluation.\n");
	} 

	slapi_ch_free ((void**) &roles);
	return rc;
}
/***************************************************************************
*
* DS_LASUserDnAttrEval
*
*
* Input:
*	attr_name	The string "userdn" - in lower case.
*	comparator	CMP_OP_EQ or CMP_OP_NE only
*	attr_pattern	A comma-separated list of users
*	cachable	Always set to FALSE.
*	subject		Subject property list
*	resource        Resource property list
*	auth_info	Authentication info, if any
*
* Returns:
*	retcode	        The usual LAS return codes.
*
* Error Handling:
*	None.
*
**************************************************************************/
struct userdnattr_info {
	char	*attr;
	int	result;
	char	*clientdn;
	Acl_PBlock	*aclpb;
};
#define ACLLAS_MAX_LEVELS 10
int 
DS_LASUserDnAttrEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator, 
		char *attr_pattern, int *cachable, void **LAS_cookie, 
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth)
{

	char			*n_currEntryDn = NULL;
	char			*s_attrName, *attrName;
	char			*ptr;
	int				matched;
	int				rc, len, i;
	char			*val;
	Slapi_Attr 		*a;
	int				levels[ACLLAS_MAX_LEVELS];
	int				numOflevels =0;
	struct userdnattr_info	info = {0};
	char			*attrs[2] = { LDAP_ALL_USER_ATTRS, NULL };
	lasInfo			lasinfo;
	int				got_undefined = 0;

	if ( 0 !=  (rc = __acllas_setup (errp, attr_name, comparator, 0, /* Don't allow range comparators */
									attr_pattern,cachable,LAS_cookie,
									subject, resource, auth_info,global_auth,
									DS_LAS_USERDNATTR, "DS_LASUserDnAttrEval", 
									&lasinfo )) ) {
		return LAS_EVAL_FAIL;
	}

	/* 
	** The userdnAttr syntax is
	** 	userdnattr = <attribute> or
	**	userdnattr = parent[0,2,4].attribute"
	**  Ex:
	**	userdnattr = manager; or
	**	userdnattr = "parent[0,2,4].manager";
	**
	**  Here 0 means current level, 2 means grandfather and 
	**   4 (great great grandfather)
	**
	** The function of this LAS is to compare the value of the
	** attribute in the Slapi_Entry with the "userdn".
	**
	** Ex: userdn: "cn=prasanta, o= netscape, c= us"
	** and in the Slapi_Entry the manager attribute  has
	** manager = <value>. Compare the userdn with manager.value to
	** determine the result.
	**
	*/
 	s_attrName = attrName = slapi_ch_strdup (attr_pattern);

	/* ignore leading/trailing whitespace */
	while(ldap_utf8isspace(attrName)) LDAP_UTF8INC(attrName);
	len = strlen(attrName);
	ptr = attrName+len-1;
	while(ptr >= attrName && ldap_utf8isspace(ptr)) {
		*ptr = '\0';
		LDAP_UTF8DEC(ptr);
	}

	
	/* See if we have a  parent[2].attr" rule */
	if (strstr(attrName, "parent[") != NULL) {
		char	*word, *str, *next;
	
		numOflevels = 0;
		n_currEntryDn = slapi_entry_get_ndn ( lasinfo.resourceEntry );
		str = attrName;

		ldap_utf8strtok_r(str, "[],. ",&next);
		/* The first word is "parent[" and so it's not important */

		while ((word= ldap_utf8strtok_r(NULL, "[],.", &next)) != NULL) {
			if (ldap_utf8isdigit(word)) {
				while (word && ldap_utf8isspace(word)) LDAP_UTF8INC(word);
				if (numOflevels < ACLLAS_MAX_LEVELS) 
					levels[numOflevels++] = atoi (word);
				else  {
					/*
					 * Here, ignore the extra levels..it's really
					 * a syntax error which should have been ruled out at parse time
					*/
					slapi_log_error( SLAPI_LOG_FATAL, plugin_name, 
						"DS_LASUserDnattr: Exceeded the ATTR LIMIT:%d: Ignoring extra levels\n",
									ACLLAS_MAX_LEVELS);
				}
			} else {
				/* Must be the attr name. We can goof of by 
				** having parent[1,2,a] but then you have to be
				** stupid to do that.
				*/
				char	*p = word;
				if (*--p == '.')  {
					attrName = word;
					break;
				}
			}
		}
		info.attr = attrName;
		info.clientdn = lasinfo.clientDn;
		info.result = 0;
	} else {
		levels[0] = 0;
		numOflevels = 1;
		
	} 

	/* No attribute name specified--it's a syntax error and so undefined */
	if (attrName == NULL ) {
		slapi_ch_free ( (void**) &s_attrName);
		return LAS_EVAL_FAIL;
	}

	slapi_log_error( SLAPI_LOG_ACL, plugin_name,"Attr:%s\n" , attrName);
	matched = ACL_FALSE;
	for (i=0; i < numOflevels; i++) {
		if ( levels[i] == 0 ) {
			Slapi_Value *sval=NULL;
			const struct berval		*attrVal;
			int j;

			/*
			 * For the add operation, the resource itself (level 0)
			 * must never be allowed to grant access--
			 * This is because access would be granted based on a value
		 	 * of an attribute in the new entry--security hole.
			 * 
			*/

			if ( lasinfo.aclpb->aclpb_optype == SLAPI_OPERATION_ADD) {
				slapi_log_error( SLAPI_LOG_ACL, plugin_name,
					"ACL info: userdnAttr does not allow ADD permission at level 0.\n");
				got_undefined = 1;
				continue;
			}
			slapi_entry_attr_find( lasinfo.resourceEntry, attrName, &a);
			if ( NULL == a ) continue;
			j= slapi_attr_first_value ( a,&sval );
			while ( j != -1 ) {
				attrVal = slapi_value_get_berval ( sval );
				/* Here if atleast 1 value matches then we are done.*/
				val = slapi_create_dn_string("%s", attrVal->bv_val);
				if (NULL == val) {
					slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
							"DS_LASUserDnAttrEval: Invalid syntax: %s\n",
							attrVal->bv_val );
					slapi_ch_free ( (void**) &s_attrName);
					return LAS_EVAL_FAIL;
				}

				if (slapi_utf8casecmp((ACLUCHP)val, (ACLUCHP)lasinfo.clientDn ) == 0) {
					char	ebuf [ BUFSIZ ];
					/* Wow it matches */
					slapi_log_error( SLAPI_LOG_ACL, plugin_name,
						"userdnAttr matches(%s, %s) level (%d)\n",
						val,
				ACL_ESCAPE_STRING_WITH_PUNCTUATION (lasinfo.clientDn, ebuf),
						0);
					matched = ACL_TRUE;
					slapi_ch_free ( (void **) &val);
					break;
				}
				slapi_ch_free ( (void**) &val);
				j = slapi_attr_next_value ( a, j, &sval );
			}
		} else {
			char		*p_dn;	/* parent dn */

			p_dn = acllas__dn_parent (n_currEntryDn, levels[i]);
			if (p_dn == NULL) continue;

			/* use new search internal API */
			{
			Slapi_PBlock *aPb = slapi_pblock_new ();

			/*
			 * This search may be chained if chaining for ACL is
			 * is enabled in the backend and the entry is in
			 * a chained backend.
			*/
			slapi_search_internal_set_pb (  aPb,
							p_dn,
							LDAP_SCOPE_BASE,
							"objectclass=*",
							&attrs[0],
							0,
							NULL /* controls */,
							NULL /* uniqueid */,
							aclplugin_get_identity (ACL_PLUGIN_IDENTITY),
							0 /* actions */);

			slapi_search_internal_callback_pb(aPb,
							  &info /* callback_data */,
							  NULL/* result_callback */,
							  acllas__verify_client,
							  NULL /* referral_callback */);
			slapi_pblock_destroy(aPb);
			}

			/*
			 *  Currently info.result is boolean so
			 * we do not need to check for ACL_DONT_KNOW
			*/
			if (info.result) {
				matched = ACL_TRUE;
				slapi_log_error( SLAPI_LOG_ACL, plugin_name,
						"userdnAttr matches at level (%d)\n", levels[i]);
			}
		}
		if (matched == ACL_TRUE) {				
			break;
		}
	}

	slapi_ch_free ( (void **) &s_attrName);

	/*
	 * If no terms were undefined, then evaluate as normal.
	 * If there was an undefined term, but another one was TRUE, then we also evaluate
	 * as normal.  Otherwise, the whole expression is UNDEFINED.
	*/
	if ( matched == ACL_TRUE || !got_undefined ) {
		if (comparator == CMP_OP_EQ) {
			rc = (matched == ACL_TRUE  ? LAS_EVAL_TRUE : LAS_EVAL_FALSE);
		} else {
			rc = (matched == ACL_TRUE ? LAS_EVAL_FALSE : LAS_EVAL_TRUE);
		}
	} else {
		rc = LAS_EVAL_FAIL;
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
			"Returning UNDEFINED for userdnattr evaluation.\n");
	} 

	return rc;
}

/***************************************************************************
*
* DS_LASLdapUrlAttrEval
*
*
* Input:
*	attr_name     The string "ldapurl" - in lower case.
*	comparator    CMP_OP_EQ or CMP_OP_NE only
*	attr_pattern  A comma-separated list of users
*	cachable      Always set to FALSE.
*	subject       Subject property list
*	resource      Resource property list
*	auth_info     Authentication info, if any
*	las_info      LAS info to pass the resource entry
*
* Returns:
*	retcode	      The usual LAS return codes.
*
* Error Handling:
*	None.
*
**************************************************************************/
int 
DS_LASLdapUrlAttrEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator, 
		char *attr_pattern, int *cachable, void **LAS_cookie, 
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth, lasInfo lasinfo)
{

	char			*n_currEntryDn = NULL;
	char			*s_attrName = NULL, *attrName = NULL;
	char			*ptr;
	int				matched;
	int				rc, len, i;
	int				levels[ACLLAS_MAX_LEVELS];
	int				numOflevels =0;
	struct userdnattr_info	info = {0};
	char			*attrs[2] = { LDAP_ALL_USER_ATTRS, NULL };
	int				got_undefined = 0;

	/* 
	** The ldapurlAttr syntax is
	** 	userdnattr = <attribute> or
	**	userdnattr = parent[0,2,4].attribute"
	**  Ex:
	**	userdnattr = manager; or
	**	userdnattr = "parent[0,2,4].manager";
	**
	**  Here 0 means current level, 2 means grandfather and 
	**   4 (great great grandfather)
	**
	** The function of this LAS is to compare the value of the
	** attribute in the Slapi_Entry with the "ldapurl". 
	**
	** Ex: ldapurl: ldap:///dc=example,dc=com??sub?(l=Mountain View)
	** and in the Slapi_Entry of the bind user has
	** l = Mountain View. Compare the bind user's 'l' and the value to
	** determine the result.
	**
	*/
 	s_attrName = attrName = slapi_ch_strdup(attr_pattern);

	/* ignore leading/trailing whitespace */
	while (ldap_utf8isspace(attrName)) LDAP_UTF8INC(attrName);
	len = strlen(attrName);
	ptr = attrName+len-1;
	while (ptr >= attrName && ldap_utf8isspace(ptr)) {
		*ptr = '\0';
		LDAP_UTF8DEC(ptr);
	}

	/* See if we have a  parent[2].attr" rule */
	if (strstr(attrName, "parent[") != NULL) {
		char	*word, *str, *next;
	
		numOflevels = 0;
		n_currEntryDn = slapi_entry_get_ndn ( lasinfo.resourceEntry );
		str = attrName;

		ldap_utf8strtok_r(str, "[],. ",&next);
		/* The first word is "parent[" and so it's not important */

		while ((word= ldap_utf8strtok_r(NULL, "[],.", &next)) != NULL) {
			if (ldap_utf8isdigit(word)) {
				while (word && ldap_utf8isspace(word)) LDAP_UTF8INC(word);
				if (numOflevels < ACLLAS_MAX_LEVELS) 
					levels[numOflevels++] = atoi (word);
				else  {
					/*
					 * Here, ignore the extra levels..it's really
					 * a syntax error which should have been ruled out at parse time
					*/
					slapi_log_error( SLAPI_LOG_FATAL, plugin_name, 
						"DS_LASLdapUrlattr: Exceeded the ATTR LIMIT:%d: Ignoring extra levels\n",
									ACLLAS_MAX_LEVELS);
				}
			} else {
				/* Must be the attr name. We can goof of by 
				** having parent[1,2,a] but then you have to be
				** stupid to do that.
				*/
				char	*p = word;
				if (*--p == '.')  {
					attrName = word;
					break;
				}
			}
		}
		info.attr = attrName;
		info.clientdn = lasinfo.clientDn;
		info.aclpb = lasinfo.aclpb;
		info.result = 0;
	} else {
		levels[0] = 0;
		numOflevels = 1;
		
	} 

	/* No attribute name specified--it's a syntax error and so undefined */
	if (attrName == NULL ) {
		slapi_ch_free ( (void**) &s_attrName);
		return LAS_EVAL_FAIL;
	}

	slapi_log_error( SLAPI_LOG_ACL, plugin_name,"Attr:%s\n" , attrName);
	matched = ACL_FALSE;
	for (i = 0; i < numOflevels; i++) {
		if ( levels[i] == 0 ) { /* parent[0] or the target itself */
			Slapi_Value             *sval = NULL;
			const struct  berval	*attrVal;
			Slapi_Attr				*attrs;
			int i;
	
			/* Get the attr from the resouce entry */
			if ( 0 == slapi_entry_attr_find (lasinfo.resourceEntry,
											 attrName, &attrs) ) {
				i = slapi_attr_first_value ( attrs, &sval );
				if ( i == -1 ) {
					/* Attr val not there
					 * so it's value cannot equal other one */
					matched = ACL_FALSE;
					continue; /* try next level */
				}
			} else {
				/* Not there  so it cannot equal another one */
				matched = ACL_FALSE;
				continue; /* try next level */
			}
			
			while ( matched != ACL_TRUE && (sval != NULL)) {
				attrVal = slapi_value_get_berval ( sval );
				matched = acllas__client_match_URL ( lasinfo.aclpb, 
													 lasinfo.clientDn, 
								     				 attrVal->bv_val);
				if ( matched != ACL_TRUE ) 
					i = slapi_attr_next_value ( attrs, i, &sval );
				if ( matched == ACL_DONT_KNOW ) {
					got_undefined = 1;
				}
			}
		} else {
			char		*p_dn;	/* parent dn */
			Slapi_PBlock *aPb = NULL;

			p_dn = acllas__dn_parent (n_currEntryDn, levels[i]);
			if (p_dn == NULL) continue;

			/* use new search internal API */
			aPb = slapi_pblock_new ();

			/*
			 * This search may be chained if chaining for ACL is
			 * is enabled in the backend and the entry is in
			 * a chained backend.
			 */
			slapi_search_internal_set_pb (  aPb,
							p_dn,
							LDAP_SCOPE_BASE,
							"objectclass=*",
							&attrs[0],
							0,
							NULL /* controls */,
							NULL /* uniqueid */,
							aclplugin_get_identity (ACL_PLUGIN_IDENTITY),
							0 /* actions */);

			slapi_search_internal_callback_pb(aPb,
							  &info /* callback_data */,
							  NULL/* result_callback */,
							  acllas__verify_ldapurl,
							  NULL /* referral_callback */);
			slapi_pblock_destroy(aPb);

			/*
			 *  Currently info.result is boolean so
			 * we do not need to check for ACL_DONT_KNOW
			*/
			if (info.result) {
				matched = ACL_TRUE;
				slapi_log_error( SLAPI_LOG_ACL, plugin_name,
						"userdnAttr matches at level (%d)\n", levels[i]);
			}
		}
		if (matched == ACL_TRUE) {				
			break;
		}
	}
	slapi_ch_free ( (void **) &s_attrName);

	/*
	 * If no terms were undefined, then evaluate as normal.
	 * If there was an undefined term, but another one was TRUE, 
	 * then we also evaluate as normal.  
	 * Otherwise, the whole expression is UNDEFINED.
	 */
	if ( matched == ACL_TRUE || !got_undefined ) {
		if (comparator == CMP_OP_EQ) {
			rc = (matched == ACL_TRUE  ? LAS_EVAL_TRUE : LAS_EVAL_FALSE);
		} else {
			rc = (matched == ACL_TRUE ? LAS_EVAL_FALSE : LAS_EVAL_TRUE);
		}
	} else {
		rc = LAS_EVAL_FAIL;
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
			"Returning UNDEFINED for userdnattr evaluation.\n");
	} 

	return rc;
}

/***************************************************************************
*
* DS_LASAuthMethodEval
*
*
* Input:
*	attr_name	The string "authmethod" - in lower case.
*	comparator	CMP_OP_EQ or CMP_OP_NE only
*	attr_pattern	A comma-separated list of users
*	cachable	Always set to FALSE.
*	subject		Subject property list
*	resource        Resource property list
*	auth_info	Authentication info, if any
*
* Returns:
*	retcode	        The usual LAS return codes.
*
* Error Handling:
*	None.
*
**************************************************************************/
int 
DS_LASAuthMethodEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator, 
		char *attr_pattern, int *cachable, void **LAS_cookie, 
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth)
{

	char		*attr;
	char		*ptr;
	int			len;
	int			matched;
	int			rc;
	char		*s = NULL;
	lasInfo			lasinfo;

	if ( 0 !=  (rc = __acllas_setup (errp, attr_name, comparator, 0, /* Don't allow range comparators */
									attr_pattern,cachable,LAS_cookie,
									subject, resource, auth_info,global_auth,
									DS_LAS_AUTHMETHOD, "DS_LASAuthMethodEval", 
									&lasinfo )) ) {
		return LAS_EVAL_FAIL;
	}

 	attr = attr_pattern;

	matched = ACL_FALSE;
	/* ignore leading whitespace */
	s = strstr (attr, SLAPD_AUTH_SASL);
	if ( s) {
		s +=4;
		attr = s;
	}
 
	while(ldap_utf8isspace(attr)) LDAP_UTF8INC(attr);
	len = strlen(attr);
	ptr = attr+len-1;
	while(ptr >= attr && ldap_utf8isspace(ptr)) {
		*ptr = '\0';
		LDAP_UTF8DEC(ptr);
	}

	slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
				"DS_LASAuthMethodEval:authtype:%s authmethod:%s\n", 
				lasinfo.authType, attr);

	/* None method means, we don't care -- otherwise we care */
	if ((strcasecmp(attr, "none") == 0) ||
		(strcasecmp(attr, lasinfo.authType) == 0)) {
		matched = ACL_TRUE;
	}

	if ( matched == ACL_TRUE || matched == ACL_FALSE) {
		if (comparator == CMP_OP_EQ) {
			rc = (matched == ACL_TRUE ? LAS_EVAL_TRUE : LAS_EVAL_FALSE);
		} else {
			rc = (matched == ACL_TRUE ? LAS_EVAL_FALSE : LAS_EVAL_TRUE);
		}
	} else {
		rc = LAS_EVAL_FAIL;
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
			"Returning UNDEFINED for authmethod evaluation.\n");
	}

	return rc;
}

/***************************************************************************
*
* DS_LASSSFEval
*
*
* Input:
*       attr_name       The string "ssf" - in lower case.
*       comparator      CMP_OP_EQ, CMP_OP_NE, CMP_OP_GT, CMP_OP_LT, CMP_OP_GE, CMP_OP_LE
*       attr_pattern    An integer representing the SSF
*       cachable        Always set to FALSE.
*       subject         Subject property list
*       resource        Resource property list
*       auth_info       Authentication info, if any
*
* Returns:
*       retcode         The usual LAS return codes.
*
* Error Handling:
*       None.
*
**************************************************************************/
int
DS_LASSSFEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator,
                char *attr_pattern, int *cachable, void **LAS_cookie,
                PList_t subject, PList_t resource, PList_t auth_info,
                PList_t global_auth)
{
	char            *attr;
	char            *ptr;
	int             len;
	int             rc;
	lasInfo         lasinfo;
	int		aclssf;

	if ( 0 !=  (rc = __acllas_setup (errp, attr_name, comparator, 1, /* Allow range comparators */
					attr_pattern,cachable,LAS_cookie,
					subject, resource, auth_info,global_auth,
					DS_LAS_SSF, "DS_LASSSFEval",
					&lasinfo )) ) {
		return LAS_EVAL_FAIL;
	}

	attr = attr_pattern;

	/* ignore leading and trailing whitespace */
	while(ldap_utf8isspace(attr)) LDAP_UTF8INC(attr);
	len = strlen(attr);
	ptr = attr+len-1;
	while(ptr >= attr && ldap_utf8isspace(ptr)) {
		*ptr = '\0';
		LDAP_UTF8DEC(ptr);
	}

	/* Convert SSF from bind rule to an int. */
	aclssf = (int) strtol(attr, &ptr, 10);
	if (*ptr != '\0') {
		rc = LAS_EVAL_FAIL;
		slapi_log_error( SLAPI_LOG_ACL, plugin_name,
			"Error parsing numeric SSF from bind rule.\n");
		slapi_log_error( SLAPI_LOG_ACL, plugin_name,
			"Returning UNDEFINED for ssf evaluation.\n");
	}

	/* Check for negative values or a value overflow. */
	if ((aclssf < 0) || (((aclssf == INT_MAX) || (aclssf == INT_MIN)) && (errno == ERANGE))){
		rc = LAS_EVAL_FAIL;
		slapi_log_error( SLAPI_LOG_ACL, plugin_name,
			"SSF \"%s\" is invalid. Value must range from 0 to %d",
			attr, INT_MAX);
		slapi_log_error( SLAPI_LOG_ACL, plugin_name,
			"Returning UNDEFINED for ssf evaluation.\n");
	}

	slapi_log_error( SLAPI_LOG_ACL, plugin_name,
		"DS_LASSSFEval: aclssf:%d, ssf:%d\n",
		aclssf, lasinfo.ssf);

	switch ((int)comparator) {
		case CMP_OP_EQ:
			if (lasinfo.ssf == aclssf) {
				rc = LAS_EVAL_TRUE;
			} else {
				rc = LAS_EVAL_FALSE;
			}
			break;
		case CMP_OP_NE:
			if (lasinfo.ssf != aclssf) {
				rc = LAS_EVAL_TRUE;
			} else {
				rc = LAS_EVAL_FALSE;
			}
			break;
		case CMP_OP_GT:
			if (lasinfo.ssf > aclssf) {
				rc = LAS_EVAL_TRUE;
			} else {
				rc = LAS_EVAL_FALSE;
			}
			break;
		case CMP_OP_LT:
			if (lasinfo.ssf < aclssf) {
				rc = LAS_EVAL_TRUE;
			} else {
				rc = LAS_EVAL_FALSE;
			}
			break;
		case CMP_OP_GE:
			if (lasinfo.ssf >= aclssf) {
				rc = LAS_EVAL_TRUE;
			} else {
				rc = LAS_EVAL_FALSE;
			}
			break;
		case CMP_OP_LE:
			if (lasinfo.ssf <= aclssf) {
				rc = LAS_EVAL_TRUE;
			} else {
				rc = LAS_EVAL_FALSE;
			}
			break;
		default:
			/* This should never happen since the comparator is
			 * validated by __acllas_setup(), but better safe
			 * than sorry. */
			rc = LAS_EVAL_FAIL;
			slapi_log_error( SLAPI_LOG_ACL, plugin_name,
				"Invalid comparator \"%d\" evaluating SSF.\n",
				(int)comparator);
			slapi_log_error( SLAPI_LOG_ACL, plugin_name,
				"Returning UNDEFINED for ssf evaluation.\n");
	}

	return rc;
}


/****************************************************************************
* Struct to evaluate and keep the current members being evaluated
*
*		 0 1 2 3 4 5
*	member: [a,b,c,d,e,f]
* 	c_idx may point to 2 i.e to "c" if "c" is being evaluated to
*		see if any of "c" members is the clientDN.
*	lu_idx points to the last used spot i.e 5. 
*	lu_idx++ is the next free spot.
*
*	We allocate ACLLAS_MAX_GRP_MEMBER ptr first and then we add if it 
*	is required. 
*
***************************************************************************/
#define ACLLAS_MAX_GRP_MEMBER 50
struct member_info 
{
	char			*member;		/* member DN */
	int				parentId;		/* parent of this member */
};

struct eval_info
{
	int					result;		/* result status */
	char				*userDN;	/* client's normalized DN */
	int 				c_idx;		/* Index to the current member being processed */
	int					lu_idx;		/* Index to the slot where the last member is stored */
	char				**member;	/* mmebers list */
	struct member_info 	**memberInfo;/* array of memberInfo  */
	CERTCertificate		*clientCert;	/* ptr to cert */
	struct acl_pblock 	*aclpb;	/*aclpblock */
};

#ifdef FOR_DEBUGGING
static void
dump_member_info ( struct eval_info *info, struct member_info *minfo, char *buf )
{
	if ( minfo )
	{
		if ( minfo->parentId >= 0 )
		{
			dump_member_info ( info, minfo->parentId, buf );
		}
		else
		{
			strcat ( buf, "<nil>" );
		}
		strcat ( buf, "->" );
		strcat ( buf, minfo->member );
	}
}

static void
dump_eval_info (char *caller, struct eval_info *info, int idx)
{
	char buf[1024];
	int len;
	int i;

	if ( idx < 0 )
	{
		sprintf ( buf, "\nuserDN=\"%s\"\nmember=", info->userDN);
		if (info->member && *info->member)
		{
			len = strlen (buf);
			/* member is a char ** */
			sprintf ( &(buf[len]), "\"%s\"", *info->member );
		}
		len = strlen (buf);
		sprintf ( &(buf[len]), "\nmemberinfo[%d]-[%d]:", info->c_idx, info->lu_idx );
		if ( info->memberInfo )
		for (i = 0; i <= info->lu_idx; i++)
		{
			len = strlen(buf);
			sprintf ( &buf[len], "\n  [%d]: ", i );
			dump_member_info ( info, info->memberInfo[i], buf );
		}
		slapi_log_error ( SLAPI_LOG_FATAL, NULL, "\n======== candidate member info in eval_info ========%s\n\n", buf );
	}
	else
	{
		sprintf (buf, "evaluated candidate [%d]=", idx);
		switch (info->result)
		{
			case ACL_TRUE:
				strcat (buf, "ACL_TRUE\n");
				break;
			case ACL_FALSE:
				strcat (buf, "ACL_FALSE\n");
				break;
			case ACL_DONT_KNOW:
				strcat (buf, "ACL_DONT_KNOW\n");
				break;
			default:
				len = strlen (buf);
				sprintf ( &(buf[len]), "%d\n", info->result );
				break;
		}
		dump_member_info ( info, info->memberInfo[idx], buf );
		slapi_log_error ( SLAPI_LOG_FATAL, NULL, "%s\n", buf );
	}
}
#endif

/***************************************************************************
*
* acllas__user_ismember_of_group
*
* 	Check if the user is a member of the group and nested groups..
*
* Input:
*	char	*groupdn	- DN of the group
*	char	*clientDN	- Dn of the client
*
* Returns:
*	ACL_TRUE		- the user is a member of the group.
*	ACL_FALSE		- Not a member
*	ACL_DONT_KNOW	- Any errors eg. resource limits exceeded and we could
*					not compelte the evaluation.
*
* Error Handling:
*	None.
*
**************************************************************************/
static int
acllas__user_ismember_of_group( struct acl_pblock *aclpb,
				char* groupDN, 
				char* clientDN,
				int cache_status,
				CERTCertificate	*clientCert)
{
 

 	char			*attrs[5];
	char			*currDN;
	int			i,j;
	int			result = ACL_FALSE;
	struct eval_info	info = {0};
	int			nesting_level;
	int 			numOfMembersAtCurrentLevel;
	int			numOfMembersVisited;
	int			totalMembersVisited;
	int			numOfMembers;
	int			max_nestlevel;
	int			max_memberlimit;
	aclUserGroup		*u_group;
	struct member_info	*groupMember = NULL;
	struct member_info 	*parentGroup = NULL;

	/* 
	** First, Let's look thru the cached list and determine if the client is
	** a member of the cached list of groups.
	*/
	if ( (u_group = aclg_get_usersGroup ( aclpb , clientDN )) == NULL) {
		 slapi_log_error( SLAPI_LOG_ACL, plugin_name,
			"Failed to find/allocate a usergroup--aborting evaluation\n");
		return(ACL_DONT_KNOW);
	}

	slapi_log_error( SLAPI_LOG_ACL, plugin_name, "Evaluating user %s in group %s?\n",
		clientDN, groupDN );

	/* Before I start using, get a reader lock on the group cache */
	aclg_lock_groupCache ( 1 /* reader */ );
	for ( i= 0; i < u_group->aclug_numof_member_group; i++) {
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, "-- In %s\n",
		u_group->aclug_member_groups[i] );
		if ( slapi_utf8casecmp((ACLUCHP)groupDN, (ACLUCHP)u_group->aclug_member_groups[i]) == 0){
			aclg_unlock_groupCache ( 1 /* reader */ );
			slapi_log_error( SLAPI_LOG_ACL, plugin_name, "Evaluated ACL_TRUE\n");
			return ACL_TRUE;
		}
	}

	/* see if we know the client is not a member of a group. */
	for ( i= 0; i < u_group->aclug_numof_notmember_group; i++) {
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, "-- Not in %s\n",
		u_group->aclug_notmember_groups[i] );
		if ( slapi_utf8casecmp((ACLUCHP)groupDN, (ACLUCHP)u_group->aclug_notmember_groups[i]) == 0){
			aclg_unlock_groupCache ( 1 /* reader */ );
			slapi_log_error( SLAPI_LOG_ACL, plugin_name, "Evaluated ACL_FALSE\n");
			return ACL_FALSE;
		}
	}

	/* 
	** That means we didn't find the the group in the cache. -- we have to add it
	** so no need for READ lock - need to get a WRITE lock. We will get it just before
	** modifying it.
	*/
	aclg_unlock_groupCache ( 1 /* reader */ );

	/* Indicate the initialization handler  -- this module will be 
	** called by the backend to evaluate the entry.
	*/
	info.result = ACL_FALSE;
	if (clientDN && *clientDN != '\0') 
		info.userDN = clientDN;
	else 
		info.userDN = NULL;

	info.c_idx = 0;
	info.memberInfo = (struct member_info **) slapi_ch_malloc (ACLLAS_MAX_GRP_MEMBER * sizeof(struct member_info *));
	groupMember = (struct member_info *) slapi_ch_malloc ( sizeof (struct member_info) );
	groupMember->member = slapi_ch_strdup(groupDN);
	groupMember->parentId = -1;
	info.memberInfo[0] = groupMember;
	info.lu_idx = 0;

	attrs[0] = type_member;
	attrs[1] = type_uniquemember;
	attrs[2] = type_memberURL;
	attrs[3] = type_memberCert;
	attrs[4] = NULL;

	currDN = groupMember->member;

	/* nesting level is 0 to begin with */
	nesting_level = 0;
	numOfMembersVisited = 0;
	totalMembersVisited = 0;
	numOfMembersAtCurrentLevel = 1;

	if (clientCert)
		info.clientCert = clientCert;
	else 
		info.clientCert = NULL;
	info.aclpb = aclpb;

	max_memberlimit = aclpb->aclpb_max_member_sizelimit;
	max_nestlevel = aclpb->aclpb_max_nesting_level;

#ifdef FOR_DEBUGGING
	dump_eval_info ( "acllas__user_ismember_of_group", &info, -1 );
#endif

eval_another_member:

	numOfMembers = info.lu_idx - info.c_idx;

	/* Use new search internal API */
	{
		Slapi_PBlock * aPb = slapi_pblock_new ();
		
		/*
		 * This search may NOT be chained--we demand that group
		 * definition be local.
		*/
		slapi_search_internal_set_pb (  aPb,
						currDN,
						LDAP_SCOPE_BASE,
						filter_groups,
						&attrs[0],
						0,
						NULL /* controls */,
						NULL /* uniqueid */,
						aclplugin_get_identity (ACL_PLUGIN_IDENTITY),
						SLAPI_OP_FLAG_NEVER_CHAIN /* actions */);
		slapi_search_internal_callback_pb(aPb,
						  &info /* callback_data */,
						  NULL/* result_callback */,
						  acllas__handle_group_entry,
						  NULL /* referral_callback */); 

		if ( info.result == ACL_TRUE )
			slapi_log_error( SLAPI_LOG_ACL, plugin_name,"-- In %s\n", info.memberInfo[info.c_idx]->member ); 
		else if ( info.result == ACL_FALSE )
			slapi_log_error( SLAPI_LOG_ACL, plugin_name,"-- Not in %s\n", info.memberInfo[info.c_idx]->member ); 

		slapi_pblock_destroy (aPb);
	}

	if (info.result == ACL_TRUE) {
		/* 
		** that means the client is a member of the
		** group or one of the nested groups. We are done.
		*/
		result = ACL_TRUE;
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, "Evaluated ACL_TRUE\n");
		goto free_and_return;
	}
	numOfMembersVisited++;

	if (numOfMembersVisited == numOfMembersAtCurrentLevel) {
		/* This means we have looked at all the members for this level */
		numOfMembersVisited = 0;
		
		/* Now we are ready to look at the next level */
		nesting_level++;
	
		/* So, far we have visited ... */
		totalMembersVisited += numOfMembersAtCurrentLevel;

		/* How many members in the next level ? */
		numOfMembersAtCurrentLevel = 
			info.lu_idx - totalMembersVisited +1;
	}

	if ((nesting_level > max_nestlevel)) {
		 slapi_log_error( SLAPI_LOG_ACL, plugin_name,
			"GroupEval:Member not found within the allowed nesting level (Allowed:%d Looked at:%d)\n", 
			max_nestlevel, nesting_level);

		result = ACL_DONT_KNOW; /* don't try to cache info based on this result */
		goto free_and_return;
	}

	/* limit of -1 means "no limit */
	if (info.c_idx > max_memberlimit && 
			max_memberlimit != -1 ) {
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
			"GroupEval:Looked at too many entries:(%d, %d)\n",
				info.c_idx, info.lu_idx);
		result = ACL_DONT_KNOW; /* don't try to cache info based on this result */
		goto free_and_return;
	}
	if (info.lu_idx > info.c_idx) {
		if (numOfMembers == (info.lu_idx - info.c_idx)) {
			/* That means it's not a GROUP. It is just another
			** useless member which doesn't match. Remove  the BAD dude.
			*/
			groupMember = info.memberInfo[info.c_idx];

			if  (groupMember ) {
				if ( groupMember->member )  slapi_ch_free ( (void **) &groupMember->member );
				slapi_ch_free ( (void **) &groupMember );
				info.memberInfo[info.c_idx] = NULL;
			}
		}
		info.c_idx++;

		/* Go thru the stack and see if we have already 
		** evaluated this group. If we have, then skip it.
		*/
		while (1) {
			int	evalNext=0;
			int	j;
			if (info.c_idx >  info.lu_idx)  {
				/* That means we have crossed the limit. We
				** may end of in this situation if we 
				** have circular groups
				*/
				info.c_idx = info.lu_idx;	
				goto free_and_return;
			}
		
			/* Break out of the loop if we have searched to the end */
			groupMember = info.memberInfo[info.c_idx];
			if ( (NULL == groupMember) || ((currDN = groupMember->member)!= NULL))
				break;

			for (j = 0; j < info.c_idx; j++) {
				groupMember = info.memberInfo[j];
				if (groupMember->member && 
					(slapi_utf8casecmp((ACLUCHP)currDN, (ACLUCHP)groupMember->member) == 0)) {
					/* Don't need the duplicate */
					groupMember = info.memberInfo[info.c_idx];
					slapi_ch_free ( (void **) &groupMember->member );
					slapi_ch_free ( (void **) &groupMember );
					info.memberInfo[info.c_idx] = NULL;
					info.c_idx++;
					evalNext=1;
					break;
				}
			}
			if (!evalNext) break;
		}
		/* Make sure that we have a valid DN to chug along */
		groupMember = info.memberInfo[info.c_idx];
		if ((info.c_idx <= info.lu_idx) && ((currDN = groupMember->member) != NULL))
			goto eval_another_member;
	} 

free_and_return:
	/* Remove the unnecessary members from the list  which
	** we might have accumulated during the last execution
	** and we don't need to look at them.
	*/
	i = info.c_idx;
	i++;
	while (i <= info.lu_idx) {
		groupMember = info.memberInfo[i];
		slapi_ch_free ( (void **) &groupMember->member );
		slapi_ch_free ( (void **) &groupMember );
		info.memberInfo[i] = NULL;
		i++;
	}

	/* 
	** Now we have a list which has all the groups 
	** which we  need to cache
	*/ 
	info.lu_idx = info.c_idx;

	/* since we are updating the groupcache, get a write lock */
	aclg_lock_groupCache ( 2 /* writer */ );

	/* 
	** Keep the result of the evaluation in the cache.
	** We have 2 lists: member_of and not_member_of. We can use this 
	** cached information next time we evaluate groups.
	*/
	if (result == ACL_TRUE && 
		(cache_status & ACLLAS_CACHE_MEMBER_GROUPS)) {
		int	ngr = 0;
	
		/* get the last group which the user is a member of */	
		groupMember = info.memberInfo[info.c_idx];

		while ( groupMember ) {
			int already_cached = 0;

			parentGroup = (groupMember->parentId<0)?NULL:info.memberInfo[groupMember->parentId];
			for (j=0; j < u_group->aclug_numof_member_group;j++){
				if (slapi_utf8casecmp( (ACLUCHP)groupMember->member,
                                 (ACLUCHP)u_group->aclug_member_groups[j]) == 0) {
					already_cached = 1;
                   	break;
  				}
			}
			if (already_cached)  {
				groupMember = parentGroup;
				parentGroup = NULL;
				continue;
			}

			ngr = u_group->aclug_numof_member_group++;
			if (u_group->aclug_numof_member_group >= 
					u_group->aclug_member_group_size){
				u_group->aclug_member_groups = 
					(char **) slapi_ch_realloc (
						(void *) u_group->aclug_member_groups,
						(u_group->aclug_member_group_size +
						ACLUG_INCR_GROUPS_LIST) *
						sizeof (char *));
				u_group->aclug_member_group_size +=
							ACLUG_INCR_GROUPS_LIST;
			} 
			u_group->aclug_member_groups[ngr] = slapi_ch_strdup ( groupMember->member );
			slapi_log_error ( SLAPI_LOG_ACL, plugin_name, 
					"Adding Group (%s) ParentGroup (%s) to the IN GROUP List\n",
					groupMember->member , parentGroup ? parentGroup->member: "NULL");

			groupMember = parentGroup;
			parentGroup = NULL;
		}
	} else if (result == ACL_FALSE && 
			(cache_status & ACLLAS_CACHE_NOT_MEMBER_GROUPS)) {
		int	ngr = 0;

		/* NOT IN THE GROUP LIST */	
		/* get the last group which the user is a member of */	
		groupMember = info.memberInfo[info.c_idx];

		while ( groupMember ) {
			int already_cached = 0;

			parentGroup = (groupMember->parentId<0)?NULL:info.memberInfo[groupMember->parentId];
			for (j=0; j < u_group->aclug_numof_notmember_group;j++){
				if (slapi_utf8casecmp( (ACLUCHP)groupMember->member,
                               	(ACLUCHP)u_group->aclug_notmember_groups[j]) == 0) {
					already_cached = 1;
                   	break;
 				}
			}
			if (already_cached)  {
				groupMember = parentGroup;
				parentGroup = NULL;
				continue;
			}

			ngr = u_group->aclug_numof_notmember_group++;
			if (u_group->aclug_numof_notmember_group >= 
					u_group->aclug_notmember_group_size){
				u_group->aclug_notmember_groups = 
			     		(char **) slapi_ch_realloc (
					(void *) u_group->aclug_notmember_groups,
					(u_group->aclug_notmember_group_size +
					 ACLUG_INCR_GROUPS_LIST) *
					 sizeof (char *));
				u_group->aclug_notmember_group_size +=
							ACLUG_INCR_GROUPS_LIST;
			} 
			u_group->aclug_notmember_groups[ngr] = slapi_ch_strdup ( groupMember->member );
			slapi_log_error ( SLAPI_LOG_ACL, plugin_name, 
					"Adding Group (%s) ParentGroup (%s) to the NOT IN GROUP List\n",
					groupMember->member , parentGroup ? parentGroup->member: "NULL");

			groupMember = parentGroup;
			parentGroup = NULL;
		}
	} else if ( result == ACL_DONT_KNOW ) {

		/*
		 * We terminated the search without reaching a conclusion--so
		 * don't cache any info based on this evaluation.
		*/
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, "Evaluated ACL_DONT_KNOW\n");
	} 

	/* Unlock the group cache, we are done with updating */
	aclg_unlock_groupCache ( 2 /* writer */ );

	for (i=0; i <= info.lu_idx; i++) {
		groupMember = info.memberInfo[i];
		if ( NULL == groupMember ) continue;

		slapi_ch_free ( (void **) &groupMember->member );
		slapi_ch_free ( (void **) &groupMember );
	}

	/* free the pointer array.*/
	slapi_ch_free ( (void **) &info.memberInfo);
	return result;
}

/***************************************************************************
*
* acllas__handle_group_entry
*	
*	handler called. Compares the userdn value and determines if it's
*	a member of not.
*
* Input:
*
*
* Returns:
*
* Error Handling:
*
**************************************************************************/
static int
acllas__handle_group_entry (Slapi_Entry* e, void *callback_data)
{
	struct eval_info	*info;
 	Slapi_Attr		*currAttr, *nextAttr;
	char			*n_dn, *attrType;
	int				n;
	int				i;

	info = (struct eval_info *) callback_data;
	info->result = ACL_FALSE;
 
 	if (e == NULL) {
		return 0;
	}

	slapi_entry_first_attr ( e,  &currAttr);
	if ( NULL == currAttr ) return 0;

	slapi_attr_get_type ( currAttr, &attrType );
	if (NULL == attrType ) return 0;

	do {
		Slapi_Value *sval = NULL;
		const struct berval		*attrVal;

 		if ((strcasecmp (attrType, type_member) == 0) ||
 				(strcasecmp (attrType, type_uniquemember) == 0 ))  {

			i = slapi_attr_first_value ( currAttr,&sval );
			while ( i != -1 ) {
				struct member_info	*groupMember = NULL;
				attrVal = slapi_value_get_berval ( sval );
				n_dn = slapi_create_dn_string( attrVal->bv_val );
				if (NULL == n_dn) {
					slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
						"acllas__handle_group_entry: Invalid syntax: %s\n",
						attrVal->bv_val );
					return 0;
				}
				n = ++info->lu_idx;
				if (n < 0) {
					slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
						  "acllas__handle_group_entry: last member index lu_idx is overflown:%d: Too many group ACL members\n", n);
					return 0;
				}
				if (!(n % ACLLAS_MAX_GRP_MEMBER)) {
					struct member_info **orig_memberInfo = info->memberInfo;
					info->memberInfo = (struct member_info **)slapi_ch_realloc(
							(char *)info->memberInfo,
							(n + ACLLAS_MAX_GRP_MEMBER) *
							sizeof(struct member_info *));
					if (!info->memberInfo) {
						slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
										 "acllas__handle_group_entry: out of memory - could not allocate space for %d group members\n",
										 n + ACLLAS_MAX_GRP_MEMBER );
						info->memberInfo = orig_memberInfo;
						return 0;
					}
				}

				/* allocate the space for the member and attch it to the list */
				groupMember = (struct member_info *)slapi_ch_malloc(
								sizeof ( struct member_info ) );
				groupMember->member = n_dn;
				groupMember->parentId = info->c_idx;
				info->memberInfo[n] = groupMember;

				if (info->userDN && 
				    slapi_utf8casecmp((ACLUCHP)n_dn, (ACLUCHP)info->userDN) == 0) {
					info->result = ACL_TRUE;	
					return 0;
				}
				i = slapi_attr_next_value ( currAttr, i, &sval );
			}
		/* Evaluate Dynamic groups */
		} else if (strcasecmp ( attrType, type_memberURL) == 0) {
			char		*memberURL, *savURL;

			if (!info->userDN) continue;

			i= slapi_attr_first_value ( currAttr,&sval );
			while ( i != -1 ) {
			        attrVal = slapi_value_get_berval ( sval );
				/*
				 * memberURL may start with "ldap:///" or "ldap://host:port"
				 * ldap://localhost:11000/o=ace industry,c=us??
				 * or
				 * ldap:///o=ace industry,c=us??
				 */
				if (strncasecmp( attrVal->bv_val, "ldap://",7) == 0 ||
					strncasecmp( attrVal->bv_val, "ldaps://",8) == 0) {
					savURL = memberURL = 
							slapi_create_dn_string("%s", attrVal->bv_val);
					if (NULL == savURL) {
						slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
							"acllas__handle_group_entry: Invalid syntax: %s\n",
							attrVal->bv_val );
						return 0;
					}
					slapi_log_error( SLAPI_LOG_ACL, plugin_name,
						  "ACL Group Eval:MemberURL:%s\n", memberURL);
					info->result = acllas__client_match_URL (
									info->aclpb, 
									info->userDN,
									memberURL);
					slapi_ch_free ( (void**) &savURL);
					if (info->result == ACL_TRUE)
						return 0;
				} else {
					/* This means that the URL is ill-formed */
					slapi_log_error( SLAPI_LOG_ACL, plugin_name,
						"ACL Group Eval:Badly Formed MemberURL:%s\n", attrVal->bv_val);
				}
				i = slapi_attr_next_value ( currAttr, i, &sval );
			}
		/* Evaluate Fortezza groups */
		} else if ((strcasecmp (attrType, type_memberCert) == 0) ) {
			/* Do we have the certificate around */
			if (!info->clientCert) {
			      slapi_log_error( SLAPI_LOG_ACL, plugin_name,
				" acllas__handle_group_entry:Client Cert missing\n" );
				continue;
			}
			i = slapi_attr_first_value ( currAttr,&sval );
			while ( i != -1 ) {
			        attrVal = slapi_value_get_berval ( sval );
			 	if (ldapu_member_certificate_match (
							info->clientCert,
						        attrVal->bv_val) == LDAP_SUCCESS) {
					info->result = ACL_TRUE;
					return 0;
				}
				i = slapi_attr_next_value ( currAttr, i, &sval );
			}
		}
	
		attrType = NULL;	
		/* get the next attr */
		slapi_entry_next_attr ( e, currAttr, &nextAttr );
		if ( NULL == nextAttr ) break;
	
		currAttr = nextAttr;	
		slapi_attr_get_type ( currAttr, &attrType );
		
	} while ( NULL != attrType );

	return 0;
}
/***************************************************************************
*
* DS_LASGroupDnAttrEval
*
*
* Input:
*	attr_name	The string "groupdnattr" - in lower case.
*	comparator	CMP_OP_EQ or CMP_OP_NE only
*	attr_pattern	A comma-separated list of users
*	cachable	Always set to FALSE.
*	subject		Subject property list
*	resource        Resource property list
*	auth_info	Authentication info, if any
*
* Returns:
*	retcode	        The usual LAS return codes.
*
* Error Handling:
*	None.
*
**************************************************************************/
struct groupdnattr_info
{
        char            *attrName;      /* name of the attribute */
        int             numofGroups;    /* number of groups */
        char            **member;
};
int 
DS_LASGroupDnAttrEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator, 
		char *attr_pattern, int *cachable, void **LAS_cookie, 
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth)
{

	char			*s_attrName = NULL;
	char			*attrName;
	char			*ptr;
	int				matched;
	int				rc;
	int				len;
	Slapi_Attr 		*attr;
	int				levels[ACLLAS_MAX_LEVELS];
	int				numOflevels = 0;
	char			*n_currEntryDn = NULL;
	lasInfo			lasinfo;
	int				got_undefined = 0;

	if ( 0 !=  (rc = __acllas_setup (errp, attr_name, comparator, 0, /* Don't allow range comparators */
									attr_pattern,cachable,LAS_cookie,
									subject, resource, auth_info,global_auth,
									DS_LAS_GROUPDNATTR, "DS_LASGroupDnAttrEval", 
									&lasinfo )) ) {
		return LAS_EVAL_FAIL;
	}

	/* For anonymous client, the answer is XXX come back to this */
	if ( lasinfo.anomUser )
		return LAS_EVAL_FALSE;

	/* 
	** The groupdnAttr syntax is
	** 	groupdnattr = <attribute>
	**  Ex:
	**	groupdnattr = SIEmanager;
	**
	** The function of this LAS is to  find out if the client belongs
	** to any group  that is specified in the attr.
	*/
	attrName = attr_pattern;
	if (strstr(attrName, LDAP_URL_prefix)) {

		/* In this case "grppupdnattr="ldap:///base??attr" */

		if ((strstr (attrName, ACL_RULE_MACRO_DN_KEY) != NULL) ||
			(strstr (attrName, ACL_RULE_MACRO_DN_LEVELS_KEY) != NULL) ||
			(strstr (attrName, ACL_RULE_MACRO_ATTR_KEY) != NULL)) {			
				
				matched = aclutil_evaluate_macro( attrName, &lasinfo,
													ACL_EVAL_GROUPDNATTR);
		} else{

			matched = acllas__eval_memberGroupDnAttr(attrName, 
												lasinfo.resourceEntry, 
												lasinfo.clientDn, 
												lasinfo.aclpb);
		}
		if ( matched == ACL_DONT_KNOW) {
			got_undefined = 1;
		}
	} else {
		int	i;
		char	*n_groupdn;

		/* ignore leading/trailing whitespace */
		while(ldap_utf8isspace(attrName)) LDAP_UTF8INC(attrName);
		len = strlen(attrName);
		ptr = attrName+len-1;
		while(ptr >= attrName && ldap_utf8isspace(ptr)) {
			*ptr = '\0';
			LDAP_UTF8DEC(ptr);
		}

		slapi_log_error( SLAPI_LOG_ACL, plugin_name,"Attr:%s\n" , attrName);

		/* See if we have a  parent[2].attr" rule */
		if (strstr(attrName, "parent[") != NULL) {
			char	*word, *str, *next;

			numOflevels = 0;
			n_currEntryDn = slapi_entry_get_ndn ( lasinfo.resourceEntry ) ;
			s_attrName = attrName = slapi_ch_strdup ( attr_pattern );
			str = attrName;

			ldap_utf8strtok_r(str, "[],. ",&next);
			/* The first word is "parent[" and so it's not important */

			while ((word= ldap_utf8strtok_r(NULL, "[],.", &next)) != NULL) {
				if (ldap_utf8isdigit(word)) {
					while (word && ldap_utf8isspace(word)) LDAP_UTF8INC(word);
					if (numOflevels < ACLLAS_MAX_LEVELS) 
						levels[numOflevels++] = atoi (word);
					else  {
						/*
						 * Here, ignore the extra levels..it's really
						 * a syntax error which should have been ruled out at parse time
						*/
						slapi_log_error( SLAPI_LOG_FATAL, plugin_name, 
						"DS_LASGroupDnattr: Exceeded the ATTR LIMIT:%d: Ignoring extra levels\n",
						ACLLAS_MAX_LEVELS);
					}
				} else {
					/* Must be the attr name. We can goof of by 
					** having parent[1,2,a] but then you have to be
					** stupid to do that.
					*/
					char	*p = word;
					if (*--p == '.')  {
						attrName = word;
						break;
					}
				}
			}
		} else {
			levels[0] = 0;
			numOflevels = 1;
		} 
	
		matched = ACL_FALSE;
		for (i=0; i < numOflevels; i++) {
		    if ( levels[i] == 0 ) {
				Slapi_Value *sval=NULL;
				const struct berval		*attrVal;
				int attr_i;
				
				/*
			 	* For the add operation, the resource itself (level 0)
			 	* must never be allowed to grant access--
			 	* This is because access would be granted based on a value
		 	 	* of an attribute in the new entry--security hole.
				* XXX is this therefore FALSE or DONT_KNOW ?
				*/

				if ( lasinfo.aclpb->aclpb_optype == SLAPI_OPERATION_ADD) {
					slapi_log_error( SLAPI_LOG_ACL, plugin_name,
					"ACL info: groupdnAttr does not allow ADD permission at level 0.\n");
					got_undefined = 1;
					continue;
				}
				slapi_entry_attr_find ( lasinfo.resourceEntry, attrName, &attr);
				if ( !attr) continue;
				attr_i= slapi_attr_first_value ( attr,&sval );
				while ( attr_i != -1 ) {
			        attrVal = slapi_value_get_berval ( sval );
					n_groupdn = slapi_create_dn_string("%s", attrVal->bv_val);
					if (NULL == n_groupdn) {
						slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
							"DS_LASGroupDnAttrEval: Invalid syntax: %s\n",
							attrVal->bv_val );
						return 0;
					}
					matched =  acllas__user_ismember_of_group (
										lasinfo.aclpb, n_groupdn, lasinfo.clientDn,
										ACLLAS_CACHE_MEMBER_GROUPS, 
										lasinfo.aclpb->aclpb_clientcert);
					slapi_ch_free ( (void **) &n_groupdn);
					if (matched == ACL_TRUE ) {
						slapi_log_error( SLAPI_LOG_ACL, plugin_name,
						"groupdnattr matches at level (%d)\n", levels[i]);
						break;
					} else if ( matched == ACL_DONT_KNOW ) {
                		/* record this but keep going--maybe another group will evaluate to TRUE */
						got_undefined = 1;
					}
					attr_i= slapi_attr_next_value ( attr, attr_i, &sval );
				}
		    } else {
				char			*p_dn;
				struct groupdnattr_info	info;
				char			*attrs[2];
				int			j;

				info.numofGroups = 0;
				attrs[0] = info.attrName = attrName;
				attrs[1] = NULL;
			
				p_dn = acllas__dn_parent (n_currEntryDn, levels[i]);

				if (p_dn == NULL) continue;

				/* Use new search internal API */
				{

				Slapi_PBlock *aPb = slapi_pblock_new ();
				/*
			 	 * This search may NOT  be chained--if the user's definition is
				 * remote and the group is dynamic and the user entry
				 * changes then we would not notice--so don't go
				 * find the user entry in the first place.
				*/
				slapi_search_internal_set_pb (  aPb,
								p_dn,
								LDAP_SCOPE_BASE,
								"objectclass=*",
								&attrs[0],
								0,
								NULL /* controls */,
								NULL /* uniqueid */,
								aclplugin_get_identity (ACL_PLUGIN_IDENTITY),
								SLAPI_OP_FLAG_NEVER_CHAIN /* actions */);
				slapi_search_internal_callback_pb(aPb,
								  &info /* callback_data */,
								  NULL/* result_callback */,
								  acllas__get_members,
								  NULL /* referral_callback */);
				slapi_pblock_destroy (aPb);
				}

				if (info.numofGroups <= 0) {
					continue;
				}
				for (j=0; j <info.numofGroups; j++) {
					if (slapi_utf8casecmp((ACLUCHP)info.member[j], 
											(ACLUCHP)lasinfo.clientDn) == 0) {
						matched = ACL_TRUE;
						break;
					}
					matched = acllas__user_ismember_of_group (
							lasinfo.aclpb, info.member[j],
							lasinfo.clientDn, ACLLAS_CACHE_ALL_GROUPS,
							lasinfo.aclpb->aclpb_clientcert); 
					if (matched == ACL_TRUE) {
                		break;
            		} else if ( matched == ACL_DONT_KNOW ) {
                		/* record this but keep going--maybe another group will evaluate to TRUE */
                		got_undefined = 1;
            		}
				}
				/* Deallocate the member array and the member struct */
				for (j=0; j < info.numofGroups; j++)
					slapi_ch_free ((void **) &info.member[j]);
				slapi_ch_free ((void **) &info.member);
		   	}
		   	if (matched == ACL_TRUE) {
				slapi_log_error( SLAPI_LOG_ACL, plugin_name,
						"groupdnattr matches at level (%d)\n", levels[i]);
				break;
			} else if ( matched == ACL_DONT_KNOW ) {
                /* record this but keep going--maybe another group at another level
				 * will evaluate to TRUE.
				*/
                got_undefined = 1;
            }

		} /* NumofLevels */
	}
	if (s_attrName) slapi_ch_free ((void**) &s_attrName );

	/*
	 * If no terms were undefined, then evaluate as normal.
	 * If there was an undefined term, but another one was TRUE, then we also evaluate
	 * as normal.  Otherwise, the whole expression is UNDEFINED.
	*/
	if ( matched == ACL_TRUE || !got_undefined ) {
		if (comparator == CMP_OP_EQ) {
			rc = (matched == ACL_TRUE  ? LAS_EVAL_TRUE : LAS_EVAL_FALSE);
		} else {
			rc = (matched == ACL_TRUE ? LAS_EVAL_FALSE : LAS_EVAL_TRUE);
		}
	} else {
		rc = LAS_EVAL_FAIL;
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
			"Returning UNDEFINED for groupdnattr evaluation.\n");
	} 

	return rc;
}

/*
 * acllas__eval_memberGroupDnAttr
 *
 * return ACL_TRUE, ACL_FALSE or ACL_DONT_KNOW
 * 
 *	Inverse group evaluation. Find all the groups that the user is a 
 *	member of. Find all teh groups that contain those groups. Do an
 *	upward nested level search. By the end of it, we will know all the
 *	groups that the clinet is a member of under that search scope.
 *
 *	This model seems to be very fast if we have few groups at the
 *	leaf level.
 *
 */
static int
acllas__eval_memberGroupDnAttr (char *attrName, Slapi_Entry *e,
				char *n_clientdn, struct acl_pblock *aclpb)
{

	Slapi_Attr		*attr;
	char			*s, *p;
	char			*str, *s_str, *base, *groupattr = NULL;
	int				i,j,k,matched, enumerate_groups;
	aclUserGroup	*u_group;
	char			ebuf [ BUFSIZ ];
	Slapi_Value     *sval=NULL;
	const struct berval	*attrVal;

	/* Parse the URL -- getting the group attr and counting up '?'s.
	 * If there is no group attr and there are 3 '?' marks,
	 * we parse the URL with ldap_url_parse to get base dn and filter.
	 */ 
	s_str = str = slapi_ch_strdup(attrName);
	while (str && ldap_utf8isspace(str)) LDAP_UTF8INC( str );
	str +=8;
	s = strchr (str, '?');
	if (s) {
		p = s;
		p++;
		*s = '\0';
		base = str;
		s = strchr (p, '?');
		if (s) *s = '\0';

		groupattr = p;
	} else {
		slapi_ch_free ( (void **)&s_str );
		return ACL_FALSE;
	}

	if ( (u_group = aclg_get_usersGroup ( aclpb , n_clientdn )) == NULL) {
		slapi_log_error( SLAPI_LOG_ACL, plugin_name,
			"Failed to find/allocate a usergroup--aborting evaluation\n");
		slapi_ch_free ( (void **)&s_str );
		return(ACL_DONT_KNOW);
	}

	/*
	** First find out if we have already searched this base or 
	** if we are searching a subtree to an already enumerated base.
	*/
	enumerate_groups = 1;
	for (j=0; j < aclpb->aclpb_numof_bases; j++) {
		if (slapi_dn_issuffix(aclpb->aclpb_grpsearchbase[j], base)) {
			enumerate_groups = 0;
			break;
		}
	}
			
	
	/* See if we have already enumerated all the groups which the
	** client is a member of.
	*/
	if (enumerate_groups) {
		char			filter_str[BUFSIZ];
		char			*attrs[3];
		struct eval_info	info = {0};
		char			*curMemberDn;
		int			Done = 0;
		int			ngr, tt;
		char		*normed = NULL;

		/* Add the scope to the list of scopes */
		if (aclpb->aclpb_numof_bases >= (aclpb->aclpb_grpsearchbase_size-1)) {
			aclpb->aclpb_grpsearchbase = (char **)
					slapi_ch_realloc ( 
					   (void *) aclpb->aclpb_grpsearchbase,
					   (aclpb->aclpb_grpsearchbase_size +
					         ACLPB_INCR_BASES) * 
					    sizeof (char *));
			aclpb->aclpb_grpsearchbase_size += ACLPB_INCR_BASES;
		}
		normed = slapi_create_dn_string("%s", base);
		if (NULL == normed) {
			slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
						"acllas__eval_memberGroupDnAttr: Invalid syntax: %s\n",
						base );
			slapi_ch_free ( (void **)&s_str );
			return ACL_FALSE;
		}
		aclpb->aclpb_grpsearchbase[aclpb->aclpb_numof_bases++] = normed;
		/* Set up info to do a search */
		attrs[0] = type_member;
		attrs[1] = type_uniquemember;
		attrs[2] = NULL;

		info.c_idx = info.lu_idx = 0;
		info.member = 
			(char **) slapi_ch_malloc (ACLLAS_MAX_GRP_MEMBER * sizeof(char *));
		curMemberDn = n_clientdn;

		while (!Done) {
			char *filter_str_ptr = &filter_str[0];
			char *new_filter_str = NULL;
			int lenf = strlen(curMemberDn)<<1;

			if (lenf > (BUFSIZ - 28)) { /* 28 for "(|(uniquemember=%s)(member=%s))" */
				new_filter_str = slapi_ch_malloc(lenf + 28);
				filter_str_ptr = new_filter_str;
			}

			/*
			** Search the db for groups that the client is a member of.
			** Once found cache it. cache only unique groups.
			*/
			tt = info.lu_idx;
			sprintf (filter_str_ptr,"(|(uniquemember=%s)(member=%s))", 
						curMemberDn, curMemberDn); 

			/* Use new search internal API */
			{
				Slapi_PBlock *aPb = slapi_pblock_new ();
				/*
		 		 * This search may NOT be chained--we demand that group
		 		 * definition be local.
				*/			
				slapi_search_internal_set_pb (  aPb,
								base,
								LDAP_SCOPE_SUBTREE,
								filter_str_ptr,
								&attrs[0],
								0,
								NULL /* controls */,
								NULL /* uniqueid */,
								aclplugin_get_identity (ACL_PLUGIN_IDENTITY),
								SLAPI_OP_FLAG_NEVER_CHAIN /* actions */);	
				slapi_search_internal_callback_pb(aPb,
								  &info /* callback_data */,
								  NULL/* result_callback */,
								  acllas__add_allgroups,
								  NULL /* referral_callback */);
				slapi_pblock_destroy (aPb);
			}

			if (new_filter_str) slapi_ch_free((void **) &new_filter_str);

			if (tt == info.lu_idx) {
				slapi_log_error( SLAPI_LOG_ACL, plugin_name, "currDn:(%s) \n\tNO MEMBER ADDED\n", 
								ACL_ESCAPE_STRING_WITH_PUNCTUATION (curMemberDn, ebuf));
			} else {
				for (i=tt; i < info.lu_idx; i++)
					slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
						"currDn:(%s) \n\tADDED MEMBER[%d]=%s\n", 
						ACL_ESCAPE_STRING_WITH_PUNCTUATION (curMemberDn, ebuf), i, info.member[i]);
			}

			if (info.c_idx >= info.lu_idx) {
				for (i=0; i < info.lu_idx; i++) {
				   int already_cached = 0;
				   for (j=0; j < u_group->aclug_numof_member_group; 
									j++){
					if (slapi_utf8casecmp(
								(ACLUCHP)info.member[i],
								(ACLUCHP)u_group->aclug_member_groups[j]) == 0) {
						slapi_ch_free ((void **) &info.member[i] );
						info.member[i] = NULL;
						already_cached = 1;
						break;
					}
					
                                   }

				   if (already_cached) continue;

				   ngr = u_group->aclug_numof_member_group++;
				   if (u_group->aclug_numof_member_group >= 
					   u_group->aclug_member_group_size){
					   u_group->aclug_member_groups =
					       (char **) slapi_ch_realloc (
				                 (void *) u_group->aclug_member_groups,
				                 (u_group->aclug_member_group_size +
				                   ACLUG_INCR_GROUPS_LIST) * sizeof(char *));

					   u_group->aclug_member_group_size += 
							    ACLUG_INCR_GROUPS_LIST;
				   }
				   u_group->aclug_member_groups[ngr] = info.member[i];
				   info.member[i] = NULL;
				}
				slapi_ch_free ((void **) &info.member);
				Done = 1;
			} else {
				curMemberDn = info.member[info.c_idx];
				info.c_idx++;
			}
		}
	}

	for (j=0; j < u_group->aclug_numof_member_group; j++)
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
				"acllas__eval_memberGroupDnAttr:GROUP[%d] IN CACHE:%s\n", 
					j, ACL_ESCAPE_STRING_WITH_PUNCTUATION (u_group->aclug_member_groups[j], ebuf));

	matched = ACL_FALSE;
	slapi_entry_attr_find( e, groupattr, &attr);
	if (attr == NULL) {
		slapi_ch_free ( (void **)&s_str );
		return ACL_FALSE;
	}
	k = slapi_attr_first_value ( attr,&sval );
	while ( k != -1 ) {
        char *n_attrval;
		attrVal = slapi_value_get_berval ( sval );
		n_attrval = slapi_create_dn_string("%s", attrVal->bv_val);
		if (NULL == n_attrval) {
			slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
						"acllas__eval_memberGroupDnAttr: Invalid syntax: %s\n",
						attrVal->bv_val );
			slapi_ch_free ( (void **)&s_str );
			return ACL_FALSE;
		}

		/*  We support: The attribute value can be a USER or a GROUP.
		** Let's compare with the client, thi might be just an user. If it is not
		** then we test it against the list of groups.
		*/
		if (slapi_utf8casecmp ((ACLUCHP)n_attrval, (ACLUCHP)n_clientdn) == 0 ) {
			matched = ACL_TRUE;
			slapi_ch_free ( (void **)&n_attrval );
			break;
		}
		for (j=0; j <u_group->aclug_numof_member_group; j++) {
			if ( slapi_utf8casecmp((ACLUCHP)n_attrval, 
									(ACLUCHP)u_group->aclug_member_groups[j]) == 0) {
				matched = ACL_TRUE;
				break;
			}
		}
		slapi_ch_free ( (void **)&n_attrval );
		if (matched == ACL_TRUE) break;
		k= slapi_attr_next_value ( attr, k, &sval );
	}
	slapi_ch_free ( (void **)&s_str );
	return matched;
}

static int
acllas__add_allgroups (Slapi_Entry* e, void *callback_data)
{
	int						i, n, m;
	struct eval_info        *info;
	char					*n_dn;

	info = (struct eval_info *) callback_data;

	/*
	** Once we are here means this is a valid group. First see
	** If we have already seen this group. If not, add it to the
	** member list.
	*/
	n_dn = slapi_ch_strdup ( slapi_entry_get_ndn ( e ) );
	for (i=0; i < info->lu_idx; i++) {
		if (slapi_utf8casecmp((ACLUCHP)n_dn, (ACLUCHP)info->member[i]) == 0) {
			slapi_ch_free ( (void **) &n_dn);
			return 0;
		}
	}

	m = info->lu_idx;
	n = ++info->lu_idx;
	if (!(n % ACLLAS_MAX_GRP_MEMBER)) {
		info->member = (char **) slapi_ch_realloc (
					(void *) info->member,
					(n+ACLLAS_MAX_GRP_MEMBER) * sizeof(char *));
	}
	info->member[m] = n_dn;
	return 0;
}
/*
 * 
 * acllas__dn_parent
 * 
 *   This code should belong to dn.c. However this is specific to acl and I had 
 *   2 choices 1) create a new API or 2) reuse the slapi_dN_parent
 *
 *   Returns a ptr to the parent based on the level.
 *
 */
#define DNSEPARATOR(c)  (c == ',' || c == ';')
static char*
acllas__dn_parent( char *dn, int level)
{
	char	*s, *dnstr;
	int	inquote;
	int	curLevel;
	int	lastLoop = 0;

	if ( dn == NULL || *dn == '\0' ) {
		return( NULL );
	}

	/* An X.500-style name, which looks like  foo=bar,sha=baz,... */
	/* Do we have any dn seprator or not */
	if ((strchr(dn,',') == NULL) && (strchr(dn,';') == NULL))
		return (NULL);

	inquote = 0;
	curLevel = 1;
	dnstr = dn;
	while ( curLevel <= level) {
		if (lastLoop) break;
		if (curLevel == level) lastLoop = 1;
		for ( s = dnstr; *s; s++ ) {
			if ( *s == '\\' ) {
				if ( *(s + 1) )
					s++;
				continue;
			}
			if ( inquote ) {
				if ( *s == '"' )
					inquote = 0;
			} else {
				if ( *s == '"' )
					inquote = 1;
				else if ( DNSEPARATOR( *s ) ) {
					if (curLevel == level)
						return(  s + 1 );
					dnstr = s + 1;
					curLevel++;
					break;
				}
			}
		}
		if ( *s == '\0') {
			/* Got to the end of the string without reaching level,
			 * so return NULL.
			*/
			return(NULL);
		}
	}

	return( NULL );
}
/*
 * acllas__verify_client
 *
 * returns 1 if the attribute exists in the entry and
 * it's value is equal to the client Dn.
 * If the attribute is not in the entry, or it is and the
 * value differs from the clientDn then returns FALSE.
 *  
 *  Verify if client's DN is stored in the attrbute or not.
 *  This is a handler from a search being done at
 *  DS_LASUserDnAttrEval().
 *
 */
static int
acllas__verify_client (Slapi_Entry* e, void *callback_data)
{

	Slapi_Attr		*attr;
	char			*val;
	struct userdnattr_info *info;
	Slapi_Value             *sval;
	const struct berval		*attrVal;
	int i;

	info = (struct userdnattr_info *) callback_data;
	
	slapi_entry_attr_find( e, info->attr, &attr);
	if (attr == NULL) return 0;

	i = slapi_attr_first_value ( attr,&sval );
	while ( i != -1 ) {
		attrVal = slapi_value_get_berval ( sval );
		val = slapi_create_dn_string("%s", attrVal->bv_val);
		if (NULL == val) {
			slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
							"acllas__verify_client: Invalid syntax: %s\n",
							attrVal->bv_val );
			return 0;
		}

		if (slapi_utf8casecmp((ACLUCHP)val, (ACLUCHP)info->clientdn ) == 0) {
			info->result = 1;
			slapi_ch_free ( (void **) &val);
			return 0;
		}
		slapi_ch_free ( (void **) &val);
		i = slapi_attr_next_value ( attr, i, &sval );
	}
	return 0;
}

/*
 * acllas__verify_ldapurl
 *
 * returns 1 if the attribute exists in the entry and
 * it's value is equal to the client Dn.
 * If the attribute is not in the entry, or it is and the
 * value differs from the clientDn then returns FALSE.
 *  
 *  Verify if client's entry includes the attribute value that
 *  matches the filter in LDAPURL
 *  This is a handler from a search being done at DS_LASLdapUrlAttrEval().
 *
 */
static int
acllas__verify_ldapurl(Slapi_Entry* e, void *callback_data)
{

	Slapi_Attr		*attr;
	struct userdnattr_info *info;
	Slapi_Value             *sval;
	const struct berval		*attrVal;
	int rc;

	info = (struct userdnattr_info *) callback_data;
	info->result = ACL_FALSE;

	rc = slapi_entry_attr_find( e, info->attr, &attr);
	if (rc != 0 || attr == NULL) {
		return 0;
	}

	rc = slapi_attr_first_value ( attr, &sval );
	if ( rc == -1 ) {
		return 0;
	}

	while (rc != -1 && sval != NULL) {
		attrVal = slapi_value_get_berval ( sval );
		info->result = acllas__client_match_URL ( info->aclpb, 
												  info->clientdn, 
							     				  attrVal->bv_val);
		if ( info->result == ACL_TRUE ) {
				return 0;
		}
		rc = slapi_attr_next_value ( attr, rc, &sval );
	}
	return 0;
}

/*
 *
 * acllas__get_members
 *
 *	Collects all the values of the specified attribute which should be group names.
 */
static int
acllas__get_members (Slapi_Entry* e, void *callback_data)
{

	Slapi_Attr		*attr;
	struct groupdnattr_info	*info;
	Slapi_Value             *sval=NULL;
	const struct berval		*attrVal;
	int			i;

	info = (struct groupdnattr_info *) callback_data;
	slapi_entry_attr_find (e, info->attrName, &attr);
	if ( !attr ) return 0;

	slapi_attr_get_numvalues ( attr, &info->numofGroups );
	    
	info->member = (char **) slapi_ch_malloc (info->numofGroups * sizeof(char *));
	i = slapi_attr_first_value ( attr,&sval );
	while ( i != -1 ) {
	    attrVal =slapi_value_get_berval ( sval );
	    info->member[i] = slapi_create_dn_string ("%s", attrVal->bv_val);
		if (NULL == info->member[i]) {
			slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
							"acllas__get_members: Invalid syntax: %s\n",
							attrVal->bv_val );
		}
	    i = slapi_attr_next_value ( attr, i, &sval );
	}
	return 0;	
}

/*
 * DS_LASUserAttrEval
 *	LAS to evaluate the userattr rule
 *
 *   userAttr = "attrName#Type"
 *
 *   <Type> ::= "USERDN" | "GROUPDN" | "ROLEDN" | "LDAPURL" | <value>
 *   <value>::== <any printable String>
 *
 *   Example:
 *		userAttr = "manager#USERDN"    --- same as userdnattr
 *		userAttr = "owner#GROUPDN"     --- same as groupdnattr
 *				 = "ldap:///o=sun.com?owner#GROUPDN 
 * 		userAttr = "attr#ROLEDN"       --- The value of attr contains a roledn
 * 		userAttr = "myattr#LDAPURL"    --- The value contains a LDAP URL
 *											which can have scope and filter
 *											bits.
 *	    userAttr = "OU#Directory Server"
 * 									--- In this case the client's OU and the
 * 									    resource entry's OU must have
 * 										"Directory Server" value.
 *
 * Returns:
 *	retcode	        The usual LAS return codes.
 */
int 
DS_LASUserAttrEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator, 
		char *attr_pattern, int *cachable, void **LAS_cookie, 
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth)
{

	char			*attrName;
	char			*attrValue = NULL;
	int				rc;
	int				matched = ACL_FALSE;
	char			*p;
	lasInfo			lasinfo;
	int				got_undefined = 0;

	if ( 0 !=  (rc = __acllas_setup (errp, attr_name, comparator, 0, /* Don't allow range comparators */
									attr_pattern,cachable,LAS_cookie,
									subject, resource, auth_info,global_auth,
									DS_LAS_USERATTR, "DS_LASUserAttrEval", 
									&lasinfo )) ) {
		return LAS_EVAL_FAIL;
	}

	/* Which rule are we evaluating ? */
	attrName = slapi_ch_strdup (attr_pattern );
	if ( NULL  == (p = strchr ( attrName, '#' ))) {
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
			  "DS_LASUserAttrEval:Invalid value(%s)\n", attr_pattern);
		slapi_ch_free ( (void **) &attrName );
		return LAS_EVAL_FAIL;
	}
	attrValue = p;
	attrValue++; /* skip the # */
	*p = '\0';  /* null terminate the attr name */

	if  ( 0 == strncasecmp ( attrValue, "USERDN", 6)) {
		matched = DS_LASUserDnAttrEval (errp,DS_LAS_USERDNATTR, comparator,
							attrName, cachable, LAS_cookie,
							subject, resource, auth_info, global_auth);
		goto done_las;
	} else if  ( 0 == strncasecmp ( attrValue, "GROUPDN", 7)) {
		matched = DS_LASGroupDnAttrEval (errp,DS_LAS_GROUPDNATTR, comparator,
							attrName, cachable, LAS_cookie,
							subject, resource, auth_info, global_auth);
		goto done_las;
	} else if  ( 0 == strncasecmp ( attrValue, "LDAPURL", 7) ) {
		matched = DS_LASLdapUrlAttrEval(errp, DS_LAS_USERATTR, comparator,
							attrName, cachable, LAS_cookie,
							subject, resource, auth_info, global_auth, lasinfo);
		goto done_las;
	} else if  ( 0 == strncasecmp ( attrValue, "ROLEDN", 6)) {
		matched = DS_LASRoleDnAttrEval (errp,DS_LAS_ROLEDN, comparator,
							attrName, cachable, LAS_cookie,
							subject, resource, auth_info, global_auth);
		goto done_las;
	}

	if ( lasinfo.aclpb && ( NULL == lasinfo.aclpb->aclpb_client_entry )) {
		/* SD 00/16/03 pass NULL in case the req is chained */
		char **attrs=NULL;

		/* Use new search internal API */
		Slapi_PBlock *aPb = slapi_pblock_new ();
		/*
		 * This search may be chained if chaining for ACL is
		 * is enabled in the backend and the entry is in
		 * a chained backend.
		 */
		slapi_search_internal_set_pb (  aPb,
						lasinfo.clientDn,
						LDAP_SCOPE_BASE,
						"objectclass=*",
						attrs,
						0,
						NULL /* controls */,
						NULL /* uniqueid */,
						aclplugin_get_identity (ACL_PLUGIN_IDENTITY),
						0 /* actions */);
		slapi_search_internal_callback_pb(aPb,
						  lasinfo.aclpb /* callback_data */, 
						  NULL/* result_callback */,
						  acllas__handle_client_search,
						  NULL /* referral_callback */);
		slapi_pblock_destroy (aPb);
						
	}

	slapi_log_error ( SLAPI_LOG_ACL, plugin_name, 
				"DS_LASUserAttrEval: AttrName:%s, attrVal:%s\n", attrName, attrValue );

	/*
	 * Here it's the userAttr = "OU#Directory Server" case.
	 * Allocate the Slapi_Value on the stack and init it by reference
	 * to avoid having to malloc and free memory.
	*/
	Slapi_Value v;
	
	slapi_value_init_string_passin(&v, attrValue);
	rc = slapi_entry_attr_has_syntax_value ( lasinfo.resourceEntry, attrName,
									 &v );
	if (rc) {
	   rc = slapi_entry_attr_has_syntax_value ( 
									lasinfo.aclpb->aclpb_client_entry, 
									attrName, &v );
		if (rc) matched = ACL_TRUE;
	}
	/* Nothing to free--cool */				

	/*
	 * Find out what the result is, in
	 * this case matched is one of ACL_TRUE, ACL_FALSE or ACL_DONT_KNOW
	 * and got_undefined says whether a logical term evaluated to ACL_DONT_KNOW.
	 *
	 */
	if ( matched == ACL_TRUE || !got_undefined) {
		if (comparator == CMP_OP_EQ) {
			rc = (matched == ACL_TRUE ? LAS_EVAL_TRUE : LAS_EVAL_FALSE);
		} else {
			rc = (matched == ACL_TRUE ? LAS_EVAL_FALSE : LAS_EVAL_TRUE);
		}
	} else {
		rc = LAS_EVAL_FAIL;
	}

	slapi_ch_free ( (void **) &attrName );
	return rc;

done_las:
	/*
	 * In this case matched is already LAS_EVAL_TRUE or LAS_EVAL_FALSE or
	 * LAS_EVAL_FAIL.
	*/
	if ( matched != LAS_EVAL_FAIL ) {
		if (comparator == CMP_OP_EQ) {
			rc = matched;
		} else {
			rc = (matched == LAS_EVAL_TRUE ? LAS_EVAL_FALSE : LAS_EVAL_TRUE);
		}
	} 

	slapi_ch_free ( (void **) &attrName );
	return rc;
}

/*
 * acllas__client_match_URL
 * 	Match a client to a URL.
 *
 * Returns:
 *	ACL_TRUE 		- matched the URL
 *	ACL_FALSE		- Sorry; no match
 *
 */
static int
acllas__client_match_URL (struct acl_pblock *aclpb, char *n_clientdn, char *url )
{

	LDAPURLDesc	*ludp = NULL;
	int		rc = 0;
	Slapi_Filter	*f = NULL;
	char *rawdn = NULL;
	char *dn = NULL;
	size_t dnlen = 0;
	char *p = NULL;
	char *normed = NULL;
	/* ldap(s)://host:port/suffix?attrs?scope?filter */
	const size_t 	LDAP_URL_prefix_len = strlen(LDAP_URL_prefix_core);
	const size_t 	LDAPS_URL_prefix_len = strlen(LDAPS_URL_prefix_core);
	size_t 	prefix_len = 0;
	char Q = '?';
	char *hostport = NULL;
	int result = ACL_FALSE;

	if ( NULL == aclpb ) {
		slapi_log_error (SLAPI_LOG_ACL, plugin_name, 
			"acllas__client_match_URL: NULL acl pblock\n");
		return ACL_FALSE;
	}

	/* Get the client's entry if we don't have already */
	if ( NULL == aclpb->aclpb_client_entry ) {
		/* SD 00/16/03 Get every attr in case req chained */
		char **attrs=NULL;

		/* Use new search internal API */
		Slapi_PBlock *  aPb = slapi_pblock_new ();
		/*
		 * This search may be chained if chaining for ACL is
		 * is enabled in the backend and the entry is in
		 * a chained backend.
		*/	
		slapi_search_internal_set_pb (  aPb,
						n_clientdn,
						LDAP_SCOPE_BASE,
						"objectclass=*",
						attrs,
						0,
						NULL /* controls */,
						NULL /* uniqueid */,
						aclplugin_get_identity (ACL_PLUGIN_IDENTITY),
						0 /* actions */);
		slapi_search_internal_callback_pb(aPb,
						  aclpb /* callback_data */,
						  NULL/* result_callback */,
						  acllas__handle_client_search,
						  NULL /* referral_callback */);
		slapi_pblock_destroy (aPb);
	}

	if ( NULL == aclpb->aclpb_client_entry ) {
		slapi_log_error (SLAPI_LOG_ACL, plugin_name, 
			"acllas__client_match_URL: Unable to get client's entry\n");
		goto done;
	}

	/* DN potion of URL must be normalized before calling ldap_url_parse.
	 * lud_dn is pointing at the middle of lud_string.
	 * lud_dn won't be freed in ldap_free_urldesc.
	 */
	/* remove the "ldap{s}:///" part */
	if (strncasecmp (url, LDAP_URL_prefix, LDAP_URL_prefix_len) == 0) {
		prefix_len = LDAP_URL_prefix_len;
	} else if (strncasecmp (url, LDAPS_URL_prefix, LDAPS_URL_prefix_len) == 0) {
		prefix_len = LDAPS_URL_prefix_len;
	} else {
		slapi_log_error (SLAPI_LOG_ACL, plugin_name, 
			"acllas__client_match_URL: url %s does not have a recognized ldap protocol prefix\n", url);
		goto done;
	}
	rawdn = url + prefix_len; /* ldap(s)://host:port/... or ldap(s):///... */
	                          /* rawdn at  ^             or           ^    */
	/* let rawdn point the suffix */
	if ('/' == *(rawdn+1)) { /* ldap(s):/// */
		rawdn += 2;
	} else {
		char *tmpp = rawdn;
		rawdn = strchr(tmpp, '/');
		size_t hostport_len = 0;
		if (NULL == rawdn) {
			slapi_log_error (SLAPI_LOG_ACL, plugin_name, 
				"acllas__client_match_URL: url %s does not have a valid ldap protocol prefix\n", url);
			goto done;
		}
		hostport_len = ++rawdn - tmpp; /* ldap(s)://host:port/... */
		                               /*           <-------->    */
		hostport = (char *)slapi_ch_malloc(hostport_len + 1);
		memcpy(hostport, tmpp, hostport_len);
		*(hostport+hostport_len) = '\0';
	}
	p = strchr(rawdn, Q);
	if (p) { 
		/* url has scope and/or filter: ldap(s):///suffix?attr?scope?filter */
		*p = '\0'; /* null terminate the dn part of rawdn */
	}
	rc = slapi_dn_normalize_ext(rawdn, 0, &dn, &dnlen);
	if (rc < 0) {
		slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
						 "acllas__client_match_URL: error normalizing dn [%s] part of URL [%s]\n", rawdn, url);
		goto done;
	} else if (rc == 0) { /* url is passed in and not terminated with NULL*/
		*(dn + dnlen) = '\0';
	}
	/* else - rawdn normalized in place */
	normed = slapi_ch_smprintf("%s%s%s%s%s", 
			 (prefix_len==LDAP_URL_prefix_len)?
			  LDAP_URL_prefix_core:LDAPS_URL_prefix_core,
							   hostport?hostport:"", dn, p?"?":"",p?p+1:"");
	if (p) {
		*p = Q; /* put the Q back in rawdn which will un-null terminate the DN part */
	}
	if (rc > 0) {
		/* dn was allocated in slapi_dn_normalize_ext */
		slapi_ch_free_string(&dn);
	}
	rc = slapi_ldap_url_parse(normed, &ludp, 1, NULL);
	if (rc) {
		slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
						 "acllas__client_match_URL: url [%s] is invalid: %d (%s)\n",
						 normed, rc, slapi_urlparse_err2string(rc));
		goto done;
	}
	if ( ( NULL == ludp->lud_dn) || ( NULL == ludp->lud_filter) ) {
		slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
						 "acllas__client_match_URL: url [%s] has no base dn [%s] or filter [%s]\n",
						 normed,
						 NULL == ludp->lud_dn ? "null" : ludp->lud_dn,
						 NULL == ludp->lud_filter ? "null" :  ludp->lud_filter );
		goto done;
	}

	/* Check the scope */
	if ( ludp->lud_scope == LDAP_SCOPE_SUBTREE ) {
		if (!slapi_dn_issuffix(n_clientdn, ludp->lud_dn)) {
			slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
							 "acllas__client_match_URL: url [%s] scope is subtree but dn [%s] "
							 "is not a suffix of [%s]\n",
							 normed, ludp->lud_dn, n_clientdn );
			goto done;
		}
	} else if ( ludp->lud_scope == LDAP_SCOPE_ONELEVEL ) {
		char    *parent = slapi_dn_parent (n_clientdn);

		if (slapi_utf8casecmp ((ACLUCHP)parent, (ACLUCHP)ludp->lud_dn) != 0 ) {
			slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
							 "acllas__client_match_URL: url [%s] scope is onelevel but dn [%s] "
							 "is not a direct child of [%s]\n",
							 normed, ludp->lud_dn, parent );
			slapi_ch_free_string(&parent);
			goto done;
		}
		slapi_ch_free_string(&parent);
	} else  { /* default */
		if (slapi_utf8casecmp ( (ACLUCHP)n_clientdn, (ACLUCHP)ludp->lud_dn) != 0 ) {
			slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
							 "acllas__client_match_URL: url [%s] scope is base but dn [%s] "
							 "does not match [%s]\n",
							 normed, ludp->lud_dn, n_clientdn );
			goto done;
		}

	} 

	/* Convert the filter string */
	f = slapi_str2filter ( ludp->lud_filter );

	if (ludp->lud_filter && (f == NULL)) { /* bogus filter */
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
						"DS_LASUserAttrEval: The member URL [%s] search filter in entry [%s] is not valid: [%s]\n",
						normed, n_clientdn, ludp->lud_filter);
		goto done;
    }

	result = ACL_TRUE;
	if (f && (0 != slapi_vattr_filter_test ( aclpb->aclpb_pblock, 
				aclpb->aclpb_client_entry, f, 0 /* no acces chk */ )))
		result = ACL_FALSE;

done:
	slapi_ch_free_string(&hostport);
	ldap_free_urldesc( ludp );
	slapi_ch_free_string(&normed);
	slapi_filter_free ( f, 1 ) ;

	return result;
}
static int
acllas__handle_client_search ( Slapi_Entry *e, void *callback_data )
{
        struct acl_pblock *aclpb = (struct acl_pblock *) callback_data;

        /* If we are here means we have found the entry */
	if ( NULL == aclpb-> aclpb_client_entry)
		aclpb->aclpb_client_entry = slapi_entry_dup ( e );
        return 0;
}
/*
*
* Do all the necessary setup for all the
* LASes.
* It will only fail if it's passed garbage (which should not happen) or
* if the data it needs to stock the lasinfo is not available, which
* also should not happen.
*
*
* Return value: 0 or one of these
* 	#define LAS_EVAL_TRUE       -1
* 	#define LAS_EVAL_FALSE      -2
* 	#define LAS_EVAL_DECLINE    -3
* 	#define LAS_EVAL_FAIL       -4
* 	#define LAS_EVAL_INVALID    -5
*/

static int
__acllas_setup ( NSErr_t *errp, char *attr_name, CmpOp_t comparator,
		int allow_range, char *attr_pattern, int *cachable, void **LAS_cookie,
        PList_t subject, PList_t resource, PList_t auth_info,
        PList_t global_auth, char *lasType, char*lasName, lasInfo *linfo)
{

	int		rc;
	memset ( linfo, 0, sizeof ( lasInfo) );

  	*cachable = 0;
  	*LAS_cookie = (void *)0;

	if (strcmp(attr_name, lasType) != 0) {
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
			  "%s:Invalid LAS(%s)\n", lasName, attr_name);
		return LAS_EVAL_INVALID;
	}

	/* Validate the comparator */
	if (allow_range && (comparator != CMP_OP_EQ) && (comparator != CMP_OP_NE) &&
	    (comparator != CMP_OP_GT) && (comparator != CMP_OP_LT) &&
	    (comparator != CMP_OP_GE) && (comparator != CMP_OP_LE)) {
		slapi_log_error( SLAPI_LOG_ACL, plugin_name,
			"%s:Invalid comparator(%d)\n", lasName, (int)comparator);
		return LAS_EVAL_INVALID;
	} else if (!allow_range && (comparator != CMP_OP_EQ) && (comparator != CMP_OP_NE)) {
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
			"%s:Invalid comparator(%d)\n", lasName, (int)comparator);
		return LAS_EVAL_INVALID;
	}

    
	/* Get the client DN */
	rc = ACL_GetAttribute(errp, DS_ATTR_USERDN, (void **)&linfo->clientDn,
			      subject, resource, auth_info, global_auth);

	if ( rc != LAS_EVAL_TRUE ) {
		acl_print_acllib_err(errp, NULL);
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
		    "%s:Unable to get the clientdn attribute(%d)\n",lasName, rc);
		return LAS_EVAL_FAIL;
	}

	/* Check if we have a user or not */
	if (linfo->clientDn) {
		/* See if it's a anonymous user */
		if (*(linfo->clientDn) == '\0') 
			linfo->anomUser = ACL_TRUE;
	} else {
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
			   "%s: No user\n",lasName);
		return LAS_EVAL_FAIL;
	}

	if ((rc = PListFindValue(subject, DS_ATTR_ENTRY, 
					(void **)&linfo->resourceEntry, NULL)) < 0)	{
		acl_print_acllib_err(errp, NULL);
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
		          "%s:Unable to get the Slapi_Entry attr(%d)\n",lasName, rc);
		return LAS_EVAL_FAIL;
	}

	/* Get ACLPB */
	rc = ACL_GetAttribute(errp, DS_PROP_ACLPB, (void **)&linfo->aclpb,
				subject, resource, auth_info, global_auth);
	if ( rc != LAS_EVAL_TRUE ) {
		acl_print_acllib_err(errp, NULL);
		slapi_log_error( SLAPI_LOG_ACL, plugin_name,
			"%s:Unable to get the ACLPB(%d)\n", lasName, rc);
		return LAS_EVAL_FAIL;
	}
	if (NULL == attr_pattern ) {
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
		          "%s:No rule value in the ACL\n", lasName);
	
		return LAS_EVAL_FAIL;
	}
	/* get the  authentication type */
	if ((rc = PListFindValue(subject, DS_ATTR_AUTHTYPE, 
					(void **)&linfo->authType, NULL)) < 0) {
		acl_print_acllib_err(errp, NULL);
		slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
		          "%s:Unable to get the auth type(%d)\n", lasName, rc);
		return LAS_EVAL_FAIL;
	}

	/* get the SSF */
	if ((rc = PListFindValue(subject, DS_ATTR_SSF,
					(void **)&linfo->ssf, NULL)) < 0) {
		acl_print_acllib_err(errp, NULL);
		slapi_log_error( SLAPI_LOG_ACL, plugin_name,
			"%s:Unable to get the ssf(%d)\n", lasName, rc);
	}
	return 0;	
}

/*
 * See if clientDN has role roleDN.
 * Here we know the user is not anon and that the role
 * is not the anyone role ie. it's actually worth invoking the roles code.
*/

static int acllas__user_has_role( struct acl_pblock *aclpb,
								  Slapi_DN *roleDN, Slapi_DN *clientDn) {

	int present = 0;

	if ( NULL == aclpb ) {
		slapi_log_error (  SLAPI_LOG_ACL, plugin_name, 
			"acllas__user_has_role: NULL acl pblock\n");
		return ACL_FALSE;
	}

	/* Get the client's entry if we don't have already */
	if ( NULL == aclpb->aclpb_client_entry ) {
		/* SD 00/16/03 Get every attr in case req chained */
		char **attrs=NULL;

		/* Use new search internal API */
		Slapi_PBlock *  aPb = slapi_pblock_new ();
		/*
		 * This search may NOT be chained--the user and the role definition
		 * must be co-located (chaining is not supported for the roles
		 * plugin in 5.0
		*/
		slapi_search_internal_set_pb (  aPb,
						slapi_sdn_get_ndn(clientDn),
						LDAP_SCOPE_BASE,
						"objectclass=*",
						attrs,
						0,
						NULL /* controls */,
						NULL /* uniqueid */,
						aclplugin_get_identity (ACL_PLUGIN_IDENTITY),
						SLAPI_OP_FLAG_NEVER_CHAIN /* actions */);
		slapi_search_internal_callback_pb(aPb,
						  aclpb /* callback_data */,
						  NULL/* result_callback */,
						  acllas__handle_client_search,
						  NULL /* referral_callback */);
		slapi_pblock_destroy (aPb);
	}

	if ( NULL == aclpb->aclpb_client_entry ) {
		slapi_log_error (  SLAPI_LOG_ACL, plugin_name, 
			"acllas__user_has_role: Unable to get client's entry\n");
		return ACL_FALSE;
	}

	/* If the client has the role then it's a match, otherwise no */

	slapi_role_check( aclpb->aclpb_client_entry, roleDN, &present);
	if ( present ) {
		return(ACL_TRUE);
	}							

	return(ACL_FALSE);
}

int
DS_LASRoleDnAttrEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator, 
		char *attr_pattern, int *cachable, void **LAS_cookie, 
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth)
{

	char			*attrName;
	int				matched;
	int				rc;
	Slapi_Attr 		*attr;
	lasInfo			lasinfo;
	Slapi_Value     *sval=NULL;
	const struct berval	*attrVal;
	int				k=0;
	int				got_undefined = 0;

	if ( 0 !=  (rc = __acllas_setup (errp, attr_name, comparator, 0, /* Don't allow range comparators */
									attr_pattern,cachable,LAS_cookie,
									subject, resource, auth_info,global_auth,
									DS_LAS_ROLEDN, "DS_LASRoleDnAttrEval", 
									&lasinfo )) ) {
		return LAS_EVAL_FAIL;
	}

	/* For anonymous client, they have no roles so the match is false. */
	if ( lasinfo.anomUser )
		return LAS_EVAL_FALSE;

	/* 
	**
	** The function of this LAS is to  find out if the client has
	** the role specified in the attr.
	** attr_pattern looks like: "ROLEDN cn=role1,o=sun.com"
	*/
	attrName = attr_pattern;

	matched = ACL_FALSE;
	slapi_entry_attr_find( lasinfo.resourceEntry, attrName, &attr);
	if (attr == NULL) {
		/*
		 * Here the entry does not contain the attribute so the user
		 * cannot have this "null" role 
		*/
		return LAS_EVAL_FALSE;
	}

	if (lasinfo.aclpb->aclpb_optype == SLAPI_OPERATION_ADD) {
		/*
		 * Here the entry does not contain the attribute so the user
		 * cannot have this "null" role or
		 * For the add operation, the resource itself 
		 * must never be allowed to grant access--
		 * This is because access would be granted based on a value
		 * of an attribute in the new entry--security hole.
		 * XXX is this therefore FALSE or DONT_KNOW ? 
		 *
		 * 
		*/
		slapi_log_error( SLAPI_LOG_ACL, plugin_name,
				"ACL info: userattr=XXX#ROLEDN does not allow ADD permission.\n");
		got_undefined = 1;
	} else {

		/*
		 * Got the first value.
		 * Test all the values of this attribute--if the client has _any_
		 * of the roles then it's a match.
		*/
		k = slapi_attr_first_value ( attr,&sval );
		while ( k != -1 ) {
	        char *n_attrval;
			Slapi_DN *roleDN;

			attrVal = slapi_value_get_berval ( sval );
			n_attrval = slapi_create_dn_string("%s", attrVal->bv_val);
			if (NULL == n_attrval) {
				slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
							"DS_LASRoleDnAttrEval: Invalid syntax: %s\n",
							attrVal->bv_val );
				return LAS_EVAL_FAIL;
			}
			roleDN = slapi_sdn_new_dn_byval(n_attrval);

			/*  We support: The attribute value can be a USER or a GROUP.
			** Let's compare with the client, thi might be just an user. If it is not
			** then we test it against the list of groups.
			*/
			matched = acllas__user_has_role(lasinfo.aclpb,
							roleDN, lasinfo.aclpb->aclpb_authorization_sdn);
			slapi_ch_free ( (void **)&n_attrval );
			slapi_sdn_free(&roleDN);
			if (matched == ACL_TRUE) {
				break;
			} else if ( matched == ACL_DONT_KNOW ) {
				/* record this but keep going--maybe another group will evaluate to TRUE */
				got_undefined = 1;
			}
			k= slapi_attr_next_value ( attr, k, &sval );
		}/* while */
	}

	/*
	 * If no terms were undefined, then evaluate as normal.
	 * If there was an undefined term, but another one was TRUE, then we also evaluate
	 * as normal.  Otherwise, the whole expression is UNDEFINED.
	*/
	if ( matched == ACL_TRUE || !got_undefined ) {
		if (comparator == CMP_OP_EQ) {
			rc = (matched == ACL_TRUE  ? LAS_EVAL_TRUE : LAS_EVAL_FALSE);
		} else {
			rc = (matched == ACL_TRUE ? LAS_EVAL_FALSE : LAS_EVAL_TRUE);
		}
	} else {
		rc = LAS_EVAL_FAIL;
	} 
	return (rc);
}

/*
 * Here, determine if lasinfo->clientDn matches user (which contains
 * a ($dn) or a $attr component or both.) As defined in the aci 
 * lasinfo->aclpb->aclpb_curr_aci,
 * which is the current aci being evaluated.
 * 
 * returns: ACL_TRUE for matched,
 * 			ACL_FALSE for matched.
 *			ACL_DONT_KNOW otherwise.
 */

int
aclutil_evaluate_macro( char * rule, lasInfo *lasinfo,
						acl_eval_types evalType )
{
	int matched = 0;
	aci_t *aci;
	char *matched_val = NULL;
	char **candidate_list = NULL;
	char **inner_list = NULL;	
	char **sptr = NULL;
	char **tptr = NULL;
	char *t = NULL;
	char *s = NULL;
	struct acl_pblock *aclpb = lasinfo->aclpb;

	aci = lasinfo->aclpb->aclpb_curr_aci;
	/* Get a pointer to the ndn in the resouirce */
	slapi_entry_get_ndn (  lasinfo->resourceEntry );

	/*
	 * First, get the matched value from the target resource.
	 * We have alredy done this matching once beofer at tasrget match time.
	 */

	slapi_log_error(SLAPI_LOG_ACL, plugin_name,
	 				"aclutil_evaluate_macro for aci '%s' index '%d'\n",
					aci->aclName, aci->aci_index );
	if ( aci->aci_macro == NULL ) {
		/* No $dn in the target, it's a $attr type subject rule */
		matched_val = NULL;
	} else {
	
		/*
		 * Look up the matched_val value calculated
		 * from the target and stored judiciously there for us.
		 */

		if ( (matched_val = (char *)acl_ht_lookup( aclpb->aclpb_macro_ht,
								(PLHashNumber)aci->aci_index)) == NULL) {
			slapi_log_error(SLAPI_LOG_ACL, plugin_name,
				"ACL info: failed to locate the calculated target"
				"macro for aci '%s' index '%d'\n",
				aci->aclName, aci->aci_index );
			return(ACL_FALSE); /* Not a match */
		} else {
			slapi_log_error(SLAPI_LOG_ACL, plugin_name,
				"ACL info: found matched_val (%s) for aci index %d"
				"in macro ht\n", 
				aci->aclName, aci->aci_index );
		}
	}

	/*
	 * Now, make a candidate
	 * list of strings to match against the client.
	 * This involves replacing ($dn) or [$dn] by either the matched
	 * value, or all the suffix substrings of matched_val.
	 * If there is no $dn then the candidate list is just
	 * user itself.
	 * 
	*/

	candidate_list = acllas_replace_dn_macro( rule, matched_val, lasinfo);

	sptr= candidate_list;
	while( *sptr != NULL && !matched) {

		s = *sptr;

		/*
		 * Now s may contain some $attr macros.
		 * So, make a candidate list, got by replacing each occurence
		 * of $attr with all the values that attribute has in
		 * the resource entry.
		*/

		inner_list = acllas_replace_attr_macro( s, lasinfo);

		tptr = inner_list;
		while( tptr && *tptr != NULL && (matched != ACL_TRUE) ){

			t = *tptr;

			/*
			 * Now, at last t is a candidate string we can
			 * match agains the client.
			 *
			 * $dn and $attr can appear in userdn, graoupdn and roledn
			 * rules, so we we need to decide which type we
			 * currently evaluating and evaluate that.
			 *
			 * If the string generated was undefined, eg it contained
			 * ($attr.ou) and the entry did not have an ou attribute,then
			 * the empty string is returned for this.  So it we find
			 * an empty string in the list, skip it--it does not match.
			*/
		
			if ( *t != '\0') {
				if ( evalType == ACL_EVAL_USER ) {
				
					matched = acllas_eval_one_user( lasinfo->aclpb,
												lasinfo->clientDn, t);
				} else if (evalType == ACL_EVAL_GROUP) {

					matched = acllas_eval_one_group(t, lasinfo);
				} else if (evalType == ACL_EVAL_ROLE) {
					matched = acllas_eval_one_role(t, lasinfo);
				} else if (evalType == ACL_EVAL_GROUPDNATTR) {
					matched = acllas__eval_memberGroupDnAttr(t, 
												lasinfo->resourceEntry, 
												lasinfo->clientDn, 
												lasinfo->aclpb);
				} else if ( evalType == ACL_EVAL_TARGET_FILTER) {

					matched = acllas_eval_one_target_filter(t,
									lasinfo->resourceEntry);
					
				}
			}
					
			tptr++;			

		}/*inner while*/
		charray_free(inner_list);

		sptr++;
	}/* outer while */

	charray_free(candidate_list);

	return(matched);

}

/*
 * Here, replace the first occurrence of $(dn) with matched_val.
 * replace any occurrence of $[dn] with each of the suffix substrings
 * of matched_val.
 * Each of these strings is returned in a NULL terminated list of strings. 
 *
 * If there is no $dn thing then the returned list just contains rule itself.
 *
 * eg. rule: cn=fred,ou=*, ($dn), o=sun.com
 *     matched_val: ou=People,o=icnc
 *
 * Then we return the list
 * cn=fred,ou=*,ou=People,o=icnc,o=sun.com NULL
 * 
 * eg. rule: cn=fred,ou=*,[$dn], o=sun.com
 *     matched_val: ou=People,o=icnc
 *
 * Then we return the list
 * cn=fred,ou=*,ou=People,o=icnc,o=sun.com 
 * cn=fred,ou=*,o=icnc,o=sun.com
 * NULL
 * 
 * 
*/

static char **
acllas_replace_dn_macro( char *rule, char *matched_val, lasInfo *lasinfo) {
	
	char **a = NULL;
	char *patched_rule = NULL;
	char *rule_to_use = NULL;
	char *new_patched_rule = NULL;	
	int	matched_val_len = 0;
	int j = 0;
	int has_macro_dn = 0;
	int has_macro_levels = 0;
	
	/* Determine what the rule's got once */
	if ( strstr(rule, ACL_RULE_MACRO_DN_KEY) != NULL) {
		/* ($dn) exists */
		has_macro_dn = 1;
	}

	if ( strstr(rule, ACL_RULE_MACRO_DN_LEVELS_KEY) != NULL) {
		/* [$dn] exists */
		has_macro_levels = 1;
	}

	if ( (!has_macro_dn && !has_macro_levels) || !matched_val ) { /* No ($dn) and no [$dn] ... */
		/* ... or no value to replace */
		/*
		 * No $dn thing, just return a list with two elements, rule and NULL.
		 * charray_add will create the list and null terminate it.		
		 */

		charray_add( &a, slapi_ch_strdup(rule));
		return(a);
	} else {

		/*
		 * Have an occurrence of the macro rules
		 *
		 * First, replace all occurrencers of ($dn) with the matched_val
		 */
		if ( has_macro_dn) {
			patched_rule =
				acl_replace_str(rule, ACL_RULE_MACRO_DN_KEY, matched_val);
		}

		/* If there are no [$dn] we're done */

		if ( !has_macro_levels ) {			
			charray_add( &a, patched_rule);
			return(a);
		} else {

			/*
		 	 * It's a [$dn] type, so walk matched_val, splicing in all
		 	 * the suffix substrings and adding each such string to
		 	 * to the returned list.
			 * get_next_component() does not return the commas--the
			 * prefix and suffix should come with their commas.
			 *
			 * All occurrences of each [$dn] are replaced with each level.
			 * 
			 * If has_macro_dn then patched_rule is the rule to strart with,
			 * and this needs to be freed at the end, otherwise
			 * just use rule.
			 */
	
			if (patched_rule) {
				rule_to_use = patched_rule;
			} else {
				rule_to_use = rule;
			}

			matched_val_len = strlen(matched_val);
			j = 0;
			
			while( j <  matched_val_len) {

				new_patched_rule = 
					acl_replace_str(rule_to_use, ACL_RULE_MACRO_DN_LEVELS_KEY, 
										&matched_val[j]);								
				charray_add( &a, new_patched_rule);										
	
				j += acl_find_comp_end(&matched_val[j]);
			}
 						
			if (patched_rule) {
					slapi_ch_free((void**)&patched_rule);
			}
			
			return(a);					
		} 
	}
}

/*
 * Here, replace any occurrence of $attr.attrname with the
 * value of attrname from lasinfo->resourceEntry.
 * 
 *
 * If there is no $attr thing then the returned list just contains rule
 * itself.
 *
 * eg. rule: cn=fred,ou=*,ou=$attr.ou,o=sun.com
 *     ou: People
 *	   ou: icnc 
 *
 * Then we return the list
 * cn=fred,ou=*,ou=People,o=sun.com
 * cn=fred,ou=*,ou=icnc,o=sun.com
 *  
*/

static char **
acllas_replace_attr_macro( char *rule, lasInfo *lasinfo)
{
	char **a = NULL;
	char **working_list = NULL;
	Slapi_Entry *e = lasinfo->resourceEntry;
	char *str, *working_rule;
	char *macro_str, *macro_attr_name;
	int l;
	Slapi_Attr *attr = NULL;
	
	str = strstr(rule, ACL_RULE_MACRO_ATTR_KEY);
	if ( str == NULL ) {

		charray_add(&a, slapi_ch_strdup(rule));
	    return(a);

	} else {
	
		working_rule = slapi_ch_strdup(rule);
		str = strstr(working_rule, ACL_RULE_MACRO_ATTR_KEY);
		charray_add(&working_list, working_rule );
		
		while( str != NULL) {

			/*
			 * working_rule is the first member of working_list.
			 * str points to the next $attr.attrName in working_rule.
			 * each member of working_list needs to have each occurence of
			 * $attr.atrName replaced with the value of attrName in e.
			 * If attrName is multi valued then this generates another
			 * list which replaces the old one.
			 */
			l = acl_strstr(&str[0], ")");
			macro_str = slapi_ch_malloc(l+2);
			strncpy( macro_str, &str[0], l+1);
			macro_str[l+1] = '\0';

			str = strstr(macro_str, ".");
			if (!str) {
				slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
						"acllas_replace_attr_macro: Invalid macro \"%s\".",
						macro_str);
				slapi_ch_free_string(&macro_str);
				charray_free(working_list);
				return NULL;
			}
 
			str++;								/* skip the . */
			l = acl_strstr(&str[0], ")");
			macro_attr_name = slapi_ch_malloc(l+1);
			strncpy( macro_attr_name, &str[0], l);
			macro_attr_name[l] = '\0';

        	slapi_entry_attr_find ( e, macro_attr_name, &attr );
        	if ( NULL == attr ) {
            	
				/*
				 * Here, if a $attr.attrName is such that the attrName
				 * does not occur in the entry then return a ""--
				 * this will go back to the matching code in 
				 * aclutil_evaluate_macro() where "" will
				 * be taken as the candidate.
				*/
				
				slapi_ch_free_string(&macro_str);
				slapi_ch_free_string(&macro_attr_name);
			
				charray_free(working_list);
				return NULL;
				
			} else{

				const struct berval *attrValue;
				Slapi_Value *sval;
               	int i, j;
				char *patched_rule;

	            i= slapi_attr_first_value ( attr, &sval );
    	        while(i != -1) {
        	    	attrValue = slapi_value_get_berval(sval);

					j = 0;
					while( working_list[j] != NULL) {
	 
						patched_rule =
							acl_replace_str(working_list[j], 
									macro_str, attrValue->bv_val);
                	    charray_add(&a, patched_rule);
						j++;
					}
							
                   i= slapi_attr_next_value( attr, i, &sval );
				}/* while */

				/*
				 * Here, a is working_list, where each member has had
				 * macro_str replaced with attrVal.  We hand a over,
				 * so we must set it to NULL since the working list
				 * may be free'd later. */

				charray_free(working_list);
				if (a == NULL) {
					/* This shouldn't happen, but we play
					 * if safe to avoid any problems. */
					slapi_ch_free_string(&macro_str);
					slapi_ch_free_string(&macro_attr_name);
					charray_add(&a, slapi_ch_strdup(""));
					return(a);
				} else {
					working_list = a;
					working_rule = a[0];
					a = NULL;
				}
			}
			slapi_ch_free_string(&macro_str);
			slapi_ch_free_string(&macro_attr_name);
			
			str = strstr(working_rule, ACL_RULE_MACRO_ATTR_KEY);
		
        }/* while */
		
		return(working_list);
	}
}

/*
 * returns ACL_TRUE, ACL_FALSE or ACL_DONT_KNOW.
 *
 * user is a string from the userdn keyword which may contain
 * * components.  This routine does the compare component by component, so
 * that * behaves differently to "normal".
 * Any ($dn) or $attr must have been removed from user before this is called.
*/
static int
acllas_eval_one_user( struct acl_pblock *aclpb, char * clientDN, char *rule) {

	int exact_match = 0;
	const size_t 	LDAP_URL_prefix_len = strlen(LDAP_URL_prefix);

	/* URL format */
	if (strchr (rule, '?') != NULL) {
				/* URL format */
				if (acllas__client_match_URL ( aclpb, clientDN, 
							     rule) == ACL_TRUE) {
					exact_match = 1;					
				}			
	} else if ( strstr(rule, "=*") == NULL ) {
		/* Just a straight compare */
			/* skip the ldap:/// part */
			rule += LDAP_URL_prefix_len;
			exact_match = !slapi_utf8casecmp((ACLUCHP)clientDN,
											(ACLUCHP)rule);
	} else{
		/* Here, contains a =*, so need to match comp by comp */
		/* skip the ldap:/// part */
		rule += LDAP_URL_prefix_len;
		acl_match_prefix( rule, clientDN, &exact_match);
	}
	if ( exact_match) {
		return( ACL_TRUE);
	} else {
		return(ACL_FALSE);
	}
}

/*
 * returns ACL_TRUE, ACL_FALSE and ACL_DONT_KNOW.
 *
 * The user string has had all ($dn) and $attr replaced
 * so the only dodgy thing left is a *.
 *
 * If * appears in such a user string, then it matches only that
 * component, not .*, like it would otherwise.
 * 
*/
static int
acllas_eval_one_group(char *groupbuf, lasInfo *lasinfo) {

	if (groupbuf) {
		return( acllas__user_ismember_of_group (
				      						lasinfo->aclpb,
				      						groupbuf, 
				      						lasinfo->clientDn,
											ACLLAS_CACHE_ALL_GROUPS,
											lasinfo->aclpb->aclpb_clientcert
				   						));
	} else {
		return(ACL_FALSE);	/* not in the empty group */
	}
}

/*
 * returns ACL_TRUE for match, ACL_FALSE for not a match, ACL_DONT_KNOW otherwise.
*/
static int
acllas_eval_one_role(char *role, lasInfo *lasinfo) {
	
	Slapi_DN *roleDN = NULL;
	int rc = ACL_FALSE;	
	char    ebuf [ BUFSIZ ];

	/*
	 * See if lasinfo.clientDn has role rolebuf.
	 * Here we know it's not an anom user nor 
	 * a an anyone user--the client dn must be matched against
	 * a real role.
	*/ 

	roleDN = slapi_sdn_new_dn_byval(role);
	if (role) {
		rc = acllas__user_has_role(									
								lasinfo->aclpb,	      						
			      				roleDN, 
			      				lasinfo->aclpb->aclpb_authorization_sdn);
	} else {	/* The user does not have the empty role */
		rc = ACL_FALSE;
	}
	slapi_sdn_free(&roleDN );

	/* Some useful logging */
	if (rc == ACL_TRUE ) {
		slapi_log_error( SLAPI_LOG_ACL, plugin_name,
                        "role evaluation: user '%s' does have role '%s'\n",
                ACL_ESCAPE_STRING_WITH_PUNCTUATION (lasinfo->clientDn, ebuf),
                        role);
	} else {
		slapi_log_error( SLAPI_LOG_ACL, plugin_name,
                        "role evaluation: user '%s' does NOT have role '%s'\n",
                ACL_ESCAPE_STRING_WITH_PUNCTUATION (lasinfo->clientDn, ebuf),
				role);
	}
	return(rc);
}

/*
 * returns ACL_TRUE if e matches the filter str, ACL_FALSE if not,
 * ACL_DONT_KNOW otherwise.
*/
static int acllas_eval_one_target_filter( char * str, Slapi_Entry *e) {
	
	int rc = ACL_FALSE;
	Slapi_Filter *f = NULL;							

	PR_ASSERT(str);

	if ((f = slapi_str2filter(str)) == NULL) {
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
        	"Warning: Bad targetfilter(%s) in aci: does not match\n", str);       	
		return(ACL_DONT_KNOW);
	}

	if (slapi_vattr_filter_test(NULL, e, f, 0 /*don't do acess chk*/)!= 0) {
			rc = ACL_FALSE;	/* Filter does not match */
	} else {
			rc = ACL_TRUE;  /* filter does match */
	}
	slapi_filter_free(f, 1);

	return(rc);

}





/***************************************************************************/
/*				E	N	D			   */
/***************************************************************************/

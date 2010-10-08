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

/*****************************************************************************
* acl.h
*
*	Header file for ACL processing 
*
*****************************************************************************/
#ifndef _ACL_H_
#define _ACL_H_

/* Required to get portable printf/scanf format macros */
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>

/* NSPR uses the print macros a bit differently than ANSI C.  We
 * need to use ll for a 64-bit integer, even when a long is 64-bit.
 */
#undef PRIu64
#define PRIu64  "llu"
#undef PRI64
#define PRI64   "ll"

#else
#error Need to define portable format macros such as PRIu64
#endif /* HAVE_INTTYPES_H */

#include 	<stdio.h>
#include 	<string.h>
#include 	<sys/types.h>
#include	<limits.h>
#ifndef _WIN32
#include 	<sys/socket.h>
#include 	<netinet/in.h>
#include 	<arpa/inet.h>
#include 	<netdb.h>
#endif

#include 	<ldap.h>
#include 	<las.h>
#include	<aclproto.h>
#include	<aclerror.h>
#include	"prcvar.h"
#include	"slapi-plugin.h"
#include	"slap.h"
#include	"slapi-private.h"
#include	"portable.h"
#include 	"prrwlock.h"
#include	"avl.h"

#include	"cert.h"

#include	<plhash.h>

#ifdef SOLARIS
	#include <tnf/probe.h>
#else
	#define TNF_PROBE_0_DEBUG(a,b,c)
	#define TNF_PROBE_1_DEBUG(a,b,c,d,e,f)
#endif

#define ACL_PLUGIN_NAME "NSACLPlugin"
extern char *plugin_name;

/*
 * Define the OID for version 2 of the proxied authorization control if
 * it is not already defined (it is in recent copies of ldap.h).
 */
#ifndef LDAP_CONTROL_PROXIEDAUTH
#define LDAP_CONTROL_PROXIEDAUTH	"2.16.840.1.113730.3.4.18"
#endif

#define ACLUCHP unsigned char *

static char* const aci_attr_type 			= "aci";
static char* const filter_string 			= "aci=*";
static char* const aci_targetdn 			= "target";
static char* const aci_targetattr 			= "targetattr";
static char* const aci_targetattrfilters 	= "targattrfilters";
static char* const aci_targetfilter 		= "targetfilter";

static char* const LDAP_URL_prefix_core 	= "ldap://";
static char* const LDAPS_URL_prefix_core 	= "ldaps://";

static char* const LDAP_URL_prefix 	= "ldap:///";
static char* const LDAPS_URL_prefix 	= "ldaps:///";

static char* const access_str_compare 	= "compare";
static char* const access_str_search  	= "search";
static char* const access_str_read    	= "read";
static char* const access_str_write   	= "write";
static char* const access_str_delete	= "delete";
static char* const access_str_add		= "add";
static char* const access_str_selfwrite = "selfwrite";
static char* const access_str_proxy 	= "proxy";

#define ACL_INIT_ATTR_ARRAY 5 

/* define the method */
#define DS_METHOD  "ds_method"

#define ACL_ESCAPE_STRING_WITH_PUNCTUATION(x,y) (slapi_is_loglevel_set(SLAPI_LOG_ACL) ? escape_string_with_punctuation(x,y) : "")

/* Lases */
#define DS_LAS_USER 		"user"
#define DS_LAS_GROUP 		"group"
#define DS_LAS_USERDN		"userdn"
#define DS_LAS_GROUPDN		"groupdn"
#define	DS_LAS_USERDNATTR	"userdnattr"
#define	DS_LAS_AUTHMETHOD	"authmethod"
#define	DS_LAS_GROUPDNATTR	"groupdnattr"
#define DS_LAS_USERATTR		"userattr"
#define DS_LAS_ROLEDN		"roledn"
#define DS_LAS_ROLEDNATTR	"rolednattr"
#define DS_LAS_SSF		"ssf"


/* These define the things that aclutil_evaluate_macro() supports */
typedef enum
{
	ACL_EVAL_USER,
	ACL_EVAL_GROUP,
	ACL_EVAL_ROLE,
	ACL_EVAL_GROUPDNATTR,
	ACL_EVAL_TARGET_FILTER
}acl_eval_types;

typedef enum
{
	ACL_RULE_MACRO_DN_TYPE,
	ACL_RULE_MACRO_DN_LEVELS_TYPE
}acl_rule_macro_types;

#define ACL_TARGET_MACRO_DN_KEY "($dn)"
#define ACL_RULE_MACRO_DN_KEY	"($dn)"
#define ACL_RULE_MACRO_DN_LEVELS_KEY "[$dn]"
#define ACL_RULE_MACRO_ATTR_KEY "($attr."

#define ACL_EVAL_USER	0
#define ACL_EVAL_GROUP	1
#define ACL_EVAL_ROLE	2

/* The LASes are implemented in the libaccess library */
#define DS_LAS_TIMEOFDAY	"timeofday"
#define DS_LAS_DAYOFWEEK	"dayofweek"


/* ACL function return codes */
#define ACL_TRUE 		   			1	/* evaluation results to TRUE */
#define ACL_OK						ACL_TRUE
#define ACL_FALSE		   			0	/* evaluation results to FALSE */
#define ACL_ERR			  			-1	/* generic error */
#define ACL_TARGET_FILTER_ERR	  	-2	/* Target filter not set properly */
#define ACL_TARGETATTR_FILTER_ERR 	-3	/* TargetAttr filter not set properly */
#define ACL_TARGETFILTER_ERR	  	-4	/* Target filter not set properly */
#define ACL_SYNTAX_ERR		  		-5	/* Syntax error */
#define ACL_ONEACL_TEXT_ERR	  		-6	/* ONE ACL text error */
#define ACL_ERR_CONCAT_HANDLES	  	-7	/* unable to concat the handles */
#define ACL_INVALID_TARGET	  		-8	/* invalid target */
#define	ACL_INVALID_AUTHMETHOD 	  	-9	/* multiple client auth */
#define ACL_INVALID_AUTHORIZATION 	-10	/* no authorization */
#define ACL_INCORRECT_ACI_VERSION 	-11	/* incorrect version # */
#define ACL_DONT_KNOW				-12	/* the world is an uncertain place */

/* supported by the DS */
#define DS_PROP_CONNECTION 	"connection"
#define DS_ATTR_USERDN     	"userdn"
#define	DS_ATTR_ENTRY		"entry"
#define DS_PROP_ACLPB		"aclblock"
#define DS_ATTR_AUTHTYPE	"authtype"
#define DS_ATTR_CERT		"clientcert"
#define DS_ATTR_SSF		"ssf"

#define ACL_ANOM_MAX_ACL 40
struct scoped_entry_anominfo {
	short anom_e_targetInfo[ACL_ANOM_MAX_ACL];
	short anom_e_nummatched;
	short anom_e_isrootds;	
};

typedef struct targetattr {
	int			attr_type;
#define	ACL_ATTR_FILTER		0x01
#define	ACL_ATTR_STRING		0x02
#define	ACL_ATTR_STAR		0x04  	/* attr is * only */
   
	union {
	  char			*attr_str;
	  struct slapi_filter	*attr_filter;      
    }u;      	
}Targetattr;

typedef struct targetattrfilter {
	char	*attr_str;
	char	*filterStr;
	struct slapi_filter	*filter;  /*  value filter */
    
}Targetattrfilter;

typedef struct Aci_Macro {
	char	*match_this;
	char 	*macro_ptr;		/* ptr into match_this */
}aciMacro;

typedef PLHashTable acl_ht_t;

/* Access Control Item (aci): Stores information about a particular ACL */
typedef struct aci {
	int						aci_type;	/* Type of resurce */

/* THE FIRST BYTE WAS USED TO KEEP THE RIGHTS. ITS BEEN MOVED TO
** aci_access and is now free.
** 
**
** 
*/

#define ACI_TARGET_MACRO_DN			(int)0x000001
#define ACI_TARGET_FILTER_MACRO_DN	(int)0x000002
#define ACI_TARGET_DN				(int)0x000100	/* target has DN */
#define ACI_TARGET_ATTR				(int)0x000200	/* target is an attr */
#define ACI_TARGET_PATTERN			(int)0x000400	/* target has some patt */
#define ACI_TARGET_FILTER			(int)0x000800	/* target has a filter */
#define	ACI_ACLTXT					(int)0x001000	/* ACI has text only */
#define	ACI_TARGET_NOT				(int)0x002000	/* it's a !=  */
#define	ACI_TARGET_ATTR_NOT			(int)0x004000	/* It's a != manager */
#define ACI_TARGET_FILTER_NOT		(int)0x008000	/* It's a != filter */
#define ACI_UNUSED2					(int)0x010000    /* Unused */
#define ACI_HAS_ALLOW_RULE			(int)0x020000	/* allow (...) */
#define ACI_HAS_DENY_RULE			(int)0x040000	/* deny (...) */
#define ACI_CONTAIN_NOT_USERDN		(int)0x080000	/* userdn != blah */
#define ACI_TARGET_ATTR_ADD_FILTERS	(int)0x100000
#define ACI_TARGET_ATTR_DEL_FILTERS	(int)0x200000
#define ACI_CONTAIN_NOT_GROUPDN		(int)0x400000	/* groupdn != blah */
#define ACI_CONTAIN_NOT_ROLEDN		(int)0x800000

	int				aci_access;

/*
 * See also aclpb_access which is used to store rights too.
*/   
      
	short			aci_ruleType;	/* kinds of rules in the ACL */

#define ACI_USERDN_RULE		(short)  0x0001
#define	ACI_USERDNATTR_RULE	(short)  0x0002
#define	ACI_GROUPDN_RULE	(short)  0x0004
#define ACI_GROUPDNATTR_RULE (short) 0x0008
#define ACI_AUTHMETHOD_RULE	(short)	 0x0010
#define ACI_IP_RULE			(short)  0x0020
#define ACI_DNS_RULE		(short)  0x0040
#define ACI_TIMEOFDAY_RULE	(short)  0x0080
#define ACI_DAYOFWEEK_RULE	(short)  0x0010
#define ACI_USERATTR_RULE	(short)  0x0200
/*
 * These are extension of USERDN/GROUPDN rule. However since the
 * semantics are quite different, we classify them as different rules.
 * ex: groupdn = "ldap:///cn=helpdesk, ou=$attr.dept, o=$dn.o, o=isp"
 */
#define ACI_PARAM_DNRULE	(short)	0x0400
#define ACI_PARAM_ATTRRULE	(short)	0x0800
#define ACI_USERDN_SELFRULE (short) 0x1000
#define ACI_ROLEDN_RULE		(short) 0x2000
#define ACI_SSF_RULE		(short) 0x4000



#define ACI_ATTR_RULES ( ACI_USERDNATTR_RULE | ACI_GROUPDNATTR_RULE | ACI_USERATTR_RULE  | ACI_PARAM_DNRULE | ACI_PARAM_ATTRRULE  | ACI_USERDN_SELFRULE)
#define ACI_CACHE_RESULT_PER_ENTRY ACI_ATTR_RULES

	short					aci_elevel;	/* Based on the aci type some idea about the
								** execution flow 
										*/
	int						aci_index;		/* index #	   */
	Slapi_DN				*aci_sdn;		/* location */
	Slapi_Filter			*target;		/* Target is a DN */
	Targetattr				**targetAttr;
	char					*targetFilterStr;
	struct slapi_filter		*targetFilter;  /* Target has a filter */
	Targetattrfilter		**targetAttrAddFilters;
	Targetattrfilter		**targetAttrDelFilters;
	char					*aclName;		/* ACL name */
	struct ACLListHandle	*aci_handle;	/*handle of the ACL */
	aciMacro				*aci_macro;
	struct aci				*aci_next;		/* next  one */
}aci_t;

/* Aci excution level 
** The idea is that for each handle types, we can prioritize which one to evaluate first.
** Evaluating the user before the group is better.
*/
#define ACI_ELEVEL_USERDN_ANYONE	0
#define ACI_ELEVEL_USERDN_ALL		1
#define ACI_ELEVEL_USERDN			2
#define ACI_ELEVEL_USERDNATTR		3
#define ACI_ELEVEL_GROUPDNATTR_URL	4
#define ACI_ELEVEL_GROUPDNATTR		5
#define ACI_ELEVEL_GROUPDN			6
#define ACI_MAX_ELEVEL				ACI_ELEVEL_GROUPDN +1
#define ACI_DEFAULT_ELEVEL			ACI_MAX_ELEVEL


#define ACLPB_MAX_SELECTED_ACLS			200

typedef struct result_cache {
	int				aci_index;
	short			aci_ruleType;
	short			result;
#define ACLPB_CACHE_READ_RES_ALLOW		(short)0x0001 /* used for ALLOW handles only */
#define ACLPB_CACHE_READ_RES_DENY		(short)0x0002 /* used for DENY handles only */ 
#define ACLPB_CACHE_SEARCH_RES_ALLOW		(short)0x0004 /* used for ALLOW handles only */
#define ACLPB_CACHE_SEARCH_RES_DENY		(short)0x0008 /* used for DENY handles only */
#define ACLPB_CACHE_SEARCH_RES_SKIP		(short)0x0010 /* used for both types */
#define ACLPB_CACHE_READ_RES_SKIP		(short)0x0020 /* used for both types */
}r_cache_t;
#define ACLPB_MAX_CACHE_RESULTS			ACLPB_MAX_SELECTED_ACLS

/*
 *  This is use to keep the result of the evaluation of the attr.
 *  We are only intrested in read/searc only.
 */
struct acl_attrEval {
	char		*attrEval_name;			/* Attribute Name */
	short		attrEval_r_status;		/* status of read evaluation */
	short		attrEval_s_status;		/* status of search evaluation */
	int			attrEval_r_aciIndex;	/* Index of the ACL which grants access*/
	int			attrEval_s_aciIndex;	/* Index of the ACL which grants access*/

#define ACL_ATTREVAL_SUCCESS 		0x1
#define	ACL_ATTREVAL_FAIL			0x2
#define	ACL_ATTREVAL_RECOMPUTE		0x4
#define ACL_ATTREVAL_DETERMINISTIC	7
#define ACL_ATTREVAL_INVALID		0x8

};
typedef struct acl_attrEval AclAttrEval;


/*
 * Struct to keep the evaluation context information.  This struct is
 * used in multiple  places ( different instance )  to keep the context for
 * current entry evaluation, previous entry evaluation or previous operation
 * evaluation status.
 */
#define ACLPB_MAX_ATTR_LEN 100
#define	ACLPB_MAX_ATTRS		100
struct	acleval_context {

	/* Information about the attrs */
	AclAttrEval	acle_attrEval[ACLPB_MAX_ATTRS];
	short		acle_numof_attrs;

	/* Handles information */
	short		acle_numof_tmatched_handles;
	int			acle_handles_matched_target[ACLPB_MAX_SELECTED_ACLS];	
};
typedef struct acleval_context	aclEvalContext;


struct acl_usergroup {
	short			aclug_signature;
/*
 * To modify refcnt you need either the write lock on the whole cache or
 * the reader lock on the whole cache plus this refcnt mutex
*/
	short			aclug_refcnt;
	PRLock			*aclug_refcnt_mutex;
	

	char			*aclug_ndn;		/* Client's normalized DN */

	char			**aclug_member_groups;
	short			aclug_member_group_size;
	short			aclug_numof_member_group;

	char			**aclug_notmember_groups;
	short			aclug_notmember_group_size;
	short			aclug_numof_notmember_group;
	struct acl_usergroup	*aclug_next;
	struct acl_usergroup	*aclug_prev;
	
};
typedef struct acl_usergroup	aclUserGroup;

#define ACLUG_INCR_GROUPS_LIST	20

struct  aci_container {
	Slapi_DN		*acic_sdn;		/* node DN */
	aci_t			*acic_list;		/* List of the ACLs for that node */
	int				acic_index;		/* index to the container array */
};
typedef struct  aci_container AciContainer;

struct acl_pblock {
	int							aclpb_state;

#define ACLPB_ACCESS_ALLOWED_ON_A_ATTR		0x000001
#define	ACLPB_ACCESS_DENIED_ON_ALL_ATTRS	0x000002
#define	ACLPB_ACCESS_ALLOWED_ON_ENTRY		0x000004
#define ACLPB_ATTR_STAR_MATCHED				0x000008
#define ACLPB_FOUND_ATTR_RULE				0x000010
#define ACLPB_SEARCH_BASED_ON_LIST			0x000020
#define ACLPB_EXECUTING_DENY_HANDLES		0x000040
#define ACLPB_EXECUTING_ALLOW_HANDLES		0x000080
#define ACLPB_ACCESS_ALLOWED_USERATTR		0x000100
#ifdef DETERMINE_ACCESS_BASED_ON_REQUESTED_ATTRIBUTES 
 #define ACLPB_USER_SPECIFIED_ATTARS		0x000200
 #define ACLPB_USER_WANTS_ALL_ATTRS			0x000400
#endif
#define ACLPB_EVALUATING_FIRST_ATTR			0x000800
#define ACLPB_FOUND_A_ENTRY_TEST_RULE		0x001000
#define ACLPB_SEARCH_BASED_ON_ENTRY_LIST	0x002000
#define ACLPB_DONOT_USE_CONTEXT_ACLS		0x004000
#define ACLPB_HAS_ACLCB_EVALCONTEXT			0x008000
#define	ACLPB_COPY_EVALCONTEXT				0x010000
#define	ACLPB_MATCHES_ALL_ACLS				0x020000
#define ACLPB_INITIALIZED					0x040000
#define	ACLPB_INCR_ACLCB_CACHE				0x080000
#define	ACLPB_UPD_ACLCB_CACHE				0x100000
#define	ACLPB_ATTR_RULE_EVALUATED			0x200000
#define ACLPB_DONOT_EVALUATE_PROXY			0x400000


#define ACLPB_RESET_MASK ( ACLPB_ACCESS_ALLOWED_ON_A_ATTR | ACLPB_ACCESS_DENIED_ON_ALL_ATTRS | \
			   ACLPB_ACCESS_ALLOWED_ON_ENTRY | ACLPB_ATTR_STAR_MATCHED |  \
			   ACLPB_FOUND_ATTR_RULE | ACLPB_EVALUATING_FIRST_ATTR |  \
			   ACLPB_FOUND_A_ENTRY_TEST_RULE )
#define ACLPB_STATE_ALL				0x3fffff

	int						aclpb_res_type;

	#define ACLPB_NEW_ENTRY	0x100
	#define ACLPB_EFFECTIVE_RIGHTS	0x200
	#define ACLPB_RESTYPE_ALL			0x7ff
	
    /*
     * The bottom bye used to be for rights. It's free now as they have
     * been moved to aclpb_access.    
	*/
    
	int 					aclpb_access;
    			
#define ACLPB_SLAPI_ACL_WRITE_ADD	0x200
#define ACLPB_SLAPI_ACL_WRITE_DEL	0x400

	/* stores the requested access during an operation */
    
	short 					aclpb_signature;
	short					aclpb_type;
#define ACLPB_TYPE_MAIN		1
#define ACLPB_TYPE_MAIN_STR "Main Block"
#define ACLPB_TYPE_PROXY	2
#define ACLPB_TYPE_PROXY_STR	"Proxy Block"

	Slapi_Entry				*aclpb_client_entry;	/* A copy of client's entry */
	Slapi_PBlock			*aclpb_pblock;		/* back to LDAP PBlock */
	int						aclpb_optype;		/* current optype from pb */

	/* Current entry/dn/attr evaluation info */
	Slapi_Entry				*aclpb_curr_entry;		/* current Entry being processed */
	int						aclpb_num_entries;
	Slapi_DN				*aclpb_curr_entry_sdn;	/* Entry's SDN */
	Slapi_DN				*aclpb_authorization_sdn; /* dn used for authorization */

	AclAttrEval				*aclpb_curr_attrEval; 	/* Current attr being evaluated */
	struct berval			*aclpb_curr_attrVal;	/* Value of Current attr 	*/
	Slapi_Entry				*aclpb_filter_test_entry;	/* Scratch entry */
	aci_t					*aclpb_curr_aci;
	char					*aclpb_Evalattr;	/* The last attr evaluated  */

	/* Plist and eval info */
	ACLEvalHandle_t			*aclpb_acleval;		/* acleval handle for evaluation */
	struct PListStruct_s    *aclpb_proplist;/* All the needed property */

	/* DENY ACI HANDLES */
	aci_t					**aclpb_deny_handles;
	int						aclpb_deny_handles_size;
	int						aclpb_num_deny_handles;

	/* ALLOW ACI HANDLES */
	aci_t					**aclpb_allow_handles;
	int						aclpb_allow_handles_size;
	int						aclpb_num_allow_handles;		

	/* This is used in the groupdnattr="URL" rule
	** Keep a list of base where searched has been done
	*/
	char					**aclpb_grpsearchbase;
	int						aclpb_grpsearchbase_size;
	int						aclpb_numof_bases;

	aclUserGroup			*aclpb_groupinfo;
	
	/* Keep the Group nesting level */
	int 					aclpb_max_nesting_level;
	int 					aclpb_max_member_sizelimit;


    /* To keep the results in the cache */

	int						aclpb_last_cache_result;
	struct	result_cache	aclpb_cache_result[ACLPB_MAX_CACHE_RESULTS];

	/* Index numbers of ACLs selected  based on a locality search*/
	char					*aclpb_search_base;
	int						aclpb_base_handles_index[ACLPB_MAX_SELECTED_ACLS];
	int						aclpb_handles_index[ACLPB_MAX_SELECTED_ACLS];

	/* Evaluation context info
	** 1) Context cached from aclcb ( from connection struct )
	** 2) Context cached from previous entry evaluation
	** 3) current entry evaluation info
	*/
	aclEvalContext			aclpb_curr_entryEval_context;
	aclEvalContext			aclpb_prev_entryEval_context;
	aclEvalContext			aclpb_prev_opEval_context;

	/* Currentry anom profile sumamry */
	struct scoped_entry_anominfo	 aclpb_scoped_entry_anominfo;

	/* Some Statistics gathering */
	PRUint16				aclpb_stat_acllist_scanned;
	PRUint16				aclpb_stat_aclres_matched;
	PRUint16				aclpb_stat_total_entries;
	PRUint16				aclpb_stat_anom_list_scanned;
	PRUint16				aclpb_stat_num_copycontext;
	PRUint16				aclpb_stat_num_copy_attrs;
	PRUint16				aclpb_stat_num_tmatched_acls;
	PRUint16				aclpb_stat_unused;
	CERTCertificate			*aclpb_clientcert;
	AciContainer			*aclpb_aclContainer;
	struct acl_pblock 		*aclpb_proxy;		/* Child proxy block */
	acl_ht_t				*aclpb_macro_ht;	/* ht for partial macro strs */

	struct acl_pblock 		*aclpb_prev;		/* Previpous in the chain */
	struct acl_pblock 		*aclpb_next;		/* Next in the chain */
};
typedef struct acl_pblock Acl_PBlock;

/* PBLCOK TYPES */
typedef enum 
{
	ACLPB_BINDDN_PBLOCK,
	ACLPB_PROXYDN_PBLOCK,
	ACLPB_ALL_PBLOCK
}aclpb_types;


#define	ACLPB_EVALCONTEXT_CURR	1
#define	ACLPB_EVALCONTEXT_PREV	2
#define	ACLPB_EVALCONTEXT_ACLCB	3



/* Cleaning/ deallocating/ ...  acl_freeBlock() */
#define ACL_CLEAN_ACLPB 	1
#define ACL_COPY_ACLCB		2
#define ACL_CLEAN_ACLCB		3

/* used to differentiate acl plugins sharing the same lib */
#define ACL_PLUGIN_IDENTITY		1
#define ACL_PREOP_PLUGIN_IDENTITY 	2


/* start with 50 and then add 50 more as required 
 * The first ACI_MAX_ELEVEL slots are predefined.
 */
#define ACLPB_INCR_LIST_HANDLES			ACI_MAX_ELEVEL + 43

#define ACLPB_INCR_BASES 5

/*
 * acl private block which hangs from connection structure.
 * This is allocated the first time an operation is done and freed when the
 * connection are cleaned.
 * 
 */
struct acl_cblock {

	short				aclcb_aclsignature;
	short				aclcb_state;
#define ACLCB_HAS_CACHED_EVALCONTEXT	0x1

	Slapi_DN			*aclcb_sdn;		/* Contains bind SDN */
	aclEvalContext		aclcb_eval_context;
	PRLock				*aclcb_lock; 	/* shared lock */
};

struct acl_groupcache {
	short			aclg_state;		/* status information */
	short			aclg_signature;
	int				aclg_num_userGroups;
	aclUserGroup	*aclg_first;
	aclUserGroup	*aclg_last;
	PRRWLock		*aclg_rwlock;		/* lock to monitor the group cache */	
};
typedef struct acl_groupcache	aclGroupCache;


/* Type of extensions that can be registered */
typedef enum
{
		ACL_EXT_OPERATION,			/* extension for Operation object */
		ACL_EXT_CONNECTION,		/* extension for Connection object */
		ACL_EXT_ALL
}ext_type;

/* Used to pass data around in acllas.c */

typedef struct {
	char		*clientDn;
	char		*authType;
	int			anomUser;
	Acl_PBlock	*aclpb;
	Slapi_Entry	*resourceEntry;
	int		ssf;
}lasInfo;


/* reasons why the subject allowed/denied access--good for logs */

typedef enum{
ACL_REASON_NO_ALLOWS,
ACL_REASON_RESULT_CACHED_DENY,
ACL_REASON_EVALUATED_DENY, /* evaluated deny */
ACL_REASON_RESULT_CACHED_ALLOW, /* cached allow */
ACL_REASON_EVALUATED_ALLOW, /* evalauted allow */
ACL_REASON_NO_MATCHED_RESOURCE_ALLOWS, /* were allows/denies, but none matched */
ACL_REASON_NONE, /* no reason available */
ACL_REASON_ANON_ALLOWED,
ACL_REASON_ANON_DENIED,
ACL_REASON_NO_MATCHED_SUBJECT_ALLOWS,
ACL_REASON_EVALCONTEXT_CACHED_ALLOW,
ACL_REASON_EVALCONTEXT_CACHED_NOT_ALLOWED,
ACL_REASON_EVALCONTEXT_CACHED_ATTR_STAR_ALLOW
}aclReasonCode_t;

typedef struct{
	aci_t *deciding_aci;
	aclReasonCode_t reason;
}aclResultReason_t;
#define ACL_NO_DECIDING_ACI_INDEX -10


/* Extern declaration for backend state change fnc: acllist.c and aclinit.c */

void acl_be_state_change_fnc ( void *handle, char *be_name, int old_state,
															int new_state);


/* Extern declaration for ATTRs */

extern int
DS_LASIpGetter(NSErr_t *errp, PList_t subject, PList_t resource, PList_t
		auth_info, PList_t global_auth, void *arg);
extern int
DS_LASDnsGetter(NSErr_t *errp, PList_t subject, PList_t resource, PList_t
		auth_info, PList_t global_auth, void *arg);
extern int
DS_LASUserDnGetter(NSErr_t *errp, PList_t subject, PList_t resource, PList_t
		auth_info, PList_t global_auth, void *arg);
extern int
DS_LASGroupDnGetter(NSErr_t *errp, PList_t subject, PList_t resource, PList_t
		auth_info, PList_t global_auth, void *arg);
extern int
DS_LASEntryGetter(NSErr_t *errp, PList_t subject, PList_t resource, 
		PList_t auth_info, PList_t global_auth, void *arg);

extern int
DS_LASCertGetter(NSErr_t *errp, PList_t subject, PList_t resource, 
		PList_t auth_info, PList_t global_auth, void *arg);

/* function declartion for LAses supported by DS */

extern int DS_LASUserEval(NSErr_t *errp, char *attribute, CmpOp_t comparator,
		char *pattern, int *cachable, void **las_cookie,
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth);

extern int DS_LASGroupEval(NSErr_t *errp, char *attribute, CmpOp_t comparator,
		char *pattern, int *cachable, void **las_cookie,
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth);

extern int DS_LASUserDnEval(NSErr_t *errp, char *attribute, CmpOp_t comparator,
		char *pattern, int *cachable, void **las_cookie,
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth);

extern int DS_LASGroupDnEval(NSErr_t *errp, char *attribute, CmpOp_t comparator,
		char *pattern, int *cachable, void **las_cookie,
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth);

extern int DS_LASRoleDnEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator, 
		char *attr_pattern, int *cachable, void **LAS_cookie, 
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth);

extern int DS_LASUserDnAttrEval(NSErr_t *errp, char *attribute, 
		CmpOp_t comparator,
		char *pattern, int *cachable, void **las_cookie,
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth);

extern int DS_LASAuthMethodEval(NSErr_t *errp, char *attribute, 
		CmpOp_t comparator,
		char *pattern, int *cachable, void **las_cookie,
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth);

extern int DS_LASGroupDnAttrEval(NSErr_t *errp, char *attribute, 
		CmpOp_t comparator,
		char *pattern, int *cachable, void **las_cookie,
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth);

extern int DS_LASRoleDnAttrEval(NSErr_t *errp, char *attribute, 
		CmpOp_t comparator,
		char *pattern, int *cachable, void **las_cookie,
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth);

extern int DS_LASUserAttrEval(NSErr_t *errp, char *attribute, 
		CmpOp_t comparator,
		char *pattern, int *cachable, void **las_cookie,
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth);

extern int DS_LASSSFEval(NSErr_t *errp, char *attribute,
		CmpOp_t comparator,
		char *pattern, int *cachable, void **las_cookie,
		PList_t subject, PList_t resource, PList_t auth_info,
		PList_t global_auth);

/* other function declaration */
int 		aclinit_main();
int			acl_match_substring (struct slapi_filter *f, char *str, int match);
void		acl_print_acllib_err(NSErr_t *errp, char * str);
void		acl_initBlock ( Slapi_PBlock *pb );
void 		acl_freeBlock ( Slapi_PBlock *pb, int state );
int  		acl_read_access_allowed_on_entry ( Slapi_PBlock *pb, Slapi_Entry *e,
                                   char **attrs, int access);
int  		acl_access_allowed_modrdn ( Slapi_PBlock *pb, Slapi_Entry *e, char *attr,
                                  struct berval *val, int access);
int  		acl_read_access_allowed_on_attr ( Slapi_PBlock *pb, Slapi_Entry *e, char *attr,
                                  struct berval *val, int access);
void 		acl_set_acllist (Slapi_PBlock *pb, int scope, char *base);
void 		acl_gen_err_msg(int access, char *edn, char *attr, char **errbuf);
void 		acl_modified ( Slapi_PBlock *pb, int optype, char *dn, void *change);
int 		acl_access_allowed_disjoint_resource( Slapi_PBlock *pb, Slapi_Entry *e,
					char *attr, struct berval *val, int access );
int 		acl_access_allowed_main ( Slapi_PBlock *pb, Slapi_Entry *e, char **attrs, 
                          		  struct berval *val, int access , int flags, char **errbuf);
int 		acl_access_allowed( Slapi_PBlock *pb, Slapi_Entry *e, char *attr,
				        	struct berval *val, int access );
int 		acl_verify_syntax(const Slapi_DN *e_sdn, const struct berval *bval);
aclUserGroup * acl_get_usersGroup ( struct acl_pblock *aclpb , char *n_dn);
void		acl_print_acllib_err (NSErr_t *errp , char * str);	
int 		acl_check_mods( Slapi_PBlock *pb, Slapi_Entry *e, LDAPMod **mods, char **errbuf );
int 		acl_verify_aci_syntax (Slapi_Entry *e, char **errbuf);
char * 		acl__access2str(int access);
void		acl_strcpy_special (char *d, char *s);
int			acl_parse(char * str, aci_t *aci_item);
char *		acl_access2str ( int access );
int 		acl_init_ext ();
void * 		acl_get_ext (ext_type type, void *object);
void  		acl_set_ext (ext_type type, void *object, void *data);
void		acl_reset_ext_status (ext_type type, void *object);
void 		acl_init_op_ext ( Slapi_PBlock *pb , int type, char *dn, int copy);
void *		acl_operation_ext_constructor (void *object, void *parent );
void 		acl_operation_ext_destructor ( void *ext, void *object, void *parent );
void *		acl_conn_ext_constructor (void *object, void *parent );
void 		acl_conn_ext_destructor ( void *ext, void *object, void *parent );
void        acl_clean_aclEval_context ( aclEvalContext *clean_me, int scrub_only );
void        acl_copyEval_context ( struct acl_pblock *aclpb, aclEvalContext *src,
                        aclEvalContext *dest , int copy_attr_only );
struct acl_pblock *	acl_get_aclpb ( Slapi_PBlock *pb, int type );
int 		acl_client_anonymous ( Slapi_PBlock *pb );
short		acl_get_aclsignature();
void		acl_set_aclsignature( short value);
void		acl_regen_aclsignature();
struct acl_pblock * acl_new_proxy_aclpb( Slapi_PBlock *pb );
void 		acl_set_authorization_dn( Slapi_PBlock *pb, char *dn, int type );
void 		acl_init_aclpb ( Slapi_PBlock *pb , Acl_PBlock *aclpb, 
								const char *dn, int copy_from_aclcb);
int 		acl_create_aclpb_pool ();
int			acl_skip_access_check ( Slapi_PBlock *pb,  Slapi_Entry *e );

int			aclext_alloc_lockarray ();

int			aclutil_str_append(char **str1, const char *str2);
void		aclutil_print_err (int rv , const Slapi_DN *sdn,
			const struct berval* val, char **errbuf);
void		aclutil_print_aci (aci_t *aci_item, char *type);
short		aclutil_gen_signature ( short c_signature );
void		aclutil_print_resource( struct acl_pblock *aclpb, char *right , char *attr, char *clientdn );
char *		aclutil_expand_paramString ( char *str, Slapi_Entry *e );


void		acllist_init_scan (Slapi_PBlock *pb, int scope, char *base);
aci_t * 	acllist_get_first_aci (Acl_PBlock *aclpb, PRUint32 *cookie );
aci_t * 	acllist_get_next_aci ( Acl_PBlock *aclpb, aci_t *curraci, PRUint32 *cookie );
aci_t *		acllist_get_aci_new ();
void		acllist_free_aci (aci_t *item);
void		acllist_acicache_READ_UNLOCK(void);
void		acllist_acicache_READ_LOCK(void);
void		acllist_acicache_WRITE_UNLOCK(void);
void		acllist_acicache_WRITE_LOCK(void);
void		acllist_aciscan_update_scan ( Acl_PBlock *aclpb, char *edn );
int 		acllist_remove_aci_needsLock( const Slapi_DN *sdn,  const struct berval *attr );
int 		acllist_insert_aci_needsLock( const Slapi_DN *e_sdn, const struct berval* aci_attr);
int 		acllist_init ();
int			acllist_moddn_aci_needsLock ( Slapi_DN *oldsdn, char *newdn );
void		acllist_print_tree ( Avlnode *root, int *depth, char *start, char *side);
AciContainer *acllist_get_aciContainer_new ( );
void 		acllist_done_aciContainer (  AciContainer *);

aclUserGroup* aclg_find_userGroup (char *n_dn);
void 		aclg_regen_ugroup_signature( aclUserGroup *ugroup);
void		aclg_markUgroupForRemoval ( aclUserGroup *u_group );
void		aclg_reader_incr_ugroup_refcnt(aclUserGroup* u_group);
int			aclg_numof_usergroups(void);
int 		aclgroup_init ();
void		aclg_regen_group_signature ();
void		aclg_reset_userGroup ( struct acl_pblock *aclpb );
void     	aclg_init_userGroup ( struct acl_pblock *aclpb, const char *dn , int got_lock);
aclUserGroup * aclg_get_usersGroup ( struct acl_pblock *aclpb , char *n_dn);

void		aclg_lock_groupCache (int type );
void		aclg_unlock_groupCache (int type );

int			aclanom_init();
int 		aclanom_match_profile (Slapi_PBlock *pb,  struct acl_pblock *aclpb, 
									Slapi_Entry *e, char *attr, int access);
void		aclanom_get_suffix_info(Slapi_Entry *e, struct acl_pblock *aclpb );
void		aclanom_invalidateProfile();
typedef enum{
	DONT_TAKE_ACLCACHE_READLOCK,
	DO_TAKE_ACLCACHE_READLOCK,
	DONT_TAKE_ACLCACHE_WRITELOCK,
	DO_TAKE_ACLCACHE_WRITELOCK
}acl_lock_flag_t;
void 		aclanom_gen_anomProfile (acl_lock_flag_t lock_flag);
int 		aclanom_is_client_anonymous ( Slapi_PBlock *pb );
int 		aclinit_main ();
typedef struct aclinit_handler_callback_data {
#define		ACL_ADD_ACIS 	1
#define		ACL_REMOVE_ACIS 0
	int	op;
	int retCode;
	acl_lock_flag_t lock_flag;
}aclinit_handler_callback_data_t;
int
aclinit_search_and_update_aci ( int thisbeonly, const Slapi_DN *base,
								char *be_name, int scope, int op,
								acl_lock_flag_t lock_flag);
void 		*aclplugin_get_identity(int plug);
int
acl_dn_component_match( const char *ndn, char *match_this, int component_number);
char *
acl_match_macro_in_target( const char *ndn, char *match_this,
									char *macro_ptr);
char*	get_next_component(char *dn, int *index);
int		acl_match_prefix( char *macro_prefix, const char *ndn, 
							int *exact_match);
char *
get_this_component(char *dn, int *index);
int
acl_find_comp_end( char * s);
char *
acl_replace_str(char * s, char *substr, char* replace_with);
int		acl_strstr(char * s, char *substr);
int		aclutil_evaluate_macro( char * rule, lasInfo *lasinfo,
								acl_eval_types evalType );
int aclutil_str_append_ext(char **dest, size_t *dlen, const char *src, size_t slen);

/* acl hash table functions */
void acl_ht_add_and_freeOld(acl_ht_t * acl_ht, PLHashNumber key,char *value);
acl_ht_t *acl_ht_new(void);
void acl_ht_free_all_entries_and_values( acl_ht_t *acl_ht);
void acl_ht_remove( acl_ht_t *acl_ht, PLHashNumber key);
void *acl_ht_lookup( acl_ht_t *acl_ht, PLHashNumber key);
void acl_ht_display_ht( acl_ht_t *acl_ht);

/* acl get effective rights */
int
acl_get_effective_rights ( Slapi_PBlock *pb, Slapi_Entry *e,
    char **attrs, struct berval *val, int access, char **errbuf );

char* aclutil__access_str (int type , char str[]);

int aclplugin_preop_common( Slapi_PBlock *pb );

#endif /* _ACL_H_ */

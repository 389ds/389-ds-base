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

#include "acl.h"

/****************************************************************************/
/* prototypes                                                               */
/****************************************************************************/
static int __aclp__parse_aci(char *str, aci_t  *aci_item, char **errbuf);
static int __aclp__sanity_check_acltxt(aci_t *aci_item, char *str);
static char *	__aclp__normalize_acltxt (aci_t *aci_item, char *str);
static char *	__aclp__getNextLASRule(aci_t *aci_item, char *str,
        								 char **endOfCurrRule);
static int	__aclp__get_aci_right ( char *str);
static int	__aclp__init_targetattr (aci_t *aci, char *attr_val, char **errbuf);
static int	__acl__init_targetattrfilters( aci_t *aci_item, char *str);
static int process_filter_list( Targetattrfilter ***attrfilterarray,
								char * str);
static int __acl_init_targetattrfilter( Targetattrfilter *attrfilter, char *str );
static void 	__aclp_chk_paramRules ( aci_t *aci_item, char *start,
										char *end);
static void	__acl_strip_trailing_space( char *str);
static void	__acl_strip_leading_space( char **str);
static char *	__acl_trim_filterstr( char * str ); 
static int acl_verify_exactly_one_attribute( char *attr_name, Slapi_Filter *f);
static int type_compare( Slapi_Filter *f, void *arg);
static int acl_check_for_target_macro( aci_t *aci_item, char *value);
static int get_acl_rights_as_int( char * strValue);
/***************************************************************************
*
* acl_parse
*
*	Parses the input string  and copies the information into the
*	correct place in the aci.
*
*
* Input:
* 	Slapi_PBlock	*pb	- Parameter block
*	char	*str		- Input string which has the ACL
*				  This is a duped copy, so here we have
*				  the right to stich '\0' characters into str for
*				  processing purposes.  If you want to keep
*				  a piece of str, you'll need to dup it
*				  as it gets freed outside the scope of acl_parse.
*	aci_t	*item		- the aci item where the ACL info will be
*				- stored.
*
* Returns:
*		0				-- Parsed okay
*		< 0	 			-- error codes
*
* Error Handling:
*	None.
*
**************************************************************************/
int
acl_parse(Slapi_PBlock *pb, char * str, aci_t *aci_item, char **errbuf)
{

	int  		rv=0;
	char 		*next=NULL;
	char 		*save=NULL;

	while(*str) {
		__acl_strip_leading_space( &str );
		if (*str == '\0') break;

		if (*str == '(') {
			if ((next = slapi_find_matching_paren(str)) == NULL) {
				return(ACL_SYNTAX_ERR);
			}
		} else if (!next) {
			/* the statement does not start with a parenthesis */
			return(ACL_SYNTAX_ERR);
		} else {
			/* then we have done all the processing */
			return  0;
		}
		LDAP_UTF8INC(str);	/* skip the "(" */
		save = next;
		LDAP_UTF8INC(next);
		*save = '\0';

		/* Now we have a "str)" */
		if ( 0 != (rv = __aclp__parse_aci(str, aci_item, errbuf))) {
			return(rv);
		}
		
		/* Move to the next */
		str = next;
	}

	/* check if have a ACLTXT or not */
	if (!(aci_item->aci_type & ACI_ACLTXT))
		return ACL_SYNTAX_ERR;

	if (aci_item->target) {
		Slapi_Filter		*f;

		/* Make sure that the target is a valid target.
		** Example: ACL is located in
		** "ou=engineering, o=ace industry, c=us
		** but if the target is "o=ace industry, c=us",
		** then it's an ERROR.
		*/
		f = aci_item->target;
		if (aci_item->aci_type & ACI_TARGET_DN) {
			char           *avaType;
			struct berval   *avaValue;

			Slapi_DN *targdn = slapi_sdn_new();
			slapi_filter_get_ava ( f, &avaType, &avaValue );
			slapi_sdn_init_dn_byref(targdn, avaValue->bv_val);

			if (!slapi_sdn_get_dn(targdn)) {
				/* not a valid DN */
				slapi_sdn_free(&targdn);
				return ACL_INVALID_TARGET;
			}

			if (!slapi_sdn_issuffix(targdn, aci_item->aci_sdn)) {
				slapi_sdn_free(&targdn);
				return ACL_INVALID_TARGET;
			}

			if (slapi_sdn_compare(targdn, aci_item->aci_sdn)) {
				int target_check = 0;
				if (pb) {
					slapi_pblock_get(pb, SLAPI_ACI_TARGET_CHECK, &target_check);
				}
				if (target_check != 1) {
					/* Make sure that the target exists */
					int rc = 0;
					Slapi_PBlock *temppb = slapi_pblock_new();
					slapi_search_internal_set_pb_ext(temppb, targdn,
						LDAP_SCOPE_BASE, "(objectclass=*)", NULL, 1, NULL, NULL,
							(void *)plugin_get_default_component_id(), 0);
					slapi_search_internal_pb(temppb);
					slapi_pblock_get(temppb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
					if (rc != LDAP_SUCCESS) {
						slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
							"The ACL target %s does not exist\n", slapi_sdn_get_dn(targdn));
					}
	
					slapi_free_search_results_internal(temppb);
					slapi_pblock_destroy(temppb);
					if (pb) {
						target_check = 1;
						slapi_pblock_set(pb, SLAPI_ACI_TARGET_CHECK, &target_check);
					}
				}
			}
			slapi_sdn_free(&targdn);
		}
	}

	/*
	** We need to keep the taregetFilterStr for anyone ACL only.
	** same for targetValueFilterStr.
	** We need to keep it for macros too as it needs to be expnaded at eval time.
	** 
	*/
	if ((aci_item->aci_elevel != ACI_ELEVEL_USERDN_ANYONE) &&
		!(aci_item->aci_type & ACI_TARGET_MACRO_DN)) {
			slapi_ch_free((void **)&aci_item->targetFilterStr);
	}

	/*
	 * If we parsed the aci and there was a ($dn) on the user side
	 * but none in hte taget then that's an error as the user side
	  * value is derived from the target side value.
	*/

	if (!(aci_item->aci_type & ACI_TARGET_MACRO_DN) &&
		(aci_item->aci_ruleType & ACI_PARAM_DNRULE)) {
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
		"acl_parse: A macro in a subject ($dn) must have a macro in the target.\n");
		return(ACL_INVALID_TARGET);
	}

	return 0;
}

/***************************************************************************
*
* __aclp__parse_aci
*
*	Parses Each individual subset of information/
*
* Input:
*	char	*str		- Input string which has the ACL like "str)"
*	aci_t	*item		- the aci item where the ACL info will be
*				- stored.
*
* Returns:
*		0				-- Parsed okay
*		< 0	 			-- error codes
*
* Error Handling:
*	None.
*
**************************************************************************/
static int
__aclp__parse_aci(char *str, aci_t  *aci_item, char **errbuf)
{

	int		len;
	int		rv;
	int		type;
	char	*tmpstr;
	char	*s = NULL;
	char	*value = NULL;
	Slapi_Filter 	*f = NULL;
	int  	targetattrlen = strlen(aci_targetattr);
	int		targetdnlen = strlen (aci_targetdn);
	int		tfilterlen = strlen(aci_targetfilter);
   	int 	targetattrfilterslen = strlen(aci_targetattrfilters);

	__acl_strip_leading_space( &str );

	if (*str == '\0') {
		return(ACL_SYNTAX_ERR);
	}

	/* The first letter should tell us something */
	switch(*str) {
	   case 'v':
		type = ACI_ACLTXT;
		rv = __aclp__sanity_check_acltxt(aci_item, str);
		if (rv) {
			return rv;
		}
		break;

	   case 't':
       if (strncmp(str, aci_targetattrfilters,targetattrfilterslen ) == 0) {
			type = ACI_TARGET_ATTR;

		
			/* 
			 * The targetattrfilters bit looks like this:
			 *  (targetattrfilters="add= attr1:F1 && attr2:F2 ... && attrn:Fn,
			 *                      del= attr1:F1 && attr2:F2... && attrn:Fn")
			 */
			if (0 != (rv = __acl__init_targetattrfilters(aci_item, str))) {
				return  rv;
			}
		} else if (strncmp(str, aci_targetattr,targetattrlen ) == 0) {
			type = ACI_TARGET_ATTR;

			if ( (s = strstr( str, "!=" )) != NULL ) {
				type |= ACI_TARGET_ATTR_NOT;
				strncpy(s, " ", 1);
			}
			/* Get individual components of the targetattr.
			 * (targetattr = "cn || u* || phone ||tel:add:(tel=1234) 
			 *  || sn:del:(gn=5678)")
			 * If it contains a value filter, the type will also be
			 *	ACI_TARGET_VALUE_ATTR.
			 */
			if (0 != (rv =  __aclp__init_targetattr(aci_item, str, errbuf))) {
				return  rv;
			}
		} else if (strncmp(str, aci_targetfilter,tfilterlen ) == 0) {
			if ( aci_item->targetFilter)
				return ACL_SYNTAX_ERR;

			type = ACI_TARGET_FILTER;
			/* we need to remove the targetfilter stuff*/
			if ( strstr( str, "!=" ) != NULL ) {
				type |= ACI_TARGET_FILTER_NOT;
			}

			/*
			 * If it's got a macro in the targetfilter then it must
			 * have a target and it must have a macro.
			*/
		
			if ((strcasestr(str, ACL_RULE_MACRO_DN_KEY) != NULL) ||
			    (strcasestr(str, ACL_RULE_MACRO_DN_LEVELS_KEY) != NULL)) {
			
				/* Must have a targetmacro */
				if ( !(aci_item->aci_type & ACI_TARGET_MACRO_DN)) {
					slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
						"acl_parse: A macro in a targetfilter ($dn) must have a macro in the target.\n");
					return(ACL_SYNTAX_ERR);
				}

				type|= ACI_TARGET_FILTER_MACRO_DN;
			}

			tmpstr = strchr(str, '=');
			if (NULL == tmpstr) {
				return ACL_SYNTAX_ERR;
			}
			tmpstr++;
			__acl_strip_leading_space(&tmpstr);

			/*
             * Trim off enclosing quotes and enclosing
             * superfluous brackets.
             * The result has been duped so it can be kept.
            */

			tmpstr = __acl_trim_filterstr( tmpstr );

			f = slapi_str2filter(tmpstr);

			/* save the filter string */
            aci_item->targetFilterStr = tmpstr;

		} else if (strncmp(str, aci_targetdn, targetdnlen) == 0) {
			char		*tstr = NULL;
			size_t    LDAP_URL_prefix_len = 0;
			size_t	tmplen = 0;
			type = ACI_TARGET_DN;
			/* Keep a copy of the target attr */
			if (aci_item->target) {
				return (ACL_SYNTAX_ERR);
			}
			if ( (s = strstr( str, "!=" )) != NULL ) {
				type |= ACI_TARGET_NOT;
				strncpy(s, " ", 1);
			}			
			if ( (s = strchr( str, '=' )) != NULL ) {
				value = s + 1;
				__acl_strip_leading_space(&value);
				__acl_strip_trailing_space(value);
				len =  strlen ( value );
				/* strip double quotes */
				if (*value == '"' &&  value[len-1] == '"') {
					value[len-1] = '\0';
					value++;
				}	
				__acl_strip_leading_space(&value);
			} else {
				return ( ACL_SYNTAX_ERR );
			}
			if (0 ==
				strncasecmp(value, LDAP_URL_prefix, strlen(LDAP_URL_prefix))) {
				LDAP_URL_prefix_len = strlen(LDAP_URL_prefix);
			} else if (0 == strncasecmp(value, LDAPS_URL_prefix,
										strlen(LDAPS_URL_prefix))) {
				LDAP_URL_prefix_len = strlen(LDAPS_URL_prefix);
			} else {
				return ( ACL_SYNTAX_ERR );
			}

			value += LDAP_URL_prefix_len;
			rv = slapi_dn_normalize_case_ext(value, 0, &tmpstr, &tmplen);
			if (rv < 0) {
				return ACL_SYNTAX_ERR;
			} else if (rv == 0) { /* value passed in; not null terminated */
				*(tmpstr + tmplen) = '\0';
			}
			tstr = slapi_ch_smprintf("(target=%s)", tmpstr);
			if (rv > 0) {
				slapi_ch_free_string(&tmpstr);
			}
			if ( (rv = acl_check_for_target_macro( aci_item, value)) == -1) {
				slapi_ch_free ( (void **) &tstr );				
				return(ACL_SYNTAX_ERR);
			} else if ( rv > 0) {
				/* is present, so the type is now ACL_TARGET_MACRO_DN */
				type = ACI_TARGET_MACRO_DN;
			} else {
				/* it's a normal target with no macros inside */	
				f = slapi_str2filter ( tstr );				
			}
			slapi_ch_free_string ( &tstr );
		} else {
			/* did start with a 't' but was not a recognsied keyword */
			return(ACL_SYNTAX_ERR);
		}

		/*
		 * Here, it was a recognised keyword that started with 't'.
		 * Check that the filter associated with ACI_TARGET_DN and
		 * ACI_TARGET_FILTER are OK.
		*/
		if (f == NULL) {
			/* The following types require a filter to have been created */
			if (type & ACI_TARGET_DN)
				return ACL_TARGET_FILTER_ERR;
			else if (type & ACI_TARGET_FILTER) 
				return ACL_TARGETFILTER_ERR;
		} else {
			int	filterChoice;

			filterChoice = slapi_filter_get_choice ( f );
			if ( (type & ACI_TARGET_DN) &&
				( filterChoice == LDAP_FILTER_PRESENT)) {
					slapi_log_error(SLAPI_LOG_ACL, plugin_name,
					"acl__parse_aci: Unsupported filter type:%d\n", filterChoice);
				return(ACL_SYNTAX_ERR);
			} else if (( filterChoice == LDAP_FILTER_SUBSTRINGS) &&
					(type & ACI_TARGET_DN)) {
				type &= ~ACI_TARGET_DN;
				type |= ACI_TARGET_PATTERN;
			}
		}

		if ((type & ACI_TARGET_DN) ||
			(type & ACI_TARGET_PATTERN)) {
			if (aci_item->target) {
				/* There is something already. ERROR */
				slapi_log_error(SLAPI_LOG_ACL, plugin_name,
					 "Multiple targets in the ACL syntax\n");
				slapi_filter_free(f, 1);
				return(ACL_SYNTAX_ERR);
			}  else {
				aci_item->target = f;
			}
		} else if ( type & ACI_TARGET_FILTER) {
			if (aci_item->targetFilter) {
				slapi_log_error(SLAPI_LOG_ACL, plugin_name,
				       "Multiple target Filters in the ACL Syntax\n");
				slapi_filter_free(f, 1);
				return(ACL_SYNTAX_ERR);
			} else {
				aci_item->targetFilter = f;
			}
		}
		break; /* 't' */
		default:
			/* Here the keyword did not start with 'v' ot 't' so error */
			slapi_log_error(SLAPI_LOG_ACL, plugin_name,
				       "Unknown keyword at \"%s\"\n Expecting"
						" \"target\", \"targetattr\", \"targetfilter\", \"targattrfilters\""						
						" or \"version\"\n", str);
			return(ACL_SYNTAX_ERR);
	}/* switch() */

	/* Store the type info */
	aci_item->aci_type |= type;

	return 0;
}

/***************************************************************************
* acl__sanity_check_acltxt
*
*	Check the input ACL text. Reports any errors. Also forgivs if certain 
*	things are missing.
*
* Input:
*	char 	*str		- String containg the acl text
*	int	*err		- error status
*
* Returns:
*		0     --- good status
*		<0 	  --- error
*
* Error Handling:
*	None.
*
*
**************************************************************************/
static int 
__aclp__sanity_check_acltxt (aci_t *aci_item, char *str) 
{
	NSErr_t         errp;
	char            *s;
	ACLListHandle_t *handle = NULL;
	char            *newstr = NULL;
	char            *word;
	char            *next;
	const char      *brkstr = " ;";
	int             checkversion = 0;

	memset (&errp, 0, sizeof(NSErr_t));
	newstr = str;

	while ((s = strstr(newstr, "authenticate")) != NULL) {
		char	*next;
		next = s + 12;
		s--;
		while (s > str && ldap_utf8isspace(s)) LDAP_UTF8DEC(s);
		if (s && *s == ';') {
			/* We don't support authenticate stuff */
			return ACL_INVALID_AUTHORIZATION;

		} else {
			newstr = next;
		}
	}

	newstr = slapi_ch_strdup (str);
	for (word = ldap_utf8strtok_r(newstr, brkstr, &next); word;
	     word = ldap_utf8strtok_r(NULL, brkstr, &next)) {
		if (0 == strcasecmp(word, "version")) {
			checkversion = 1;
		} else if (checkversion) {
			checkversion = 0;
			if ('3' != *word) {
				slapi_ch_free ( (void **) &newstr );
				return ACL_INCORRECT_ACI_VERSION;
			}
		} else if ((s = strstr(word, "($")) || (s = strstr(word, "[$"))) {
			int attr_macro = -1;

			/* See if this is a valid macro keyword. */
			if ((0 != strncasecmp(s, ACL_RULE_MACRO_DN_KEY,
			                      sizeof(ACL_RULE_MACRO_DN_KEY) - 1)) &&
			    (0 != strncasecmp(s, ACL_RULE_MACRO_DN_LEVELS_KEY,
			                      sizeof(ACL_RULE_MACRO_DN_LEVELS_KEY) - 1)) &&
			    (0 != (attr_macro = strncasecmp(s, ACL_RULE_MACRO_ATTR_KEY,
			                      sizeof(ACL_RULE_MACRO_ATTR_KEY) - 1)))) {
				slapi_ch_free ( (void **) &newstr );
				return ACL_SYNTAX_ERR;
			}

			/* For the $attr macro, validate that the attribute name is
			 * legal per RFC 4512. */
			if (attr_macro == 0) {
				int start = 1;
				char *p = NULL;

				for (p = s + sizeof(ACL_RULE_MACRO_ATTR_KEY) - 1;
					p && *p && *p != ')'; p++) {
					if (start) {
						if (!isalpha(*p)) {
							slapi_ch_free ( (void **) &newstr );
							return ACL_SYNTAX_ERR;
						}
						start = 0;
					} else {
						if (!(isalnum(*p) || (*p == '-'))) {
							slapi_ch_free ( (void **) &newstr );
							return ACL_SYNTAX_ERR;
						}
					}
				}
			}
		}
	}
	slapi_ch_free ( (void **) &newstr );

	/* We need to normalize the DNs in the userdn and group dn
	** so that, it's only done once.
	*/
	if ((newstr = __aclp__normalize_acltxt (aci_item,  str )) == NULL) {
		return ACL_SYNTAX_ERR;
	}
	slapi_log_error(SLAPI_LOG_ACL, plugin_name, "Normalized String:%s\n", newstr);

	/* check for acl syntax error */ 
	if ((handle = (ACLListHandle_t *) ACL_ParseString(&errp, newstr)) == NULL) {
		acl_print_acllib_err(&errp, str);
		slapi_ch_free_string(&newstr);
		return ACL_SYNTAX_ERR;
	} else {
		/* get the rights and the aci type */
		aci_item->aci_handle = handle;
		nserrDispose(&errp);
		slapi_ch_free_string(&newstr);

		return  0;
	}
}

/*
 * If the src includes "ldap(s):///<dn>", normalize <dn> and copy
 * the string starting from start to *dest.
 * If isstrict is non-zero, if ldap(s):/// is not included in the src
 * string, it returns an error (-1).  
 * If isstrict is zero, the string is copied as is.
 *
 * return value: 0 or positive: success
 *                    negative: failure
 */
int
__aclp__copy_normalized_str (char *src, char *endsrc, char *start,
							 char **dest, size_t *destlen, int isstrict)
{
	char *p = NULL;
	int rc = -1; 
	const char *dn = NULL;

	p = PL_strnstr(src, LDAP_URL_prefix, endsrc - src);
	if (p) {
		p += strlen(LDAP_URL_prefix);
	} else {
		p = PL_strnstr(src, LDAPS_URL_prefix, endsrc - src);
		if (p) {
			p += strlen(LDAPS_URL_prefix);
		}
	}

	if (isstrict && ((NULL == p) || 0 == strlen(p))) {
		return rc; /* error */
	}
	
	rc = 0;
	if (p && strlen(p) > 0) {
		size_t len = 0;
		Slapi_DN sdn;
		char bak;
		/* strip the string starting from ? */
		char *q = PL_strnchr(p, '?', endsrc - p);
		if (q) {
			len = q - p;
		} else {
			len = endsrc - p;
		}
		bak = *(p + len);
		*(p + len) = '\0';
		/* Normalize the value of userdn and append it to ret_str */
		slapi_sdn_init_dn_byref(&sdn, p);
		dn = slapi_sdn_get_dn(&sdn);
		/* Normalization failed so return an error (-1) */
		if (!dn) {
			slapi_sdn_done(&sdn);
			return -1;
		}
		/* append up to ldap(s):/// */
		aclutil_str_append_ext(dest, destlen, start, p - start);
		/* append the DN part */
		aclutil_str_append_ext(dest, destlen, dn, strlen(dn));
		slapi_sdn_done(&sdn);
		*(p + len) = bak;
		if (q) {
			/* append the rest from '?' */
			aclutil_str_append_ext(dest, destlen, q, endsrc - q);
		}
	} else {
		aclutil_str_append_ext(dest, destlen, start, endsrc - start);
	}

	return rc;
}

/******************************************************************************
*
* acl__normalize_acltxt
*
*
* XXXrbyrne this routine should be re-written when someone eventually 
* gets sick enough of it.  Same for getNextLAS() below.
* 
*	Normalize the acltxt i.e normalize all the DNs specified in the
*	Userdn and Groupdn rule so that we normalize once here and not 
*	over and over again at the runtime in the LASes. We have to normalize
*	before we generate the handle otherwise it's of no use.
*	Also convert deny to deny absolute
*
*	The string that comes in is something like:
*	version 3.0; acl "Dept domain administration"; allow (all)
*	 groupdn = "ldap:///cn=Domain Administrators, o=$dn.o, o=ISP"; )
*	
*	Returns NULL on error.
*
******************************************************************************/
static char *
__aclp__normalize_acltxt ( aci_t * aci_item, char * str )
{

	char		*s, *p;
	char		*end;
	char		*aclstr, *s_aclstr;
	char		*prevend = NULL;
	char		*ret_str = NULL;
	size_t		retstr_len = 0;
	int			len;
	char		*aclName;
	char		*nextACE;
	char		*tmp_str = NULL;
	char		*acestr = NULL;
	char		*s_acestr = NULL;
	int 		aci_rights_val 	= 0; /* bug 389975 */ 
	int			rc = 0;

	/* make a copy first */
	s_aclstr = aclstr = slapi_ch_strdup ( str );

	/* The rules are like this version 3.0; acl "xyz"; rule1; rule2; */
	s = strchr (aclstr, ';');
	if (NULL == s) {
		goto error;
	}
	aclstr = ++s;

	/* From DS 4.0, we support both aci (or aci) "name"  -- we have to change to acl
	** as libaccess will not like it
	*/
	s = aclstr;
	while (s && ldap_utf8isspace(s)) LDAP_UTF8INC(s);
	 *(s+2 ) = 'l';
	
	aclName = s+3;

	s = strchr (aclstr, ';');
	if (NULL == s) {
		goto error;
	}

	aclstr = s;
	LDAP_UTF8INC(aclstr);
	*s = '\0';

	/* Here aclName is the acl description string */
	aci_item->aclName = slapi_ch_strdup ( aclName );

	retstr_len = strlen(str) * 3;
	ret_str = (char *)slapi_ch_calloc(sizeof(char), retstr_len);
	aclutil_str_append_ext (&ret_str, &retstr_len, s_aclstr, strlen(s_aclstr));
	aclutil_str_append_ext (&ret_str, &retstr_len, ";", 1);

	/* start with the string */
	acestr = aclstr;

	/*
	 * Here acestr is something like:
	 *
	 * " allow (all) groupdn = "ldap:///cn=Domain Administrators, o=$dn.o, o=ISP";)"
	 */

normalize_nextACERule:

	/* now we are in the rule part */
	tmp_str = acestr;
	s = strchr (tmp_str, ';');
	if (s == NULL) {
		goto error;
	}

	nextACE = s;
	LDAP_UTF8INC(nextACE);
	*s = '\0';

	/* acestr now will hold copy of the ACE. Also add
	** some more space in case we need to add "absolute"
	** for deny rule. We will never need more 3 times 
	** the len (even if all the chars are escaped).
	*/
	__acl_strip_leading_space(&tmp_str);
	len = strlen (tmp_str);
	s_acestr = acestr = slapi_ch_calloc (1, 3 * len);

	/*
	 * Now it's something like:
	 * allow (all) groupdn = "ldap:///cn=Domain Administrators, o=$dn.o, o=ISP";
	 */
	if (strncasecmp(tmp_str, "allow", 5) == 0) {
		memcpy(acestr, tmp_str, len);
		tmp_str += 5;
		/* gather the rights */
		aci_rights_val =  __aclp__get_aci_right (tmp_str);/* bug 389975 */
		aci_item->aci_type |= ACI_HAS_ALLOW_RULE;

		s = strchr(acestr, ')');
		if (NULL == s) {
			/* wrong syntax */
			goto error;
		}
		/* add "allow(rights...)" */
		aclutil_str_append_ext(&ret_str, &retstr_len, acestr, s - acestr + 1);
		prevend = s + 1;
	} else if (strncasecmp(tmp_str, "deny", 4) == 0) {
		char 		*d_rule ="deny absolute";
		/* Then we have to  add "absolute" to the deny rule
		**  What we are doing here is to tackle this situation.
		**
		** allow -- deny -- allow
		** deny -- allow
		**
		** by using deny absolute we force the precedence rule
		** i.e deny has a precedence over allow. Since there doesn't 
		** seem to be an easy to detect the mix, forcing this
		** to all the deny rules will do the job.
		*/
		__acl_strip_leading_space(&tmp_str);
		tmp_str += 4;

		/* We might have an absolute there already */
		if ((s = strstr (tmp_str, "absolute")) != NULL) {
			tmp_str = s;
			tmp_str += 8;
		}
		/* gather the rights */
		aci_rights_val =  __aclp__get_aci_right (tmp_str);/* bug 389975 */
		aci_item->aci_type |= ACI_HAS_DENY_RULE;

		len = strlen ( d_rule );
		memcpy (acestr, d_rule, len );
		memcpy (acestr+len, tmp_str, strlen (tmp_str) );

		s = strchr(acestr, ')');
		if (NULL == s) {
			/* wrong syntax */
			goto error;
		}
		/* add "deny(rights...)" */
		aclutil_str_append_ext(&ret_str, &retstr_len, acestr, s - acestr + 1);
		prevend = s + 1;
	} else {
		/* wrong syntax */
		aci_rights_val = -1 ;
	}
	if (aci_rights_val == -1 )
	{
		/* wrong syntax */
		goto error;
	} else
		aci_item->aci_access |= aci_rights_val;
 
	/* Normalize all the DNs in the userdn, groupdn, roledn rules */
	/*
	 *
	 * Here acestr starts like this:
	 * " allow (all) groupdn = "ldap:///cn=Domain Administrators,o=$dn.o,o=ISP"
	 */
	s =  __aclp__getNextLASRule(aci_item, acestr, &end);
	while ( s && (s < end) ) {
		if ( (0 == strncmp(s, DS_LAS_USERDNATTR, 10)) ||
			 (0 == strncmp(s, DS_LAS_USERATTR, 8)) ) {
			/* 
			** For userdnattr/userattr rule, the resources changes and hence
			** we cannot cache the result. See above for more comments.
			*/
			aci_item->aci_elevel = ACI_ELEVEL_USERDNATTR;

			rc = __aclp__copy_normalized_str(s, end, prevend,
											 &ret_str, &retstr_len, 0);
			if (rc < 0) {
				goto error;
			}
		} else if ( 0 == strncmp ( s, DS_LAS_USERDN, 6 )) {
                        char *prefix;
                        
			p = PL_strnchr (s, '=', end - s);
			if (NULL == p) {
				goto error;
			}
			p--;
			if ( strncmp (p, "!=", 2) == 0 ) {
				aci_item->aci_type |= ACI_CONTAIN_NOT_USERDN;
			}

			/* XXXrbyrne
			 * Here we need to scan for more ldap:/// within
			 * this userdn rule type:
			 * eg. userdn = "ldap:///cn=joe,o=sun.com || ldap:///self"
			 * This is handled correctly in DS_LASUserDnEval
			 * but the bug here is not setting ACI_USERDN_SELFRULE
			 * which would ensure that acl info is not cached from
			 * one resource entry to the next. (bug 558519)
			*/
			rc = __aclp__copy_normalized_str(s, end, prevend,
											 &ret_str, &retstr_len, 1);
			if (rc < 0) {
				goto error;
			}

                        /* skip the ldap prefix */
                        prefix = PL_strncasestr(p, LDAP_URL_prefix, end - p);
                        if (prefix) {
                                prefix += strlen(LDAP_URL_prefix);
                        } else {
                                prefix = PL_strncasestr(p, LDAPS_URL_prefix, end - p);
                                if (prefix) {
                                        prefix += strlen(LDAPS_URL_prefix);
                                }
                        }
                        if (prefix == NULL) {
                                /* userdn value does not starts with LDAP(S)_URL_prefix */
                                goto error;
                        }
                        p = prefix;


			/* we have a rule like userdn = "ldap:///blah". s points to blah now.
			** let's find if we have a SELF rule like userdn = "ldap:///self".
			** Since the resource changes on entry basis, we can't cache the 
			** evalation of handle for all time. The cache result is valid
			** within the evaluation of that resource.
			*/
			if (strncasecmp(p, "self", 4) == 0) {
				aci_item->aci_ruleType |= ACI_USERDN_SELFRULE;
			} else if ( strncasecmp(p, "anyone", 6) == 0 ) {
				aci_item->aci_elevel = ACI_ELEVEL_USERDN_ANYONE;

			} else if ( strncasecmp(p, "all", 3) == 0 ) {
				if ( aci_item->aci_elevel > ACI_ELEVEL_USERDN_ALL )
					aci_item->aci_elevel = ACI_ELEVEL_USERDN_ALL;

			} else {
				if ( aci_item->aci_elevel > ACI_ELEVEL_USERDN )
					aci_item->aci_elevel = ACI_ELEVEL_USERDN;
			}

			/* See if we have a parameterized rule */
			__aclp_chk_paramRules ( aci_item, p, end );			
		} else if ( 0 == strncmp ( s, DS_LAS_GROUPDNATTR, 11)) {
			/* 
			** For groupdnattr rule, the resources changes and hence
			** we cannot cache the result. See above for more comments.
			*/
			/* Find out if we have a URL type of rule */
			p = PL_strnstr (s, "ldap", end - s);
			if (NULL != p) {
				if ( aci_item->aci_elevel > ACI_ELEVEL_GROUPDNATTR_URL )
					aci_item->aci_elevel = ACI_ELEVEL_GROUPDNATTR_URL;
			} else if ( aci_item->aci_elevel > ACI_ELEVEL_GROUPDNATTR ) {
				aci_item->aci_elevel = ACI_ELEVEL_GROUPDNATTR;
			}
			aci_item->aci_ruleType |= ACI_GROUPDNATTR_RULE;

			rc = __aclp__copy_normalized_str(s, end, prevend,
											 &ret_str, &retstr_len, 0);
			if (rc < 0) {
				goto error;
			}
		} else if ( 0 == strncmp ( s, DS_LAS_GROUPDN, 7)) {

			p = PL_strnchr (s, '=', end - s);
			if (NULL == p) {
				goto error;
			}
			p--;
			if ( strncmp (p, "!=", 2) == 0)
				aci_item->aci_type |= ACI_CONTAIN_NOT_GROUPDN;

			rc = __aclp__copy_normalized_str(s, end, prevend,
											 &ret_str, &retstr_len, 1);
			if (rc < 0) {
				goto error;
			}

			/* check for param rules */
			__aclp_chk_paramRules ( aci_item, p, end );

			if ( aci_item->aci_elevel > ACI_ELEVEL_GROUPDN )
				aci_item->aci_elevel = ACI_ELEVEL_GROUPDN;
			aci_item->aci_ruleType |= ACI_GROUPDN_RULE;
	
		} else if ( 0 == strncmp ( s, DS_LAS_ROLEDN, 6)) {

			p = PL_strnchr (s, '=', end - s);
			if (NULL == p) {
				goto error;
			}
			p--;
			if ( strncmp (p, "!=", 2) == 0)
				aci_item->aci_type |= ACI_CONTAIN_NOT_ROLEDN;

			rc = __aclp__copy_normalized_str(s, end, prevend,
											 &ret_str, &retstr_len, 1);
			if (rc < 0) {
				goto error;
			}

			/* check for param rules */
			__aclp_chk_paramRules ( aci_item, p, end );

			/* XXX need this for roledn ?
			if ( aci_item->aci_elevel > ACI_ELEVEL_GROUPDN )
				aci_item->aci_elevel = ACI_ELEVEL_GROUPDN;*/
			aci_item->aci_ruleType |= ACI_ROLEDN_RULE;
		} else {
			/* adding the string no need to be processed
			 * (e.g., dns="lab.example.com)" */
			aclutil_str_append_ext(&ret_str, &retstr_len, 
									prevend, end - prevend);
		}
		prevend = end;
		s = ++end;
		s =  __aclp__getNextLASRule(aci_item, s, &end);
		if (NULL == s) {
			/* adding the rest of the string, e.g. '\"' */
			aclutil_str_append_ext(&ret_str, &retstr_len, 
									prevend, strlen(prevend));
		}
	} /* while */

	slapi_ch_free_string (&s_acestr);
    __acl_strip_trailing_space(ret_str);
	aclutil_str_append_ext(&ret_str, &retstr_len, ";", 1);

	if (nextACE) {
		s = strstr (nextACE, "allow");
		if (s == NULL) s = strstr (nextACE, "deny");
		if (s == NULL)  {
			if (nextACE && *nextACE != '\0')
				aclutil_str_append (&ret_str, nextACE);
			slapi_ch_free_string (&s_aclstr);
			return (ret_str);
		}
		acestr = nextACE;
		goto normalize_nextACERule;
	}

	slapi_ch_free_string (&s_aclstr);
	return (ret_str);

error:
	slapi_ch_free_string (&ret_str);
	slapi_ch_free_string (&s_aclstr);
	slapi_ch_free_string (&s_acestr);
	return NULL;
}
/*
 * 
 * acl__getNextLASRule
 *	Find the next rule. 
 *
 * Returns:
 *    endOfCurrRule	- end of current rule
 *    nextRule		- start of next rule
 */
static char *
__aclp__getNextLASRule (aci_t *aci_item, char *original_str , char **endOfCurrRule)
{
	char *newstr = NULL, *word = NULL, *next = NULL, *start = NULL, *end = NULL;
	char *ruleStart = NULL;
	int  len, ruleLen = 0;
	int  in_dn_expr = 0;

	if (endOfCurrRule) {
		*endOfCurrRule = NULL;
	}
	newstr = slapi_ch_strdup (original_str);
	
	if ( (strncasecmp(newstr, "allow", 5) == 0) ||
		 (strncasecmp(newstr, "deny", 4) == 0) )  {
		ldap_utf8strtok_r(newstr, ")", &next);
	} else {
		ldap_utf8strtok_r(newstr, " ", &next);
	}

	/*
	 * The first word is of no interest  -- skip it
	 * it's allow or deny followed by the rights (<rights>),
	 * so skip over the rights as well or it's 'and', 'or',....
	 */
 
	while ( (word = ldap_utf8strtok_r(NULL, " ", &next)) != NULL) {
			int		got_rule = 0;
			int		ruleType = 0;
			/* 
			** The next word must be one of these to be considered
			** a valid rule.
			** This is making me crazy. We might have a case like
			** "((userdn=". strtok is returning me that word.
			*/
			len = strlen ( word );
			word [len] = '\0';

			if ( (ruleStart= strstr(word, DS_LAS_USERDNATTR)) != NULL) {
				ruleType |= ACI_USERDNATTR_RULE;
				ruleLen = strlen ( DS_LAS_USERDNATTR) ;
			} else if ( (ruleStart = strstr(word, DS_LAS_USERDN)) != NULL) {
				ruleType = ACI_USERDN_RULE;
				ruleLen = strlen ( DS_LAS_USERDN);
				in_dn_expr = 1;
			} else if ( (ruleStart = strstr(word, DS_LAS_GROUPDNATTR)) != NULL) {
				ruleType = ACI_GROUPDNATTR_RULE;
				ruleLen = strlen ( DS_LAS_GROUPDNATTR) ;
			} else if ((ruleStart= strstr(word, DS_LAS_GROUPDN)) != NULL) {
				ruleType = ACI_GROUPDN_RULE;
				ruleLen = strlen ( DS_LAS_GROUPDN) ;
				in_dn_expr = 1;
			} else if ((ruleStart = strstr(word, DS_LAS_USERATTR)) != NULL) {
				ruleType = ACI_USERATTR_RULE;
				ruleLen = strlen ( DS_LAS_USERATTR) ;
			} else if ((ruleStart= strstr(word, DS_LAS_ROLEDN)) != NULL) {
				ruleType = ACI_ROLEDN_RULE;
				ruleLen = strlen ( DS_LAS_ROLEDN);
				in_dn_expr = 1;
			} else if ((ruleStart= strstr(word, DS_LAS_AUTHMETHOD)) != NULL) {
				ruleType = ACI_AUTHMETHOD_RULE;
				ruleLen = strlen ( DS_LAS_AUTHMETHOD);
			} else if ((ruleStart = strstr(word, ACL_ATTR_IP)) != NULL) { 
				ruleType = ACI_IP_RULE;
				ruleLen = strlen ( ACL_ATTR_IP) ;
			} else if ((ruleStart = strstr(word, DS_LAS_TIMEOFDAY)) != NULL)  {
				ruleType = ACI_TIMEOFDAY_RULE;
				ruleLen = strlen ( DS_LAS_TIMEOFDAY) ;
			} else if ((ruleStart = strstr(word, DS_LAS_DAYOFWEEK)) != NULL) {
				ruleType = ACI_DAYOFWEEK_RULE;
				ruleLen = strlen ( DS_LAS_DAYOFWEEK) ;
			} else if ((ruleStart = strstr(word, ACL_ATTR_DNS)) != NULL) {
				ruleType = ACI_DNS_RULE;
				ruleLen = strlen ( ACL_ATTR_DNS) ;
			} else if ((ruleStart = strstr(word, DS_LAS_SSF)) != NULL) {
				ruleType = ACI_SSF_RULE;
				ruleLen = strlen ( DS_LAS_SSF) ;
			}
			/* Here, we've found a space...if we were in in_dn_expr mode
			 * and we'vve found a closure for that ie.a '"' or a ')'
			 * eg. "'ldap:///all"' or 'ldap:///all")' then exit in_dn_expr mode.
			*/
			if ( in_dn_expr && (word[len-1] == '"' ||
								(len>1 && word[len-2] == '"') ||
                                (len>2 && word[len-3] == '"')) ) {
				in_dn_expr = 0;
			}

			/*
			 * ruleStart may be NULL as word could be (all) for example.
			 * this word will just be skipped--we're really waiting for 
			 * userdn or groupdn or...
			*/ 

			if ( ruleStart && ruleType ) {
				/* Look in the current word for "=" or else look into
				** the next word -- if none of them are true, then this
				** is not the start of the rule 
				*/
				char	*tmpStr = ruleStart + ruleLen;
				if ( strchr ( tmpStr, '=')  ||
						((word = ldap_utf8strtok_r(NULL, " ", &next) ) &&
							word && ((strncmp ( word, "=", 1) == 0 ) ||
									 (strncmp ( word, "!=",2) ==0)	 ||
									 (strncmp ( word, ">", 1) == 0 ) ||
									 (strncmp ( word, "<", 1) == 0 ) ||
									 (strncmp ( word, "<", 1) == 0 ) ||
									 (strncmp ( word, "<=",2) ==0 )	 ||
									 (strncmp ( word, ">=",2) ==0)   ||
									 (strncmp ( word, "=>",2) ==0)   ||
									 (strncmp ( word, "=<",2) ==0))
																	 ) ) {
					aci_item->aci_ruleType |= ruleType;
					got_rule = 1;
				}
			}
			if ( NULL == start && got_rule ) {
				/*
				 * We've just found a rule start--keep going though because
				 * we need to return the end of this rule too.
				*/
				start= ruleStart;
				got_rule = 0;
			} else {
				/*
				 * Here, we have a candidate for the end of the rule we've found
				 * (the start of which is currently in start).
				 * But we need to be sure it really is the end and not a
				 * "fake end" due to a keyword bbeing embeded in a dn.
				*/
				if (word && !in_dn_expr &&
					((strcasecmp(word, "and") == 0) ||
					 (strcasecmp(word, "or") == 0) ||
					 (strcasecmp(word, "not") == 0) ||
					 (strcasecmp(word, ";") == 0))) {
					/* If we have start, then it really is the end */
					word--;
					if (start) {
						end = word;
						break;
					} else { 
					/* We found a fake end, but we've no start so keep going */						
					}
				}
			}
	} /* while */

	if ( end ) {
		/* Found an end to the rule and it's not the last rule */
		len = end - newstr;
		end = original_str + len;
		while ( (end != original_str) && *end != '\"' ) end--;
		if (end == original_str) {
			char *tmpp = NULL;
			/* The rule has a problem!  Not double quoted?
			   It should be like this:
			   userdn="ldap:///cn=*,ou=testou,o=example.com"
			   But we got this?
			   userdn=ldap:///cn=*,ou=testou,o=example.com
			 */
			tmpp = original_str + len;
			/* Just excluding the trailing spaces */
			while ( (tmpp != original_str) && *tmpp == ' ' ) tmpp--;
			if (tmpp != original_str) {
				tmpp++;
			}
			end = tmpp;
		}
		if (endOfCurrRule) {
			*endOfCurrRule = end;
		}
		len = start - newstr;
		ruleStart =  original_str + len;
	} else {
		/* Walked off the end of the string so it's the last rule */
		end = original_str + strlen(original_str) - 1;
		while ( (end != original_str) && *end != '\"' ) end--;
		if (end == original_str) {
			char *tmpp = NULL;
			/* The rule has a problem!  Not double quoted?
			   It should be like this:
			   userdn="ldap:///cn=*,ou=testou,o=example.com"
			   But we got this?
			   userdn=ldap:///cn=*,ou=testou,o=example.com
			 */
			tmpp = original_str + strlen(original_str) - 1;
			/* Just excluding the trailing spaces */
			while ( (tmpp != original_str) && *tmpp == ' ' ) tmpp--;
			if (tmpp != original_str) {
				tmpp++;
			}
			end = tmpp;
		}
		if (endOfCurrRule) {
			*endOfCurrRule = end;
		}
	}
	if ( start ) {
		/* Got a rule, fixup the pointer */
		len = start - newstr;
		ruleStart =  original_str + len;
	}
	slapi_ch_free ( (void **) &newstr );

	/*
	 * Here, ruleStart points to the start of the next rule in original_str.
	 * end points to the end of this rule.
	*/

	return ( ruleStart );
}

/***************************************************************************
* acl__get_aci_right
*
* 	Go thru the one acl text str and figure our the rights declared.
*
*****************************************************************************/
static int 
__aclp__get_aci_right (char *str)
{

	char	*sav_str = slapi_ch_strdup(str);
	char	*t, *tt;
	int   	type = 0;
	char 	*delimiter = ",";
	char 	*val = NULL;
	int 	aclval = 0;

	t = sav_str;
	__acl_strip_leading_space( &t );

	if (*t == '(' ) {
		if ((tt = slapi_find_matching_paren(t)) == NULL) {
			slapi_ch_free ( (void **) &sav_str );
			return -1;
		} else {
			t++; /* skip the first character which is ( */
			*tt = '\0';
		}
	} else {
		slapi_ch_free ( (void **) &sav_str );	
		return -1;
	}
 	/* get the tokens separated by ","  */	
	val = ldap_utf8strtok_r(t,delimiter, &tt);
	if (val == NULL )
	{
		slapi_ch_free ( (void **) &sav_str );
		return -1;
	}
	while (val != NULL)
	{
		/* get the corresponding integer value */
		aclval = get_acl_rights_as_int(val);
		if (aclval == -1 )
		{
			type = -1;
			break;
		}
		type |= aclval;
		val = ldap_utf8strtok_r(NULL,delimiter, &tt); /* get the next token */ 
	}

	slapi_ch_free ( (void **) &sav_str );
	return type;
	
}

static int get_acl_rights_as_int( char * strValue)
{

	if (strValue == NULL )
		return -1;
	/* First strip out the leading and trailing spaces  */
	__acl_strip_leading_space( &strValue );
	__acl_strip_trailing_space( strValue );
	
	/* We have to do a strcasecmp (case insensitive cmp) becuase we should return 
	   only if it is exact match. */
	
	if (strcasecmp (strValue, "read") == 0 )
		return SLAPI_ACL_READ;
	else if (strcasecmp (strValue, "write") == 0 )
		return SLAPI_ACL_WRITE;
	else if (strcasecmp (strValue, "search") == 0 )
		return SLAPI_ACL_SEARCH;
	else if (strcasecmp (strValue, "compare") == 0 )
		return SLAPI_ACL_COMPARE;
	else if (strcasecmp (strValue, "add") == 0 )
		return SLAPI_ACL_ADD;
	else if (strcasecmp (strValue, "delete") == 0 )
		return SLAPI_ACL_DELETE;
	else if (strcasecmp (strValue, "proxy") == 0 )
		return SLAPI_ACL_PROXY;
	else if (strcasecmp (strValue, "selfwrite") == 0 )
		return (SLAPI_ACL_SELF | SLAPI_ACL_WRITE);
	else if (strcasecmp (strValue, "all") == 0 )
		return SLAPI_ACL_ALL;
	else
		return -1; /* error */
}
/***************************************************************************
*
* acl_access2str
*
*	Convert the access bits into character strings.
*	Example: "read, self read"
*
* Input:
*
*	int	access		- The access in bits
*	char	**rights	- rights in chars
*
* Returns:
*	NULL			- No rights to start with
*	right			- rights converted.
*
* Error Handling:
*	None.
*
**************************************************************************/
char *
acl_access2str(int access)
{

	if ( access & SLAPI_ACL_COMPARE ) {
		return access_str_compare;
	} else if ( access & SLAPI_ACL_SEARCH ) {
		return access_str_search;
	} else if ( access & SLAPI_ACL_READ ) {
		return access_str_read;
	} else if ( access & SLAPI_ACL_DELETE) {
		return  access_str_delete;
	} else if ( access & SLAPI_ACL_ADD) {
		return  access_str_add;
	} else if ( (access & SLAPI_ACL_WRITE ) && (access & SLAPI_ACL_SELF)) {
		return access_str_selfwrite;
	} else if (access & SLAPI_ACL_WRITE ) {
		return access_str_write;
	} else if (access & SLAPI_ACL_PROXY ) {
		return access_str_proxy;
	}

	return NULL;
}
/***************************************************************************
*
* __aclp__init_targetattr
*
*	Parse the targetattr string and create a array of attrs. This will 
*	help us to do evaluation at run time little faster.
*	entry.
*	Here, also extract any target value filters.
*
* Input:
*	aci_t	*aci		-- The aci item
*	char	*str		-- the targetattr string
*
* Returns:
*	ACL_OK			- everything ok
*	ACL_SYNTAX_ERROR	- in case of error.
*
*	
***************************************************************************/
static int 
__aclp__init_targetattr (aci_t *aci, char *attr_val, char **errbuf)
{

	int		numattr=0;
	Targetattr	**attrArray;
	char		*s, *end_attr, *str;
	int		len;
	Targetattr	*attr = NULL;

	s = strchr (attr_val, '=');
	if (NULL == s) {
		return ACL_SYNTAX_ERR;
	}
	s++;
	__acl_strip_leading_space(&s);
	__acl_strip_trailing_space(s);
	len = strlen(s);
    /* Simple targetattr statements may not be quoted e.g.
       targetattr=* or targetattr=userPassword
       if it begins with a quote, it must end with one as well
    */
	if (*s == '"') {
		if (s[len-1] == '"') {
			s[len-1] = '\0'; /* trim trailing quote */
		} else {
			/* error - if it begins with a quote, it must end with a quote */
			char *errstr = 
					slapi_ch_smprintf("The statement does not begin and end "
					                  "with a \": [%s]. ", attr_val);
			slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
							"__aclp__init_targetattr: %s\n", errstr);
			if (errbuf) {
				aclutil_str_append(errbuf, errstr);
			}
			slapi_ch_free_string(&errstr);
			return ACL_SYNTAX_ERR;
		}
		s++; /* skip leading quote */
	}

	str = s;
	attrArray = aci->targetAttr;

	if (attrArray[0] != NULL) {
		/* 
		** That means we are visiting more than once. 
		** Syntax error. We have a case like:  (targetattr) (targetattr) 
		*/
		return ACL_SYNTAX_ERR;
	}

	while (str != 0 && *str != 0) {
		int lenstr = 0;

		__acl_strip_leading_space(&str);

		if ((end_attr = strstr(str, "||")) != NULL) {
			/* skip the two '|'  chars */
			auto char *t = end_attr;
			LDAP_UTF8INC(end_attr);
			LDAP_UTF8INC(end_attr);
			*t = 0;
		}
		__acl_strip_trailing_space(str);

		/*
		 * Here:
		 * end_attr points to the next attribute thing.
		 *
		 * str points to the current one to be processed and it looks like this:
		 * rbyrneXXX Watchout is it OK to use : as the speperator ?
		 * cn
		 * c*n*
		 * *
		 *
		 * The attribute goes in the attrTarget list.
		 */
		attr = (Targetattr *) slapi_ch_malloc (sizeof (Targetattr));
		memset (attr, 0, sizeof(Targetattr));
                                
		/* strip double quotes */
		lenstr = strlen(str);
		if (*str == '"' && *(str + lenstr - 1) == '"') {
			*(str + lenstr - 1) = '\0';
			str++;
		}
		if (strchr(str, '*')) {
			/* It contains a * so it's something like * or cn* */
			if (strcmp(str, "*" ) != 0) {
				char			line[100];
				char *lineptr = &line[0];
				char *newline = NULL;
				struct  slapi_filter	*f = NULL;

				if (lenstr > 92) { /* 100 - 8 for "(attr=%s)\0" */
					newline = slapi_ch_malloc(lenstr + 8);
					lineptr = newline;
				}

				attr->attr_type = ACL_ATTR_FILTER;
				sprintf (lineptr, "(attr=%s)", str);
				f = slapi_str2filter (lineptr);
	
				if (f == NULL)  {
					char *errstr = slapi_ch_smprintf("Unable to generate filter"
					                                 " (%s). ", lineptr);
					slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
							"__aclp__init_targetattr: %s\n", errstr);
					if (errbuf) {
						aclutil_str_append(errbuf, errstr);
					}
					slapi_ch_free_string(&errstr);
				} else {
					attr->u.attr_filter = f;
				}

				slapi_ch_free_string(&newline);
			} else {
				attr->attr_type = ACL_ATTR_STAR;
				attr->u.attr_str = slapi_ch_strdup (str);
			}

		} else {
			/* targetattr = str or targetattr != str */
			/* Make sure str is a valid attribute */
			if (slapi_attr_syntax_exists((const char *)str)) {
				attr->u.attr_str = slapi_ch_strdup (str);
				attr->attr_type = ACL_ATTR_STRING;
			} else {
				char *errstr = slapi_ch_smprintf("targetattr \"%s\" does not "
				                  "exist in schema. Please add attributeTypes "
				                  "\"%s\" to schema if necessary. ", str, str);
				slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
				                "__aclp__init_targetattr: %s\n", errstr);
				if (errbuf) {
					aclutil_str_append(errbuf, errstr);
				}
				slapi_ch_free_string(&errstr);
				slapi_ch_free((void **)&attr);
				/* NULL terminate the list - the realloc below does not NULL terminate
				   the list, and the list is normally only NULL terminated when the
				   function returns with success */
				attrArray[numattr] = NULL;
				return ACL_SYNTAX_ERR;
			}
		}

		/*
		 * Add the attr to the targetAttr list
		 */

		attrArray[numattr] = attr;
		numattr++;
		if (!(numattr % ACL_INIT_ATTR_ARRAY)) {
			aci->targetAttr = (Targetattr **) slapi_ch_realloc (
						    (void *) aci->targetAttr,
						    (numattr+ACL_INIT_ATTR_ARRAY) *
						     sizeof(Targetattr *));
            				attrArray = aci->targetAttr;
		}

		/* Move on to the next attribute in the list */
		str = end_attr;

	} /* while */

	/* NULL teminate the list */
	attrArray[numattr] = NULL;
	return 0;
}

void
acl_strcpy_special (char *d, char *s)
{
    for (; *s; LDAP_UTF8INC(s)) {
	switch (*s) {
	  case '.':
	  case '\\':
	  case '[':
	  case ']':
	  case '*':
	  case '+':
	  case '^':
	  case '$':
	    *d = '\\';
	    LDAP_UTF8INC(d);
	    /* FALL */
	  default:
	    d += LDAP_UTF8COPY(d, s);
	}
    }
    *d = '\0';
}
/***************************************************************************
*
* acl_verify_aci_syntax
*    verify if the aci's being added for the entry has a valid syntax or not.
*
* Input:
*	Slapi_PBlock	*pb	- Parameter block
*	Slapi_Entry	*e		- The Slapi_Entry itself
*	char	**errbuf;	-- error message
*
* Returns:
*	-1 (ACL_ERR)		- Syntax error
*	0			- No error
*
* Error Handling:
*	None.
*
**************************************************************************/
int
acl_verify_aci_syntax (Slapi_PBlock *pb, Slapi_Entry *e, char **errbuf)
{

	if (e != NULL) {
		Slapi_DN		*e_sdn;
		int				rv;
		Slapi_Attr		*attr = NULL;
		Slapi_Value     *sval=NULL;
		const struct berval	*attrVal;
		int i;

		e_sdn = slapi_entry_get_sdn ( e );
	
		slapi_entry_attr_find (e, aci_attr_type, &attr);
		if  (! attr ) return 0;

		i= slapi_attr_first_value ( attr,&sval );
		while ( i != -1 ) {
			attrVal = slapi_value_get_berval ( sval );
			rv = acl_verify_syntax( pb, e_sdn, attrVal, errbuf );
			if ( 0 != rv ) {
				aclutil_print_err(rv, e_sdn, attrVal, errbuf);
				return ACL_ERR;
			}
			i = slapi_attr_next_value ( attr, i, &sval );
		}
	}
	return(0);
}	
/***************************************************************************
*
* acl__verify_syntax
*	Called from slapi_acl_check_mods() to verify if the new aci being
*	added/replaced has the right syntax or not.
*
* Input:
*	Slapi_PBlock	*pb	- Parameter block
*	Slapi_DN	*e_sdn	- sdn of the entry
*	berval	 *bval		- The berval containg the aci value
*
* Returns:
*	return values from acl__parse_aci()
*
* Error Handling:
*	None.
*
**************************************************************************/

int
acl_verify_syntax(Slapi_PBlock *pb, const Slapi_DN *e_sdn,
	const struct berval *bval, char **errbuf)
{
	aci_t			*aci_item;
	int			rv = 0;
	char			*str;
	aci_item = acllist_get_aci_new ();
	slapi_sdn_set_ndn_byval ( aci_item->aci_sdn, slapi_sdn_get_ndn ( e_sdn ) );

	/* make a copy the the string */
	str = slapi_ch_strdup(bval->bv_val);
	rv = acl_parse(pb, str, aci_item, errbuf);

	/* cleanup before you leave ... */
	acllist_free_aci (aci_item);
	slapi_ch_free ( (void **) &str );
	return(rv);
}
static void
__aclp_chk_paramRules ( aci_t *aci_item, char *start, char *end)
{

	size_t 			len;
	char			*str;
	char			*p, *s;


	len = end - start;

	s = str = (char *) slapi_ch_calloc(1, len + 1);
	memcpy ( str, start, len);
	while ( (p= strchr ( s, '$'))  != NULL) {
		p++;		/* skip the $ */
		if ( 0 == strncasecmp ( p, "dn", 2))
			aci_item->aci_ruleType |= ACI_PARAM_DNRULE;
		else if ( 0 == strncasecmp ( p, "attr", 4))
			aci_item->aci_ruleType |= ACI_PARAM_ATTRRULE;
		
		s = p;
	}
	slapi_ch_free ( (void **) &str );
}

/*
 * Check for an ocurrence of a macro aci in the target.
 * value is the normalized target string.
 *
 * this is something like:
 * (target="ldap:///cn=*,ou=people,($dn),o=sun.com")
 *
 *
 * returns 1 if there is a $dn present.
 * returns 0 if not.
 * returns -1 is syntax error.
 * If succes then:
 * ACI_TARGET_MACRO_DN is the type.
 * type can also include, ACI_TARGET_PATTERN, ACI_TARGET_NOT.
 * Also aci_item->aci_macro->match_this is set to be
 * cn=*,ou=people,($dn),o=sun.com, to be used later.
 * 
 * . we allow at most one ($dn) in a target.
 * . if a "*" accurs with it, it must be the first component and at most
 *   once.
 * . it's ok for ($dn) to occur on it's own in a target, but if it appears in
 * a user rule, then it must be in the target. 
 *  
 *
 * 
*/

static int
acl_check_for_target_macro( aci_t *aci_item, char *value)
{

	char			*str = NULL;

	str = strstr(value, ACL_TARGET_MACRO_DN_KEY /* ($dn) */);	
	
	if (str != NULL) {
		char *p0 = NULL, *p1 = NULL;
		/* Syntax check: 
		 * error return if ($dn) is in '[' and ']', e.g., "[($dn)]" */
		p0 = strchr(value, '[');
		if (p0 && p0 < str) {
			p1 = strchr(value, ']');
			if (p1 && p1 < str) {
				/* [...] ... ($dn) : good */
				;
			} else {
				/* [...($dn)...] or [...($dn... : bad */
				return -1;
			}
		}
		aci_item->aci_type &= ~ACI_TARGET_DN;
		aci_item->aci_type |= ACI_TARGET_MACRO_DN;
		aci_item->aci_macro = (aciMacro *)slapi_ch_malloc(sizeof(aciMacro));
		aci_item->aci_macro->match_this = slapi_ch_strdup(value);
		aci_item->aci_macro->macro_ptr = strstr( aci_item->aci_macro->match_this,
												 ACL_TARGET_MACRO_DN_KEY);
		return(1);											
	}

	return(0);
}

/* Strip trailing spaces from str by writing '\0' into them */

static void
__acl_strip_trailing_space( char *str) {

	char *ptr = NULL;
	int	len = 0;

	if (*str) {
		/* ignore trailing whitespace */
		len = strlen(str);
		ptr = str+len-1;
		while(ptr >= str && ldap_utf8isspace(ptr)) {
			*ptr = '\0';
			LDAP_UTF8DEC(ptr);
		}
	}
}

/*
 * Strip leading spaces by resetting str to point to the first
 * non-space charater.
*/

static void
__acl_strip_leading_space( char **str) {

	char *tmp_ptr = NULL;

	tmp_ptr = *str;
	while ( *tmp_ptr && ldap_utf8isspace( tmp_ptr ) ) LDAP_UTF8INC(tmp_ptr);
	*str = tmp_ptr;
}


/*
 * str is a string containing an LDAP filter.
 * Trim off enclosing quotes and enclosing
 * superfluous brackets.
 * The result is duped so it can be kept.
*/

static char *
__acl_trim_filterstr( char * str ) { 

	char *tmpstr;
	int 	len;
	char *end;

	tmpstr = str;

	/* If the last char is a "," take it out */

	len = strlen (tmpstr);
	if (len>0 && (tmpstr[len-1] == ',')) {
		tmpstr [len-1] = '\0';
	}


	/* Does it have quotes around it */
	len = strlen (tmpstr);
	if (*tmpstr == '"' && tmpstr[len-1] == '"') {
		tmpstr [len-1] = '\0';
		tmpstr++;
	}

	str = tmpstr;

	/* If we have a filter like
	** (((&(...) (...)))), we need to get rid of the
	** multiple parens or slapi_str2filter will not
	** evaluate properly. Need to package like
	** (filter ). probably I should fix str2filter
	** code.
	*/

	while (*tmpstr++ == '(' && *tmpstr == '(') {
		if ((end = slapi_find_matching_paren( str )) != NULL) {
			*end = '\0';
			str++;
		}
	}

	return( slapi_ch_strdup(str));
}

/*
 * Here str points to a targetattrfilters thing which looks tlike this:
 *
 * targetattrfilters="add=attr1:F1 && attr2:F2 ... && attrn:Fn,
 *                    del=attr1:F1 && attr2:F2... && attrn:Fn")
 * 
 *
 */

static int __acl__init_targetattrfilters( aci_t *aci, char *input_str) {

	char	*s, *str;
	int		len;	
	char 	*addlistptr = NULL;
	char 	*dellistptr = NULL;

	if (aci->targetAttrAddFilters != NULL ||
		aci->targetAttrDelFilters != NULL) {

		/* 
		** That means we are visiting more than once. 
		** Syntax error.
		** We have a case like:  (targetattrfilters) (targetattrfilters) 
		*/

		return ACL_SYNTAX_ERR;
	}

    /* First, skip the "targetattrfilters"  */
    
    s = strchr (input_str, '=');
    if (NULL == s) {
        return ACL_SYNTAX_ERR;
    }
    s++;							/* skip the = */
    __acl_strip_leading_space(&s);	/* skip to next significant character */
    __acl_strip_trailing_space(s);
    len = strlen(s);				/* Knock off the " and trailing ) */
	if (*s == '"' && s[len-1] == '"') {
		s[len-1] = '\0';
		s++;						/* skip the first " */
	} else {						/* No matching quotes */
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
						"__aclp__init_targetattrfilters: Error: The statement does not begin and end with a \": [%s]\n",
						s);
    	return (ACL_SYNTAX_ERR);
    }
    
   	str = s;
    
    /* 
     * Here str looks like add=attr1:F1...attrn:Fn,
     *					   del=attr1:F1...attrn:Fn
     *
     * extract the add and del filter lists and process each one
     * in turn.
    */
    
    s = strchr (str, '=');
    if (NULL == s) {
        return ACL_SYNTAX_ERR;
    }
    *s = '\0';
    s++;							/* skip the = */
    __acl_strip_leading_space(&s);	/* start of the first filter list */

        
    /*
     * Now str is add or del
     * s points to the first filter list.
    */
    
    if (strcmp(str, "add") == 0) {
        aci->aci_type |= ACI_TARGET_ATTR_ADD_FILTERS;
        addlistptr = s;

		/* Now isolate the first filter list. */
		if ((str = strstr(s , "del=")) || ((str = strstr(s , "del ="))) ) {
        	str--;
			*str = '\0';
        	str++;
		}


    } else if (strcmp(str, "del") == 0) {
        aci->aci_type |= ACI_TARGET_ATTR_DEL_FILTERS;
        dellistptr = s;

		/* Now isolate the first filter list. */
		if ((str = strstr(s , "add=")) || ((str = strstr(s , "add ="))) ) {
        	str--;
			*str = '\0';
        	str++;
		}
    } else {
        return(ACL_SYNTAX_ERR);
    }

	__acl_strip_trailing_space(s); 

    /*
 	 * Here, we have isolated the first filter list.
	 * There may be a second one.
     * Now, str points to the start of the
     * string that contains the second filter list.
	 * If there is none then str is NULL.
    */

	if (str != NULL ){

		__acl_strip_leading_space(&str);
		s = strchr (str, '=');
		if (NULL == s) {
			return ACL_SYNTAX_ERR;
		}
		*s = '\0';
		s++;
		__acl_strip_trailing_space(str);
		__acl_strip_leading_space(&s);
        

		/*
		 * s points to the start of the second filter list.
		 * str is add or del
		*/

		if (aci->aci_type & ACI_TARGET_ATTR_ADD_FILTERS) {
		
    		if (strcmp(str, "del") == 0) {
				aci->aci_type |= ACI_TARGET_ATTR_DEL_FILTERS;
				dellistptr = s;
			} else {
				return(ACL_SYNTAX_ERR);
			}
		} else if ( aci->aci_type & ACI_TARGET_ATTR_DEL_FILTERS ) {
			if (strcmp(str, "add") == 0) {
				aci->aci_type |= ACI_TARGET_ATTR_ADD_FILTERS;
				addlistptr = s;
			} else {
				return(ACL_SYNTAX_ERR);
			}
		}
    }
	
	/*
	 * addlistptr points to the add filter list.
	 * dellistptr points to the del filter list.
	 * In both cases the strings have been leading and trailing space
	 * stripped.
	 * Either may be NULL.
	*/

	if (process_filter_list( &aci->targetAttrAddFilters, addlistptr) 
			== ACL_SYNTAX_ERR) {
		return( ACL_SYNTAX_ERR);
	}

	if (process_filter_list( &aci->targetAttrDelFilters, dellistptr) 
			== ACL_SYNTAX_ERR) {
		return( ACL_SYNTAX_ERR);
	}
    
	return(0);    

}

/*
 * We have a list of filters that looks like this:
 * attr1:F1 &&....attrn:Fn
 * 
 * We need to put each component into a targetattrfilter component of
 * the array.
 *
 */
static int process_filter_list( Targetattrfilter ***input_attrFilterArray,
						  char * input_str) {

	char *str, *end_attr;
	Targetattrfilter *attrfilter = NULL;
	int		numattr=0, rc = 0;
	Targetattrfilter **attrFilterArray = NULL;

	str = input_str;

	while (str != 0 && *str != 0) {

		if ((end_attr = strstr(str, "&&")) != NULL) {
			/* skip the two '|'  chars */
			auto char *t = end_attr;
			LDAP_UTF8INC(end_attr);
			LDAP_UTF8INC(end_attr);
			*t = 0;
		}
		__acl_strip_trailing_space(str);
		__acl_strip_leading_space(&str);

		/*
		 * Here:
		 * end_attr points to the next attribute thing.
		 *
	  	 * str points to the current one to be processed and it looks like
		 * this:
		 * 
		 * attr1:F1
		 *
		*/

		attrfilter = (Targetattrfilter *) slapi_ch_malloc (sizeof (Targetattrfilter));
		memset (attrfilter, 0, sizeof(Targetattrfilter));

		if (strstr( str,":") != NULL) {
			if ( __acl_init_targetattrfilter( attrfilter, str ) != 0 ) {
				slapi_ch_free((void**)&attrfilter);
				rc = ACL_SYNTAX_ERR;
				break;
			}        
		} else {
			slapi_ch_free((void**)&attrfilter);
			rc = ACL_SYNTAX_ERR;
			break;
		}

		/*
		 * Add the attrfilter to the targetAttrFilter list
		 */
		attrFilterArray = (Targetattrfilter **) slapi_ch_realloc (
						    (void *) attrFilterArray,
						    ((numattr+1)*sizeof(Targetattrfilter *)) ); 
		attrFilterArray[numattr] = attrfilter; 
		numattr++;		
	
		/* Move on to the next attribute in the list */
		str = end_attr;
	}/* while */

	/* NULL terminate the list */
	
	attrFilterArray = (Targetattrfilter **) slapi_ch_realloc (
						    (void *) attrFilterArray,
						    ((numattr+1)*sizeof(Targetattrfilter *)) ); 
	attrFilterArray[numattr] = NULL;
	if(rc){
		free_targetattrfilters(&attrFilterArray);
	} else {
		*input_attrFilterArray = attrFilterArray;
	}

	return rc;
}

/*
 * Take str and put it into the attrfilter component.
 *
 * str looks as follows: attr1:F1
 *
 * It has had leading and trailing space stripped.
*/

static int __acl_init_targetattrfilter( Targetattrfilter *attrfilter,
										char *str ) {

	char *tmp_ptr, *s, *filter_ptr;
	Slapi_Filter *f	= NULL;

	s = str;

	/* First grab the attribute name */
    
    if ( (tmp_ptr = strstr( str, ":")) == NULL ) {
		/* No :, syntax error */
  		slapi_log_error(SLAPI_LOG_ACL, plugin_name,
                                "Bad targetattrfilter %s:%s\n",
                                str,"Expecting \":\"");

		return(ACL_SYNTAX_ERR);
	}
	*tmp_ptr = '\0';
	LDAP_UTF8INC(tmp_ptr);
    
    __acl_strip_trailing_space(s);
    
    /* s should be the attribute name-make sure it's non-empty. */
    
	if ( *s == '\0' ) {
		slapi_log_error(SLAPI_LOG_ACL, plugin_name,
                                "No attribute name in targattrfilters\n");
		return(ACL_SYNTAX_ERR);
	}

    attrfilter->attr_str = slapi_ch_strdup (s);
    
	/* Now grab the filter */

	filter_ptr = tmp_ptr;	
	__acl_strip_leading_space(&filter_ptr);		
	__acl_strip_trailing_space(filter_ptr);

	/* trim dups the string, so we need to free it later if it's not kept. */
	tmp_ptr = __acl_trim_filterstr(filter_ptr);

	if ((f = slapi_str2filter(tmp_ptr)) == NULL) {
  		slapi_log_error(SLAPI_LOG_ACL, plugin_name,
                                "Bad targetattr filter for attribute %s:%s\n",
                                attrfilter->attr_str,tmp_ptr);
        slapi_ch_free( (void **) &attrfilter->attr_str);
		slapi_ch_free( (void **) &tmp_ptr);		
		return(ACL_SYNTAX_ERR);
	}

	/*
	 * Here verify that the named attribute is the only one
	 * that appears in the filter.
	*/

	if (acl_verify_exactly_one_attribute( attrfilter->attr_str, f) != 
				SLAPI_FILTER_SCAN_NOMORE) {
  		slapi_log_error(SLAPI_LOG_ACL, plugin_name,
              "Exactly one attribute type per filter allowed in targattrfilters (%s)\n",
				attrfilter->attr_str);
        slapi_ch_free( (void **) &attrfilter->attr_str);
		slapi_ch_free( (void **) &tmp_ptr);		
		slapi_filter_free( f, 1 );
		return(ACL_SYNTAX_ERR);
	}

	/* free the tmp_ptr */
	slapi_ch_free( (void **) &tmp_ptr);
    attrfilter->filterStr = slapi_ch_strdup (filter_ptr);
	attrfilter->filter = f;

	return(LDAP_SUCCESS);
}

/*
 * Returns 0 if attr_name is the only attribute name to 
 * appear in original_filter AND it appears at least once.
 * Otherwise returns STOP_FILTER_SCAN.
*/

static int acl_verify_exactly_one_attribute( char *attr_name, 
											Slapi_Filter *original_filter) {
	int error_code;
	
	return( slapi_filter_apply( original_filter, type_compare,
			 (void *)attr_name, &error_code));

}

static int type_compare( Slapi_Filter *f, void *arg) {

	/* Compare only the base names: eg cn and cn;lang-eb will be the same. */

	char *t = (char *)arg;
	char *filter_type;
	int	rc = SLAPI_FILTER_SCAN_STOP;
	
	if (slapi_filter_get_attribute_type( f, &filter_type) == 0) {
		t = slapi_attr_syntax_normalize(t);
		filter_type = slapi_attr_syntax_normalize(filter_type);

		if (slapi_attr_type_cmp(filter_type, t, 1) == 0) {
			rc = SLAPI_FILTER_SCAN_CONTINUE;
		}

		slapi_ch_free( (void **)&t );
		slapi_ch_free( (void **)&filter_type );
	}

	return rc;
}

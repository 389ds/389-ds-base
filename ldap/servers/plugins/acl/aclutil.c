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

/**************************************************************************
* Defines and usefuls stuff
****************************************************************************/

/*************************************************************************
* Prototypes
*************************************************************************/
static  void	aclutil__typestr (int type , char str[]);
static  void	aclutil__Ruletypestr (int type , char str[]);
static char* 	__aclutil_extract_dn_component ( char **e_dns,  int position, 
											char *attrName );
static int		acl_find_comp_start(char * s, int pos );
static PRIntn	acl_ht_free_entry_and_value(PLHashEntry *he, PRIntn i,
																void *arg);
static PLHashNumber acl_ht_hash( const void *key);
#ifdef FOR_DEBUGGING
static PRIntn	acl_ht_display_entry(PLHashEntry *he, PRIntn i, void *arg);
#endif

/***************************************************************************/
/*	UTILITY FUNCTIONS						   */
/***************************************************************************/
int
aclutil_str_appened(char **str1, const char *str2)
{
	int new_len;
 
    if ( str1 == NULL || str2 == NULL )
        return(0);
 
    if ( *str1 == NULL ) {
        new_len = strlen(str2) + 1;
        *str1 = (char *)slapi_ch_malloc(new_len);
        *str1[0] = 0;
    } else {
        new_len = strlen(*str1) + strlen(str2) + 1;
        *str1 = (char *)slapi_ch_realloc(*str1, new_len);
    }
    if ( *str1 == NULL )
        return(-1);
 
    strcat(*str1, str2);
    return(0);
}

/***************************************************************************/
/*	Print routines     						   */
/***************************************************************************/

/* print eroror message returned from the ACL Library */
#define ACLUTIL_ACLLIB_MSGBUF_LEN 200
void
acl_print_acllib_err (NSErr_t *errp , char * str)
{
	char 		msgbuf[ ACLUTIL_ACLLIB_MSGBUF_LEN ];

	if  ((NULL == errp ) || !slapi_is_loglevel_set ( SLAPI_LOG_ACL ) )
		return;

	aclErrorFmt(errp, msgbuf, ACLUTIL_ACLLIB_MSGBUF_LEN, 1);      
	msgbuf[ACLUTIL_ACLLIB_MSGBUF_LEN-1] = '\0';

	if (msgbuf)
		slapi_log_error(SLAPI_LOG_ACL, plugin_name,"ACL LIB ERR:(%s)(%s)\n", 
				msgbuf, str ? str: "NULL"); 
}
void
aclutil_print_aci (aci_t *aci_item, char *type)
{
	char	str[BUFSIZ];
	const	char *dn; 

	if ( ! slapi_is_loglevel_set ( SLAPI_LOG_ACL ) )
		return;

	if (!aci_item) {
		
		slapi_log_error (SLAPI_LOG_ACL, plugin_name,
			"acl__print_aci: Null item\n");
		return;
	}


	slapi_log_error (SLAPI_LOG_ACL, plugin_name,
		"***BEGIN ACL INFO[ Name:%s]***\n", aci_item->aclName);

	slapi_log_error (SLAPI_LOG_ACL, plugin_name,
		"ACL Index:%d   ACL_ELEVEL:%d\n", aci_item->aci_index, aci_item->aci_elevel );
	aclutil__access_str (aci_item->aci_access, str);    
    aclutil__typestr (aci_item->aci_type, &str[strlen(str)]);
	slapi_log_error (SLAPI_LOG_ACL, plugin_name,
		   "ACI type:(%s)\n", str);

	aclutil__Ruletypestr (aci_item->aci_ruleType, str);
	slapi_log_error (SLAPI_LOG_ACL, plugin_name,
		   "ACI RULE type:(%s)\n",str);
	
	dn = slapi_sdn_get_dn ( aci_item->aci_sdn );
	slapi_log_error (SLAPI_LOG_ACL, plugin_name,
		"Slapi_Entry DN:%s\n", escape_string_with_punctuation (dn, str));

	slapi_log_error (SLAPI_LOG_ACL, plugin_name,
		"***END ACL INFO*****************************\n");

}
void
aclutil_print_err (int rv , const Slapi_DN *sdn, const struct berval* val,
	char **errbuf)
{
	char	ebuf [BUFSIZ];
	/* 
	 * The maximum size of line is ebuf_size + the log message
	 * itself (less than 200 characters for all but potentially ACL_INVALID_TARGET)
	 */
	char	line [BUFSIZ + 200]; 
	char	str  [1024];
	const char	*dn;
	char *lineptr = line;
	char *newline = NULL;

	if ( rv >= 0)
		return;

	if (val->bv_len > 0 && val->bv_val != NULL) {
	    PR_snprintf (str, sizeof(str), "%.1023s", val->bv_val);
	} else {
	    str[0] = '\0';
	}

	dn = slapi_sdn_get_dn ( sdn );
	if (dn && (rv == ACL_INVALID_TARGET) && ((strlen(dn) + strlen(str)) > BUFSIZ)) {
		/*
		 * if (str_length + dn_length + 200 char message) > (BUFSIZ + 200) line
		 * we have to make space for a bigger line...
		 */
		newline = slapi_ch_malloc(strlen(dn) + strlen(str) + 200);
		lineptr = newline;
	}

	switch (rv) {
	   case ACL_TARGET_FILTER_ERR:
		sprintf (line, "ACL Internal Error(%d): "
			 "Error in generating the target filter for the ACL(%s)\n",
			 rv, escape_string_with_punctuation (str, ebuf));
		break;
	   case ACL_TARGETATTR_FILTER_ERR:
		sprintf (line, "ACL Internal Error(%d): "
			 "Error in generating the targetattr filter for the ACL(%s)\n",
			 rv, escape_string_with_punctuation (str, ebuf));
		break;
	   case ACL_TARGETFILTER_ERR:
		sprintf (line, "ACL Internal Error(%d): "
			 "Error in generating the targetfilter filter for the ACL(%s)\n",
			 rv, escape_string_with_punctuation (str, ebuf));
		break;
	   case ACL_SYNTAX_ERR:
		sprintf (line, "ACL Syntax Error(%d):%s\n",
			 rv, escape_string_with_punctuation (str, ebuf));
		break;
	   case ACL_ONEACL_TEXT_ERR:
		sprintf (line, "ACL Syntax Error in the Bind Rules(%d):%s\n",
			 rv, escape_string_with_punctuation (str, ebuf));
		break;
	   case ACL_ERR_CONCAT_HANDLES:
		sprintf (line, "ACL Internal Error(%d): "
			 "Error in Concatenating List handles\n",
			 rv);
		break;
	   case ACL_INVALID_TARGET:
		sprintf (lineptr, "ACL Invalid Target Error(%d): "
			 "Target is beyond the scope of the ACL(SCOPE:%s)",
			 rv, dn ? escape_string_with_punctuation (dn, ebuf) : "NULL");
		sprintf (lineptr + strlen(lineptr), " %s\n", escape_string_with_punctuation (str, ebuf));
		break;
	   case ACL_INVALID_AUTHMETHOD:
		sprintf (line, "ACL Multiple auth method Error(%d):"
			 "Multiple Authentication Metod in the ACL(%s)\n",
			 rv, escape_string_with_punctuation (str, ebuf));
		break;
	   case ACL_INVALID_AUTHORIZATION:
		sprintf (line, "ACL Syntax Error(%d):"
			 "Invalid Authorization statement in the ACL(%s)\n",
			 rv, escape_string_with_punctuation (str, ebuf));
		break;
	   case ACL_INCORRECT_ACI_VERSION:
		sprintf (line, "ACL Syntax Error(%d):"
			 "Incorrect version Number in the ACL(%s)\n",
			 rv, escape_string_with_punctuation (str, ebuf));
		break;
	   default:
		sprintf (line, "ACL Internal Error(%d):"
			 "ACL generic error (%s)\n",
			 rv, escape_string_with_punctuation (str, ebuf));
		break;
	}

	if (errbuf) {
		/* If a buffer is provided, then copy the error */
		aclutil_str_appened(errbuf, lineptr );	
	}

	slapi_log_error( SLAPI_LOG_FATAL, plugin_name, "%s", lineptr);
	if (newline) slapi_ch_free((void **) &newline);
}

/***************************************************************************
* Convert access to str
***************************************************************************/
char* 
aclutil__access_str (int type , char str[])
{
	char	*p;

	str[0] = '\0';
	p = str;

	if (type & SLAPI_ACL_COMPARE) {
		strcpy (p, "compare ");
		p = strchr (p, '\0');	
	}
	if (type & SLAPI_ACL_SEARCH) {
		strcpy (p, "search ");
		p = strchr (p, '\0');	
	}
	if (type & SLAPI_ACL_READ) {
		strcpy (p, "read ");
		p = strchr (p, '\0');	
	}
	if (type & SLAPI_ACL_WRITE) {
		strcpy (p, "write ");
		p = strchr (p, '\0');	
	}
	if (type & SLAPI_ACL_DELETE) {
		strcpy (p, "delete ");
		p = strchr (p, '\0');	
	}
	if (type & SLAPI_ACL_ADD) {
		strcpy (p, "add ");
		p = strchr (p, '\0');	
	}
	if (type & SLAPI_ACL_SELF) {
		strcpy (p, "self ");
		p = strchr (p, '\0');	
	}
	if (type & SLAPI_ACL_PROXY) {
		strcpy (p, "proxy ");        
	}
	return str;
}

/***************************************************************************
* Convert type to str
***************************************************************************/
static void 
aclutil__typestr (int type , char str[])
{
	char	*p;
        
    /* Start copying in at whatever location is passed in */
    
	p = str;
	
	if (type & ACI_TARGET_DN) {
		strcpy (p, "target_DN ");
		p = strchr (p, '\0');	
	}
	if (type & ACI_TARGET_ATTR) {
		strcpy (p, "target_attr ");
		p = strchr (p, '\0');	
	}
	if (type & ACI_TARGET_PATTERN) {
		strcpy (p, "target_patt ");
		p = strchr (p, '\0');	
	}
	if ((type & ACI_TARGET_ATTR_ADD_FILTERS) | (type & ACI_TARGET_ATTR_DEL_FILTERS)) {
		strcpy (p, "targetattrfilters ");
		p = strchr (p, '\0');	
	}
    if (type & ACI_TARGET_FILTER) {
		strcpy (p, "target_filter ");
		p = strchr (p, '\0');	
	}
	if (type & ACI_ACLTXT) {
		strcpy (p, "acltxt ");
		p = strchr (p, '\0');	
	}
	if (type & ACI_TARGET_NOT) {
		strcpy (p, "target_not ");
		p = strchr (p, '\0');	
	}
	if (type & ACI_TARGET_ATTR_NOT) {
		strcpy (p, "target_attr_not ");
		p = strchr (p, '\0');	
	}
	if (type & ACI_TARGET_FILTER_NOT) {
		strcpy (p, "target_filter_not ");
		p = strchr (p, '\0');	
	}

	if (type & ACI_HAS_ALLOW_RULE) {
		strcpy (p, "allow_rule ");
		p = strchr (p, '\0');	
	}
	if (type & ACI_HAS_DENY_RULE) {
		strcpy (p, "deny_rule ");
		p = strchr (p, '\0');	
	}
}
static void 
aclutil__Ruletypestr (int type , char str[])
{
	char	*p;

	str[0] = '\0';
	p = str;
	if ( type & ACI_USERDN_RULE) {
		strcpy (p, "userdn ");
		p = strchr (p, '\0');	
	}
	if ( type & ACI_USERDNATTR_RULE) {
		strcpy (p, "userdnattr ");
		p = strchr (p, '\0');	
	}
	if ( type & ACI_USERATTR_RULE) {
		strcpy (p, "userattr ");
		p = strchr (p, '\0');	
	}
	if ( type & ACI_GROUPDN_RULE) {
		strcpy (p, "groupdn ");
		p = strchr (p, '\0');	
	}
	if ( type & ACI_GROUPDNATTR_RULE) {
		strcpy (p, "groupdnattr ");
		p = strchr (p, '\0');	
	}
	if ( type & ACI_ROLEDN_RULE) {
		strcpy (p, "roledn ");
		p = strchr (p, '\0');	
	}
	if ( type & ACI_IP_RULE) {
		strcpy (p, "ip ");
		p = strchr (p, '\0');	
	}
	if ( type & ACI_DNS_RULE) {
		strcpy (p, "dns ");
		p = strchr (p, '\0');	
	}
	if ( type & ACI_TIMEOFDAY_RULE) {
		strcpy (p, "timeofday ");
		p = strchr (p, '\0');	
	}
	if ( type & ACI_DAYOFWEEK_RULE) {
		strcpy (p, "dayofweek ");
		p = strchr (p, '\0');	
	}
	if ( type & ACI_AUTHMETHOD_RULE) {
		strcpy (p, "authmethod ");
		p = strchr (p, '\0');	
	}
	if ( type & ACI_PARAM_DNRULE) {
		strcpy (p, "paramdn ");
		p = strchr (p, '\0');	
	}
	if ( type & ACI_PARAM_ATTRRULE) {
		strcpy (p, "paramAttr ");
		p = strchr (p, '\0');	
	}
}
/*
** acl_gen_err_msg
** 	This function is called by backend to generate the error message
**	if access is denied.
*/
void 
acl_gen_err_msg(int access, char *edn, char *attr, char **errbuf)
{
	char *line = NULL;

	if (access & SLAPI_ACL_WRITE) {
		line = PR_smprintf(
			"Insufficient 'write' privilege to the '%s' attribute of entry '%s'.\n",
			attr ? attr: "NULL",  edn);
	} else if ( access & SLAPI_ACL_ADD ) {
		line = PR_smprintf(
			"Insufficient 'add' privilege to add the entry '%s'.\n",edn);

	} else if ( access & SLAPI_ACL_DELETE ) {
		line = PR_smprintf(
			"Insufficient 'delete' privilege to delete the entry '%s'.\n",edn);
	}
	aclutil_str_appened(errbuf, line );

	if (line) {
		PR_smprintf_free(line);
		line = NULL;
	}
}
short
aclutil_gen_signature ( short c_signature )
{
	short	o_signature;
	o_signature = c_signature ^ (slapi_rand() % 32768);
	if (!o_signature)
		o_signature = c_signature ^ (slapi_rand() % 32768);

	return o_signature;
}

void 
aclutil_print_resource( struct acl_pblock *aclpb, char *right , char *attr, char *clientdn )
{

	char		str[BUFSIZ];
	const char  *dn;
    

	if ( aclpb == NULL) return;

	if ( ! slapi_is_loglevel_set ( SLAPI_LOG_ACL ) )
		return;

	slapi_log_error (SLAPI_LOG_ACL, plugin_name, "    ************ RESOURCE INFO STARTS *********\n");
	slapi_log_error (SLAPI_LOG_ACL, plugin_name, "    Client DN: %s\n", 
		   clientdn ? escape_string_with_punctuation (clientdn, str) : "NULL");
	aclutil__access_str (aclpb->aclpb_access, str);
    aclutil__typestr (aclpb->aclpb_res_type, &str[strlen(str)]);
	slapi_log_error (SLAPI_LOG_ACL, plugin_name, "    resource type:%d(%s)\n",
		   aclpb->aclpb_res_type, str);

	dn = slapi_sdn_get_dn ( aclpb->aclpb_curr_entry_sdn );
	slapi_log_error (SLAPI_LOG_ACL, plugin_name, "    Slapi_Entry DN: %s\n", 
		   dn ? escape_string_with_punctuation ( dn , str) : "NULL");

	slapi_log_error (SLAPI_LOG_ACL, plugin_name, "    ATTR: %s\n", attr ? attr : "NULL");
	slapi_log_error (SLAPI_LOG_ACL, plugin_name, "    rights:%s\n", right ? right: "NULL");
	slapi_log_error (SLAPI_LOG_ACL, plugin_name, "    ************ RESOURCE INFO ENDS   *********\n");
}
/*
 * The input string contains a rule like
 * "cn=helpdesk, ou=$attr.deptName, o=$dn.o, o=ISP"
 *
 * Where $attr -- means  look into the attribute list for values
 *	     $dn -- means look into the entry's dn
 *
 *	We extract the values from the entry and returned a string 
 *  with the values added.
 *  For "$attr" rule - if we find multiple values then it is
 *	the pattern is not expanded.
 *  For "$dn" rule, if we find multiple of them, we use the relative
 *	position.
 *  NOTE: The caller is responsible in freeing the memory.
 */
char *
aclutil_expand_paramString ( char *str, Slapi_Entry *e )
{

	char		**e_dns;
	char		**a_dns;
	char		*attrName;
	char		*s, *p;
	char		*attrVal;
	int			i, len;
	int			ncomponents, type;
	int			rc = -1;
	char		*buf = NULL;


	e_dns = ldap_explode_dn ( slapi_entry_get_ndn ( e ), 0 );
	a_dns = ldap_explode_dn ( str, 0 );

	i = 0;
	ncomponents = 0;
	while ( a_dns[ncomponents] )
		ncomponents++;


	for (i=0; i < ncomponents; i++ ) {

		/* Look for"$" char */
		if ( (s = strchr ( a_dns[i], '$') ) != NULL) {
			p = s;
			s++;
			if ( strncasecmp (s, "dn", 2) == 0 )
				type = 1;	
			else if ( strncasecmp (s, "attr", 4) == 0 )
				type = 2;
			else {
				/* error */
				goto cleanup;
			}
			*p = '\0';
			aclutil_str_appened ( &buf,a_dns[i]);

			if ( type == 1 ) {
				/* xyz = $dn.o */
				s +=3;
				attrName = s;

				attrVal = __aclutil_extract_dn_component (e_dns, 
											ncomponents-i, attrName);
				if ( NULL == attrVal ) /*error*/
					goto cleanup;

			} else {
				Slapi_Attr			*attr;
				const struct berval *attrValue;
				int					kk;
				Slapi_Value			*sval, *t_sval;
				
			
				/* The pattern is x=$attr.o" */
				s +=5;
				attrName = s;

				slapi_entry_attr_find ( e, attrName, &attr );
				if ( NULL == attr )
					goto cleanup;

				kk= slapi_attr_first_value ( attr, &sval );
				if ( kk != -1 ) {
					t_sval = sval;
					kk= slapi_attr_next_value( attr, kk, &sval );
					if ( kk != -1 )  /* can't handle multiple --error */
						goto cleanup;
				}
				attrValue = slapi_value_get_berval ( t_sval );
				attrVal = attrValue->bv_val;
			}
		} else {
			attrVal = a_dns[i];
		}
		aclutil_str_appened ( &buf, attrVal);
		aclutil_str_appened ( &buf, ",");
	}
	rc = 0;		/* everything is okay*/
	/* remove the last comma */
	if (buf) {
		len = strlen ( buf);
		buf[len-1] = '\0';
	}

cleanup:

	ldap_value_free ( a_dns );
	ldap_value_free ( e_dns );
	if ( 0 != rc ) /* error */ {
		slapi_ch_free ( (void **) &buf );
		buf = NULL;
	}

	return buf;
}
static char *
__aclutil_extract_dn_component ( char **e_dns,  int position, char *attrName )
{

	int			i, matched, len;
	char		*s;
	int			matchedPosition;

	len = strlen ( attrName );

	/* First check if there thare are multiple of these */
	i = matched = 0;
	while ( e_dns[i] ) {
		if (0 == strncasecmp (e_dns[i], attrName, len) ) {
			matched++;
			matchedPosition = i;
		}
		i++;
	}

	if (!matched )
		return NULL;

	if ( matched > 1 ) {
		matchedPosition = i - position;
	}

	if ( NULL == e_dns[matchedPosition])
		return NULL;

	s = strstr ( e_dns[matchedPosition], "=");
	if ( NULL == s) 
		return NULL;
	else
		return s+1;
}

/*
 * Does the first component of ndn match the first component of match_this ?
*/

int
acl_dn_component_match( const char *ndn, char *match_this, int component_number) {

	return(1);
}

/*
 * Here, ndn is a resource dn and match_this is a dn, containing a macro, ($dn).
 * 
 * eg. ndn is cn=fred,ou=groups,ou=people,ou=icnc,o=ISP and 
 * match_this is "ou=Groups,($dn),o=ISP" or
 * 				"cn=*,ou=Groups,($dn),o=ISP".
 *
 * They match if:
 * 				match_this is a suffix of ndn
 *
 * It returns NULL, if they do not match.
 * Otherwise it returns a copy of the substring of ndn that matches the ($dn).
 *
 * eg. in the above example, "ou=people,ou=icnc"
*/

char *
acl_match_macro_in_target( const char *ndn, char * match_this,
								  char *macro_ptr) {

	char *macro_prefix = NULL;
	int	macro_prefix_len = 0;
	char *macro_suffix = NULL;
	char *tmp_ptr = NULL;
	char *matched_val = NULL;
	char *ret_val = NULL;
	int ndn_len = 0;
	int macro_suffix_len = 0;
	int ndn_prefix_len = 0;
	int ndn_prefix_end = 0;
	int matched_val_len = 0;

	/*
	 * First, grab the macro_suffix--the bit after the ($dn)
	 * 
	*/
		
	if (strlen(macro_ptr) == strlen(ACL_TARGET_MACRO_DN_KEY)) {		
		macro_suffix = NULL;	/* just  ($dn) */
	} else {
		if ( macro_ptr[strlen(ACL_TARGET_MACRO_DN_KEY)] == ',') {
			macro_suffix = &macro_ptr[strlen(ACL_TARGET_MACRO_DN_KEY) + 1];
		} else {
			macro_suffix = &macro_ptr[strlen(ACL_TARGET_MACRO_DN_KEY)];
		}
	}			

	/*
	 * First ensure that the suffix of match_this is
	 * a suffix of ndn.
	*/
	
	ndn_len = strlen(ndn);	
	if ( macro_suffix != NULL) {
		macro_suffix_len = strlen(macro_suffix);				
		if( macro_suffix_len >= ndn_len ) {

			/* 
			 * eg ndn: 			o=icnc,o=sun.com
			 * 	  match_this:	($dn),o=icnc,o=sun.com
			*/			
			return(NULL);	/* ($dn) must match something. */
		} else {
			/* 
			 * eg ndn: 			ou=People,o=icnc,o=sun.com
			 * 	  match_this:	($dn),o=icnc,o=sun.com
			 *
			 * we can do a direct strncmp() because we know that
			 * there can be no "*" after the ($dn)...by definition.
			*/
			if (strncasecmp( macro_suffix, &ndn[ndn_len-macro_suffix_len],
					 macro_suffix_len) != 0) {				
				return(NULL); /* suffix must match */
			}
		}
	}

	/* Here, macro_suffix is a suffix of ndn.
	 * 
	 *
	 * Now, look at macro_prefix, if it is NULL, then ($dn) matches
	 * ndn[0..ndn_len-macro_suffix_len].
	 * (eg, ndn: 		cn=fred,ou=People,o=sun.com
	 * 		match_this: ($dn),o=sun.com.
	 *
	*/

	macro_prefix = slapi_ch_strdup(match_this);
	
	/* we know it's got a $(dn) */
	tmp_ptr = strstr(macro_prefix, ACL_TARGET_MACRO_DN_KEY);	
	*tmp_ptr = '\0';
	/* There may be a NULL prefix eg. match_this: ($dn),o=sun.com */
	macro_prefix_len = strlen(macro_prefix);
	if (macro_prefix_len == 0) {
		slapi_ch_free((void **) &macro_prefix);
		macro_prefix = NULL;
	}
	
	if (macro_prefix == NULL ) {
		/*
		 * ($dn) matches ndn[0..ndn_len-macro_suffix_len]
		*/
		int matched_val_len = 0;

		matched_val_len = ndn_len-macro_suffix_len;		
		
		matched_val = (char *)slapi_ch_malloc(matched_val_len + 1);		
		strncpy(matched_val, ndn, ndn_len-macro_suffix_len);
		/*
		 * Null terminate matched_val, removing trailing "," if there is
		 * one.
		*/
		if (matched_val_len > 1) {
			if (matched_val[matched_val_len-1] == ',' ) {
				matched_val[matched_val_len-1] = '\0';
			} else {
				matched_val[matched_val_len] = '\0';
			}
		}
		ret_val = matched_val;
	} else {
		

		/*
		 * If it is not NULL, then if macro_prefix contains a * then
		 * it needs to be an exact prefix of ndn (modulo the * component
	 	 * which matches anything) becuase that's the semantics
		 * of target patterns containing *'s, except that we just
		 * make it match one component.
	 	 * If it is such a prefix then ($dn) matches that portion of ndn
	 	 * from the end of the prefix, &ndn[ndn_prefix_end] to 
	 	 * ndn_suffix_start.
	 	 * If ndn_prefix_len > ndn_len-macro_suffix_len then return(NULL),
	 	 * otherwise $(dn) matches ndn[ndn_prefix_len..ndn_len-macro_suffix_len].
		 *
		 *
		 * eg.	ndn: 		cn=fred,ou=P,o=sun.com
	 	 * 		match_this: cn=*,($dn),o=sun.com
		*/	
		
		if ( strstr(macro_prefix, "=*") != NULL ) {
			int exact_match = 0;			

			ndn_prefix_len = acl_match_prefix( macro_prefix, ndn, &exact_match);
			if (  ndn_prefix_len != -1 ) {
				
				/*
				 * ndn[0..ndn_prefix_len] is the prefix in ndn.
				 * ndn[ndn_prefix_len..ndn_len-macro_suffix_len] is the
				 * matched string.
				*/
				if (ndn_prefix_len >= ndn_len-macro_suffix_len) {

					/*
					 * eg ndn: cn=fred,ou=People,o=icnc,o=sun.com
					 *		   cn=*,ou=People,o=icnc,($dn),o=icnc,o=sun.com	
					*/
				
					ret_val = NULL;	/* matched string is empty */
				} else {
		
					/*
					 * eg ndn: cn=fred,ou=People,o=icnc,o=sun.com
					 *		   cn=*,ou=People,($dn),o=sun.com
					*/

					matched_val_len = ndn_len-macro_suffix_len-ndn_prefix_len;
					matched_val = (char *)slapi_ch_malloc(matched_val_len + 1);
					strncpy(matched_val, &ndn[ndn_prefix_len], matched_val_len);
					if (matched_val_len > 1) {
						if (matched_val[matched_val_len-1] == ',' ) {
							matched_val[matched_val_len-1] = '\0';
						} else {
							matched_val[matched_val_len] = '\0';
						}
					}
					matched_val[matched_val_len] = '\0';
					ret_val = matched_val;
				}
			} else {
				/* Was not a prefix so not a match */
				ret_val = NULL;
			}			
		} else {

			/*
	 		 *
	 		 * If macro_prefix is not NULL and it does not
	 		 * contain a =* then
			 * we need to ensure that macro_prefix is a substring
			 * ndn.
			 * If it is and the position of the character after it's end in
			 * ndn is
			 * ndn_prefix_end then ($dn) matches
			 * ndn[ndn_prefix_end..ndn_len-macro_suffix_len].
			 *
			 *
			 * One important principal is that ($dn) matches a maximal
			 * chunk--this way it will serve to make the link
			 * between resources and users at each level of the structure.
			 *
			 * eg. ndn: ou=Groups,ou=Groups,ou=Groups,c=fr
			 *     macro_prefix: ou=Groups,($dn),c=fr
			 *
			 * then ($dn) matches ou=Groups,ou=Groups.
			 *
			 * 
			 *
			 * If it is not a substring, then there is no match.
			 * If it is a substring and 
			 * ndn[ndn_prefix_end..ndn_len-macro_suffix_len] is empty then
			 * it's also not a match as we demand that ($dn) match a non-empty
			 * string.
			 * 
			 *
	 		 *
	 		 * (eg. ndn:		cn=fred,o=icnc,ou=People,o=sun.com
	 		 * 		match_this: o=icnc,($dn),o=sun.com.)
			 *	 		
			 *
			 * (eg. ndn: cn=fred,o=menlo park,ou=People,o=icnc,o=sun.com
			 * 		match_this: o=menlo park,ou=People,($dn),o=sun.com			
			 * 
			*/						
			
			ndn_prefix_end = acl_strstr((char *)ndn, macro_prefix);
			if ( ndn_prefix_end == -1) {
				ret_val = NULL;
			} else {
				/* Is a substring */

				ndn_prefix_end += macro_prefix_len;

				/*
				 * make sure the matching part is non-empty:
				 *
				 * ndn[ndn_prefix_end..mndn_len-macro_suffix_len].
				*/

				if ( ndn_prefix_end >= ndn_len-macro_suffix_len) {
					ret_val = NULL;
				} else {
					/*
					 * ($dn) matches the non-empty string segment
					 * ndn[ndn_prefix_end..mndn_len-macro_suffix_len]
					 * the -1 is because macro_suffix_eln does not include
					 * the coma before the suffix.
					*/

					matched_val_len = ndn_len-macro_suffix_len-
										ndn_prefix_end - 1;
					
					matched_val = (char *)slapi_ch_malloc(matched_val_len + 1);
					strncpy(matched_val, &ndn[ndn_prefix_end],
							matched_val_len);
					matched_val[matched_val_len] = '\0';

					ret_val = matched_val;
				}
			}
		}/* contains an =* */	
		slapi_ch_free((void **) &macro_prefix);		
	}/* macro_prefix != NULL */

	return(ret_val);
}

/*
 * Checks to see if macro_prefix is an exact prefix of ndn.
 * macro_prefix may contain a * component.
 *
 * The length of the matched prefix in ndn is returned.
 * If it was not a match, a negative int is returned.
 * Also, if the string matched exactly,
 * exact_match is set to 1, other wise it was a proper prefix.
 * 
*/

int
acl_match_prefix( char *macro_prefix, const char *ndn, int *exact_match) {

	int ret_code = -1;
	int macro_prefix_len = 0;
	int ndn_len = 0;
	int i = 0;
	int j = 0;
	int done = 0;
	int t = 0;
	char * tmp_str = NULL;
	int k,l = 0;
	
	*exact_match = 0;		/* default to not an exact match */

	/* The NULL prefix matches everthing*/
	if (macro_prefix == NULL) {
		if ( ndn == NULL ) {
			*exact_match = 1;
		}
		return(0);
	} else {
		/* macro_prefix is not null, so if ndn is NULL, it's not a match. */
		if ( ndn == NULL) {
			return(-1);	
		}
	}
	/*
	 * Here, neither macro_prefix nor ndn are NULL.
	 * 
	 * eg. macro_prefix: cn=*,ou=people,o=sun.com
	 * 	   ndn		   : cn=fred,ou=people,o=sun.com
	*/

	
	/*
	 * Here, there is a component with a * (eg. cn=* ) so
	 * we need to step through the macro_prefix, and where there is
	 * such a * match on that component,
	 * when we run out of * componenets, jsut do a straight match.
	 *
	 * Out of interest, the following extended regular expression
	 * will match just one ou rdn value from a string:
	 * "^uid=admin,ou=\([^,]*\\\,\)*[^,]*,o=sun.com$"
	 * 
	 *
	 * eg. cn=fred,ou=People,o=sun.com
	 * 	   
	 * 
	 * s points to the = of the component.
	*/
	
	macro_prefix_len = strlen(macro_prefix);
	ndn_len = strlen(ndn);
	i = 0;
	j = 0;
	done = 0;		
	while ( !done ) {

		/* Here ndn[0..i] has matched macro_prefix[0..j] && j<= i
		 * i<=ndn_len j<=macro_prefix_len */

		if ( (t = acl_strstr(&macro_prefix[j], "=*")) < 0 ) {
			/*
			 * No more *'s, do a straight match on
			 * macro_prefix[j..macro_prefix_len] and
			 * ndn[i..macro_prefix_len]
			*/ 
						
			if( macro_prefix_len-j > ndn_len-i) {
				/* Not a prefix, nor a match */
				*exact_match = 0;
				ret_code = -1;
				done = 1;
			} else {
				/*
				 * ndn_len-i >= macro_prefix_len - j
				 * if macro_prefix_len-j is 0, then
				 * it's a null prefix, so it matches.
				 * If in addition ndn_len-i is 0 then it's
				 * an exact match.
				 * Otherwise, do the cmp.
				*/
				
				if ( macro_prefix_len-j == 0) {
					done = 1;
					ret_code = i;
					if ( ndn_len-i == 0) {
						*exact_match = 1;
					}
				}else {

					if (strncasecmp(&macro_prefix[j], &ndn[i],
										macro_prefix_len-j) == 0) {
						*exact_match = (macro_prefix_len-j == ndn_len-i);
						ret_code = i + macro_prefix_len -j;
						done = 1;
					} else {
						/* not a prefix not a match */
						*exact_match = 0;
						ret_code = -1;
						done = 1;
					}
				}
			}
		}else {
			/*
			 * Is another * component, so:
			 * 1. match that component in macro_prefix (at pos k say)
			 * with the corresponding compoent (at pos l say ) in ndn 
			 * 
			 * 2. match the intervening string ndn[i..l] and
			 * macro_prefix[j..k].
			*/

			/* First, find the start of the component in macro_prefix. */

			t++; /* move to the--this way we will look for "ou=" in ndn */			
			k = acl_find_comp_start(macro_prefix, t);

			/* Now identify that component in ndn--if it's not there no match */
			tmp_str = slapi_ch_malloc(t-k+1);
			strncpy(tmp_str, &macro_prefix[k], t-k);
			tmp_str[t-k] = '\0';
			l = acl_strstr((char*)&ndn[i], tmp_str);
			if (l == -1) {
				*exact_match = 0;
				ret_code = -1;
				done = 1;
			} else {
				/*
				 * Found the comp in ndn, so the comp matches.
				 * Now test the intervening string segments:
				 * ndn[i..l] and macro_prefix[j..k]
				*/

				if ( k-j != l-i ) {			
					*exact_match = 0;
					ret_code = -1;
					done = 1;
				} else{
					if (strncasecmp(&macro_prefix[j], &ndn[i], k-j) != 0) {
						*exact_match = 0;
						ret_code = -1;
						done = 1;	
					} else {	
						/* Matched, so bump i and j and keep going.*/
						i += acl_find_comp_end((char*)&ndn[l]);
						j += acl_find_comp_end((char*)&macro_prefix[k]);
					}											
				}
			}
			slapi_ch_free((void **)&tmp_str);
		}
	}/* while */

	return(ret_code);
		
}

/*
 * returns the index in s of where the component at position
 * s[pos] starts.
 * This is the index of the character after the first unescaped comma
 * moving backwards in s from pos.
 * If this is not found then return 0, ie. the start of the string.
 * If the index returned is > strlen(s) then could not find it.
 * only such case is if you pass ",", in which case there is no component start.
*/

static int 
acl_find_comp_start(char * s, int pos ) {

	int i =0;
	int comp_start = 0;
	
	i = pos;	
	while( i > 0 && (s[i] != ',' ||
			s[i-1] == '\\')) {
		i--;
	}
	/*
	 * i == 0 || (s[i] == ',' && s[i-1] != '\\')
	*/
	if (i==0) {
		/* Got all the way with no unescaped comma */
		if (s[i] == ',') {
			comp_start = i+1;
		} else {
			comp_start = i;
		}
	} else { /* Found an unescaped comma */
		comp_start = i + 1;
	}	 

	return( comp_start);
}

/* 
 * returns the index in s of the first character after the
 * first unescaped comma.
 * If ther is no such character, returns strlen(s);
*/

int
acl_find_comp_end( char * s) {

	int i = 0;
	int s_len = 0;
	
	s_len = strlen(s);

	if ( s_len == 0 || s_len == 1) {
		return(s_len);
	} 

	/* inv: i+1<s_len && (s[i] == '\\' || s[i+1] != ',')*/

	i = 0;
	while( i+1 < s_len && (s[i] == '\\' ||
			s[i+1] != ',')) {
		i++;
	}
	if ( i + 1 == s_len) {
		return(s_len);
	} else {
		return(i+2);
	}
}	

/*
 * return the index in s where substr occurs, if none
 * returns -1.
*/

int
acl_strstr(char * s, char *substr) {

	char *t = NULL;
	char *tmp_str = NULL;

	tmp_str = slapi_ch_strdup(s);
	
	if ( (t = strstr(tmp_str, substr)) == NULL ) {
		slapi_ch_free((void **)&tmp_str);
		return(-1);
	} else {
		int l = 0;
		*t = '\0';
		l = strlen(tmp_str);
		slapi_ch_free((void **)&tmp_str);
		return(l);
	}
}

/*
 * replace all occurences of substr in s with replace_str.
 *
 * returns a malloced version of the patched string.
*/

char *
acl_replace_str(char * s, char *substr, char* replace_with_str) {
		
		char *str = NULL;
		char *working_s, *suffix, *prefix, *patched;
		int replace_with_len, substr_len, prefix_len, suffix_len;

		if ( (str = strstr(s, substr)) == NULL) {
			return(slapi_ch_strdup(s));
		} else {

				
			replace_with_len = strlen(replace_with_str);
			substr_len  = strlen(substr);			
		
			working_s = slapi_ch_strdup(s);	
			prefix = working_s;
			str = strstr(prefix, substr);
			
			while (str != NULL) {
				
				/*
				 * working_s is a copy of the string to be patched
				 * str points to a substr to be patched
				 * prefix points to working_s
				*/	
			
				*str = '\0';
	
				suffix = &str[substr_len];
				prefix_len = strlen(prefix);
				suffix_len = strlen(suffix);
			
				patched = (char *)slapi_ch_malloc(prefix_len +
												replace_with_len +
												suffix_len +1 );
				strcpy(patched, prefix);
				strcat(patched, replace_with_str);
				strcat(patched, suffix);

				slapi_ch_free((void **)&working_s);

				working_s = patched;
				prefix = working_s;
				str = strstr(prefix, substr);		
				
			}

			return(working_s);
		}

}


/*
 * Start at index and return a malloced string that is the
 * next component in dn (eg. "ou=People"),
 * or NULL if couldn't find the next one.
*/

char *
get_next_component(char *dn, int *index) {

	int dn_len = strlen(dn);
	int start_next = -1;
	int i = 0;
	char *ret_comp;

	if (*index>= dn_len) {
		return(NULL);
	}

	start_next = acl_find_comp_end( &dn[*index]);
	
	if ( start_next >= dn_len ) {
		*index = start_next;
		return(NULL);	/* no next comp */
	}
			
	/*
	 *Here, start_next should be the start of the next
	 * component--so far have not run off the end.
	*/

	i = acl_find_comp_end( &dn[start_next]);		
	
	/*
	 * Here, the matched string is all from start_next to i.
	*/

	ret_comp = (char *)slapi_ch_malloc(i - start_next +1);
	memcpy( ret_comp, &dn[start_next], i-start_next);
	ret_comp[i-start_next] = '\0';
		
	return(ret_comp);
} 

char *
get_this_component(char *dn, int *index) {

	int dn_len = strlen(dn);	
	int i = 0;
	char *ret_comp;

	if (*index>= dn_len) {
		return(NULL);
	}
	
	if (dn_len == *index + 1) {
		/* Just return a copy of the string. */
		return(slapi_ch_strdup(dn));		
	}else {
		/* *index + 1 < dn_len */
		i = *index+1;
		while( (dn[i] != '\0') && dn[i] != ',' && dn[i-1] != '\\') {
				i += 1;		
		}

		/*
		 * Here, the matched string is all from *index to i.
		*/

		ret_comp = (char *)slapi_ch_malloc(i - *index +1);
		memcpy( ret_comp, &dn[*index], i - *index);
		ret_comp[i-*index] = '\0';

		if (i < dn_len) {
			/* Found a comma before the end */
			*index = i + 1; /* skip it */
		}
		
		return(ret_comp);		
	}
	
} 

/* acl hash table funcs */

/*
 * Add the key adn value to the ht.
 * If it already exists then remove the old one and free
 * the value.
*/
void acl_ht_add_and_freeOld(acl_ht_t * acl_ht,
							PLHashNumber key,
							char *value){
	char *old_value = NULL;	
	uintptr_t pkey = (uintptr_t)key;

	if ( (old_value = (char *)acl_ht_lookup( acl_ht, key)) != NULL ) {
		acl_ht_remove( acl_ht, key);
		slapi_ch_free((void **)&old_value);
	}

	PL_HashTableAdd( acl_ht, (const void *)pkey, value);
}

/*
 * Return a new acl_ht_t *
*/
acl_ht_t *acl_ht_new(void) {
	
	return(PL_NewHashTable(30, acl_ht_hash, /* key hasher */
			PL_CompareValues,				/* keyCompare */
			PL_CompareStrings, 0, 0));		/* value compare */
}

static PLHashNumber acl_ht_hash( const void *key) {

	return( (PLHashNumber)((uintptr_t)key) );
}

/* Free all the values in the ht */
void acl_ht_free_all_entries_and_values( acl_ht_t *acl_ht) {

	PL_HashTableEnumerateEntries( acl_ht, acl_ht_free_entry_and_value,
															NULL);
}

static PRIntn
acl_ht_free_entry_and_value(PLHashEntry *he, PRIntn i, void *arg)
{	

	slapi_ch_free((void **)&he->value);		/* free value */	 
	
	/* Free this entry anfd go on to next one */
    return ( HT_ENUMERATE_NEXT | HT_ENUMERATE_REMOVE);
}

/* Free all the values in the ht */
void acl_ht_display_ht( acl_ht_t *acl_ht) {

#ifdef FOR_DEBUGGING
	PL_HashTableEnumerateEntries( acl_ht, acl_ht_display_entry, NULL);
#endif
}

#ifdef FOR_DEBUGGING
static PRIntn
acl_ht_display_entry(PLHashEntry *he, PRIntn i, void *arg)
{
	PLHashNumber aci_index = (PLHashNumber)he->key;
    char *matched_val = (char *)he->value;
	
	LDAPDebug(LDAP_DEBUG_ACL,"macro ht entry: key='%d' matched_val='%s'"
								"keyhash='%d'\n", 
				aci_index, (matched_val ? matched_val: "NULL"),
				(PLHashNumber)he->keyHash);
	
    return HT_ENUMERATE_NEXT;

}
#endif

/* remove this entry from the ht--doesn't free the value.*/
void acl_ht_remove( acl_ht_t *acl_ht, PLHashNumber key) {

	PL_HashTableRemove( acl_ht, (const void *)((uintptr_t)key) );
}

/* Retrieve a pointer to the value of the entry with key */
void *acl_ht_lookup( acl_ht_t *acl_ht,
								PLHashNumber key) {

	return( PL_HashTableLookup( acl_ht, (const void *)((uintptr_t)key)) );	
}


/***************************************************************************/
/*                              E       N       D                          */
/***************************************************************************/


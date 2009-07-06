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
 * ldapauth.cpp: Implements LDAP integration in the web server.
 * 
 * Nitin More, John Kristian
 */

/* #define DBG_PRINT */

#include <stdio.h>		/* for BUFSIZ */
#include <string.h>		/* for strncpy, strcat */
#include <ldap.h>
#include <prprf.h>

#define DEFINE_LDAPU_STRINGS 1
#include <ldaputil/certmap.h>
#include <ldaputil/errors.h>
#include <ldaputil/ldapauth.h>

#include <ldaputili.h>

#include "slapi-plugin.h"

/*
 * ldapu_find
 *   Description:
 *	Caller should free res if it is not NULL.
 *   Arguments:
 *	ld		Pointer to LDAP (assumes connection has been
 *	    		established and the client has called the
 *	    		appropriate bind routine)
 *	base		basedn (where to start the search)
 *	scope		scope for the search.  One of
 *	    		LDAP_SCOPE_SUBTREE, LDAP_SCOPE_ONELEVEL, and
 *	    		LDAP_SCOPE_BASE
 *	filter		LDAP filter
 *	attrs		A NULL-terminated array of strings indicating which
 *	    		attributes to return for each matching entry.  Passing
 *	    		NULL for this parameter causes all available
 *	    		attributes to be retrieved.
 *	attrsonly	A boolean value that should be zero if both attribute
 *	    		types and values are to be returned, non-zero if only
 *	    		types are wanted.
 *	res		A result parameter which will contain the results of
 *			the search upon completion of the call.
 *   Return Values:
 *	LDAPU_SUCCESS	if entry is found
 *	LDAPU_FAILED	if entry is not found
 *	<rv>		if error, where <rv> can be passed to
 *			ldap_err2string to get an error string.
 */
int ldapu_find (LDAP *ld, const char *base, int scope,
		const char *filter, const char **attrs,
		int attrsonly, LDAPMessage **res)
{
    int retval;
#ifdef USE_THIS_CODE /* ASYNCHRONOUS */
    int msgid;
#endif
    int numEntries;

    *res = 0;

    /* If base is NULL set it to null string */
    if (!base) {
	DBG_PRINT1("ldapu_find: basedn is missing -- assuming null string\n");
	base = "";
    }

    if (!filter || !*filter) {
	DBG_PRINT1("ldapu_find: filter is missing -- assuming objectclass=*\n");
	filter = ldapu_strings[LDAPU_STR_FILTER_DEFAULT];
    }
    
    DBG_PRINT2("\tbase:\t\"%s\"\n", base);
    DBG_PRINT2("\tfilter:\t\"%s\"\n", filter ? filter : "<NULL>");
    DBG_PRINT2("\tscope:\t\"%s\"\n",
	       (scope == LDAP_SCOPE_SUBTREE ? "LDAP_SCOPE_SUBTREE"
		: (scope == LDAP_SCOPE_ONELEVEL ? "LDAP_SCOPE_ONELEVEL"
		   : "LDAP_SCOPE_BASE")));

    retval = ldapu_search_s(ld, base, scope, filter, (char **)attrs,
			   attrsonly, res);

    if (retval != LDAP_SUCCESS)
    {
	/* retval = ldap_result2error(ld, *res, 0); */
	DBG_PRINT2("ldapu_search_s: %s\n", ldapu_err2string(retval));
	return(retval);
    }

    numEntries = ldapu_count_entries(ld, *res);

    if (numEntries == 1) {
	/* success */
	return LDAPU_SUCCESS;
    }
    else if (numEntries == 0) {
	/* not found -- but not an error */
	DBG_PRINT1("ldapu_search_s: Entry not found\n");
	return LDAPU_FAILED;
    }
    else if (numEntries > 0) {
	/* Found more than one entry! */
	DBG_PRINT1("ldapu_search_s: Found more than one entry\n");
	return LDAPU_ERR_MULTIPLE_MATCHES;
    }
    else {
	/* should never get here */
	DBG_PRINT1("ldapu_search_s: should never reach here\n");
	ldapu_msgfree(ld, *res);
	return LDAP_OPERATIONS_ERROR;
    }
}


/* Search function for the cases where base = "" = NULL suffix, that is, search to
 * be performed on the entire DIT tree. 
 * We actually do various searches taking a naming context at a time as the base for
 * the search. */

int ldapu_find_entire_tree (LDAP *ld, int scope,
		const char *filter, const char **attrs,
		int attrsonly, LDAPMessage ***res)
{
    int retval = LDAPU_FAILED;
    int rv,i, num_namingcontexts;
    LDAPMessage *result_entry, *result = NULL;
    const char *suffix_attr[2] = {"namingcontexts", NULL};
	/* these are private suffixes that may contain pseudo users
	   e.g. replication manager that may have certs */
	int num_private_suffix = 1;
    const char *private_suffix_list[2] = {"cn=config", NULL};
    char **suffix_list, **suffix = NULL;
    
    rv = ldapu_find(ld, "",LDAP_SCOPE_BASE, "objectclass=*", suffix_attr, 0, &result);
    if (rv != LDAP_SUCCESS) {
         if (result) ldapu_msgfree(ld, result);
         return rv;
    }

    result_entry = ldapu_first_entry(ld, result);
    suffix = ldapu_get_values(ld, result_entry, suffix_attr[0]);
    suffix_list = suffix;
    num_namingcontexts = slapi_ldap_count_values(suffix);
	/* add private suffixes to our list of suffixes to search */
	if (num_private_suffix) {
		suffix_list = ldapu_realloc(suffix_list,
									sizeof(char *)*(num_namingcontexts+num_private_suffix+1));
		if (!suffix_list) {
			if (result) {
				ldapu_msgfree(ld, result);
			}
			retval = LDAPU_FAILED;
			return retval;
		}
		for (i = num_namingcontexts; i < (num_namingcontexts+num_private_suffix); ++i) {
			suffix_list[i] = strdup(private_suffix_list[i-num_namingcontexts]);
		}
		suffix_list[i] = NULL;
		num_namingcontexts += num_private_suffix;
		suffix = suffix_list;
	}
    if (result) ldapu_msgfree(ld, result);
    result = 0;
    i = 0;

    /* ugaston - the caller function must remember to free the memory allocated here */
    *res = (LDAPMessage **) ldapu_malloc((num_namingcontexts + 1) * sizeof(LDAPMessage *));
    while (suffix && *suffix) {
        rv = ldapu_find(ld, *suffix, scope, filter, attrs, attrsonly, &result);
	if (scope == LDAP_SCOPE_BASE && rv == LDAP_SUCCESS) {
	     retval = rv;
	     (*res)[i++] = result;
	     break;
	}
	  
	switch (rv) {
	     case LDAP_SUCCESS:
	          if (retval == LDAP_SUCCESS) {
		        retval = LDAPU_ERR_MULTIPLE_MATCHES;
			(*res)[i++] = result;
			break;
		  }
	     case LDAPU_ERR_MULTIPLE_MATCHES:
	          retval = rv;
		  (*res)[i++] = result;
		  break;
	     default:
		  if (retval != LDAP_SUCCESS && retval != LDAPU_ERR_MULTIPLE_MATCHES) {
		        retval = rv;
		  }
		  if (result) ldapu_msgfree(ld, result);
		  result = 0;
		  break;    
	}
	  
	suffix++;
    }

    (*res)[i] = NULL;
    ldapu_value_free(ld, suffix_list);
    return retval;
}


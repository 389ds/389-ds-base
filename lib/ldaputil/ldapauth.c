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

#include <ldaputil/certmap.h>
#include <ldaputil/errors.h>
#include <ldaputil/ldapauth.h>

#include <ldaputili.h>

/* If we are not interested in the returned attributes, just ask for one
 * attribute in the call to ldap_search.  Also don't ask for the attribute
 * value -- just the attr.
 */
static const char *default_search_attrs[] = { "c" , 0 };
static int default_search_attrsonly = 1;

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
    num_namingcontexts = ldap_count_values(suffix);
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



/*
 * ldapu_find_uid_attrs
 *   Description:
 *	Maps the given uid to a user dn.  Caller should free res if it is not
 *	NULL.  Accepts the attrs & attrsonly args.
 *   Arguments:
 *	ld		Pointer to LDAP (assumes connection has been
 *	    		established and the client has called the
 *	    		appropriate bind routine)
 *	uid		User's name
 *	base		basedn (where to start the search)
 *	attrs		list of attributes to retrieve
 *	attrsonly	flag indicating if attr values are to be retrieved
 *	res		A result parameter which will contain the results of
 *			the search upon completion of the call.
 *   Return Values:
 *	LDAPU_SUCCESS	if entry is found
 *	LDAPU_FAILED	if entry is not found
 *	<rv>		if error, where <rv> can be passed to
 *			ldap_err2string to get an error string.
 */
int ldapu_find_uid_attrs (LDAP *ld, const char *uid, const char *base,
			  const char **attrs, int attrsonly,
			  LDAPMessage **res)
{
    int		scope = LDAP_SCOPE_SUBTREE;
    char	filter[ BUFSIZ ];
    int		retval;

    /* setup filter as (uid=<uid>) */
    PR_snprintf(filter, sizeof(filter), ldapu_strings[LDAPU_STR_FILTER_USER], uid);

    retval = ldapu_find(ld, base, scope, filter, attrs, attrsonly, res);

    return retval;
}

/*
 * ldapu_find_uid
 *   Description:
 *	Maps the given uid to a user dn.  Caller should free res if it is not
 *	NULL. 
 *   Arguments:
 *	ld		Pointer to LDAP (assumes connection has been
 *	    		established and the client has called the
 *	    		appropriate bind routine)
 *	uid		User's name
 *	base		basedn (where to start the search)
 *	res		A result parameter which will contain the results of
 *			the search upon completion of the call.
 *   Return Values:
 *	LDAPU_SUCCESS	if entry is found
 *	LDAPU_FAILED	if entry is not found
 *	<rv>		if error, where <rv> can be passed to
 *			ldap_err2string to get an error string.
 */
int ldapu_find_uid (LDAP *ld, const char *uid, const char *base,
		    LDAPMessage **res)
{
    const char	**attrs = 0;	/* get all attributes ... */
    int		attrsonly = 0;	/* ... and their values */
    int		retval;

    retval = ldapu_find_uid_attrs(ld, uid, base, attrs, attrsonly, res);

    return retval;
}

/*
 * ldapu_find_userdn
 *   Description:
 *	Maps the given uid to a user dn.  Caller should free dn if it is not
 *	NULL. 
 *   Arguments:
 *	ld		Pointer to LDAP (assumes connection has been
 *	    		established and the client has called the
 *	    		appropriate bind routine)
 *	uid		User's name
 *	base		basedn (where to start the search)
 *	dn 		user dn
 *   Return Values:
 *	LDAPU_SUCCESS	if entry is found
 *	LDAPU_FAILED	if entry is not found
 *	<rv>		if error, where <rv> can be passed to
 *			ldap_err2string to get an error string.
 */
int ldapu_find_userdn (LDAP *ld, const char *uid, const char *base,
		       char **dn)
{
    LDAPMessage *res = 0;
    int		retval;

    retval = ldapu_find_uid_attrs(ld, uid, base, default_search_attrs,
				  default_search_attrsonly, &res);

    if (retval == LDAPU_SUCCESS) {
	LDAPMessage *entry;

	entry = ldapu_first_entry(ld, res);
	*dn = ldapu_get_dn(ld, entry);
    }
    else {
	*dn = 0;
    }

    if (res) ldapu_msgfree(ld, res);

    return retval;
}

/*
 * ldapu_find_group_attrs
 *   Description:
 *	Maps the given groupid to a group dn.  Caller should free res if it is
 *	not NULL.  Accepts the attrs & attrsonly args.
 *   Arguments:
 *	ld		Pointer to LDAP (assumes connection has been
 *	    		established and the client has called the
 *	    		appropriate bind routine)
 *	groupid		Groups's name
 *	base		basedn (where to start the search)
 *	attrs		list of attributes to retrieve
 *	attrsonly	flag indicating if attr values are to be retrieved
 *	res		A result parameter which will contain the results of
 *			the search upon completion of the call.
 *   Return Values:
 *	LDAPU_SUCCESS	if entry is found
 *	LDAPU_FAILED	if entry is not found
 *	<rv>		if error, where <rv> can be passed to
 *			ldap_err2string to get an error string.
 */
int ldapu_find_group_attrs (LDAP *ld, const char *groupid,
			    const char *base, const char **attrs,
			    int attrsonly, LDAPMessage **res)
{
    int		scope = LDAP_SCOPE_SUBTREE;
    char	filter[ BUFSIZ ];
    int		retval;

    /* setup the filter */
    PR_snprintf(filter, sizeof(filter),
	    ldapu_strings[LDAPU_STR_FILTER_GROUP],
	    groupid);

    retval = ldapu_find(ld, base, scope, filter, attrs, attrsonly, res);

    return retval;
}

/*
 * ldapu_find_group
 *   Description:
 *	Maps the given groupid to a group dn.  Caller should free res if it is
 *	not NULL. 
 *   Arguments:
 *	ld		Pointer to LDAP (assumes connection has been
 *	    		established and the client has called the
 *	    		appropriate bind routine)
 *	groupid		Groups's name
 *	base		basedn (where to start the search)
 *	res		A result parameter which will contain the results of
 *			the search upon completion of the call.
 *   Return Values:
 *	LDAPU_SUCCESS	if entry is found
 *	LDAPU_FAILED	if entry is not found
 *	<rv>		if error, where <rv> can be passed to
 *			ldap_err2string to get an error string.
 */
int ldapu_find_group (LDAP *ld, const char *groupid, const char *base,
		      LDAPMessage **res)
{
    const char	**attrs = 0;	/* get all attributes ... */
    int		attrsonly = 0;	/* ... and their values */
    int		retval;

    retval = ldapu_find_group_attrs (ld, groupid, base, attrs, attrsonly, res);

    return retval;
}

/*
 * ldapu_find_groupdn
 *   Description:
 *	Maps the given groupid to a group dn.  Caller should free dn if it is
 *	not NULL. 
 *   Arguments:
 *	ld		Pointer to LDAP (assumes connection has been
 *	    		established and the client has called the
 *	    		appropriate bind routine)
 *	groupid		Groups's name
 *	base		basedn (where to start the search)
 *	dn 		group dn
 *   Return Values:
 *	LDAPU_SUCCESS	if entry is found
 *	LDAPU_FAILED	if entry is not found
 *	<rv>		if error, where <rv> can be passed to
 *			ldap_err2string to get an error string.
 */
int ldapu_find_groupdn (LDAP *ld, const char *groupid, const char *base,
			char **dn)
{
    LDAPMessage *res = 0;
    int		retval;

    retval = ldapu_find_group_attrs(ld, groupid, base, default_search_attrs,
				    default_search_attrsonly, &res);

    if (retval == LDAPU_SUCCESS) {
	LDAPMessage *entry;

	/* get ldap entry */
	entry = ldapu_first_entry(ld, res);
	*dn = ldapu_get_dn(ld, entry);
    }
    else {
	*dn = 0;
    }

    if (res) ldapu_msgfree(ld, res);

    return retval;
}


/*
 * continuable_err
 *	Description:
 *	    Returns true for benign errors (i.e. errors for which recursive
 *	    search can continue.
 *	Return Values:
 *	    0 (zero) - if not a benign error
 *	    1        - if a benign error -- search can continue.
 */
static int continuable_err (int err)
{
    return (err == LDAPU_FAILED);
}

int ldapu_auth_udn_gdn_recurse (LDAP *ld, const char *userdn,
				const char *groupdn, const char *base,
				int recurse_cnt)
{
    char	filter[ BUFSIZ ];
    const char	**attrs = default_search_attrs;
    int		attrsonly = default_search_attrsonly;
    LDAPMessage *res = 0;
    int		retval;
    char	member_filter[ BUFSIZ ];

    if (recurse_cnt >= 30)
	return LDAPU_ERR_CIRCULAR_GROUPS;

    /* setup the filter */
    PR_snprintf(member_filter, sizeof(member_filter), ldapu_strings[LDAPU_STR_FILTER_MEMBER], userdn, userdn);

    retval = ldapu_find(ld, groupdn, LDAP_SCOPE_BASE, member_filter, attrs,
			attrsonly, &res);

    if (res) ldap_msgfree(res);

    if (retval != LDAPU_SUCCESS && continuable_err(retval)) {
	LDAPMessage *entry;

	DBG_PRINT2("Find parent groups of \"%s\"\n", userdn);

	/* Modify the filter to include the objectclass check */
	PR_snprintf(filter, sizeof(filter), ldapu_strings[LDAPU_STR_FILTER_MEMBER_RECURSE],
		member_filter);
	retval = ldapu_find(ld, base, LDAP_SCOPE_SUBTREE, filter,
			    attrs, attrsonly, &res);

	if (retval == LDAPU_SUCCESS || retval == LDAPU_ERR_MULTIPLE_MATCHES) {
	    /* Found at least one group the userdn is member of */

	    if (!res) {
		/* this should never happen */
		retval = LDAPU_ERR_EMPTY_LDAP_RESULT;
	    }
	    else {
		retval = LDAPU_ERR_MISSING_RES_ENTRY;

		for (entry = ldap_first_entry(ld, res); entry != NULL;
		     entry = ldap_next_entry(ld, entry))
		{
		    char *dn = ldap_get_dn(ld, entry);

		    retval = ldapu_auth_udn_gdn_recurse(ld, dn, groupdn,
							base, recurse_cnt+1);
		    ldap_memfree(dn);

		    if (retval == LDAPU_SUCCESS || !continuable_err(retval)) {
			break;
		    }
		}
	    }
	}

	if (res) ldap_msgfree(res);
    }

    return retval;
}

/*
 *  ldapu_auth_userdn_groupdn:
 *	Description:
 *	    Checks if the user (userdn) belongs to the given group (groupdn).
 *	Arguments:
 *	    ld			Pointer to LDAP (assumes connection has been
 *				established and the client has called the
 *				appropriate bind routine)
 *	    userdn		User's full DN -- actually it could be a group
 *				dn to check subgroup membership.
 *	    groupdn		Group's full DN
 *	Return Values: (same as ldapu_find)
 *	    LDAPU_SUCCESS	if user is member of the group
 *	    LDAPU_FAILED	if user is not a member of the group
 *	    <rv>		if error, where <rv> can be passed to
 *				ldap_err2string to get an error string.
 */
int ldapu_auth_userdn_groupdn (LDAP *ld, const char *userdn,
			       const char *groupdn, const char *base)
{
    return ldapu_auth_udn_gdn_recurse(ld, userdn, groupdn, base, 0);
}


/*
 *  ldapu_auth_uid_groupdn:
 *	Description:
 *	    Similar to ldapu_auth_userdn_groupdn but first maps the uid to a
 *	    full user DN before calling ldapu_auth_userdn_groupdn.
 *	Arguments:
 *	    ld			Pointer to LDAP (assumes connection has been
 *				established and the client has called the
 *				appropriate bind routine)
 *	    uid   		User's login name
 *	    groupdn		Group's full DN
 *	    base		basedn (where to start the search)
 *	Return Values: (same as ldapu_find)
 *	    LDAPU_SUCCESS	if user is member of the group
 *	    LDAPU_FAILED	if user is not a member of the group
 *	    <rv>		if error, where <rv> can be passed to
 *				ldap_err2string to get an error string.
 */
int ldapu_auth_uid_groupdn (LDAP *ld, const char *uid, const char *groupdn,
			    const char *base)
{
    int  retval;
    char *dn;

    /* First find userdn for the given uid and
       then call ldapu_auth_userdn_groupdn */
    retval = ldapu_find_userdn(ld, uid, base, &dn);

    if (retval == LDAPU_SUCCESS) {

	retval = ldapu_auth_userdn_groupdn(ld, dn, groupdn, base);
	ldap_memfree(dn);
    }

    return retval;
}

/*
 *  ldapu_auth_uid_groupid:
 *	Description:
 *	    Similar to ldapu_auth_uid_groupdn but first maps the groupid to a
 *	    full group DN before calling ldapu_auth_uid_groupdn.
 *	Arguments:
 *	    ld			Pointer to LDAP (assumes connection has been
 *				established and the client has called the
 *				appropriate bind routine)
 *	    uid   		User's login name
 *	    groupid		Group's name
 *	    base		basedn (where to start the search)
 *	Return Values: (same as ldapu_find)
 *	    LDAPU_SUCCESS	if user is member of the group
 *	    LDAPU_FAILED	if user is not a member of the group
 *	    <rv>		if error, where <rv> can be passed to
 *				ldap_err2string to get an error string.
 */
int ldapu_auth_uid_groupid (LDAP *ld, const char *uid,
			    const char *groupid, const char *base)
{
    int	    retval;
    char    *dn;

    /* First find groupdn for the given groupid and
       then call ldapu_auth_uid_groupdn */
    retval = ldapu_find_groupdn(ld, groupid, base, &dn);

    if (retval == LDAPU_SUCCESS) {
	retval = ldapu_auth_uid_groupdn(ld, uid, dn, base);
	ldapu_memfree(ld, dn);
    }

    return retval;
}

/*
 *  ldapu_auth_userdn_groupid:
 *	Description:
 *	    Similar to ldapu_auth_userdn_groupdn but first maps the groupid to a
 *	    full group DN before calling ldapu_auth_userdn_groupdn.
 *	Arguments:
 *	    ld			Pointer to LDAP (assumes connection has been
 *				established and the client has called the
 *				appropriate bind routine)
 *	    userdn		User's full DN
 *	    groupid		Group's name
 *	    base		basedn (where to start the search)
 *	Return Values: (same as ldapu_find)
 *	    LDAPU_SUCCESS	if user is member of the group
 *	    LDAPU_FAILED	if user is not a member of the group
 *	    <rv>		if error, where <rv> can be passed to
 *				ldap_err2string to get an error string.
 */
int ldapu_auth_userdn_groupid (LDAP *ld, const char *userdn,
			       const char *groupid, const char *base)
{
    int	    retval;
    char    *groupdn;

    /* First find groupdn for the given groupid and
       then call ldapu_auth_userdn_groupdn */
    retval = ldapu_find_groupdn(ld, groupid, base, &groupdn);

    if (retval == LDAPU_SUCCESS) {
	retval = ldapu_auth_userdn_groupdn(ld, userdn, groupdn, base);
	ldap_memfree(groupdn);
    }

    return retval;
}


LDAPUStr_t *ldapu_str_alloc (const int size)
{
    LDAPUStr_t *lstr = (LDAPUStr_t *)ldapu_malloc(sizeof(LDAPUStr_t));

    if (!lstr) return 0;
    lstr->size = size < 0 ? 1024 : size;
    lstr->str = (char *)ldapu_malloc(lstr->size*sizeof(char));
    lstr->len = 0;
    lstr->str[lstr->len] = 0;

    return lstr;
}


void ldapu_str_free (LDAPUStr_t *lstr)
{
    if (lstr) {
	if (lstr->str) ldapu_free(lstr->str);
	ldapu_free((void *)lstr);
    }
}


int ldapu_str_append(LDAPUStr_t *lstr, const char *arg)
{
    int arglen = strlen(arg);
    int len = lstr->len + arglen;

    if (len >= lstr->size) {
	/* realloc some more */
	lstr->size += arglen > 4095 ? arglen+1 : 4096;
	lstr->str = (char *)ldapu_realloc(lstr->str, lstr->size);
	if (!lstr->str) return LDAPU_ERR_OUT_OF_MEMORY;
    }

    memcpy((void *)&(lstr->str[lstr->len]), (void *)arg, arglen);
    lstr->len += arglen;
    lstr->str[lstr->len] = 0;
    return LDAPU_SUCCESS;
}


/*
 *  ldapu_auth_userdn_groupids_recurse:
 *	Description:
 *	    Checks if the user is member of the given comma separated list of
 *	    group names.
 *	Arguments:
 *	    ld			Pointer to LDAP (assumes connection has been
 *				established and the client has called the
 *				appropriate bind routine)
 *	    filter		filter to use in the search
 *	    groupids		some representation of group names.  Example,
 *				a comma separated names in a string, hash
 *				table, etc.  This function doesn't need to
 *				know the name of the groups.  It calls the
 *				following function to check if one of the
 *				groups returned by the search is in the list.
 *	    grpcmpfn		group name comparison function.
 *	    base		basedn (where to start the search)
 *	    recurse_cnt		recursion count to detect circular groups
 *	    group_out		if successful, pointer to the user's group
 *	Return Values: (same as ldapu_find)
 *	    LDAPU_SUCCESS	if user is member of one of the groups
 *	    LDAPU_FAILED	if user is not a member of the group
 *	    <rv>		if error, where <rv> can be passed to
 *				ldap_err2string to get an error string.
 */
static int ldapu_auth_userdn_groupids_recurse (LDAP *ld, const char *filter,
					       void *groupids,
					       LDAPU_GroupCmpFn_t grpcmpfn,
					       const char *base,
					       int recurse_cnt,
					       char **group_out)
{
    LDAPMessage *res = 0;
    const char	*attrs[] = { "CN", 0 };
    int		attrsonly = 0;
    LDAPMessage *entry;
    int		rv;
    int		retval;
    int		i;
    int		done;

    if (recurse_cnt >= 30)
	return LDAPU_ERR_CIRCULAR_GROUPS;

    /* Perform the ldap lookup */
    retval = ldapu_find(ld, base, LDAP_SCOPE_SUBTREE, filter, attrs,
			attrsonly, &res);

    if (retval != LDAPU_SUCCESS && retval != LDAPU_ERR_MULTIPLE_MATCHES ) {
	/* user is not a member of any group */
	if (res) ldap_msgfree(res);
	return retval;
    }

    retval = LDAPU_FAILED;
    done = 0;

    /* check if one of the matched groups is one of the given groups */
    for (entry = ldap_first_entry(ld, res); entry != NULL && !done;
	 entry = ldap_next_entry(ld, entry))
    {
	struct berval **bvals;

	if ((bvals = ldap_get_values_len(ld, entry, "CN")) == NULL) {
	    /* This shouldn't happen */
	    retval = LDAPU_ERR_MISSING_ATTR_VAL;
	    continue;
	}

	/* "CN" may have multiple values */
	/* Check each value of "CN" against the 'groupids' */
	for ( i = 0; bvals[i] != NULL; i++ ) {
	    rv = (*grpcmpfn)(groupids, bvals[i]->bv_val, bvals[i]->bv_len);
	    if (rv == LDAPU_SUCCESS) {
		char *group = (char *)ldapu_malloc(bvals[i]->bv_len+1);
		
		if (!group) {
		    retval = LDAPU_ERR_OUT_OF_MEMORY;
		}
		else {
		    strncpy(group, bvals[i]->bv_val, bvals[i]->bv_len);
		    group[bvals[i]->bv_len] = 0;
		    *group_out = group;
		    retval = LDAPU_SUCCESS;
		}
		done = 1;	/* exit from the outer loop too */
		break;
	    }
	}

	ldap_value_free_len(bvals);
    }

    if (retval == LDAPU_FAILED) {
	/* None of the matched groups is in 'groupids' */
	/* Perform the nested group membership check */
	LDAPUStr_t *filter1;
	LDAPUStr_t *filter2;
	char *rfilter = 0;
	int rlen;
	/* Finally we need a filter which looks like:
	   (| (& (objectclass=groupofuniquenames)
	         (| (uniquemember=<grp1dn>)(uniquemember=<grp2dn>) ...))
	      (& (objectclass=groupofnames)
	         (| (member=<grp1dn>)(member=<grp2dn>) ...)))
	   Construct 2 sub-filters first as follows:
	       (uniquemember=<grp1dn>)(uniquemember=<grp2dn>)...  AND
	       (member=<grp1dn>)(member=<grp2dn>)...
	   Then insert them in the main filter.
	 */
	filter1 = ldapu_str_alloc(1024);
	filter2 = ldapu_str_alloc(1024);
	if (!filter1 || !filter2) return LDAPU_ERR_OUT_OF_MEMORY;
	rv = LDAPU_SUCCESS;

	for (entry = ldap_first_entry(ld, res); entry != NULL;
	     entry = ldap_next_entry(ld, entry))
	{
	    char *dn = ldap_get_dn(ld, entry);
	    if (((rv = ldapu_str_append(filter1, "(uniquemember="))
		 != LDAPU_SUCCESS) ||
		((rv = ldapu_str_append(filter1, dn)) != LDAPU_SUCCESS) ||
		((rv = ldapu_str_append(filter1, ")")) != LDAPU_SUCCESS) ||
		((rv = ldapu_str_append(filter2, "(member="))
		 != LDAPU_SUCCESS) ||
		((rv = ldapu_str_append(filter2, dn)) != LDAPU_SUCCESS) ||
		((rv = ldapu_str_append(filter2, ")")) != LDAPU_SUCCESS))
	    {
		ldap_memfree(dn);
		break;
	    }
	    ldap_memfree(dn);
	}

	if (rv != LDAPU_SUCCESS) {
	    /* something went wrong in appending to filter1 or filter2 */
	    ldapu_str_free(filter1);
	    ldapu_str_free(filter2);
	    retval = rv;
	}
	else {
	    /* Insert the 2 filters in the main filter */
	    rlen = filter1->len + filter2->len +
		strlen("(| (& (objectclass=groupofuniquenames)"
		       "(| ))"
		       "(& (objectclass=groupofnames)"
		       "(| )))") + 1;
	    rfilter = (char *)ldapu_malloc(rlen);
	    if (!rfilter) return LDAPU_ERR_OUT_OF_MEMORY;
	    sprintf(rfilter,
		    "(| (& (objectclass=groupofuniquenames)"
		    "(| %s))"
		    "(& (objectclass=groupofnames)"
		    "(| %s)))",
		    filter1->str, filter2->str);
	    ldapu_str_free(filter1);
	    ldapu_str_free(filter2);
	    retval = ldapu_auth_userdn_groupids_recurse(ld, rfilter, groupids,
							grpcmpfn, base, 
							++recurse_cnt,
							group_out);
	    ldapu_free(rfilter);
	}

    }

    if (res) ldap_msgfree(res);
    return retval;
}

/*
 *  ldapu_auth_userdn_groupids:
 *	Description:
 *	    Checks if the user is member of the given comma separated list of
 *	    group names.
 *	Arguments:
 *	    ld			Pointer to LDAP (assumes connection has been
 *				established and the client has called the
 *				appropriate bind routine)
 *	    userdn		User's full DN
 *	    groupids		some representation of group names.  Example,
 *				a comma separated names in a string, hash
 *				table, etc.  This function doesn't need to
 *				know the name of the groups.  It calls the
 *				following function to check if one of the
 *				groups returned by the search is in the list.
 *	    grpcmpfn		group name comparison function.
 *	    base		basedn (where to start the search)
 *	    group_out		if successful, pointer to the user's group
 *	Return Values: (same as ldapu_find)
 *	    LDAPU_SUCCESS	if user is member of one of the groups
 *	    LDAPU_FAILED	if user is not a member of the group
 *	    <rv>		if error, where <rv> can be passed to
 *				ldap_err2string to get an error string.
 */
int ldapu_auth_userdn_groupids (LDAP *ld, const char *userdn,
				void *groupids,
				LDAPU_GroupCmpFn_t grpcmpfn,
				const char *base,
				char **group_out)
{
    char	*filter;
    int		len;
    int		rv;

    *group_out = 0;
    /* allocate a big enough filter */
    /* The filter looks like:
       (| (& (objectclass=groupofuniquenames)(uniquemember=<userdn>))
          (& (objectclass=groupofnames)(member=<userdn>)))
     */

    len = 2 * strlen(userdn) + 1 +
	strlen("(| (& (objectclass=groupofuniquenames)(uniquemember=))"
	       "(& (objectclass=groupofnames)(member=)))");
    filter = (char *)ldapu_malloc(len);

    if (!filter) return LDAPU_ERR_OUT_OF_MEMORY;

    sprintf(filter, "(| (& (objectclass=groupofuniquenames)(uniquemember=%s))"
	    "(& (objectclass=groupofnames)(member=%s)))",
	    userdn, userdn);

    rv = ldapu_auth_userdn_groupids_recurse(ld, filter, groupids,
					    grpcmpfn, base, 
					    0, group_out);

    ldapu_free(filter);
    return rv;
}


/*
 *  ldapu_auth_userdn_attrfilter:
 *	Description:
 *	    Checks if the user's entry has the given attributes
 *	Arguments:
 *	    ld			Pointer to LDAP (assumes connection has been
 *				established and the client has called the
 *				appropriate bind routine)
 *	    userdn		User's full DN
 *	    attrfilter		attribute filter
 *	Return Values: (same as ldapu_find)
 *	    LDAPU_SUCCESS	if user is member of the group
 *	    LDAPU_FAILED	if user is not a member of the group
 *	    <rv>		if error, where <rv> can be passed to
 *				ldap_err2string to get an error string.
 */
int ldapu_auth_userdn_attrfilter (LDAP *ld, const char *userdn,
				  const char *attrfilter)
{
    const char	*base = userdn;
    int		scope = LDAP_SCOPE_BASE;
    const char	*filter = attrfilter;
    const char	**attrs = default_search_attrs;
    int		attrsonly = default_search_attrsonly;
    LDAPMessage *res = 0;
    int		retval;

    retval = ldapu_find(ld, base, scope, filter, attrs, attrsonly, &res);

    if (res) ldapu_msgfree(ld, res);

    return retval;
}

/*
 *  ldapu_auth_uid_attrfilter:
 *	Description:
 *	    Checks if the user's entry has the given attributes.  First maps
	    the uid to userdn.
 *	Arguments:
 *	    ld			Pointer to LDAP (assumes connection has been
 *				established and the client has called the
 *				appropriate bind routine)
 *	    uid   		User's name
 *	    attrfilter		attribute filter
 *	    base		basedn (where to start the search)
 *	Return Values: (same as ldapu_find)
 *	    LDAPU_SUCCESS	if user is member of the group
 *	    LDAPU_FAILED	if user is not a member of the group
 *	    <rv>		if error, where <rv> can be passed to
 *				ldap_err2string to get an error string.
 */
int ldapu_auth_uid_attrfilter (LDAP *ld, const char *uid, const char *attrfilter,
			       const char *base)
{
    int		scope = LDAP_SCOPE_SUBTREE;
    char	filter[ BUFSIZ ];
    const char	**attrs = default_search_attrs;
    int		attrsonly = default_search_attrsonly;
    LDAPMessage *res = 0;
    int		retval;

    /* setup filter as (& (uid=<uid>) (attrfilter)) */
    if (*attrfilter == '(') 
	PR_snprintf(filter, sizeof(filter), "(& (uid=%s) %s)", uid, attrfilter);
    else
	PR_snprintf(filter, sizeof(filter), "(& (uid=%s) (%s))", uid, attrfilter);

    retval = ldapu_find(ld, base, scope, filter, attrs, attrsonly, &res);

    if (res) ldapu_msgfree(ld, res);

    return retval;
}

/*
 *  ldapu_auth_userdn_password:
 *	Description:
 *	    Checks the user's password against LDAP by binding using the
 *	    userdn and the password.
 *	Arguments:
 *	    ld			Pointer to LDAP (assumes connection has been
 *				established and the client has called the
 *				appropriate bind routine)
 *	    userdn		User's full DN
 *	    password		User's password (clear text)
 *	Return Values: (same as ldapu_find)
 *	    LDAPU_SUCCESS	if user credentials are valid
 *	    <rv>		if error, where <rv> can be passed to
 *				ldap_err2string to get an error string.
 */
int ldapu_auth_userdn_password (LDAP *ld, const char *userdn, const char *password)
{
    int retval;

    DBG_PRINT2("\tuserdn:\t\"%s\"\n", userdn);
    DBG_PRINT2("\tpassword:\t\"%s\"\n", password);

    retval = ldap_simple_bind_s(ld, userdn, password);

    if (retval != LDAP_SUCCESS)
    {
	DBG_PRINT2("ldap_simple_bind_s: %s\n", ldap_err2string(retval));
	return(retval);
    }

    return LDAPU_SUCCESS;
}

/*
 *  ldapu_auth_uid_password:
 *	Description:
 *	    First converts the uid to userdn and calls
 *	    ldapu_auth_userdn_password.
 *	Arguments:
 *	    ld			Pointer to LDAP (assumes connection has been
 *				established and the client has called the
 *				appropriate bind routine)
 *	    uid   		User's name
 *	    password		User's password (clear text)
 *	Return Values: (same as ldapu_find)
 *	    LDAPU_SUCCESS	if user credentials are valid
 *	    <rv>		if error, where <rv> can be passed to
 *				ldap_err2string to get an error string.
 */
int ldapu_auth_uid_password (LDAP *ld, const char *uid,
			     const char *password, const char *base)
{
    int retval;
    char *dn;

    /* First find userdn for the given uid and
       then call ldapu_auth_userdn_password */
    retval = ldapu_find_userdn(ld, uid, base, &dn);

    if (retval == LDAPU_SUCCESS) {
	retval = ldapu_auth_userdn_password(ld, dn, password);
	ldapu_memfree(ld, dn);
    }

    return retval;
}


/* ldapu_string_set --
 * This function is not tested yet for its usefulness.  This is to be used to
 * customize the strings used in the LDAP searches performed through
 * 'ldaputil'.  This could also be extended to setting customized error
 * messages (and even i18n equivalent of error messages).
 */
NSAPI_PUBLIC int ldapu_string_set (const int type, const char *filter)
{
    if (!filter || !*filter) return LDAPU_ERR_INVALID_STRING;

    if (type < 0 || type >= LDAPU_STR_MAX_INDEX)
	return LDAPU_ERR_INVALID_STRING_INDEX;

    ldapu_strings[type] = strdup(filter);

    if (!ldapu_strings[type]) return LDAPU_ERR_OUT_OF_MEMORY;

    return LDAPU_SUCCESS;
}


NSAPI_PUBLIC const char *ldapu_string_get (const int type)
{
    if (type < 0 || type >= LDAPU_STR_MAX_INDEX)
	return 0;

    return ldapu_strings[type];
}

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

/*	lasemail.cpp
 *	This file contains the Email LAS code.
 */

#include <ldap.h>
#include <nsacl/aclapi.h>

#define ACL_ATTR_EMAIL "email"

extern "C" {
extern int LASEmailEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator, char *attr_pattern, ACLCachable_t *cachable, void **LAS_cookie, PList_t subject, PList_t resource, PList_t auth_info, PList_t global_auth);
extern void LASEmailFlush(void **las_cookie);
extern int LASEmailModuleInit();
}


/*
 *  LASEmailEval
 *    INPUT
 *	attr_name	The string "email" - in lower case.
 *	comparator	CMP_OP_EQ or CMP_OP_NE only
 *	attr_pattern	A comma-separated list of emails
 *			(we currently support only one e-mail addr)
 *	*cachable	Always set to ACL_NOT_CACHABLE.
 *      subject		Subject property list
 *      resource        Resource property list
 *      auth_info	Authentication info, if any
 *    RETURNS
 *	retcode	        The usual LAS return codes.
 */
int LASEmailEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator, 
		 char *attr_pattern, ACLCachable_t *cachable,
		 void **LAS_cookie, PList_t subject, PList_t resource,
		 PList_t auth_info, PList_t global_auth)
{
    char	    *uid;
    char	    *email;
    int		    rv;
    LDAP	    *ld;
    char	    *basedn;
    LDAPMessage	    *res;
    int		    numEntries;
    char	    filter[1024];
    int		    matched;

    *cachable = ACL_NOT_CACHABLE;
    *LAS_cookie = (void *)0;

    if (strcmp(attr_name, ACL_ATTR_EMAIL) != 0) {
	fprintf(stderr, "LASEmailEval called for incorrect attr \"%s\"\n",
		attr_name);
	return LAS_EVAL_INVALID;
    }

    if ((comparator != CMP_OP_EQ) && (comparator != CMP_OP_NE)) {
	fprintf(stderr, "LASEmailEval called with incorrect comparator %d\n",
		comparator);
	return LAS_EVAL_INVALID;
    }

    if (!strcmp(attr_pattern, "anyone")) {
        *cachable = ACL_INDEF_CACHABLE;
	return comparator == CMP_OP_EQ ? LAS_EVAL_TRUE : LAS_EVAL_FALSE;
    }

    /* get the authenticated user name */
    rv = ACL_GetAttribute(errp, ACL_ATTR_USER, (void **)&uid,
			  subject, resource, auth_info, global_auth);

    if (rv != LAS_EVAL_TRUE) {
	return rv;
    }

    /* We have an authenticated user */
    if (!strcmp(attr_pattern, "all")) {
	return comparator == CMP_OP_EQ ? LAS_EVAL_TRUE : LAS_EVAL_FALSE;
    }

    /* do an ldap lookup for: (& (uid=<user>) (mail=<email>)) */
    rv = ACL_LDAPDatabaseHandle(errp, NULL, &ld, &basedn);

    if (rv != LAS_EVAL_TRUE) {
	fprintf(stderr, "unable to get LDAP handle\n");
	return rv;
    }

    /* Formulate the filter -- assume single e-mail in attr_pattern */
    /* If we support multiple comma separated e-mail addresses in the
     * attr_pattern then the filter will look like:
     *     (& (uid=<user>) (| (mail=<email1>) (mail=<email2>)))
     */
    sprintf(filter, "(& (uid=%s) (mail=%s))", uid, attr_pattern);

    rv = ldap_search_s(ld, basedn, LDAP_SCOPE_SUBTREE, filter,
		       0, 0, &res);

    if (rv != LDAP_SUCCESS)
    {
	fprintf(stderr, "ldap_search_s: %s\n", ldap_err2string(rv));
	return LAS_EVAL_FAIL;
    }

    numEntries = ldap_count_entries(ld, res);

    if (numEntries == 1) {
	/* success */
	LDAPMessage *entry = ldap_first_entry(ld, res);
	char *dn = ldap_get_dn(ld, entry);

	fprintf(stderr, "ldap_search_s: Entry found. DN: \"%s\"\n", dn);
	ldap_memfree(dn);
	matched = 1;
    }
    else if (numEntries == 0) {
	/* not found -- but not an error */
	fprintf(stderr, "ldap_search_s: Entry not found. Filter: \"%s\"\n",
		filter);
	matched = 0;
    }
    else if (numEntries > 0) {
	/* Found more than one entry! */
	fprintf(stderr, "ldap_search_s: Found more than one entry. Filter: \"%s\"\n",
		   filter);
	return LAS_EVAL_FAIL;
    }

    if (comparator == CMP_OP_EQ) {
	rv = (matched ? LAS_EVAL_TRUE : LAS_EVAL_FALSE);
    }
    else {
	rv = (matched ? LAS_EVAL_FALSE : LAS_EVAL_TRUE);
    }

    return rv;
}


/*	LASEmailFlush
 *	Deallocates any memory previously allocated by the LAS
 */
void
LASEmailFlush(void **las_cookie)
{
    /* do nothing */
    return;
}

/* LASEmailModuleInit --
 *  Register the e-mail LAS.
 *
 *  To load this functions in the web server, compile the file in
 *  "lasemail.so" and add the following lines to the
 *  <ServerRoot>/https-<name>/config/obj.conf file.  Be sure to change the 
 *  "lasemail.so" portion to the full pathname.  E.g.  /nshome/lib/lasemail.so.
 *
 *  Init fn="load-modules" funcs="LASEmailModuleInit" shlib="lasemail.so"
 *  Init fn="acl-register-module" module="lasemail" func="LASEmailModuleInit"
 */
int LASEmailModuleInit ()
{
    NSErr_t	err = NSERRINIT;
    NSErr_t	*errp = &err;
    int rv;

    rv = ACL_LasRegister(errp, ACL_ATTR_EMAIL, LASEmailEval, LASEmailFlush);

    if (rv < 0) {
	fprintf(stderr, "ACL_LasRegister failed.  Error: %d\n", rv);
	return rv;
    }

    return rv;
}


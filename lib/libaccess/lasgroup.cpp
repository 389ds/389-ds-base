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


/* #define DBG_PRINT */

/*	lasgroup.c
 *	This file contains the Group LAS code.
 */
#include <stdio.h>
#include <string.h>
#include <netsite.h>
#include "aclpriv.h"
#include <libaccess/usrcache.h>
#include <libaccess/las.h>
#include <libaccess/dbtlibaccess.h>
#include <libaccess/aclerror.h>
#include <ldaputil/errors.h>	/* for DBG_PRINT */
#include "aclutil.h"

#ifdef UTEST
extern char *LASGroupGetUser();
#endif /* UTEST */


/*
 *  LASGroupEval
 *    INPUT
 *	attr_name	The string "group" - in lower case.
 *	comparator	CMP_OP_EQ or CMP_OP_NE only
 *	attr_pattern	A comma-separated list of groups
 *	*cachable	Always set to ACL_NOT_CACHABLE
 *      subject		Subjust property list
 *	resource	Resource property list
 *	auth_info	Authentication info, if any
 *    RETURNS
 *	retcode	        The usual LAS return codes.
 */
int LASGroupEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator, 
		 char *attr_pattern, ACLCachable_t *cachable,
                 void **LAS_cookie, PList_t subject, PList_t resource,
                 PList_t auth_info, PList_t global_auth)
{
    char	    *groups = attr_pattern;
    int		    retcode;
    char	    *member_of;
    char	    *user;
    char	    *dbname;
    time_t	    *req_time = 0;
    const char	    *group;
    char	    delim;
    int		    len;
    int		    rv;

    *cachable = ACL_NOT_CACHABLE;
    *LAS_cookie = (void *)0;

    if (strcmp(attr_name, ACL_ATTR_GROUP) != 0) {
	nserrGenerate(errp, ACLERRINVAL, ACLERR4900, ACL_Program, 2, XP_GetAdminStr(DBT_lasGroupEvalReceivedRequestForAt_), attr_name);
	return LAS_EVAL_INVALID;
    }

    if ((comparator != CMP_OP_EQ) && (comparator != CMP_OP_NE)) {
	nserrGenerate(errp, ACLERRINVAL, ACLERR4910, ACL_Program, 2, XP_GetAdminStr(DBT_lasgroupevalIllegalComparatorDN_), comparator_string(comparator));
	return LAS_EVAL_INVALID;
    }

    if (!strcmp(attr_pattern, "anyone")) {
        *cachable = ACL_INDEF_CACHABLE;
	return comparator == CMP_OP_EQ ? LAS_EVAL_TRUE : LAS_EVAL_FALSE;
    }

    /* Get the authenticated user */
    rv = ACL_GetAttribute(errp, ACL_ATTR_USER, (void **)&user,
			  subject, resource, auth_info, global_auth);

    if (rv != LAS_EVAL_TRUE) {
	return rv;
    }

    rv = ACL_AuthInfoGetDbname(auth_info, &dbname);

    if (rv < 0) {
	char rv_str[16];
	sprintf(rv_str, "%d", rv);
	nserrGenerate(errp, ACLERRFAIL, ACLERR4920, ACL_Program, 2, XP_GetAdminStr(DBT_lasGroupEvalUnableToGetDatabaseName), rv_str);
        return LAS_EVAL_FAIL;
    }

    /* Regardless of cache, req_time needs to be filled. */
    req_time = acl_get_req_time(resource);
    if (NULL == req_time) {
        return LAS_EVAL_FAIL;
    }

    rv = LAS_EVAL_FALSE;
    if (acl_usr_cache_enabled()) {
	/* Loop through all the groups and check if any is in the cache */
	group = groups;
	delim = ',';
	while((group = acl_next_token_len(group, delim, &len)) != NULL) {
	    rv = acl_usr_cache_group_len_check(user, dbname, group, len, *req_time);
	    if (rv == LAS_EVAL_TRUE) {
		/* cached group exists */
		break;
	    }
	    if (0 != (group = strchr(group+len, delim)))
		group++;
	    else
		break;
	}
	/* group need not be NULL-terminated */
	/* If you need to use it, copy it properly */
	group = 0;
    }

    if (rv != LAS_EVAL_TRUE) {
	/* not found in the cache or not one of the groups we want */
	PListDeleteProp(subject, ACL_ATTR_GROUPS_INDEX, ACL_ATTR_GROUPS);
	PListInitProp(subject, ACL_ATTR_GROUPS_INDEX, ACL_ATTR_GROUPS, groups, 0);
	PListDeleteProp(subject, ACL_ATTR_USER_ISMEMBER_INDEX, ACL_ATTR_USER_ISMEMBER);
	rv = ACL_GetAttribute(errp, ACL_ATTR_USER_ISMEMBER, (void **)&member_of,
			      subject, resource, auth_info, global_auth);

	PListDeleteProp(subject, ACL_ATTR_GROUPS_INDEX, ACL_ATTR_GROUPS);

	if (rv != LAS_EVAL_TRUE && rv != LAS_EVAL_FALSE) {
	    return rv;
	}

	if (rv == LAS_EVAL_TRUE) {
	    /* User is a member of one of the groups */
	    /* update the user's cache */
	    acl_usr_cache_set_group(user, dbname, member_of, *req_time);
	}
    }

    if (rv == LAS_EVAL_TRUE) {
	retcode = (comparator == CMP_OP_EQ ? LAS_EVAL_TRUE : LAS_EVAL_FALSE);
    }
    else {
	/* User is not a member of any of the groups */
	retcode = (comparator == CMP_OP_EQ ? LAS_EVAL_FALSE : LAS_EVAL_TRUE);
    }

    DBG_PRINT4("%s LASGroupEval: uid = \"%s\" groups = \"%s\"\n",
	       (retcode == LAS_EVAL_FALSE) ? "LAS_EVAL_FALSE"
	       : (retcode == LAS_EVAL_TRUE) ? "LAS_EVAL_TRUE"
	       : "Error",
	       user, attr_pattern);

    return retcode;
}


/*	LASGroupFlush
 *	Deallocates any memory previously allocated by the LAS
 */
void
LASGroupFlush(void **las_cookie)
{
    /* do nothing */
    return;
}

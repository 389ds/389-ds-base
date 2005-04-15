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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*	lasuser.c
 *	This file contains the User LAS code.
 */

#include <netsite.h>
#include <base/shexp.h>
#include <base/util.h>
#include <libaccess/las.h>
#include <libaccess/dbtlibaccess.h>
#include <libaccess/aclerror.h>
#include "aclutil.h"

#ifdef	UTEST
extern char * LASUserGetUser();
#endif


/*
 *  LASUserEval
 *    INPUT
 *	attr_name	The string "user" - in lower case.
 *	comparator	CMP_OP_EQ or CMP_OP_NE only
 *	attr_pattern	A comma-separated list of users
 *	*cachable	Always set to ACL_NOT_CACHABLE.
 *      subject		Subject property list
 *      resource        Resource property list
 *      auth_info	Authentication info, if any
 *    RETURNS
 *	retcode	        The usual LAS return codes.
 */
int LASUserEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator, 
		char *attr_pattern, ACLCachable_t *cachable,
                void **LAS_cookie, PList_t subject, PList_t resource,
                PList_t auth_info, PList_t global_auth)
{
    char	    *uid;
    char	    *users;
    char	    *user;
    char	    *comma;
    int		    retcode;
    int		    matched;
    int		    is_owner;
    int		    rv;

    *cachable = ACL_NOT_CACHABLE;
    *LAS_cookie = (void *)0;

    if (strcmp(attr_name, ACL_ATTR_USER) != 0) {
	nserrGenerate(errp, ACLERRINVAL, ACLERR5700, ACL_Program, 2, XP_GetAdminStr(DBT_lasUserEvalReceivedRequestForAtt_), attr_name);
	return LAS_EVAL_INVALID;
    }

    if ((comparator != CMP_OP_EQ) && (comparator != CMP_OP_NE)) {
	nserrGenerate(errp, ACLERRINVAL, ACLERR5710, ACL_Program, 2, XP_GetAdminStr(DBT_lasuserevalIllegalComparatorDN_), comparator_string(comparator));
	return LAS_EVAL_INVALID;
    }

    if (!strcmp(attr_pattern, "anyone")) {
        *cachable = ACL_INDEF_CACHABLE;
	return comparator == CMP_OP_EQ ? LAS_EVAL_TRUE : LAS_EVAL_FALSE;
    }

    /* get the authenticated user name */
#ifndef	UTEST

    rv = ACL_GetAttribute(errp, ACL_ATTR_USER, (void **)&uid,
			  subject, resource, auth_info, global_auth);

    if (rv != LAS_EVAL_TRUE) {
	return rv;
    }
#else
    uid	= (char *)LASUserGetUser();
#endif

    /* We have an authenticated user */
    if (!strcmp(attr_pattern, "all")) {
	return comparator == CMP_OP_EQ ? LAS_EVAL_TRUE : LAS_EVAL_FALSE;
    }

    users = STRDUP(attr_pattern);

    if (!users) {
	nserrGenerate(errp, ACLERRNOMEM, ACLERR5720, ACL_Program, 1,
	XP_GetAdminStr(DBT_lasuserevalRanOutOfMemoryN_));
	return LAS_EVAL_FAIL;
    }

    user = users;
    matched = 0;

    /* check if the uid is one of the users */
    while(user != 0 && *user != 0 && !matched) {
	if ((comma = strchr(user, ',')) != NULL) {
	    *comma++ = 0;
	}

	/* ignore leading whitespace */
	while(*user == ' ' || *user == '\t') user++;

	if (*user) {
	    /* ignore trailing whitespace */
	    int len = strlen(user);
	    char *ptr = user+len-1;

	    while(*ptr == ' ' || *ptr == '\t') *ptr-- = 0;
	}

	if (!strcasecmp(user, ACL_ATTR_OWNER)) {
	    rv = ACL_GetAttribute(errp, ACL_ATTR_IS_OWNER, (void **)&is_owner,
				  subject, resource, auth_info, global_auth);
	    if (rv == LAS_EVAL_TRUE)
		matched = 1;
	    else
		/* continue checking for next user */
		user = comma;
	}
	else if (!WILDPAT_CASECMP(uid, user)) {
	    /* uid is one of the users */
	    matched = 1;
	}
	else {
	    /* continue checking for next user */
	    user = comma;
	}
    }

    if (comparator == CMP_OP_EQ) {
	retcode = (matched ? LAS_EVAL_TRUE : LAS_EVAL_FALSE);
    }
    else {
	retcode = (matched ? LAS_EVAL_FALSE : LAS_EVAL_TRUE);
    }

    FREE(users);
    return retcode;
}


/*	LASUserFlush
 *	Deallocates any memory previously allocated by the LAS
 */
void
LASUserFlush(void **las_cookie)
{
    /* do nothing */
    return;
}

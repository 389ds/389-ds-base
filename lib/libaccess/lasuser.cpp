/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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

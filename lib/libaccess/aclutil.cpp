/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Source file for the TimeOfDay and DayOfWeek LAS drivers
 */

#include <netsite.h>
#include <base/crit.h>
/*  #include <plhash.h>  */
#include <libaccess/acl.h>
#include "aclpriv.h"
#include <libaccess/las.h>
#include <libaccess/nserror.h>
#include "aclutil.h"

/*      Generic evaluator of comparison operators in attribute evaluation
 *      statements.
 *      INPUT
 *              CmpOp_t ACL_TOKEN_EQ, ACL_TOKEN_NE etc.
 *              result          0 if equal, >0 if real > pattern, <0 if
 *                              real < pattern.
 *      RETURNS
 *              LAS_EVAL_TRUE or LAS_EVAL_FALSE	or LAS_EVAL_INVALID
 *	DEBUG
 *		Can add asserts that the strcmp failure cases are one of the
 *		remaining legal comparators.
 */
int
evalComparator(CmpOp_t ctok, int result)
{
    if (result == 0) {
        switch(ctok) {
        case CMP_OP_EQ:
        case CMP_OP_GE:
        case CMP_OP_LE:
            return LAS_EVAL_TRUE;
        case CMP_OP_NE:
        case CMP_OP_GT:
        case CMP_OP_LT:
            return LAS_EVAL_FALSE;
        default:
            return LAS_EVAL_INVALID;
        }
    } else if (result > 0) {
        switch(ctok) {
        case CMP_OP_GT:
        case CMP_OP_GE:
        case CMP_OP_NE:
            return LAS_EVAL_TRUE;
        case CMP_OP_LT:
        case CMP_OP_LE:
        case CMP_OP_EQ:
            return LAS_EVAL_FALSE;
        default:
            return LAS_EVAL_INVALID;
        }
    } else {				/* real < pattern */
        switch(ctok) {
        case CMP_OP_LT:
        case CMP_OP_LE:
        case CMP_OP_NE:
            return LAS_EVAL_TRUE;
        case CMP_OP_GT:
        case CMP_OP_GE:
        case CMP_OP_EQ:
            return LAS_EVAL_FALSE;
        default:
            return LAS_EVAL_INVALID;
        }
    }
}


/* 	Takes a string and returns the same string with all uppercase
*	letters converted to lowercase.
*/
void
makelower(char	*string)
{
     while (*string) {
          *string = tolower(*string);
          string++;
     }
}


/* 	Given an LAS_EVAL_* value, translates to ACL_RES_*  */
int
EvalToRes(int value)
{
	switch (value) {
	case LAS_EVAL_TRUE:
		return ACL_RES_ALLOW;
	case LAS_EVAL_FALSE:
		return ACL_RES_DENY;
	case LAS_EVAL_DECLINE:
		return ACL_RES_FAIL;
	case LAS_EVAL_FAIL:
		return ACL_RES_FAIL;
	case LAS_EVAL_INVALID:
		return ACL_RES_INVALID;
	case LAS_EVAL_NEED_MORE_INFO:
		return ACL_RES_DENY;
        default:
		PR_ASSERT(1);
		return ACL_RES_ERROR;
	}
}

const char *comparator_string (int comparator)
{
    static char invalid_cmp[32];

    switch(comparator) {
    case CMP_OP_EQ: return "CMP_OP_EQ";
    case CMP_OP_NE: return "CMP_OP_NE";
    case CMP_OP_GT: return "CMP_OP_GT";
    case CMP_OP_LT: return "CMP_OP_LT";
    case CMP_OP_GE: return "CMP_OP_GE";
    case CMP_OP_LE: return "CMP_OP_LE";
    default:
	sprintf(invalid_cmp, "unknown comparator %d", comparator);
	return invalid_cmp;
    }
}

/* Return the pointer to the next token after replacing the following 'delim'
 * char with NULL.
 * WARNING - Modifies the first parameter */
char *acl_next_token (char **ptr, char delim)
{
    char *str = *ptr;
    char *token = str;
    char *comma;

    if (!token) { *ptr = 0; return 0; }

    /* ignore leading whitespace */
    while(*token && isspace(*token)) token++;

    if (!*token) { *ptr = 0; return 0; }

    if ((comma = strchr(token, delim)) != NULL) {
	*comma++ = 0;
    }

    {
	/* ignore trailing whitespace */
	int len = strlen(token);
	char *sptr = token+len-1;
	
	while(*sptr == ' ' || *sptr == '\t') *sptr-- = 0;
    }

    *ptr = comma;
    return token;
}


/* Returns a pointer to the next token and it's length */
/* tokens are separated by 'delim' characters */
/* ignores whitespace surrounding the tokens */
const char *acl_next_token_len (const char *ptr, char delim, int *len)
{
    const char *str = ptr;
    const char *token = str;
    const char *comma;

    *len = 0;

    if (!token) { return 0; }

    /* ignore leading whitespace */
    while(*token && isspace(*token)) token++;

    if (!*token) { return 0; }
    if (*token == delim) { return token; } /* str starts with delim! */

    if ((comma = strchr(token, delim)) != NULL) {
	*len = comma - token;
    }
    else {
	*len = strlen(token);
    }

    {
	/* ignore trailing whitespace */
	const char *sptr = token + *len - 1;
	
	while(*sptr == ' ' || *sptr == '\t') {
	    sptr--;
	    (*len)--;
	}
    }

    return token;
}

/* acl_get_req_time --
 * If the REQ_TIME is available on the 'resource' plist, return it.
 * Otherwise, make a system call to get the time and insert the time on the
 * 'resource' PList.  Allocate the time_t structure using the 'resource'
 * PList's pool.
 */
time_t *acl_get_req_time (PList_t resource)
{
    time_t *req_time = 0;
    int rv = PListGetValue(resource, ACL_ATTR_TIME_INDEX, (void **)&req_time,
			   NULL);

    if (rv < 0) {
	req_time = (time_t *)pool_malloc(PListGetPool(resource), sizeof(time_t));
	time(req_time);
	PListInitProp(resource, ACL_ATTR_TIME_INDEX, ACL_ATTR_TIME,
		      (void *)req_time, NULL);
    }

    return req_time;
}

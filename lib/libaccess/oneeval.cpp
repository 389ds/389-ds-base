/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Description (acleval.c)
 *
 *	This module provides functions for evaluating Access Control List
 *	(ACL) structures in memory.
 *
 */

#include <string.h>
#include <sys/types.h>
#include <assert.h>

#include <netsite.h>
#include <base/systems.h>
#include <base/crit.h>
#include <libaccess/nserror.h>
#include <libaccess/acl.h>
#include "aclpriv.h"
#include <libaccess/aclproto.h>
#include <libaccess/las.h>
#include <libaccess/symbols.h>
#include <libaccess/aclerror.h>
#include <libaccess/aclglobal.h>
#include <libaccess/dbtlibaccess.h>
#include <libaccess/aclerror.h>
#include "access_plhash.h"
#include "aclutil.h"
#include "aclcache.h"
#include "oneeval.h"
#include "permhash.h"

static ACLDispatchVector_t __nsacl_vector = {

    /* Error frame stack support */

    nserrDispose,
    nserrFAlloc,
    nserrFFree,
    nserrGenerate,

    /* Property list support */

    PListAssignValue,
    PListCreate,
    PListDefProp,
    PListDeleteProp,
    PListFindValue,
    PListInitProp,
    PListNew,
    PListDestroy,
    PListGetValue,
    PListNameProp,
    PListSetType,
    PListSetValue,
    PListEnumerate,
    PListDuplicate,
    PListGetPool,

    /* ACL attribute handling */

    ACL_LasRegister,

    /* method/dbtype registration routines */

    ACL_MethodRegister,
    ACL_MethodIsEqual,
    ACL_MethodNameIsEqual,
    ACL_MethodFind,
    ACL_MethodGetDefault,
    ACL_MethodSetDefault,
    ACL_AuthInfoGetMethod,

    ACL_DbTypeRegister,
    ACL_DbTypeIsEqual,
    ACL_DbTypeNameIsEqual,
    ACL_DbTypeFind,
    ACL_DbTypeGetDefault,
    ACL_AuthInfoGetDbType,
    ACL_DbTypeIsRegistered,
    ACL_DbTypeParseFn,

    ACL_AttrGetterRegister,

    ACL_ModuleRegister,
    ACL_GetAttribute,
    ACL_DatabaseRegister,
    ACL_DatabaseFind,
    ACL_DatabaseSetDefault,
    ACL_LDAPDatabaseHandle,
    ACL_AuthInfoGetDbname,
    ACL_CacheFlushRegister,
    ACL_CacheFlush,

    /*  ACL language and file interfaces */

    ACL_ParseFile,
    ACL_ParseString,
    ACL_WriteString,
    ACL_WriteFile,
    ACL_FileRenameAcl,
    ACL_FileDeleteAcl,
    ACL_FileGetAcl,
    ACL_FileSetAcl,

    /*  ACL Expression construction interfaces  */

    ACL_ExprNew,
    ACL_ExprDestroy,
    ACL_ExprSetPFlags,
    ACL_ExprClearPFlags,
    ACL_ExprTerm,
    ACL_ExprNot,
    ACL_ExprAnd,
    ACL_ExprOr,
    ACL_ExprAddAuthInfo,
    ACL_ExprAddArg,
    ACL_ExprSetDenyWith,
    ACL_ExprGetDenyWith,
    ACL_ExprAppend,

    /* ACL manipulation */

    ACL_AclNew,
    ACL_AclDestroy,

    /* ACL list manipulation */

    ACL_ListNew,
    ACL_ListConcat,
    ACL_ListAppend,
    ACL_ListDestroy,
    ACL_ListFind,
    ACL_ListAclDelete,
    ACL_ListGetNameList,
    ACL_NameListDestroy,

    /* ACL evaluation */

    ACL_EvalTestRights,
    ACL_EvalNew,
    ACL_EvalDestroy,
    ACL_EvalSetACL,
    ACL_EvalGetSubject,
    ACL_EvalSetSubject,
    ACL_EvalGetResource,
    ACL_EvalSetResource,

    /* Access to critical section for ACL cache */

    ACL_CritEnter,
    ACL_CritExit,

    /* Miscellaneous functions */

    ACL_AclGetTag,
    ACL_ListGetFirst,
    ACL_ListGetNext,

    /* Functions added after ES 3.0 release */
    ACL_DatabaseGetDefault,
    ACL_SetDefaultResult,
    ACL_GetDefaultResult
};

NSAPI_PUBLIC ACLDispatchVector_t *__nsacl_table = &__nsacl_vector;

int ACLEvalAce(
            NSErr_t *errp, 
            ACLEvalHandle_t *acleval, 
            ACLExprHandle_t *ace, 
            ACLCachable_t *cachable,
	    PList_t autharray[],
	    PList_t global_auth
           )
{
    ACLCachable_t      local_cachable;
    int                result;
    ACLExprEntry_t    *expr;
    int	               expr_index = 0;

    expr    = &ace->expr_arry[0];
    *cachable    = ACL_INDEF_CACHABLE;

    while (TRUE)
    {
        local_cachable    = ACL_NOT_CACHABLE;

        /*    Call the LAS driver    */
	if (!expr->las_eval_func) {
            ACL_CritEnter();
	    if (!expr->las_eval_func) {	/* Must check again after locking */
	        ACL_LasFindEval(errp, expr->attr_name, &expr->las_eval_func);
	        if (!expr->las_eval_func) {	/* Couldn't find it */
                    ACL_CritExit();
		    return LAS_EVAL_INVALID;
		}
	    }
	    ACL_CritExit();
	}
        result    = (*expr->las_eval_func)(
			  errp, 
                          expr->attr_name, 
                          expr->comparator, 
                          expr->attr_pattern, 
                          &local_cachable, 
                          &expr->las_cookie,
			  acleval->subject,
			  acleval->resource,
			  autharray ? autharray[expr_index] : NULL,
			  global_auth);

        /*    Evaluate the cachable value    */
        if (local_cachable < *cachable) {

            /* Take the minimum value */
            *cachable = local_cachable;
        }

        /*    Evaluate the return code    */
        switch (result) {
            case LAS_EVAL_TRUE:
                if (expr->true_idx < 0) 
                    return (expr->true_idx);
                else {
		    expr_index = expr->true_idx;
                    expr    = &ace->expr_arry[expr->true_idx];
		}
                break;

            case LAS_EVAL_FALSE:
                if (expr->false_idx < 0) 
                    return (expr->false_idx);
                else {
		    expr_index = expr->false_idx;
                    expr    = &ace->expr_arry[expr->false_idx];
		}
                break;

            default:
                return (result);
        }

    }
}


int
ACL_EvalDestroyContext(ACLListCache_t *cache)
{
    ACLAceEntry_t	*cur_ace, *next_ace;
    ACLAceNumEntry_t    *cur_num_p, *next_num_p;
    ACLExprHandle_t	*acep;

    if (!cache)
        return 0;

    PR_HashTableDestroy(cache->Table);
    cache->Table    = NULL;

    cur_ace    = cache->acelist;
    cache->acelist    = NULL;
    while (cur_ace) {
	if (cur_ace->autharray)
	    PERM_FREE(cur_ace->autharray);
	if ((cur_ace->global_auth) && 
	    (cur_ace->acep->expr_type == ACL_EXPR_TYPE_AUTH))
	    PListDestroy(cur_ace->global_auth);
        next_ace = cur_ace->next;
        acep     = cur_ace->acep;    /* The ACE structure itself */
        PERM_FREE(cur_ace);
        cur_ace = next_ace;
    }

    cur_num_p    = cache->chain_head;
    cache->chain_head    = NULL;
    while (cur_num_p) {
        next_num_p = cur_num_p->chain;
        PERM_FREE(cur_num_p);
        cur_num_p = next_num_p;
    }

    PERM_FREE(cache);

    return 0;
}


/*    ACLEvalBuildContext
 *    Builds three structures:
 *      Table - A hash table of all access rights referenced by any ACE in any
 *              of the ACLs in this list.  Each hash entry then has a list of
 *              the relevant ACEs, in the form of indexes to the ACE linked
 *              list.
 *      ACE List - A linked list of all the ACEs in the proper evaluation order.
 *
 *    For concurrency control, the caller must call ACL_CritEnter()
 */
int
ACLEvalBuildContext(
    NSErr_t            *errp,
    ACLEvalHandle_t    *acleval)
{
    ACLHandle_t        *acl;
    ACLExprHandle_t    *ace;
    int                ace_cnt = -1;
    ACLAceEntry_t      *acelast, *new_ace;
    ACLAceNumEntry_t   *entry, *temp_entry;
    char               **argp;
    ACLListCache_t     *cache;
    ACLWrapper_t       *wrapper;
    PList_t	       curauthplist=NULL, absauthplist=NULL;
    int		       i, rv;
    ACLExprEntry_t     *expr;
    PList_t	       authplist;

    /*     Allocate the cache context and link it into the ACLListHandle    */
    cache = (ACLListCache_t *)PERM_CALLOC(sizeof(ACLListCache_t));
    if (cache == NULL) {
	nserrGenerate(errp, ACLERRNOMEM, ACLERR4010, ACL_Program, 0);
	goto error;
    }

    /*    Allocate the access rights hash table    */
    cache->Table    =  PR_NewHashTable(0,
				       PR_HashString,
				       PR_CompareStrings,
				       PR_CompareValues,
				       &ACLPermAllocOps,
				       NULL);

    if (cache->Table == NULL) {
	nserrGenerate(errp, ACLERRNOMEM, ACLERR4000, ACL_Program, 1,
	              XP_GetAdminStr(DBT_EvalBuildContextUnableToCreateHash));
	goto error;
    }

    wrapper = acleval->acllist->acl_list_head;

    /* 	  Loop through all the ACLs in the list    */
    while (wrapper)        
    {
	acl = wrapper->acl;
        ace = acl->expr_list_head;

        while (ace)    /* Loop through all the ACEs in this ACL    */
        {

            /* allocate a new ace list entry and link it in    to the ordered
             * list.
             */
            new_ace = (ACLAceEntry_t *)PERM_CALLOC(sizeof(ACLAceEntry_t));
            if (new_ace == (ACLAceEntry_t *)NULL) {
		nserrGenerate(errp, ACLERRNOMEM, ACLERR4020, ACL_Program, 1,
		XP_GetAdminStr(DBT_EvalBuildContextUnableToAllocAceEntry));
		goto error;
            }
            new_ace->acep    = ace;
            ace_cnt++;

            if (cache->acelist == NULL)
                cache->acelist = acelast    = new_ace;
            else {
                acelast->next  = new_ace;
                acelast        = new_ace;
                new_ace->acep  = ace;
            }
            new_ace->next      = NULL;

            argp    = ace->expr_argv;

	    switch (ace->expr_type) 
	    {
	    case ACL_EXPR_TYPE_ALLOW:
	    case ACL_EXPR_TYPE_DENY:

                /* Add this ACE to the appropriate entries in the access rights
                 * hash table
                 */
                while (*argp)
                {
                    entry = 
		      (ACLAceNumEntry_t *)PERM_CALLOC(sizeof(ACLAceNumEntry_t));
                    if (entry == (ACLAceNumEntry_t *)NULL) {
		         nserrGenerate(errp, ACLERRNOMEM, ACLERR4030, ACL_Program, 1,
    	                               XP_GetAdminStr(DBT_EvalBuildContextUnableToAllocAceEntry));
			goto error;
                    }
                    if (cache->chain_head == NULL) 
                        cache->chain_head = cache->chain_tail = entry;
                    else {
                        cache->chain_tail->chain    = entry;
                        cache->chain_tail    = entry;
                    }
                    entry->acenum = ace_cnt;
   
			/*
			 * OK to call PL_HasTableLookup() even though it mods
			 * the Table as this routine is called in critical section.
			*/ 
		    temp_entry = (ACLAceNumEntry_t *)PL_HashTableLookup(cache->Table, *argp); 
		    /*  the first ACE for this right?  */
                    if (temp_entry) {
			/* Link it in at the end */
			while (temp_entry->next) {
			    temp_entry = temp_entry->next;
			}
			temp_entry->next = entry;
                    } else                    /* just link it in */
		        PR_HashTableAdd(cache->Table, *argp, entry);

                    argp++;

                }

		/*  See if any of the clauses require authentication.  */
		if (curauthplist) {
                    for (i = 0; i < ace->expr_term_index; i++) {
                        expr = &ace->expr_arry[i];
                        rv = PListFindValue(curauthplist, expr->attr_name, 
					    NULL, &authplist);
                    	if (rv > 0) {
                            /*  First one for this ACE?  */
                            if (!new_ace->autharray) {
                                new_ace->autharray = (PList_t *)PERM_CALLOC(sizeof(PList_t *) * ace->expr_term_index);
                                if (!new_ace->autharray) {
				    nserrGenerate(errp, ACLERRNOMEM, ACLERR4040, ACL_Program, 1, XP_GetAdminStr(DBT_EvalBuildContextUnableToAllocAuthPointerArray));
                                    goto error;
			        }
		            }
			new_ace->autharray[i] = authplist;
		        }
		    }
		}
		break;

	    case ACL_EXPR_TYPE_AUTH:

		/*  Allocate the running auth tables if none yet  */
		if (!curauthplist) {
		    curauthplist = PListNew(NULL);
		    if (!curauthplist) {
			nserrGenerate(errp, ACLERRNOMEM, ACLERR4050, ACL_Program, 1, XP_GetAdminStr(DBT_EvalBuildContextUnableToAllocAuthPlist));
			goto error;
		    }
		    absauthplist = PListNew(NULL);
		    if (!absauthplist) {
			nserrGenerate(errp, ACLERRNOMEM, ACLERR4050, ACL_Program, 1, XP_GetAdminStr(DBT_EvalBuildContextUnableToAllocAuthPlist));
			goto error;
		    }
		} else {  /* duplicate the existing auth table */
		    curauthplist = PListDuplicate(curauthplist, NULL, 0);
		    if (!curauthplist) {
			nserrGenerate(errp, ACLERRNOMEM, ACLERR4050, ACL_Program, 1, XP_GetAdminStr(DBT_EvalBuildContextUnableToAllocAuthPlist));
			goto error;
		    }
		}

		/*  For each listed attribute  */
                while (*argp)
                {
		    /*  skip any attributes that were absoluted  */
		    if (PListFindValue(absauthplist, *argp, NULL, NULL) < 0)
		    {
			/*  Save pointer to the property list  */
			PListInitProp(curauthplist, 0, *argp, ace->expr_auth,
				      ace->expr_auth);
                        if (IS_ABSOLUTE(ace->expr_flags))
			    PListInitProp(absauthplist, 0, *argp, NULL,
			    NULL);
		    }

                    argp++;
		}

		break;

	    case ACL_EXPR_TYPE_RESPONSE:
		(void) ACL_ExprGetDenyWith(NULL, ace, &cache->deny_type,
                                    &cache->deny_response); 
		break;

	    default:
		PR_ASSERT(0);
	    
	    }	/*  switch expr_type  */

          new_ace->global_auth = curauthplist;
          ace = ace->expr_next;        
        }

	/*  Next ACL please    */
        wrapper = wrapper->wrap_next;        
    }

    if (absauthplist)
	PListDestroy(absauthplist);

    /* This must be done last to avoid a race in initialization */
    acleval->acllist->cache    = (void *)cache;

    return 0;

error:
    if (absauthplist)
	PListDestroy(absauthplist);
    if (cache) {
        ACL_EvalDestroyContext(cache);
    }
    acleval->acllist->cache = NULL;
    return ACL_RES_ERROR;
}

/*    ACL_InvalidateSubjectPList
 *    Given a new authentication plist, enumerate the plist and for each
 *    key in the plist, search for the matching key in the subject plist
 *    and delete any matches.  E.g. "user", "group".
 */
void
ACL_InvalidateSubjectPList(char *attr, const void *value, void *user_data)
{
    PList_t subject = (PList_t)user_data;

    PListDeleteProp(subject, 0, attr);
    return;
}

NSAPI_PUBLIC int ACL_SetDefaultResult (NSErr_t *errp,
				       ACLEvalHandle_t *acleval,
				       int result)
{
    int rv;

    switch(result) {
    case ACL_RES_ALLOW:
    case ACL_RES_DENY:
    case ACL_RES_FAIL:
    case ACL_RES_INVALID:
	acleval->default_result = result;
	rv = 0;
	break;
    default:
	rv = -1;
    }

    return rv;
}

NSAPI_PUBLIC int ACL_GetDefaultResult (ACLEvalHandle_t *acleval)
{
    return acleval->default_result;
}

/*    ACL_INTEvalTestRights
 *    INPUT
 *    *errp         The usual error context stack
 *    *acleval      A list of ACLs
 *    **rights      An array of strings listing the requested rights
 *    **map_generic An array of strings listing the specific rights
 *                  that map from the generic rights.
 *    OUTPUT
 *    **deny_type   bong file type passed on the way back out  
 *    **deny_response bong file pathname passed on the way back out  
 *    **acl_tag	    Name of the ACL that denies access
 *    *expr_num     ACE number within the denying ACL
 *    *cachable	    Is the result cachable?
 */
static int
ACL_INTEvalTestRights(
    NSErr_t          *errp,
    ACLEvalHandle_t  *acleval,
    char    **rights,
    char    **map_generic,
    char    **deny_type,
    char    **deny_response,
    char    **acl_tag,
    int     *expr_num,
    ACLCachable_t *cachable)
{
    struct     rights_ent {
        char right[64];               /* lowercase-ed rights string    */
        int result;                   /* Interim result value          */
        int absolute;                 /* ACE with absolute keyword     */
	int count;		      /* # specific + generic rights   */
        ACLAceNumEntry_t *acelist[ACL_MAX_GENERIC+1];    
				      /* List of relevant ACEs         */
    };
    struct rights_ent *rarray_p;
    struct    rights_ent rights_arry[ACL_MAX_TEST_RIGHTS];
    ACLAceNumEntry_t *alllist;  /* List of ACEs for "all" rights */
    ACLAceEntry_t *cur_ace;
    ACLListCache_t *cache;
    int rights_cnt = 0;
    int prev_acenum, cur_acenum;
    int i, j, right_num, delta;
    ACLCachable_t ace_cachable;
    int result;
    int absolute;
    int skipflag;
    int g_num;    /* index into the generic rights array.  */
    char **g_rights;
    PList_t global_auth=NULL;
    int allow_error = 0;
    int allow_absolute = 0;
    char *allow_tag = NULL;
    int allow_num = 0;
    int default_result = ACL_GetDefaultResult(acleval);

    *acl_tag  = NULL;
    *expr_num = 0;
    *cachable = ACL_INDEF_CACHABLE;

    /*
     * The acleval contains the list of acis we are asking about.
     * In our case it's always of length 1.
     * The acleval is a per aclpb structure but
     * the acllist is a global structure derived from the global
     * aci cache--so access to acllist is multi-threaded.
     * Hence, for example the use of the "read-only" hash
     * lookup routines in this function--ACL_EvalTestRights()
	 * is called in a "reader only context" so this code is therefore
	 * thread-safe. 
	*/

    if (acleval->acllist == ACL_LIST_NO_ACLS) return ACL_RES_ALLOW;

    /*    Build up the access right - indexed structures    */
    if (acleval->acllist->cache == NULL) {
        ACL_CritEnter();
        if (acleval->acllist->cache == NULL) {	/* Check again */
            if (ACLEvalBuildContext(errp, acleval) == ACL_RES_ERROR) {
		nserrGenerate(errp, ACLERRINTERNAL, ACLERR4110, ACL_Program,
		              1, XP_GetAdminStr(DBT_EvalTestRightsEvalBuildContextFailed));
                ACL_CritExit();
                return ACL_RES_ERROR;
            }
	}
	ACL_CritExit();
    }
    cache = (ACLListCache_t *)acleval->acllist->cache;
    *deny_response = cache->deny_response;
    *deny_type = cache->deny_type;

    /*     For the list of rights requested, get back the list of relevant
     *     ACEs. If we want
     *     to alter the precedence of allow/deny, this would be a good 
     *     place to do it.
     */

    while (*rights)
    {
        rarray_p = &rights_arry[rights_cnt];

	/* Initialize the rights array entry */
        strcpy(&rarray_p->right[0], *rights);
        makelower(&rarray_p->right[0]);
        rarray_p->result    = default_result;
        rarray_p->absolute  = 0;
        rarray_p->count    = 1;		// There's always the specific right

	/* Locate the list of ACEs that apply to the right */
	rarray_p->acelist[0] = 
	  (ACLAceNumEntry_t *)ACL_HashTableLookup_const(cache->Table, rarray_p->right);

        /* See if the requested right also maps back to a generic right and
         * if so, locate the acelist for it as well.
         */
        if (map_generic)
        {
            for (g_rights=map_generic, g_num=0; *g_rights; g_rights++, g_num++)
            {
                if (strstr(*g_rights, rarray_p->right)) {
		    //  Add it to our acelist, but skip 0 'cause that's the
		    //  specific right.
		    rarray_p->acelist[rarray_p->count++] = 
		      (ACLAceNumEntry_t *)ACL_HashTableLookup_const(cache->Table, 
		      (char *)generic_rights[g_num]);
		    PR_ASSERT (rarray_p->count < ACL_MAX_GENERIC);
		}
            }
        }

        rights_cnt++;
        rights++;
	PR_ASSERT (rights_cnt < ACL_MAX_TEST_RIGHTS);
    }

    /*    Special case - look for an entry that applies to "all" rights     */
    alllist = (ACLAceNumEntry_t *)ACL_HashTableLookup_const(cache->Table, "all");

    /*    Ok, we've now got a list of relevant ACEs.  Now evaluate things.  */
    prev_acenum    = -1;
    cur_ace        = cache->acelist;

    /* Loop through the relevant ACEs for the requested rights    */
    while (TRUE)
    {
        cur_acenum = 10000;            /* Pick a really high num so we lose */
        /* Find the lowest ACE among the rights lists */
        for (i=0; i<rights_cnt; i++) {
            rarray_p = &rights_arry[i];
	    if (rarray_p->absolute) continue;	// This right doesn't matter
	    for (j=0; j<rarray_p->count; j++) {
                if  ((rarray_p->acelist[j] != NULL) &&
                     (rarray_p->acelist[j]->acenum < cur_acenum)) {
                    cur_acenum = rarray_p->acelist[j]->acenum;
		}
	    }
        }

        /* Special case - look for the "all" rights ace list and see if its
         * the lowest of all.
         */
        if (alllist && (alllist->acenum < cur_acenum))
                cur_acenum = alllist->acenum;

        /* If no new ACEs then we're done - evaluate the rights list    */
        if (cur_acenum == 10000) 
            break;

        /* Locate that ACE and evaluate it.  We have to step through the
         * linked list of ACEs to find it.
         */
        if (prev_acenum == -1)
            delta = cur_acenum;
        else
            delta = cur_acenum - prev_acenum;

        for (i=0; i<delta; i++)
            cur_ace = cur_ace->next;

        if (global_auth  &&  global_auth != cur_ace->global_auth) {
	    /* We must enumerate the auth_info plist and remove entries for
	     * each attribute from the subject property list.
	     */
	     PListEnumerate(cur_ace->global_auth, ACL_InvalidateSubjectPList,
	                    acleval->subject);
	}
        global_auth = cur_ace->global_auth;

        result    = ACLEvalAce(errp, acleval, cur_ace->acep, &ace_cachable,
			cur_ace->autharray, cur_ace->global_auth);

        /*    Evaluate the cachable value    */
        if (ace_cachable < *cachable) {
            /* Take the minimum value */
            *cachable = ace_cachable;
        }

        /* Under certain circumstances, no matter what happens later,
         * the current result is not gonna change.
         */
        if ((result != LAS_EVAL_TRUE) && (result != LAS_EVAL_FALSE)) {
	    if (cur_ace->acep->expr_type != ACL_EXPR_TYPE_ALLOW) {
		if (allow_error) {
	            *acl_tag = allow_tag;
	            *expr_num = allow_num;
	            return (allow_error);
		} else {
                    *acl_tag = cur_ace->acep->acl_tag;
        	    *expr_num = cur_ace->acep->expr_number;
                    return (EvalToRes(result));
		}
	    } else {
		/* If the error is on an allow statement, continue processing
		 * and see if a subsequent allow works.  If not, remember the
		 * error and return it.
		 */
		if (!allow_error) {
		    allow_error = EvalToRes(result);
                    allow_tag = cur_ace->acep->acl_tag;
	            allow_num = cur_ace->acep->expr_number;
		}
                if (IS_ABSOLUTE(cur_ace->acep->expr_flags)) {
		    allow_absolute = 1;
		}
	    }
	}

        /* Now apply the result to the rights array.  Look to see which rights'
         * acelist include the current one, or if the current one is on the
         * "all" rights ace list.
         */
        for (right_num=0; right_num<rights_cnt; right_num++) 
        {
            rarray_p = &rights_arry[right_num];

            /*    Have we fixated on a prior result?    */
            if (rarray_p->absolute) 
                continue;

            skipflag = 1;

            // Did this ace apply to this right?
	    for (i=0; i<rarray_p->count; i++) {
                if ((rarray_p->acelist[i]) &&
                    (rarray_p->acelist[i]->acenum == cur_acenum)) {
                    rarray_p->acelist[i] = rarray_p->acelist[i]->next;
                    skipflag = 0;
		}
            }

            /* This ace was on the "all" rights queue */
            if ((alllist) && (alllist->acenum == cur_acenum)) {
                skipflag = 0;
            }

            if (skipflag)
                continue;    /* doesn't apply to this right */

            if (IS_ABSOLUTE(cur_ace->acep->expr_flags) && (result ==
              LAS_EVAL_TRUE)) {
                rarray_p->absolute     = 1;
                absolute    = 1;
            } else 
                absolute    = 0;

            switch (cur_ace->acep->expr_type) {
            case ACL_EXPR_TYPE_ALLOW:
                if (result == LAS_EVAL_TRUE) {
                    rarray_p->result = ACL_RES_ALLOW;
		    if (!allow_absolute) {
			/* A previous ALLOW error was superceded */
		        allow_error = 0;
		    }
		}
		else if (!*acl_tag) {
	            *acl_tag = cur_ace->acep->acl_tag;
	            *expr_num = cur_ace->acep->expr_number;
		}
                break;
            case ACL_EXPR_TYPE_DENY:
                if (result == LAS_EVAL_TRUE) {
	            *acl_tag = cur_ace->acep->acl_tag;
	            *expr_num = cur_ace->acep->expr_number;
                    if (absolute) {
			if (allow_error) {
	                    *acl_tag = allow_tag;
	                    *expr_num = allow_num;
			    return (allow_error);
			}
                        return (ACL_RES_DENY);
		    }
                    rarray_p->result = ACL_RES_DENY;
                }
                break;
            default:
                /* a non-authorization ACE, just ignore    */
                break;
            }

        }

        /* This ace was on the "all" rights queue */
        if ((alllist) && (alllist->acenum == cur_acenum)) {
                alllist = alllist->next;
        }

        /* If this is an absolute, check to see if all the rights
         * have already been fixed by this or previous absolute
         * statements.  If so, we can compute the response without
         * evaluating any more of the ACL list.
         */
        if (absolute) {
            for (i=0; i<rights_cnt; i++) {
                /* Non absolute right, so skip this section */
                if    (rights_arry[i].absolute == 0)
                    break;
                /* This shouldn't be possible, but check anyway.
                 * Any absolute non-allow result should already 
                 * have been returned earlier.
                 */
                if    (rights_arry[i].result != ACL_RES_ALLOW) {
		    char result_str[16];
		    sprintf(result_str, "%d", rights_arry[i].result);
		    nserrGenerate(errp, ACLERRINTERNAL, ACLERR4100, ACL_Program, 3, XP_GetAdminStr(DBT_EvalTestRightsInterimAbsoluteNonAllowValue), rights[i], result_str);
                    break;
                }
                if (i == (rights_cnt - 1))
                    return ACL_RES_ALLOW;
            }
        }

        prev_acenum    = cur_acenum;

    }        /* Next ACE    */

    /*    Do an AND on the results for the individual rights    */
    for (right_num=0; right_num<rights_cnt; right_num++) 
        if    (rights_arry[right_num].result != ACL_RES_ALLOW) {
            if (allow_error) {
                *acl_tag = allow_tag;
                *expr_num = allow_num;
	        return (allow_error);
	    }
            return (rights_arry[right_num].result);
	}

    return (ACL_RES_ALLOW);

}


/*  ACL_CachableAclList
 *  Returns 1 if the ACL list will always evaluate to ALLOW for http_get.
 */
NSAPI_PUBLIC int
ACL_CachableAclList(ACLListHandle_t *acllist)
{
    ACLEvalHandle_t *acleval;
    char *bong;
    char *bong_type;
    char *acl_tag;
    int  expr_num;
    int  rv;
    static char *rights[] = { "http_get", NULL };
    ACLCachable_t cachable=ACL_INDEF_CACHABLE;

    if (!acllist  ||  acllist == ACL_LIST_NO_ACLS) {
        return 1;
    }
    acleval = ACL_EvalNew(NULL, NULL);
    ACL_EvalSetACL(NULL, acleval, acllist);
    rv = ACL_INTEvalTestRights(NULL, acleval, rights, http_generic, 
                               &bong_type, &bong, &acl_tag, &expr_num, 
                               &cachable);

    ACL_EvalDestroyNoDecrement(NULL, NULL, acleval);
    if (rv == ACL_RES_ALLOW  &&  cachable == ACL_INDEF_CACHABLE) {
        return 1;
    }

    return 0;
}
    

NSAPI_PUBLIC int
ACL_EvalTestRights(
    NSErr_t          *errp,
    ACLEvalHandle_t  *acleval,
    char    **rights,
    char    **map_generic,
    char    **deny_type,
    char    **deny_response,
    char    **acl_tag,
    int     *expr_num)
{
    ACLCachable_t cachable;

    return (ACL_INTEvalTestRights(errp, acleval, rights, map_generic, 
                                  deny_type, deny_response, 
                                  acl_tag, expr_num, &cachable));
}


NSAPI_PUBLIC ACLEvalHandle_t *
ACL_EvalNew(NSErr_t *errp, pool_handle_t *pool)
{
    ACLEvalHandle_t *rv = ((ACLEvalHandle_t *)pool_calloc(pool, sizeof(ACLEvalHandle_t), 1));
    rv->default_result = ACL_RES_DENY;
    return rv;
}

NSAPI_PUBLIC void
ACL_EvalDestroy(NSErr_t *errp, pool_handle_t *pool, ACLEvalHandle_t *acleval)
{
    if (!acleval->acllist  ||  acleval->acllist == ACL_LIST_NO_ACLS)
	return;
    PR_ASSERT(acleval->acllist->ref_count > 0);

    ACL_CritEnter();
    PR_ASSERT(ACL_CritHeld());
    if (--acleval->acllist->ref_count == 0) {
	if (ACL_LIST_IS_STALE(acleval->acllist)) {
	    ACL_ListDestroy(errp, acleval->acllist);
	}
    }
    ACL_CritExit();
    pool_free(pool, acleval);
}

NSAPI_PUBLIC void
ACL_EvalDestroyNoDecrement(NSErr_t *errp, pool_handle_t *pool, ACLEvalHandle_t *acleval)
{
    /*if (!acleval->acllist  ||  acleval->acllist == ACL_LIST_NO_ACLS)
	return; */

    /* olga: we need to free acleval unconditionally to avoid memory leaks */
    if (acleval)
        pool_free(pool, acleval);
}

NSAPI_PUBLIC int
ACL_ListDecrement(NSErr_t *errp, ACLListHandle_t *acllist)
{
    if (!acllist  ||  acllist == ACL_LIST_NO_ACLS)
	return 0;

    PR_ASSERT(ACL_AssertAcllist(acllist));

    ACL_CritEnter();
    PR_ASSERT(ACL_CritHeld());
    if (--acllist->ref_count == 0) {
	if (ACL_LIST_IS_STALE(acllist)) {
	    ACL_ListDestroy(errp, acllist);
	}
    }
    ACL_CritExit();

    return 0;
}

NSAPI_PUBLIC int
ACL_EvalSetACL(NSErr_t *errp, ACLEvalHandle_t *acleval, ACLListHandle_t *acllist)
{
        PR_ASSERT(ACL_AssertAcllist(acllist));

        acleval->acllist = acllist;
        return(0);
}

NSAPI_PUBLIC int
ACL_EvalSetSubject(NSErr_t *errp, ACLEvalHandle_t *acleval, PList_t subject)
{
	acleval->subject = subject;
	return 0;
}

NSAPI_PUBLIC PList_t
ACL_EvalGetSubject(NSErr_t *errp, ACLEvalHandle_t *acleval)
{
	return (acleval->subject);
}

NSAPI_PUBLIC int
ACL_EvalSetResource(NSErr_t *errp, ACLEvalHandle_t *acleval, PList_t resource)
{
	acleval->resource = resource;
	return 0;
}

NSAPI_PUBLIC PList_t
ACL_EvalGetResource(NSErr_t *errp, ACLEvalHandle_t *acleval)
{
	return (acleval->resource);
}

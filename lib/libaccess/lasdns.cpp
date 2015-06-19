/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


/*	lasdns.c
 *	This file contains the DNS LAS code.
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netsite.h>
extern "C" {
#include <prnetdb.h>
}
#include <base/plist.h>
#include <base/pool.h>
#include <libaccess/nserror.h>
#include <libaccess/nsauth.h>
#include <libaccess/acl.h>
#include "aclpriv.h"
#include <libaccess/aclproto.h>
#include <libaccess/las.h>
#include "lasdns.h"
#include "aclutil.h"
#include "aclcache.h"
#include "permhash.h"
#include <libaccess/dbtlibaccess.h>
#include <libaccess/aclerror.h>
#include "access_plhash.h"
extern "C" {
#include <nspr.h>
}

#ifdef	UTEST
extern int LASDnsGetDns(char **dnsv);
#endif

/*    LASDnsMatch
 *    Given an array of fully-qualified dns names, tries to match them 
 *    against a given hash table.
 *    INPUT
 *    dns	DNS string	
 *    context	pointer to an LAS DNS context structure
 *
 *				In our usage, this context is derived from
 *				an acllist cache, which is a representation of
 *				of some global acis--in other words it's a global thing
 *				shared between threads--hence the use
 *				of the "read-only" hash lookup routines here.
 */
int
LASDnsMatch(char *token, LASDnsContext_t *context)
{

    /* Test for the unusual case where "*" is allowed */
    if (ACL_HashTableLookup_const(context->Table, "*"))
        return LAS_EVAL_TRUE;

    /*  Start with the full name.  Then strip off each component
     *  leaving the remainder starting with a period.  E.g.
     *    splash.mcom.com
     *    .mcom.com
     *    .com
     *  Search the hash table for each remaining portion.  Remember that
     *  wildcards were put in with the leading period intact.
     */
    do {
	if (ACL_HashTableLookup_const(context->Table, token))
            return LAS_EVAL_TRUE;

        token = strchr(&token[1], '.');
    } while (token != NULL);

    return LAS_EVAL_FALSE;

}

/*  LASDNSBuild
 *  Builds a hash table of all the hostnames provided (plus their aliases
 *  if aliasflg is true).  Wildcards are only permitted in the leftmost
 *  field.  They're represented in the hash table by a leading period.
 *  E.g. ".mcom.com".
 *
 *  RETURNS	Zero on success, else LAS_EVAL_INVALID
 */
int
LASDnsBuild(NSErr_t *errp, char *attr_pattern, LASDnsContext_t *context, int aliasflg)
{
    size_t delimiter; /* length of valid tokeni */
    char token[256];  /* max length dns name */
    int i;
    char **p;
    pool_handle_t *pool;
    PRStatus error=PR_SUCCESS;
    char	buffer[PR_NETDB_BUF_SIZE];
#ifdef	UTEST
    struct hostent *he, host;
#else
    PRHostEnt *he, host;
#endif
    char *end_attr_pattern;

    if (attr_pattern == NULL) {
        nserrGenerate(errp, ACLERRINVAL, ACLERR4770, ACL_Program, 1, 
                      XP_GetAdminStr(DBT_lasdnsbuildInvalidAttributePattern_));
        return LAS_EVAL_INVALID;
    }

    context->Table = PR_NewHashTable(0,
                                     PR_HashCaseString,
                                     PR_CompareCaseStrings,
                                     PR_CompareValues,
                                     &ACLPermAllocOps,
                                     NULL);
    pool = pool_create();
    context->pool = pool;
    if ((!context->Table) || (!context->pool)) {
        nserrGenerate(errp, ACLERRNOMEM, ACLERR4700, ACL_Program, 1, 
                      XP_GetAdminStr(DBT_lasdnsbuildUnableToAllocateHashT_));
        return LAS_EVAL_INVALID;
    }

    end_attr_pattern = attr_pattern + strlen(attr_pattern);
    do {
        size_t maxsize = sizeof(token);
        /*  Get a single hostname from the pattern string        */
        delimiter = strcspn(attr_pattern, ", \t");
        if (delimiter >= maxsize) {
            delimiter = maxsize-1;
        }
        PL_strncpyz(token, attr_pattern, delimiter + 1);
        token[delimiter] = '\0';

        /*  Skip any white space after the token                 */
        attr_pattern += delimiter;
        if (attr_pattern < end_attr_pattern) {
            attr_pattern += strspn(attr_pattern, ", \t");
        }

        /*  If there's a wildcard, strip it off but leave the "."
         *  Can't have aliases for a wildcard pattern.
         *  Treat "*" as a special case.  If so, go ahead and hash it.
         */
        if (token[0] == '*') {
            if (token[1] != '\0') {
                if (!PR_HashTableAdd(context->Table, pool_strdup(pool, &token[1]), (void *)-1)) {
                    nserrGenerate(errp, ACLERRFAIL, ACLERR4710, ACL_Program, 2, 
                                  XP_GetAdminStr(DBT_lasdnsbuildUnableToAddKeySN_), token);
                    return LAS_EVAL_INVALID;
                }
            } else {
                if (!PR_HashTableAdd(context->Table, pool_strdup(pool, token), (void *)-1)) {
                    nserrGenerate(errp, ACLERRFAIL, ACLERR4720, ACL_Program, 2, XP_GetAdminStr(DBT_lasdnsbuildUnableToAddKeySN_), token);
                    return LAS_EVAL_INVALID;
                }
            }
        } else  {
        /*  This is a single hostname add it to the hash table        */
          if (!PR_HashTableAdd(context->Table, pool_strdup(pool, &token[0]), (void *)-1)) {
            nserrGenerate(errp, ACLERRFAIL, ACLERR4730, ACL_Program, 2, XP_GetAdminStr(DBT_lasdnsbuildUnableToAddKeySN_), token);
            return LAS_EVAL_INVALID;
          }

          if (aliasflg) {
            void *iter = NULL;
            int addrcnt = 0;
            PRNetAddr *netaddr = (PRNetAddr *)PERM_CALLOC(sizeof(PRNetAddr));
            PRAddrInfo *infop = PR_GetAddrInfoByName(token,
                            PR_AF_UNSPEC, (PR_AI_ADDRCONFIG|PR_AI_NOCANONNAME));
            if (!netaddr) {
                if (infop) {
                    PR_FreeAddrInfo(infop);
                }
                return LAS_EVAL_NEED_MORE_INFO; /* hostname not known to dns? */
            }
            if (!infop) {
                if (netaddr) {
                    PERM_FREE(netaddr);
                }
                return LAS_EVAL_NEED_MORE_INFO; /* hostname not known to dns? */
            }
            /* need to count the address, first */
            while ((iter = PR_EnumerateAddrInfo(iter, infop, 0, netaddr))) {
                addrcnt++;
            }
            if (0 == addrcnt) {
                PERM_FREE(netaddr);
                PR_FreeAddrInfo(infop);
                return LAS_EVAL_NEED_MORE_INFO; /* hostname not known to dns? */
            }
            iter = NULL; /* from the beginning */
            memset(netaddr, 0, sizeof(PRNetAddr));
            for (i = 0; i < addrcnt; i++) {
                iter = PR_EnumerateAddrInfo( iter, infop, 0, netaddr );
                if (NULL == iter) {
                    break;
                }
                error = PR_GetHostByAddr(netaddr, buffer, 
                                         PR_NETDB_BUF_SIZE, &host);
                if (error == PR_SUCCESS) {
                    he = &host;
                } else {
                    continue;
                }
                if (he->h_name) {
                    /* Add it to the hash table */
                    if (!PR_HashTableAdd(context->Table, 
                                         pool_strdup(pool, he->h_name),
                                         (void *)-1)) {
                        nserrGenerate(errp, ACLERRFAIL, ACLERR4750, 
                               ACL_Program, 2, 
                               XP_GetAdminStr(DBT_lasdnsbuildUnableToAddKeySN_),
                               he->h_name);
                        PERM_FREE(netaddr);
                        PR_FreeAddrInfo(infop);
                        return LAS_EVAL_INVALID;
                    }
                }
                 
                if (he->h_aliases && he->h_aliases[0]) {
                    for (p = he->h_aliases; *p; ++p) {
                        /* Add it to the hash table */
                        if (!PR_HashTableAdd(context->Table,
                                             pool_strdup(pool, *p),
                                             (void *)-1)) {
                            nserrGenerate(errp, ACLERRFAIL, ACLERR4760,
                               ACL_Program, 2,
                               XP_GetAdminStr(DBT_lasdnsbuildUnableToAddKeySN_),
                               *p);
                            PERM_FREE(netaddr);
                            PR_FreeAddrInfo(infop);
                            return LAS_EVAL_INVALID;
                        }
                    }
                }
            } /* for (i = 0; i < addrcnt; i++) */
            PERM_FREE(netaddr);
            PR_FreeAddrInfo(infop);
          } /* if aliasflg */
        } /* else - single hostname */
    } while ((attr_pattern != NULL) && 
             (attr_pattern[0] != '\0') && 
             (delimiter != 0));

    return 0;
}

/*  LASDnsFlush
 *  Given the address of a las_cookie for a DNS expression entry, frees up 
 *  all allocated memory for it.  This includes the hash table, plus the
 *  context structure.
 */
void
LASDnsFlush(void **las_cookie)
{
    if (*las_cookie == NULL)
        return;

    pool_destroy(((LASDnsContext_t *)*las_cookie)->pool);
    PR_HashTableDestroy(((LASDnsContext_t *)*las_cookie)->Table);
    PERM_FREE(*las_cookie);
    *las_cookie = NULL;
    return;
}

/*
 *	LASDnsEval
 *	INPUT
 *	attr_name	The string "dns" - in lower case.
 *	comparator	CMP_OP_EQ or CMP_OP_NE only
 *	attr_pattern	A comma-separated list of DNS names
 *			Any segment(s) in a DNS name can be wildcarded using
 *			"*".  Note that this is not a true Regular Expression
 *			form.
 *	*cachable	Always set to ACL_INDEF_CACHE
 *      subject		Subject property list
 *      resource 	Resource property list
 *      auth_info	Authentication info, if any
 *	RETURNS
 *	ret code	The usual LAS return codes.
 */
int LASDnsEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator,
           char *attr_pattern, ACLCachable_t *cachable, void **LAS_cookie,
           PList_t subject, PList_t resource,
           PList_t auth_info, PList_t global_auth)
{
    int			result;
    int			aliasflg;
    char		*my_dns;
    LASDnsContext_t 	*context = NULL;
    int			rv;

    *cachable = ACL_INDEF_CACHABLE;

    if (strcmp(attr_name, "dns") == 0) {
        /* Enable aliasflg for "dns", which allows "dns" hostname to look up
         * DSN hash table using the primary hostname. */
        aliasflg = 1;
    } else if (strcmp(attr_name, "dnsalias") == 0) {
        aliasflg = 1;
    } else {
        nserrGenerate(errp, ACLERRINVAL, ACLERR4800, ACL_Program, 2, XP_GetAdminStr(DBT_lasDnsBuildReceivedRequestForAtt_), attr_name);
        return LAS_EVAL_INVALID;
    }

    if ((comparator != CMP_OP_EQ) && (comparator != CMP_OP_NE)) {
        nserrGenerate(errp, ACLERRINVAL, ACLERR4810, ACL_Program, 2, XP_GetAdminStr(DBT_lasdnsevalIllegalComparatorDN_), comparator_string(comparator));
        return LAS_EVAL_INVALID;
    }

    /* If this is the first time through, build the pattern tree first.  */
    if (*LAS_cookie == NULL) {
        ACL_CritEnter();
        if (*LAS_cookie == NULL) {	/* Must check again */
            *LAS_cookie = context = 
                (LASDnsContext_t *)PERM_MALLOC(sizeof(LASDnsContext_t));
            if (context == NULL) {
                nserrGenerate(errp, ACLERRNOMEM, ACLERR4820, ACL_Program, 1, XP_GetAdminStr(DBT_lasdnsevalUnableToAllocateContex_));
                ACL_CritExit();
                return LAS_EVAL_FAIL;
            }
            context->Table = NULL;
            if (LASDnsBuild(errp, attr_pattern, context, aliasflg) ==
                                                            LAS_EVAL_INVALID) {
                /* Error is already printed in LASDnsBuild */
                ACL_CritExit();
                return LAS_EVAL_FAIL;
            }
            /* After this line, it is assured context->Table is not NULL. */
        } else {
            context = (LASDnsContext *) *LAS_cookie;
        }
        ACL_CritExit();
    } else {
        ACL_CritEnter();
        context = (LASDnsContext *) *LAS_cookie;
        ACL_CritExit();
    }

    /* Call the DNS attribute getter */
#ifdef  UTEST
    LASDnsGetDns(&my_dns);      /* gets stuffed on return       */
#else
    rv = ACL_GetAttribute(errp, ACL_ATTR_DNS, (void **)&my_dns,
			  subject, resource, auth_info, global_auth);

    if (rv != LAS_EVAL_TRUE) {
        if (subject || resource) {	
	    char rv_str[16];
            /* Don't ereport if called from ACL_CachableAclList */
	    sprintf(rv_str, "%d", rv);
	    nserrGenerate(errp, ACLERRINVAL, ACLERR4830, ACL_Program, 2, XP_GetAdminStr(DBT_lasdnsevalUnableToGetDnsErrorDN_), rv_str);
        }
	return LAS_EVAL_FAIL;
    }
#endif

    result = LASDnsMatch(my_dns, context);

    if (comparator == CMP_OP_NE) {
    	if (result == LAS_EVAL_FALSE)
            return LAS_EVAL_TRUE;
        else if (result == LAS_EVAL_TRUE)
            return LAS_EVAL_FALSE;
    }
    return (result);
}

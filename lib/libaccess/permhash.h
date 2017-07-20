/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef _PERMHASH_H_
#define _PERMHASH_H_

#include <string.h>
#include <plhash.h>
#include <base/pool.h>
#include <base/util.h>

static void *
ACL_PermAllocTable(void *pool, PRSize size)
{
    return pool_malloc((pool_handle_t *)pool, size);
}

static void
ACL_PermFreeTable(void *pool, void *item)
{
    pool_free((pool_handle_t *)pool, item);
}

static PLHashEntry *
ACL_PermAllocEntry(void *pool, const void *unused)
{
    return ((PLHashEntry *)pool_malloc((pool_handle_t *)pool, sizeof(PLHashEntry)));
}

static void
ACL_PermFreeEntry(void *pool, PLHashEntry *he, PRUintn flag)
{
    if (flag == HT_FREE_ENTRY) {
        pool_free((pool_handle_t *)pool, he);
    }
}

static PLHashAllocOps ACLPermAllocOps = {
    ACL_PermAllocTable,
    ACL_PermFreeTable,
    ACL_PermAllocEntry,
    ACL_PermFreeEntry};

#ifndef NO_ACL_HASH_FUNCS
static PLHashNumber
PR_HashCaseString(const void *key)
{
    PLHashNumber h;
    const unsigned char *s;

    h = 0;
    for (s = (const unsigned char *)key; *s; s++)
        h = (h >> 28) ^ (h << 4) ^ tolower(*s);
    return h;
}

static int
PR_CompareCaseStrings(const void *v1, const void *v2)
{
    const char *s1 = (const char *)v1;
    const char *s2 = (const char *)v2;

    return (strcasecmp(s1, s2) == 0);
}
#endif /* NO_ACL_HASH_FUNCS */


#endif /* _PERMHASH_H */

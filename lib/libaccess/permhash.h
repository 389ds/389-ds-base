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

#ifndef	_PERMHASH_H_
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
    if (flag == HT_FREE_ENTRY){
        pool_free((pool_handle_t *)pool, he);
    }
}

static PLHashAllocOps ACLPermAllocOps = {
    ACL_PermAllocTable,
    ACL_PermFreeTable,
    ACL_PermAllocEntry,
    ACL_PermFreeEntry
};

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

#ifdef XP_WIN32
    return (util_strcasecmp(s1, s2) == 0);
#else
    return (strcasecmp(s1, s2) == 0);
#endif
}
#endif /* NO_ACL_HASH_FUNCS */


#endif	/* _PERMHASH_H */

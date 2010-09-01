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
 * Source file for the ACL_CacheFlush-related routines.
 */

#include	<prlog.h>
#include	<base/util.h>
#include	<libaccess/acl.h>
#include	"aclpriv.h"
#include	<libaccess/aclproto.h>
#include	<libaccess/aclglobal.h>
#include	<libaccess/las.h>
#include	"aclcache.h"

extern void ACL_DatabaseDestroy(void);

PRIntn
deletelists(PRHashEntry *he, PRIntn i, void *arg)
{
    ACLListHandle_t *acllist=(ACLListHandle_t *)he->value;
    NSErr_t *errp = 0;

    PR_ASSERT(he);
    PR_ASSERT(he->value);

    if (acllist->ref_count) {
	//  If the list is in use, increment the counter.  Then set the 
	//  stale flag.  The other user can't delete the list since we're
	//  counted as well.  Finally, decrement the counter and whoever
	//  sets it to zero will delete the ACL List.
	PR_ASSERT(ACL_CritHeld());
	acllist->ref_count++;
	acllist->flags |= ACL_LIST_STALE;
	if (--acllist->ref_count == 0)
	    ACL_ListDestroy(errp, acllist);
    } else {
	ACL_ListDestroy(errp, acllist);
    }

    return 0;
}

PRIntn
restartdeletelists(PRHashEntry *he, PRIntn i, void *arg)
{
    NSErr_t *errp = 0;

    //  Cannot be anyone left using the lists, so just free them no matter
    //  what.
    ACLListHandle_t *acllist=(ACLListHandle_t *)he->value;

    PR_ASSERT(he);
    PR_ASSERT(he->value);

    ACL_ListDestroy(errp, acllist);

    return 0;
}

static AclCacheFlushFunc_t AclCacheFlushRoutine = NULL;

NSAPI_PUBLIC int
ACL_CacheFlushRegister(AclCacheFlushFunc_t flush_func)
{
    PR_ASSERT(flush_func);
    AclCacheFlushRoutine = flush_func;

    return 0;
}

NSAPI_PUBLIC int
ACL_CacheFlush(void)
{
    ACLGlobal_p	newACLGlobal;
    NSErr_t *errp = 0;

    PR_ASSERT(ACLGlobal);
    PR_ASSERT(ACLGlobal->masterlist);
    PR_ASSERT(ACLGlobal->listhash);
    PR_ASSERT(ACLGlobal->urihash);
    PR_ASSERT(ACLGlobal->urigethash);
    PR_ASSERT(ACLGlobal->pool);

    ACL_CritEnter();

    //	Swap the pointers.  Keep using the current database/method tables
    //  until the new ones are built.  This is a kludge.  An in-progress
    //  evaluation could conceivably get messed up, but the window seems
    //  small.
    newACLGlobal = oldACLGlobal;

    oldACLGlobal = ACLGlobal;
    ACLGlobal = newACLGlobal;

    //  Prepare the new ACLGlobal structure
    ACL_UriHashInit();	/* Also initializes ACLGlobal->pool */
    ACL_ListHashInit();
    ACLGlobal->evalhash = oldACLGlobal->evalhash;
    ACLGlobal->flushhash = oldACLGlobal->flushhash;
    ACLGlobal->methodhash = oldACLGlobal->methodhash;
    ACLGlobal->dbtypehash = oldACLGlobal->dbtypehash;
    ACLGlobal->dbnamehash = oldACLGlobal->dbnamehash;
    ACLGlobal->attrgetterhash = oldACLGlobal->attrgetterhash;
    ACLGlobal->databasepool = oldACLGlobal->databasepool;
    ACLGlobal->methodpool = oldACLGlobal->methodpool;

    //	Mark all existing ACL Lists as stale. Delete any unreferenced ones.
    PR_HashTableEnumerateEntries(oldACLGlobal->listhash, deletelists, NULL);

    //  Delete the old master list.
    ACL_ListDestroy(errp, oldACLGlobal->masterlist);
    oldACLGlobal->masterlist = NULL;
    PR_HashTableDestroy(oldACLGlobal->listhash);
    oldACLGlobal->listhash = NULL;
    PR_HashTableDestroy(oldACLGlobal->urihash);
    oldACLGlobal->urihash = NULL;
    PR_HashTableDestroy(oldACLGlobal->urigethash);
    oldACLGlobal->urigethash = NULL;
    pool_destroy(oldACLGlobal->pool);
    oldACLGlobal->pool = NULL;
    memset(oldACLGlobal, 0, sizeof(ACLGlobal_s));


    //  Read in the ACLs again in lib/frame
    if (AclCacheFlushRoutine) {
        (*AclCacheFlushRoutine)();
    }

    ACL_CritExit();

    return 0;
}


NSAPI_PUBLIC void 
ACL_Restart(void *clntData)
{
    NSErr_t *errp = 0;

    PR_ASSERT(ACLGlobal);
    PR_ASSERT(ACLGlobal->masterlist);
    PR_ASSERT(ACLGlobal->listhash);
    PR_ASSERT(ACLGlobal->urihash);
    PR_ASSERT(ACLGlobal->urigethash);
    PR_ASSERT(ACLGlobal->pool);

    //	Unlike ACL_CacheFlush, this routine can be much more cavalier about
    //	freeing up memory, since there's guaranteed to be no users about at
    //	this time.

    ACL_DatabaseDestroy();
    ACL_MethodSetDefault(errp, ACL_METHOD_INVALID);

    //	Mark all existing ACL Lists as stale. Delete any unreferenced ones
    //  (i.e. all of them)
    PR_HashTableEnumerateEntries(ACLGlobal->listhash, restartdeletelists, NULL);

    //  Delete the  master list.
    ACL_ListDestroy(errp, ACLGlobal->masterlist);

    ACL_LasHashDestroy();
    PR_HashTableDestroy(ACLGlobal->listhash);
    PR_HashTableDestroy(ACLGlobal->urihash);
    PR_HashTableDestroy(ACLGlobal->urigethash);
    pool_destroy(ACLGlobal->pool);

    PERM_FREE(ACLGlobal);
    ACLGlobal = NULL;
    PERM_FREE(oldACLGlobal);
    oldACLGlobal = NULL;

    return;
}

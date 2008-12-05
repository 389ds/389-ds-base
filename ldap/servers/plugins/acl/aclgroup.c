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

#include "acl.h"

/***************************************************************************
 * 
 * This module deals with the global user group cache.
 *
 * A LRU queue mechanism is used to maintain the groups the user currently in.
 * At this moment the QUEUE is invalidated if there is a group change. A better
 * way would have been to invalidate only the one which are effected.
 * However to accomplish that will require quite a bit of work which may not be
 * cost-efftive.
 **************************************************************************/
static aclGroupCache   *aclUserGroups;
#define ACL_MAXCACHE_USERGROUPS 200

#define ACLG_LOCK_GROUPCACHE_READ()      PR_RWLock_Rlock ( aclUserGroups->aclg_rwlock )
#define ACLG_LOCK_GROUPCACHE_WRITE()     PR_RWLock_Wlock ( aclUserGroups->aclg_rwlock )
#define ACLG_ULOCK_GROUPCACHE_WRITE()    PR_RWLock_Unlock ( aclUserGroups->aclg_rwlock )
#define ACLG_ULOCK_GROUPCACHE_READ()     PR_RWLock_Unlock ( aclUserGroups->aclg_rwlock )


static void		__aclg__delete_userGroup ( aclUserGroup *u_group );


int 
aclgroup_init ()
{

	aclUserGroups = ( aclGroupCache * ) slapi_ch_calloc (1, sizeof ( aclGroupCache ) );
	if ( NULL ==  (aclUserGroups->aclg_rwlock = PR_NewRWLock( PR_RWLOCK_RANK_NONE,"Group LOCK"))) {
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name, "Unable to allocate RWLOCK for group cache\n");
		return 1;
	}
	return 0;
}

/*
 *  aclg_init_userGroup
 *
 * Go thru the Global Group CACHE and see if we have group information for 
 * the user.  The user's group cache is invalidated when a group is modified
 * (in which case ALL usergroups are invalidated) or when the user's entry
 * is modified in which case just his is invalidated.
 *
 * We need to scan the whole cache looking for a valid entry that matches
 * this user.  If we find invalid entries along the way.
 *
 *	If we don't have anything it's fine. we will allocate a space when we
 * 	need it i.e during  the group evaluation.
 *
 * 	Inputs:
 *		struct acl_pblock		- ACL private block
 *		char *dn			- the client's dn
 *		int got_lock			- 1: already obtained WRITE Lock
 *						- 0: Nope; get one 
 *	Returns:
 *		None.
 */

void
aclg_init_userGroup ( struct acl_pblock *aclpb, const char *n_dn , int got_lock )
{
	aclUserGroup		*u_group = NULL;
	aclUserGroup		*next_ugroup = NULL;
	aclUserGroup		*p_group, *n_group;	
	int found = 0;
		
	/* Check for Anonymous  user */
	if ( n_dn && *n_dn == '\0') return;

	if ( !got_lock ) ACLG_LOCK_GROUPCACHE_WRITE ();
	u_group = aclUserGroups->aclg_first;
	aclpb->aclpb_groupinfo = NULL;

	while ( u_group != NULL ) {
		next_ugroup = u_group->aclug_next;
		if ( aclUserGroups->aclg_signature != u_group->aclug_signature) {
			/*
			 * This means that this usergroup is no longer valid and
			 * this operation so delete this one if no one is using it.
			*/
			
			if ( !u_group->aclug_refcnt ) {
				slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
					"In traversal group deallocation\n" );
				__aclg__delete_userGroup (u_group);								
			}			
		} else {

			/*
			 * Here, u_group is valid--if it matches then take it.
			*/
			if ( slapi_utf8casecmp((ACLUCHP)u_group->aclug_ndn, 
										(ACLUCHP)n_dn ) == 0 ) {
					u_group->aclug_refcnt++;
					aclpb->aclpb_groupinfo = u_group;
					found = 1;
					break;
			}
		}
		u_group = next_ugroup;
	}
	
	/* Move the new one to the top of the queue */
	if ( found )  {
		p_group  = u_group->aclug_prev;
		n_group = u_group->aclug_next;

		if ( p_group )  {
			aclUserGroup	*t_group = NULL;

			p_group->aclug_next = n_group;
			if ( n_group ) n_group->aclug_prev = p_group;

			t_group = aclUserGroups->aclg_first;
			if ( t_group ) t_group->aclug_prev = u_group;

			u_group->aclug_prev = NULL;
			u_group->aclug_next = t_group;
			aclUserGroups->aclg_first = u_group;

			if ( u_group == aclUserGroups->aclg_last )
				aclUserGroups->aclg_last = p_group;
		}
		slapi_log_error(SLAPI_LOG_ACL, plugin_name, "acl_init_userGroup: found in cache for dn:%s\n", n_dn);
	}
	if (!got_lock ) ACLG_ULOCK_GROUPCACHE_WRITE ();
}


/*
 *
 * aclg_reset_userGroup
 *	Reset the reference count to the user's group.
 *
 *	Inputs:
 *		struct	acl_pblock		-- The acl private block.
 *	Returns:
 *		None.
 *
 *	Note: A WRITE Lock on the GroupCache is obtained during the change:
 */
void
aclg_reset_userGroup ( struct acl_pblock *aclpb )
{

	aclUserGroup	*u_group;

	ACLG_LOCK_GROUPCACHE_WRITE();

	if ( (u_group = aclpb->aclpb_groupinfo) != NULL ) {
		u_group->aclug_refcnt--;

		/* If I am the last one but I was using an invalid group cache
		** in the meantime, it is time now to get rid of it so that we will
		** not have duplicate cache.
		*/
		if ( !u_group->aclug_refcnt && 
			( aclUserGroups->aclg_signature != u_group->aclug_signature )) {
			__aclg__delete_userGroup ( u_group );
		}
	}
	ACLG_ULOCK_GROUPCACHE_WRITE();
	aclpb->aclpb_groupinfo = NULL;
}

/*
 * Find a user group in the global cache, returning a pointer to it,
 * ensuring that the refcnt has been bumped to stop
 * another thread freeing it underneath us.
*/

aclUserGroup*
aclg_find_userGroup(char *n_dn)
{
	aclUserGroup		*u_group = NULL;	
	int			i;

	/* Check for Anonymous  user */
	if ( n_dn && *n_dn == '\0') return (NULL) ;

	ACLG_LOCK_GROUPCACHE_READ ();
		u_group = aclUserGroups->aclg_first;
	
		for ( i=0; i < aclUserGroups->aclg_num_userGroups; i++ ) {
			if ( aclUserGroups->aclg_signature == u_group->aclug_signature &&
							slapi_utf8casecmp((ACLUCHP)u_group->aclug_ndn, 
													(ACLUCHP)n_dn ) == 0 ) {					
				aclg_reader_incr_ugroup_refcnt(u_group);
				break;
			}
			u_group = u_group->aclug_next;
		}
	
	ACLG_ULOCK_GROUPCACHE_READ ();
	return(u_group);
}

/*
 * Mark a usergroup for removal from the usergroup cache.
 * It will be removed by the first operation traversing the cache
 * that finds it.
*/
void
aclg_markUgroupForRemoval ( aclUserGroup* u_group) {		

	ACLG_LOCK_GROUPCACHE_WRITE ();					
		aclg_regen_ugroup_signature(u_group);
		u_group->aclug_refcnt--;
	ACLG_ULOCK_GROUPCACHE_WRITE ();	
}

/*
 *
 * aclg_get_usersGroup
 *
 *	If we already have a the group info then we are done. If we
 *	don't, then allocate a new one and attach it.
 *
 *	Inputs:
 *		struct	acl_pblock		-- The acl private block.
 *		char *n_dn			- normalized client's DN
 *
 *	Returns:
 *		aclUserGroup			- The Group info block.
 *
 */
aclUserGroup *
aclg_get_usersGroup ( struct acl_pblock *aclpb , char *n_dn) 
{

	aclUserGroup		*u_group, *f_group;

	if ( aclpb && aclpb->aclpb_groupinfo )
		return aclpb->aclpb_groupinfo;

	ACLG_LOCK_GROUPCACHE_WRITE();

	/* try it one more time. We might have one in the meantime */
	aclg_init_userGroup  (aclpb, n_dn , 1 /* got the lock */);
	if ( aclpb && aclpb->aclpb_groupinfo ) {
		ACLG_ULOCK_GROUPCACHE_WRITE();
		return aclpb->aclpb_groupinfo;
	}

	/*
	 * It is possible at this point that we already have a group cache for the user
	 * but is is invalid. We can't use it anayway. So, we march along and allocate a new one.
	 * That's fine as the invalid one will be deallocated when done.
	 */

	slapi_log_error( SLAPI_LOG_ACL, plugin_name, "ALLOCATING GROUP FOR:%s\n", n_dn );
	u_group = ( aclUserGroup * ) slapi_ch_calloc ( 1, sizeof ( aclUserGroup ) );
	
	u_group->aclug_refcnt = 1;
	if ( (u_group->aclug_refcnt_mutex = PR_NewLock()) == NULL ) {
		slapi_ch_free((void **)&u_group);
		ACLG_ULOCK_GROUPCACHE_WRITE();
		return(NULL);
	}

	u_group->aclug_member_groups = (char **)
					slapi_ch_calloc ( 1, 
					    (ACLUG_INCR_GROUPS_LIST * sizeof (char *)));
	u_group->aclug_member_group_size = ACLUG_INCR_GROUPS_LIST;
	u_group->aclug_numof_member_group = 0;

	u_group->aclug_notmember_groups = (char **)
					slapi_ch_calloc ( 1,
					   (ACLUG_INCR_GROUPS_LIST * sizeof (char *)));
	u_group->aclug_notmember_group_size = ACLUG_INCR_GROUPS_LIST;
	u_group->aclug_numof_notmember_group = 0;

	u_group->aclug_ndn = slapi_ch_strdup ( n_dn ) ;	
	
	u_group->aclug_signature = aclUserGroups->aclg_signature;

	/* Do we have alreday the max number. If we have then delete the last one */
	if ( aclUserGroups->aclg_num_userGroups >= ACL_MAXCACHE_USERGROUPS - 5 ) {
		aclUserGroup		*d_group;
		
		/* We need to traverse thru  backwards and delete the one with a refcnt = 0 */
		d_group = aclUserGroups->aclg_last;
		while ( d_group ) {
			if ( !d_group->aclug_refcnt ) {
				__aclg__delete_userGroup ( d_group );
				break;
			} else {
				d_group = d_group->aclug_prev;
			}
		}

		/* If we didn't find any, which should be never, 
		** we have 5 more tries to do it.
		*/
	} 
	f_group = aclUserGroups->aclg_first;
	u_group->aclug_next = f_group;
	if ( f_group ) f_group->aclug_prev = u_group;
		
	aclUserGroups->aclg_first =  u_group;
	if ( aclUserGroups->aclg_last == NULL )
		aclUserGroups->aclg_last = u_group;

	aclUserGroups->aclg_num_userGroups++;

	/* Put it in the queue */
	ACLG_ULOCK_GROUPCACHE_WRITE();

	/* Now hang on to it */
	aclpb->aclpb_groupinfo = u_group;
	return u_group;
}

/*
 * 
 * __aclg__delete_userGroup
 *
 *	Delete the User's Group cache.
 *
 *	Inputs:
 * 		aclUserGroup		- remove this one
 *	Returns:
 *		None.
 *
 *	Note: A WRITE Lock on the GroupCache is obtained by the caller
 */ 
static void
__aclg__delete_userGroup ( aclUserGroup *u_group )
{

	aclUserGroup		*next_group, *prev_group;
	int			i;

	if ( !u_group ) return;

	prev_group = u_group->aclug_prev;
	next_group = u_group->aclug_next;

	/*
	 * At this point we must have a 0 refcnt or else we are in a bad shape.
	 * If we don't have one then at least remove the user's dn so that it will
	 * be in a condemned state and later deleted.
	 */
	
	slapi_log_error( SLAPI_LOG_ACL, plugin_name, "DEALLOCATING GROUP FOR:%s\n", u_group->aclug_ndn );

	slapi_ch_free ( (void **) &u_group->aclug_ndn );

	PR_DestroyLock(u_group->aclug_refcnt_mutex);

	/* Remove the member GROUPS */
	for (i=0; i < u_group->aclug_numof_member_group; i++ )
		slapi_ch_free ( (void **) &u_group->aclug_member_groups[i] );
	slapi_ch_free ( (void **) &u_group->aclug_member_groups );

	/* Remove the NOT member GROUPS */
	for (i=0; i < u_group->aclug_numof_notmember_group; i++ )
		slapi_ch_free ( (void **) &u_group->aclug_notmember_groups[i] );
	slapi_ch_free ( (void **) &u_group->aclug_notmember_groups );

	slapi_ch_free ( (void **) &u_group );

	if ( prev_group == NULL && next_group == NULL ) {
		aclUserGroups->aclg_first = NULL;
		aclUserGroups->aclg_last = NULL;
	} else if ( prev_group == NULL ) {
		next_group->aclug_prev = NULL;
		aclUserGroups->aclg_first = next_group;
	} else {
		prev_group->aclug_next = next_group;
		if ( next_group ) 
			next_group->aclug_prev = prev_group;
		else 
			aclUserGroups->aclg_last = prev_group;
	}
	aclUserGroups->aclg_num_userGroups--;
}

void
aclg_regen_group_signature( )
{
	aclUserGroups->aclg_signature = aclutil_gen_signature ( aclUserGroups->aclg_signature );
}

void
aclg_regen_ugroup_signature( aclUserGroup *ugroup)
{
	ugroup->aclug_signature =
		aclutil_gen_signature ( ugroup->aclug_signature );
}

void 
aclg_lock_groupCache ( int type /* 1 for reader and 2 for writer */)
{

	if (type == 1 )
		ACLG_LOCK_GROUPCACHE_READ();
	else
		ACLG_LOCK_GROUPCACHE_WRITE();
}

void 
aclg_unlock_groupCache ( int type /* 1 for reader and 2 for writer */)
{

	if (type == 1 )
		ACLG_ULOCK_GROUPCACHE_READ();
	else
		ACLG_ULOCK_GROUPCACHE_WRITE();
}


/*
 * If you have the write lock on the group cache, you can
 * increment the refcnt without taking the mutex.
 * If you just have the reader lock on the refcnt then you need to
 * take the mutex on the refcnt to increment it--which is what this routine is
 * for.
 *
*/

void
aclg_reader_incr_ugroup_refcnt(aclUserGroup* u_group) {
	
	PR_Lock(u_group->aclug_refcnt_mutex);
		u_group->aclug_refcnt++;
	PR_Unlock(u_group->aclug_refcnt_mutex);
}

/* You need the usergroups read lock to call this routine*/
int
aclg_numof_usergroups(void) {
	
	return(aclUserGroups->aclg_num_userGroups);
}


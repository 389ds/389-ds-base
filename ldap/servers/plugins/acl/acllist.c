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


/************************************************************************
 *
 * ACLLIST
 *
 * All the ACLs are read when the server is started. The ACLs are 
 * parsed and kept in an AVL tree. All the ACL List management are
 * in this file.
 *
 * The locking on the aci cache is implemented using the acllist_acicache*()
 * routines--a read/write lock.
 *
 * The granularity of the view of the cache is the entry level--ie.
 * when an entry is being modified (mod,add,delete,modrdn) and the mod
 * involves the aci attribute, then other operations will see the acl cache
 * before the whole change or after the whole change, but not during the change.
 * cf. acl.c:acl_modified()
 *
 * The only tricky issue is that there is also locking
 * implemented for the anonymous profile and sometimes we need to take both
 * locks cf. aclanom_anom_genProfile().  The rule is
 * always take the acicache lock first, followed by the anon lock--following
 * this rule will ensure no dead lock scenarios can arise.
 *
 * Some routines are called in different places with different lock
 * contexts--for these routines acl_lock_flag_t is used to
 * pass the context.
 * 
 */
#include "acl.h"

static Slapi_RWLock *aci_rwlock = NULL;
#define ACILIST_LOCK_READ()     slapi_rwlock_rdlock  (aci_rwlock )
#define ACILIST_UNLOCK_READ()   slapi_rwlock_unlock (aci_rwlock )
#define ACILIST_LOCK_WRITE()    slapi_rwlock_wrlock  (aci_rwlock )
#define ACILIST_UNLOCK_WRITE()  slapi_rwlock_unlock (aci_rwlock )


/* Root of the TREE */
static Avlnode *acllistRoot = NULL;

#define CONTAINER_INCR 2000 

/* The container array */
static AciContainer		**aciContainerArray;
static PRUint32 		currContainerIndex =0;
static PRUint32			maxContainerIndex = 0;	
static int				curAciIndex = 1;

/* PROTOTYPES */
static int		__acllist_add_aci ( aci_t *aci );
static int		__acllist_aciContainer_node_cmp ( caddr_t d1, caddr_t d2 );
static int		__acllist_aciContainer_node_dup ( caddr_t d1, caddr_t d2 );
static void 	__acllist_free_aciContainer (  AciContainer **container);

void my_print( Avlnode	*root );

int
acllist_init ()
{

	if (( aci_rwlock = slapi_new_rwlock() ) == NULL ) {
		slapi_log_error( SLAPI_LOG_FATAL, plugin_name, 
							"acllist_init:failed in getting the rwlock\n" );
		return 1;
	}
	
	aciContainerArray =  (AciContainer **) slapi_ch_calloc ( 1, 
										CONTAINER_INCR * sizeof ( AciContainer * ) );
	maxContainerIndex = CONTAINER_INCR;
	currContainerIndex = 0;

	return 0;
}

/*
 * This is the callback for backend state changes.
 * It needs to add/remove acis as backends come up and go down.
 *
 * The strategy is simple:
 * When a backend moves to the SLAPI_BE_STATE_ON then we go get all the acis
 * add them to the cache.
 * When a backend moves out of the SLAPI_BE_STATE_ON then we remove them all.
 * 
*/

void acl_be_state_change_fnc ( void *handle, char *be_name, int old_state,
															int new_state) {
	Slapi_Backend *be=NULL;
	const Slapi_DN *sdn;


	if ( old_state == SLAPI_BE_STATE_ON && new_state != SLAPI_BE_STATE_ON) {

		slapi_log_error ( SLAPI_LOG_ACL, plugin_name, 
			"Backend %s is no longer STARTED--deactivating it's acis\n",
			be_name);
		
		if ( (be = slapi_be_select_by_instance_name( be_name )) == NULL) {
			slapi_log_error ( SLAPI_LOG_ACL, plugin_name, 
			"Failed to retreive backend--NOT activating it's acis\n");
			return;
		}

		/* 
		 * Just get the first suffix--if there are multiple XXX ?
		*/

		if ( (sdn = slapi_be_getsuffix( be, 0)) == NULL ) {
			slapi_log_error ( SLAPI_LOG_ACL, plugin_name, 
			"Failed to retreive backend--NOT activating it's acis\n");
			return;
		}

		aclinit_search_and_update_aci ( 1,		/* thisbeonly */
										sdn,	/* base */
										be_name,/* be name */
										LDAP_SCOPE_SUBTREE,
										ACL_REMOVE_ACIS,
										DO_TAKE_ACLCACHE_WRITELOCK);
		
	} else if ( old_state != SLAPI_BE_STATE_ON && new_state == SLAPI_BE_STATE_ON) {
		slapi_log_error ( SLAPI_LOG_ACL, plugin_name, 
			"Backend %s is now STARTED--activating it's acis\n", be_name);

		if ( (be = slapi_be_select_by_instance_name( be_name )) == NULL) {
			slapi_log_error ( SLAPI_LOG_ACL, plugin_name, 
			"Failed to retreive backend--NOT activating it's acis\n");
			return;
		}

		/* 
		 * In fact there can onlt be one sufffix here.
		*/

		if ( (sdn = slapi_be_getsuffix( be, 0)) == NULL ) {
			slapi_log_error ( SLAPI_LOG_ACL, plugin_name, 
			"Failed to retreive backend--NOT activating it's acis\n");
			return;
		}
																	
		aclinit_search_and_update_aci ( 1,	/* thisbeonly */
										sdn,									
										be_name,	/* be name  */
										LDAP_SCOPE_SUBTREE,
										ACL_ADD_ACIS,
										DO_TAKE_ACLCACHE_WRITELOCK);
	}

}

/* This routine must be called with the acicache write lock taken */
int
acllist_insert_aci_needsLock( const Slapi_DN *e_sdn, const struct berval* aci_attr)
{
	return(acllist_insert_aci_needsLock_ext(NULL, e_sdn, aci_attr));
}

int
acllist_insert_aci_needsLock_ext( Slapi_PBlock *pb, const Slapi_DN *e_sdn, const struct berval* aci_attr)
{

	aci_t			*aci;
	char			*acl_str;
	int				rv =0;

	if (aci_attr->bv_len <= 0) 
		return  0;

	aci = acllist_get_aci_new ();
	slapi_sdn_set_ndn_byval ( aci->aci_sdn, slapi_sdn_get_ndn ( e_sdn ) );

	acl_str = slapi_ch_strdup(aci_attr->bv_val);
	/* Parse the ACL TEXT */
	if (  0 != (rv = acl_parse ( pb, acl_str, aci, NULL )) ) {
		slapi_log_error (SLAPI_LOG_FATAL, plugin_name,
				"ACL PARSE ERR(rv=%d): %s\n", rv, acl_str );
		slapi_ch_free ( (void **) &acl_str );
		acllist_free_aci ( aci );
		
		return 1;
	}

	/* Now add it to the list */
	if ( 0 != (rv =__acllist_add_aci ( aci ))) {
		slapi_log_error (SLAPI_LOG_ACL, plugin_name,
				"ACL ADD ACI ERR(rv=%d): %s\n", rv, acl_str );
		slapi_ch_free ( (void **) &acl_str );
		acllist_free_aci ( aci );
		return 1;
	}

	slapi_ch_free ( (void **) &acl_str );
	acl_regen_aclsignature ();
	if ( aci->aci_elevel == ACI_ELEVEL_USERDN_ANYONE)
		aclanom_invalidateProfile ();
	return 0;
}

/* This routine must be called with the acicache write lock taken */
static int
__acllist_add_aci ( aci_t *aci )
{

	int					rv = 0; /* OK */
	AciContainer		*aciListHead;
	AciContainer		*head;
	PRUint32			i;

	aciListHead = 	acllist_get_aciContainer_new ( );
	slapi_sdn_set_ndn_byval ( aciListHead->acic_sdn, slapi_sdn_get_ndn ( aci->aci_sdn ) );
	
	/* insert the aci */
	switch (avl_insert ( &acllistRoot, aciListHead, __acllist_aciContainer_node_cmp, 
								__acllist_aciContainer_node_dup ) ) {
		
	case 1:		/* duplicate ACL on the same entry */

		/* Find the node that contains the acl. */
		if ( NULL == (head = (AciContainer *) avl_find( acllistRoot, aciListHead, 
										(IFP) __acllist_aciContainer_node_cmp ) ) ) {
			slapi_log_error ( SLAPI_PLUGIN_ACL, plugin_name,
								"Can't insert the acl in the tree\n");
			rv = 1;
		} else {
			aci_t		*t_aci;;

			/* Attach the list */	
			t_aci = head->acic_list;;
			while ( t_aci && t_aci->aci_next ) 
				t_aci = t_aci->aci_next;
			
			/* Now add the new one to the end of the list */
			t_aci->aci_next = aci;

			slapi_log_error ( SLAPI_LOG_ACL, plugin_name, "Added the ACL:%s to existing container:[%d]%s\n", 
					aci->aclName, head->acic_index, slapi_sdn_get_ndn( head->acic_sdn ));
		}

		/* now free the tmp container */
		aciListHead->acic_list = NULL;
		__acllist_free_aciContainer ( &aciListHead );

		break;
	default:
		/*  The container is inserted. Now hook up the aci and setup the 
		 * container index. Donot free the "aciListHead" here.
		 */
		aciListHead->acic_list = aci;

		/* 
		 * First, see if we have an open slot or not - -if we have reuse it
		 */
		i = 0;
		while ( (i < currContainerIndex) && aciContainerArray[i] )
			i++;

		if ( currContainerIndex >=  (maxContainerIndex - 2)) {
			maxContainerIndex += CONTAINER_INCR;
			aciContainerArray =  (AciContainer **) slapi_ch_realloc ( (char *) aciContainerArray, 
														maxContainerIndex * sizeof ( AciContainer * ) );
		}
		aciListHead->acic_index = i;
		/* If i < currContainerIndex, we are just re-using an old slot.               */
		/* We don't need to increase currContainerIndex if we just re-use an old one. */
		if (i == currContainerIndex)
			currContainerIndex++;

		aciContainerArray[ aciListHead->acic_index ] = aciListHead;

		slapi_log_error ( SLAPI_LOG_ACL, plugin_name, "Added %s to container:%d\n", 
								slapi_sdn_get_ndn( aciListHead->acic_sdn ), aciListHead->acic_index );
		break;
	}

	return rv;
}



static int
__acllist_aciContainer_node_cmp ( caddr_t d1, caddr_t d2 )
{

	int				rc =0;
	AciContainer	*c1 =  (AciContainer *) d1;
	AciContainer	*c2 =  (AciContainer *) d2;


	rc = slapi_sdn_compare ( c1->acic_sdn, c2->acic_sdn );
	return rc;
}

static int
__acllist_aciContainer_node_dup ( caddr_t d1, caddr_t d2 )
{

	/* we allow duplicates  -- they are not exactly duplicates
	** but multiple aci value on the same node
	*/
	return 1;

}


/*
 * Remove the ACL
 *
 * This routine must be called with the aclcache write lock taken.
 * It takes in addition the one for the anom profile taken in
 * aclanom_invalidateProfile().
 * They _must_ be taken in this order or there
 * is a deadlock scenario with aclanom_gen_anomProfile() which
 * also takes them is this order.
*/

int
acllist_remove_aci_needsLock( const Slapi_DN *sdn,  const struct berval *attr )
{

	aci_t			*head, *next;
	int				rv = 0;
	AciContainer	*aciListHead, *root;
	AciContainer	*dContainer;
	int				removed_anom_acl = 0;

	/* we used to delete the ACL by value but we don't do that anymore.
	 * rather we delete all the acls in that entry and then repopulate it if 
	 * there are any more acls.
	 */

	aciListHead = 	acllist_get_aciContainer_new ( );
	slapi_sdn_set_ndn_byval ( aciListHead->acic_sdn, slapi_sdn_get_ndn ( sdn ) );

	/* now find it */
	if ( NULL == (root = (AciContainer *) avl_find( acllistRoot, aciListHead, 
										(IFP) __acllist_aciContainer_node_cmp ))) {
		/* In that case we don't have any acl for this entry. cool !!! */

		__acllist_free_aciContainer ( &aciListHead );
		slapi_log_error ( SLAPI_LOG_ACL, plugin_name,
				"No acis to remove in this entry\n" );
		return 0;
	}

	head = root->acic_list;
	if ( head)
		next = head->aci_next;
	while ( head ) {
		if ( head->aci_elevel == ACI_ELEVEL_USERDN_ANYONE)
			removed_anom_acl = 1;

		/* Free the acl */
		acllist_free_aci ( head );

		head = next;
		next = NULL;
		if ( head && head->aci_next )
			next = head->aci_next;
	}
	root->acic_list = NULL;

	/* remove the container from the slot */
	aciContainerArray[root->acic_index] = NULL;

	slapi_log_error ( SLAPI_LOG_ACL, plugin_name,
				"Removing container[%d]=%s\n",  root->acic_index,
					slapi_sdn_get_ndn ( root->acic_sdn) );
	dContainer = (AciContainer *) avl_delete ( &acllistRoot, aciListHead, 
										__acllist_aciContainer_node_cmp );
	__acllist_free_aciContainer ( &dContainer );

	acl_regen_aclsignature ();
	if ( removed_anom_acl )
		aclanom_invalidateProfile ();

	/*
	 * Now read back the entry and repopulate ACLs for that entry, but
	 * only if a specific aci was deleted, otherwise, we do a
	 * "When Harry met Sally" and nail 'em all.
	*/

	if ( attr != NULL) {

		if (0 != (rv = aclinit_search_and_update_aci (	0,		/* thisbeonly */
												   	sdn,	/* base */
													NULL,	/* be name */
													LDAP_SCOPE_BASE,
													ACL_ADD_ACIS,
											DONT_TAKE_ACLCACHE_WRITELOCK))) {
			slapi_log_error ( SLAPI_LOG_FATAL, plugin_name,
						" Can't add the rest of the acls for entry:%s after delete\n",
						slapi_sdn_get_dn ( sdn ) );
		}
	}

	/* Now free the tmp container we used */
	__acllist_free_aciContainer ( &aciListHead );

	/*
	 * regenerate the anonymous profile if we have deleted
	 * anyone acls.
	 * We don't need the aclcache readlock because the context of
	 * this routine is we have the write lock already.
	*/
	if ( removed_anom_acl )
		aclanom_gen_anomProfile(DONT_TAKE_ACLCACHE_READLOCK);

	return rv;
}

AciContainer *
acllist_get_aciContainer_new ( )
{

	AciContainer *head;

	head = (AciContainer * ) slapi_ch_calloc ( 1, sizeof ( AciContainer ) );
	head->acic_sdn = slapi_sdn_new ( );
	head->acic_index = -1;

	return head;
}	
static void
__acllist_free_aciContainer (  AciContainer **container)
{

	PR_ASSERT ( container != NULL );

	if ( (*container)->acic_index >= 0 ) 
		aciContainerArray[ (*container)->acic_index] = NULL;
	if ( (*container)->acic_sdn )
		slapi_sdn_free ( &(*container)->acic_sdn );
	slapi_ch_free ( (void **) container );

}

void
acllist_done_aciContainer ( AciContainer *head )
{

	PR_ASSERT ( head != NULL );

	slapi_sdn_done ( head->acic_sdn );
	head->acic_index = -1;
	
	/* The caller is responsible for taking care of list */
	head->acic_list = NULL;
}

	
aci_t *
acllist_get_aci_new ()
{
	aci_t	*aci_item;

	aci_item = (aci_t *) slapi_ch_calloc (1, sizeof (aci_t));
	aci_item->aci_sdn = slapi_sdn_new ();
	aci_item->aci_index = curAciIndex++;
	aci_item->aci_elevel = ACI_DEFAULT_ELEVEL;	/* by default it's a complex */
	aci_item->targetAttr = (Targetattr **) slapi_ch_calloc (
							ACL_INIT_ATTR_ARRAY,
							sizeof (Targetattr *));
	return aci_item;
} 

void
acllist_free_aci(aci_t *item)
{

	Targetattr			**attrArray;

	/* The caller is responsible for taking 
	** care of list issue
	*/
	if (item == NULL) return;

	slapi_sdn_free ( &item->aci_sdn );
	slapi_filter_free (item->target, 1);

	/* slapi_filter_free(item->targetAttr, 1); */
	attrArray  = item->targetAttr;
	if (attrArray) {
		int			i = 0;
		Targetattr		*attr;

		while (attrArray[i] != NULL) {
			attr = attrArray[i];
			if (attr->attr_type & ACL_ATTR_FILTER) {
				slapi_filter_free(attr->u.attr_filter, 1);
			} else {
				slapi_ch_free ( (void **) &attr->u.attr_str );
			}
			slapi_ch_free ( (void **) &attr );
			i++;
		}
		/* Now free the array */
		slapi_ch_free ( (void **) &attrArray );
	}
    
    /* Now free any targetattrfilters in this aci item */
    
    if ( item->targetAttrAddFilters ) {
	    free_targetattrfilters(&item->targetAttrAddFilters);
    }
    
    if ( item->targetAttrDelFilters ) {
	    free_targetattrfilters(&item->targetAttrDelFilters);
    }
	
	if (item->targetFilterStr) slapi_ch_free ( (void **) &item->targetFilterStr );
	slapi_filter_free(item->targetFilter, 1);

	/* free the handle */
	if (item->aci_handle) ACL_ListDestroy(NULL, item->aci_handle);

	/* Free the name */
    if (item->aclName) slapi_ch_free((void **) &item->aclName);

	/* Free any macro info*/
	if (item->aci_macro) {
		slapi_ch_free((void **) &item->aci_macro->match_this);		
		slapi_ch_free((void **) &item->aci_macro);		
	}		

	/* free at last -- free at last */
	slapi_ch_free ( (void **) &item );
}

void
free_targetattrfilters( Targetattrfilter ***attrFilterArray)
{
    if (*attrFilterArray) {
		int			i = 0;
		Targetattrfilter		*attrfilter;

		while ((*attrFilterArray)[i] != NULL) {
			attrfilter = (*attrFilterArray)[i];
			
            if ( attrfilter->attr_str != NULL) {
				slapi_ch_free ( (void **) &attrfilter->attr_str );
			}
            
            if (attrfilter->filter != NULL) {
				slapi_filter_free(attrfilter->filter, 1);
			}
            
            if( attrfilter->filterStr != NULL) {
				slapi_ch_free ( (void **) &attrfilter->filterStr );
			}
            
			slapi_ch_free ( (void **) &attrfilter );
			i++;
		}
		/* Now free the array */
		slapi_ch_free ( (void **) attrFilterArray );
	}
}

/* SEARCH */
void
acllist_init_scan (Slapi_PBlock *pb, int scope, const char *base)
{
	Acl_PBlock			*aclpb;
	AciContainer		*root;
	char				*basedn = NULL;
	int					index;

	if ( acl_skip_access_check ( pb, NULL ) ) {
		return;
	}

	/*acllist_print_tree ( acllistRoot, &depth, "top", "top")	; */
	/* my_print ( acllistRoot );*/
	/* If we have an anonymous profile and I am an anom dude - let's skip it */
	if ( aclanom_is_client_anonymous ( pb )) {
		return;
	}
	aclpb = acl_get_aclpb (pb, ACLPB_BINDDN_PBLOCK );
	if ( !aclpb ) {
		slapi_log_error ( SLAPI_LOG_FATAL, plugin_name,  "Missing aclpb 4 \n" );
		return;
	}

	aclpb->aclpb_handles_index[0] = -1;

	/* If base is NULL - it means we are going to go thru all the ACLs
	 * This is needed when we do anonymous profile generation.
	 */
	if ( NULL == base ) {
		return;
	}
	
	aclpb->aclpb_state |= ACLPB_SEARCH_BASED_ON_LIST ;
	
	acllist_acicache_READ_LOCK();
	
	basedn = slapi_ch_strdup (base);
	index = 0;
	aclpb->aclpb_search_base = slapi_ch_strdup ( base );

	while (basedn) {
		char		*tmp = NULL;
		
		slapi_sdn_set_normdn_byref(aclpb->aclpb_aclContainer->acic_sdn, basedn);

		root = (AciContainer *) avl_find(acllistRoot, 
		                                 (caddr_t) aclpb->aclpb_aclContainer, 
		                                 (IFP) __acllist_aciContainer_node_cmp);
		if ( index >= aclpb_max_selected_acls -2 ) {
			aclpb->aclpb_handles_index[0] = -1;
			slapi_ch_free ( (void **) &basedn);
			break;
		} else if ( NULL != root ) {
			aclpb->aclpb_base_handles_index[index++] = root->acic_index;
			aclpb->aclpb_base_handles_index[index] = -1;
		} else if ( NULL == root ) {
			/* slapi_dn_parent returns the "parent" dn syntactically.
			 * Most likely, basedn is above suffix (e.g., dn=com).
			 * Thus, no need to make it FATAL. */
			slapi_log_error ( SLAPI_LOG_ACL, plugin_name, 
			                  "Failed to find root for base: %s \n", basedn );
		}
		tmp = slapi_dn_parent ( basedn );
		slapi_ch_free ( (void **) &basedn);
		basedn = tmp;
	}

	acllist_done_aciContainer ( aclpb->aclpb_aclContainer);

	if ( aclpb->aclpb_base_handles_index[0] == -1 )	
		aclpb->aclpb_state &= ~ACLPB_SEARCH_BASED_ON_LIST ;

	acllist_acicache_READ_UNLOCK();
}

/*
 * Initialize aclpb_handles_index[] (sentinel -1) to
 * contain a list of all aci items at and above edn in the DIT tree.
 * This list will be subsequestly scanned to find applicable aci's for
 * the given operation.
*/

/* edn is normalized & case-ignored */
void 
acllist_aciscan_update_scan (  Acl_PBlock *aclpb, char *edn )
{

	int		index = 0;
	char		*basedn = NULL;
	AciContainer	*root;
	int is_not_search_base = 1;

	if ( !aclpb ) {
		slapi_log_error ( SLAPI_LOG_ACL, plugin_name,
				"acllist_aciscan_update_scan: NULL acl pblock\n");
		return;
	}

	/* First copy the containers indx from the base to the one which is
	 * going to be used.
	 * The base handles get done in acllist_init_scan().
	 * This stuff is only used if it's a search operation.
	 */
	if ( aclpb->aclpb_search_base ) {
		if ( strcasecmp ( edn, aclpb->aclpb_search_base) == 0) {
			is_not_search_base = 0;
		}
		for (index = 0; (aclpb->aclpb_base_handles_index[index] != -1) && 
		                (index < aclpb_max_selected_acls - 2); index++) ;
		memcpy(aclpb->aclpb_handles_index, aclpb->aclpb_base_handles_index,
		       sizeof(*aclpb->aclpb_handles_index) * index);
	}
	aclpb->aclpb_handles_index[index] = -1;

	/*
	 * Here, make a list of all the aci's that will apply
	 * to edn ie. all aci's at and above edn in the DIT tree.
	 * 
	 * Do this by walking up edn, looking at corresponding
	 * points in the acllistRoot aci tree.
	 *
	 * If is_not_search_base is true, then we need to iterate on edn, otherwise
	 * we've already got all the base handles above.
	 * 
	*/ 

	if (is_not_search_base) {
		
		basedn = slapi_ch_strdup ( edn );

		while (basedn ) {
			char		*tmp = NULL;
	
			slapi_sdn_set_ndn_byref ( aclpb->aclpb_aclContainer->acic_sdn, basedn );

			root = (AciContainer *) avl_find( acllistRoot, 
									(caddr_t) aclpb->aclpb_aclContainer, 
									(IFP) __acllist_aciContainer_node_cmp);
		
			slapi_log_error ( SLAPI_LOG_ACL, plugin_name,
						"Searching AVL tree for update:%s: container:%d\n", basedn ,
					root ? root->acic_index: -1);	
			if ( index >= aclpb_max_selected_acls -2 ) {
				aclpb->aclpb_handles_index[0] = -1;
				slapi_ch_free ( (void **) &basedn);
				break;
			} else  if ( NULL != root ) {
				aclpb->aclpb_handles_index[index++] = root->acic_index;
				aclpb->aclpb_handles_index[index] = -1;
			} 
			tmp = slapi_dn_parent ( basedn );
			slapi_ch_free ( (void **) &basedn);
			basedn = tmp;
			if ( aclpb->aclpb_search_base  && tmp &&
				( 0 ==  strcasecmp ( tmp, aclpb->aclpb_search_base))) {
				slapi_ch_free ( (void **) &basedn);
				tmp = NULL;
			}
		} /* while */
	}

	acllist_done_aciContainer ( aclpb->aclpb_aclContainer );
}

aci_t *
acllist_get_first_aci (Acl_PBlock *aclpb, PRUint32 *cookie )
{

	int			val;

	*cookie = val = 0;
	if ( aclpb && aclpb->aclpb_handles_index[0] != -1 ) {
		val = aclpb->aclpb_handles_index[*cookie];
	}
	if ( NULL == aciContainerArray[val]) {
		return ( acllist_get_next_aci ( aclpb, NULL, cookie ) );
	} 

	return  (aciContainerArray[val]->acic_list );
}
/*
 * acllist_get_next_aci
 *	Return the next aci in the list
 *
 *	 Inputs
 *	Acl_PBlock *aclpb				-- acl Main block;  if  aclpb= NULL, 
 *							-- then we scan thru the whole list.
 *							-- which is used by anom profile code.
 *	aci_t *curaci					-- the current aci
 *	PRUint32 *cookie				-- cookie -- to maintain a state (what's next)
 *
 */

aci_t *
acllist_get_next_aci ( Acl_PBlock *aclpb, aci_t *curaci, PRUint32 *cookie )
{
	PRUint32	val;
	int			scan_entire_list;

	/*
	   Here, if we're passed a curaci and there's another aci in the same node,
	   return that one.
	*/

	if ( curaci && curaci->aci_next )
		return ( curaci->aci_next );

	/*
	   Determine if we need to scan the entire list of acis.
	   We do if the aclpb==NULL or if the first handle index is -1.
	   That means that we want to go through
	   the entire aciContainerArray up to the currContainerIndex to get
	   acis; the -1 in the first position is a special keyword which tells
	   us that the acis have changed, so we need to go through all of them.
	*/

	scan_entire_list = (aclpb == NULL || aclpb->aclpb_handles_index[0] == -1);

start:
	(*cookie)++;
	val = *cookie;

	/* if we are not scanning the entire aciContainerArray list, we only want to
	   look at the indexes specified in the handles index */
	if ( !scan_entire_list )
		val = aclpb->aclpb_handles_index[*cookie];

	/* the hard max end */
	if ( val >= maxContainerIndex)
		return NULL;

	/* reached the end of the array */   
	if ((!scan_entire_list && (*cookie >= (aclpb_max_selected_acls-1))) ||
		(*cookie >= currContainerIndex)) {
		return NULL;
	}

	/* if we're only using the handles list for our aciContainerArray
	   indexes, the -1 value marks the end of that list */
	if ( !scan_entire_list && (aclpb->aclpb_handles_index[*cookie] == -1) ) {
		return NULL;
	}

	/* if we're scanning the entire list, and we hit a null value in the
	   middle of the list, just try the next one; this can happen if
	   an aci was deleted - it can leave "holes" in the array */
	if ( scan_entire_list && ( NULL == aciContainerArray[val])) {
		goto start;
	}

	if ( aciContainerArray[val] )
		return (aciContainerArray[val]->acic_list );
	else
		return NULL;
}

void
acllist_acicache_READ_UNLOCK( )
{
	ACILIST_UNLOCK_READ ();

}

void
acllist_acicache_READ_LOCK()
{
	/* get a reader lock */
	ACILIST_LOCK_READ ();	

}

void
acllist_acicache_WRITE_UNLOCK( )
{
	ACILIST_UNLOCK_WRITE ();

}

void
acllist_acicache_WRITE_LOCK( )
{
	ACILIST_LOCK_WRITE ();

}

/* This routine must be called with the acicache write lock taken */
/* newdn is normalized (no need to be case-ignored) */
int
acllist_moddn_aci_needsLock ( Slapi_DN *oldsdn, char *newdn )
{
	AciContainer		*aciListHead;
	AciContainer		*head;
	aci_t *acip;
	const char *oldndn;

	/* first get the container */

	aciListHead =   acllist_get_aciContainer_new ( );
	slapi_sdn_free(&aciListHead->acic_sdn);
	aciListHead->acic_sdn = oldsdn;

	if ( NULL == (head = (AciContainer *) avl_find( acllistRoot, aciListHead,
	     (IFP) __acllist_aciContainer_node_cmp ) ) ) {

		slapi_log_error ( SLAPI_PLUGIN_ACL, plugin_name,
		         "Can't find the acl in the tree for moddn operation:olddn%s\n",
		         slapi_sdn_get_ndn ( oldsdn ));
		aciListHead->acic_sdn = NULL;
		__acllist_free_aciContainer ( &aciListHead );
		return 1;
	}

	/* Now set the new DN */
	slapi_sdn_set_normdn_byval(head->acic_sdn, newdn);

	/* If necessary, reset the target DNs, as well. */
	oldndn = slapi_sdn_get_ndn(oldsdn);
	for (acip = head->acic_list; acip; acip = acip->aci_next) {
		const char *ndn = slapi_sdn_get_ndn(acip->aci_sdn);
		char *p = PL_strstr(ndn, oldndn);
		if (p) {
			if (p == ndn) {
				/* target dn is identical, replace it with new DN*/
				slapi_sdn_set_normdn_byval(acip->aci_sdn, newdn);
			} else {
				/* target dn is a descendent of olddn, merge it with new DN*/
				char *mynewdn;
				*p = '\0';
				mynewdn = slapi_ch_smprintf("%s%s", ndn, newdn);
				slapi_sdn_set_normdn_passin(acip->aci_sdn, mynewdn);
			}
		}
	}
    
	aciListHead->acic_sdn = NULL;
	__acllist_free_aciContainer ( &aciListHead );

	return 0;
}

void
acllist_print_tree ( Avlnode *root, int *depth, char *start, char *side)
{

	AciContainer		*aciHeadList;

	if ( NULL == root ) {
		return;
	}
	aciHeadList = (AciContainer *) root->avl_data;
	slapi_log_error ( SLAPI_LOG_ACL, "plugin_name",
						"Container[ Depth=%d%s-%s]: %s\n", *depth, start, side,
						slapi_sdn_get_ndn ( aciHeadList->acic_sdn ) );

	(*depth)++;

	acllist_print_tree ( root->avl_left,  depth, side, "L" );
	acllist_print_tree ( root->avl_right,  depth, side, "R" );

	(*depth)--;

}

static 
void
ravl_print( Avlnode	*root, int	depth )
{
	int	i;

	AciContainer        *aciHeadList;
	if ( root == 0 )
		return;

	ravl_print( root->avl_right, depth+1 );

	for ( i = 0; i < depth; i++ )
		printf( "   " );
	aciHeadList = (AciContainer *) root->avl_data;
	printf( "%s\n",  slapi_sdn_get_ndn ( aciHeadList->acic_sdn ) );

	ravl_print( root->avl_left, depth+1 );
}

void
my_print( Avlnode	*root )
{
	printf( "********\n" );

	if ( root == 0 )
		printf( "\tNULL\n" );
	else
		( void ) ravl_print( root, 0 );

	printf( "********\n" );
}

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
 *
 * psearch.c - persistent search
 * August 1997, ggood@netscape.com
 *
 * Open issues:
 *  - we increment and decrement active_threads in here.  Are there
 *    conditions under which this can prevent a server shutdown?
 */

#include "slap.h"
#include "fe.h"

/*
 * A structure used to create a linked list 
 * of entries being sent by a particular persistent
 * search result thread.
 * The ctrl is an "Entry Modify Notification" control
 * which we may send back with entries.
 */
typedef struct _ps_entry_queue_node {
    Slapi_Entry			*pe_entry;
    LDAPControl			*pe_ctrls[2];
    struct _ps_entry_queue_node	*pe_next;
} PSEQNode;

/*
 * Information about a single persistent search
 */
typedef struct _psearch {
    Slapi_PBlock	*ps_pblock;
    PRLock		*ps_lock;
    PRThread		*ps_tid;
    PRInt32		ps_complete;
    PSEQNode		*ps_eq_head;
    PSEQNode		*ps_eq_tail;
    time_t		ps_lasttime;
    ber_int_t		ps_changetypes;
    int			ps_send_entchg_controls;
    struct _psearch	*ps_next;
} PSearch;

/*
 * A list of outstanding persistent searches.
 */
typedef struct _psearch_list {
    rwl		*pl_rwlock;	/* R/W lock struct to serialize access */
    PSearch	*pl_head;	/* Head of list */
    PRLock	*pl_cvarlock;	/* Lock for cvar */
    PRCondVar	*pl_cvar;	/* ps threads sleep on this */
} PSearch_List;

/*
 * Convenience macros for locking the list of persistent searches
 */
#define PSL_LOCK_READ()    psearch_list->pl_rwlock->rwl_acquire_read_lock( psearch_list->pl_rwlock)
#define PSL_UNLOCK_READ()  psearch_list->pl_rwlock->rwl_relinquish_read_lock( psearch_list->pl_rwlock )
#define PSL_LOCK_WRITE()   psearch_list->pl_rwlock->rwl_acquire_write_lock( psearch_list->pl_rwlock )
#define PSL_UNLOCK_WRITE() psearch_list->pl_rwlock->rwl_relinquish_write_lock( psearch_list->pl_rwlock )
    

/*
 * Convenience macro for checking if the Persistent Search subsystem has
 * been initialized.
 */
#define PS_IS_INITIALIZED()	(psearch_list != NULL)
    
/* Main list of outstanding persistent searches */
static PSearch_List	*psearch_list = NULL;

/* Forward declarations */
static void ps_send_results( void *arg );
static PSearch *psearch_alloc();
static void ps_add_ps( PSearch *ps );
static void ps_remove( PSearch *dps );
static void pe_ch_free( PSEQNode **pe );
static int create_entrychange_control( ber_int_t chgtype, ber_int_t chgnum,
	const char *prevdn, LDAPControl **ctrlp );


/*
 * Initialize the list structure which contains the list
 * of outstanding persistent searches.  This must be
 * called early during server startup.
 */
void
ps_init_psearch_system()
{
    if ( !PS_IS_INITIALIZED()) {
	psearch_list = (PSearch_List *) slapi_ch_calloc( 1, sizeof( PSearch_List ));
	if (( psearch_list->pl_rwlock = rwl_new()) == NULL ) {
	    LDAPDebug( LDAP_DEBUG_ANY, "init_psearch_list: cannot initialize lock structure. "
		    "The server is terminating.\n", 0, 0, 0 );
	    exit( -1 );
	}
	if (( psearch_list->pl_cvarlock = PR_NewLock()) == NULL ) {
	    LDAPDebug( LDAP_DEBUG_ANY, "init_psearch_list: cannot create new lock.  "
		    "The server is terminating.\n", 0, 0, 0 );
	    exit( -1 );
	}
	if (( psearch_list->pl_cvar = PR_NewCondVar( psearch_list->pl_cvarlock )) == NULL ) {
	    LDAPDebug( LDAP_DEBUG_ANY, "init_psearch_list: cannot create new condition variable.  "
		    "The server is terminating.\n", 0, 0, 0 );
	    exit( -1 );
	}
	psearch_list->pl_head = NULL;
    }
}




/*
 * Close all outstanding persistent searches.
 * To be used when the server is shutting down.
 */
void
ps_stop_psearch_system()
{
    PSearch	*ps;

	if ( PS_IS_INITIALIZED()) {
		PSL_LOCK_WRITE();
		for ( ps = psearch_list->pl_head; NULL != ps; ps = ps->ps_next ) {
			PR_AtomicIncrement( &ps->ps_complete );
		}
		PSL_UNLOCK_WRITE();
		ps_wakeup_all();
	}
}




/*
 * Add the given pblock to the list of outstanding persistent searches.
 * Then, start a thread to send the results to the client as they
 * are dispatched by add, modify, and modrdn operations.
 */
void
ps_add( Slapi_PBlock *pb, ber_int_t changetypes, int send_entchg_controls )
{
    PSearch	*ps;

    if ( PS_IS_INITIALIZED() && NULL != pb ) {
	/* Create the new node */
	ps = psearch_alloc();
	ps->ps_pblock = pb;
	ps->ps_changetypes = changetypes;
	ps->ps_send_entchg_controls = send_entchg_controls;

	/* Add it to the head of the list of persistent searches */
	ps_add_ps( ps );

	/* Start a thread to send the results */
	ps->ps_tid = PR_CreateThread( PR_USER_THREAD, ps_send_results,
		(void *) ps, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
		PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE );

        /* Checking if the thread is succesfully created and 
         * if the thread is not created succesfully.... we send 
         * error messages to the Log file 
         */ 
       if(NULL == (ps->ps_tid)){ 
            int prerr;
            prerr = PR_GetError(); 
            LDAPDebug(LDAP_DEBUG_ANY,"persistent search PR_CreateThread()failed in the " 
                     "ps_add function: " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
					 prerr, slapd_pr_strerror(prerr), 0); 
   
         /* Now remove the ps from the list so call the function ps_remove */ 
            ps_remove(ps); 
            PR_DestroyLock ( ps->ps_lock );
            ps->ps_lock = NULL;
            slapi_ch_free((void **) &ps->ps_pblock );
            slapi_ch_free((void **) &ps );
          } 	
    }
}



/*
 * Remove the given PSearch from the list of outstanding persistent
 * searches and delete its resources.
 */
static void
ps_remove( PSearch *dps )
{
  PSearch	*ps;
  
  if ( PS_IS_INITIALIZED() && NULL != dps ) {
	PSL_LOCK_WRITE();
	if ( dps == psearch_list->pl_head ) {
	  /* Remove from head */
	  psearch_list->pl_head = psearch_list->pl_head->ps_next;
	} else {
	  /* Find and remove from list */
	  ps = psearch_list->pl_head;
	  while ( NULL != ps->ps_next ) {
		if ( ps->ps_next == dps ) {
		  ps->ps_next = ps->ps_next->ps_next;
		  break;
		} else {
		  ps = ps->ps_next;
		}
	  }
	}
	PSL_UNLOCK_WRITE();
  }
}

/*
 * Free a persistent search node (and everything it holds).
 */
static void
pe_ch_free( PSEQNode **pe )
{
    if ( pe != NULL && *pe != NULL ) {
	if ( (*pe)->pe_entry != NULL ) {
	    slapi_entry_free( (*pe)->pe_entry );
	    (*pe)->pe_entry = NULL;
	}

	if ( (*pe)->pe_ctrls[0] != NULL ) {
	    ldap_control_free( (*pe)->pe_ctrls[0] );
	    (*pe)->pe_ctrls[0] = NULL;
	}

	slapi_ch_free( (void **)pe );
    }
}


/*
 * Thread routine for sending search results to a client
 * which is persistently waiting for them.
 *
 * This routine will terminate when either (a) the ps_complete
 * flag is set, or (b) the associated operation is abandoned.
 * In any case, the thread won't notice until it wakes from
 * sleeping on the ps_list condition variable, so it needs
 * to be awakened.
 */
static void
ps_send_results( void *arg )
{
    PSearch *ps = (PSearch *)arg;
	PSEQNode *peq, *peqnext;
	struct slapi_filter *filter = 0;
	char *base = NULL;
	Slapi_DN *sdn = NULL;
	char *fstr = NULL;
	char **pbattrs = NULL;
	int conn_acq_flag = 0;
    
    g_incr_active_threadcnt();

    /* need to acquire a reference to this connection so that it will not
       be released or cleaned up out from under us */
    PR_Lock( ps->ps_pblock->pb_conn->c_mutex );
    conn_acq_flag = connection_acquire_nolock(ps->ps_pblock->pb_conn);    
    PR_Unlock( ps->ps_pblock->pb_conn->c_mutex );

	if (conn_acq_flag) {
		slapi_log_error(SLAPI_LOG_CONNS, "Persistent Search",
						"conn=%" NSPRIu64 " op=%d Could not acquire the connection - psearch aborted\n",
						ps->ps_pblock->pb_conn->c_connid, ps->ps_pblock->pb_op->o_opid);
	}

    PR_Lock( psearch_list->pl_cvarlock );

    while ( (conn_acq_flag == 0) && !ps->ps_complete ) {
	/* Check for an abandoned operation */
	if ( ps->ps_pblock->pb_op == NULL || slapi_op_abandoned( ps->ps_pblock ) ) {
		slapi_log_error(SLAPI_LOG_CONNS, "Persistent Search",
						"conn=%" NSPRIu64 " op=%d The operation has been abandoned\n",
						ps->ps_pblock->pb_conn->c_connid, ps->ps_pblock->pb_op->o_opid);
	    break;
	}
	if ( NULL == ps->ps_eq_head ) {
	    /* Nothing to do */
	    PR_WaitCondVar( psearch_list->pl_cvar, PR_INTERVAL_NO_TIMEOUT );
	} else {
	    /* dequeue the item */
	    int		attrsonly;
	    char	**attrs;
	    LDAPControl	**ectrls;
	    Slapi_Entry	*ec;
		Slapi_Filter	*f = NULL;
		
	    PR_Lock( ps->ps_lock );

		peq = ps->ps_eq_head;
		ps->ps_eq_head = peq->pe_next;
	    if ( NULL == ps->ps_eq_head ) {
			ps->ps_eq_tail = NULL;
	    }

	    PR_Unlock( ps->ps_lock );

	    /* Get all the information we need to send the result */
	    ec = peq->pe_entry;
	    slapi_pblock_get( ps->ps_pblock, SLAPI_SEARCH_ATTRS, &attrs );
	    slapi_pblock_get( ps->ps_pblock, SLAPI_SEARCH_ATTRSONLY, &attrsonly );
	    if ( !ps->ps_send_entchg_controls || peq->pe_ctrls[0] == NULL ) {
		ectrls = NULL;
	    } else {
		ectrls = peq->pe_ctrls;
	    }

	    /*
	     * Send the result.  Since send_ldap_search_entry can block for
	     * up to 30 minutes, we relinquish all locks before calling it.
	     */
	    PR_Unlock(psearch_list->pl_cvarlock);

		/*
		 * The entry is in the right scope and matches the filter
		 * but we need to redo the filter test here to check access
		 * controls. See the comments at the slapi_filter_test()
		 * call in ps_service_persistent_searches().		 
		*/
		slapi_pblock_get( ps->ps_pblock, SLAPI_SEARCH_FILTER, &f );			

		/* See if the entry meets the filter and ACL criteria */
		if ( slapi_vattr_filter_test( ps->ps_pblock, ec, f,
			    1 /* verify_access */ ) == 0 ) {
			int rc = 0;
	    	slapi_pblock_set( ps->ps_pblock, SLAPI_SEARCH_RESULT_ENTRY, ec );
	    	rc = send_ldap_search_entry( ps->ps_pblock, ec,
										 ectrls, attrs, attrsonly );
			if (rc) {
				slapi_log_error(SLAPI_LOG_CONNS, "Persistent Search",
								"conn=%" NSPRIu64 " op=%d Error %d sending entry %s with op status %d\n",
								ps->ps_pblock->pb_conn->c_connid, ps->ps_pblock->pb_op->o_opid,
								rc, slapi_entry_get_dn_const(ec), ps->ps_pblock->pb_op->o_status);
			}
		}
	    
		PR_Lock(psearch_list->pl_cvarlock);

		/* Deallocate our wrapper for this entry */
		pe_ch_free( &peq );
	}
    }
    PR_Unlock( psearch_list->pl_cvarlock );
    ps_remove( ps );

    /* indicate the end of search */
    plugin_call_plugins( ps->ps_pblock , SLAPI_PLUGIN_POST_SEARCH_FN );

	/* free things from the pblock that were not free'd in do_search() */
	/* we strdup'd this in search.c - need to free */
	slapi_pblock_get( ps->ps_pblock, SLAPI_ORIGINAL_TARGET_DN, &base );
	slapi_pblock_set( ps->ps_pblock, SLAPI_ORIGINAL_TARGET_DN, NULL );
	slapi_ch_free_string(&base);

	/* Free SLAPI_SEARCH_* before deleting op since those are held by op */
	slapi_pblock_get( ps->ps_pblock, SLAPI_SEARCH_TARGET_SDN, &sdn );
	slapi_pblock_set( ps->ps_pblock, SLAPI_SEARCH_TARGET_SDN, NULL );
	slapi_sdn_free(&sdn);

    slapi_pblock_get( ps->ps_pblock, SLAPI_SEARCH_STRFILTER, &fstr );
    slapi_pblock_set( ps->ps_pblock, SLAPI_SEARCH_STRFILTER, NULL );
	slapi_ch_free_string(&fstr);

    slapi_pblock_get( ps->ps_pblock, SLAPI_SEARCH_ATTRS, &pbattrs );
    slapi_pblock_set( ps->ps_pblock, SLAPI_SEARCH_ATTRS, NULL );
	if ( pbattrs != NULL )
	{
		charray_free( pbattrs );
	}
	
	slapi_pblock_get(ps->ps_pblock, SLAPI_SEARCH_FILTER, &filter );
	slapi_pblock_set(ps->ps_pblock, SLAPI_SEARCH_FILTER, NULL );
	slapi_filter_free(filter, 1);

    /* Clean up the connection structure */
    PR_Lock( ps->ps_pblock->pb_conn->c_mutex );

	slapi_log_error(SLAPI_LOG_CONNS, "Persistent Search",
					"conn=%" NSPRIu64 " op=%d Releasing the connection and operation\n",
					ps->ps_pblock->pb_conn->c_connid, ps->ps_pblock->pb_op->o_opid);
    /* Delete this op from the connection's list */
    connection_remove_operation( ps->ps_pblock->pb_conn, ps->ps_pblock->pb_op );
    operation_free(&(ps->ps_pblock->pb_op),ps->ps_pblock->pb_conn);
    ps->ps_pblock->pb_op=NULL;

    /* Decrement the connection refcnt */
    if (conn_acq_flag == 0) { /* we acquired it, so release it */
	connection_release_nolock (ps->ps_pblock->pb_conn);
    }
    PR_Unlock( ps->ps_pblock->pb_conn->c_mutex );

    PR_DestroyLock ( ps->ps_lock );
    ps->ps_lock = NULL;

    slapi_ch_free((void **) &ps->ps_pblock );
	for ( peq = ps->ps_eq_head; peq; peq = peqnext) {
		peqnext = peq->pe_next;
		pe_ch_free( &peq );
	}
    slapi_ch_free((void **) &ps );
    g_decr_active_threadcnt();
}



/*
 * Allocate and initialize an empty PSearch node.
 */
static PSearch *
psearch_alloc()
{
    PSearch 	*ps;

    ps = (PSearch *) slapi_ch_calloc( 1, sizeof( PSearch ));

    ps->ps_pblock = NULL;
    if (( ps->ps_lock = PR_NewLock()) == NULL ) {
	LDAPDebug( LDAP_DEBUG_ANY, "psearch_add: cannot create new lock.  "
		"Persistent search abandoned.\n", 0, 0, 0 );
	slapi_ch_free((void **)&ps);
	return( NULL );
    }
    ps->ps_tid = (PRThread *) NULL;
    ps->ps_complete = 0;
    ps->ps_eq_head = ps->ps_eq_tail = (PSEQNode *) NULL;
    ps->ps_lasttime = (time_t) 0L;
    ps->ps_next = NULL;
    return ps;
}



/*
 * Add the given persistent search to the
 * head of the list of persistent searches.
 */
static void
ps_add_ps( PSearch *ps )
{
    if ( PS_IS_INITIALIZED() && NULL != ps ) {
		PSL_LOCK_WRITE();
		ps->ps_next = psearch_list->pl_head;
		psearch_list->pl_head = ps;
		PSL_UNLOCK_WRITE();
    }
}



/*
 * Wake up all threads sleeping on
 * the psearch_list condition variable.
 */
void
ps_wakeup_all()
{
	if ( PS_IS_INITIALIZED()) {
		PR_Lock( psearch_list->pl_cvarlock );
		PR_NotifyAllCondVar( psearch_list->pl_cvar );
		PR_Unlock( psearch_list->pl_cvarlock );
	}
}


/*
 * Check if there are any persistent searches.  If so,
 * the check to see if the chgtype is one of those the
 * client is interested in.  If so, then check to see if
 * the entry matches any of the filters the searches.
 * If so, then enqueue the entry on that persistent search's
 * ps_entryqueue and signal it to wake up and send the entry.
 *
 * Note that if eprev is NULL we assume that the entry's DN
 * was not changed by the op. that called this function.  If
 * chgnum is 0 it is unknown so we won't ever send it to a
 * client in the EntryChangeNotification control.
 */
void
ps_service_persistent_searches( Slapi_Entry *e, Slapi_Entry *eprev, ber_int_t chgtype,
	ber_int_t chgnum )
{
	LDAPControl *ctrl = NULL;
	PSearch	*ps = NULL;
	PSEQNode *pe = NULL;
	int  matched = 0;
	const char *edn;

	if ( !PS_IS_INITIALIZED()) {
		return;
	}

	if ( NULL == e ) {
		/* For now, some backends such as the chaining backend do not provide a post-op entry */
		return;
	}

	PSL_LOCK_READ();
	edn = slapi_entry_get_dn_const(e);

	for ( ps = psearch_list ? psearch_list->pl_head : NULL; NULL != ps; ps = ps->ps_next ) {
		char *origbase = NULL;
		Slapi_DN *base = NULL;
		Slapi_Filter	*f;
		int		scope;

		/* Skip the node that doesn't meet the changetype,
		 * or is unable to use the change in ps_send_results()
		 */
		if (( ps->ps_changetypes & chgtype ) == 0 ||
				ps->ps_pblock->pb_op == NULL ||
				slapi_op_abandoned( ps->ps_pblock ) ) {
			continue;
		}

		slapi_log_error(SLAPI_LOG_CONNS, "Persistent Search",
						"conn=%" NSPRIu64 " op=%d entry %s with chgtype %d "
						"matches the ps changetype %d\n",
						ps->ps_pblock->pb_conn->c_connid,
						ps->ps_pblock->pb_op->o_opid,
						edn, chgtype, ps->ps_changetypes);

		slapi_pblock_get( ps->ps_pblock, SLAPI_SEARCH_FILTER, &f );
		slapi_pblock_get( ps->ps_pblock, SLAPI_ORIGINAL_TARGET_DN, &origbase );
		slapi_pblock_get( ps->ps_pblock, SLAPI_SEARCH_TARGET_SDN, &base );
		slapi_pblock_get( ps->ps_pblock, SLAPI_SEARCH_SCOPE, &scope );
		if (NULL == base) {
			base = slapi_sdn_new_dn_byref(origbase);
			slapi_pblock_set(ps->ps_pblock, SLAPI_SEARCH_TARGET_SDN, base);
		}

		/*
		 * See if the entry meets the scope and filter criteria.
		 * We cannot do the acl check here as this thread
		 * would then potentially clash with the ps_send_results()
		 * thread on the aclpb in ps->ps_pblock.
		 * By avoiding the acl check in this thread, and leaving all the acl
		 * checking to the ps_send_results() thread we avoid
		 * the ps_pblock contention problem.
		 * The lesson here is "Do not give multiple threads arbitary access
		 * to the same pblock" this kind of muti-threaded access
		 * to the same pblock must be done carefully--there is currently no
		 * generic satisfactory way to do this.
		*/
		if ( slapi_sdn_scope_test( slapi_entry_get_sdn_const(e), base, scope ) &&
			 slapi_vattr_filter_test( ps->ps_pblock, e, f, 0 /* verify_access */ ) == 0 ) {
			PSEQNode *pOldtail;

			/* The scope and the filter match - enqueue it */

			matched++;
			pe = (PSEQNode *)slapi_ch_calloc( 1, sizeof( PSEQNode ));
			pe->pe_entry = slapi_entry_dup( e );
			if ( ps->ps_send_entchg_controls ) {
				/* create_entrychange_control() is more
				 * expensive than slapi_dup_control()
				 */
				if ( ctrl == NULL ) {
					int rc;
					rc = create_entrychange_control( chgtype, chgnum,
							eprev ? slapi_entry_get_dn_const(eprev) : NULL,
							&ctrl );
					if ( rc != LDAP_SUCCESS ) {
						char ebuf[ BUFSIZ ];
		   				LDAPDebug( LDAP_DEBUG_ANY, "ps_service_persistent_searches:"
						" unable to create EntryChangeNotification control for"
						" entry \"%s\" -- control won't be sent.\n",
						escape_string( slapi_entry_get_dn_const(e), ebuf), 0, 0 );
					}
				}
				if ( ctrl ) {
					pe->pe_ctrls[0] = slapi_dup_control( ctrl );
				}
			}

			/* Put it on the end of the list for this pers search */
			PR_Lock( ps->ps_lock );
			pOldtail = ps->ps_eq_tail;
			ps->ps_eq_tail = pe;
			if ( NULL == ps->ps_eq_head ) {
				ps->ps_eq_head = ps->ps_eq_tail;
			}
			else {
				pOldtail->pe_next = ps->ps_eq_tail;
			}
			PR_Unlock( ps->ps_lock );
		}
	}

   	PSL_UNLOCK_READ();

	/* Were there any matches? */
	if ( matched ) {
		ldap_control_free( ctrl );
		/* Turn 'em loose */
		ps_wakeup_all();
		LDAPDebug( LDAP_DEBUG_TRACE, "ps_service_persistent_searches: enqueued entry "
			"\"%s\" on %d persistent search lists\n", slapi_entry_get_dn_const(e), matched, 0 );
	} else {
		LDAPDebug( LDAP_DEBUG_TRACE, "ps_service_persistent_searches: entry "
			"\"%s\" not enqueued on any persistent search lists\n", slapi_entry_get_dn_const(e), 0, 0 );
	}

}

/*
 * Parse the value from an LDAPv3 "Persistent Search" control.  They look
 * like this:
 * 
 *    PersistentSearch ::= SEQUENCE {
 *	changeTypes INTEGER,
 *	-- the changeTypes field is the logical OR of 
 *	-- one or more of these values: add (1), delete (2),
 *	-- modify (4), modDN (8).  It specifies which types of
 *	-- changes will cause an entry to be returned.
 *	changesOnly BOOLEAN, -- skip initial search?
 *	returnECs BOOLEAN,   -- return "Entry Change" controls?
 *   }
 *
 * Return an LDAP error code (LDAP_SUCCESS if all goes well).
 *
 * This function is standalone; it does not require initialization of
 * the PS subsystem.
 */
int
ps_parse_control_value( struct berval *psbvp, ber_int_t *changetypesp, int *changesonlyp, int *returnecsp )
{
    int rc= LDAP_SUCCESS;

    if ( psbvp->bv_len == 0 || psbvp->bv_val == NULL )
    {
    	rc= LDAP_PROTOCOL_ERROR;
    }
    else
    {
        BerElement *ber= ber_init( psbvp );
        if ( ber == NULL )
        {
        	rc= LDAP_OPERATIONS_ERROR;
        }
        else
        {
            if ( ber_scanf( ber, "{ibb}", changetypesp, changesonlyp, returnecsp ) == LBER_ERROR )
            {
            	rc= LDAP_PROTOCOL_ERROR;
            }
        	/* the ber encoding is no longer needed */
        	ber_free(ber,1);
        }
    }

    return( rc );
}


/*
 * Create an LDAPv3 "Entry Change Notification" control.  They look like this:
 *
 *	EntryChangeNotification ::= SEQUENCE {
 *	    changeType		ENUMERATED {
 *		add	(1),	-- LDAP_CHANGETYPE_ADD
 *		delete	(2),    -- LDAP_CHANGETYPE_DELETE
 *		modify	(4),    -- LDAP_CHANGETYPE_MODIFY
 *		moddn	(8),    -- LDAP_CHANGETYPE_MODDN
 *	    },
 *	    previousDN	 LDAPDN OPTIONAL,   -- included for MODDN ops. only
 *	    changeNumber INTEGER OPTIONAL,  -- included if supported by DSA
 *	}
 *
 * This function returns an LDAP error code (LDAP_SUCCESS if all goes well).
 * The value returned in *ctrlp should be free'd use ldap_control_free().
 * If chgnum is 0 we omit it from the control.
 */
static int
create_entrychange_control( ber_int_t chgtype, ber_int_t chgnum, const char *dn,
	LDAPControl **ctrlp )
{
    int			rc;
    BerElement		*ber;
    struct berval	*bvp;
	const char *prevdn= dn;

    if ( prevdn == NULL ) {
	prevdn = "";
    }

    if ( ctrlp == NULL || ( ber = der_alloc()) == NULL ) {
	return( LDAP_OPERATIONS_ERROR );
    }

    *ctrlp = NULL;

    if (( rc = ber_printf( ber, "{e", chgtype )) != -1 ) {
	if ( chgtype == LDAP_CHANGETYPE_MODDN ) {
	    rc = ber_printf( ber, "s", prevdn );
	}
	if ( rc != -1 && chgnum != 0 ) {
	    rc = ber_printf( ber, "i", chgnum );
	}
	if ( rc != -1 ) {
	    rc = ber_printf( ber, "}" );
	}
    }

    if ( rc != -1 ) {
	rc = ber_flatten( ber, &bvp );
    }
    ber_free( ber, 1 );

    if ( rc == -1 ) {
	return( LDAP_OPERATIONS_ERROR );
    }

    *ctrlp = (LDAPControl *)slapi_ch_malloc( sizeof( LDAPControl ));
    (*ctrlp)->ldctl_iscritical = 0;
    (*ctrlp)->ldctl_oid = slapi_ch_strdup( LDAP_CONTROL_ENTRYCHANGE );
    (*ctrlp)->ldctl_value = *bvp;	/* struct copy */

	bvp->bv_val = NULL;
	ber_bvfree( bvp );

    return( LDAP_SUCCESS );
}

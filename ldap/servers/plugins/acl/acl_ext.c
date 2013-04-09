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

#include 	"acl.h"

static void acl__done_aclpb ( struct acl_pblock *aclpb );
#ifdef FOR_DEBUGGING
static void	acl__dump_stats ( struct acl_pblock *aclpb , const char *block_type);
static char * acl__get_aclpb_type ( Acl_PBlock *aclpb );
#endif
static Acl_PBlock * acl__get_aclpb_from_pool ( );
static int acl__put_aclpb_back_to_pool ( Acl_PBlock *aclpb );
static Acl_PBlock * acl__malloc_aclpb ( );
static void acl__free_aclpb ( Acl_PBlock **aclpb_ptr);
static PRLock *aclext_get_lock ();


struct acl_pbqueue {
	Acl_PBlock			*aclq_free;
	Acl_PBlock			*aclq_busy;
	short				aclq_nfree;
	short				aclq_nbusy;
	PRLock				*aclq_lock;
};
typedef struct acl_pbqueue Acl_PBqueue;

static Acl_PBqueue	*aclQueue;

/* structure with information for each extension */
typedef struct acl_ext
{
	char 	*object_name;      /* name of the object extended   */
	int 	object_type;       /* handle to the extended object */
	int 	handle;            /* extension handle              */
} acl_ext;

static acl_ext acl_ext_list [ACL_EXT_ALL];

/*
 * EXTENSION  INITIALIZATION, CONSTRUCTION, & DESTRUCTION
 *
 */
int
acl_init_ext ()
{
	int rc;

	acl_ext_list[ACL_EXT_OPERATION].object_name = SLAPI_EXT_OPERATION;

	rc = slapi_register_object_extension(plugin_name, SLAPI_EXT_OPERATION,
                                         acl_operation_ext_constructor,
                                         acl_operation_ext_destructor,
                                         &acl_ext_list[ACL_EXT_OPERATION].object_type,
                                         &acl_ext_list[ACL_EXT_OPERATION].handle);

	if ( rc != 0 ) return rc;

	acl_ext_list[ACL_EXT_CONNECTION].object_name = SLAPI_EXT_CONNECTION;
	rc = slapi_register_object_extension(plugin_name, SLAPI_EXT_CONNECTION,
                                         acl_conn_ext_constructor,
                                         acl_conn_ext_destructor,
                                         &acl_ext_list[ACL_EXT_CONNECTION].object_type,
                                         &acl_ext_list[ACL_EXT_CONNECTION].handle);

	return rc;

}

/* Interface to get the extensions */
void * 
acl_get_ext (ext_type type, void *object)
{
		struct acl_ext 	ext;
		void			*data;

		if ( type >= ACL_EXT_ALL ) {
			slapi_log_error ( SLAPI_LOG_ACL, plugin_name, 
								"Invalid extension type:%d\n", type );
			return NULL;
		} 

        /* find the requested extension */
        ext = acl_ext_list [type];
        data = slapi_get_object_extension(ext.object_type, object, ext.handle);

        return data;
}

void
acl_set_ext (ext_type type, void *object, void *data)
{
	if ( type >= 0 && type < ACL_EXT_ALL )
	{
		struct acl_ext ext = acl_ext_list [type];
		slapi_set_object_extension ( ext.object_type, object, ext.handle, data );
	}
}

/****************************************************************************
 * Global lock array so that private extension between connection and operation 
 * co-exist
 *
 ******************************************************************************/
struct ext_lockArray {
	PRLock 		**lockArray;
	int		 	numlocks;
};

static struct ext_lockArray extLockArray;

/* PKBxxx: make this a configurable. Start with 2 * maxThreads */
#define ACLEXT_MAX_LOCKS 40

int
aclext_alloc_lockarray ( )
{

	int		i;
	PRLock	*lock;

	extLockArray.lockArray = 
			(PRLock **) slapi_ch_calloc ( ACLEXT_MAX_LOCKS, sizeof ( PRLock *) );

	for ( i =0; i < ACLEXT_MAX_LOCKS; i++) {
		if (NULL == (lock = PR_NewLock()) ) {
			slapi_log_error( SLAPI_LOG_FATAL, plugin_name, 
			   "Unable to allocate locks used for private extension\n");
			return 1;
		}
		extLockArray.lockArray[i] = lock;
	}
	extLockArray.numlocks = ACLEXT_MAX_LOCKS;
	return 0;
}
static PRUint32 slot_id =0;
static PRLock *
aclext_get_lock ()
{

	PRUint16 slot = slot_id % ACLEXT_MAX_LOCKS;
	slot_id++;
	return ( extLockArray.lockArray[slot] );

}
/****************************************************************************/
/* CONNECTION EXTENSION SPECIFIC											*/
/****************************************************************************/
void *
acl_conn_ext_constructor ( void *object, void *parent )
{
	struct acl_cblock *ext = NULL;

	ext = (struct acl_cblock * ) slapi_ch_calloc (1, sizeof (struct acl_cblock ) );
	if (( ext->aclcb_lock = aclext_get_lock () ) == NULL ) {
 		slapi_log_error( SLAPI_LOG_FATAL, plugin_name,
              		"Unable to get Read/Write lock for CONNECTION extension\n");
		slapi_ch_free ( (void **) &ext );
		return NULL;
	}
	ext->aclcb_sdn = slapi_sdn_new ();
	/* store the signatures */
	ext->aclcb_aclsignature = acl_get_aclsignature();
	/* eval_context */
	ext->aclcb_eval_context.acle_handles_matched_target = (int *)
                    slapi_ch_calloc (aclpb_max_selected_acls, sizeof (int));
	ext->aclcb_state =  -1;
	return ext;


}

void
acl_conn_ext_destructor ( void *ext, void *object, void *parent )
{
	struct acl_cblock	*aclcb  = ext;
	PRLock				*shared_lock;

	if ( NULL == aclcb )  return;
	PR_Lock ( aclcb->aclcb_lock );
	shared_lock = aclcb->aclcb_lock;
	acl_clean_aclEval_context ( &aclcb->aclcb_eval_context, 0 /* clean*/ );
	slapi_sdn_free ( &aclcb->aclcb_sdn );
	slapi_ch_free ( (void **)&(aclcb->aclcb_eval_context.acle_handles_matched_target));
	aclcb->aclcb_lock = NULL;
	slapi_ch_free ( (void **) &aclcb );

	PR_Unlock ( shared_lock );
}

/****************************************************************************/
/* OPERATION EXTENSION SPECIFIC												*/
/****************************************************************************/
void *
acl_operation_ext_constructor ( void *object, void *parent )
{
	Acl_PBlock *aclpb = NULL;

	TNF_PROBE_0_DEBUG(acl_operation_ext_constructor_start ,"ACL","");

	/* This means internal operations */
	if ( NULL == parent) {

		TNF_PROBE_1_DEBUG(acl_operation_ext_constructor_end ,"ACL","",
							tnf_string,internal_op,"");

	 	return NULL;
	}

	aclpb = acl__get_aclpb_from_pool();
	if ( NULL == aclpb ) {
		slapi_log_error ( SLAPI_LOG_FATAL, plugin_name, 
						"Operation extension allocation Failed\n");
	}

	TNF_PROBE_0_DEBUG(acl_operation_ext_constructor_end ,"ACL","");

	return   aclpb;

}

void
acl_operation_ext_destructor ( void *ext, void *object, void *parent )
{

	struct acl_cblock	*aclcb  = NULL;
	struct acl_pblock	*aclpb = NULL;

	TNF_PROBE_0_DEBUG(acl_operation_ext_destructor_start ,"ACL","");

	if ( (NULL == parent ) || (NULL == ext)) {
		TNF_PROBE_1_DEBUG(acl_operation_ext_destructor_end ,"ACL","",
							tnf_string,internal_op,"");

		return;
	}

	aclpb = (Acl_PBlock *) ext;

	if ( (NULL == aclpb) || 
		 (NULL == aclpb->aclpb_pblock) ||
		(!(aclpb->aclpb_state & ACLPB_INITIALIZED)))
		goto clean_aclpb;

	if ( NULL == aclpb->aclpb_authorization_sdn ) {
		slapi_log_error (SLAPI_LOG_FATAL, plugin_name, "NULL aclcb_autorization_sdn\n");
		goto clean_aclpb;
	}

	/* get the connection  extension */
	aclcb = (struct acl_cblock *) acl_get_ext ( ACL_EXT_CONNECTION, parent );

	/* We are about to get out of this connection. Move all the
	** cached information to the acl private block which hangs
	** from the connection struct.
	*/
	if ( aclcb && aclcb->aclcb_lock &&
		( (aclpb->aclpb_state & ACLPB_UPD_ACLCB_CACHE ) ||
		(aclpb->aclpb_state & ACLPB_INCR_ACLCB_CACHE ) ) ) {

		aclEvalContext		*c_evalContext;
		int					attr_only = 0;
		PRLock			*shared_lock = aclcb->aclcb_lock;

		if (aclcb->aclcb_lock ) PR_Lock ( shared_lock );
		else {
			goto clean_aclpb;
		}
		if ( !aclcb->aclcb_lock ) {
			slapi_log_error (SLAPI_LOG_FATAL, plugin_name, "aclcb lock released! aclcb cache can't be refreshed\n");
			PR_Unlock ( shared_lock );
			goto clean_aclpb;
		}

		/* We need to refresh the aclcb cache */
		if ( aclpb->aclpb_state & ACLPB_UPD_ACLCB_CACHE )
			acl_clean_aclEval_context ( &aclcb->aclcb_eval_context, 0 /* clean*/ );
			if ( aclpb->aclpb_prev_entryEval_context.acle_numof_attrs ) {
				c_evalContext = &aclpb->aclpb_prev_entryEval_context;
			} else {
				c_evalContext = &aclpb->aclpb_curr_entryEval_context;
			}

			if (( aclpb->aclpb_state & ACLPB_INCR_ACLCB_CACHE ) &&
				! ( aclpb->aclpb_state & ACLPB_UPD_ACLCB_CACHE ))
				attr_only = 1;

			acl_copyEval_context ( NULL, c_evalContext, &aclcb->aclcb_eval_context, attr_only );

			aclcb->aclcb_aclsignature = aclpb->aclpb_signature;
			if ( aclcb->aclcb_sdn &&
					(0 != slapi_sdn_compare ( aclcb->aclcb_sdn,
										aclpb->aclpb_authorization_sdn ) ) ) {
				slapi_sdn_set_ndn_byval( aclcb->aclcb_sdn,
					slapi_sdn_get_ndn ( aclpb->aclpb_authorization_sdn ) );
			}
			aclcb->aclcb_state = 0;
			aclcb->aclcb_state |= ACLCB_HAS_CACHED_EVALCONTEXT;
		
			PR_Unlock ( shared_lock );
	}

clean_aclpb:
	if ( aclpb ) {

		if ( aclpb->aclpb_proxy ) {
			TNF_PROBE_0_DEBUG(acl_proxy_aclpbdoneback_start ,"ACL","");

			acl__done_aclpb( aclpb->aclpb_proxy );

			/* Put back to the Pool */
			acl__put_aclpb_back_to_pool ( aclpb->aclpb_proxy );
			aclpb->aclpb_proxy = NULL;
			TNF_PROBE_0_DEBUG(acl_proxy_aclpbdoneback_end ,"ACL","");

		}
		
		TNF_PROBE_0_DEBUG(acl_aclpbdoneback_start ,"ACL","");

		acl__done_aclpb( aclpb);
		acl__put_aclpb_back_to_pool ( aclpb );

		TNF_PROBE_0_DEBUG(acl_aclpbdoneback_end ,"ACL","");

	}

	TNF_PROBE_0_DEBUG(acl_operation_ext_destructor_end ,"ACL","");

}

/****************************************************************************/
/* FUNCTIONS TO MANAGE THE ACLPB POOL									    */
/****************************************************************************/

/*
 * Get the right  acl pblock
 */
struct acl_pblock *
acl_get_aclpb (  Slapi_PBlock *pb, int type )
{
	Acl_PBlock			*aclpb = NULL;
	void				*op = NULL;

	slapi_pblock_get ( pb, SLAPI_OPERATION, &op );
	aclpb = (Acl_PBlock *) acl_get_ext ( ACL_EXT_OPERATION, op );
	if (NULL == aclpb ) return NULL;

	if ( type == ACLPB_BINDDN_PBLOCK )
		return aclpb;
	else if ( type == ACLPB_PROXYDN_PBLOCK )
		return aclpb->aclpb_proxy;
	else
		slapi_log_error ( SLAPI_LOG_FATAL, plugin_name,
						"acl_get_aclpb: Invalid aclpb type %d\n", type );
	return NULL;
}
/*
 * Create a new proxy acl pblock 
 *
 */
struct acl_pblock *
acl_new_proxy_aclpb( Slapi_PBlock *pb )
{
	void				*op;
	Acl_PBlock			*aclpb = NULL;
	Acl_PBlock			*proxy_aclpb = NULL;

	slapi_pblock_get ( pb, SLAPI_OPERATION, &op );
	aclpb = (Acl_PBlock *) acl_get_ext ( ACL_EXT_OPERATION, op );
	if (NULL == aclpb ) return NULL;

	proxy_aclpb = acl__get_aclpb_from_pool();
	if (NULL == proxy_aclpb) return NULL;
	proxy_aclpb->aclpb_type = ACLPB_TYPE_PROXY;

	aclpb->aclpb_proxy = proxy_aclpb;

	return proxy_aclpb;

}
static int
acl__handle_config_entry (Slapi_Entry *e,  void *callback_data )
{
	*(int * )callback_data = slapi_entry_attr_get_int( e, "nsslapd-threadnumber");
		
	return 0;
}

static int
acl__handle_plugin_config_entry (Slapi_Entry *e,  void *callback_data )
{
    int value = slapi_entry_attr_get_int(e, ATTR_ACLPB_MAX_SELECTED_ACLS);
    if (value) {
        aclpb_max_selected_acls = value;
        aclpb_max_cache_results = value;
    } else {
        aclpb_max_selected_acls = DEFAULT_ACLPB_MAX_SELECTED_ACLS;
        aclpb_max_cache_results = DEFAULT_ACLPB_MAX_SELECTED_ACLS;
    }

    return 0;
}

/*
 * Create a pool of acl pblock. Created during the  ACL plugin
 * initialization.
 */
int
acl_create_aclpb_pool ()
{
	Acl_PBlock			*aclpb;
	Acl_PBlock			*prev_aclpb;
	Acl_PBlock			*first_aclpb;
	int					i;
	int maxThreads= 0;
	int callbackData= 0;

	slapi_search_internal_callback( "cn=config", LDAP_SCOPE_BASE, "(objectclass=*)",
                            NULL, 0 /* attrsonly */,
                            &maxThreads/* callback_data */,
                            NULL /* controls */,
                            NULL /* result_callback */,
                            acl__handle_config_entry,
                            NULL /* referral_callback */);

	slapi_search_internal_callback( ACL_PLUGIN_CONFIG_ENTRY_DN, LDAP_SCOPE_BASE, "(objectclass=*)",
                            NULL, 0 /* attrsonly */,
                            &callbackData /* callback_data, not used in this case */,
                            NULL /* controls */,
                            NULL /* result_callback */,
                            acl__handle_plugin_config_entry,
                            NULL /* referral_callback */);

	/* Create a pool pf aclpb */
	maxThreads =  2 * maxThreads;

	aclQueue = ( Acl_PBqueue *) slapi_ch_calloc ( 1, sizeof (Acl_PBqueue) );
	aclQueue->aclq_lock = PR_NewLock();

	if ( NULL == aclQueue->aclq_lock ) {
		/* ERROR */
		return 1;
	}

	prev_aclpb = NULL;
	first_aclpb = NULL;
	for ( i = 0; i < maxThreads; i++ ) {
		aclpb = acl__malloc_aclpb ();
		if ( 0 == i) first_aclpb = aclpb;

		aclpb->aclpb_prev = prev_aclpb;
		if ( prev_aclpb ) prev_aclpb->aclpb_next = aclpb;
		prev_aclpb = aclpb;
	}

	/* Since this is the begining, everybody is in free list */
	aclQueue->aclq_free = first_aclpb;

	aclQueue->aclq_nfree = maxThreads;
	return 0;
}

/*
 * Destroys the Acl_PBlock pool. To be called at shutdown,
 * from function registered as SLAPI_PLUGIN_CLOSE_FN
 */
void
acl_destroy_aclpb_pool ()
{
    Acl_PBlock      *currentPbBlock;
    Acl_PBlock      *nextPbBlock;

    if (!aclQueue) {
        /* Nothing to do */
        return;
    }

    /* Free all busy pbBlocks in queue */
    currentPbBlock = aclQueue->aclq_busy;
    while (currentPbBlock) {
        nextPbBlock = currentPbBlock->aclpb_next;
        acl__free_aclpb(&currentPbBlock);
        currentPbBlock = nextPbBlock;
    }

    /* Free all free pbBlocks in queue */
    currentPbBlock = aclQueue->aclq_free;
    while (currentPbBlock) {
        nextPbBlock = currentPbBlock->aclpb_next;
        acl__free_aclpb(&currentPbBlock);
        currentPbBlock = nextPbBlock;
    }

    slapi_ch_free((void**)&aclQueue);
}

/*
 * Get a FREE acl pblock from the pool.
 *
 */
static Acl_PBlock *
acl__get_aclpb_from_pool ( )
{
	Acl_PBlock		*aclpb = NULL;
	Acl_PBlock		*t_aclpb = NULL;


	PR_Lock (aclQueue->aclq_lock );
	
	/*  Get the first aclpb from the FREE List */
	aclpb = aclQueue->aclq_free;
	if ( aclpb ) {
		t_aclpb = aclpb->aclpb_next;
		if ( t_aclpb ) t_aclpb->aclpb_prev = NULL;
		aclQueue->aclq_free = t_aclpb;
		
		/* make the this an orphon */
		aclpb->aclpb_prev = aclpb->aclpb_next = NULL;

		aclQueue->aclq_nfree--;		
	} else {
		slapi_log_error ( SLAPI_LOG_ACL, plugin_name,
				"Unable to find a free aclpb\n");
		aclpb = acl__malloc_aclpb ();
	}

	
	/* Now move it to the FRONT of busy list */
	t_aclpb = aclQueue->aclq_busy;
	aclpb->aclpb_next = t_aclpb;
	if ( t_aclpb ) t_aclpb->aclpb_prev = aclpb;
	aclQueue->aclq_busy = aclpb;
	aclQueue->aclq_nbusy++;		

	PR_Unlock (aclQueue->aclq_lock );

	return aclpb;
}
/*
 * Put the acl pblock into the FREE pool.
 *
 */
static int
acl__put_aclpb_back_to_pool ( Acl_PBlock *aclpb )
{

	Acl_PBlock		*p_aclpb, *n_aclpb;

	PR_Lock (aclQueue->aclq_lock );

	/* Remove it from the busy list */
	n_aclpb = aclpb->aclpb_next;
	p_aclpb = aclpb->aclpb_prev;

	if ( p_aclpb ) {
		p_aclpb->aclpb_next = n_aclpb;
		if ( n_aclpb ) n_aclpb->aclpb_prev = p_aclpb;
	} else {
		aclQueue->aclq_busy = n_aclpb;
		if ( n_aclpb ) n_aclpb->aclpb_prev = NULL;
	}
	aclQueue->aclq_nbusy--;


	/* Put back to the FREE list */
	aclpb->aclpb_prev = NULL;
	n_aclpb = aclQueue->aclq_free;
	aclpb->aclpb_next = n_aclpb;
	if ( n_aclpb )  n_aclpb->aclpb_prev = aclpb;
	aclQueue->aclq_free = aclpb;
	aclQueue->aclq_nfree++;		

	PR_Unlock (aclQueue->aclq_lock );

	return 0;
}

/*
 * Allocate the basic acl pb
 *
 */
static Acl_PBlock *
acl__malloc_aclpb ( )
{
	Acl_PBlock		*aclpb = NULL;


	aclpb = ( Acl_PBlock *) slapi_ch_calloc ( 1, sizeof ( Acl_PBlock) );
	
	/* Now set the propert we need  for ACL evaluations */
	if ((aclpb->aclpb_proplist = PListNew(NULL)) == NULL) {
		 slapi_log_error (SLAPI_LOG_FATAL, plugin_name,
			    "Unable to allocate the aclprop PList\n");
			goto error;
	}

	if (PListInitProp(aclpb->aclpb_proplist, 0, DS_PROP_ACLPB, aclpb, 0) < 0) {
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name, 
					"Unable to set the ACL PBLOCK in the Plist\n");
		goto error;
	}
	if (PListInitProp(aclpb->aclpb_proplist, 0, DS_ATTR_USERDN, aclpb, 0) < 0) {
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name, 
					"Unable to set the USER DN in the Plist\n");
		goto error;
	}
	if (PListInitProp(aclpb->aclpb_proplist, 0, DS_ATTR_AUTHTYPE, aclpb, 0) < 0) {
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name, 
					"Unable to set the AUTH TYPE in the Plist\n");
		goto error;
	}
	if (PListInitProp(aclpb->aclpb_proplist, 0, DS_ATTR_LDAPI, aclpb, 0) < 0) {
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
					"Unable to set the AUTH TYPE in the Plist\n");
		goto error;
	}
	if (PListInitProp(aclpb->aclpb_proplist, 0, DS_ATTR_ENTRY, aclpb, 0) < 0) {
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name, 
					"Unable to set the ENTRY TYPE in the Plist\n");
		goto error;
	}
	if (PListInitProp(aclpb->aclpb_proplist, 0, DS_ATTR_SSF, aclpb, 0) < 0) {
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
					"Unable to set the SSF in the Plist\n");
		goto error;
	}

	/* 
	 * ACL_ATTR_IP and ACL_ATTR_DNS are initialized lazily in the
	 * IpGetter and DnsGetter functions.
	 * They are removed from the aclpb property list at acl__aclpb_done()
	 * time.
	*/
            
	/* allocate the acleval struct */
	aclpb->aclpb_acleval = (ACLEvalHandle_t *) ACL_EvalNew(NULL, NULL);
	if (aclpb->aclpb_acleval == NULL) {
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name, 
							"Unable to allocate the acleval block\n");
		goto error;
	}
	/*
     * This is a libaccess routine.
     * Need to setup subject and resource property information
    */
	
    ACL_EvalSetSubject(NULL, aclpb->aclpb_acleval, aclpb->aclpb_proplist);

	/* allocate some space for  attr name  */
	aclpb->aclpb_Evalattr = (char *) slapi_ch_malloc (ACLPB_MAX_ATTR_LEN);
		 
	aclpb->aclpb_deny_handles = (aci_t **) slapi_ch_calloc (1,
						       ACLPB_INCR_LIST_HANDLES * sizeof (aci_t *));

	aclpb->aclpb_allow_handles = (aci_t **) slapi_ch_calloc (1,
						       ACLPB_INCR_LIST_HANDLES * sizeof (aci_t *));

	aclpb->aclpb_deny_handles_size = ACLPB_INCR_LIST_HANDLES;
	aclpb->aclpb_allow_handles_size = ACLPB_INCR_LIST_HANDLES;

	/* allocate the array for bases */
	aclpb->aclpb_grpsearchbase = (char **)
				slapi_ch_malloc (ACLPB_INCR_BASES * sizeof(char *));
	aclpb->aclpb_grpsearchbase_size = ACLPB_INCR_BASES;
	aclpb->aclpb_numof_bases = 0;

	/* Make sure aclpb_search_base is initialized to NULL..tested elsewhere! */
	aclpb->aclpb_search_base = NULL;

	aclpb->aclpb_authorization_sdn = slapi_sdn_new ();
	aclpb->aclpb_curr_entry_sdn = slapi_sdn_new();

	aclpb->aclpb_aclContainer = acllist_get_aciContainer_new ();

	/* hash table to store macro matched values from targets */
	aclpb->aclpb_macro_ht = acl_ht_new();

    /* allocate arrays for handles */
    aclpb->aclpb_handles_index = (int *)
                slapi_ch_calloc (aclpb_max_selected_acls, sizeof (int));
    aclpb->aclpb_base_handles_index = (int *)
                slapi_ch_calloc (aclpb_max_selected_acls, sizeof (int));

    /* allocate arrays for result cache */
    aclpb->aclpb_cache_result = (r_cache_t *)
            slapi_ch_calloc (aclpb_max_cache_results, sizeof (r_cache_t));

    /* allocate arrays for target handles in eval_context */
    aclpb->aclpb_curr_entryEval_context.acle_handles_matched_target = (int *)
                slapi_ch_calloc (aclpb_max_selected_acls, sizeof (int));
    aclpb->aclpb_prev_entryEval_context.acle_handles_matched_target = (int *)
                slapi_ch_calloc (aclpb_max_selected_acls, sizeof (int));
    aclpb->aclpb_prev_opEval_context.acle_handles_matched_target = (int *)
                slapi_ch_calloc (aclpb_max_selected_acls, sizeof (int));

    return aclpb;

error:
    acl__free_aclpb(&aclpb);

	return NULL;
}

/*
 * Free the acl pb. To be used at shutdown (SLAPI_PLUGIN_CLOSE_FN)
 * when we free the aclQueue
 */
static void
acl__free_aclpb ( Acl_PBlock **aclpb_ptr)
{
    Acl_PBlock      *aclpb = NULL;

    if (aclpb_ptr == NULL || *aclpb_ptr == NULL)
        return; // Nothing to do

    aclpb = *aclpb_ptr;

    if (aclpb->aclpb_acleval) {
        ACL_EvalDestroyNoDecrement(NULL, NULL, aclpb->aclpb_acleval);
    }

    if (aclpb->aclpb_proplist)
        PListDestroy(aclpb->aclpb_proplist);

    slapi_ch_free((void**)&(aclpb->aclpb_handles_index));
    slapi_ch_free((void**)&(aclpb->aclpb_base_handles_index));
    slapi_ch_free((void**)&(aclpb->aclpb_cache_result));
    slapi_ch_free((void**)
           &(aclpb->aclpb_curr_entryEval_context.acle_handles_matched_target));
    slapi_ch_free((void**)
           &(aclpb->aclpb_prev_entryEval_context.acle_handles_matched_target));
    slapi_ch_free((void**)
           &(aclpb->aclpb_prev_opEval_context.acle_handles_matched_target));

    slapi_ch_free((void**)aclpb_ptr);
}

/* Initializes the aclpb */
void 
acl_init_aclpb ( Slapi_PBlock *pb, Acl_PBlock *aclpb, const char *ndn, int copy_from_aclcb)
{
	struct acl_cblock	*aclcb = NULL;
	char				*authType;
	void				*conn;
	int					op_type;
	intptr_t			ssf = 0;
	

	if ( NULL == aclpb ) {
		slapi_log_error ( SLAPI_LOG_FATAL, plugin_name, "acl_init_aclpb:No ACLPB\n");
		return;
	}

	/* See if we have initialized already */
	if (aclpb->aclpb_state & ACLPB_INITIALIZED) return;

	slapi_pblock_get ( pb, SLAPI_OPERATION_TYPE, &op_type );
	if ( op_type == SLAPI_OPERATION_BIND || op_type == SLAPI_OPERATION_UNBIND )
		return;

	/* We indicate the initialize here becuase, if something goes wrong, it's cleaned up
	** properly.
	*/
	aclpb->aclpb_state = ACLPB_INITIALIZED;

	/* We make an anonymous user a non null dn which is empty */
	if (ndn && *ndn != '\0' ) 
		slapi_sdn_set_ndn_byval ( aclpb->aclpb_authorization_sdn, ndn );
	else
		slapi_sdn_set_ndn_byval ( aclpb->aclpb_authorization_sdn, "" );

	/* reset scoped entry cache to be empty */
	aclpb->aclpb_scoped_entry_anominfo.anom_e_nummatched = 0;

	if (PListAssignValue(aclpb->aclpb_proplist, DS_ATTR_USERDN,
					slapi_sdn_get_ndn(aclpb->aclpb_authorization_sdn), 0) < 0) {
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
						"Unable to set the USER DN in the Plist\n");
		return;
	}
	slapi_pblock_get ( pb, SLAPI_OPERATION_AUTHTYPE, &authType );
	if (PListAssignValue(aclpb->aclpb_proplist, DS_ATTR_AUTHTYPE, authType, 0) < 0) {
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name, 
				"Unable to set the AUTH TYPE in the Plist\n");
		return;
	}
	if(slapi_is_ldapi_conn(pb)){
		if(PListAssignValue(aclpb->aclpb_proplist, DS_ATTR_LDAPI, "yes", 0) < 0){
			slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
					"Unable to set the AUTH TYPE in the Plist\n");
			return;
		}
	}
	slapi_pblock_get ( pb, SLAPI_OPERATION_SSF, &ssf);
	if (PListAssignValue(aclpb->aclpb_proplist, DS_ATTR_SSF, (const void *)ssf, 0) < 0) {
		slapi_log_error(SLAPI_LOG_FATAL, plugin_name,
				"Unable to set the SSF in the Plist\n");
		return;
	}

	/* PKBxxx: We should be getting it from the OP struct */
	slapi_pblock_get ( pb, SLAPI_CONN_CERT, &aclpb->aclpb_clientcert );

	/* See if the we have already a cached info about user's group */
	aclg_init_userGroup ( aclpb, ndn, 0 /* get lock */ );

	slapi_pblock_get( pb, SLAPI_BE_MAXNESTLEVEL, &aclpb->aclpb_max_nesting_level );
	slapi_pblock_get( pb, SLAPI_SEARCH_SIZELIMIT, &aclpb->aclpb_max_member_sizelimit );
	if ( aclpb->aclpb_max_member_sizelimit == 0 ) {
		aclpb->aclpb_max_member_sizelimit = SLAPD_DEFAULT_LOOKTHROUGHLIMIT;
	}
	slapi_pblock_get( pb, SLAPI_OPERATION_TYPE, &aclpb->aclpb_optype );

	aclpb->aclpb_signature = acl_get_aclsignature();
	aclpb->aclpb_last_cache_result = 0;
	aclpb->aclpb_pblock = pb;
	PR_ASSERT ( aclpb->aclpb_pblock != NULL );

	/* get the connection */
	slapi_pblock_get ( pb, SLAPI_CONNECTION, &conn);
	aclcb = (struct acl_cblock *) acl_get_ext ( ACL_EXT_CONNECTION, conn );

	if (NULL == aclcb || NULL == aclcb->aclcb_lock) {
		/* This could happen if the client is dead and we are in
		** process of abondoning this operation
		*/
		slapi_log_error( SLAPI_LOG_ACL, plugin_name,
					"No CONNECTION extension\n");

	} else if ( aclcb->aclcb_state == -1 ) {
		/* indicate that we need to update the cache */
		aclpb->aclpb_state |= ACLPB_UPD_ACLCB_CACHE;
		aclcb->aclcb_state = 0; /* Nore this is ACLCB and not ACLPB */

	} else if ( copy_from_aclcb ){
		char		*cdn;
		Slapi_DN	*c_sdn; /* client SDN */

		/* check if the operation is abandoned or not.*/
		if (  slapi_op_abandoned ( pb ) ) {
			return;
		}

		slapi_pblock_get ( pb, SLAPI_CONN_DN, &cdn ); /* We *must* free cdn! */
		c_sdn = slapi_sdn_new_dn_passin( cdn );
		PR_Lock ( aclcb->aclcb_lock );
		/*
		 * since PR_Lock is taken,
		 * we can mark the connection extension ok to be destroyed.
		 */
		if  ( (aclcb->aclcb_aclsignature != acl_get_aclsignature()) || 
			( (NULL == cdn)  && aclcb->aclcb_sdn ) ||
			(cdn && (NULL == aclcb->aclcb_sdn )) ||
			(cdn && aclcb->aclcb_sdn && ( 0 != slapi_sdn_compare ( c_sdn, aclcb->aclcb_sdn ) ))) {

			/* cleanup the aclcb cache */
			acl_clean_aclEval_context ( &aclcb->aclcb_eval_context, 0 /*clean*/ );
			aclcb->aclcb_state = 0;
			aclcb->aclcb_aclsignature = 0;
			slapi_sdn_done ( aclcb->aclcb_sdn );
		}
		slapi_sdn_free ( &c_sdn );
		
		/* COPY the cached information from ACLCB --> ACLPB */
		if ( aclcb->aclcb_state & ACLCB_HAS_CACHED_EVALCONTEXT) {
			acl_copyEval_context ( aclpb, &aclcb->aclcb_eval_context , 
						&aclpb->aclpb_prev_opEval_context, 0 );
			aclpb->aclpb_state |= ACLPB_HAS_ACLCB_EVALCONTEXT;
		}
		PR_Unlock ( aclcb->aclcb_lock );
	}
	
}

/* Cleans up the aclpb */
static void 
acl__done_aclpb ( struct acl_pblock *aclpb )
{

	int		i;
	int		dump_aclpb_info = 0;
	int 	rc=-1;
	char 	*tmp_ptr=NULL;

	/* 
	** First, let's do some sanity checks to see if we have everything what 
	** it should be.
	*/		

	/* Nothing needs to be cleaned up in this case */
	if (!(aclpb->aclpb_state & ACLPB_INITIALIZED))
		return;

	/* Check the state */
	if (aclpb->aclpb_state & ~ACLPB_STATE_ALL) {
		slapi_log_error( SLAPI_LOG_FATAL, plugin_name, 
			   "The aclpb.state value (%d) is incorrect. Exceeded the limit (%d)\n",
			   aclpb->aclpb_state, ACLPB_STATE_ALL);
		dump_aclpb_info = 1;

	}

#ifdef FOR_DEBUGGING
	acl__dump_stats ( aclpb, acl__get_aclpb_type(aclpb));
#endif

	/* reset the usergroup cache */
	aclg_reset_userGroup ( aclpb );

	if ( aclpb->aclpb_res_type & ~ACLPB_RESTYPE_ALL ) {
		slapi_log_error( SLAPI_LOG_FATAL, plugin_name, 
			   "The aclpb res_type value (%d) has exceeded. Limit is (%d)\n",
				aclpb->aclpb_res_type, ACLPB_RESTYPE_ALL );
		dump_aclpb_info = 1;
	}

	if ( dump_aclpb_info ) {
		const char *ndn;
		slapi_log_error ( SLAPI_LOG_FATAL, plugin_name,
							"ACLPB value is:%p\n", aclpb );
	
		ndn = slapi_sdn_get_ndn ( aclpb->aclpb_curr_entry_sdn );	
		slapi_log_error ( SLAPI_LOG_FATAL, plugin_name, "curr_entry:%p  num_entries:%d curr_dn:%p\n", 
				aclpb->aclpb_curr_entry ? (char *) aclpb->aclpb_curr_entry : "NULL", 
				aclpb->aclpb_num_entries, 
				ndn ? ndn : "NULL");

		slapi_log_error ( SLAPI_LOG_FATAL, plugin_name, "Last attr:%p, Plist:%p acleval: %p\n",
					aclpb->aclpb_Evalattr ? aclpb->aclpb_Evalattr : "NULL",
					aclpb->aclpb_proplist ? (char *) aclpb->aclpb_proplist : "NULL",
					aclpb->aclpb_acleval  ? (char *) aclpb->aclpb_acleval : "NULL" );
	}

	/* Now Free the contents or clean it */
	slapi_sdn_done ( aclpb->aclpb_curr_entry_sdn );
	if (aclpb->aclpb_Evalattr) 
		aclpb->aclpb_Evalattr[0] = '\0';

	/* deallocate the contents of the base array */
	for (i=0; i < aclpb->aclpb_numof_bases; i++) {
		if (aclpb->aclpb_grpsearchbase[i])
			slapi_ch_free (  (void **)&aclpb->aclpb_grpsearchbase[i] );
	}
	aclpb->aclpb_numof_bases = 0;
			
	acl_clean_aclEval_context ( &aclpb->aclpb_prev_opEval_context, 0 /*claen*/ );
	acl_clean_aclEval_context ( &aclpb->aclpb_prev_entryEval_context, 0 /*clean*/ );
	acl_clean_aclEval_context ( &aclpb->aclpb_curr_entryEval_context, 0/*clean*/ );

	if ( aclpb->aclpb_client_entry ) slapi_entry_free ( aclpb->aclpb_client_entry );
	aclpb->aclpb_client_entry = NULL;

	slapi_sdn_done ( aclpb->aclpb_authorization_sdn );
	aclpb->aclpb_pblock = NULL;

	if ( aclpb->aclpb_search_base )
		slapi_ch_free ( (void **) &aclpb->aclpb_search_base );
	for ( i=0; i < aclpb->aclpb_num_deny_handles; i++ )
		aclpb->aclpb_deny_handles[i] = NULL;
	aclpb->aclpb_num_deny_handles = 0;

	for ( i=0; i < aclpb->aclpb_num_allow_handles; i++ )
		aclpb->aclpb_allow_handles[i] = NULL;
	aclpb->aclpb_num_allow_handles = 0;

	/* clear results cache */
	memset((char*)aclpb->aclpb_cache_result, 0,
		sizeof(struct result_cache)*aclpb->aclpb_last_cache_result);
	aclpb->aclpb_last_cache_result = 0;
	aclpb->aclpb_handles_index[0] = -1;
	aclpb->aclpb_base_handles_index[0] = -1;

	aclpb->aclpb_stat_acllist_scanned = 0;
	aclpb->aclpb_stat_aclres_matched = 0;
	aclpb->aclpb_stat_total_entries = 0;
	aclpb->aclpb_stat_anom_list_scanned = 0;
	aclpb->aclpb_stat_num_copycontext = 0;
	aclpb->aclpb_stat_num_copy_attrs = 0;
	aclpb->aclpb_stat_num_tmatched_acls = 0;

	aclpb->aclpb_clientcert = NULL;	
	aclpb->aclpb_proxy = NULL;
														 
	acllist_done_aciContainer ( aclpb->aclpb_aclContainer  );	

	/*
	 * Here, decide which things need to be freed/removed/whatever from the
	 * aclpb_proplist.
	*/

	/*
	 * The DS_ATTR_DNS property contains the name of the client machine.
	 *
	 * The value pointed to by this property is stored in the pblock--it
	 * points to the SLAPI_CLIENT_DNS object.  So, that memory will
	 * be freed elsewhere.
	 * 
	 * It's removed here from the aclpb_proplist as it would be an error to
	 * allow it to persist in the aclpb which is an operation time thing.
	 * If we leave it here the next time this aclpb gets used, the DnsGetter
	 * is not called by LASDnsEval/ACL_GetAttribute() as it thinks the
	 * ACL_ATTR_DNS has already been initialized.
	 * 
	*/

	if ((rc = PListFindValue(aclpb->aclpb_proplist, ACL_ATTR_DNS, 
					(void **)&tmp_ptr, NULL)) > 0)	{
		
		PListDeleteProp(aclpb->aclpb_proplist, rc,  NULL);
	}

	/* reset the LDAPI property */
	PListAssignValue(aclpb->aclpb_proplist, DS_ATTR_LDAPI, NULL, 0);

	/*
	 * Remove the DS_ATTR_IP property from the property list.
	 * The value of this property is just the property pointer
	 * (an unsigned long) so that gets freed too when we delete the
	 * property.
	 * It's removed here from the aclpb_proplist as it would be an error to
	 * allow it to persist in the aclpb which is an operation time thing.
	 * If we leave it here the next time this aclpb gets used, the DnsGetter
	 * is not called by LASIpEval/ACL_GetAttribute() as it thinks the
	 * ACL_ATTR_IP has already been initialized.
	*/

	if ((rc = PListFindValue(aclpb->aclpb_proplist, ACL_ATTR_IP, 
					(void **)&tmp_ptr, NULL)) > 0)	{
		
		PListDeleteProp(aclpb->aclpb_proplist, rc,  NULL);
	}
	
	/*
	 * The DS_ATTR_USERDN value comes from aclpb_authorization_sdn. 
	 * This memory
	 * is freed above using aclpb_authorization_sdn so we don't need to free it here
	 * before overwriting the old value.
	*/
	PListAssignValue(aclpb->aclpb_proplist, DS_ATTR_USERDN, NULL, 0);

	/*
	 * The DS_ATTR_AUTHTYPE value is a pointer into the pblock, so
	 * we do not need to free that memory before overwriting the value.
	*/
	PListAssignValue(aclpb->aclpb_proplist, DS_ATTR_AUTHTYPE, NULL, 0);

	/*
	 * DO NOT overwrite the aclpb pointer--it is initialized at malloc_aclpb
	 * time and is kept within the aclpb.
	 *
	 * PListAssignValue(aclpb->aclpb_proplist, DS_PROP_ACLPB, NULL, 0);
	*/

	/*
	 * The DS_ATTR_ENTRY value was a pointer to the entry being evaluated
	 * by the ACL code.  That entry comes from outside the context of
	 * the acl code and so is dealt with out there.  Ergo, here we can just
	 * lose the pointer to that entry.
	*/
	PListAssignValue(aclpb->aclpb_proplist, DS_ATTR_ENTRY, NULL, 0);

	aclpb->aclpb_signature = 0;

	/* reset scoped entry cache to be empty */
	aclpb->aclpb_scoped_entry_anominfo.anom_e_nummatched = 0;

	/* Free up any of the string values left in the macro ht and remove
	 * the entries.*/
	acl_ht_free_all_entries_and_values(aclpb->aclpb_macro_ht);

	/* Finally, set it to the no use state */	
	aclpb->aclpb_state = 0;

}

#ifdef FOR_DEBUGGING
static char *
acl__get_aclpb_type ( Acl_PBlock *aclpb )
{
	if (aclpb->aclpb_state & ACLPB_TYPE_PROXY)	
		return ACLPB_TYPE_PROXY_STR;

	return ACLPB_TYPE_MAIN_STR;
}

static void
acl__dump_stats ( struct acl_pblock *aclpb , const char *block_type)
{
	PRUint64 			connid = 0;
	int				opid = 0;
	Slapi_PBlock  	*pb = NULL;

	pb = aclpb->aclpb_pblock;
	if ( pb )  {
		slapi_pblock_get ( pb, SLAPI_CONN_ID, &connid );
		slapi_pblock_get ( pb, SLAPI_OPERATION_ID, &opid );
	}

	/* DUMP STAT INFO */
	slapi_log_error( SLAPI_LOG_ACL, plugin_name, 
			"**** ACL OPERATION STAT BEGIN ( aclpb:%p Block type: %s): Conn:%" PRIu64 " Operation:%d  *******\n",
			aclpb, block_type, connid, opid );
	slapi_log_error( SLAPI_LOG_ACL, plugin_name, "\tNumber of entries scanned: %d\n",
					aclpb->aclpb_stat_total_entries);
	slapi_log_error( SLAPI_LOG_ACL, plugin_name, "\tNumber of times ACL List scanned: %d\n",
					aclpb->aclpb_stat_acllist_scanned);
	slapi_log_error( SLAPI_LOG_ACL, plugin_name, "\tNumber of ACLs with target matched:%d\n",
					aclpb->aclpb_stat_num_tmatched_acls);
	slapi_log_error( SLAPI_LOG_ACL, plugin_name, "\tNumber of times acl resource matched:%d\n",
					aclpb->aclpb_stat_aclres_matched);
	slapi_log_error( SLAPI_LOG_ACL, plugin_name, "\tNumber of times ANOM list scanned:%d\n",
					aclpb->aclpb_stat_anom_list_scanned);
	slapi_log_error( SLAPI_LOG_ACL, plugin_name, "\tNumber of times Context was copied:%d\n",
					aclpb->aclpb_stat_num_copycontext);
	slapi_log_error( SLAPI_LOG_ACL, plugin_name, "\tNumber of times Attrs was copied:%d\n",
					aclpb->aclpb_stat_num_copy_attrs);
	slapi_log_error( SLAPI_LOG_ACL, plugin_name, " **** ACL OPERATION STAT END  *******\n");
}
#endif
/****************************************************************************/
/*				E	N	D													*/
/****************************************************************************/
		

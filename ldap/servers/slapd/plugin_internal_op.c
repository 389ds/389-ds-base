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

/* stevross@netscape.com June 13 1997 */

#include <stdio.h>
#include <sys/types.h>
#include "slapi-plugin.h"
#include "slap.h"

#include <ssl.h>

/* entry list node */
typedef struct Entry_Node{
    Slapi_Entry *data;
    struct Entry_Node *next;
} Entry_Node;

/* referral list node */
typedef struct Referral_Node{
    char *data;
    struct Referral_Node *next;
} Referral_Node;

/* data that must be passed to slapi_search_internal_callback */
typedef struct plugin_search_internal_data{
    int rc;
    int num_entries;
    int num_referrals;
    Entry_Node *entry_list_head;
    Referral_Node *referral_list_head;
} plugin_search_internal_data;

/* callback functions */
typedef struct callback_fn_ptrs{
    plugin_result_callback	p_res_callback;
    plugin_search_entry_callback p_srch_entry_callback;
    plugin_referral_entry_callback p_ref_entry_callback;
    void *callback_data;
}callback_fn_ptrs;

/* forward declarations */
static int seq_internal_callback_pb (Slapi_PBlock *pb, void *callback_data, 
									 plugin_result_callback prc, 
									 plugin_search_entry_callback psec, 
									 plugin_referral_entry_callback prec);
static int search_internal_pb (Slapi_PBlock *pb);
static int search_internal_callback_pb (Slapi_PBlock *pb, void *callback_data, plugin_result_callback prc, 
									    plugin_search_entry_callback psec, plugin_referral_entry_callback prec);

void
internal_getresult_callback(struct conn *unused1, 
                             struct op *op,
                             int err,
                             char *unused2, 
                             char *unused3,
                             int unused4, 
                             struct berval **unused5)
{

    if (op != NULL) 
	{
	    *((int *)op->o_handler_data) = err;
    }

}

void 
internal_res_callback(struct conn *unused1, struct op *op, int err, char *unused2, 
					  char *unused3, int unused4, struct berval **unused5)
{
    /* make sure the user has a callback defined, if so do it, otherwise do nothing */
    if(  ((callback_fn_ptrs *)op->o_handler_data) != NULL 
       && ((callback_fn_ptrs *)op->o_handler_data)->p_res_callback != NULL
   )
	{
        ((callback_fn_ptrs *)op->o_handler_data)->p_res_callback
											(err, ((callback_fn_ptrs *)op->o_handler_data)->callback_data);
    }

}

int
internal_srch_entry_callback(Slapi_Backend* be, Connection* conn, 
                             Operation* op, Slapi_Entry* e)
{
    /* make sure the user has a callback defined, if so do it, otherwise do nothing */
    if(  ((callback_fn_ptrs *)op->o_handler_data) != NULL 
       && ((callback_fn_ptrs *)op->o_handler_data)->p_srch_entry_callback != NULL
     )
    {
        return(((callback_fn_ptrs *)op->o_handler_data)->p_srch_entry_callback
												(e, ((callback_fn_ptrs *)op->o_handler_data)->callback_data));
    }

	return(0);
}

int
internal_ref_entry_callback(Slapi_Backend * be, Connection *conn, 
                            Operation *op, struct berval **ireferral)
{

    int i;

    /* make sure the user has a callback defined, if so do it, otherwise do nothing */
    if(  ((callback_fn_ptrs *)op->o_handler_data) != NULL 
       && ((callback_fn_ptrs *)op->o_handler_data)->p_ref_entry_callback != NULL
       && ireferral != NULL  	  
   )
    {
        /* loop over referrals calling callback for each one */
        for(i=0; ireferral[i] != NULL; i++)
        {
            ((callback_fn_ptrs *)op->o_handler_data)->p_ref_entry_callback(ireferral[i]->bv_val, 
														((callback_fn_ptrs *)op->o_handler_data)->callback_data);
        }
    }
    return(0);
}

Slapi_Operation*
internal_operation_new(unsigned long op_type, int flags)
{
    Slapi_Operation *op= operation_new(flags | OP_FLAG_INTERNAL /*Internal*/);
	/* set operation type: add, delete, etc */
	operation_set_type(op,op_type);
	/* Call the plugin extension constructors */
	op->o_extension = factory_create_extension(get_operation_object_type(),op,NULL /* Parent */);
    return op;
}

/******************************************************************************
*
*  do_disconnect_server
*
*
*
*
*******************************************************************************/

/* this is just a wrapper exposed to the plugins */
void
slapi_disconnect_server(Slapi_Connection *conn)
{
    do_disconnect_server(conn, conn->c_connid, -1);
}

static get_disconnect_server_fn_ptr disconnect_server_fn = NULL;

void
do_disconnect_server(Connection *conn, PRUint64 opconnid, int opid)
{
    if (NULL == disconnect_server_fn) {
        if (get_entry_point(ENTRY_POINT_DISCONNECT_SERVER, (caddr_t *)(&disconnect_server_fn)) < 0) {
            return;
        }
    }
    /* It seems that we only call this from result.c when the ber_flush fails. */
    (disconnect_server_fn)(conn, opconnid, opid, SLAPD_DISCONNECT_BER_FLUSH, 0);
}


/******************************************************************************
*
*  slapi_compare_internal
*
*		no plans to implement this, but placeholder incase we change our mind
*  
*
*******************************************************************************/


Slapi_PBlock *
slapi_compare_internal(char * dn, 
                       char *attr, 
                       char *value,
                       LDAPControl **controls)
{
    printf("slapi_compare_internal not yet implemented \n");
    return(0);
}

int
slapi_seq_callback( const char *ibase,
                    int type,
                    char *attrname,
                    char *val,
                    char **attrs,
                    int attrsonly,
                    void *callback_data,
                    LDAPControl **controls,
                    plugin_result_callback res_callback, 
                    plugin_search_entry_callback srch_callback,
                    plugin_referral_entry_callback ref_callback)								
{
	int r;
    Slapi_PBlock pb;

	if (ibase == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, NULL, 
						"slapi_seq_callback: NULL parameter\n");
		return -1;
	}

	pblock_init(&pb);

    slapi_seq_internal_set_pb(&pb, (char *)ibase, type, attrname, val, attrs, attrsonly, controls, 
		plugin_get_default_component_id(), 0);							  

    r= seq_internal_callback_pb (&pb, callback_data, res_callback, srch_callback, ref_callback);
	pblock_done(&pb);
	return r;
}



/* pblock should contain the following data (can be set via call to slapi_seq_internal_set_pb):
		SLAPI_SEARCH_TARGET set to search base
		SAPI_SEQ_TYPE set to sequential access type (SLAPI_SEQ_FIRST, SLAPI_SEQ_NEXT, etc.
		SLAPI_SEQ_ATTRNAME	the next two fields define attribute value assertion
		SLAPI_SEQ_VAL		relative to which access is performed.
		SLAPI_CONTROLS_ARG set to request controls if present
		SLAPI_SEARCH_ATTRS set to the list of attributes to return
		SLAPI_SEARCH_ATTRSONLY tells whether attribute values should be returned. 				
 */

int slapi_seq_internal_callback_pb (Slapi_PBlock *pb, void *callback_data, plugin_result_callback res_callback,
									plugin_search_entry_callback srch_callback, 
									plugin_referral_entry_callback ref_callback)
{
	if (pb == NULL)
		return -1;

	if (!allow_operation (pb))
	{
		send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL,
						 "This plugin is not configured to access operation target data", 0, NULL );
		return 0;
	}

	return (seq_internal_callback_pb (pb, callback_data, res_callback, srch_callback, ref_callback));
}

void
slapi_search_internal_set_pb (Slapi_PBlock *pb, const char *base, 
                              int scope, const char *filter, char **attrs, 
                              int attrsonly, LDAPControl **controls, 
                              const char *uniqueid, 
                              Slapi_ComponentId *plugin_identity,
                              int operation_flags)
{
	Operation *op;
	if (pb == NULL || base == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, NULL, 
		                "slapi_search_internal_set_pb: NULL parameter\n");
		return;
	}

	op= internal_operation_new(SLAPI_OPERATION_SEARCH,operation_flags);
	slapi_pblock_set(pb, SLAPI_OPERATION, op);
	slapi_pblock_set(pb, SLAPI_ORIGINAL_TARGET_DN, (void *)base);
	slapi_pblock_set(pb, SLAPI_SEARCH_SCOPE, &scope);
	slapi_pblock_set(pb, SLAPI_SEARCH_STRFILTER, (void*)filter);
	slapi_pblock_set(pb, SLAPI_CONTROLS_ARG, controls);
	slapi_pblock_set(pb, SLAPI_SEARCH_ATTRS, attrs);
	slapi_pblock_set(pb, SLAPI_SEARCH_ATTRSONLY, &attrsonly);
	if (uniqueid)
	{
		slapi_pblock_set(pb, SLAPI_TARGET_UNIQUEID, (void*)uniqueid);
	}
	slapi_pblock_set(pb, SLAPI_PLUGIN_IDENTITY, (void*)plugin_identity);
}

void
slapi_search_internal_set_pb_ext (Slapi_PBlock *pb, Slapi_DN *sdn, 
                                  int scope, const char *filter, char **attrs, 
                                  int attrsonly, LDAPControl **controls, 
                                  const char *uniqueid, 
                                  Slapi_ComponentId *plugin_identity,
                                  int operation_flags)
{
	Operation *op;
	if (pb == NULL || sdn == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, NULL, 
		                "slapi_search_internal_set_pb: NULL parameter\n");
		return;
	}

	op= internal_operation_new(SLAPI_OPERATION_SEARCH,operation_flags);
	slapi_pblock_set(pb, SLAPI_OPERATION, op);
	slapi_pblock_set(pb, SLAPI_ORIGINAL_TARGET_DN,
	                 (void *)slapi_sdn_get_udn(sdn));
	slapi_pblock_set(pb, SLAPI_TARGET_SDN, (void *)sdn);
	slapi_pblock_set(pb, SLAPI_SEARCH_SCOPE, &scope);
	slapi_pblock_set(pb, SLAPI_SEARCH_STRFILTER, (void*)filter);
	slapi_pblock_set(pb, SLAPI_CONTROLS_ARG, controls);
	slapi_pblock_set(pb, SLAPI_SEARCH_ATTRS, attrs);
	slapi_pblock_set(pb, SLAPI_SEARCH_ATTRSONLY, &attrsonly);
	if (uniqueid)
	{
		slapi_pblock_set(pb, SLAPI_TARGET_UNIQUEID, (void*)uniqueid);
	}
	slapi_pblock_set(pb, SLAPI_PLUGIN_IDENTITY, (void*)plugin_identity);
}

void slapi_seq_internal_set_pb(Slapi_PBlock *pb, char *base, int type, char *attrname, char *val,
							  char **attrs, int attrsonly, LDAPControl **controls,
							  Slapi_ComponentId *plugin_identity, int operation_flags)
{
	Operation *op;
	if (pb == NULL || base == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, NULL, 
						"slapi_seq_internal_set_pb: NULL parameter\n");
		return;
	}

    op= internal_operation_new(SLAPI_OPERATION_SEARCH,operation_flags);
	slapi_pblock_set(pb, SLAPI_OPERATION, op);
	slapi_pblock_set(pb, SLAPI_ORIGINAL_TARGET_DN, (void *)base );
	slapi_pblock_set(pb, SLAPI_SEQ_TYPE, &type);
	slapi_pblock_set(pb, SLAPI_SEQ_ATTRNAME, attrname);
	slapi_pblock_set(pb, SLAPI_SEQ_VAL, val);
    slapi_pblock_set(pb, SLAPI_SEARCH_ATTRS, attrs);
    slapi_pblock_set(pb, SLAPI_SEARCH_ATTRSONLY, &attrsonly);        
	slapi_pblock_set(pb, SLAPI_CONTROLS_ARG, controls);
	slapi_pblock_set(pb, SLAPI_PLUGIN_IDENTITY, plugin_identity);
}

static int seq_internal_callback_pb (Slapi_PBlock *pb, void *callback_data, 
									  plugin_result_callback prc, 
									  plugin_search_entry_callback psec, 
									  plugin_referral_entry_callback prec)
{
	int rc;
	LDAPControl **controls;
	Operation *op;
    struct callback_fn_ptrs   callback_handler_data;
    Slapi_Backend *be;
	char *base;
	char *attrname, *val;
	Slapi_DN *sdn = NULL;

	slapi_pblock_get(pb, SLAPI_ORIGINAL_TARGET_DN, (void *)&base );
	slapi_pblock_get(pb, SLAPI_CONTROLS_ARG, &controls);

	if (base == NULL) {
		sdn = slapi_sdn_new_ndn_byval("");
	} else {
		sdn = slapi_sdn_new_dn_byref(base);
	}

	slapi_pblock_set(pb, SLAPI_SEARCH_TARGET_SDN, sdn);

    be = slapi_be_select(sdn);

    callback_handler_data.p_res_callback         = prc;
    callback_handler_data.p_srch_entry_callback  = psec;
    callback_handler_data.p_ref_entry_callback   = prec;
    callback_handler_data.callback_data          = callback_data;

	slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    op->o_handler_data            = (void *)&callback_handler_data;
    op->o_result_handler          = internal_res_callback;
    op->o_search_entry_handler    = internal_srch_entry_callback;
    op->o_search_referral_handler = internal_ref_entry_callback;
		 
	/* set target specification of the operation used to decide which plugins are called for the operation */  
	operation_set_target_spec (op, sdn);

	/* Normalize the attribute type and value */
	slapi_pblock_get (pb, SLAPI_SEQ_ATTRNAME, &attrname);
	slapi_pblock_get (pb, SLAPI_SEQ_VAL, &val);
    attrname = slapi_attr_syntax_normalize(attrname);
    val = ( NULL == val ) ? NULL : slapi_ch_strdup( val );

    slapi_pblock_set(pb, SLAPI_BACKEND, be);
    slapi_pblock_set(pb, SLAPI_PLUGIN, be->be_database);    
	slapi_pblock_set(pb, SLAPI_SEQ_ATTRNAME, attrname);
	slapi_pblock_set(pb, SLAPI_SEQ_VAL, val);	  
	slapi_pblock_set(pb, SLAPI_REQCONTROLS, controls);

	/* set actions taken to process the operation */
	set_config_params (pb);

	/* set common parameters */
	set_common_params (pb);

    if (be->be_seq != NULL)
	{
        rc = (*be->be_seq)(pb);
	}
	else
	{
		send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL, "Function not implemented", 0, NULL);
		rc = 0;
    }

	slapi_ch_free_string(&attrname);
	slapi_ch_free_string(&val);
	/* slapi_pblock_get(pb, SLAPI_SEARCH_TARGET, &normbase); */
	slapi_pblock_get(pb, SLAPI_SEARCH_TARGET_SDN, &sdn);
	slapi_sdn_free(&sdn);
	slapi_pblock_set(pb, SLAPI_SEARCH_TARGET_SDN, NULL);

	return rc;
}

int
slapi_search_internal_callback(const char *ibase, 
                               int scope, 
                               const char *ifstr, 
                               char **attrs,
                               int attrsonly, 
                               void *callback_data,
                               LDAPControl **controls,
                               plugin_result_callback res_callback, 
                               plugin_search_entry_callback srch_callback, 
                               plugin_referral_entry_callback ref_callback)								
{
    Slapi_PBlock              pb;
    int                       rc = 0;

    pblock_init(&pb);

	slapi_search_internal_set_pb (&pb, ibase, scope, ifstr, attrs, attrsonly,
				      controls, NULL, plugin_get_default_component_id(), 0);
								   
	rc = search_internal_callback_pb (&pb, callback_data, res_callback, 
									  srch_callback, ref_callback);
    pblock_done(&pb);		
    return (rc);
}

Slapi_PBlock *
slapi_search_internal(const char *base, 
                      int scope, 
                      const char *filter, 
                      LDAPControl **controls,
                      char **attrs, 
                      int attrsonly)
{
    
	Slapi_PBlock *pb;

    /* initialize pb */
    pb = slapi_pblock_new();
	if (pb)
	{
		slapi_search_internal_set_pb (pb, base, scope, filter, attrs, attrsonly, controls, 
					  NULL, plugin_get_default_component_id(), 0); 
    
		search_internal_pb (pb);
	}
    
    return pb;
}

/* This function free searchs result or referral set on internal_ops in pblocks	*/
void
slapi_free_search_results_internal(Slapi_PBlock *pb)
{
    int i;
	
    if(pb == NULL)
    {
        return;
    }

    if(pb->pb_plugin_internal_search_op_entries != NULL)
    {
        for(i=0; pb->pb_plugin_internal_search_op_entries[i] != NULL; i++)
        {
            slapi_entry_free(pb->pb_plugin_internal_search_op_entries[i]);
        }
        slapi_ch_free((void**)&(pb->pb_plugin_internal_search_op_entries));
    }

    /* free v3 referrals made from result handler */
    if(pb->pb_plugin_internal_search_op_referrals != NULL)
    {
        for(i=0; pb->pb_plugin_internal_search_op_referrals[i] != NULL; i++)
        {
            slapi_ch_free((void**)&(pb->pb_plugin_internal_search_op_referrals[i]));
        }
        slapi_ch_free((void**)&(pb->pb_plugin_internal_search_op_referrals));
     }
}

/* this functions can be used for dn as well as uniqueid based operations */

/* pblock should contain the following data (can be set via call to slapi_search_internal_set_pb):
	For uniqueid based search:
		SLAPI_TARGET_DN set to dn that allows to select right backend
		SLAPI_TARGET_UNIQUEID set to the uniqueid of the entry we are looking for

	For dn based search:
		SLAPI_TARGET_DN set to search base
		SLAPI_ORIGINAL_TARGET_DN set to original un-normalized search base
		SLAPI_SEARCH_SCOPE set to search scope
		SLAPI_SEARCH_STRFILTER set to search filter
		SLAPI_CONTROLS_ARG set to request controls if present
		SLAPI_SEARCH_ATTRS set to the list of attributes to return
		SLAPI_SEARCH_ATTRSONLY tells whether attribute values should be returned. 				
 */
int slapi_search_internal_pb (Slapi_PBlock *pb)
{
	if (pb == NULL)
		return -1;

	if (!allow_operation (pb))
	{
		slapi_send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL,
						 "This plugin is not configured to access operation target data", 0, NULL );
		return 0;
	}

	return search_internal_pb (pb);
}

/* pblock should contain the same data as for slapi_search_internal_pb */
int slapi_search_internal_callback_pb (Slapi_PBlock *pb, void *callback_data, 
									   plugin_result_callback prc, 
									   plugin_search_entry_callback psec, 
									   plugin_referral_entry_callback prec)
{
	if (pb == NULL)
		return -1;

	if (!allow_operation (pb))
	{
		send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL,
						 "This plugin is not configured to access operation target data", 0, NULL );
		return 0;
	}

	return (search_internal_callback_pb (pb, callback_data, prc, psec, prec));
}

static int
internal_plugin_search_entry_callback(Slapi_Entry *e, void *callback_data)
{
    Entry_Node *this_entry;
  
    /* add this entry to the list of entries we are making */   
    this_entry = (Entry_Node *)slapi_ch_calloc(1,sizeof(Entry_Node));
    
	if ((this_entry->data = slapi_entry_dup(e)) == NULL) 
    {
        return(0);
    }

    this_entry->next = ((plugin_search_internal_data *) callback_data)->entry_list_head;

    ((plugin_search_internal_data *) callback_data)->entry_list_head = this_entry;
    ((plugin_search_internal_data *) callback_data)->num_entries++;

    return(0);
} 

static int
internal_plugin_search_referral_callback(char *referral, void *callback_data)
{
    Referral_Node *this_referral;
  
    /* add this to the list of referrals we are making */   
    this_referral = (Referral_Node *)slapi_ch_calloc(1,sizeof(Referral_Node));
    this_referral->data = slapi_ch_strdup(referral);
    this_referral->next = ((plugin_search_internal_data *) callback_data)->referral_list_head;

    ((plugin_search_internal_data *) callback_data)->referral_list_head = this_referral;
    ((plugin_search_internal_data *) callback_data)->num_referrals++;

    return(0);
} 

static void internal_plugin_result_callback(int rc, void *callback_data)
{
    /* put the result into pb_op_result */
    ((plugin_search_internal_data *) callback_data)->rc = rc;
}

static int search_internal_pb (Slapi_PBlock *pb)
{
    plugin_search_internal_data   psid;
    Entry_Node                    *iterator, *tmp;
    Referral_Node                 *ref_iterator, *ref_tmp;
    int                           i;
    int                           opresult = 0;
    Slapi_Entry                   **pb_search_entries = NULL;
    char                          **pb_search_referrals = NULL;

	PR_ASSERT (pb);

    /* initialize psid */
    psid.rc              = -1;
    psid.num_entries     =0;
    psid.num_referrals   =0; 
    psid.entry_list_head = NULL;
    psid.referral_list_head = NULL;

    /* setup additional pb data */
    slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
    slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, pb_search_entries);
    slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_SEARCH_REFERRALS, pb_search_referrals);
 
    /* call internal search callback, define search_entry_callback, and result_callback such
       that the results of the search are stuffed into pb */
    search_internal_callback_pb (pb, &psid, internal_plugin_result_callback, 
									  internal_plugin_search_entry_callback,  
									  internal_plugin_search_referral_callback);
    opresult = psid.rc;

    /* stuff search entry pointers from linked list to contiguous array for pblock */
    pb_search_entries = (Slapi_Entry **)slapi_ch_calloc((psid.num_entries + 1), sizeof(Slapi_Entry *));

    for(i=0,iterator = psid.entry_list_head; iterator !=NULL; iterator=iterator->next, i++)
    {
        pb_search_entries[i]= iterator->data;
    }
    pb_search_entries[i]=NULL;

    /* free the linked list now that data has been put in the array */ 
    iterator=psid.entry_list_head;
    while(iterator != NULL)
    {
        /* free the data held in this node */
        tmp                  = iterator;
        iterator             = iterator->next;

        /* free the node */
        if(tmp != NULL)
        {
            slapi_ch_free((void **) &tmp);
        }
    }
    psid.entry_list_head = NULL;

    /* stuff referrals list into an array if we got any to put into the pblock */
    if(psid.num_referrals != 0)
    {
        pb_search_referrals = (char **)slapi_ch_calloc((psid.num_referrals + 1), sizeof(char *));

        for(i=0, ref_iterator = psid.referral_list_head; ref_iterator !=NULL; ref_iterator=ref_iterator->next, i++)
        {
            pb_search_referrals[i]= ref_iterator->data;
        }
        pb_search_referrals[i]=NULL;

        /* free the linked list now that data has been put in the array */ 
        ref_iterator=psid.referral_list_head;
        while(ref_iterator != NULL)
        {

            ref_tmp              = ref_iterator;
            ref_iterator         = ref_iterator->next;

            /* free the node */
            if(ref_tmp != NULL)
            {
                slapi_ch_free((void **) &ref_tmp);
            }
	}
	psid.referral_list_head = NULL;
    }

    /* set the result, the array of entries, and the array of referrals in pb */
    slapi_pblock_set(pb, SLAPI_NENTRIES, &psid.num_entries);
    slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
    slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, pb_search_entries);
    slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_SEARCH_REFERRALS, pb_search_referrals);	

	return 0;
}

static int
search_internal_callback_pb (Slapi_PBlock *pb, void *callback_data, 
                             plugin_result_callback prc, 
                             plugin_search_entry_callback psec, 
                             plugin_referral_entry_callback prec)
{
	LDAPControl				  **controls;
	Operation                 *op;
    struct slapi_filter	      *filter = NULL;
    char                      *fstr = NULL;
    struct callback_fn_ptrs   callback_handler_data;
	int						  scope;
	char					  *ifstr;
	int						  opresult;
	int						  rc = 0;

	PR_ASSERT (pb);

	/* retrieve search parameters */
	slapi_pblock_get(pb, SLAPI_SEARCH_SCOPE, &scope);
	slapi_pblock_get(pb, SLAPI_SEARCH_STRFILTER, &ifstr);
	slapi_pblock_get(pb, SLAPI_CONTROLS_ARG, &controls);

	/* data validation */
	if (ifstr == NULL || (scope != LDAP_SCOPE_BASE && scope != LDAP_SCOPE_ONELEVEL 
        && scope != LDAP_SCOPE_SUBTREE))
    {
        opresult =  LDAP_PARAM_ERROR;
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
        return -1;
    }

    callback_handler_data.p_res_callback         = prc;
    callback_handler_data.p_srch_entry_callback  = psec;
    callback_handler_data.p_ref_entry_callback   = prec;
    callback_handler_data.callback_data          = callback_data;

	slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    op->o_handler_data            = (void *)&callback_handler_data;
    op->o_result_handler          = internal_res_callback;
    op->o_search_entry_handler    = internal_srch_entry_callback;
    op->o_search_referral_handler = internal_ref_entry_callback;
	
    filter = slapi_str2filter((fstr = slapi_ch_strdup(ifstr)));
    if(scope == LDAP_SCOPE_BASE) {
        filter->f_flags |= (SLAPI_FILTER_LDAPSUBENTRY |
                            SLAPI_FILTER_TOMBSTONE | SLAPI_FILTER_RUV);
    }

    if (NULL == filter) 
	{
    	send_ldap_result(pb, LDAP_FILTER_ERROR, NULL, NULL, 0, NULL);
		rc = -1;
    	goto done;
    }
    filter_normalize(filter);

    slapi_pblock_set(pb, SLAPI_SEARCH_FILTER, filter);
	slapi_pblock_set(pb, SLAPI_REQCONTROLS, controls);

	/* set actions taken to process the operation */
	set_config_params (pb);

	/* set parameters common for all internal operations */
	set_common_params (pb);
	{
	    int timelimit = -1;
	    int sizelimit = -1;
		int deref = LDAP_DEREF_ALWAYS;
		slapi_pblock_set(pb, SLAPI_SEARCH_DEREF, &deref);         
		slapi_pblock_set(pb, SLAPI_SEARCH_TIMELIMIT, &timelimit);
	    slapi_pblock_set(pb, SLAPI_SEARCH_SIZELIMIT, &sizelimit);
	}

	/* plugins which play with the search may 
	 * change the search params may allocate
	 * memory so we need to keep track of
	 * changed base search strings
	 */
	op_shared_search (pb, 1);    

    slapi_pblock_get(pb, SLAPI_SEARCH_FILTER, &filter);

done:
    slapi_ch_free((void **) & fstr);
	if (filter != NULL) 
    {
        slapi_filter_free(filter, 1 /* recurse */);
    }

    return(rc);
}

/* allow/disallow operation based of the plugin configuration */
PRBool allow_operation (Slapi_PBlock *pb)
{
	struct slapdplugin *plugin = NULL;
	Slapi_DN *sdnp = NULL;
	Slapi_DN sdn;
	PRBool allow;
	struct slapi_componentid * cid=NULL;

	PR_ASSERT (pb);
	
	/* make sure that users of new API provide plugin identity */
	slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &cid);
	if (cid == NULL)
	{
		slapi_log_error( SLAPI_LOG_FATAL, NULL, "allow_operation: component identity is NULL\n");
		return PR_FALSE;
	}
	plugin=(struct slapdplugin *) cid->sci_plugin;
	if (plugin == NULL)
	{
		slapi_log_error( SLAPI_LOG_FATAL, NULL, "allow_operation: plugin identity is NULL\n");
		return PR_FALSE;
	}

	slapi_sdn_init(&sdn);
	slapi_pblock_get (pb, SLAPI_TARGET_SDN, &sdnp);
	if (NULL == sdnp) {
		slapi_sdn_init_ndn_byval(&sdn,"");
		sdnp = &sdn;
	}

	allow = plugin_allow_internal_op (sdnp, plugin);

	slapi_sdn_done(&sdn);
	
	return allow;
}

/* set operation configuration based on the plugin configuration */
void set_config_params (Slapi_PBlock *pb)
{
	Slapi_Operation *operation;
	struct slapdplugin *plugin = NULL;
	char *dn;
	struct slapi_componentid * cid=NULL;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

	slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &cid);
 	if (cid)
        	plugin=(struct slapdplugin *) cid->sci_plugin;

	/* set actions */
	slapi_pblock_get( pb, SLAPI_OPERATION, &operation);
	operation_set_flag(operation,plugin_build_operation_action_bitmap(operation->o_flags, plugin));
 
	/* Check if we have a flag to keep the operation secret */
	if (operation_is_flag_set(operation, OP_FLAG_ACTION_NOLOG)) {
		/* Clear the LOG_AUDIT and LOG_CHANGES flag */
		operation_clear_flag(operation, OP_FLAG_ACTION_LOG_AUDIT | OP_FLAG_ACTION_LOG_CHANGES);
	}
	
	/*
	 *  if we are not tracking the bind dn, then use the plugin name as the
	 *  modifiersname, otherwise, we have already set the op dn as the bind dn
	 */
	if(!slapdFrontendConfig->plugin_track || slapi_sdn_isempty(&operation->o_sdn)){
		/* set name to be used for creator's and modifiers attributes */
		dn = plugin_get_dn (plugin);
		if (dn)
			slapi_sdn_init_dn_passin(&operation->o_sdn, dn);
	}
}

/* set parameters common for all internal operations */
void set_common_params (Slapi_PBlock *pb)
{
	int isroot = 1; 
	LDAPControl		**controls;

	slapi_pblock_get(pb, SLAPI_CONTROLS_ARG, &controls);

	if (NULL != controls) 
	{
		int managedsait = slapi_control_present(controls,
				LDAP_CONTROL_MANAGEDSAIT, NULL, NULL);
		int pwpolicy_ctrl = slapi_control_present(controls,
				LDAP_X_CONTROL_PWPOLICY_REQUEST, NULL, NULL);
	
		slapi_pblock_set(pb, SLAPI_MANAGEDSAIT, &managedsait);
		slapi_pblock_set(pb, SLAPI_PWPOLICY, &pwpolicy_ctrl);
	}

	slapi_pblock_set(pb, SLAPI_REQUESTOR_ISROOT, &isroot);
}


/*
 * Given a DN, find an entry by doing an internal search.  An LDAP error
 * code is returned.  To check if an entry exists without returning a
 * copy of the entry, NULL can be passed for ret_entry.
 */
int
slapi_search_internal_get_entry_ext( Slapi_DN *dn, char ** attrs, Slapi_Entry **ret_entry , void * component_identity, void *txn )
{
    Slapi_Entry **entries = NULL;
    Slapi_PBlock *int_search_pb = NULL;
    int rc = 0;
    
    if (ret_entry) {
        *ret_entry = NULL;
    }

    int_search_pb = slapi_pblock_new ();
	slapi_search_internal_set_pb ( int_search_pb,  slapi_sdn_get_dn(dn), LDAP_SCOPE_BASE, "(|(objectclass=*)(objectclass=ldapsubentry))",
								   attrs ,
								   0 /* attrsonly */, NULL /* controls */,
								   NULL /* uniqueid */,
								   component_identity, 0 /* actions */ );
	slapi_pblock_set( int_search_pb, SLAPI_TXN, txn );
	slapi_search_internal_pb ( int_search_pb );
    slapi_pblock_get( int_search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc );
    if ( LDAP_SUCCESS == rc ) {
		slapi_pblock_get( int_search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries );
		if ( NULL != entries && NULL != entries[ 0 ]) {
			/* Only need to dup the entry if the caller passed ret_entry in. */
			if (ret_entry) {
				Slapi_Entry *temp_entry = NULL;
				temp_entry = entries[ 0 ];
				*ret_entry = slapi_entry_dup(temp_entry);
			}
		} else {
			/* No entry there */
			rc = LDAP_NO_SUCH_OBJECT;
		}
    }
    slapi_free_search_results_internal(int_search_pb);
    slapi_pblock_destroy(int_search_pb);
	int_search_pb = NULL;
    return rc;
}

/*
 * Given a DN, find an entry by doing an internal search.  An LDAP error
 * code is returned.  To check if an entry exists without returning a
 * copy of the entry, NULL can be passed for ret_entry.
 */
int
slapi_search_internal_get_entry( Slapi_DN *dn, char ** attrs, Slapi_Entry **ret_entry , void * component_identity)
{
	return slapi_search_internal_get_entry_ext(dn, attrs, ret_entry, component_identity, NULL);
}

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* supplier_operation_extension.c - replication extension to the Operation object
 */


#include "repl.h"
#include "repl5.h"

/* ***** Supplier side ***** */

/* supplier operation extension constructor */
void* supplier_operation_extension_constructor (void *object, void *parent)
{
	supplier_operation_extension *ext = (supplier_operation_extension*) slapi_ch_calloc (1, sizeof (supplier_operation_extension));
	if (ext == NULL)
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "unable to create replication supplier operation extension - out of memory\n" );
	}
	else
	{
    	ext->prevent_recursive_call= 0;
	}
	return ext;
}

/* supplier operation extension destructor */
void supplier_operation_extension_destructor (void *ext,void *object, void *parent)
{
	if (ext)
	{
		supplier_operation_extension *supext = (supplier_operation_extension *)ext;
		if (supext->operation_parameters)
			operation_parameters_free (&(supext->operation_parameters));
        if (supext->repl_gen)
            slapi_ch_free ((void**)&supext->repl_gen);
		slapi_ch_free((void **)&ext);	
	}
}

/* ***** Consumer side ***** */

/* consumer operation extension constructor */
void* consumer_operation_extension_constructor (void *object, void *parent)
{
	consumer_operation_extension *ext = (consumer_operation_extension*) slapi_ch_calloc (1, sizeof (consumer_operation_extension));
	if (ext == NULL)
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "unable to create replication consumer operation extension - out of memory\n" );
	}
	if(object!=NULL && parent!=NULL)
	{
		consumer_connection_extension *connext;
		connext = (consumer_connection_extension *)repl_con_get_ext(REPL_CON_EXT_CONN, parent);
		if(NULL != connext)
		{
			/* We copy the Connection Replicated Session flag to the Replicated Operation flag */
			if (connext->isreplicationsession)
			{
				operation_set_flag((Slapi_Operation *)object,OP_FLAG_REPLICATED);
			}
			/* We set the Replication DN flag if session bound as replication dn */ 
			if (connext->is_legacy_replication_dn)
			{
				operation_set_flag((Slapi_Operation *)object, OP_FLAG_LEGACY_REPLICATION_DN);
			}
		}
	}
	else
	{
		/* (parent==NULL) for internal operations */
		PR_ASSERT(object!=NULL);
	}

	return ext;
}

/* consumer operation extension destructor */
void consumer_operation_extension_destructor (void *ext,void *object, void *parent)
{
	if (NULL != ext)
	{
		consumer_operation_extension *opext = (consumer_operation_extension *)ext;
		if (NULL != opext->search_referrals)
		{
			/* free them - search_referrals is currently unused, but 
			   free them using the obverse of the allocation method */
			opext->search_referrals = NULL;
		}
		slapi_ch_free((void **)&ext);	
	}
}

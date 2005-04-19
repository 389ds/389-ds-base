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

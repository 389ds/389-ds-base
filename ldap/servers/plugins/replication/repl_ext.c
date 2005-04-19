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
/* repl_ext.c - manages operation extensions created by the 
 *              replication system
 */


#include "repl.h"

/* structure with information for each extension */
typedef struct repl_ext
{
	char *object_name;				/* name of the object extended   */
	int object_type;				/* handle to the extended object */
	int handle;					    /* extension handle              */
} repl_ext;

/* ----------------------------- Supplier ----------------------------- */

static repl_ext repl_sup_ext_list [REPL_EXT_ALL];

/* initializes replication extensions */
void repl_sup_init_ext ()
{
    int rc;
		
	/* populate the extension list */
	repl_sup_ext_list[REPL_SUP_EXT_OP].object_name = SLAPI_EXT_OPERATION;
	
	rc = slapi_register_object_extension(repl_plugin_name,
	                     SLAPI_EXT_OPERATION, 
						 supplier_operation_extension_constructor, 
					     supplier_operation_extension_destructor, 
						 &repl_sup_ext_list[REPL_SUP_EXT_OP].object_type,
					     &repl_sup_ext_list[REPL_SUP_EXT_OP].handle);

	if(rc!=0)
	{
	    PR_ASSERT(0); /* JCMREPL Argh */
	}
}

void* repl_sup_get_ext (ext_type type, void *object)
{
	/* find the requested extension */
	repl_ext ext = repl_sup_ext_list [type];

	void* data = slapi_get_object_extension(ext.object_type, object, ext.handle);

	return data;
}

/* ----------------------------- Consumer ----------------------------- */

static repl_ext repl_con_ext_list [REPL_EXT_ALL];

/* initializes replication extensions */
void repl_con_init_ext ()
{
    int rc;
	
	/* populate the extension list */
	repl_con_ext_list[REPL_CON_EXT_OP].object_name = SLAPI_EXT_OPERATION;
	rc = slapi_register_object_extension(repl_plugin_name,
	                     SLAPI_EXT_OPERATION, 
						 consumer_operation_extension_constructor, 
					     consumer_operation_extension_destructor, 
						 &repl_con_ext_list[REPL_CON_EXT_OP].object_type,
					     &repl_con_ext_list[REPL_CON_EXT_OP].handle);
	if(rc!=0)
	{
	    PR_ASSERT(0); /* JCMREPL Argh */
	}
						 
	repl_con_ext_list[REPL_CON_EXT_CONN].object_name = SLAPI_EXT_CONNECTION;
	rc = slapi_register_object_extension(repl_plugin_name,
	                     SLAPI_EXT_CONNECTION, 
						 consumer_connection_extension_constructor, 
					     consumer_connection_extension_destructor, 
						 &repl_con_ext_list[REPL_CON_EXT_CONN].object_type,
					     &repl_con_ext_list[REPL_CON_EXT_CONN].handle);
	if(rc!=0)
	{
	    PR_ASSERT(0); /* JCMREPL Argh */
	}

    repl_con_ext_list[REPL_CON_EXT_MTNODE].object_name = SLAPI_EXT_MTNODE;
	rc = slapi_register_object_extension(repl_plugin_name,
	                     SLAPI_EXT_MTNODE, 
						 multimaster_mtnode_extension_constructor, 
					     multimaster_mtnode_extension_destructor, 
						 &repl_con_ext_list[REPL_CON_EXT_MTNODE].object_type,
					     &repl_con_ext_list[REPL_CON_EXT_MTNODE].handle);
	if(rc!=0)
	{
	    PR_ASSERT(0); /* JCMREPL Argh */
	}
}

void* repl_con_get_ext (ext_type type, void *object)
{
	/* find the requested extension */
	repl_ext ext = repl_con_ext_list [type];

	void* data = slapi_get_object_extension(ext.object_type, object, ext.handle);

	return data;
}  



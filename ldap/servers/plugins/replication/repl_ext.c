/** BEGIN COPYRIGHT BLOCK
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



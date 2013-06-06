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


/* repl5_replica.c */

#include "repl.h"   /* ONREPL - this is bad */
#include "repl5.h" 
#include "cl5_api.h"

/* global data */
static DataList *root_list;

/*
 * Mapping tree node extension management. Node stores replica object
 */

void
multimaster_mtnode_extension_init ()
{
	/* Initialize list that store node roots. It is used during
       plugin startup to create replica objects */
	root_list = dl_new ();
	dl_init (root_list, 0);
}

void
multimaster_mtnode_extension_destroy ()
{
	dl_cleanup (root_list, (FREEFN)slapi_sdn_free);
	dl_free (&root_list);
}

/* This function loops over the list of node roots, constructing replica objects 
   where exist */
void
multimaster_mtnode_construct_replicas ()
{
	Slapi_DN *root;
	int cookie;
	Replica *r;	
	mapping_tree_node *mtnode;
	multimaster_mtnode_extension *ext;

	for (root = dl_get_first (root_list, &cookie); root;
		 root = dl_get_next (root_list, &cookie))
	{
		r = replica_new(root);
        if (r)
        {

			mtnode = slapi_get_mapping_tree_node_by_dn(root);    
			if (mtnode == NULL)
			{
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
								"multimaster_mtnode_construct_replicas: "
								"failed to locate mapping tree node for %s\n",
								slapi_sdn_get_dn (root));	        
				continue;
			}
    
			ext = (multimaster_mtnode_extension *)repl_con_get_ext (REPL_CON_EXT_MTNODE, mtnode);
			if (ext == NULL)
			{
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "multimaster_mtnode_construct_replicas: "
								"failed to locate replication extension of mapping tree node for %s\n",
								slapi_sdn_get_dn (root));	        
				continue;
			}

            ext->replica = object_new(r, replica_destroy);
            if (replica_add_by_name (replica_get_name (r), ext->replica) != 0)
            {
                if(ext->replica){
                    object_release (ext->replica);
                    ext->replica = NULL;
                }
            }
        }
	}
}

void *
multimaster_mtnode_extension_constructor (void *object, void *parent)
{
    mapping_tree_node *node;
    const Slapi_DN *root;
    multimaster_mtnode_extension *ext;

    ext = (multimaster_mtnode_extension *)slapi_ch_calloc (1, sizeof (multimaster_mtnode_extension));

    node = (mapping_tree_node *)object;

    /* replica can be attached only to local public data */
    if (slapi_mapping_tree_node_is_set (node, SLAPI_MTN_LOCAL) &&
        !slapi_mapping_tree_node_is_set (node, SLAPI_MTN_PRIVATE))
    {        
        root = slapi_get_mapping_tree_node_root (node);
		/* ONREPL - we don't create replica object here because
		   we can't fully initialize replica here since backends
		   are not yet started. Instead, replica objects are created
		   during replication plugin startup */
        if (root)
        {
			/* for now just store node root in the root list */
            dl_add (root_list, slapi_sdn_dup (root));
        }
    }

    return ext;
}

void
multimaster_mtnode_extension_destructor (void* ext, void *object, void *parent)
{
    if (ext)
    {
        multimaster_mtnode_extension *mtnode_ext = (multimaster_mtnode_extension *)ext;
        if (mtnode_ext->replica)
        {
            object_release (mtnode_ext->replica);
            mtnode_ext->replica = NULL;
        }
	slapi_ch_free((void **)&ext);
    }
}

Object *
replica_get_replica_from_dn (const Slapi_DN *dn)
{
    mapping_tree_node *mtnode;
    multimaster_mtnode_extension *ext;

    if (dn == NULL)
        return NULL;

    mtnode = slapi_get_mapping_tree_node_by_dn(dn);    
    if (mtnode == NULL)
    {
        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "replica_get_replica_from_dn: "
                        "failed to locate mapping tree node for %s\n",
                        slapi_sdn_get_dn (dn));	        
        return NULL;
    }

    ext = (multimaster_mtnode_extension *)repl_con_get_ext (REPL_CON_EXT_MTNODE, mtnode);
    if (ext == NULL)
    {
        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "replica_get_replica_from_dn: "
                        "failed to locate replication extension of mapping tree node for %s\n",
                        slapi_sdn_get_dn (dn));	        
        return NULL;
    }

    if (ext->replica)
        object_acquire (ext->replica);

    return ext->replica;
}

Object *replica_get_replica_for_op (Slapi_PBlock *pb)
{
    Slapi_DN *sdn = NULL;
    Object *repl_obj = NULL;

    if (pb)
    {
        /* get replica generation for this operation */
        slapi_pblock_get (pb, SLAPI_TARGET_SDN, &sdn);
        if (NULL == sdn) {
            goto bail;
        }
        repl_obj = replica_get_replica_from_dn (sdn);
    }
bail:
    return repl_obj;
}

Object *replica_get_for_backend (const char *be_name)
{
    Slapi_Backend *be;
    const Slapi_DN *suffix;
    Object *r_obj;

    be = slapi_be_select_by_instance_name(be_name);
    if (NULL == be)
		return NULL;

    suffix = slapi_be_getsuffix(be, 0);    

    r_obj = replica_get_replica_from_dn (suffix);
    
    return r_obj;
}

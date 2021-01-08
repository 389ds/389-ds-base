/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/* repl5_replica.c */

#include "repl5.h"
#include "cl5_api.h"

/* global data */
static DataList *root_list;

/*
 * Mapping tree node extension management. Node stores replica object
 */

void
multimaster_mtnode_extension_init()
{
    /* Initialize list that store node roots. It is used during
       plugin startup to create replica objects */
    root_list = dl_new();
    dl_init(root_list, 0);
}

void
multimaster_mtnode_extension_destroy()
{
    /* First iterate over the list to free the replica infos */
    dl_cleanup(root_list, (FREEFN)slapi_sdn_free);
    dl_free(&root_list);
}

/* This function loops over the list of node roots, constructing replica objects
   where exist */
void
multimaster_mtnode_construct_replicas()
{
    Slapi_DN *root;
    int cookie;
    Replica *r;
    mapping_tree_node *mtnode;
    multimaster_mtnode_extension *ext;

    for (root = dl_get_first(root_list, &cookie); root;
         root = dl_get_next(root_list, &cookie)) {
        r = replica_new(root);
        if (r) {

            mtnode = slapi_get_mapping_tree_node_by_dn(root);
            if (mtnode == NULL) {
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                              "multimaster_mtnode_construct_replicas: "
                              "failed to locate mapping tree node for %s\n",
                              slapi_sdn_get_dn(root));
                continue;
            }

            ext = (multimaster_mtnode_extension *)repl_con_get_ext(REPL_CON_EXT_MTNODE, mtnode);
            if (ext == NULL) {
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "multimaster_mtnode_construct_replicas: "
                                                                "failed to locate replication extension of mapping tree node for %s\n",
                              slapi_sdn_get_dn(root));
                continue;
            }

            ext->replica = object_new(r, replica_destroy);
            if (replica_add_by_name(replica_get_name(r), r) != 0) {
                if (ext->replica) {
                    object_release(ext->replica);
                    ext->replica = NULL;
                }
            }
            /* Wait a few seconds for everything to startup before resuming any replication tasks */
            slapi_eq_once_rel(replica_check_for_tasks, (void *)replica_get_root(r),
                              slapi_current_rel_time_t() + 5);
        }
    }
}

void *
multimaster_mtnode_extension_constructor(void *object, void *parent __attribute__((unused)))
{
    mapping_tree_node *node;
    const Slapi_DN *root;
    multimaster_mtnode_extension *ext;

    ext = (multimaster_mtnode_extension *)slapi_ch_calloc(1, sizeof(multimaster_mtnode_extension));

    node = (mapping_tree_node *)object;

    /* replica can be attached only to local public data */
    if (slapi_mapping_tree_node_is_set(node, SLAPI_MTN_LOCAL) &&
        !slapi_mapping_tree_node_is_set(node, SLAPI_MTN_PRIVATE)) {
        root = slapi_get_mapping_tree_node_root(node);
        /* ONREPL - we don't create replica object here because
           we can't fully initialize replica here since backends
           are not yet started. Instead, replica objects are created
           during replication plugin startup */
        if (root != NULL && !slapi_sdn_isempty(root)) {
            /* for now just store node root in the root list */
            dl_add(root_list, slapi_sdn_dup(root));
        }
    }

    return ext;
}

void
multimaster_mtnode_extension_destructor(void *ext, void *object __attribute__((unused)), void *parent __attribute__((unused)))
{
    if (ext) {
        multimaster_mtnode_extension *mtnode_ext = (multimaster_mtnode_extension *)ext;
        if (mtnode_ext->replica) {
            object_release(mtnode_ext->replica);
            mtnode_ext->replica = NULL;
        }
        slapi_ch_free((void **)&ext);
    }
}

Replica *
replica_get_replica_from_dn(const Slapi_DN *dn)
{
    mapping_tree_node *mtnode;
    multimaster_mtnode_extension *ext;
    Replica *r = NULL;

    if (dn == NULL)
        return NULL;

    mtnode = slapi_get_mapping_tree_node_by_dn(dn);
    if (mtnode == NULL) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "replica_get_replica_from_dn: "
                                                        "failed to locate mapping tree node for %s\n",
                      slapi_sdn_get_dn(dn));
        return NULL;
    }

    ext = (multimaster_mtnode_extension *)repl_con_get_ext(REPL_CON_EXT_MTNODE, mtnode);
    if (ext == NULL) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "replica_get_replica_from_dn: "
                                                        "failed to locate replication extension of mapping tree node for %s\n",
                      slapi_sdn_get_dn(dn));
        return NULL;
    }

    if (ext->replica) {
        r = (Replica *)object_get_data(ext->replica);
    }

    return r;
}

Replica *
replica_get_replica_from_root(const char *repl_root)
{
    Replica *replica =NULL;
    Slapi_DN *repl_sdn = slapi_sdn_new_dn_byval(repl_root);

    replica = replica_get_replica_from_dn(repl_sdn);
    slapi_sdn_free(&repl_sdn);

    return replica;
}

Replica *
replica_get_replica_for_op(Slapi_PBlock *pb)
{
    Slapi_DN *sdn = NULL;
    Replica *replica = NULL;

    if (pb) {
        /* get replica generation for this operation */
        slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
        if (NULL == sdn) {
            goto bail;
        }
        replica = replica_get_replica_from_dn(sdn);
    }
bail:
    return replica;
}

Replica *
replica_get_for_backend(const char *be_name)
{
    Slapi_Backend *be;
    const Slapi_DN *suffix;

    be = slapi_be_select_by_instance_name(be_name);
    if (NULL == be)
        return NULL;

    suffix = slapi_be_getsuffix(be, 0);

    return replica_get_replica_from_dn(suffix);

}

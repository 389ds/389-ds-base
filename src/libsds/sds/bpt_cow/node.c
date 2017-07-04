/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "bpt_cow.h"

#ifdef SDS_DEBUG
static sds_result
sds_bptree_cow_node_path_verify(sds_bptree_transaction *btxn, sds_bptree_node *node) {
    // Verify that the node has a valid parent path back to the root!
    sds_bptree_node *target_node = node;
    while (target_node->parent != NULL) {
        target_node = target_node->parent;
    }
    if (btxn->root != target_node) {
        return SDS_INVALID_POINTER;
    }
    return SDS_SUCCESS;
}
#endif

void
sds_bptree_cow_node_siblings(sds_bptree_node *target, sds_bptree_node **left, sds_bptree_node **right) {
    /*
     * This has one difference to sds_bptree_node_siblings
     * which is that we update the parent pointer of left and right after we get them.
     * This is important, as without this, we can't walk back the tree to fix thetxn
     * parts.
     *
     * Remember, node will have been COWed for this txn, so the parent path is already
     * correct on the way back, so target->parent is valid as the parent to both left
     * and right.
     */
    sds_bptree_node_siblings(target, left, right);
    if (*left != NULL) {
        (*left)->parent = target->parent;
    }
    if (*right != NULL) {
        (*right)->parent = target->parent;
    }
}

static sds_bptree_node *
sds_bptree_cow_node_clone(sds_bptree_transaction *btxn, sds_bptree_node *node) {
    sds_bptree_node *clone_node = sds_memalign(sizeof(sds_bptree_node), SDS_CACHE_ALIGNMENT);

    clone_node->level = node->level;
    clone_node->item_count = node->item_count;
    clone_node->txn_id = btxn->txn_id;
    clone_node->parent = NULL;

    // For each key / value, dup them.
    if (node->level > 0) {
        /* This is a branch, treat it as such. */
        for (size_t i = 0; i < node->item_count; i++) {
            clone_node->keys[i] = btxn->bi->key_dup_fn(node->keys[i]);
            memcpy(&(clone_node->values), &(node->values), sizeof(void *) * SDS_BPTREE_BRANCH);
        }
    } else {
        /* This is a leaf, clone the values correctly. */
        for (size_t i = 0; i < node->item_count; i++) {
            clone_node->keys[i] = btxn->bi->key_dup_fn(node->keys[i]);
            if (node->values[i] != NULL && btxn->bi->value_dup_fn != NULL) {
                clone_node->values[i] = btxn->bi->value_dup_fn(node->values[i]);
            } else {
                clone_node->values[i] = NULL;
            }
        }
    }

    // Now push the previous node to the txn list.
    sds_bptree_node_list_push(&(btxn->owned), node);
    sds_bptree_node_list_push(&(btxn->created), clone_node);

#ifdef SDS_DEBUG
    sds_log("sds_bptree_cow_node_clone", "Cloning node_%p item_count=%d --> clone node_%p\n", node, node->item_count, clone_node);
    sds_log("sds_bptree_cow_node_clone", "txn_%p tentatively owns node_%p for cleaning\n", btxn->parent_txn, node);
#endif

    return clone_node;
}


/**
 * This function is very important. When we are done commiting in the tree, we need
 * to walk back up to the root, and clone all the branch nodes to make the copied
 * path.
 *
 * At the end, we commit our new root to the txn. This relies on checking the txn_ids
 * and such.
 *
 * HINT: This is the "COPY" part of the "COPY ON WRITE".
 *
 * IMPORTANT: This will change the target path to be the "copied" path now.
 */

static void
sds_bptree_cow_branch_clone(sds_bptree_transaction *btxn, sds_bptree_node *origin_node, sds_bptree_node *clone_node) {

#ifdef SDS_DEBUG
    sds_log("sds_bptree_cow_branch_clone", "Cloning branch from node_%p\n", clone_node);
#endif
    // Right, we have now cloned the node. We need to walk up the tree and clone
    // all the branches that path to us.
    sds_bptree_node *former_origin_node = origin_node;
    sds_bptree_node *former_clone_node = clone_node;
    sds_bptree_node *parent_node = NULL;
    sds_bptree_node *parent_clone_node = NULL;

    while (former_origin_node != NULL && former_origin_node->parent != NULL) {
        // Get the next parent node out then.
        parent_node = former_origin_node->parent;

        // This node has a parent, so we need to update it.
        if (parent_node->txn_id == btxn->txn_id) {
            // Then we are done, the whole path up to the root is already in this txn now.
            sds_bptree_node_node_replace(parent_node, former_origin_node, former_clone_node);

// TODO: This probably needs to update csums of the nodes along the branch.
#ifdef SDS_DEBUG
            sds_log("sds_bptree_cow_branch_clone", "Branch parent node_%p already within txn, finishing...\n", parent_node);
            if (btxn->bi->offline_checksumming) {
                // Update this becuase we are updating the owned node lists.
                sds_bptree_crc32c_update_node(parent_node);
            }
#endif
            // BAIL EARLY
            return;
        } else {
            // Is the parent node NOT in this txn?
            // We need to clone the parent, and then replace ourselves in it ....
#ifdef SDS_DEBUG
            sds_log("sds_bptree_cow_branch_clone", "Branch parent node_%p NOT within txn, cloning...\n", parent_node);
#endif
            // This is actually the important part!
            parent_clone_node = sds_bptree_cow_node_clone(btxn, parent_node);
            sds_bptree_node_node_replace(parent_clone_node, former_origin_node, former_clone_node);

            // TODO: This probably needs to update csums of the nodes along the branch.
#ifdef SDS_DEBUG
            if (btxn->bi->offline_checksumming) {
                // Update this becuase we are updating the owned node lists.
                sds_bptree_crc32c_update_node(parent_clone_node);
                sds_bptree_crc32c_update_node(former_clone_node);
            }
#endif

            // Update origin_node with clone_node
            former_origin_node = parent_node;
            former_clone_node = parent_clone_node;
            parent_clone_node = NULL;

        }

    }
    // We have hit the root, update the root.
    // Origin is root, update the txn root.
#ifdef SDS_DEBUG
    sds_log("sds_bptree_cow_branch_clone", "Updating txn_%p root from node_%p to node_%p\n", btxn, btxn->root, former_clone_node);
#endif
    btxn->root = former_clone_node;
}

sds_bptree_node *
sds_bptree_cow_node_prepare(sds_bptree_transaction *btxn, sds_bptree_node *node) {
#ifdef SDS_DEBUG
    sds_log("sds_bptree_node_prepare", " --> prepare node %p for txn_%p", node, btxn);
    sds_log("sds_bptree_node_prepare", "     prepare current tree root node_%p", btxn->root);
#endif
    sds_bptree_node *result_node = NULL;
    // Check if the btxn id matches the node txn id.
    if (btxn->txn_id == node->txn_id) {
        // If it does, return this node.
        result_node = node;
    } else {
        // If not, clone the node and values, return new node.
        // This will rebuild the target path to the node based on the copy
        sds_bptree_node *clone_node = sds_bptree_cow_node_clone(btxn, node);
        sds_bptree_cow_branch_clone(btxn, node, clone_node);

        // TODO: Need to update the btxn checksum?

#ifdef SDS_DEBUG
        if (btxn->bi->offline_checksumming) {
            // Update this becuase we are updating the owned node lists.
            sds_bptree_crc32c_update_btxn(btxn);
        }
#endif

        result_node = clone_node;
    }
#ifdef SDS_DEBUG
    if (sds_bptree_cow_node_path_verify(btxn, result_node)!= SDS_SUCCESS) {
        sds_log("sds_bptree_cow_node_prepare", "!!! Invalid path from cow_node to root!");
        return NULL;
    }
    sds_log("sds_bptree_node_prepare", " <-- prepared node %p for txn_%p", node, btxn);
#endif

    return result_node;
}

sds_bptree_node *
sds_bptree_cow_node_create(sds_bptree_transaction *btxn) {
    sds_bptree_node *node = sds_memalign(sizeof(sds_bptree_node), SDS_CACHE_ALIGNMENT);
    // Without memset, we need to null the max link in a value
    node->values[SDS_BPTREE_DEFAULT_CAPACITY] = NULL;
    /* On cow, this value is over-written */
    node->level = 0xFFFF;
    node->item_count = 0;
    node->parent = NULL;
    node->txn_id = btxn->txn_id;

    sds_bptree_node_list_push(&(btxn->created), node);

#ifdef SDS_DEBUG
    if (btxn->bi->offline_checksumming) {
        // Update this becuase we are updating the created node lists.
        sds_bptree_crc32c_update_btxn(btxn);
    }
    sds_log("sds_bptree_cow_node_create", "Creating node_%p item_count=%d\n", node, node->item_count);
#endif

    return node;
}

void
sds_bptree_cow_node_update(sds_bptree_transaction *btxn, sds_bptree_node *node, void *key, void *value) {
    // find the key index.
    size_t index = sds_bptree_node_key_eq_index(btxn->bi->key_cmp_fn, node, key);
    // value free
    btxn->bi->value_free_fn(node->values[index]);
    // insert.
    node->values[index] = value;
#ifdef SDS_DEBUG
    if (btxn->bi->offline_checksumming) {
    	sds_bptree_crc32c_update_node(node);
	}
#endif
}



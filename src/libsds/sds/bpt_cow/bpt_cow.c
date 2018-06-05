/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

/*
 *   _______________________________________
 * < The ultimate in bovine datastructures >
 *  ---------------------------------------
 *         \   ^__^
 *          \  (oo)\_______
 *             (__)\       )\/\
 *                 ||----w |
 *                 ||     ||
*/

#include "bpt_cow.h"
// Used by our printing code.
static FILE *fp = NULL;
static uint64_t print_iter = 0;

sds_result
sds_bptree_cow_init(sds_bptree_cow_instance **binst_ptr, uint16_t checksumming, int64_t (*key_cmp_fn)(void *a, void *b), void (*value_free_fn)(void *value), void *(*value_dup_fn)(void *key), void (*key_free_fn)(void *key), void *(*key_dup_fn)(void *key))
{
    if (binst_ptr == NULL) {
#ifdef SDS_DEBUG
        sds_log("sds_btree_init", "Invalid pointer");
#endif
        return SDS_NULL_POINTER;
    }

    *binst_ptr = sds_memalign(sizeof(sds_bptree_cow_instance), SDS_CACHE_ALIGNMENT);
    (*binst_ptr)->bi = sds_memalign(sizeof(sds_bptree_instance), SDS_CACHE_ALIGNMENT);

    // Create our locks.
    (*binst_ptr)->read_lock = sds_calloc(sizeof(pthread_rwlock_t));
    pthread_rwlock_init((*binst_ptr)->read_lock, NULL);
    (*binst_ptr)->write_lock = sds_calloc(sizeof(pthread_mutex_t));
    pthread_mutex_init((*binst_ptr)->write_lock, NULL);

    /* Take both to be sure of barriers etc. */
    pthread_mutex_lock((*binst_ptr)->write_lock);
    pthread_rwlock_wrlock((*binst_ptr)->read_lock);

    (*binst_ptr)->bi->print_iter = 0;
    (*binst_ptr)->bi->offline_checksumming = checksumming;
    (*binst_ptr)->bi->search_checksumming = checksumming;
    (*binst_ptr)->bi->key_cmp_fn = key_cmp_fn;
    (*binst_ptr)->bi->value_free_fn = value_free_fn;
    (*binst_ptr)->bi->value_dup_fn = value_dup_fn;
    (*binst_ptr)->bi->key_free_fn = key_free_fn;
    (*binst_ptr)->bi->key_dup_fn = key_dup_fn;

    // null the TXN to be sure.
    (*binst_ptr)->txn = NULL;
    // Make the first empty txn.
    // The root node is populated by the transaction.
    (*binst_ptr)->txn = sds_bptree_txn_create(*binst_ptr);
    (*binst_ptr)->tail_txn = (*binst_ptr)->txn;
    (*binst_ptr)->txn->state = SDS_TXN_READ;
    (*binst_ptr)->txn->reference_count = 1;

    // cow node create pushes a node to the created list, flush it.
    // Dump our creation list, we are past the point of no return.
    sds_bptree_node_list_release(&((*binst_ptr)->txn->created));

// Update our checksums.
#ifdef SDS_DEBUG
    if ((*binst_ptr)->bi->offline_checksumming) {
        sds_bptree_crc32c_update_instance((*binst_ptr)->bi);
        sds_bptree_crc32c_update_cow_instance(*binst_ptr);
    }
#endif

    pthread_rwlock_unlock((*binst_ptr)->read_lock);
    pthread_mutex_unlock((*binst_ptr)->write_lock);

    return SDS_SUCCESS;
}

// May block until all transactions are resolved?

sds_result
sds_bptree_cow_destroy(sds_bptree_cow_instance *binst)
{
    sds_result result = SDS_SUCCESS;
#ifdef SDS_DEBUG
    sds_log("sds_bptree_cow_destroy", "    Destroying instance %p", binst);
#endif

    // This locks and destroys *everything* you love about this tree!!!
    sds_bptree_cow_txn_destroy_all(binst);

    // Destroy the locks.
    pthread_rwlock_destroy(binst->read_lock);
    sds_free(binst->read_lock);
    pthread_mutex_destroy(binst->write_lock);
    sds_free(binst->write_lock);
    sds_free(binst->bi);
    sds_free(binst);

    return result;
}

// Read only transactions (but write can use them)

sds_result
sds_bptree_cow_search(sds_bptree_transaction *btxn, void *key)
{
    if (btxn == NULL) {
        return SDS_INVALID_TXN;
    }
    return sds_bptree_search_internal(btxn->bi, btxn->root, key);
}

sds_result
sds_bptree_cow_retrieve(sds_bptree_transaction *btxn, void *key, void **target)
{
    if (btxn == NULL) {
        return SDS_INVALID_TXN;
    }
    return sds_bptree_retrieve_internal(btxn->bi, btxn->root, key, target);
}

// These are write transactions.

sds_result
sds_bptree_cow_delete(sds_bptree_transaction *btxn, void *key)
{
    sds_result result = SDS_SUCCESS;
    sds_bptree_node *cow_node = NULL;
    sds_bptree_node *target_node = NULL;
    sds_bptree_node *next_node = NULL;

#ifdef SDS_DEBUG
    sds_log("sds_bptree_cow_delete", "==> Beginning delete of %d", key);
#endif
    // Need to fail if txn is RO
    if (btxn == NULL || btxn->state != SDS_TXN_WRITE) {
        return SDS_INVALID_TXN;
    }

    /* Check for a valid key */
    if (key == NULL) {
        return SDS_INVALID_KEY;
    }

    // get the target node (which should update the paths to it.
    result = sds_bptree_search_node_path(btxn, key, &target_node);
    if (result != SDS_SUCCESS) {
        return result;
    }

    // Check the key exists
    result = sds_bptree_node_contains_key(btxn->bi->key_cmp_fn, target_node, key);
    if (result != SDS_KEY_PRESENT) {
        return result;
    }

    // Cow the node and the path to it.
    cow_node = sds_bptree_cow_node_prepare(btxn, target_node);

    next_node = cow_node;
    target_node = cow_node->parent;

    // then delete from the cowed node it.
    sds_bptree_leaf_delete(btxn->bi, next_node, key);
    result = SDS_KEY_PRESENT;

    // Now check if the cowed node is below capacity.
    // If it is below cap, decide on merge or borrow.
    if (target_node != NULL && next_node->item_count < SDS_BPTREE_HALF_CAPACITY) {
        //  -- To merge or borrow we need to cow LEFT or RIGHT node.
        sds_bptree_node *deleted_node = NULL;
        sds_bptree_node *left = NULL;
        sds_bptree_node *right = NULL;
        /* This updates the left and right parent paths, but does NOT cow!!! */
        sds_bptree_cow_node_siblings(next_node, &left, &right);
#ifdef SDS_DEBUG
        sds_log("sds_bptree_cow_delete", " %p -> %p -> %p", left, next_node, right);
        sds_log("sds_bptree_cow_delete", " next_node->item_count = %d", next_node->item_count);
        if (right != NULL) {
            sds_log("sds_bptree_cow_delete", " right->item_count = %d", right->item_count);
            if (btxn->bi->offline_checksumming) {
                sds_bptree_crc32c_update_node(right);
            }
        }
        if (left != NULL) {
            sds_log("sds_bptree_cow_delete", " left->item_count = %d", left->item_count);
            /* Update the csum, because sibling check altered the parent ref */
            if (btxn->bi->offline_checksumming) {
                sds_bptree_crc32c_update_node(left);
            }
        }
#endif
        if (right != NULL && right->item_count > SDS_BPTREE_HALF_CAPACITY) {
/* Does right have excess keys? */
#ifdef SDS_DEBUG
            sds_log("sds_bptree_cow_delete", "Right leaf borrow");
#endif
            cow_node = sds_bptree_cow_node_prepare(btxn, right);
            sds_bptree_leaf_right_borrow(btxn->bi, next_node, cow_node);
            /* We setup next_node to be right now, so that key fixing on the path works */
            next_node = cow_node;
        } else if (left != NULL && left->item_count > SDS_BPTREE_HALF_CAPACITY) {
/* Does left have excess keys? */
#ifdef SDS_DEBUG
            sds_log("sds_bptree_cow_delete", "Left leaf borrow");
#endif
            cow_node = sds_bptree_cow_node_prepare(btxn, left);
            sds_bptree_leaf_left_borrow(btxn->bi, cow_node, next_node);
            /* This does NOT need to set next_node, because everthing is higher than us */
        } else if (right != NULL && right->item_count <= SDS_BPTREE_HALF_CAPACITY) {
/* Does right want to merge? */
#ifdef SDS_DEBUG
            sds_log("sds_bptree_cow_delete", "Right leaf contract");
#endif
            /* WARNING: DO NOT COW THE RIGHT NODE */
            sds_bptree_cow_leaf_compact(btxn, next_node, right);
            /* Setup the branch delete properly */
            deleted_node = right;
            /* Next node is correct, and ready for FIXUP */
        } else if (left != NULL && left->item_count <= SDS_BPTREE_HALF_CAPACITY) {
/* Does left want to merge? */
#ifdef SDS_DEBUG
            sds_log("sds_bptree_cow_delete", "Left leaf contract");
#endif
            cow_node = sds_bptree_cow_node_prepare(btxn, left);
            sds_bptree_cow_leaf_compact(btxn, cow_node, next_node);
            deleted_node = next_node;
            /* Next node is correct, and ready for FIXUP */
        } else {
            /* Mate, if you get here you are fucked. */
            return SDS_INVALID_NODE;
        }

        /************** Now start on the branches ****************/

        while (target_node != NULL) {

            /*
             * We need to ensure that the key we are deleting is NOT in any other node on
             * the path. If it is, we must replace it, by the rules of the B+Tree.
             */
            if (deleted_node != NULL) {
/* Make sure we delete this value from our branch */
#ifdef SDS_DEBUG
                sds_log("sds_bptree_cow_delete", "Should be removing %p from branch %p here!", deleted_node, target_node);
#endif
                sds_bptree_branch_delete(btxn->bi, target_node, deleted_node);
                /* The deed is done, remove the next reference */
                deleted_node = NULL;
            } else {
/* It means a borrow was probably done somewhere, so we need to fix the path */
#ifdef SDS_DEBUG
                sds_log("sds_bptree_cow_delete", "Should be fixing %p key to child %p here!", target_node, next_node);
#endif
                sds_bptree_branch_key_fixup(btxn->bi, target_node, next_node);
            }
            /* Then, check the properties of the node are upheld. If not, work out
             * what needs doing.
             */
            next_node = target_node;
            target_node = target_node->parent;
            /* Is the node less than half? */
            if (target_node != NULL && next_node->item_count < SDS_BPTREE_HALF_CAPACITY) {
                /* Now we do borrow and merge for branches. */

                /* Get siblings */
                left = NULL;
                right = NULL;
                /* This updates the left and right parent paths, but does NOT cow!!! */
                sds_bptree_cow_node_siblings(next_node, &left, &right);
/* Note the conditions for HALF_CAPACITY change here due to
                 * space requirements. we only merge if < half. IE for 7 keys
                 * we have 8 links. If we have a node at 3 keys, and one at 4 keys
                 * they have 4 and 5 links each. Too many! So we have to delay
                 * the merge by a fraction, to allow space for 3 keys and 3 keys.
                 *
                 */
#ifdef SDS_DEBUG
                sds_log("sds_bptree_cow_delete", " %p -> %p -> %p", left, next_node, right);
                sds_log("sds_bptree_cow_delete", " next_node->item_count = %d", next_node->item_count);
                if (right != NULL) {
                    sds_log("sds_bptree_cow_delete", " right->item_count = %d", right->item_count);
                    if (btxn->bi->offline_checksumming) {
                        sds_bptree_crc32c_update_node(right);
                    }
                }
                if (left != NULL) {
                    sds_log("sds_bptree_cow_delete", " left->item_count = %d", left->item_count);
                    if (btxn->bi->offline_checksumming) {
                        sds_bptree_crc32c_update_node(left);
                    }
                }
#endif
                if (right != NULL && right->item_count >= SDS_BPTREE_HALF_CAPACITY) {
/* Does right have excess keys? */
#ifdef SDS_DEBUG
                    sds_log("sds_bptree_cow_delete", "Right branch borrow");
#endif
                    /* Since we are about to borrow, we need to cow RIGHT */
                    cow_node = sds_bptree_cow_node_prepare(btxn, right);

                    sds_bptree_branch_right_borrow(btxn->bi, next_node, cow_node);
                    /* We setup next_node to be right now, so that key fixing on the path works */
                    next_node = cow_node;
                } else if (left != NULL && left->item_count >= SDS_BPTREE_HALF_CAPACITY) {
/* Does left have excess keys? */
#ifdef SDS_DEBUG
                    sds_log("sds_bptree_cow_delete", "Left branch borrow");
#endif
                    /* Since we are about to borrow, we need to cow LEFT */
                    cow_node = sds_bptree_cow_node_prepare(btxn, left);

                    sds_bptree_branch_left_borrow(btxn->bi, cow_node, next_node);
                    /* Next node is still on right, key fix will work next loop. */
                } else if (right != NULL && right->item_count < SDS_BPTREE_HALF_CAPACITY) {
/* Does right want to merge? */
#ifdef SDS_DEBUG
                    sds_log("sds_bptree_cow_delete", "Right branch contract");
#endif
                    /* WARNING: DO NOT COW THE RIGHT NODE */
                    sds_bptree_cow_branch_compact(btxn, next_node, right);
                    /* Setup the branch delete properly */
                    // next_node = right;
                    deleted_node = right;
                } else if (left != NULL && left->item_count < SDS_BPTREE_HALF_CAPACITY) {
/* Does left want to merge? */
#ifdef SDS_DEBUG
                    sds_log("sds_bptree_cow_delete", "Left branch contract");
#endif
                    /* Since we are about to merge, we need to cow left */
                    /* This is much, much easier than the inverse proposition ... */
                    cow_node = sds_bptree_cow_node_prepare(btxn, left);
                    sds_bptree_cow_branch_compact(btxn, cow_node, next_node);
                    deleted_node = next_node;
                } else {
                    /* Mate, if you get here you are fucked. */
                    return SDS_INVALID_NODE;
                }

            } else if (target_node == NULL && next_node->item_count == 0) {
#ifdef SDS_DEBUG
                sds_log("sds_bptree_cow_delete", "Begin deleting the root");
#endif
                if (btxn->root != next_node) {
#ifdef SDS_DEBUG
                    sds_log("sds_bptree_cow_delete", "Transaction is corrupted");
#endif
                    result = SDS_UNKNOWN_ERROR;
                    goto fail;
                }
                // Promote the root now.
                sds_bptree_cow_root_promote(btxn, next_node);
            }

        }  // End of branch "which target_node" loop
    }      // End of cow_node capacity check.

fail:
#ifdef SDS_DEBUG
    sds_log("sds_bptree_cow_delete", "<== finishing delete of %d", key);
#endif

    return result;
}

sds_result
sds_bptree_cow_insert(sds_bptree_transaction *btxn, void *key, void *value)
{
    sds_result result = SDS_SUCCESS;
    sds_bptree_node *cow_node = NULL;
    sds_bptree_node *target_node = NULL;

#ifdef SDS_DEBUG
    sds_log("sds_bptree_cow_insert", "==> Beginning insert of %d", key);
#endif
    // Need to fail if txn is RO
    if (btxn == NULL || btxn->state != SDS_TXN_WRITE) {
        return SDS_INVALID_TXN;
    }

    // First check the key doesn't already exist.
    if (key == NULL) {
        return SDS_INVALID_KEY;
    }

    // This grabs the node, but updates the path parent pointers as we descend down.
    result = sds_bptree_search_node_path(btxn, key, &target_node);
    if (result != SDS_SUCCESS) {
        return result;
    }

    if (sds_bptree_node_contains_key(btxn->bi->key_cmp_fn, target_node, key) == SDS_KEY_PRESENT) {
        // sds_bptree_node_list_release(&target_path);
        return SDS_DUPLICATE_KEY;
    }

    // Prep the node for this.
    // This will walk up the branch, and prepare the nodes, then the algo
    // for the insert will "just work", even split (provided you use the right)
    // txn id.
    //
    // This will be harder for delete due to merging of nodes.
    cow_node = sds_bptree_cow_node_prepare(btxn, target_node);

    /* The insert will happen now. Get the key ready ... */
    void *insert_key = btxn->bi->key_dup_fn(key);

    // Insert to the leaf (if it is one)
    if (cow_node->item_count < SDS_BPTREE_DEFAULT_CAPACITY) {
        sds_bptree_leaf_insert(btxn->bi, cow_node, insert_key, value);
    } else {
        sds_bptree_cow_leaf_split_and_insert(btxn, cow_node, insert_key, value);
    }
// Else, insert to leaf and let things happen.
#ifdef SDS_DEBUG
    sds_log("sds_bptree_cow_insert", "<== Finishing insert of %p", key);
#endif

    return SDS_SUCCESS;
}

sds_result
sds_bptree_cow_update(sds_bptree_transaction *btxn, void *key, void *value)
{
    sds_result result = SDS_SUCCESS;
    sds_bptree_node *cow_node = NULL;
    sds_bptree_node *target_node = NULL;

#ifdef SDS_DEBUG
    sds_log("sds_bptree_cow_update", "==> Beginning update of %d", key);
#endif
    // Need to fail if txn is RO
    if (btxn == NULL || btxn->state != SDS_TXN_WRITE) {
        return SDS_INVALID_TXN;
    }

    // First check the key doesn't already exist.
    if (key == NULL) {
        return SDS_INVALID_KEY;
    }

    // This grabs the node, but updates the path parent pointers as we descend down.
    result = sds_bptree_search_node_path(btxn, key, &target_node);
    if (result != SDS_SUCCESS) {
        return result;
    }

    if (sds_bptree_node_contains_key(btxn->bi->key_cmp_fn, target_node, key) == SDS_KEY_NOT_PRESENT) {
        // Call insert instead!
        sds_bptree_cow_insert(btxn, key, value);
    } else {

        cow_node = sds_bptree_cow_node_prepare(btxn, target_node);

        sds_bptree_cow_node_update(btxn, cow_node, key, value);
    }

// Else, insert to leaf and let things happen.
#ifdef SDS_DEBUG
    sds_log("sds_bptree_cow_update", "<== Finishing update of %d", key);
#endif
    return SDS_SUCCESS;
}

// Does this need to work on a transaction perhaps to verify the tree is "sane"?
sds_result
sds_bptree_cow_verify(sds_bptree_cow_instance *binst)
{
    sds_result result = SDS_SUCCESS;
// Verify the instance
#ifdef SDS_DEBUG
    if (binst->bi->offline_checksumming) {
        result = sds_bptree_crc32c_verify_cow_instance(binst);
        if (result != SDS_SUCCESS) {
            return result;
        }
        result = sds_bptree_crc32c_verify_instance(binst->bi);
        if (result != SDS_SUCCESS) {
            return result;
        }
    }
#endif

    // First, take an ro txn. We just verify this.
    sds_bptree_transaction *btxn = NULL;
    result = sds_bptree_cow_rotxn_begin(binst, &btxn);
    if (result != SDS_SUCCESS) {
        return result;
    }


#ifdef SDS_DEBUG
    if (binst->bi->offline_checksumming) {
        result = sds_bptree_crc32c_verify_btxn(btxn);
        if (result != SDS_SUCCESS) {
            return result;
        }
    }
#endif

    /* Now verify the tree. */
    result = sds_bptree_map_nodes(binst->bi, btxn->root, sds_bptree_cow_verify_node);

    /* Close the txn */
    sds_bptree_cow_rotxn_close(&btxn);

#ifdef SDS_DEBUG
    sds_log("sds_bptree_cow_verify", "--> Verification result %d\n", result);
#endif

    return result;
}

static sds_result
sds_node_to_dot(sds_bptree_instance *binst __attribute__((unused)), sds_bptree_node *node)
{
    if (node == NULL) {
        return SDS_INVALID_NODE;
    }
    // Given the node write it out as:
    fprintf(fp, "subgraph c%" PRIu32 " { \n    rank=\"same\";\n", node->level);
    fprintf(fp, "    node_%p [label =\" {  node=%p items=%d txn=%" PRIu64 " parent=%p | { <f0> ", node, node, node->item_count, node->txn_id, node->parent);
    for (uint64_t i = 0; i < SDS_BPTREE_DEFAULT_CAPACITY; i++) {
        fprintf(fp, "| %" PRIu64 " | <f%" PRIu64 "> ", (uint64_t)((uintptr_t)node->keys[i]), i + 1);
    }
    fprintf(fp, "}}\"]; \n}\n");
    return SDS_SUCCESS;
}

static sds_result
sds_node_ptrs_to_dot(sds_bptree_instance *binst __attribute__((unused)), sds_bptree_node *node)
{
    // for a given node,
    // printf("\"node0\":f0 -> \"node1\"");
    if (node->level == 0) {
        if (node->values[SDS_BPTREE_DEFAULT_CAPACITY] != NULL) {
            // Work around a graphviz display issue, with Left and Right pointers
            fprintf(fp, "\"node_%p\" -> \"node_%p\"; \n", node, node->values[SDS_BPTREE_DEFAULT_CAPACITY]);
        }
    } else {
        for (uint64_t i = 0; i < SDS_BPTREE_BRANCH; i++) {
            if (node->values[i] != NULL) {
                if (i == SDS_BPTREE_DEFAULT_CAPACITY) {
                    // Work around a graphviz display issue, with Left and Right pointers
                    fprintf(fp, "\"node_%p\" -> \"node_%p\"; \n", node, node->values[i]);
                } else {
                    fprintf(fp, "\"node_%p\":f%" PRIu64 " -> \"node_%p\"; \n", node, i, node->values[i]);
                }
            }
        }
    }

    return SDS_SUCCESS;
}


sds_result
sds_bptree_cow_display(sds_bptree_transaction *btxn)
{
    sds_result result = SDS_SUCCESS;

    char *path = malloc(sizeof(char) * 36);
    print_iter += 1;
#ifdef SDS_DEBUG
    sds_log("sds_bptree_cow_display", "Writing step %03d\n", print_iter);
#endif
    sprintf(path, "/tmp/graph_%03" PRIu64 ".dot", print_iter);

    /* Show the trees for all transactions. How can we do this atomically */
    fp = fopen(path, "w+");

    fprintf(fp, "digraph g {\n");
    fprintf(fp, "node [shape = record,height=.1];\n");

    fprintf(fp, "nodehdr[label = \"COW B+Tree txn_id=%" PRIu64 " \"];\n", btxn->txn_id);

    // Problem here is that fp in sds_node_to_dot is not in this file ....
    result = sds_bptree_map_nodes(btxn->bi, btxn->root, sds_node_to_dot);
    // HANDLE THE RESULT
    result = sds_bptree_map_nodes(btxn->bi, btxn->root, sds_node_ptrs_to_dot);
    // HANDLE THE RESULT

    // Should this write a dot file?
    fprintf(fp, "}\n");
    fclose(fp);
    fp = NULL;
    free(path);

    return result;
}

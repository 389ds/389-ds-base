/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "bpt.h"
// Used by our printing code.
FILE *fp = NULL;


sds_result
sds_bptree_map_nodes(sds_bptree_instance *binst, sds_bptree_node *root, sds_result (*fn)(sds_bptree_instance *binst, sds_bptree_node *) ) {
    sds_bptree_node_list *cur = sds_malloc(sizeof(sds_bptree_node_list));
    sds_bptree_node_list *prev = cur;
    sds_bptree_node_list *tail = cur;

    if (binst == NULL) {
        return SDS_NULL_POINTER;
    }

    cur->node = root;
    cur->next = NULL;
    // This should map over all NODEs of the DB, and call FN on them.
    // The fn takes one node, and returs a result if successful. No reason it can't recurse ....
    // WARNING: In the future you may need to make this non recursive, as large data sets may exceed stack in recursive.
    sds_result final_result = SDS_SUCCESS;
    sds_result result = SDS_SUCCESS;

    while (cur != NULL) {
#ifdef DEBUG
        // sds_log("sds_bptree_map_nodes", "node_%p ...", cur->node);
#endif
        if (cur->node->level > 0) {
            // Has to be <= here as this is access values, not keys!
            for (size_t i = 0; i <= cur->node->item_count; i++) {
                // Alloc a new element, and shuffle along ....
                if (cur->node->values[i] != NULL) {
                    tail->next = sds_malloc(sizeof(sds_bptree_node_list));
                    tail = tail->next;
                    tail->node = (sds_bptree_node *)cur->node->values[i];
                    tail->next = NULL;
                }
            }
        }
        result = fn(binst, cur->node);
        if (result != SDS_SUCCESS) {
#ifdef DEBUG
            sds_log("sds_bptree_map_nodes", "node_%p failed %d", cur->node, result);
#endif
            final_result = result;
        }
        prev = cur;
        cur = cur->next;
        free(prev);
    }
    // Start at the root ...
    return final_result;
}

sds_result
sds_bptree_verify_instance(sds_bptree_instance *binst)
{
    sds_result result = SDS_SUCCESS;
    // check the checksum.
#ifdef DEBUG
    if (binst->offline_checksumming) {
        result = sds_bptree_crc32c_verify_instance(binst);
    }
#endif
    // Verify that root node is not NULL (It can be empty)
    return result;
}

sds_result
sds_bptree_verify_node(sds_bptree_instance *binst, sds_bptree_node *node) {
    // - verify the hash of the node metadata
#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_result result = sds_bptree_crc32c_verify_node(node);
        if (result != SDS_SUCCESS) {
            return result;
        }
    }
#endif

    // - item_count matches number of non-null keys.
    for (size_t i = 0; i < node->item_count; i++) {
        if (node->keys[i] == NULL)
        {
            sds_log("sds_bptree_verify_node", "%d \n", node->item_count);
            return SDS_INVALID_KEY;
        }

    }


    // Check that our keys are in order in the node.
    // We only need to compare left to right, because if we have:
    // A B C D
    // If A is < B, C, D, then when we check B, we only need assert B < C, D, as A < B is already true
    for (size_t i = 0; i < node->item_count; i++) {
        for (size_t j = 0; j < i && i != j; j++) {
            // printf("cmp: ! node->keys[%d] %d > node->keys[%d] %d\n", j, node->keys[j], i, node->keys[i]);
            int64_t result = binst->key_cmp_fn(node->keys[i], node->keys[j]);
            if (result == 0) {
                return SDS_DUPLICATE_KEY;
            } else if (result < 0) {
                // If the key on the right is less than the key on the left
                return SDS_INVALID_KEY;
            }
        }
    }

    if (node->level == 0) {

        /* Verify the values in the node if we are a leaf. */
        /* Leaves must always adhere to the properties of the tree. IE half size */
        if (node->parent != NULL && node->item_count < (SDS_BPTREE_HALF_CAPACITY - 1)) {
            return SDS_INVALID_NODE;
        }

#ifdef DEBUG
        /*
        // - verify the value hashes
        // Now that we are sure of all value sizes and pointers, lets do the checksum of the data in the values
        // - NULL values have 0 size
        // - non-null have > 0 size
        for (size_t i = 0; i < node->item_count; i++) {
            sds_bptree_value *value = (sds_bptree_value *)node->values[i];

            if (value != NULL && value->size == 0 )
            {
                return SDS_INVALID_VALUE_SIZE;
            }

        }

        for (size_t i = 0; i < node->item_count; i++) {
            if (node->values[i] != NULL) {
                if (sds_bptree_crc32c_verify_value(node->values[i]) != SDS_SUCCESS) {
                    return SDS_CHECKSUM_FAILURE;
                }
            }
        }
        */
#endif

        // Check that our highest element is smaller than binst->branch_factor[0]
        // Because of the above assertion, we now know that our node is in order
        // And given the next is also, we can assert ALL nodes are in key order.
        sds_bptree_node *rnode = (sds_bptree_node *)node->values[SDS_BPTREE_DEFAULT_CAPACITY];
        if (rnode != NULL) {
            int64_t result = binst->key_cmp_fn(node->keys[node->item_count - 1], rnode->keys[0]);
            if (result > 0) {
                return SDS_INVALID_KEY;
            } else if (result == 0) {
                return SDS_DUPLICATE_KEY;
            }
        }

        // Is there an effective way to do a duplicate key check?
        // I think the above checks already assert there are no dupes in the tree.

    } else {
        // As a branch, to be valid we must have at LEAST 1 item and 2 links
        // The later check will validate the links for us :)
        if (node->item_count == 0) {
            return SDS_INVALID_NODE;
        }

        // Check that all our childrens parent refs are to us.
        for (uint16_t i = 0; i <= node->item_count; i++) {
            sds_bptree_node *cnode = (sds_bptree_node *)node->values[i];
            if (cnode->parent != node) {
                return SDS_INVALID_POINTER;
            }
        }

        if (binst->root == node) {
            if (node->parent != NULL) {
                return SDS_INVALID_POINTER;
            }
        }

        // Check that for key at index i, link i key[0] is < key.
        // For key at index i, link i + 1, is > key
        // For one key, there will always be a left and right link. That's the way it is :)
        // So we can fail if one is NULL
        for (uint16_t i = 0; i < node->item_count; i++) {
            if (node->keys[i] != NULL) {
                sds_bptree_node *lnode = (sds_bptree_node *)node->values[i];
                sds_bptree_node *rnode = (sds_bptree_node *)node->values[i + 1];

                if (lnode == NULL || rnode == NULL) {
                    // We should have two children!
                    return SDS_INVALID_POINTER;
                }
                // Check that all left keys are LESS.
                sds_bptree_node_list *path = NULL;
                size_t j = 0;

                while (lnode != NULL) {
                    for (j = 0; j < lnode->item_count; j++) {
                        /* This checks that all left keys *and* their children are less */
                        int64_t result = binst->key_cmp_fn(lnode->keys[j], node->keys[i]);
                        if (result >= 0) {
#ifdef DEBUG
                            sds_log("sds_bptree_verify_node", "    fault is in node %p with left child %p", node, lnode);
#endif
                            return SDS_INVALID_KEY_ORDER;
                        }
                        if (lnode->level > 0) {
                            sds_bptree_node_list_push(&path, lnode->values[j]);
                        }
                    }
                    if (lnode->level > 0) {
                        sds_bptree_node_list_push(&path, lnode->values[j]);
                    }
                    lnode = sds_bptree_node_list_pop(&path);
                }
                // All right keys are greater or equal
                while (rnode != NULL) {
                    for (j = 0; j < rnode->item_count; j++) {
                        /* This checks that all right keys are greatr or equal and their children */
                        int64_t result = binst->key_cmp_fn(rnode->keys[j], node->keys[i]);
                        if (result < 0) {
#ifdef DEBUG
                            sds_log("sds_bptree_verify_node", "    fault is in node %p with right child %p", node, rnode);
#endif
                            return SDS_INVALID_KEY_ORDER;
                        }
                        if (rnode->level > 0) {
                            sds_bptree_node_list_push(&path, rnode->values[j]);
                        }
                    }
                    if (rnode->level > 0) {
                        sds_bptree_node_list_push(&path, rnode->values[j]);
                    }
                    rnode = sds_bptree_node_list_pop(&path);
                } // While rnode
            }
        } // end for

    } // end else is_leaf

    return SDS_SUCCESS;
}

sds_result
sds_bptree_verify(sds_bptree_instance *binst) {
#ifdef DEBUG
    sds_log("sds_bptree_verify", "==> Beginning verification of instance %p", binst);
#endif
    // How do we get *all* the errors here, for every node? ...
    // Do me make a error msg buffer and fill it?
    sds_result total_result = SDS_SUCCESS;
    sds_result result = SDS_SUCCESS;

    result = sds_bptree_verify_instance(binst);
    if (result != SDS_SUCCESS) {
        total_result = result;
    }

    // - No node id exceeds max id. -- This requires a tree walk ...
    // - no duplicate node id
    // - no duplicate keys
    result = sds_bptree_map_nodes(binst, binst->root, sds_bptree_verify_node);
    if (result != SDS_SUCCESS) {
        total_result = result;
    }

#ifdef DEBUG
    sds_log("sds_bptree_verify", "==> Completing verification of instance %p %d", binst, total_result);
#endif

    return total_result;
}

/* This display code runs in two passes.  */
static sds_result
sds_node_to_dot(sds_bptree_instance *binst __attribute__((unused)), sds_bptree_node *node) {
    if (node == NULL) {
        return SDS_INVALID_NODE;
    }
    // Given the node write it out as:
    fprintf(fp, "subgraph c%"PRIu32" { \n    rank=\"same\";\n", node->level);
    fprintf(fp, "    node_%p [label =\" {  node=%p items=%d txn=%"PRIu64" parent=%p | { <f0> ", node, node, node->item_count, node->txn_id, node->parent );
    for (size_t i  = 0; i < SDS_BPTREE_DEFAULT_CAPACITY; i++) {
        fprintf(fp, "| %" PRIu64 " | <f%"PRIu64"> ", (uint64_t)node->keys[i], i + 1 );
    }
    fprintf(fp, "}}\"]; \n}\n");
    return SDS_SUCCESS;
}

static sds_result
sds_node_ptrs_to_dot(sds_bptree_instance *binst __attribute__((unused)), sds_bptree_node *node) {
    // for a given node, 
    // printf("\"node0\":f0 -> \"node1\"");
    if (node->level == 0) {
        if (node->values[SDS_BPTREE_DEFAULT_CAPACITY] != NULL) {
            // Work around a graphviz display issue, with Left and Right pointers
            fprintf(fp, "\"node_%p\" -> \"node_%p\"; \n", node, node->values[SDS_BPTREE_DEFAULT_CAPACITY] );
        }
    } else {
        for (size_t i  = 0; i < SDS_BPTREE_BRANCH; i++) {
            if (node->values[i] != NULL) {
                if (i == SDS_BPTREE_DEFAULT_CAPACITY) {
                    // Work around a graphviz display issue, with Left and Right pointers
                    fprintf(fp, "\"node_%p\" -> \"node_%p\"; \n", node, node->values[i] );
                } else {
                    fprintf(fp, "\"node_%p\":f%"PRIu64" -> \"node_%p\"; \n", node, i, node->values[i] );
                }
            }
        }
    }

    return SDS_SUCCESS;
}

sds_result
sds_bptree_display(sds_bptree_instance *binst) {
    sds_result result = SDS_SUCCESS;

    char *path = malloc(sizeof(char) * 20);
#ifdef DEBUG
    sds_log("sds_bptree_display", "Writing step %03d\n", binst->print_iter);
#endif
    sprintf(path, "/tmp/graph_%03d.dot", binst->print_iter);
    binst->print_iter += 1;
#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_bptree_crc32c_update_instance(binst);
    }
#endif
    // Because we change the root inst, we have to checksum it.
    // Open a file to write into.
    fp = fopen(path, "w+");

    fprintf(fp, "digraph g {\n");
    fprintf(fp, "node [shape = record,height=.1];\n");

    fprintf(fp, "nodehdr[label = \"B+Tree \"];\n");

    result = sds_bptree_map_nodes(binst, binst->root, sds_node_to_dot);
    // HANDLE THE RESULT
    result = sds_bptree_map_nodes(binst, binst->root, sds_node_ptrs_to_dot);
    // HANDLE THE RESULT

    // Should this write a dot file?
    fprintf(fp, "}\n");
    fclose(fp);
    fp = NULL;
    free(path);

    return result;
}



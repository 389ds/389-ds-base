/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * All rights reserved.
 *
 * License: License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#pragma once

#include "../sds_internal.h"
#include <sds.h>

#ifdef DEBUG
sds_result sds_bptree_crc32c_verify_instance(sds_bptree_instance *binst);
void sds_bptree_crc32c_update_instance(sds_bptree_instance *binst);
sds_result sds_bptree_crc32c_verify_node(sds_bptree_node *node);
void sds_bptree_crc32c_update_node(sds_bptree_node *node);
/*
sds_result sds_bptree_crc32c_verify_value(struct sds_bptree_value *value);
void sds_bptree_crc32c_update_value(struct sds_bptree_value *value);
*/
#endif

/* Node manipulation */

sds_bptree_node *sds_bptree_arrays_to_node_list(void **keys, void **values, size_t count);
sds_result sds_bptree_node_list_to_tree(sds_bptree_instance *binst, sds_bptree_node *node);
sds_bptree_node * sds_bptree_node_create(void);
sds_result sds_bptree_node_destroy(sds_bptree_instance *binst, sds_bptree_node *node);
sds_result sds_bptree_node_contains_key(sds_bptree_instance *binst, sds_bptree_node *node, void *key);
size_t sds_bptree_node_key_eq_index(sds_bptree_instance *binst, sds_bptree_node *node, void *key);
size_t sds_bptree_node_key_lt_index(sds_bptree_instance *binst, sds_bptree_node *node, void *key);
void sds_bptree_node_siblings(sds_bptree_node *target, sds_bptree_node **left, sds_bptree_node **right);
sds_result sds_bptree_node_retrieve_key(sds_bptree_instance *binst, sds_bptree_node *node, void *key, void **target);
void sds_bptree_node_node_replace(sds_bptree_node *target_node, sds_bptree_node *origin_node, sds_bptree_node *replace_node);
/*
sds_result sds_bptree_value_create(struct sds_bptree_instance *binst, void *value, size_t value_size, struct sds_bptree_value **new_value);
*/

/* Search and retrieve */
sds_result sds_bptree_search_node(sds_bptree_instance *binst, sds_bptree_node *root, void *key, sds_bptree_node** target_out_node);
sds_result sds_bptree_search_internal(sds_bptree_instance *binst, sds_bptree_node *root, void *key);
sds_result sds_bptree_retrieve_internal(sds_bptree_instance *binst, sds_bptree_node *root, void *key, void **target);

void * sds_bptree_node_leftmost_child_key(sds_bptree_node *parent);

/* Leaf insert and delete */

void sds_bptree_leaf_insert(sds_bptree_instance *binst, sds_bptree_node *node, void *key, void *new_value);
void sds_bptree_leaf_split_and_insert(sds_bptree_instance *binst, sds_bptree_node *left_node, sds_bptree_node *right_node, void *key, void *new_value);
void sds_bptree_leaf_compact(sds_bptree_instance *binst, sds_bptree_node *left, sds_bptree_node *right);
void sds_bptree_leaf_delete(sds_bptree_instance *binst, sds_bptree_node *node, void *key);
void sds_bptree_leaf_right_borrow(sds_bptree_instance *binst, sds_bptree_node *left, sds_bptree_node *right);
void sds_bptree_leaf_left_borrow(sds_bptree_instance *binst, sds_bptree_node *left, sds_bptree_node *right);

/* Branch insert and delete */

sds_result sds_bptree_insert_leaf_node(sds_bptree_instance *binst, sds_bptree_node *tnode, sds_bptree_node *nnode, void *nkey);
void sds_bptree_branch_split_and_insert(sds_bptree_instance *binst, sds_bptree_node *left_node, sds_bptree_node *right_node, void *key, sds_bptree_node *new_node, void **excluded_key);
void sds_bptree_branch_insert(sds_bptree_instance *binst, sds_bptree_node *node, void *key, sds_bptree_node *new_node);
void sds_bptree_branch_delete(sds_bptree_instance *binst, sds_bptree_node *node, sds_bptree_node *delete_node);
void sds_bptree_branch_key_fixup(sds_bptree_instance *binst, sds_bptree_node *parent, sds_bptree_node *child);
void sds_bptree_branch_compact(sds_bptree_instance *binst, sds_bptree_node *left, sds_bptree_node *right);
void sds_bptree_branch_right_borrow(sds_bptree_instance *binst, sds_bptree_node *left, sds_bptree_node *right);
void sds_bptree_branch_left_borrow(sds_bptree_instance *binst, sds_bptree_node *left, sds_bptree_node *right);

/* Root management */
void sds_bptree_root_promote(sds_bptree_instance *binst, sds_bptree_node *root);
void sds_bptree_root_insert(sds_bptree_instance *binst, sds_bptree_node *left_node, sds_bptree_node *right_node, void *key);

/* Node path tracking */

void sds_bptree_node_list_push(sds_bptree_node_list **list, sds_bptree_node *node);
sds_bptree_node * sds_bptree_node_list_pop(sds_bptree_node_list **list);
void sds_bptree_node_list_release(sds_bptree_node_list **list);
sds_bptree_node *sds_bptree_node_min(sds_bptree_instance *binst);

/* Set list operators */

void sds_bptree_node_list_append(sds_bptree_node **node, void *key, void *value);

/* Internal */

sds_result sds_bptree_map_nodes(sds_bptree_instance *binst, sds_bptree_node *root, sds_result (*fn)(sds_bptree_instance *binst, sds_bptree_node *));
sds_result sds_bptree_display(sds_bptree_instance *binst);

/* Verification */
sds_result sds_bptree_verify_node(sds_bptree_instance *binst, sds_bptree_node *node);



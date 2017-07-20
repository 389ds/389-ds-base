/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "bpt.h"

/* Will be used for transactions. */
void
sds_bptree_node_list_push(sds_bptree_node_list **list, sds_bptree_node *node)
{
    // node, next
    if (list == NULL) {
        return;
    }
    sds_bptree_node_list *new_head = sds_malloc(sizeof(sds_bptree_node_list));
    new_head->node = node;
    new_head->next = *list;
    *list = new_head;
    return;
}

sds_bptree_node *
sds_bptree_node_list_pop(sds_bptree_node_list **list)
{
    // Pop and free the list element.
    if (list == NULL || *list == NULL) {
        return NULL;
    }
    sds_bptree_node *next_node = (*list)->node;
    sds_bptree_node_list *old;
    old = *list;
    *list = old->next;
    sds_free(old);
    return next_node;
}

void
sds_bptree_node_list_release(sds_bptree_node_list **list)
{
    if (list == NULL || *list == NULL) {
        return;
    }
    sds_bptree_node_list *next_node = *list;
    sds_bptree_node_list *old;
    while (next_node != NULL) {
        old = next_node;
        next_node = old->next;
        sds_free(old);
    }
}

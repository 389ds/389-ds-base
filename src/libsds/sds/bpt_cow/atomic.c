/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "bpt_cow.h"

sds_result
sds_bptree_cow_search_atomic(sds_bptree_cow_instance *binst, void *key)
{
    sds_bptree_transaction *ro_btxn = NULL;
    sds_result result = sds_bptree_cow_rotxn_begin(binst, &ro_btxn);
    if (result != SDS_SUCCESS) {
        return result;
    }

    sds_result search_result = sds_bptree_cow_search(ro_btxn, key);
    result = sds_bptree_cow_rotxn_close(&ro_btxn);
    if (result != SDS_SUCCESS) {
        return result;
    }

    return search_result;
}

sds_result
sds_bptree_cow_retrieve_atomic(sds_bptree_cow_instance *binst, void *key, void **target)
{
    void *int_target = NULL;
    sds_bptree_transaction *ro_btxn = NULL;
    sds_result result = sds_bptree_cow_rotxn_begin(binst, &ro_btxn);
    if (result != SDS_SUCCESS) {
        return result;
    }

    sds_result search_result = sds_bptree_cow_retrieve(ro_btxn, key, &int_target);
    if (search_result == SDS_KEY_PRESENT) {
        *target = binst->bi->value_dup_fn(int_target);
    }
    result = sds_bptree_cow_rotxn_close(&ro_btxn);
    if (result != SDS_SUCCESS) {
        if (*target != NULL) {
            binst->bi->value_free_fn(*target);
        }
        return result;
    }

    return search_result;
}

sds_result
sds_bptree_cow_delete_atomic(sds_bptree_cow_instance *binst, void *key)
{
    sds_bptree_transaction *wr_btxn = NULL;
    sds_result result = sds_bptree_cow_wrtxn_begin(binst, &wr_btxn);
    if (result != SDS_SUCCESS) {
        return result;
    }

    sds_result search_result = sds_bptree_cow_delete(wr_btxn, key);
    if (search_result == SDS_SUCCESS) {
        result = sds_bptree_cow_wrtxn_commit(&wr_btxn);
    } else {
        result = sds_bptree_cow_wrtxn_abort(&wr_btxn);
    }
    if (result != SDS_SUCCESS) {
        return result;
    }

    return search_result;
}

sds_result
sds_bptree_cow_insert_atomic(sds_bptree_cow_instance *binst, void *key, void *value)
{
    sds_bptree_transaction *wr_btxn = NULL;
    sds_result result = sds_bptree_cow_wrtxn_begin(binst, &wr_btxn);
    if (result != SDS_SUCCESS) {
        return result;
    }

    sds_result search_result = sds_bptree_cow_insert(wr_btxn, key, value);
    if (search_result == SDS_SUCCESS) {
        result = sds_bptree_cow_wrtxn_commit(&wr_btxn);
    } else {
        result = sds_bptree_cow_wrtxn_abort(&wr_btxn);
    }
    if (result != SDS_SUCCESS) {
        return result;
    }

    return search_result;
}


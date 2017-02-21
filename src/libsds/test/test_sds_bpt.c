/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * All rights reserved.
 *
 * License: License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "test_sds.h"

static void
test_asan_overflow(void)
{
    char *test = sds_malloc(4 * sizeof(char));
    test[100] = 'a';
    // At this point, ASAN should explode.
    sds_log("test_asan", "FAIL: This should not be possible!");
}

static void
test_asan_leak(void)
{
    // Just malloc this, it should fail in the end.
    char *test = sds_malloc(4 * sizeof(char));
}

static void
test_1_invalid_binst_ptr(void **state __attribute__((unused)))
{
    // This will test that the bptree init handles a null pointer correctly.
    sds_result result = SDS_SUCCESS;

    result = sds_bptree_init(NULL, 0, sds_uint64_t_compare, sds_free, sds_uint64_t_free, sds_uint64_t_dup);

    assert_int_equal(result, SDS_NULL_POINTER);

}

// Template: sds_result test_fn(sds_bptree_instance *binst) {}

static void
test_3_single_insert(void **state)
{
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;
    uint64_t key = 1;
    // We need to strdup this, so we can free it.
    char *test = strdup("Some random value");
    result = sds_bptree_insert(binst, (void *)&key, (void *)test);
    assert_int_equal(result, SDS_SUCCESS);

    result = sds_bptree_search(binst, (void *)&key);
    assert_int_equal(result, SDS_KEY_PRESENT);
}

static void
test_4_single_null_insert_fn(void **state)
{
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;
    uint64_t key = 1;
    result = sds_bptree_insert(binst, (void *)&key, NULL);
    assert_int_equal(result, SDS_SUCCESS);
    result = sds_bptree_search(binst, (void *)&key);
    assert_int_equal(result, SDS_KEY_PRESENT);
}

/*
static void
test_5_single_null_mismatch_size_insert_fn(void **state)
{
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;
    result = sds_bptree_insert(binst, (void *)1, NULL);
    assert_int_equal(result, SDS_INVALID_VALUE_SIZE);
    result = sds_bptree_search(binst, (void *)1);
    assert_int_equal(result, SDS_KEY_NOT_PRESENT);
}
*/

static void
test_6_insert_less_than_no_split(void **state) {

    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;

    uint64_t key1 = 1;
    uint64_t key2 = 2;

    result = sds_bptree_insert(binst, (void *)&key2, NULL);
    assert_int_equal(result, SDS_SUCCESS);

    result = sds_bptree_insert(binst, (void *)&key1, NULL);
    assert_int_equal(result, SDS_SUCCESS);

    result = sds_bptree_search(binst, (void *)&key1);
    assert_int_equal(result, SDS_KEY_PRESENT);

    result = sds_bptree_search(binst, (void *)&key2);
    assert_int_equal(result, SDS_KEY_PRESENT);
}

static void
test_7_insert_greater_than_no_split(void **state) {
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;

    uint64_t key2 = 2;
    uint64_t key3 = 3;

    result = sds_bptree_insert(binst, (void *)&key2, NULL);
    assert_int_equal(result, SDS_SUCCESS);

    result = sds_bptree_insert(binst, (void *)&key3, NULL);
    assert_int_equal(result, SDS_SUCCESS);

    result = sds_bptree_search(binst, (void *)&key3);
    assert_int_equal(result, SDS_KEY_PRESENT);

    result = sds_bptree_search(binst, (void *)&key2);
    assert_int_equal(result, SDS_KEY_PRESENT);
}

static void
test_8_insert_duplicate(void **state) {
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;
    uint64_t key2 = 2;

    result = sds_bptree_insert(binst, (void *)&key2, NULL);
    assert_int_equal(result, SDS_SUCCESS);

    result = sds_bptree_insert(binst, (void *)&key2, NULL);
    assert_int_equal(result, SDS_DUPLICATE_KEY);

    result = sds_bptree_search(binst, (void *)&key2);
    assert_int_equal(result, SDS_KEY_PRESENT);
}

static void
test_9_insert_fill_and_split(void **state) {
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;

    // Add two to guarantee we don't conflict
    for (uint64_t i = 2; i <= SDS_BPTREE_DEFAULT_CAPACITY + 2; i++) {
        result = sds_bptree_insert(binst, (void *)&i, NULL);
        assert_int_equal(result, SDS_SUCCESS);
    }

    for (uint64_t i = 2; i <= SDS_BPTREE_DEFAULT_CAPACITY + 2; i++) {
        result = sds_bptree_search(binst, (void *)&i);
        assert_int_equal(result, SDS_KEY_PRESENT);
    }

}

static void
test_10_tamper_with_inst(void **state __attribute__((unused))) {
    // The verifictation in the wrapper will fail for us.

    // Create a new tree, and then destroy it.
    sds_bptree_instance *binst = NULL;
    sds_result result = SDS_SUCCESS;

    result = sds_bptree_init(&binst, 1, sds_uint64_t_compare, sds_free, sds_uint64_t_free, sds_uint64_t_dup);
    assert_int_equal(result, SDS_SUCCESS);

    binst->print_iter += 1;

    result = sds_bptree_verify(binst);
    assert_int_equal(result, SDS_CHECKSUM_FAILURE);

    result = sds_bptree_destroy(binst);
    /* If this reports unknown, it means we may not have freed some nodes. */
    assert_int_equal(result, SDS_SUCCESS);
}

static void
test_11_tamper_with_node(void **state __attribute__((unused))) {
    // The verifictation in the wrapper will fail for us.

    // Create a new tree, and then destroy it.
    sds_bptree_instance *binst = NULL;
    sds_result result = SDS_SUCCESS;

    result = sds_bptree_init(&binst, 1, sds_uint64_t_compare, sds_free, sds_uint64_t_free, sds_uint64_t_dup);
    assert_int_equal(result, SDS_SUCCESS);

    binst->root->keys[0] = (void *)1;

    result = sds_bptree_verify(binst);
    assert_int_equal(result, SDS_CHECKSUM_FAILURE);

    result = sds_bptree_destroy(binst);
    assert_int_equal(result, SDS_SUCCESS);
}

static void
test_12_insert_fill_split_and_grow(void **state) {
    // This should make the tree grow and split enough to need to add extra heigh beyond the root.
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;

    for (uint64_t i = 0; i <= ((SDS_BPTREE_DEFAULT_CAPACITY + 1) << 3) ; i++) {
        uint64_t ti = i + 2;
        // Add two to guarantee we don't conflict
        result = sds_bptree_insert(binst, (void *)&ti, NULL);
        assert_int_equal(result, SDS_SUCCESS);
        result = sds_bptree_verify(binst);

        if (result != SDS_SUCCESS) {
            sds_log("bptree_test_teardown", "FAIL: B+Tree verification failed %d binst", result);
        }
        assert_int_equal(result, SDS_SUCCESS);
    }

    for (uint64_t i = 0; i <= ((SDS_BPTREE_DEFAULT_CAPACITY + 1) << 3) ; i++) {
        uint64_t ti = i + 2;
        result = sds_bptree_search(binst, (void *)&ti);
        if (result != SDS_KEY_PRESENT) {
            sds_log("bptree_test_teardown", "FAIL: Can not find %d", ti);
        }
        assert_int_equal(result, SDS_KEY_PRESENT);
    }

}

static void
test_13_insert_fill_split_and_grow_inverse(void **state) {
    // This should make the tree grow and split enough to need to add extra heigh beyond the root.
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;

    for (uint64_t i = ((SDS_BPTREE_DEFAULT_CAPACITY + 1) << 3) + 1; i > 0  ; i--) {
        // Add two to guarantee we don't conflict
        uint64_t ti = i + 2;
        result = sds_bptree_insert(binst, (void *)&ti, NULL);
        assert_int_equal(result, SDS_SUCCESS);
        result = sds_bptree_verify(binst);

        if (result != SDS_SUCCESS) {
            sds_log("bptree_test_teardown", "FAIL: B+Tree verification failed %d binst", result);
        }
        assert_int_equal(result, SDS_SUCCESS);
    }

    for (uint64_t i = ((SDS_BPTREE_DEFAULT_CAPACITY + 1) << 3) + 1; i > 0  ; i--) {
        uint64_t ti = i + 2;
        result = sds_bptree_search(binst, (void *)&ti);
        if (result != SDS_KEY_PRESENT) {
            sds_log("bptree_test_teardown", "FAIL: Can not find %d", ti);
        }
        assert_int_equal(result, SDS_KEY_PRESENT);
    }
}

// test14 is search
static void
test_14_insert_random(void **state) {
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;

    for (uint64_t i = 0; i < 200 ; i++) {
        result = sds_bptree_insert(binst, (void *)&(fill_pattern[i]) , NULL);
        assert_int_equal(result, SDS_SUCCESS);

        result = sds_bptree_verify(binst);

        if (result != SDS_SUCCESS) {
            sds_log("bptree_test_teardown", "FAIL: B+Tree verification failed %d binst", result);
        }
        assert_int_equal(result, SDS_SUCCESS);

    }
    // search

    for (uint64_t i = 0; i < 200 ; i++) {
        result = sds_bptree_search(binst, (void *)&(fill_pattern[i]));
        if (result != SDS_KEY_PRESENT) {
            sds_log("bptree_test_teardown", "FAIL: Can not find %d", fill_pattern[i]);
        }
        assert_int_equal(result, SDS_KEY_PRESENT);
    }
}


// test15 is search not present

static void
test_15_search_none(void **state) {
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;
    // Add many nodes.

    for (uint64_t i = 1; i <= 20  ; i++) {
        // Add two to guarantee we don't conflict
        result = sds_bptree_insert(binst, (void *)&i , NULL);
        assert_int_equal(result, SDS_SUCCESS);
    }
    // search
    uint64_t ti = 25;
    result = sds_bptree_search(binst, (void *)&ti);
    // key should be there
    assert_int_equal(result, SDS_KEY_NOT_PRESENT);

}

static void
test_16_insert_and_retrieve(void **state) {
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;

    uint64_t key = 1;
    char *magic = strdup("magic");

    result = sds_bptree_insert(binst, (void *)&key, magic);
    assert_int_equal(result, SDS_SUCCESS);

    char *dest = NULL;
    result = sds_bptree_retrieve(binst, (void *)&key, (void **)&dest);
    assert_int_equal(result, SDS_KEY_PRESENT);

    assert_int_equal(strncmp(dest, magic, 6 ), 0);

    // Freed as part of the tree release
    // sds_free(dest);
    // sds_free(magic);
}

// test 17 insert and tamper with the data underneath
/*
static void
test_17_insert_and_tamper(void **state) {
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;

    char *magic = strdup("magic");

    result = sds_bptree_insert(binst, (void *)1, magic);
    assert_int_equal(result, SDS_SUCCESS);

    // Right, now we cheat. Because we know the internals of the library, we can
    // start to mess with stuff.

    sds_bptree_value *value = binst->root->values[0];

    char *data = (char *)value->data;
    strncpy(data, "hello", 6);

    char *dest = NULL;

    result = sds_bptree_retrieve(binst, (void *)1, (void **)&dest);
    assert_int_equal(result, SDS_CHECKSUM_FAILURE);

    // Put the original data back, else we fail the teardown validation ....
    strncpy(data, "magic", 6);

    // sds_free(dest);
    // sds_free(magic);
}
*/

static void
test_18_delete_single_value(void **state)
{
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;
    uint64_t key = 1;
    char *test = strdup("Some random value");
    sds_bptree_insert(binst, (void *)&key, (void *)test);
    assert_int_equal(result, SDS_SUCCESS);

    result = sds_bptree_delete(binst, (void *)&key);
    assert_int_equal(result, SDS_KEY_PRESENT);

    // sds_free(dest);
}

static void
test_19_delete_non_existant(void **state)
{
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;
    uint64_t key = 1;
    char *test = strdup("Some random value");
    // Need to add one to account for the null byte ... fucking C.
    sds_bptree_insert(binst, (void *)&key, (void *)test);
    assert_int_equal(result, SDS_SUCCESS);

    key = 2;
    result = sds_bptree_delete(binst, (void *)&key);
    assert_int_equal(result, SDS_KEY_NOT_PRESENT);

    // Should be null anyway!
    // sds_free(dest);
}

static void
test_20_delete_non_branch_key(void **state)
{
    sds_bptree_instance *binst = *state;
    // First, make a tree with 3 * SDS_BPTREE_DEFAULT_CAPACITY, then delete the highest
    sds_result result = SDS_SUCCESS;
    // Add many nodes.
    uint64_t i = 0;

    for (uint64_t i = 2; i <= (SDS_BPTREE_DEFAULT_CAPACITY * 3); i++) {
        // Add two to guarantee we don't conflict
        result = sds_bptree_insert(binst, (void *)&i , NULL);
        assert_int_equal(result, SDS_SUCCESS);

    }

    // Inspect the internal to the tree to get the key from the node.
    i = *(uint64_t *)(binst->root)->keys[0];

    result = sds_bptree_delete(binst, (void *)&i);
    assert_int_equal(result, SDS_KEY_PRESENT);


}

static void
test_21_delete_redist_left_leaf(void **state)
{
    /*
     * This will make two nodes, A, B. When we delete from B, we get below the
     * half node threshhold, so when we delete the key, we borrow max from A
     * into B as the new lowest node. This will *not* update the parent link
     * that points to B.
     */
    sds_bptree_instance *binst = *state;
    // First, make a tree with 3 * SDS_BPTREE_DEFAULT_CAPACITY, then delete the highest
    sds_result result = SDS_SUCCESS;
    uint64_t i = 2;
    // Add many nodes.

    for (i = 2; i <= (SDS_BPTREE_DEFAULT_CAPACITY * 3) ; i++) {
        // Add two to guarantee we don't conflict
        result = sds_bptree_insert(binst, (void *)&i , NULL);
        assert_int_equal(result, SDS_SUCCESS);
    }

    // I is increment 1 extra, so decrement it ....
    i--;

#ifdef DEBUG
    printf("Deleting %" PRIu64 "\n", i);
#endif
    result = sds_bptree_delete(binst, (void *)&i);
    assert_int_equal(result, SDS_KEY_PRESENT);

    i--;
#ifdef DEBUG
    printf("Deleting %" PRIu64 "\n", i);
#endif
    result = sds_bptree_delete(binst, (void *)&i);
    assert_int_equal(result, SDS_KEY_PRESENT);

    // How can I test for and prove that the value was redistributed?
    // Return node of the previous?

}

static void
test_22_delete_redist_right_leaf(void **state)
{
    /*
     * We make a node, A, B, where B > A. We delete two nodes under A, which will
     * force B to give nodes to A. This also causes an update of the root ref
     * to B.
     */
    sds_bptree_instance *binst = *state;
    // First, make a tree with 3 * SDS_BPTREE_DEFAULT_CAPACITY, then delete the highest
    sds_result result = SDS_SUCCESS;
    // Add many nodes.

    for (uint64_t i = 1; i <= (SDS_BPTREE_DEFAULT_CAPACITY * 3); i++) {
        // Add two to guarantee we don't conflict
        result = sds_bptree_insert(binst, (void *)&i , NULL);
        assert_int_equal(result, SDS_SUCCESS);
    }

    // Now delete these two keys
    uint64_t keya = SDS_BPTREE_DEFAULT_CAPACITY * 2;
    uint64_t keyb = keya + 1;
    // printf("Deleting %d\n", keya);
    // printf("Deleting %d\n", keyb);

    result = sds_bptree_delete(binst, (void *)&keya);
    assert_int_equal(result, SDS_KEY_PRESENT);

    result = sds_bptree_verify(binst);

    if (result != SDS_SUCCESS) {
        sds_log("bptree_test_teardown", "FAIL: B+Tree verification failed %d binst", result);
    }
    assert_int_equal(result, SDS_SUCCESS);

    /* Next delete */
    result = sds_bptree_delete(binst, (void *)&keyb);
    assert_int_equal(result, SDS_KEY_PRESENT);

    result = sds_bptree_verify(binst);

    if (result != SDS_SUCCESS) {
        sds_log("bptree_test_teardown", "FAIL: B+Tree verification failed %d binst", result);
    }
    assert_int_equal(result, SDS_SUCCESS);

}

static void
test_22_5_redist_left_borrow(void **state)
{
    /* We make a small tree, that only splits once. Then we start to delete
     * from the right node, which should force us to borrow from the left
     * The construction of the tree is critical to allow overloading the left
     * node to be > BHALF else this case won't trigger.
     *
     * This is also the first test to test deleting the root on deletion!
     */
    sds_bptree_instance *binst = *state;
    // First, make a tree with 3 * SDS_BPTREE_DEFAULT_CAPACITY, then delete the highest
    sds_result result = SDS_SUCCESS;
    // Add many nodes.

    for (uint64_t i = 3; i <= SDS_BPTREE_DEFAULT_CAPACITY + 2; i++) {
        // Add two to guarantee we don't conflict
        result = sds_bptree_insert(binst, (void *)&i , NULL);
        assert_int_equal(result, SDS_SUCCESS);
    }
    uint64_t key = 1;
    result = sds_bptree_insert(binst, (void *)&key , NULL);
    assert_int_equal(result, SDS_SUCCESS);
    key = 2;
    result = sds_bptree_insert(binst, (void *)&key , NULL);
    assert_int_equal(result, SDS_SUCCESS);

    for (uint64_t i = SDS_BPTREE_DEFAULT_CAPACITY; i > 2 ; i--) {
#ifdef DEBUG
        printf("Deleting %" PRIu64 "\n ", i);
#endif
        key = i + 1;
        result = sds_bptree_delete(binst, (void *)&key);
        assert_int_equal(result, SDS_KEY_PRESENT);
    }
}

static void
test_23_delete_right_merge(void **state)
{
    // Delete keys from the A, and B, node, But B second. This shuold force A- > merge
    // Because the node in Branch 4 is:
    // [ 7 | 8 | 9 | - ] -> [ 10 | 11 | 12 | - ]
    // So we need to delete One of the nodes from A To make it on B_HALF.
    // Hint: B is the right node here

    sds_bptree_instance *binst = *state;
    // First, make a tree with 3 * SDS_BPTREE_DEFAULT_CAPACITY, then delete the highest
    sds_result result = SDS_SUCCESS;
    // Add many nodes.

    for (uint64_t i = 1; i <= (SDS_BPTREE_DEFAULT_CAPACITY * 3); i++) {
        // Add two to guarantee we don't conflict
        result = sds_bptree_insert(binst, (void *)&i , NULL);
        assert_int_equal(result, SDS_SUCCESS);
    }

    // Is there a way we can make this test work with a BFACTOR of != 4?
    // Then when we delete 8 / 9 from A
    // [ 7 | 8 | - | - ] -> [ 10 | 11 | 12 | - ]
    // Now we start to remove from B.
    // [ 7 | 8 | - | - ] -> [ 10 | 11 | - | - ]
    // [ 7 | 8 | - | - ] -> [ 10 | - | - | - ]
    // At this point the merge should begin, and we get:
    // [ 7 | 8 | 10 | - ] -> [ - | - | - | - ]
    // [ 7 | 8 | 10 | - ]
    // Too easy ... ?

    for (uint64_t i = (SDS_BPTREE_DEFAULT_CAPACITY * 3); i >= SDS_BPTREE_DEFAULT_CAPACITY * 2; i--) {
        // Add two to guarantee we don't conflict
        // printf("Deleting %d\n ", i);
        result = sds_bptree_delete(binst, (void *)&i);
        assert_int_equal(result, SDS_KEY_PRESENT);
    }

    // How do we actually assert this?
    // I think we can check binst node_count.

    return;
}


static void
test_24_delete_left_merge(void **state)
{
    // Delete keys from the A and B node, but A second. This should force B -> A merge.
    // Because the node in Branch 4 is:
    // [ 7 | 8 | 9 | - ] -> [ 10 | 11 | 12 | - ]
    // So we need to delete One of the nodes from B To make it on B_HALF.
    // Hint: B is the right node here
    sds_bptree_instance *binst = *state;
    // First, make a tree with 3 * SDS_BPTREE_DEFAULT_CAPACITY, then delete the highest
    sds_result result = SDS_SUCCESS;
    uint64_t i = 2;
    // Add many nodes.

    for (i = 1; i <= (SDS_BPTREE_DEFAULT_CAPACITY * 3); i++) {
        // Add two to guarantee we don't conflict
        result = sds_bptree_insert(binst, (void *)&i , NULL);
        assert_int_equal(result, SDS_SUCCESS);
    }
    // Then when we delete 11/12 from B
    // [ 7 | 8 | 9 | - ] -> [ 10 | 11 | - | - ]
    // Now we start to remove from A.
    // [ 7 | 8 | - | - ] -> [ 10 | 11 | - | - ]
    // Now we start to remove from A.
    // [ 7 | - | - | - ] -> [ 10 | 11 | - | - ]
    // At this point the merge should begin.
    // Final result.
    // [ - | - | - | - ] -> [ 7 | 10 | 11 | - ]
    // And A should be removed too.

    for (i = SDS_BPTREE_DEFAULT_CAPACITY; i <= (SDS_BPTREE_DEFAULT_CAPACITY * 2) ; i++) {
        // Add two to guarantee we don't conflict
#ifdef DEBUG
        printf("Deleting %" PRIu64 "\n ", i);
#endif
        result = sds_bptree_delete(binst, (void *)&i);
        assert_int_equal(result, SDS_KEY_PRESENT);
    }
}

static void
test_25_delete_all_compress_root(void **state)
{
    // Add keys to grow a new root, then *delete* all the keys, we should shrink the tree.
    sds_bptree_instance *binst = *state;
    // First, make a tree with 3 * SDS_BPTREE_DEFAULT_CAPACITY, then delete the highest
    sds_result result = SDS_SUCCESS;
    uint64_t i = 0;
    // Add many nodes.

    for (i = 1; i <= (SDS_BPTREE_DEFAULT_CAPACITY * 3); i++) {
        // Add two to guarantee we don't conflict
        result = sds_bptree_insert(binst, (void *)&i , NULL);
        assert_int_equal(result, SDS_SUCCESS);
    }

    for (i = 1; i <= (SDS_BPTREE_DEFAULT_CAPACITY * 3); i++) {
        // Add two to guarantee we don't conflict
#ifdef DEBUG
        printf("Deleting %" PRIu64 "\n ", i);
#endif
        result = sds_bptree_delete(binst, (void *)&i);
        assert_int_equal(result, SDS_KEY_PRESENT);
    }

    return;
}

static void
test_26_delete_right_branch_merge(void **state)
{
    // Delete keys from the A, and B, node, But B second. This shuold force A- > merge
    // Because the node in Branch 4 is:
    // [ 7 | 8 | 9 | - ] -> [ 10 | 11 | 12 | - ]
    // So we need to delete One of the nodes from A To make it on B_HALF.
    // Hint: B is the right node here

    // Difference here, is that we want to act on the branch, not the leafs

    sds_bptree_instance *binst = *state;
    // First, make a tree with 3 * SDS_BPTREE_DEFAULT_CAPACITY, then delete the highest
    sds_result result = SDS_SUCCESS;
    uint64_t i = 2;
    // Add many nodes.

    for (i = 1; i <= (SDS_BPTREE_DEFAULT_CAPACITY * 6); i++) {
        // Add two to guarantee we don't conflict
        result = sds_bptree_insert(binst, (void *)&i , NULL);
        assert_int_equal(result, SDS_SUCCESS);
    }
    // Then when we delete 8 / 9 from A
    // [ 7 | 8 | - | - ] -> [ 10 | 11 | 12 | - ]
    // Now we start to remove from B.
    // [ 7 | 8 | - | - ] -> [ 10 | 11 | - | - ]
    // [ 7 | 8 | - | - ] -> [ 10 | - | - | - ]
    // At this point the merge should begin, and we get:
    // [ 7 | 8 | 10 | - ] -> [ - | - | - | - ]
    // [ 7 | 8 | 10 | - ]
    // Too easy ... ?

    for (i = (SDS_BPTREE_DEFAULT_CAPACITY * 6); i >= SDS_BPTREE_DEFAULT_CAPACITY * 2; i--) {
        // Add two to guarantee we don't conflict
        // printf("Deleting %d\n ", i);
        result = sds_bptree_delete(binst, (void *)&i);
        assert_int_equal(result, SDS_KEY_PRESENT);

    }

    // How do we actually assert this?
    // I think we can check binst node_count.

    return;
}


static void
test_27_delete_left_branch_merge(void **state)
{
    // Delete keys from the A and B node, but A second. This should force B -> A merge.
    // Because the node in Branch 4 is:
    // [ 7 | 8 | 9 | - ] -> [ 10 | 11 | 12 | - ]
    // So we need to delete One of the nodes from B To make it on B_HALF.
    // Hint: B is the right node here
    // Difference here is we are acting on the branches not the leaves alone now.
    sds_bptree_instance *binst = *state;
    // First, make a tree with 3 * SDS_BPTREE_DEFAULT_CAPACITY, then delete the highest
    sds_result result = SDS_SUCCESS;
    uint64_t i = 2;
    // Add many nodes.

    for (i = 1; i <= (SDS_BPTREE_DEFAULT_CAPACITY * 6); i++) {
        // Add two to guarantee we don't conflict
        result = sds_bptree_insert(binst, (void *)&i , NULL);
        assert_int_equal(result, SDS_SUCCESS);
    }

    // Then when we delete 11/12 from B
    // [ 7 | 8 | 9 | - ] -> [ 10 | 11 | - | - ]
    // Now we start to remove from A.
    // [ 7 | 8 | - | - ] -> [ 10 | 11 | - | - ]
    // Now we start to remove from A.
    // [ 7 | - | - | - ] -> [ 10 | 11 | - | - ]
    // At this point the merge should begin.
    // Final result.
    // [ - | - | - | - ] -> [ 7 | 10 | 11 | - ]
    // And A should be removed too.
    for (i = SDS_BPTREE_DEFAULT_CAPACITY * 2; i <= (SDS_BPTREE_DEFAULT_CAPACITY * 6) ; i++) {
        // Add two to guarantee we don't conflict
#ifdef DEBUG
        printf("Deleting %" PRIu64 "\n ", i);
#endif
        result = sds_bptree_delete(binst, (void *)&i);
        assert_int_equal(result, SDS_KEY_PRESENT);
        result = sds_bptree_verify(binst);
        assert_int_equal(result, SDS_SUCCESS);
    }
}

// Test for branch key borrowing. Will need to add things out of order.

// stress test and random operations. Should pass
static void
test_28_insert_and_delete_random(void **state) {
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;

    for (uint64_t i = 0; i < 200 ; i++) {
        result = sds_bptree_insert(binst, (void *)&(fill_pattern[i]), NULL);
        assert_int_equal(result, SDS_SUCCESS);

        result = sds_bptree_verify(binst);

        if (result != SDS_SUCCESS) {
            sds_log("bptree_test_teardown", "FAIL: B+Tree verification failed %d binst", result);
        }
        assert_int_equal(result, SDS_SUCCESS);

    }
    // search

    for (uint64_t i = 0; i < 200 ; i++) {
        result = sds_bptree_search(binst, (void *)&(fill_pattern[i]));
        if (result != SDS_KEY_PRESENT) {
            sds_log("bptree_test_teardown", "FAIL: Can not find %d", fill_pattern[i]);
        }
        assert_int_equal(result, SDS_KEY_PRESENT);

        result = sds_bptree_delete(binst, (void *)&(fill_pattern[i]));
        if (result != SDS_KEY_PRESENT) {
            sds_log("bptree_test_teardown", "FAIL: Can not delete %d", fill_pattern[i]);
        }
        assert_int_equal(result, SDS_KEY_PRESENT);

        result = sds_bptree_search(binst, (void *)&(fill_pattern[i]));
        if (result != SDS_KEY_NOT_PRESENT) {
            sds_log("bptree_test_teardown", "FAIL: Can find %d", fill_pattern[i]);
        }
        assert_int_equal(result, SDS_KEY_NOT_PRESENT);

        result = sds_bptree_verify(binst);

        if (result != SDS_SUCCESS) {
            sds_log("bptree_test_teardown", "FAIL: B+Tree verification failed %d binst", result);
        }
        assert_int_equal(result, SDS_SUCCESS);

    }
}

// stress test and random operations. Should pass
static void
test_29_insert_and_delete_random_large(void **state) {
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;

    for (uint64_t i = 0; i < 2048 ; i++) {
        result = sds_bptree_insert(binst, (void *)&(fill_pattern[i]), NULL);
        assert_int_equal(result, SDS_SUCCESS);

        result = sds_bptree_verify(binst);

        if (result != SDS_SUCCESS) {
            sds_log("bptree_test_teardown", "FAIL: B+Tree verification failed %d binst", result);
        }
        assert_int_equal(result, SDS_SUCCESS);

    }
    for (uint64_t i = 0; i < 2048 ; i++) {
        result = sds_bptree_search(binst, (void *)&(fill_pattern[i]));
        if (result != SDS_KEY_PRESENT) {
            sds_log("bptree_test_teardown", "FAIL: Can not find %d", fill_pattern[i]);
        }
        assert_int_equal(result, SDS_KEY_PRESENT);

        result = sds_bptree_delete(binst, (void *)&(fill_pattern[i]));
        if (result != SDS_KEY_PRESENT) {
            sds_log("bptree_test_teardown", "FAIL: Can not delete %d", fill_pattern[i]);
        }
        assert_int_equal(result, SDS_KEY_PRESENT);

        result = sds_bptree_search(binst, (void *)&(fill_pattern[i]));
        if (result != SDS_KEY_NOT_PRESENT) {
            sds_log("bptree_test_teardown", "FAIL: Can find %d", fill_pattern[i]);
        }
        assert_int_equal(result, SDS_KEY_NOT_PRESENT);

        result = sds_bptree_verify(binst);

        if (result != SDS_SUCCESS) {
            sds_log("bptree_test_teardown", "FAIL: B+Tree verification failed %d binst", result);
        }
        assert_int_equal(result, SDS_SUCCESS);
    }
}

static void
test_30_insert_and_delete_strings(void **state  __attribute__((unused))) {
    // Create a new tree, and then destroy it.
    sds_bptree_instance *binst = NULL;
    sds_result result = SDS_SUCCESS;

    result = sds_bptree_init(&binst, 1, sds_strcmp, sds_free, sds_free, sds_strdup);
    assert_int_equal(result, SDS_SUCCESS);

    for (uint64_t i = 0; i < 200 ; i++) {
        /* Make a new string */
        char *ptr = sds_malloc(sizeof(char) * 4);
        /*  */
        sprintf(ptr, "%03"PRIu64, i);
        /* Insert CLONES this .... */
        result = sds_bptree_insert(binst, ptr, NULL);
        /* Free the string now, the tree cloned it! */
        sds_free(ptr);
        assert_int_equal(result, SDS_SUCCESS);
        result = sds_bptree_verify(binst);
        assert_int_equal(result, SDS_SUCCESS);
    }

    for (uint64_t i = 20; i < 150; i+=3) {
        char *ptr = sds_malloc(sizeof(char) * 4);
        sprintf(ptr, "%03"PRIu64, i);
        result = sds_bptree_delete(binst, ptr);
        assert_int_equal(result, SDS_KEY_PRESENT);
        sds_free(ptr);
        result = sds_bptree_verify(binst);
        assert_int_equal(result, SDS_SUCCESS);
    }

    result = sds_bptree_destroy(binst);
    assert_int_equal(result, SDS_SUCCESS);

}

int
run_bpt_tests (void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_1_invalid_binst_ptr),
#ifdef DEBUG
        cmocka_unit_test(test_10_tamper_with_inst),
        cmocka_unit_test(test_11_tamper_with_node),
        /*
        cmocka_unit_test_setup_teardown(test_17_insert_and_tamper,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_5_single_null_mismatch_size_insert_fn,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        */
#endif
        cmocka_unit_test_setup_teardown(test_3_single_insert,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_4_single_null_insert_fn,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_6_insert_less_than_no_split,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_7_insert_greater_than_no_split,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_8_insert_duplicate,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_9_insert_fill_and_split,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_12_insert_fill_split_and_grow,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_13_insert_fill_split_and_grow_inverse,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_14_insert_random,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_15_search_none,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_16_insert_and_retrieve,
                                        /* Setup a string capable tree instead */
                                        bptree_str_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_18_delete_single_value,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_19_delete_non_existant,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_20_delete_non_branch_key,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_21_delete_redist_left_leaf,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_22_delete_redist_right_leaf,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_22_5_redist_left_borrow,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_23_delete_right_merge,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_24_delete_left_merge,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_25_delete_all_compress_root,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_26_delete_right_branch_merge,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_27_delete_left_branch_merge,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_28_insert_and_delete_random,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_29_insert_and_delete_random_large,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test(test_30_insert_and_delete_strings),
    };

    return cmocka_run_group_tests_name("bpt", tests, NULL, NULL);

}

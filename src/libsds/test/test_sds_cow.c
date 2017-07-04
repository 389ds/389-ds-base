/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/


/* test_bptree is the test driver for the B+ Tree implemented by bptree/bptree.c */

/* Contains test data. */
#include "test_sds.h"

// Predefine for now ...
#ifdef SDS_DEBUG
sds_result sds_bptree_cow_display(sds_bptree_transaction *btxn);
#endif

static void
test_1_cow_init(void **state __attribute__((unused))) {
    sds_bptree_cow_instance *binst;
    sds_result result = SDS_SUCCESS;
    // Check a null init
    result = sds_bptree_cow_init(NULL, 1, sds_uint64_t_compare, sds_uint64_t_free, sds_uint64_t_dup, sds_uint64_t_free, sds_uint64_t_dup);
    assert_int_equal(result, SDS_NULL_POINTER);
    // Check a real init
    result = sds_bptree_cow_init(&binst, 1, sds_uint64_t_compare, sds_uint64_t_free, sds_uint64_t_dup, sds_uint64_t_free, sds_uint64_t_dup);
    assert_int_equal(result, SDS_SUCCESS);
    // Destroy.
    result = sds_bptree_cow_destroy(binst);
    assert_int_equal(result, SDS_SUCCESS);
}

// Test transactions.

static void
test_2_begin_rotxn(void **state) {
    /* Take a read only txn */
    sds_bptree_cow_instance *binst = *state;
    sds_result result = SDS_SUCCESS;
    sds_bptree_transaction *btxn = NULL;

    result = sds_bptree_cow_rotxn_begin(binst, &btxn);
    assert_int_equal(result, SDS_SUCCESS);
    /* Check the refcount is 2 */
    assert_int_equal(binst->txn->reference_count, 2);
    /* Close it */
    result = sds_bptree_cow_rotxn_close(&btxn);
    assert_int_equal(result, SDS_SUCCESS);
    /* Check the refcount is 1 */
    assert_int_equal(binst->txn->reference_count, 1);
}

static void
test_3_begin_wrtxn_no_read(void **state) {
    sds_bptree_cow_instance *binst = *state;
    sds_result result = SDS_SUCCESS;
    sds_bptree_transaction *btxn = NULL;
    sds_bptree_transaction *btxn_ref = NULL;

    /* Make a txn, and then abort. Make sure it doesn't update. */
    result = sds_bptree_cow_wrtxn_begin(binst, &btxn);
    assert_int_equal(result, SDS_SUCCESS);
    /* Make sure the txn id's differ */
    assert_int_not_equal(btxn->txn_id, binst->txn->txn_id);

    result = sds_bptree_cow_wrtxn_abort(&btxn);
    assert_int_equal(result, SDS_SUCCESS);
    assert_ptr_not_equal(btxn, binst->txn);

    /* Now make a txn and commit, assert it's added. */
    result = sds_bptree_cow_wrtxn_begin(binst, &btxn);
    assert_int_equal(result, SDS_SUCCESS);

    /* Commit will null the txn pointer, so we take a ref. */
    btxn_ref = btxn;
    result = sds_bptree_cow_wrtxn_commit(&btxn);
    assert_int_equal(result, SDS_SUCCESS);
    assert_ptr_equal(btxn_ref, binst->txn);
}

static void
test_4_begin_wrtxn_w_read(void **state) {
    sds_bptree_cow_instance *binst = *state;
    sds_result result = SDS_SUCCESS;
    sds_bptree_transaction *ro1_btxn = NULL;
    sds_bptree_transaction *ro2_btxn = NULL;
    sds_bptree_transaction *ro3_btxn = NULL;
    sds_bptree_transaction *wr1_btxn = NULL;

    /* Take a read txn */
    result = sds_bptree_cow_rotxn_begin(binst, &ro1_btxn);
    assert_int_equal(result, SDS_SUCCESS);
    /* Take and commit a write txn */
    result = sds_bptree_cow_wrtxn_begin(binst, &wr1_btxn);
    assert_int_equal(result, SDS_SUCCESS);
    result = sds_bptree_cow_wrtxn_commit(&wr1_btxn);
    assert_int_equal(result, SDS_SUCCESS);
    /* Take a new read txn. */
    result = sds_bptree_cow_rotxn_begin(binst, &ro2_btxn);
    assert_int_equal(result, SDS_SUCCESS);
    /* Assert they are different. */
    assert_ptr_not_equal(ro1_btxn, ro2_btxn);
    /* Take and commit a write txn */
    result = sds_bptree_cow_wrtxn_begin(binst, &wr1_btxn);
    assert_int_equal(result, SDS_SUCCESS);
    result = sds_bptree_cow_wrtxn_commit(&wr1_btxn);
    assert_int_equal(result, SDS_SUCCESS);
    /* Take a read txn */
    result = sds_bptree_cow_rotxn_begin(binst, &ro3_btxn);
    assert_int_equal(result, SDS_SUCCESS);
    /* Assert they are different. */
    assert_ptr_not_equal(ro3_btxn, ro2_btxn);
    assert_ptr_not_equal(ro3_btxn, ro1_btxn);
    /* Close them all, order doesn't matter */
    result = sds_bptree_cow_rotxn_close(&ro1_btxn);
    assert_int_equal(result, SDS_SUCCESS);
    result = sds_bptree_cow_rotxn_close(&ro3_btxn);
    assert_int_equal(result, SDS_SUCCESS);
    result = sds_bptree_cow_rotxn_close(&ro2_btxn);
    assert_int_equal(result, SDS_SUCCESS);
}

// Test destroy with an open write txn.
// Test destroy with an open read txn.

// Test basic insert and delete

static void
test_misuse_rotxn(void **state) {
    sds_bptree_cow_instance *binst = *state;
    sds_result result = SDS_SUCCESS;
    sds_bptree_transaction *ro_btxn = NULL;

    result = sds_bptree_cow_rotxn_begin(binst, &ro_btxn);
    assert_int_equal(result, SDS_SUCCESS);
    result = sds_bptree_cow_insert(ro_btxn, NULL, NULL);
    assert_int_equal(result, SDS_INVALID_TXN);
    result = sds_bptree_cow_delete(ro_btxn, NULL);
    assert_int_equal(result, SDS_INVALID_TXN);
    result = sds_bptree_cow_rotxn_close(&ro_btxn);
    assert_int_equal(result, SDS_SUCCESS);
}

// Similar to the basic tests of bptree, we need to quickly assert that at least
// within a write transaction, our tree operations *are* sane.
static void
test_basic_insert(void **state) {
    sds_bptree_cow_instance *binst = *state;
    sds_result result = SDS_SUCCESS;
    sds_bptree_transaction *wr_btxn = NULL;

    uint64_t key1 = 1;
    uint64_t key2 = 2;

    result = sds_bptree_cow_wrtxn_begin(binst, &wr_btxn);
    assert_int_equal(result, SDS_SUCCESS);

    // Now do an insert, and search.
    result = sds_bptree_cow_insert(wr_btxn, (void *)&key1, NULL);
    assert_int_equal(result, SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_search(wr_btxn, (void *)&key1), SDS_KEY_PRESENT);
    assert_int_equal(sds_bptree_cow_search(wr_btxn, (void *)&key2), SDS_KEY_NOT_PRESENT);

    // assert_int_equal(sds_bptree_cow_display(wr_btxn), SDS_SUCCESS);
    // Dump the current ro txn too.
    // THIS IS A HACK, YOU SHOULD BE TAKING AN RO TXN YOURSELF!
    // now insert enough to cause some splits and search.
    for (uint64_t i = 10; i < (10 + SDS_BPTREE_DEFAULT_CAPACITY); i ++) {
        result = sds_bptree_cow_insert(wr_btxn, (void *)&i, NULL);
        assert_int_equal(result, SDS_SUCCESS);
        assert_int_equal(sds_bptree_cow_search(wr_btxn, (void *)&i), SDS_KEY_PRESENT);
        // assert_int_equal(sds_bptree_cow_display(wr_btxn), SDS_SUCCESS);
    }

    // assert_int_equal(sds_bptree_cow_display(binst->txn), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_verify(binst), SDS_SUCCESS);
    // Check that an ro_txn still has nothing
    // commit
    assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);
    // now check it's all there.
    assert_int_equal(sds_bptree_cow_verify(binst), SDS_SUCCESS);

    // destroy ro txns
}

// These tests are based on the bpt tests, but basically just do hard tests
// early on.

static void
test_large_insert(void **state) {
    sds_bptree_cow_instance *binst = *state;
    sds_result result = SDS_SUCCESS;
    sds_bptree_transaction *wr_btxn = NULL;
    sds_bptree_transaction *ro_btxn = NULL;

    for (uint64_t i = 1; i <= ((SDS_BPTREE_DEFAULT_CAPACITY + 1) << 4) ; i++) {
        result = sds_bptree_cow_wrtxn_begin(binst, &wr_btxn);
        assert_int_equal(result, SDS_SUCCESS);

        result = sds_bptree_cow_insert(wr_btxn, (void *)&i, NULL);
        assert_int_equal(result, SDS_SUCCESS);
        assert_int_equal(sds_bptree_cow_search(wr_btxn, (void *)&i), SDS_KEY_PRESENT);
        if (i == 40) {
            result = sds_bptree_cow_rotxn_begin(binst, &ro_btxn);
            assert_int_equal(result, SDS_SUCCESS);
        }
        // assert_int_equal(sds_bptree_cow_display(wr_btxn), SDS_SUCCESS);

        assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);
        // now check it's all there.
        assert_int_equal(sds_bptree_cow_verify(binst), SDS_SUCCESS);
    }

    // assert_int_equal(sds_bptree_cow_display(ro_btxn), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_rotxn_begin(binst, &wr_btxn), SDS_SUCCESS);
    // assert_int_equal(sds_bptree_cow_display(wr_btxn), SDS_SUCCESS);

    result = sds_bptree_cow_rotxn_close(&ro_btxn);
    result = sds_bptree_cow_rotxn_close(&wr_btxn);
    assert_int_equal(result, SDS_SUCCESS);
}

// Test random with commits between each insert.

static void
test_random_insert(void **state) {
    sds_bptree_cow_instance *binst = *state;
    sds_result result = SDS_SUCCESS;
    sds_bptree_transaction *wr_btxn = NULL;

    for (uint64_t i = 1; i <= 200 ; i++) {
        result = sds_bptree_cow_wrtxn_begin(binst, &wr_btxn);
        assert_int_equal(result, SDS_SUCCESS);

        result = sds_bptree_cow_insert(wr_btxn, (void *)&(fill_pattern[i]), NULL);
        assert_int_equal(result, SDS_SUCCESS);
        assert_int_equal(sds_bptree_cow_search(wr_btxn, (void *)&(fill_pattern[i])), SDS_KEY_PRESENT);
        // assert_int_equal(sds_bptree_cow_display(wr_btxn), SDS_SUCCESS);

        assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);
        // now check it's all there.
        assert_int_equal(sds_bptree_cow_verify(binst), SDS_SUCCESS);
    }
}

// Test rotxn, wrtxn + insert, rotxn then search on both and assert the difference.

static void
test_out_of_order_txn_close(void **state) {
    sds_bptree_cow_instance *binst = *state;

    sds_bptree_transaction *wr_btxn = NULL;
    sds_bptree_transaction *ro_btxn_a = NULL;
    sds_bptree_transaction *ro_btxn_b = NULL;
    sds_bptree_transaction *ro_btxn_c = NULL;
    uint64_t i = 2;
    uint64_t key1 = 1;
    uint64_t key3 = 3;

    // Prepare the tree.
    assert_int_equal(sds_bptree_cow_wrtxn_begin(binst, &wr_btxn), SDS_SUCCESS);
    for (; i <= ((SDS_BPTREE_DEFAULT_CAPACITY + 1) * 2); i+=2) {
        // This needs to add enough nodes to trigger a split
        assert_int_equal(sds_bptree_cow_insert(wr_btxn, (void *)&i, NULL), SDS_SUCCESS);
    }
    assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);

    // First, open a write commit, and add an *odd* value.
    assert_int_equal(sds_bptree_cow_wrtxn_begin(binst, &wr_btxn), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_insert(wr_btxn, (void *)&key1, NULL), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);

    // Now take an RO txn to it.
    assert_int_equal(sds_bptree_cow_rotxn_begin(binst, &ro_btxn_a), SDS_SUCCESS);

    // Second, open another write commit, add some more.
    assert_int_equal(sds_bptree_cow_wrtxn_begin(binst, &wr_btxn), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_insert(wr_btxn, (void *)&i, NULL), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);

    // Now take an RO txn to it.
    assert_int_equal(sds_bptree_cow_rotxn_begin(binst, &ro_btxn_b), SDS_SUCCESS);

    // Now take the third write txn, add some value.
    assert_int_equal(sds_bptree_cow_wrtxn_begin(binst, &wr_btxn), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_insert(wr_btxn, (void *)&key3, NULL), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);

    assert_int_equal(sds_bptree_cow_rotxn_begin(binst, &ro_btxn_c), SDS_SUCCESS);

    // Now free the second txn.
    /* At this point, ro_btxn_b should "own" the nodes in btxn_a, because of COW */
    assert_int_equal(sds_bptree_cow_rotxn_close(&ro_btxn_b), SDS_SUCCESS);

    // Now seacch for the taken value in the FIRST txn. Node may be freed.
    assert_int_equal(sds_bptree_cow_search(ro_btxn_a, (void *)&key1), SDS_KEY_PRESENT);
    assert_int_equal(sds_bptree_cow_search(ro_btxn_a, (void *)&key3), SDS_KEY_NOT_PRESENT);
    assert_int_equal(sds_bptree_cow_search(ro_btxn_a, (void *)&i), SDS_KEY_NOT_PRESENT);

    // Now close the first txn.
    assert_int_equal(sds_bptree_cow_rotxn_close(&ro_btxn_a), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_rotxn_close(&ro_btxn_c), SDS_SUCCESS);

}

// Test that leaving an RO txn alive doesn't break shutdown.
static void
test_dangling_txn_close(void **state) {
    sds_bptree_cow_instance *binst = *state;

    sds_bptree_transaction *ro_btxn_a = NULL;
    uint64_t key1 = 1;
    uint64_t key2 = 2;

    // Add a value
    assert_int_equal(sds_bptree_cow_insert_atomic(binst, (void *)&key1, NULL), SDS_SUCCESS);
    // Open a txn and leave it dangling.
    assert_int_equal(sds_bptree_cow_rotxn_begin(binst, &ro_btxn_a), SDS_SUCCESS);
    // Now complete another write to be sure it's there in the tail.
    assert_int_equal(sds_bptree_cow_insert_atomic(binst, (void *)&key2, NULL), SDS_SUCCESS);

}

// Test aborting a txn doesn't cause leaks.

static void
test_txn_abort(void **state) {
    sds_bptree_cow_instance *binst = *state;

    sds_bptree_transaction *wr_btxn = NULL;
    uint64_t key1 = 1;
    uint64_t key2 = 2;

    // Add a value
    assert_int_equal(sds_bptree_cow_insert_atomic(binst, (void *)&key1, NULL), SDS_SUCCESS);

    // Create a new txn.
    assert_int_equal(sds_bptree_cow_wrtxn_begin(binst, &wr_btxn), SDS_SUCCESS);
    // Do an insert to trigger the COW
    assert_int_equal(sds_bptree_cow_insert(wr_btxn, (void *)&key2, NULL), SDS_SUCCESS);
    // And abort it!
    assert_int_equal(sds_bptree_cow_wrtxn_abort(&wr_btxn), SDS_SUCCESS);

}

// Should set and retrieve a value from the tree atomically.
// Perhaps a string type?
static void
test_txn_atomic_retrieve(void **state) {
    sds_bptree_cow_instance *binst = *state;
    uint64_t key5 = 5;
    assert_int_equal(sds_bptree_cow_insert_atomic(binst, (void *)&key5, sds_uint64_t_dup((void *)&key5)), SDS_SUCCESS);
    uint64_t *x = 0;
    assert_int_equal(sds_bptree_cow_retrieve_atomic(binst, (void *)&key5, (void **)&x), SDS_KEY_PRESENT);
    assert_int_equal(*x, 5);
    /* With the atomics WE OWN the memory as it was dupped */
    /* else it's not very atomic!!! */
    sds_uint64_t_free(x);
}

/* Test double free of a txn */
static void
test_txn_double_close(void **state) {
    sds_bptree_cow_instance *binst = *state;

    sds_bptree_transaction *ro_btxn_a = NULL;
    assert_int_equal(sds_bptree_cow_rotxn_begin(binst, &ro_btxn_a), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_rotxn_close(&ro_btxn_a), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_rotxn_close(&ro_btxn_a), SDS_INVALID_TXN);
}
/* Test misuse of write txn */
static void
test_txn_post_commit_use(void **state) {
    sds_bptree_cow_instance *binst = *state;
    sds_bptree_transaction *wr_btxn = NULL;
    uint64_t key1 = 1;

    assert_int_equal(sds_bptree_cow_wrtxn_begin(binst, &wr_btxn), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_insert(wr_btxn, (void *)&key1, NULL), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);

    assert_int_equal(sds_bptree_cow_rotxn_close(&wr_btxn), SDS_INVALID_TXN);
    assert_int_equal(sds_bptree_cow_wrtxn_abort(&wr_btxn), SDS_INVALID_TXN);
    assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_INVALID_TXN);
}

/* Test that all txn search fns handle null */
static void
test_null_txn(void **state __attribute__((unused))) {
    // sds_bptree_cow_instance *binst = *state;
    uint64_t key1 = 1;
    void *x = NULL;

    assert_int_equal(sds_bptree_cow_insert(NULL, (void *)&key1, NULL), SDS_INVALID_TXN);
    assert_int_equal(sds_bptree_cow_delete(NULL, (void *)&key1), SDS_INVALID_TXN);
    assert_int_equal(sds_bptree_cow_search(NULL, (void *)&key1), SDS_INVALID_TXN);
    assert_int_equal(sds_bptree_cow_retrieve(NULL, (void *)&key1, &x), SDS_INVALID_TXN);
}

static void
test_txn_delete_simple(void **state) {
    sds_bptree_cow_instance *binst = *state;
    sds_bptree_transaction *wr_btxn = NULL;
    uint64_t key1 = 1;

    assert_int_equal(sds_bptree_cow_wrtxn_begin(binst, &wr_btxn), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_insert(wr_btxn, (void *)&key1, NULL), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);

    assert_int_equal(sds_bptree_cow_wrtxn_begin(binst, &wr_btxn), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_delete(wr_btxn, (void *)&key1), SDS_KEY_PRESENT);
    assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);
}

static void
test_txn_delete_leaf_left(void **state) {
    /* Add enough to create some extra nodes */
    sds_bptree_cow_instance *binst = *state;
    sds_bptree_transaction *wr_btxn = NULL;

    assert_int_equal(sds_bptree_cow_wrtxn_begin(binst, &wr_btxn), SDS_SUCCESS);
    for (uint64_t i = 1; i <= SDS_BPTREE_DEFAULT_CAPACITY * 4; i++) {
        assert_int_equal(sds_bptree_cow_insert(wr_btxn, (void *)&i, NULL), SDS_SUCCESS);
        // assert_int_equal(sds_bptree_cow_display(wr_btxn), SDS_SUCCESS);
    }
    assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);
    /* Now delete down to create borrow and merge left */

    for (uint64_t i = SDS_BPTREE_DEFAULT_CAPACITY * 4; i > SDS_BPTREE_DEFAULT_CAPACITY * 2; i--) {
        assert_int_equal(sds_bptree_cow_wrtxn_begin(binst, &wr_btxn), SDS_SUCCESS);
        assert_int_equal(sds_bptree_cow_delete(wr_btxn, (void *)&i), SDS_KEY_PRESENT);
        // assert_int_equal(sds_bptree_cow_display(wr_btxn), SDS_SUCCESS);
        assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);
        assert_int_equal(sds_bptree_cow_verify(binst), SDS_SUCCESS);
    }
}

static void
test_txn_delete_leaf_right(void **state) {
    /* Add enough to create some extra nodes */
    sds_bptree_cow_instance *binst = *state;
    sds_bptree_transaction *wr_btxn = NULL;

    assert_int_equal(sds_bptree_cow_wrtxn_begin(binst, &wr_btxn), SDS_SUCCESS);
    for (uint64_t i = 1; i <= SDS_BPTREE_DEFAULT_CAPACITY * 4; i++) {
        assert_int_equal(sds_bptree_cow_insert(wr_btxn, (void *)&i, NULL), SDS_SUCCESS);
        // assert_int_equal(sds_bptree_cow_display(wr_btxn), SDS_SUCCESS);
    }
    assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);
    /* Now delete up to create borrow and merge right */

    for (uint64_t i = 1; i < SDS_BPTREE_DEFAULT_CAPACITY * 2; i++) {
        assert_int_equal(sds_bptree_cow_wrtxn_begin(binst, &wr_btxn), SDS_SUCCESS);
        assert_int_equal(sds_bptree_cow_delete(wr_btxn, (void *)&i), SDS_KEY_PRESENT);
        // assert_int_equal(sds_bptree_cow_display(wr_btxn), SDS_SUCCESS);
        assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);
        assert_int_equal(sds_bptree_cow_verify(binst), SDS_SUCCESS);
    }
}

static void
test_txn_delete_branch_left(void **state) {
    /* Add enough to create some extra nodes */
    sds_bptree_cow_instance *binst = *state;
    sds_bptree_transaction *wr_btxn = NULL;

    assert_int_equal(sds_bptree_cow_wrtxn_begin(binst, &wr_btxn), SDS_SUCCESS);
    for (uint64_t i = 1; i <= (SDS_BPTREE_DEFAULT_CAPACITY << 3); i++) {
        assert_int_equal(sds_bptree_cow_insert(wr_btxn, (void *)&i, NULL), SDS_SUCCESS);
        // assert_int_equal(sds_bptree_cow_display(wr_btxn), SDS_SUCCESS);
    }
    assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);
    /* Now delete down to create borrow and merge left in branches */
    for (uint64_t i = (SDS_BPTREE_DEFAULT_CAPACITY << 3); i > 0; i--) {
        assert_int_equal(sds_bptree_cow_wrtxn_begin(binst, &wr_btxn), SDS_SUCCESS);
        assert_int_equal(sds_bptree_cow_delete(wr_btxn, (void *)&i), SDS_KEY_PRESENT);
        // assert_int_equal(sds_bptree_cow_display(wr_btxn), SDS_SUCCESS);
        assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);
        assert_int_equal(sds_bptree_cow_verify(binst), SDS_SUCCESS);
    }
}

static void
test_txn_delete_branch_right(void **state) {
    /* Add enough to create some extra nodes */
    sds_bptree_cow_instance *binst = *state;
    sds_bptree_transaction *wr_btxn = NULL;

    assert_int_equal(sds_bptree_cow_wrtxn_begin(binst, &wr_btxn), SDS_SUCCESS);
    for (uint64_t i = 1; i <= (SDS_BPTREE_DEFAULT_CAPACITY << 3); i++) {
        assert_int_equal(sds_bptree_cow_insert(wr_btxn, (void *)&i, NULL), SDS_SUCCESS);
        // assert_int_equal(sds_bptree_cow_display(wr_btxn), SDS_SUCCESS);
    }
    assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);
    /* Now delete down to create borrow and merge left in branches */
    for (uint64_t i = 1; i <= (SDS_BPTREE_DEFAULT_CAPACITY << 3); i++) {
        assert_int_equal(sds_bptree_cow_wrtxn_begin(binst, &wr_btxn), SDS_SUCCESS);
        assert_int_equal(sds_bptree_cow_delete(wr_btxn, (void *)&i), SDS_KEY_PRESENT);
        // assert_int_equal(sds_bptree_cow_display(wr_btxn), SDS_SUCCESS);
        assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);
        assert_int_equal(sds_bptree_cow_verify(binst), SDS_SUCCESS);
    }
}

static void
test_cow_update(void **state) {
    sds_bptree_cow_instance *binst = *state;
    sds_bptree_transaction *wr_btxn = NULL;
    sds_bptree_transaction *ro_btxn_a = NULL;
    sds_bptree_transaction *ro_btxn_b = NULL;

    uint64_t key1 = 1;
    uint64_t key2 = 2;
    uint64_t key3 = 3;
    uint64_t *result = NULL;

    assert_int_equal(sds_bptree_cow_wrtxn_begin(binst, &wr_btxn), SDS_SUCCESS);
    // We have to dup the keys on insert as values, the value life time is now bound to the tree
    assert_int_equal(sds_bptree_cow_insert(wr_btxn, (void *)&key1, sds_uint64_t_dup((void *)&key1)), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);

    assert_int_equal(sds_bptree_cow_rotxn_begin(binst, &ro_btxn_a), SDS_SUCCESS);
    /* Don't close yet! */

    assert_int_equal(sds_bptree_cow_wrtxn_begin(binst, &wr_btxn), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_update(wr_btxn, (void *)&key1, sds_uint64_t_dup((void *)&key2)), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_update(wr_btxn, (void *)&key2, sds_uint64_t_dup((void *)&key3)), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_wrtxn_commit(&wr_btxn), SDS_SUCCESS);

    assert_int_equal(sds_bptree_cow_rotxn_begin(binst, &ro_btxn_b), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_retrieve(ro_btxn_b, (void *)&key1, (void *)&result), SDS_KEY_PRESENT);
    assert_int_equal(*result, 2);
    assert_int_equal(sds_bptree_cow_retrieve(ro_btxn_b, (void *)&key2, (void *)&result), SDS_KEY_PRESENT);
    assert_int_equal(*result, 3);
    /* Make sure we didn't affect the previous transaction. */
    assert_int_equal(sds_bptree_cow_retrieve(ro_btxn_a, (void *)&key1, (void *)&result), SDS_KEY_PRESENT);
    assert_int_equal(*result, 1);

    /* Close now */
    assert_int_equal(sds_bptree_cow_rotxn_close(&ro_btxn_a), SDS_SUCCESS);
    assert_int_equal(sds_bptree_cow_rotxn_close(&ro_btxn_b), SDS_SUCCESS);
}

int
run_cow_tests (void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_1_cow_init),
        cmocka_unit_test_setup_teardown(test_2_begin_rotxn,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_3_begin_wrtxn_no_read,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_4_begin_wrtxn_w_read,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_misuse_rotxn,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_basic_insert,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_large_insert,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_random_insert,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_out_of_order_txn_close,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_dangling_txn_close,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_txn_abort,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_txn_atomic_retrieve,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_txn_double_close,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_txn_post_commit_use,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_null_txn,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_txn_delete_simple,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_txn_delete_leaf_left,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_txn_delete_leaf_right,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_txn_delete_branch_left,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_txn_delete_branch_right,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
        cmocka_unit_test_setup_teardown(test_cow_update,
                                        bptree_test_cow_setup,
                                        bptree_test_cow_teardown),
    };
    return cmocka_run_group_tests_name("bpt_cow", tests, NULL, NULL);
}


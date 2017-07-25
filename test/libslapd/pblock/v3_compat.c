/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2017 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "../../test_slapd.h"
#include <string.h>

/* We need this for ndn init */
#include <slap.h>

/*
 * Assert that the compatability requirements of the plugin V3 pblock API
 * are upheld.
 *
 * This will be critical in the migration to V4 so that refactors can guarantee
 * we are not altering code behaviours.
 */


void
test_libslapd_pblock_v3c_target_dn(void **state __attribute__((unused)))
{
    ndn_cache_init();
    /* Create a pblock */
    Slapi_PBlock *pb = slapi_pblock_new();
    Slapi_Operation *op = slapi_operation_new(SLAPI_OP_FLAG_INTERNAL);
    slapi_pblock_init(pb);

    char *dn = NULL;
    char *test_dn = "cn=Directory Manager";
    Slapi_DN *sdn = NULL;

    /* SLAPI_TARGET_DN */
    /* Check that with no operation we get -1 */
    assert_int_equal(slapi_pblock_get(pb, SLAPI_TARGET_DN, &dn), -1);
    assert_int_equal(slapi_pblock_set(pb, SLAPI_TARGET_DN, &dn), -1);

    /* Add the operation */
    assert_int_equal(slapi_pblock_set(pb, SLAPI_OPERATION, op), 0);

    /* Check that with a null target_address we get NULL */
    assert_int_equal(slapi_pblock_get(pb, SLAPI_TARGET_DN, &dn), 0);
    assert_null(dn);

    /* Set a DN */
    assert_int_equal(slapi_pblock_set(pb, SLAPI_TARGET_DN, test_dn), 0);
    /* Check it was set */
    assert_int_equal(slapi_pblock_get(pb, SLAPI_TARGET_DN, &dn), 0);
    assert_int_equal(strcmp(dn, test_dn), 0);
    /* Check that TARGET_SDN is not null now  */
    assert_int_equal(slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn), 0);
    assert_non_null(sdn);

    /* Assert we did not influence ORIGINAL_TARGET_DN or UNIQUEID */
    assert_int_equal(slapi_pblock_get(pb, SLAPI_ORIGINAL_TARGET_DN, &dn), 0);
    assert_null(dn);
    assert_int_equal(slapi_pblock_get(pb, SLAPI_TARGET_UNIQUEID, &dn), 0);
    assert_null(dn);

    /* pblock currently DOES NOT free the target_dn, so we must do this */
    slapi_sdn_free(&sdn);

    /* A property we cannot easily test is that setting a new DN frees the
     * OLD sdn. But, we can test in SDN that setting via SDN does NOT free.
     *
     * The only effective way to test this would be to crash sadly.
     */

    /* It works! */
    slapi_pblock_destroy(pb);
    ndn_cache_destroy();
}


void
test_libslapd_pblock_v3c_target_sdn(void **state __attribute__((unused)))
{
    ndn_cache_init();
    /* SLAPI_TARGET_SDN */
    Slapi_PBlock *pb = slapi_pblock_new();
    Slapi_Operation *op = slapi_operation_new(SLAPI_OP_FLAG_INTERNAL);
    slapi_pblock_init(pb);

    char *dn = NULL;
    Slapi_DN *sdn = NULL;
    Slapi_DN *test_a_sdn = NULL;
    Slapi_DN *test_b_sdn = NULL;

    /* Check our aliases - once we assert these are the same
     * we can then extend to say that these behaviours hold for all these
     * pblock aliases.
     */
    assert_int_equal(SLAPI_TARGET_SDN, SLAPI_ADD_TARGET_SDN);
    assert_int_equal(SLAPI_TARGET_SDN, SLAPI_BIND_TARGET_SDN);
    assert_int_equal(SLAPI_TARGET_SDN, SLAPI_COMPARE_TARGET_SDN);
    assert_int_equal(SLAPI_TARGET_SDN, SLAPI_DELETE_TARGET_SDN);
    assert_int_equal(SLAPI_TARGET_SDN, SLAPI_MODIFY_TARGET_SDN);
    assert_int_equal(SLAPI_TARGET_SDN, SLAPI_MODRDN_TARGET_SDN);
    assert_int_equal(SLAPI_TARGET_SDN, SLAPI_SEARCH_TARGET_SDN);

    /* Check that with no operation we get -1 */
    assert_int_equal(slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn), -1);
    assert_int_equal(slapi_pblock_set(pb, SLAPI_TARGET_SDN, sdn), -1);
    /* Add the operation */
    assert_int_equal(slapi_pblock_set(pb, SLAPI_OPERATION, op), 0);

    /* Check that with a null target_address we get NULL */
    assert_int_equal(slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn), 0);
    assert_null(sdn);

    /* Create and SDN and set it. */
    test_a_sdn = slapi_sdn_new_dn_byval("cn=a,cn=test");
    test_b_sdn = slapi_sdn_new_dn_byval("cn=b,cn=test");

    assert_int_equal(slapi_pblock_set(pb, SLAPI_TARGET_SDN, test_a_sdn), 0);
    assert_int_equal(slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn), 0);
    /* Assert we get it back, not a dup, the real pointer */
    assert_ptr_equal(test_a_sdn, sdn);
    assert_int_equal(slapi_sdn_compare(sdn, test_a_sdn), 0);

    /* Make a new one, and assert we haven't freed the previous. */
    assert_int_equal(slapi_pblock_set(pb, SLAPI_TARGET_SDN, test_b_sdn), 0);
    assert_int_equal(slapi_sdn_compare(sdn, test_a_sdn), 0);
    assert_int_equal(slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn), 0);
    /* Assert we get it back, not a dup, the real pointer */
    assert_ptr_equal(test_b_sdn, sdn);
    assert_int_equal(slapi_sdn_compare(sdn, test_b_sdn), 0);
    assert_int_not_equal(slapi_sdn_compare(sdn, test_a_sdn), 0);

    /* Assert we did not influence ORIGINAL_TARGET_DN or UNIQUEID */
    assert_int_equal(slapi_pblock_get(pb, SLAPI_ORIGINAL_TARGET_DN, &dn), 0);
    assert_null(dn);
    assert_int_equal(slapi_pblock_get(pb, SLAPI_TARGET_UNIQUEID, &dn), 0);
    assert_null(dn);

    /* Free everything ourselves! */
    slapi_sdn_free(&test_a_sdn);
    slapi_sdn_free(&test_b_sdn);

    /* It works! */
    slapi_pblock_destroy(pb);
    ndn_cache_destroy();
}

/* nf here means "no implicit free". For now implies no dup */
void
_test_libslapi_pblock_v3c_generic_nf_char(Slapi_PBlock *pb, int type, int *conflicts)
{
    /* We have to accept a valid PB, because some tests require operations etc. */
    char *out = NULL;
    char *test_a_in = slapi_ch_strdup("some awesome value");
    char *test_b_in = slapi_ch_strdup("some other value");

    /* Check we start nulled. */
    assert_int_equal(slapi_pblock_get(pb, type, &out), 0);
    assert_null(out);
    /* Now, set a value into the pblock. */
    assert_int_equal(slapi_pblock_set(pb, type, test_a_in), 0);
    assert_int_equal(slapi_pblock_get(pb, type, &out), 0);
    assert_ptr_equal(test_a_in, out);
    assert_int_equal(strcmp(out, test_a_in), 0);

    /* Test another value, and does not free a. */
    assert_int_equal(slapi_pblock_set(pb, type, test_b_in), 0);
    assert_int_equal(slapi_pblock_get(pb, type, &out), 0);
    assert_ptr_equal(test_b_in, out);
    assert_int_equal(strcmp(out, test_b_in), 0);
    assert_int_not_equal(strcmp(out, test_a_in), 0);

    /* conflicts takes a null terminated array of values that we want to assert we do not influence */
    for (size_t i = 0; conflicts[i] != 0; i++) {
        assert_int_equal(slapi_pblock_get(pb, conflicts[i], &out), 0);
        assert_null(out);
    }

    slapi_ch_free_string(&test_a_in);
    slapi_ch_free_string(&test_b_in);
}

void
test_libslapd_pblock_v3c_original_target_dn(void **state __attribute__((unused)))
{
    ndn_cache_init();
    /* SLAPI_ORIGINAL_TARGET_DN */
    Slapi_PBlock *pb = slapi_pblock_new();
    Slapi_Operation *op = slapi_operation_new(SLAPI_OP_FLAG_INTERNAL);
    int conflicts[] = {SLAPI_TARGET_UNIQUEID, SLAPI_TARGET_SDN, 0};
    slapi_pblock_init(pb);

    /* Add the operation */
    assert_int_equal(slapi_pblock_set(pb, SLAPI_OPERATION, op), 0);
    /* Run the generic char * tests */
    _test_libslapi_pblock_v3c_generic_nf_char(pb, SLAPI_ORIGINAL_TARGET_DN, conflicts);

    /* It works! */
    slapi_pblock_destroy(pb);
    ndn_cache_destroy();
}

void
test_libslapd_pblock_v3c_target_uniqueid(void **state __attribute__((unused)))
{
    /* SLAPI_TARGET_UNIQUEID */
    Slapi_PBlock *pb = slapi_pblock_new();
    Slapi_Operation *op = slapi_operation_new(SLAPI_OP_FLAG_INTERNAL);
    int conflicts[] = {SLAPI_ORIGINAL_TARGET_DN, SLAPI_TARGET_SDN, 0};
    slapi_pblock_init(pb);

    /* Add the operation */
    assert_int_equal(slapi_pblock_set(pb, SLAPI_OPERATION, op), 0);
    /* Run the generic char * tests */
    _test_libslapi_pblock_v3c_generic_nf_char(pb, SLAPI_TARGET_UNIQUEID, conflicts);

    /* It works! */
    slapi_pblock_destroy(pb);
}

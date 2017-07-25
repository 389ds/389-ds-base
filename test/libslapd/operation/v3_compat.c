/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2017 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "../../test_slapd.h"

/* This is somewhat internal to the server! Thus the slap.h */
/* WARNING: Slap.h needs cert.h, which requires the -I/lib/ldaputil!!! */
/* It also pulls in nspr! Bad! */

#include <slap.h>

/*
 * Assert that the compatability requirements of the plugin V3 pblock API
 * are upheld.
 *
 * This is for checking the operation type maintains a correct interface.
 */

void
test_libslapd_operation_v3c_target_spec(void **state __attribute__((unused)))
{
    /* Need to start the ndn cache ... */
    ndn_cache_init();
    /* Will we need to test PB / op interactions? */
    /* Test the operation of the target spec is maintained. */
    Slapi_Operation *op = slapi_operation_new(SLAPI_OP_FLAG_INTERNAL);
    Slapi_DN *test_a_sdn = slapi_sdn_new_dn_byval("cn=a,cn=test");
    Slapi_DN *a_sdn = NULL;
    Slapi_DN *b_sdn = NULL;

    /* Create an SDN */
    /* Set it. */
    operation_set_target_spec(op, test_a_sdn);
    /* Assert the pointers are different because target_spec DUPS */
    a_sdn = operation_get_target_spec(op);
    assert_ptr_not_equal(a_sdn, test_a_sdn);
    assert_int_equal(slapi_sdn_compare(a_sdn, test_a_sdn), 0);

    /* Set a new SDN */
    operation_set_target_spec_str(op, "cn=b,cn=test");
    /* Assert the old SDN is there, as we don't free on set */
    b_sdn = operation_get_target_spec(op);
    assert_ptr_not_equal(b_sdn, a_sdn);
    assert_ptr_not_equal(b_sdn, test_a_sdn);
    assert_int_not_equal(slapi_sdn_compare(a_sdn, b_sdn), 0);

    /* free everything */
    slapi_sdn_free(&test_a_sdn);
    slapi_sdn_free(&a_sdn);
    /* target_spec in now the b_sdn, so operation free will free it */
    // slapi_sdn_free(&b_sdn);
    operation_free(&op, NULL);
    /* Close ndn cache */
    ndn_cache_destroy();
}

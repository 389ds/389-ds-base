
/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2017 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "../../test_slapd.h"

/* Define this here to access the symbol in libslapd */
uint64_t pblock_analytics_query(Slapi_PBlock *pb, int access_type);

void
test_libslapd_pblock_analytics(void **state __attribute__((unused)))
{
#ifdef PBLOCK_ANALYTICS
    /* Create a pblock */
    Slapi_PBlock *pb = slapi_pblock_new();
    slapi_pblock_init(pb);
    /* Check the counters are 0 */
    assert_int_equal(pblock_analytics_query(pb, SLAPI_BACKEND_COUNT), 0);

    uint32_t becount = 0;
    /* Test get and set */
    slapi_pblock_get(pb, SLAPI_BACKEND_COUNT, &becount);

    /* Make sure the counters were changed correctly */
    assert_int_equal(pblock_analytics_query(pb, SLAPI_BACKEND_COUNT), 1);

    /* It works! */
    slapi_pblock_destroy(pb);
#endif
}

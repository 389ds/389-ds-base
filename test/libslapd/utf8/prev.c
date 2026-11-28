/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2026 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "../../test_slapd.h"
#include <string.h>

void
test_libslapd_utf8_prev_safe(void **state __attribute__((unused)))
{
    char ascii[] = "ab";
    char mb[] = "a\xC3\xA9";
    char run[10];
    char *v;

    /* Nothing precedes the start of the value. */
    assert_null(ldap_utf8prev_safe(ascii, ascii));
    assert_null(ldap_utf8prev_safe(NULL, ascii));
    assert_null(ldap_utf8prev_safe(ascii, NULL));

    /* Well-formed input: same answer as ldap_utf8prev(). */
    assert_ptr_equal(ldap_utf8prev_safe(ascii + 1, ascii), ascii);
    assert_ptr_equal(ldap_utf8prev_safe(ascii + 2, ascii), ascii + 1);
    assert_ptr_equal(ldap_utf8prev_safe(mb + 3, mb), mb + 1);
    assert_ptr_equal(ldap_utf8prev_safe(mb + 3, mb), ldap_utf8prev(mb + 3));
    assert_ptr_equal(ldap_utf8prev_safe(mb + 1, mb), mb);

    /*
     * A value starting with stray continuation bytes: the walk stops at
     * the start instead of running off the front. Heap allocated so that
     * a read below it is caught when built with AddressSanitizer.
     */
    v = slapi_ch_malloc(3);
    memcpy(v, "\xA0\x80 ", 3);
    assert_ptr_equal(ldap_utf8prev_safe(v + 1, v), v);
    assert_ptr_equal(ldap_utf8prev_safe(v + 2, v), v);
    assert_ptr_equal(ldap_utf8prev_safe(v + 3, v), v + 2);
    slapi_ch_free((void **)&v);

    /* The six-byte bound of ldap_utf8prev() is kept. */
    run[0] = 'a';
    memset(run + 1, 0x80, 8);
    run[9] = '\0';
    assert_ptr_equal(ldap_utf8prev_safe(run + 9, run), run + 3);
    assert_ptr_equal(ldap_utf8prev_safe(run + 9, run), ldap_utf8prev(run + 9));
}

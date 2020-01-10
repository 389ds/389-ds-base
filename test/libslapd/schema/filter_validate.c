/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2019 William Brown <william@blackhats.net.au>
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "../../test_slapd.h"

#include <slap.h>
#include <proto-slap.h>
#include <string.h>

/*
 * This is a test-only function, that is able to generate test, mock
 * asyntaxinfo's for us.
 */

static struct asyntaxinfo *
attr_syntax_add_from_name(char *name, char *oid) {

    char *names[2] = {0};
    names[0] = name;

    /* This is apparently leaking the allocated struct, but not it's memebers ... huh?  */
    struct asyntaxinfo *asi = NULL;

    attr_syntax_create(
        oid, // attr_oid
        names, // attr_names
        "testing attribute type",
        NULL, // attr_supe
        NULL, // attr eq
        NULL, // attr order
        NULL, // attr sub
        NULL, // exten
        DIRSTRING_SYNTAX_OID, // attr_syntax
        SLAPI_SYNTAXLENGTH_NONE,// syntaxlen
        SLAPI_ATTR_FLAG_STD_ATTR | SLAPI_ATTR_FLAG_OPATTR, // flags
        &asi // syntaxinfo **, where the function returns data to.
    );

    assert_true(attr_syntax_add(asi, 0) == 0);

    return asi;
}

Slapi_Filter_Result
validate_filter(char *fstr, Slapi_Filter_Policy policy) {

    char fdup[256] = {0};
    strcpy(fdup, fstr);
    struct slapi_filter *f = slapi_str2filter(fdup);
    Slapi_PBlock *pb = slapi_pblock_new();
    assert_true(f != NULL);

    Slapi_Filter_Result r = slapi_filter_schema_check(pb, f, policy);
    // Based on policy, we could assert if flags are set.

    slapi_pblock_destroy(pb);
    slapi_filter_free(f, 1);
    return r;
}


void
test_libslapd_schema_filter_validate_simple(void **state __attribute__((unused)))
{
    /* Setup a fake schema */
    attr_syntax_write_lock();

    /* We'll add two attributes, test_a and test_b */
    struct asyntaxinfo *a = attr_syntax_add_from_name("test_a", "1.1.0.0.0.0.1");
    struct asyntaxinfo *b = attr_syntax_add_from_name("test_b", "1.1.0.0.0.0.2");

    attr_syntax_unlock_write();

    /* Get the read lock ready */
    attr_syntax_read_lock();

    /* Test some simple filters */
    char *invalid = "(&(non_exist=a)(more_not_real=b))";
    char *par_valid = "(&(test_a=a)(more_not_real=b))";
    char *valid = "(&(test_a=a)(test_b=b))";
    char *valid_case = "(&(Test_A=a)(Test_B=b))";

    /* Did they pass given the policy and expectations? */

    /* simple error cases */
    Slapi_PBlock *pb = slapi_pblock_new();
    assert_true(slapi_filter_schema_check(pb, NULL, FILTER_POLICY_OFF) == FILTER_SCHEMA_FAILURE);
    slapi_pblock_destroy(pb);

    /* policy off, always success no matter what */
    assert_true(validate_filter(invalid, FILTER_POLICY_OFF) == FILTER_SCHEMA_SUCCESS);
    assert_true(validate_filter(par_valid, FILTER_POLICY_OFF) == FILTER_SCHEMA_SUCCESS);
    assert_true(validate_filter(valid, FILTER_POLICY_OFF) == FILTER_SCHEMA_SUCCESS);
    /* policy warning */
    assert_true(validate_filter(invalid, FILTER_POLICY_WARNING) == FILTER_SCHEMA_WARNING);
    assert_true(validate_filter(par_valid, FILTER_POLICY_WARNING) == FILTER_SCHEMA_WARNING);
    assert_true(validate_filter(valid, FILTER_POLICY_WARNING) == FILTER_SCHEMA_SUCCESS);
    /* policy strict */
    assert_true(validate_filter(invalid, FILTER_POLICY_STRICT) == FILTER_SCHEMA_FAILURE);
    assert_true(validate_filter(par_valid, FILTER_POLICY_STRICT) == FILTER_SCHEMA_FAILURE);
    assert_true(validate_filter(valid, FILTER_POLICY_STRICT) == FILTER_SCHEMA_SUCCESS);

    /* policy warning, complex filters. Try to exercise all the paths in the parser!! */
    assert_true(validate_filter("(|(!(b=b))(le<=a)(ge>=a)(sub=*a*)(pres=*))", FILTER_POLICY_WARNING) == FILTER_SCHEMA_WARNING);

    /* Check case sense */
    assert_true(validate_filter(valid_case, FILTER_POLICY_WARNING) == FILTER_SCHEMA_SUCCESS);


    attr_syntax_unlock_read();

    /* Cleanup */
    attr_syntax_write_lock();
    attr_syntax_delete(a, 0);
    attr_syntax_delete(b, 0);
    attr_syntax_unlock_write();

    return;
}

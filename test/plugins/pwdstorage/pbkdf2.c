/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2017 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "../../test_slapd.h"

#include <nss.h>
#include <pwdstorage.h>

int
test_plugin_pwdstorage_nss_setup(void **state __attribute__((unused)))
{
    int result = NSS_Initialize(NULL, "", "", SECMOD_DB, NSS_INIT_READONLY | NSS_INIT_NOCERTDB | NSS_INIT_NOMODDB);
    assert_true(result == 0);
    return result;
}

int
test_plugin_pwdstorage_nss_stop(void **state __attribute__((unused)))
{
    NSS_Shutdown();
    return 0;
}

void
test_plugin_pwdstorage_pbkdf2_auth(void **state __attribute__((unused)))
{

#if (NSS_VMAJOR * 100 + NSS_VMINOR) > 328
    /* Check that given various known passwords and hashes they validate (or don't) */

    /* 'password' */
    const char *password_a = "AAB1MHPzX9ZP+HDQYp/+qxQwJAW5cXhRvXX1+w0NBMVX6FyMv2uzIvtBfvn6A3o84gKW9fBl5hGPeH87bQMZs977SvCV09P8MV/fkkjH7EoYNXoSQ6FFBpjm3orFplT9Y5PY14xRvJS4iicQ82uKaaARlkbn0uLaHBNS18uz1YFzuYUlf4lqh+uy1VzAR3YQW9FWKL9TYCsTRx75EGUMYj/f7826CqrHNubnljh4s5gi31y+2qsdzdRerT1ISZC5z0kQbkXZYM7UCa4hlbSQl3mO6lpyxk44oiPkbKKii+bS+KRdIMeMgFawXo2L4+IYx+qXvJRwyi1M8vIxK+dnc2kOrLF9E7rZvs0hn9PuXMW3Itq46wPL3R51wo+0ki4gA36ZNF3PegbjFiAvrh24/D3SQMBjfk1YMDstNGJaMefd3bS1";

    /*
     * 'password' - but we mucked with the rounds
     * note the 5th char  of the b64 is "L" not "M'. This changes the rounds from
     * 30000 to 29996, which means we should fail
     */
    const char *password_a_rounds = "AAB1LHPzX9ZP+HDQYp/+qxQwJAW5cXhRvXX1+w0NBMVX6FyMv2uzIvtBfvn6A3o84gKW9fBl5hGPeH87bQMZs977SvCV09P8MV/fkkjH7EoYNXoSQ6FFBpjm3orFplT9Y5PY14xRvJS4iicQ82uKaaARlkbn0uLaHBNS18uz1YFzuYUlf4lqh+uy1VzAR3YQW9FWKL9TYCsTRx75EGUMYj/f7826CqrHNubnljh4s5gi31y+2qsdzdRerT1ISZC5z0kQbkXZYM7UCa4hlbSQl3mO6lpyxk44oiPkbKKii+bS+KRdIMeMgFawXo2L4+IYx+qXvJRwyi1M8vIxK+dnc2kOrLF9E7rZvs0hn9PuXMW3Itq46wPL3R51wo+0ki4gA36ZNF3PegbjFiAvrh24/D3SQMBjfk1YMDstNGJaMefd3bS1";

    /*
     * 'password' - but we mucked with the salt  Note the change in the 8th char from
     * z to 0.
     */
    const char *password_a_salt = "AAB1MHP0X9ZP+HDQYp/+qxQwJAW5cXhRvXX1+w0NBMVX6FyMv2uzIvtBfvn6A3o84gKW9fBl5hGPeH87bQMZs977SvCV09P8MV/fkkjH7EoYNXoSQ6FFBpjm3orFplT9Y5PY14xRvJS4iicQ82uKaaARlkbn0uLaHBNS18uz1YFzuYUlf4lqh+uy1VzAR3YQW9FWKL9TYCsTRx75EGUMYj/f7826CqrHNubnljh4s5gi31y+2qsdzdRerT1ISZC5z0kQbkXZYM7UCa4hlbSQl3mO6lpyxk44oiPkbKKii+bS+KRdIMeMgFawXo2L4+IYx+qXvJRwyi1M8vIxK+dnc2kOrLF9E7rZvs0hn9PuXMW3Itq46wPL3R51wo+0ki4gA36ZNF3PegbjFiAvrh24/D3SQMBjfk1YMDstNGJaMefd3bS1";

    assert_true(pbkdf2_sha256_pw_cmp("password", password_a) == 0);
    assert_false(pbkdf2_sha256_pw_cmp("password", password_a_rounds) == 0);
    assert_false(pbkdf2_sha256_pw_cmp("password", password_a_salt) == 0);
    assert_false(pbkdf2_sha256_pw_cmp("password_b", password_a) == 0);
#endif
}

void
test_plugin_pwdstorage_pbkdf2_rounds(void **state __attribute__((unused)))
{
#if (NSS_VMAJOR * 100 + NSS_VMINOR) > 328
    /* Check the benchmark, and make sure we get a valid timestamp */
    assert_true(pbkdf2_sha256_benchmark_iterations() > 0);
    /*
     * provide various values to the calculator, to check we get the right
     * number of rounds back.
     */
    /*
     * On a very slow system, we get the default min rounds out.
     */
    assert_true(pbkdf2_sha256_calculate_iterations(10000000000) == 2048);
    /*
     * On a "fast" system, we should see more rounds.
     */
    assert_true(pbkdf2_sha256_calculate_iterations(800000000) == 2048);
    assert_true(pbkdf2_sha256_calculate_iterations(5000000) == 10000);
    assert_true(pbkdf2_sha256_calculate_iterations(2500000) == 20000);
#endif
}

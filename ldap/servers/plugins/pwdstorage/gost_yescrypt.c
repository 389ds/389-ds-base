/** BEGIN COPYRIGHT BLOCK
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pwdstorage.h"

#include <crypt.h>

#ifdef XCRYPT_VERSION_STR
#include <errno.h>
int
gost_yescrypt_pw_cmp(const char *userpwd, const char *dbpwd)
{
    /* return 0 If the passwords match, return 1 if passwords do not match. */
    int rc = 1;
    char *hash;
    struct crypt_data output = {0};

    hash = crypt_rn(userpwd, dbpwd, &output, (int) sizeof(output));
    if (!hash) {
        slapi_log_err(SLAPI_LOG_ERR, GOST_YESCRYPT_SCHEME_NAME,
                      "Unable to hash userpwd value: %d\n", errno);
        return rc;
    }

    if (slapi_ct_memcmp(hash, dbpwd, strlen(dbpwd)) == 0) {
        rc = 0;
    }

    return rc;
}

char *
gost_yescrypt_pw_enc(const char *pwd)
{
    const char *prefix = "$gy$";
    char salt[CRYPT_GENSALT_OUTPUT_SIZE];
    char *hash;
    char *enc = NULL;
    struct crypt_data output = {0};

    /* 0 - means default, in Y2020 it defaults to 5 */
    if (!crypt_gensalt_rn(prefix, 0, NULL, 0, salt, (int) sizeof(salt))) {
        slapi_log_err(SLAPI_LOG_ERR, GOST_YESCRYPT_SCHEME_NAME,
                      "Unable to generate salt: %d\n", errno);
        return NULL;
    }

    hash = crypt_rn(pwd, salt, &output, (int) sizeof(output));
    if (!hash) {
        slapi_log_err(SLAPI_LOG_ERR, GOST_YESCRYPT_SCHEME_NAME,
                      "Unable to hash pwd value: %d\n", errno);
        return NULL;
    }
    enc = slapi_ch_smprintf("%c%s%c%s", PWD_HASH_PREFIX_START,
                            GOST_YESCRYPT_SCHEME_NAME, PWD_HASH_PREFIX_END,
                            hash);

    return enc;
}

#else

/*
 * We do not have xcrypt, so always fail all checks.
 */
int
gost_yescrypt_pw_cmp(const char *userpwd __attribute__((unused)), const char *dbpwd __attribute__((unused)))
{
    slapi_log_err(SLAPI_LOG_ERR, GOST_YESCRYPT_SCHEME_NAME,
                  "Unable to use gost_yescrypt_pw_cmp, xcrypt is not available.\n");
    return 1;
}

char *
gost_yescrypt_pw_enc(const char *pwd __attribute__((unused)))
{
    slapi_log_err(SLAPI_LOG_ERR, GOST_YESCRYPT_SCHEME_NAME,
                  "Unable to use gost_yescrypt_pw_enc, xcrypt is not available.\n");
    return NULL;
}
#endif

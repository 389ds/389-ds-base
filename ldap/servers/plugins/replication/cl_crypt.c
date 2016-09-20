/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2010 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* cl_crypt.c - handles changelog encryption. */

#include <errno.h>
#include <sys/stat.h>
#if defined( OS_solaris ) || defined( hpux )
#include <sys/types.h>
#include <sys/statvfs.h>
#endif
#if defined( linux )
#include <sys/vfs.h>
#endif

#include "slapi-plugin.h"
#include "cl5_api.h"
#include "cl_crypt.h"

/*
 * BACK_INFO_CRYPT_INIT
 */
int
clcrypt_init(const CL5DBConfig *config, void **clcrypt_handle)
{
    int rc = 0;
    char *cookie = NULL;
    Slapi_Backend *be = NULL;
    back_info_crypt_init crypt_init = {0};

    slapi_log_error(SLAPI_LOG_TRACE, repl_plugin_name, "-> clcrypt_init\n");
    /* Encryption is not specified */
    if (!config->encryptionAlgorithm || !clcrypt_handle) {
        goto bail;
    }
    crypt_init.dn = "cn=changelog5,cn=config";
    crypt_init.encryptionAlgorithm = config->encryptionAlgorithm;

    be = slapi_get_first_backend(&cookie);
    while (be) {
        crypt_init.be = be;
        rc = slapi_back_ctrl_info(be, BACK_INFO_CRYPT_INIT,
                                  (void *)&crypt_init);
        if (LDAP_SUCCESS == rc) {
            break; /* Successfully fetched */
        }
        be = slapi_get_next_backend(cookie);
    }
    slapi_ch_free((void **)&cookie);

    if (LDAP_SUCCESS == rc && crypt_init.state_priv) {
        *clcrypt_handle = crypt_init.state_priv;
        rc = 0;
    } else {
        rc = 1;
    }
bail:
    slapi_log_error(SLAPI_LOG_TRACE, repl_plugin_name,
                    "<- clcrypt_init : %d\n", rc);
    return rc;
}

/*
 * return values:  0 - success
 *              :  1 - no encryption
 *              : -1 - error 
 *
 * output value: out: non-NULL - encryption successful
 *                  :     NULL - no encryption or failure
 */
int
clcrypt_encrypt_value(void *clcrypt_handle,
                      struct berval *in, struct berval **out)
{
    int rc = -1;
    char *cookie = NULL;
    Slapi_Backend *be = NULL;
    back_info_crypt_value crypt_value = {0};

    slapi_log_error(SLAPI_LOG_TRACE, repl_plugin_name, 
                    "-> clcrypt_encrypt_value\n");
    if (NULL == out) {
        goto bail;
    }
    *out = NULL;
    if (NULL == clcrypt_handle) {
        rc = 1;
        goto bail;
    }
    crypt_value.state_priv = clcrypt_handle;
    crypt_value.in = in;

    be = slapi_get_first_backend(&cookie);
    while (be) {
        rc = slapi_back_ctrl_info(be, BACK_INFO_CRYPT_ENCRYPT_VALUE,
                                  (void *)&crypt_value);
        if (LDAP_SUCCESS == rc) {
            break; /* Successfully fetched */
        }
        be = slapi_get_next_backend(cookie);
    }
    slapi_ch_free((void **)&cookie);
    if (LDAP_SUCCESS == rc && crypt_value.out) {
        *out = crypt_value.out;
        rc = 0;
    } else {
        rc = -1;
    }
bail:
    slapi_log_error(SLAPI_LOG_TRACE, repl_plugin_name, 
                    "<- clcrypt_encrypt_entry (returning %d)\n", rc);
    return rc;
}

/*
 * return values:  0 - success
 *              :  1 - no encryption
 *              : -1 - error 
 *
 * output value: out: non-NULL - encryption successful
 *                  :     NULL - no encryption or failure
 */
int 
clcrypt_decrypt_value(void *clcrypt_handle,
                      struct berval *in, struct berval **out)
{
    int rc = -1;
    char *cookie = NULL;
    Slapi_Backend *be = NULL;
    back_info_crypt_value crypt_value = {0};

    slapi_log_error(SLAPI_LOG_TRACE, repl_plugin_name, 
                    "-> clcrypt_decrypt_value\n");
    if (NULL == out) {
        goto bail;
    }
    *out = NULL;
    if (NULL == clcrypt_handle) {
        rc = 1;
        goto bail;
    }
    crypt_value.state_priv = clcrypt_handle;
    crypt_value.in = in;

    be = slapi_get_first_backend(&cookie);
    while (be) {
        rc = slapi_back_ctrl_info(be, BACK_INFO_CRYPT_DECRYPT_VALUE,
                                  (void *)&crypt_value);
        if (LDAP_SUCCESS == rc) {
            break; /* Successfully fetched */
        }
        be = slapi_get_next_backend(cookie);
    }
    slapi_ch_free((void **)&cookie);
    if (LDAP_SUCCESS == rc && crypt_value.out) {
        *out = crypt_value.out;
        rc = 0;
    } else {
        rc = -1;
    }
bail:
    slapi_log_error(SLAPI_LOG_TRACE, repl_plugin_name, 
                    "<- clcrypt_decrypt_entry (returning %d)\n", rc);
    return rc;
}

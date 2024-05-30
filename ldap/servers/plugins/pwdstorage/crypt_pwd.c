/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * slapd hashed password routines
 *
 */

#if defined(hpux) || defined(LINUX) || defined(__FreeBSD__)
# ifndef __USE_XOPEN
#  define __USE_XOPEN /* linux */
# endif              /* __USE_XOPEN */
# ifndef _DEFAULT_SOURCE
#  define _DEFAULT_SOURCE
# endif              /* !defined(_DEFAULT_SOURCE) */
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef __FreeBSD__
# include <crypt.h>
#endif
#include "pwdstorage.h"

/* characters used in crypt encoding */
static unsigned char itoa64[] = /* 0 ... 63 => ascii - 64 */
    "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

/* Use the same salt lengths as shadow */
#define CRYPT_UNIX_SALT_LENGTH 2
#define CRYPT_MD5_SALT_LENGTH 8
#define CRYPT_SHA_SALT_LENGTH 16

#define CRYPT_SALT_STRING_MAXLEN CRYPT_SHA_SALT_LENGTH + 1

int
crypt_pw_cmp(const char *userpwd, const char *dbpwd)
{
    int rc = -1;
    char *cp = NULL;
    size_t dbpwd_len = strlen(dbpwd);
    struct crypt_data data;
    data.initialized = 0;

    /*
     * there MUST be at least 2 chars of salt and some pw bytes, else this is INVALID and will
     * allow any password to bind as we then only compare SALTS.
     */
    if (dbpwd_len >= 3) {
        /* we use salt (first 2 chars) of encoded password in call to crypt_r() */
        cp = crypt_r(userpwd, dbpwd, &data);
    }
    /* If these are not the same length, we can not proceed safely with memcmp. */
    if (cp && dbpwd_len == strlen(cp)) {
        rc = slapi_ct_memcmp(dbpwd, cp, dbpwd_len);
    } else {
        rc = -1;
    }
    return rc;
}

static char *
crypt_pw_enc_by_hash(const char *pwd, int salt_len, const char *algo_id)
{
    char salt[CRYPT_SALT_STRING_MAXLEN];
    char *algo_salt = NULL;
    char *cry;
    char *enc = NULL;
    int i;
    struct crypt_data data;
    data.initialized = 0;

    /* Fill salt with salt_len random chars */
    slapi_rand_array(salt, salt_len);

    /* Convert each 8bit random char to a 6bit base64 char */
    for (i=0; i < salt_len; i++) {
        salt[i] = itoa64[salt[i] & 0x3f];
    }
    salt[i] = 0;

    algo_salt = slapi_ch_smprintf("%s%s", algo_id, salt);

    cry = crypt_r(pwd, algo_salt, &data);
    if (cry != NULL) {
        enc = slapi_ch_smprintf("%c%s%c%s", PWD_HASH_PREFIX_START, CRYPT_SCHEME_NAME, PWD_HASH_PREFIX_END, cry);
    }
    slapi_ch_free_string(&algo_salt);

    return (enc);
}

char *
crypt_pw_enc(const char *pwd)
{
    return crypt_pw_enc_by_hash(pwd, CRYPT_UNIX_SALT_LENGTH, "");
}

char *
crypt_pw_md5_enc(const char *pwd)
{
    return crypt_pw_enc_by_hash(pwd, CRYPT_MD5_SALT_LENGTH, "$1$");
}
char *
crypt_pw_sha256_enc(const char *pwd)
{
    return crypt_pw_enc_by_hash(pwd, CRYPT_SHA_SALT_LENGTH, "$5$");
}
char *
crypt_pw_sha512_enc(const char *pwd)
{
    return crypt_pw_enc_by_hash(pwd, CRYPT_SHA_SALT_LENGTH, "$6$");
}

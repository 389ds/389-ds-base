/** BEGIN COPYRIGHT BLOCK
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
 * MD5 Password Encryption/Comparison routines by David Irving, Fred Brittain,
 * and Aaron Gagnon --  University of Maine Farmington
 * Donated to the RedHat Directory Server Project 2005-06-10
 */

#include <string.h>
#include <sys/types.h>
#include <stdio.h>
#include <pk11func.h>
#include <nss.h>
#include <nssb64.h>
#include "pwdstorage.h"

#define MD5_HASH_LEN 20
#define MD5_SUBSYSTEM_NAME "MD5 password hash"

int
md5_pw_cmp(const char *userpwd, const char *dbpwd)
{
    int rc = -1;
    char *bver;
    PK11Context *ctx = NULL;
    unsigned int outLen;
    unsigned char hash_out[MD5_HASH_LEN];
    unsigned char b2a_out[MD5_HASH_LEN * 2]; /* conservative */
    SECItem binary_item;
    size_t dbpwd_len = strlen(dbpwd);

    ctx = PK11_CreateDigestContext(SEC_OID_MD5);
    if (ctx == NULL) {
        slapi_log_err(SLAPI_LOG_PLUGIN, MD5_SUBSYSTEM_NAME,
                      "Could not create context for digest operation for password compare");
        goto loser;
    }

    if (dbpwd_len >= sizeof b2a_out) {
        slapi_log_err(SLAPI_LOG_PLUGIN, MD5_SUBSYSTEM_NAME,
                      "The hashed password stored in the user entry is longer than any valid md5 hash");
        goto loser;
    }

    /* create the hash */
    PK11_DigestBegin(ctx);
    PK11_DigestOp(ctx, (const unsigned char *)userpwd, strlen(userpwd));
    PK11_DigestFinal(ctx, hash_out, &outLen, sizeof hash_out);
    PK11_DestroyContext(ctx, 1);

    /* convert the binary hash to base64 */
    binary_item.data = hash_out;
    binary_item.len = outLen;
    bver = NSSBase64_EncodeItem(NULL, (char *)b2a_out, sizeof b2a_out, &binary_item);
    /* bver points to b2a_out upon success */
    if (bver) {
        rc = slapi_ct_memcmp(bver, dbpwd, dbpwd_len);
    } else {
        slapi_log_err(SLAPI_LOG_PLUGIN, MD5_SUBSYSTEM_NAME,
                      "Could not base64 encode hashed value for password compare");
    }
loser:
    return rc;
}

char *
md5_pw_enc(const char *pwd)
{
    char *bver, *enc = NULL;
    PK11Context *ctx = NULL;
    unsigned int outLen;
    unsigned char hash_out[MD5_HASH_LEN];
    unsigned char b2a_out[MD5_HASH_LEN * 2]; /* conservative */
    SECItem binary_item;

    ctx = PK11_CreateDigestContext(SEC_OID_MD5);
    if (ctx == NULL) {
        slapi_log_err(SLAPI_LOG_PLUGIN, MD5_SUBSYSTEM_NAME,
                      "Could not create context for digest operation for password encoding");
        return NULL;
    }

    /* create the hash */
    PK11_DigestBegin(ctx);
    PK11_DigestOp(ctx, (const unsigned char *)pwd, strlen(pwd));
    PK11_DigestFinal(ctx, hash_out, &outLen, sizeof hash_out);
    PK11_DestroyContext(ctx, 1);

    /* convert the binary hash to base64 */
    binary_item.data = hash_out;
    binary_item.len = outLen;
    bver = NSSBase64_EncodeItem(NULL, (char *)b2a_out, sizeof b2a_out, &binary_item);
    if (bver) {
        enc = slapi_ch_smprintf("%c%s%c%s", PWD_HASH_PREFIX_START, MD5_SCHEME_NAME,
                                PWD_HASH_PREFIX_END, bver);
    } else {
        slapi_log_err(SLAPI_LOG_PLUGIN, MD5_SUBSYSTEM_NAME,
                      "Could not base64 encode hashed value for password encoding");
    }

    return (enc);
}

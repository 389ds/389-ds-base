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
 * Based on MD5 Password Encryption/Comparison routines by David Irving,
 * Fred Brittain, and Aaron Gagnon --  University of Maine Farmington
 * Donated to the RedHat Directory Server Project 2005-06-10
 */

#include <string.h>
#include <sys/types.h>
#include <stdio.h>
#include <pk11func.h>
#include <nss.h>
#include <nssb64.h>
#include <sechash.h>
#include "pwdstorage.h"

#define MD5_DEFAULT_SALT_LENGTH 4
#define MD5_MAX_SALT_LENGTH 16
#define SALTED_MD5_SUBSYSTEM_NAME "Salted MD5 password hash"

int
smd5_pw_cmp(const char *userpwd, const char *dbpwd)
{
    int rc = -1;
    PK11Context *ctx = NULL;
    unsigned int outLen;
    unsigned char userhash[MD5_LENGTH];
    PRUint32 hash_len;
    char quick_dbhash[MD5_LENGTH + MD5_DEFAULT_SALT_LENGTH + 1];
    char *dbhash = quick_dbhash;
    struct berval salt;
    char *hashresult = NULL;

    ctx = PK11_CreateDigestContext(SEC_OID_MD5);
    if (ctx == NULL) {
        slapi_log_err(SLAPI_LOG_PLUGIN, SALTED_MD5_SUBSYSTEM_NAME,
                      "Could not create context for digest operation for password compare");
        goto loser;
    }

    /*
    * Decode hash stored in database.
    */
    hash_len = pwdstorage_base64_decode_len(dbpwd, 0);
    if (hash_len >= sizeof(quick_dbhash)) { /* get more space: */
        dbhash = (char *)slapi_ch_calloc(hash_len + 1, sizeof(char));
        if (dbhash == NULL)
            goto loser;
    } else {
        memset(quick_dbhash, 0, sizeof(quick_dbhash));
    }

    hashresult = PL_Base64Decode(dbpwd, 0, dbhash);
    if (NULL == hashresult) {
        slapi_log_err(SLAPI_LOG_PLUGIN, SALTED_MD5_SUBSYSTEM_NAME,
                      "smd5_pw_cmp: userPassword \"%s\" is the wrong length "
                      "or is not properly encoded BASE64\n",
                      dbpwd);
        goto loser;
    }

    salt.bv_val = (void *)(dbhash + MD5_LENGTH); /* salt starts after hash value */
    salt.bv_len = hash_len - MD5_LENGTH;         /* remaining bytes must be salt */

    /* create the hash */
    memset(userhash, 0, sizeof(userhash));
    PK11_DigestBegin(ctx);
    PK11_DigestOp(ctx, (const unsigned char *)userpwd, strlen(userpwd));
    PK11_DigestOp(ctx, (unsigned char *)(salt.bv_val), salt.bv_len);
    PK11_DigestFinal(ctx, userhash, &outLen, sizeof userhash);
    PK11_DestroyContext(ctx, 1);

    /* Compare everything up to the salt. */
    rc = slapi_ct_memcmp(userhash, dbhash, MD5_LENGTH);

loser:
    if (dbhash && dbhash != quick_dbhash)
        slapi_ch_free_string((char **)&dbhash);
    return rc;
}

char *
smd5_pw_enc(const char *pwd)
{
    char *bver, *enc = NULL;
    PK11Context *ctx = NULL;
    unsigned int outLen;
    unsigned char hash_out[MD5_LENGTH + MD5_DEFAULT_SALT_LENGTH];
    unsigned char b2a_out[(MD5_LENGTH * 2) + (MD5_MAX_SALT_LENGTH * 2)]; /* conservative */
    unsigned char *salt = hash_out + MD5_LENGTH;
    struct berval saltval;
    SECItem binary_item;

    ctx = PK11_CreateDigestContext(SEC_OID_MD5);
    if (ctx == NULL) {
        slapi_log_err(SLAPI_LOG_PLUGIN, SALTED_MD5_SUBSYSTEM_NAME,
                      "Could not create context for digest operation for password encoding");
        return NULL;
    }

    /* prepare the hash output area */
    memset(hash_out, 0, sizeof(hash_out));

    /* generate a new random salt */
    slapi_rand_array((void *)salt, MD5_DEFAULT_SALT_LENGTH);
    saltval.bv_val = (void *)salt;
    saltval.bv_len = MD5_DEFAULT_SALT_LENGTH;

    /* create the hash */
    PK11_DigestBegin(ctx);
    PK11_DigestOp(ctx, (const unsigned char *)pwd, strlen(pwd));
    PK11_DigestOp(ctx, (unsigned char *)(saltval.bv_val), saltval.bv_len);
    PK11_DigestFinal(ctx, hash_out, &outLen, sizeof hash_out);
    PK11_DestroyContext(ctx, 1);

    /* convert the binary hash to base64 */
    binary_item.data = hash_out;
    binary_item.len = outLen + MD5_DEFAULT_SALT_LENGTH;
    bver = NSSBase64_EncodeItem(NULL, (char *)b2a_out, sizeof b2a_out, &binary_item);
    if (bver) {
        enc = slapi_ch_smprintf("%c%s%c%s", PWD_HASH_PREFIX_START, SALTED_MD5_SCHEME_NAME,
                                PWD_HASH_PREFIX_END, bver);
    } else {
        slapi_log_err(SLAPI_LOG_PLUGIN, SALTED_MD5_SUBSYSTEM_NAME,
                      "Could not base64 encode hashed value for password encoding");
    }

    return (enc);
}

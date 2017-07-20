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

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "pwdstorage.h"
#include "prtime.h"
#include "prlong.h"

#include <pk11func.h>
#include <pk11pqg.h>

#define SHA_SALT_LENGTH 8 /* number of bytes of data in salt */

SECStatus
sha_salted_hash(char *hash_out, const char *pwd, struct berval *salt, unsigned int secOID)
{
    PK11Context *ctx;
    unsigned int outLen;
    unsigned int shaLen;
    SECStatus rc;

    switch (secOID) {
    case SEC_OID_SHA1:
        shaLen = SHA1_LENGTH;
        break;
    case SEC_OID_SHA256:
        shaLen = SHA256_LENGTH;
        break;
    case SEC_OID_SHA384:
        shaLen = SHA384_LENGTH;
        break;
    case SEC_OID_SHA512:
        shaLen = SHA512_LENGTH;
        break;
    default:
        /* An unknown secOID was passed in.  We shouldn't get here. */
        rc = SECFailure;
        return rc;
    }

    if (salt && salt->bv_len) {
        ctx = PK11_CreateDigestContext(secOID);
        if (ctx == NULL) {
            rc = SECFailure;
        } else {
            PK11_DigestBegin(ctx);
            PK11_DigestOp(ctx, (unsigned char *)pwd, strlen(pwd));
            PK11_DigestOp(ctx, (unsigned char *)(salt->bv_val), salt->bv_len);
            PK11_DigestFinal(ctx, (unsigned char *)hash_out, &outLen, shaLen);
            PK11_DestroyContext(ctx, 1);
            if (outLen == shaLen)
                rc = SECSuccess;
            else
                rc = SECFailure;
        }
    } else {
        /*backward compatibility*/
        rc = PK11_HashBuf(secOID, (unsigned char *)hash_out, (unsigned char *)pwd, strlen(pwd));
    }

    return rc;
}

char *
salted_sha_pw_enc(const char *pwd, unsigned int shaLen)
{
    char hash[MAX_SHA_HASH_SIZE + SHA_SALT_LENGTH];
    char *salt = hash + shaLen;
    struct berval saltval;
    char *enc;
    size_t encsize;
    char *schemeName;
    unsigned int schemeNameLen;
    unsigned int secOID;

    /* Determine which algorithm we're using */
    switch (shaLen) {
    case SHA1_LENGTH:
        schemeName = SALTED_SHA1_SCHEME_NAME;
        schemeNameLen = SALTED_SHA1_NAME_LEN;
        secOID = SEC_OID_SHA1;
        break;
    case SHA256_LENGTH:
        schemeName = SALTED_SHA256_SCHEME_NAME;
        schemeNameLen = SALTED_SHA256_NAME_LEN;
        secOID = SEC_OID_SHA256;
        break;
    case SHA384_LENGTH:
        schemeName = SALTED_SHA384_SCHEME_NAME;
        schemeNameLen = SALTED_SHA384_NAME_LEN;
        secOID = SEC_OID_SHA384;
        break;
    case SHA512_LENGTH:
        schemeName = SALTED_SHA512_SCHEME_NAME;
        schemeNameLen = SALTED_SHA512_NAME_LEN;
        secOID = SEC_OID_SHA512;
        break;
    default:
        /* An unknown shaLen was passed in.  We shouldn't get here. */
        return (NULL);
    }

    memset(hash, 0, sizeof(hash));
    saltval.bv_val = (void *)salt;
    saltval.bv_len = SHA_SALT_LENGTH;

    /* generate a new random salt */
    slapi_rand_array(salt, SHA_SALT_LENGTH);

    /* hash the user's key */
    if (sha_salted_hash(hash, pwd, &saltval, secOID) != SECSuccess) {
        return (NULL);
    }

    encsize = 3 + schemeNameLen +
              LDIF_BASE64_LEN(shaLen + SHA_SALT_LENGTH);
    if ((enc = slapi_ch_calloc(encsize, sizeof(char))) == NULL) {
        return (NULL);
    }

    sprintf(enc, "%c%s%c", PWD_HASH_PREFIX_START, schemeName,
            PWD_HASH_PREFIX_END);
    (void)PL_Base64Encode(hash, (shaLen + SHA_SALT_LENGTH), enc + 2 + schemeNameLen);
    PR_ASSERT(0 == enc[encsize - 1]); /* must be null terminated */

    return (enc);
}

/*
 * Wrapper functions for password encoding
 */
char *
salted_sha1_pw_enc(const char *pwd)
{
    return salted_sha_pw_enc(pwd, SHA1_LENGTH);
}

char *
salted_sha256_pw_enc(const char *pwd)
{
    return salted_sha_pw_enc(pwd, SHA256_LENGTH);
}

char *
salted_sha384_pw_enc(const char *pwd)
{
    return salted_sha_pw_enc(pwd, SHA384_LENGTH);
}

char *
salted_sha512_pw_enc(const char *pwd)
{
    return salted_sha_pw_enc(pwd, SHA512_LENGTH);
}

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2016 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * slapd hashed password routines
 *
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "pwdstorage.h"

#include <pk11pub.h>

/* Need this for htonl and ntohl */
#include <arpa/inet.h>

/* WB Nist recommend 128 bits (16 bytes) in 2016, may as well go for more to future proof. */
/* !!!!!!!! NEVER CHANGE THESE VALUES !!!!!!!! */
#define PBKDF2_SALT_LENGTH 64
#define PBKDF2_ITERATIONS_LENGTH 4
/* If this isn't 256 NSS explodes without setting an error code .... */
#define PBKDF2_HASH_LENGTH 256
#define PBKDF2_TOTAL_LENGTH (PBKDF2_ITERATIONS_LENGTH + PBKDF2_SALT_LENGTH + PBKDF2_HASH_LENGTH)
/* ======== END NEVER CHANGE THESE VALUES ==== */

/* 
 * WB - It's important we keep this private, and we increment it over time.
 * Administrators are likely to forget to update it, or they will set it too low.
 * We therfore keep it private, so we can increase it as our security recomendations
 * change and improve.
 *
 * At the same time we MUST increase this with each version of Directory Server
 * This value is written into the hash, so it's safe to change.
 */
#define PBKDF2_ITERATIONS 30000

static const char *schemeName = PBKDF2_SHA256_SCHEME_NAME;
static const PRUint32 schemeNameLength = PBKDF2_SHA256_NAME_LEN;

/* For requesting the slot which supports these types */
static CK_MECHANISM_TYPE mechanism_array[] = {CKM_SHA256_HMAC, CKM_PKCS5_PBKD2};

void
pbkdf2_sha256_extract(char *hash_in, SECItem *salt, PRUint32 *iterations)
{
    /*
     * This will take the input of hash_in (generated from pbkdf2_sha256_hash) and
     * populate the hash (output of nss pkbdf2), salt, and iterations.
     * Enough space should be avaliable in these for the values to fit into.
     */

    memcpy(iterations, hash_in, PBKDF2_ITERATIONS_LENGTH);
    /* We use ntohl on this value to make sure it's correct endianess. */
    *iterations = ntohl(*iterations);

    /* warning: pointer targets in assignment differ in signedness [-Wpointer-sign] */
    salt->data = (unsigned char *)(hash_in + PBKDF2_ITERATIONS_LENGTH);
    salt->len = PBKDF2_SALT_LENGTH;
}

SECStatus
pbkdf2_sha256_hash(char *hash_out, size_t hash_out_len, SECItem *pwd, SECItem *salt, PRUint32 iterations)
{
    SECItem *result = NULL;
    SECAlgorithmID *algid = NULL;
    PK11SlotInfo *slot = NULL;
    PK11SymKey *symkey = NULL;

    /* We assume that NSS is already started. */
    algid = PK11_CreatePBEV2AlgorithmID(SEC_OID_PKCS5_PBKDF2, SEC_OID_HMAC_SHA256, SEC_OID_HMAC_SHA256, hash_out_len, iterations, salt);

    if (algid != NULL) {
        /* Gets the best slot that provides SHA256HMAC and PBKDF2 (may not be the default!) */
        slot = PK11_GetBestSlotMultiple(mechanism_array, 2, NULL);
        if (slot != NULL) {
            symkey = PK11_PBEKeyGen(slot, algid, pwd, PR_FALSE, NULL);
            PK11_FreeSlot(slot);
            if (symkey == NULL) {
                /* We try to get the Error here but NSS has two or more error interfaces, and sometimes it uses none of them. */
                PRInt32 status = PORT_GetError();
                slapi_log_err(SLAPI_LOG_ERR, (char *)schemeName, "Unable to retrieve symkey from NSS. Error code might be %d ???\n", status);
                slapi_log_err(SLAPI_LOG_ERR, (char *)schemeName, "The most likely cause is your system has nss 3.21 or lower. PBKDF2 requires nss 3.22 or higher.\n");
                return SECFailure;
            }
        } else {
            slapi_log_err(SLAPI_LOG_ERR, (char *)schemeName, "Unable to retrieve slot from NSS.\n");
            return SECFailure;
        }
        SECOID_DestroyAlgorithmID(algid, PR_TRUE);
    } else {
        /* Uh oh! */
        slapi_log_err(SLAPI_LOG_ERR, (char *)schemeName, "Unable to generate algorithm ID.\n");
        return SECFailure;
    }

    if (PK11_ExtractKeyValue(symkey) == SECSuccess) {
        result = PK11_GetKeyData(symkey);
        if (result != NULL && result->len <= hash_out_len) {
            memcpy(hash_out, result->data, result->len);
            PK11_FreeSymKey(symkey);
        } else {
            PK11_FreeSymKey(symkey);
            slapi_log_err(SLAPI_LOG_ERR, (char *)schemeName, "Unable to retrieve (get) hash output.\n");
            return SECFailure;
        }
    } else {
        slapi_log_err(SLAPI_LOG_ERR, (char *)schemeName, "Unable to extract hash output.\n");
        return SECFailure;
    }

    return SECSuccess;
}

char *
pbkdf2_sha256_pw_enc(const char *pwd)
{
    char hash[ PBKDF2_TOTAL_LENGTH ];
    size_t encsize = 3 + schemeNameLength + LDIF_BASE64_LEN(PBKDF2_TOTAL_LENGTH);
    char *enc = slapi_ch_calloc(encsize, sizeof(char));
    PRUint32 iterations = PBKDF2_ITERATIONS;

    SECItem saltItem;
    SECItem passItem;
    char salt[PBKDF2_SALT_LENGTH];

    memset(hash, 0, PBKDF2_TOTAL_LENGTH);
    memset(salt, 0, PBKDF2_SALT_LENGTH);
    saltItem.data = (unsigned char *)salt;
    saltItem.len = PBKDF2_SALT_LENGTH;
    passItem.data = (unsigned char *)pwd;
    passItem.len = strlen(pwd);

    /* make a new random salt */
    slapi_rand_array(salt, PBKDF2_SALT_LENGTH);

    /*
     * Preload the salt and iterations to the output.
     * memcpy the iterations to the hash_out
     * We use ntohl on this value to make sure it's correct endianess.
     */
    iterations = htonl(iterations);
    memcpy(hash, &iterations, PBKDF2_ITERATIONS_LENGTH);
    /* memcpy the salt to the hash_out */
    memcpy(hash + PBKDF2_ITERATIONS_LENGTH, saltItem.data, PBKDF2_SALT_LENGTH);

    /*
     *                      This offset is to make the hash function put the values
     *                      In the correct part of the memory.
     */
    if ( pbkdf2_sha256_hash(hash + PBKDF2_ITERATIONS_LENGTH + PBKDF2_SALT_LENGTH, PBKDF2_HASH_LENGTH, &passItem, &saltItem, PBKDF2_ITERATIONS) != SECSuccess ) {
        slapi_log_err(SLAPI_LOG_ERR, (char *)schemeName, "Could not generate pbkdf2_sha256_hash!\n");
        slapi_ch_free_string(&enc);
        return NULL;
    }

    sprintf(enc, "%c%s%c", PWD_HASH_PREFIX_START, schemeName, PWD_HASH_PREFIX_END);
    (void)PL_Base64Encode( hash, PBKDF2_TOTAL_LENGTH, enc + 2 + schemeNameLength);
    PR_ASSERT(enc[encsize - 1] == '\0');

    slapi_log_err(SLAPI_LOG_PLUGIN, (char *)schemeName, "Generated hash %s\n", enc);

    return enc;
}

PRInt32
pbkdf2_sha256_pw_cmp(const char *userpwd, const char *dbpwd)
{
    PRInt32 result = 1; /* Default to fail. */
    char dbhash[ PBKDF2_TOTAL_LENGTH ];
    char userhash[ PBKDF2_HASH_LENGTH ];
    PRUint32 dbpwd_len = strlen(dbpwd);
    SECItem saltItem;
    SECItem passItem;
    PRUint32 iterations = 0;

    /* Our hash value is always at a known offset. */
    char *hash = dbhash + PBKDF2_ITERATIONS_LENGTH + PBKDF2_SALT_LENGTH;

    slapi_log_err(SLAPI_LOG_PLUGIN, (char *)schemeName, "Comparing password\n");

    memset(dbhash, 0, PBKDF2_TOTAL_LENGTH);

    passItem.data = (unsigned char *)userpwd;
    passItem.len = strlen(userpwd);

    /* Decode the DBpwd to bytes from b64 */
    if ( PL_Base64Decode( dbpwd, dbpwd_len, dbhash) == NULL ) {
        slapi_log_err(SLAPI_LOG_ERR, (char *)schemeName, "Unable to base64 decode dbpwd value\n");
        return result;
    }
    /* extract the fields */
    pbkdf2_sha256_extract(dbhash, &saltItem, &iterations);

    /* Now send the userpw to the hash function, with the salt + iter. */
    if ( pbkdf2_sha256_hash(userhash, PBKDF2_HASH_LENGTH, &passItem, &saltItem, iterations) != SECSuccess ) {
        slapi_log_err(SLAPI_LOG_ERR, (char *)schemeName, "Unable to hash userpwd value\n");
        return result;
    }
    /* Now compare the result of pbkdf2_sha256_hash. */
    result = memcmp(userhash, hash, PBKDF2_HASH_LENGTH);

    return result;
}



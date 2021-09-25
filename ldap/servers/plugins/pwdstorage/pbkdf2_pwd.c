/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2016 Red Hat, Inc.
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
 *
 * So let's assume that we have 72 threads, and we want to process say ... 10,000 binds per
 * second. At 72 threads, that's 138 ops per second per thread. This means each op have to take
 * 7.2 milliseconds to complete. We know binds really are quicker, but for now, lets say this can
 * be 2 milliseconds to time for.
 */

#define PBKDF2_MILLISECONDS 2

/*
 * We would like to raise this, but today due to NSS issues we have to be conservative. Regardless
 * it's still better than ssha512.
 */
#define PBKDF2_MINIMUM 2048

static uint32_t PBKDF2_ITERATIONS = 8192;

static const char *schemeName = PBKDF2_SHA256_SCHEME_NAME;
static const uint32_t schemeNameLength = PBKDF2_SHA256_NAME_LEN;

/* For requesting the slot which supports these types */
static CK_MECHANISM_TYPE mechanism_array[] = {CKM_SHA256_HMAC, CKM_PKCS5_PBKD2};

/* Used in our startup benching code */
#define PBKDF2_BENCH_ROUNDS 25000
#define PBKDF2_BENCH_LOOP 8

void
pbkdf2_sha256_extract(char *hash_in, SECItem *salt, uint32_t *iterations)
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
pbkdf2_sha256_hash(char *hash_out, size_t hash_out_len, SECItem *pwd, SECItem *salt, uint32_t iterations)
{
    SECAlgorithmID *algid = NULL;
    PK11SlotInfo *slot = NULL;
    PK11SymKey *symkey = NULL;
    SECItem *wrapKeyData = NULL;
    SECStatus rv = SECFailure;

    /* We assume that NSS is already started. */
    algid = PK11_CreatePBEV2AlgorithmID(SEC_OID_PKCS5_PBKDF2, SEC_OID_HMAC_SHA256, SEC_OID_HMAC_SHA256, hash_out_len, iterations, salt);

    if (algid != NULL) {
        /* Gets the best slot that provides SHA256HMAC and PBKDF2 (may not be the default!) */
        slot = PK11_GetBestSlotMultiple(mechanism_array, 2, NULL);
        if (slot != NULL) {
            symkey = PK11_PBEKeyGen(slot, algid, pwd, PR_FALSE, NULL);
            if (symkey == NULL) {
                /* We try to get the Error here but NSS has two or more error interfaces, and sometimes it uses none of them. */
                int32_t status = PORT_GetError();
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

    /*
     * First, we need to generate a wrapped key for PK11_Decrypt call:
     * slot is the same slot we used in PK11_PBEKeyGen()
     * 256 bits / 8 bit per byte
     */
    PK11SymKey *wrapKey = PK11_KeyGen(slot, CKM_AES_ECB, NULL, 256/8, NULL);
    PK11_FreeSlot(slot);
    if (wrapKey == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "pbkdf2_sha256_hash", "Unable to generate a wrapped key.\n");
        return SECFailure;
	}

    wrapKeyData = (SECItem *)PORT_Alloc(sizeof(SECItem));
    /* Align the wrapped key with 32 bytes. */
    wrapKeyData->len = (PK11_GetKeyLength(symkey) + 31) & ~31;
    /* Allocate the aligned space for pkc5PBE key plus AESKey block */
    wrapKeyData->data = (unsigned char *)slapi_ch_calloc(wrapKeyData->len, sizeof(unsigned char));

    /* Get symkey wrapped with wrapKey - required for PK11_Decrypt call */
    rv = PK11_WrapSymKey(CKM_AES_ECB, NULL, wrapKey, symkey, wrapKeyData);
    if (rv != SECSuccess) {
        PK11_FreeSymKey(symkey);
        PK11_FreeSymKey(wrapKey);
        SECITEM_FreeItem(wrapKeyData, PR_TRUE);
        slapi_log_err(SLAPI_LOG_ERR, "pbkdf2_sha256_hash", "Unable to wrap the symkey. (%d)\n", rv);
        return SECFailure;
    }

    /* Allocate the space for our result */
    void *result = (char *)slapi_ch_calloc(wrapKeyData->len, sizeof(char));
    unsigned int result_len = 0;

    /* User wrapKey to decrypt the wrapped contents.
     * result is the hash that we need;
     * result_len is the actual lengh of the data;
     * has_out_len is the maximum (the space we allocted for hash_out)
     */
    rv = PK11_Decrypt(wrapKey, CKM_AES_ECB, NULL, result, &result_len, hash_out_len, wrapKeyData->data, wrapKeyData->len);
    PK11_FreeSymKey(symkey);
    PK11_FreeSymKey(wrapKey);
    SECITEM_FreeItem(wrapKeyData, PR_TRUE);

    if (rv == SECSuccess) {
        if (result != NULL && result_len <= hash_out_len) {
            memcpy(hash_out, result, result_len);
            slapi_ch_free((void **)&result);
        } else {
            slapi_log_err(SLAPI_LOG_ERR, "pbkdf2_sha256_hash", "Unable to retrieve (get) hash output.\n");
            slapi_ch_free((void **)&result);
            return SECFailure;
        }
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "pbkdf2_sha256_hash", "Unable to extract hash output. (%d)\n", rv);
        slapi_ch_free((void **)&result);
        return SECFailure;
    }

    return SECSuccess;
}

char *
pbkdf2_sha256_pw_enc_rounds(const char *pwd, uint32_t iterations)
{
    char hash[PBKDF2_TOTAL_LENGTH];
    size_t encsize = 3 + schemeNameLength + LDIF_BASE64_LEN(PBKDF2_TOTAL_LENGTH);
    char *enc = slapi_ch_calloc(encsize, sizeof(char));

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
    if (pbkdf2_sha256_hash(hash + PBKDF2_ITERATIONS_LENGTH + PBKDF2_SALT_LENGTH, PBKDF2_HASH_LENGTH, &passItem, &saltItem, PBKDF2_ITERATIONS) != SECSuccess) {
        slapi_log_err(SLAPI_LOG_ERR, (char *)schemeName, "Could not generate pbkdf2_sha256_hash!\n");
        slapi_ch_free_string(&enc);
        return NULL;
    }

    sprintf(enc, "%c%s%c", PWD_HASH_PREFIX_START, schemeName, PWD_HASH_PREFIX_END);
    (void)PL_Base64Encode(hash, PBKDF2_TOTAL_LENGTH, enc + 2 + schemeNameLength);
    PR_ASSERT(enc[encsize - 1] == '\0');

    slapi_log_err(SLAPI_LOG_PLUGIN, (char *)schemeName, "Generated hash %s\n", enc);

    return enc;
}

char *
pbkdf2_sha256_pw_enc(const char *pwd)
{
    return pbkdf2_sha256_pw_enc_rounds(pwd, PBKDF2_ITERATIONS);
}

int32_t
pbkdf2_sha256_pw_cmp(const char *userpwd, const char *dbpwd)
{
    int32_t result = 1; /* Default to fail. */
    char dbhash[PBKDF2_TOTAL_LENGTH] = {0};
    char userhash[PBKDF2_HASH_LENGTH] = {0};
    int32_t dbpwd_len = strlen(dbpwd);
    SECItem saltItem;
    SECItem passItem;
    uint32_t iterations = 0;

    slapi_log_err(SLAPI_LOG_PLUGIN, (char *)schemeName, "Comparing password\n");

    passItem.data = (unsigned char *)userpwd;
    passItem.len = strlen(userpwd);

    /* Decode the DBpwd to bytes from b64 */
    if (PL_Base64Decode(dbpwd, dbpwd_len, dbhash) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, (char *)schemeName, "Unable to base64 decode dbpwd value\n");
        return result;
    }
    /* extract the fields */
    pbkdf2_sha256_extract(dbhash, &saltItem, &iterations);

    /* Now send the userpw to the hash function, with the salt + iter. */
    if (pbkdf2_sha256_hash(userhash, PBKDF2_HASH_LENGTH, &passItem, &saltItem, iterations) != SECSuccess) {
        slapi_log_err(SLAPI_LOG_ERR, (char *)schemeName, "Unable to hash userpwd value\n");
        return result;
    }

    /* Our hash value is always at a known offset in the decoded string. */
    char *hash = dbhash + PBKDF2_ITERATIONS_LENGTH + PBKDF2_SALT_LENGTH;

    /* Now compare the result of pbkdf2_sha256_hash. */
    result = memcmp(userhash, hash, PBKDF2_HASH_LENGTH);

    return result;
}

uint64_t
pbkdf2_sha256_benchmark_iterations()
{
    /* Time how long it takes to do PBKDF2_BENCH_LOOP attempts of PBKDF2_BENCH_ROUNDS rounds */
    uint64_t time_nsec = 0;
    char *results[PBKDF2_BENCH_LOOP] = {0};
    struct timespec start_time;
    struct timespec finish_time;

    clock_gettime(CLOCK_MONOTONIC, &start_time);

    for (size_t i = 0; i < PBKDF2_BENCH_LOOP; i++) {
        results[i] = pbkdf2_sha256_pw_enc_rounds("Eequee9mutheuchiehe4", PBKDF2_BENCH_ROUNDS);
    }

    clock_gettime(CLOCK_MONOTONIC, &finish_time);

    for (size_t i = 0; i < PBKDF2_BENCH_LOOP; i++) {
        slapi_ch_free((void **)&(results[i]));
    }

    /* Work out the execution time. */
    time_nsec = (finish_time.tv_sec - start_time.tv_sec) * 1000000000;
    if (finish_time.tv_nsec > start_time.tv_nsec) {
        time_nsec += finish_time.tv_nsec - start_time.tv_nsec;
    } else {
        time_nsec += 1000000000 - (start_time.tv_nsec - finish_time.tv_nsec);
    }

    time_nsec = time_nsec / PBKDF2_BENCH_LOOP;

    return time_nsec;
}

uint32_t
pbkdf2_sha256_calculate_iterations(uint64_t time_nsec)
{
    /*
     * So we know that we have nsec for a single round of PBKDF2_BENCH_ROUNDS now.
     * first, we get the cost of "every 1000 rounds"
     */
    uint64_t number_thou_rounds = PBKDF2_BENCH_ROUNDS / 1000;
    uint64_t thou_time_nsec = time_nsec / number_thou_rounds;

    /*
     * Now we have the cost of 1000 rounds. Now, knowing this we say
     * we want an attacker to have to expend say ... example 8 ms of work
     * to try a password. So this is 1,000,000 ns = 1ms, ergo
     * 8,000,000
     */
    uint64_t attack_work_nsec = PBKDF2_MILLISECONDS * 1000000;

    /*
     * Knowing the attacker time and our cost, we can divide this
     * to get how many thousands of rounds we should use.
     */
    uint64_t thou_rounds = (attack_work_nsec / thou_time_nsec);

    /*
     * Finally, we make the rounds in terms of thousands, and cast it.
     */
    uint32_t final_rounds = thou_rounds * 1000;

    if (final_rounds < PBKDF2_MINIMUM) {
        final_rounds = PBKDF2_MINIMUM;
    }

    return final_rounds;
}


int
pbkdf2_sha256_start(Slapi_PBlock *pb __attribute__((unused)))
{
    /* Run the time generator */
    uint64_t time_nsec = pbkdf2_sha256_benchmark_iterations();
    /* Calculate the iterations */
    /* set it globally */
    PBKDF2_ITERATIONS = pbkdf2_sha256_calculate_iterations(time_nsec);
    /* Make a note of it. */
    slapi_log_err(SLAPI_LOG_INFO, (char *)schemeName, "Based on CPU performance, chose %" PRIu32 " rounds\n", PBKDF2_ITERATIONS);
    return 0;
}

/* Do we need the matching close function? */
int
pbkdf2_sha256_close(Slapi_PBlock *pb __attribute__((unused)))
{
    return 0;
}

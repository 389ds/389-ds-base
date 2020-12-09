/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2015 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdio.h>
#include <ldap.h>
#include <nss.h>
#include <svrcore.h>
#define NEED_TOK_PBE /* see slap.h - defines tokPBE and ptokPBE */
#include "rever.h"

struct pk11MechItem
{
    CK_MECHANISM_TYPE type;
    const char *mechName;
};

static const struct pk11MechItem DESmech = {CKM_DES_CBC, "DES CBC encryption"};
static const struct pk11MechItem AESmech = {CKM_AES_CBC, "AES CBC encryption"};
static Slapi_Mutex *pbe_lock = NULL;

struct pk11ContextStore
{
    PK11SlotInfo *slot;
    const struct pk11MechItem *mech;
    PK11SymKey *key;
    SECItem *params;
    int length;
    unsigned char *crypt;
    char *algid_base64;
};

/*
 *  der_algid converting functions:
 *
 *  SECStatus ATOB_ConvertAsciiToItem(SECItem *binary_item, const char *ascii);
 *  char *    BTOA_ConvertItemToAscii(SECItem *binary_item);
 *
 */

static int encode_path(char *inPlain, char **outCipher, char *path, int mech);
static int decode_path(char *inCipher, char **outPlain, char *path, int mech, char *algid);
static SVRCOREError genKey(struct pk11ContextStore **out, char *path, int mech, PRArenaPool *arena, char *algid);
static SVRCOREError cryptPassword(struct pk11ContextStore *store, char *clear, unsigned char **out);
static SVRCOREError decryptPassword(struct pk11ContextStore *store, unsigned char *cipher, char **out, int len);
static void freePBE(struct pk11ContextStore *store);

void
init_pbe_plugin()
{
    if (!pbe_lock) {
        pbe_lock = slapi_new_mutex();
    }
}

int
encode(char *inPlain, char **outCipher, int mech)
{
    return encode_path(inPlain, outCipher, NULL, mech);
}

static int
encode_path(char *inPlain, char **outCipher, char *path, int mech)
{
    struct pk11ContextStore *context = NULL;
    PRArenaPool *arena = NULL;
    unsigned char *cipher = NULL;
    char *tmp = NULL;
    char *base = NULL;
    int len = 0;
    int err;

    *outCipher = NULL;
    err = 1;

    if (genKey(&context, path, mech, arena, NULL) == SVRCORE_Success) {
        /* Try an encryption */
        if (cryptPassword(context, inPlain, &cipher) == SVRCORE_Success) {
            base = BTOA_DataToAscii(cipher, context->length);
            if (base != NULL) {
                const char *scheme;
                if (mech == AES_MECH) {
                    scheme = AES_REVER_SCHEME_NAME;
                    len = 3 + strlen(scheme) + strlen(context->algid_base64) + strlen(base) + 1;
                    if ((tmp = slapi_ch_malloc(len))) {
                        /*
                         * {AES-<BASE64_ALG_ID>}<ENCODED PASSWORD>
                         */
                        sprintf(tmp, "%c%s-%s%c%s", PWD_HASH_PREFIX_START, scheme,
                                context->algid_base64, PWD_HASH_PREFIX_END, base);
                    }
                } else {
                    /* Old school DES */
                    scheme = DES_REVER_SCHEME_NAME;
                    if ((tmp = slapi_ch_malloc(3 + strlen(scheme) + strlen(base)))) {
                        sprintf(tmp, "%c%s%c%s", PWD_HASH_PREFIX_START, scheme,
                                PWD_HASH_PREFIX_END, base);
                    }
                }
                if (tmp != NULL) {
                    *outCipher = tmp;
                    tmp = NULL;
                    err = 0;
                }
                PORT_Free(base);
            }
        }
    }
    freePBE(context);

    return (err);
}

int
decode(char *inCipher, char **outPlain, int mech, char *algid)
{
    return decode_path(inCipher, outPlain, NULL, mech, algid);
}


static int
decode_path(char *inCipher, char **outPlain, char *path, int mech, char *algid)
{
    struct pk11ContextStore *context = NULL;
    PRArenaPool *arena = NULL;
    unsigned char *base = NULL;
    char *plain = NULL;
    int err;
    int len = 0;

    *outPlain = NULL;
    err = 1;

    if (mech == DES_MECH) {
        slapi_log_err(SLAPI_LOG_NOTICE, "decode_path",
                "Decoding DES reversible password, DES is no longer a supported algorithm, "
                "please use AES reversible password encryption.\n");

    }

    if (genKey(&context, path, mech, arena, algid) == SVRCORE_Success) {
        /* it seems that there is memory leak in that function: bug 400170 */
        base = ATOB_AsciiToData(inCipher, (unsigned int *)&len);
        if (base != NULL) {
            if (decryptPassword(context, base, &plain, len) == SVRCORE_Success) {
                *outPlain = plain;
                err = 0;
            }
        }
    }

    slapi_ch_free_string(&algid);
    PORT_Free(base);
    PORT_FreeArena(arena, PR_TRUE);
    freePBE(context);

    return (err);
}

static void
freePBE(struct pk11ContextStore *store)
{
    if (store) {
        if (store->slot)
            slapd_pk11_freeSlot(store->slot);
        if (store->key)
            slapd_pk11_freeSymKey(store->key);
        if (store->params)
            SECITEM_FreeItem(store->params, PR_TRUE);
        slapi_ch_free((void **)&store->crypt);
        slapi_ch_free_string(&store->algid_base64);
        slapi_ch_free((void **)&store);
    }
}

static SVRCOREError
genKey(struct pk11ContextStore **out, char *path, int mech, PRArenaPool *arena, char *alg)
{
    SVRCOREError err = SVRCORE_Success;
    struct pk11ContextStore *store = NULL;
    SECItem *pwitem = NULL;
    SECItem *result = NULL;
    SECItem *salt = NULL;
    SECItem der_algid = {0};
    SECAlgorithmID *algid = NULL;
    SECOidTag algoid;
    CK_MECHANISM pbeMech;
    CK_MECHANISM cryptoMech;
    /* Have to use long form init due to internal structs */
    SECAlgorithmID my_algid = {{0}, {0}};
    char *configdir = NULL;
    char *der_ascii = NULL;
    char *iv = NULL;
    int free_it = 0;

    arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);

    store = (struct pk11ContextStore *)slapi_ch_calloc(1, sizeof(*store));
    if (store == NULL) {
        err = SVRCORE_NoMemory_Error;
        goto done;
    }
    *out = store;

    store->slot = slapd_pk11_getInternalKeySlot();
    if (store->slot == NULL) {
        err = SVRCORE_NoSuchToken_Error;
        goto done;
    }

    /* Generate a key and parameters to do the encryption */
    if (mech == AES_MECH) {
        store->mech = &AESmech;
        algoid = SEC_OID_AES_256_CBC;
    } else {
        store->mech = &DESmech;
        algoid = SEC_OID_PKCS5_PBE_WITH_MD2_AND_DES_CBC;
    }

    /* Generate a unique id, used as salt for the key generation */
    if (path == NULL) {
        configdir = config_get_configdir();
        if (configdir == NULL) {
            err = SVRCORE_System_Error;
            goto done;
        }
    } else {
        configdir = slapi_ch_strdup(path);
    }
    if (slapi_uniqueIDGenerateFromNameString(&iv, NULL, configdir, strlen(configdir)) != UID_SUCCESS) {
        err = SVRCORE_System_Error;
        goto done;
    }

    pwitem = (SECItem *)PORT_Alloc(sizeof(SECItem));
    if (pwitem == NULL) {
        err = SVRCORE_NoMemory_Error;
        goto done;
    }
    pwitem->type = siBuffer;
    pwitem->data = (unsigned char *)PORT_Alloc(strlen(iv) + 1);
    if (pwitem->data == NULL) {
        err = SVRCORE_NoMemory_Error;
        goto done;
    }
    strcpy((char *)pwitem->data, iv);
    pwitem->len = strlen(iv) + 1;

    salt = (SECItem *)PORT_Alloc(sizeof(SECItem));
    if (salt == NULL) {
        err = SVRCORE_NoMemory_Error;
        goto done;
    }
    salt->type = siBuffer;
    salt->data = (unsigned char *)PORT_Alloc(strlen(iv) + 1);
    if (salt->data == NULL) {
        err = SVRCORE_NoMemory_Error;
        goto done;
    }
    strcpy((char *)salt->data, iv);
    salt->len = strlen(iv) + 1;

    if (!alg) {
        /*
         * This is DES, or we are encoding AES - the process is the same.
         */
        algid = slapd_pk11_createPBEAlgorithmID(algoid, 2, salt);
        free_it = 1; /* we need to free this algid */

        /*
         * The following is only need for AES - we need to store
         * algid for future decodings(unlike with DES).  So convert
         * algid to its DER encoding.  Then convert the DER to ascii,
         * and finally convert the DER ascii to base64 so we can store
         * it in the cipher prefix.
         */
        SEC_ASN1EncodeItem(arena, &der_algid, algid, SEC_ASN1_GET(SECOID_AlgorithmIDTemplate));
        der_ascii = BTOA_ConvertItemToAscii(&der_algid);
        store->algid_base64 = PL_Base64Encode(der_ascii, strlen(der_ascii), NULL);
        slapi_ch_free_string(&der_ascii);
    } else {
        /*
         * We are decoding AES - use the supplied algid
         */

        /* Decode the base64 der encoding */
        der_ascii = PL_Base64Decode(alg, strlen(alg), NULL);

        /* convert the der ascii to the SEC item */
        ATOB_ConvertAsciiToItem(&der_algid, der_ascii);
        SEC_ASN1DecodeItem(arena, &my_algid, SEC_ASN1_GET(SECOID_AlgorithmIDTemplate), &der_algid);
        SECITEM_FreeItem(&der_algid, PR_FALSE);
        algid = &my_algid;
        slapi_ch_free_string(&der_ascii);
    }

    slapi_lock_mutex(pbe_lock);
    store->key = slapd_pk11_pbeKeyGen(store->slot, algid, pwitem, 0, 0);
    if (store->key == 0) {
        slapi_unlock_mutex(pbe_lock);
        err = SVRCORE_System_Error;
        goto done;
    }

    slapi_unlock_mutex(pbe_lock);

    if (mech == AES_MECH) {
        cryptoMech.mechanism = PK11_GetPBECryptoMechanism(algid, &store->params, pwitem);
        if (cryptoMech.mechanism == CKM_INVALID_MECHANISM) {
            err = SVRCORE_System_Error;
            goto done;
        }
    } else {
        /* DES */
        pbeMech.mechanism = slapd_pk11_algtagToMechanism(algoid);
        result = slapd_pk11_paramFromAlgid(algid);
        if (result) {
            pbeMech.pParameter = result->data;
            pbeMech.ulParameterLen = result->len;
        }
        if (slapd_pk11_mapPBEMechanismToCryptoMechanism(&pbeMech, &cryptoMech, pwitem,
                                                        PR_FALSE) != CKR_OK) {
            err = SVRCORE_System_Error;
            goto done;
        }

        store->params = (SECItem *)PORT_Alloc(sizeof(SECItem));
        if (store->params == NULL) {
            err = SVRCORE_System_Error;
            goto done;
        }
        store->params->type = store->mech->type;
        store->params->data = (unsigned char *)PORT_Alloc(cryptoMech.ulParameterLen);
        if (store->params->data == NULL) {
            err = SVRCORE_System_Error;
            goto done;
        }
        memcpy(store->params->data, (unsigned char *)cryptoMech.pParameter, cryptoMech.ulParameterLen);
        store->params->len = cryptoMech.ulParameterLen;
        PORT_Free(cryptoMech.pParameter);
    }

done:
    SECITEM_FreeItem(result, PR_TRUE);
    SECITEM_FreeItem(pwitem, PR_TRUE);
    SECITEM_FreeItem(salt, PR_TRUE);
    if (free_it) {
        secoid_destroyAlgorithmID(algid, PR_TRUE);
    }
    slapi_ch_free_string(&configdir);
    slapi_ch_free_string(&iv);
    if (arena) {
        PORT_FreeArena(arena, PR_TRUE);
    }
    return (err);
}

static SVRCOREError
decryptPassword(struct pk11ContextStore *store, unsigned char *cipher, char **out, int len)
{
    SVRCOREError err = SVRCORE_Success;
    unsigned char *plain = NULL;
    unsigned char *cipher_with_padding = NULL;
    SECStatus rv;
    PK11Context *ctx = NULL;
    int outLen = 0;
    int blocksize = 0;

    blocksize = slapd_pk11_getBlockSize(store->mech->type, 0);
    store->length = len;

    /*
     * store->length is the max. length of the returned clear text -
     * must be >= length of crypted bytes - also must be a multiple
     * of blocksize
     */
    if (blocksize != 0) {
        store->length += blocksize - (store->length % blocksize);
    }

    /* plain will hold the returned clear text */
    plain = (unsigned char *)slapi_ch_calloc(sizeof(unsigned char),
                                             store->length + 1);
    if (!plain) {
        err = SVRCORE_NoMemory_Error;
        goto done;
    }

    /*
     * create a buffer holding the original cipher bytes, padded with
     * zeros to a multiple of blocksize - do not need +1 since buffer is not
     * a string
     */
    cipher_with_padding = (unsigned char *)slapi_ch_calloc(sizeof(unsigned char),
                                                           store->length);
    if (!cipher_with_padding) {
        err = SVRCORE_NoMemory_Error;
        goto done;
    }
    memcpy(cipher_with_padding, cipher, len);

    ctx = slapd_pk11_createContextBySymKey(store->mech->type, CKA_DECRYPT,
                                           store->key, store->params);
    if (!ctx) {
        err = SVRCORE_System_Error;
        goto done;
    }

    /*
     * Warning - there is a purify UMR in the NSS des code - you may see it when the
     * password is not a multiple of 8 bytes long
     */
    rv = slapd_pk11_cipherOp(ctx, plain, &outLen, store->length,
                             cipher_with_padding, store->length);
    if (rv) {
        err = SVRCORE_System_Error;
    }

    rv = slapd_pk11_finalize(ctx);
    /*
     * We must do the finalize, but we only want to set the err return
     * code if it is not already set
     */
    if (rv && (SVRCORE_Success == err))
        err = SVRCORE_System_Error;

done:
    if (err == SVRCORE_Success) {
        *out = (char *)plain;
    } else {
        slapi_ch_free((void **)&plain);
    }

    slapi_ch_free((void **)&cipher_with_padding);
    /* We should free the PK11Context... Something like : */
    if (ctx) {
        slapd_pk11_destroyContext(ctx, PR_TRUE);
    }

    return err;
}

static SVRCOREError
cryptPassword(struct pk11ContextStore *store, char *clear, unsigned char **out)
{
    SVRCOREError err = SVRCORE_Success;
    SECStatus rv;
    PK11Context *ctx = NULL;
    unsigned char *clear_with_padding = NULL; /* clear with padding up to blocksize */
    int blocksize = 0;
    int outLen = 0;

    blocksize = slapd_pk11_getBlockSize(store->mech->type, 0);
    store->length = strlen(clear);

    /*
     * The size of the clear text buffer passed to the encryption functions
     * must be a multiple of blocksize (usually 8 bytes) - we allocate a buffer
     * of this size, copy the clear text password into it, and pad the rest with
     * zeros.
     */
    if (blocksize != 0) {
        store->length += blocksize - (store->length % blocksize);
    }

    /*
     * store->crypt will hold the crypted password - it must be >= clear length
     * store->crypt is freed in NSS; let's not use slapi_ch_calloc
     */
    store->crypt = (unsigned char *)calloc(sizeof(unsigned char),
                                           store->length + 1);
    if (!store->crypt) {
        err = SVRCORE_NoMemory_Error;
        goto done;
    }

    /* Create a buffer big enough to hold the clear text password and padding */
    clear_with_padding = (unsigned char *)slapi_ch_calloc(sizeof(unsigned char),
                                                          store->length + 1);
    if (!clear_with_padding) {
        err = SVRCORE_NoMemory_Error;
        goto done;
    }
    /*
     * Copy the clear text password into the buffer - the calloc insures the
     * remainder is zero padded .
     */
    strcpy((char *)clear_with_padding, clear);

    ctx = slapd_pk11_createContextBySymKey(store->mech->type, CKA_ENCRYPT,
                                           store->key, store->params);
    if (!ctx) {
        err = SVRCORE_System_Error;
        goto done;
    }

    rv = slapd_pk11_cipherOp(ctx, store->crypt, &outLen, store->length,
                             clear_with_padding, store->length);
    if (rv) {
        err = SVRCORE_System_Error;
    }

    rv = slapd_pk11_finalize(ctx);
    /*
     * We must do the finalize, but we only want to set the err return
     * code if it is not already set
     */
    if (rv && (SVRCORE_Success == err)) {
        err = SVRCORE_System_Error;
    }

done:
    if (err == SVRCORE_Success) {
        *out = store->crypt;
    }

    slapi_ch_free((void **)&clear_with_padding);
    /* We should free the PK11Context... Something like : */
    if (ctx) {
        slapd_pk11_destroyContext(ctx, PR_TRUE);
    }

    return err;
}

/*
 * The UUID name based generator was broken on x86 platforms.  We use
 * this to generate the password encryption key.  During migration,
 * we have to fix this so we can use the fixed generator.  The env.
 * var USE_BROKEN_UUID tells the uuid generator to use the old
 * broken method to create the UUID.  That will allow us to decrypt
 * the password to the correct clear text, then we can turn off
 * the broken method and use the fixed method to encrypt the
 * password.
 */
char *
migrateCredentials(char *oldpath, char *newpath, char *oldcred)
{
    static char *useBrokenUUID = "USE_BROKEN_UUID=1";
    static char *disableBrokenUUID = "USE_BROKEN_UUID=0";
    char *plain = NULL;
    char *cipher = NULL;

    init_pbe_plugin();

    slapd_pk11_configurePKCS11(NULL, NULL, tokPBE, ptokPBE, NULL, NULL, NULL, NULL, 0, 0);
    NSS_NoDB_Init(NULL);

    if (getenv("MIGRATE_BROKEN_PWD")) {
        putenv(useBrokenUUID);
    }

    if (decode_path(oldcred, &plain, oldpath, DES_MECH, NULL) == 0) {
        if (getenv("MIGRATE_BROKEN_PWD")) {
            putenv(disableBrokenUUID);
        }
        if (encode_path(plain, &cipher, newpath, AES_MECH) != 0) {
            return (NULL);
        } else {
            return (cipher);
        }
    } else {
        return (NULL);
    }
}

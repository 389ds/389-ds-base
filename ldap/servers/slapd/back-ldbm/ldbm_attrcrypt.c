/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2010 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* This file handles attribute encryption.  */
/* #define DEBUG_ATTRCRYPT 1 */

#include "back-ldbm.h"
#include "attrcrypt.h"
#include "pk11func.h"
#include "keyhi.h"
#include "nss.h"

/*
 * Todo:
 * Remember to free the private structures in the attrinfos, so avoid a leak.
 */

#define ATTRCRYPT "attrcrypt"

attrcrypt_cipher_entry attrcrypt_cipher_list[] =
    {
        {ATTRCRYPT_CIPHER_AES, "AES", CKM_AES_CBC_PAD, CKM_AES_CBC_PAD, CKM_AES_CBC_PAD, 128 / 8, 16},
        {ATTRCRYPT_CIPHER_DES3, "3DES", CKM_DES3_CBC_PAD, CKM_DES3_CBC_PAD, CKM_DES3_CBC_PAD, 112 / 8, 8},
        {0}};

#define KEY_ATTRIBUTE_NAME "nsSymmetricKey"

/*
 * We maintain one of these structures per cipher that we handle
 */

typedef struct _attrcrypt_cipher_state
{
    char *cipher_display_name;
    PRLock *cipher_lock;
    PK11SlotInfo *slot;
    PK11SymKey *key;
    attrcrypt_cipher_entry *ace;
} attrcrypt_cipher_state;

struct _attrcrypt_state_private
{
    attrcrypt_cipher_state *acs_array[1];
};

/*
 * Return
 */
enum
{
    KEYMGMT_SUCCESS = 0,
    KEYMGMT_ERR_NO_ENTRY,     /* Entry to store key does not exist */
    KEYMGMT_ERR_NO_KEY_ATTR,  /* Entry has no key attribute */
    KEYMGMT_ERR_NO_KEY_VALUE, /* Empty key */
    KEYMGMT_ERR_CANT_UNWRAP,  /* Key failed to unwrap */
    KEYMGMT_ERR_OTHER         /* Other error */
};

static int attrcrypt_wrap_key(attrcrypt_cipher_state *acs, PK11SymKey *symmetric_key, SECKEYPublicKey *public_key, SECItem *wrapped_symmetric_key);
static int attrcrypt_unwrap_key(attrcrypt_cipher_state *acs, SECKEYPrivateKey *private_key, SECItem *wrapped_symmetric_key, PK11SymKey **unwrapped_symmetric_key);
static int _back_crypt_cleanup_private(attrcrypt_state_private **state_priv);
static void _back_crypt_acs_list_add(attrcrypt_state_private **state_priv, attrcrypt_cipher_state *acs);
static int _back_crypt_keymgmt_get_key(attrcrypt_cipher_state *acs, SECKEYPrivateKey *private_key, PK11SymKey **key_from_store, const char *dn_string);
static int _back_crypt_crypto_op(attrcrypt_private *priv, attrcrypt_cipher_state *acs, char *in_data, size_t in_size, char **out_data, size_t *out_size, int encrypt, backend *be, struct attrinfo *ai /* just for debugging */);

/*
 * Copied from front-end because it's private to plugins
 */

static int
local_valuearray_count(Slapi_Value **va)
{
    int i = 0;
    if (va != NULL) {
        while (NULL != va[i])
            i++;
    }
    return (i);
}

/*
 * Helper functions for key management
 */

static Slapi_Entry *
getConfigEntry(const char *dn, Slapi_Entry **e2)
{
    Slapi_DN sdn;

    slapi_sdn_init_dn_byref(&sdn, dn);
    slapi_search_internal_get_entry(&sdn, NULL, e2,
                                    plugin_get_default_component_id());
    slapi_sdn_done(&sdn);
    return *e2;
}

/**
 * Free an entry
 */
static void
freeConfigEntry(Slapi_Entry **e)
{
    if ((e != NULL) && (*e != NULL)) {
        slapi_entry_free(*e);
        *e = NULL;
    }
}

static int
attrcrypt_get_ssl_cert_name(char **cert_name)
{
    char *config_entry_dn = "cn=RSA,cn=encryption,cn=config";
    Slapi_Entry *config_entry = NULL;
    const char *personality = NULL;
    const char *token = NULL;

    *cert_name = NULL;
    getConfigEntry(config_entry_dn, &config_entry);
    if (NULL == config_entry) {
        return -1;
    }
    token = slapi_entry_attr_get_ref(config_entry, "nsssltoken");
    personality = slapi_entry_attr_get_ref(config_entry, "nssslpersonalityssl");
    if (token && personality) {
        if (!strcasecmp(token, "internal") ||
            !strcasecmp(token, "internal (software)")) {
            *cert_name = slapi_ch_strdup(personality);
        } else {
            /* external PKCS #11 token - attach token name */
            *cert_name = slapi_ch_smprintf("%s:%s", token, personality);
        }
    }
    freeConfigEntry(&config_entry);
    return 0;
}

/* Retrieve a symmetric key from dse.ldif for a specified cipher */
static int
attrcrypt_keymgmt_get_key(ldbm_instance *li, attrcrypt_cipher_state *acs, SECKEYPrivateKey *private_key, PK11SymKey **key_from_store)
{
    int ret = 0;
    char *dn_template = "cn=%s,cn=encrypted attribute keys,cn=%s,cn=%s,cn=plugins,cn=config";
    char *instance_name = li->inst_name;
    char *dn_string = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_keymgmt_get_key", "->\n");
    dn_string = slapi_create_dn_string(dn_template,
                                       acs->ace->cipher_display_name, instance_name,
                                       li->inst_li->li_plugin->plg_name);
    if (NULL == dn_string) {
        slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_keymgmt_get_key",
                      "Failed create attrcrypt key dn for plugin %s, "
                      "instance %s, cypher %s\n",
                      li->inst_li->li_plugin->plg_name,
                      li->inst_name, acs->ace->cipher_display_name);
        ret = -1;
        goto bail;
    }
    ret = _back_crypt_keymgmt_get_key(acs, private_key, key_from_store,
                                      (const char *)dn_string);
bail:
    slapi_ch_free_string(&dn_string);
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_keymgmt_get_key", "<-\n");
    return ret;
}

/* Store a symmetric key for a given cipher in dse.ldif */
static int
attrcrypt_keymgmt_store_key(ldbm_instance *li, attrcrypt_cipher_state *acs, SECKEYPublicKey *public_key, PK11SymKey *key_to_store)
{
    int ret = 0;
    SECItem wrapped_symmetric_key = {0};
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_keymgmt_store_key", "->\n");
    /* Wrap the key and then store it in the right place in dse.ldif */
    ret = attrcrypt_wrap_key(acs, key_to_store, public_key, &wrapped_symmetric_key);
    if (!ret) {
        /* Make the entry to store */
        Slapi_Entry *e = NULL;
        Slapi_PBlock *pb = slapi_pblock_new();
        Slapi_Value *key_value = NULL;
        struct berval key_as_berval = {0};
        int rc = 0;
        char *entry_template =
            "dn: cn=%s,cn=encrypted attribute keys,cn=%s,cn=ldbm database,cn=plugins,cn=config\n"
            "objectclass:top\n"
            "objectclass:extensibleObject\n"
            "cn:%s\n";
        char *instance_name = li->inst_name;
        char *entry_string = slapi_ch_smprintf(entry_template, acs->ace->cipher_display_name, instance_name, acs->ace->cipher_display_name);
        e = slapi_str2entry(entry_string, 0);
        /* Add the key as a binary attribute */
        key_as_berval.bv_val = (char *)wrapped_symmetric_key.data;
        key_as_berval.bv_len = wrapped_symmetric_key.len;
        key_value = slapi_value_new_berval(&key_as_berval);
        /* key_value is now a copy of key_as_berval - free wrapped_symmetric_key */
        slapi_ch_free_string((char **)&wrapped_symmetric_key.data);
        slapi_entry_add_value(e, KEY_ATTRIBUTE_NAME, key_value);
        slapi_value_free(&key_value);
        /* Store the entry */
        slapi_add_entry_internal_set_pb(pb, e, NULL, li->inst_li->li_identity, 0);
        rc = slapi_add_internal_pb(pb);
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
        if (rc != LDAP_SUCCESS) {
            char *resulttext = NULL;
            slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &resulttext);
            slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_keymgmt_store_key",
                          "Failed to add config key entries to the DSE: %d: %s: %s\n",
                          rc, ldap_err2string(rc), resulttext ? resulttext : "unknown");
            ret = -1;
        }
        slapi_ch_free((void **)&entry_string);
        slapi_pblock_destroy(pb);
    }
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_keymgmt_store_key", "<-\n");
    return ret;
}

/*
 * Helper functions for key generation and wrapping
 */

/* Wrap a key with the server's public assymetric key for storage */
static int
attrcrypt_wrap_key(attrcrypt_cipher_state *acs, PK11SymKey *symmetric_key, SECKEYPublicKey *public_key, SECItem *wrapped_symmetric_key)
{
    int ret = 0;
    SECStatus s = 0;
    CK_MECHANISM_TYPE wrap_mechanism = CKM_RSA_PKCS;
    SECKEYPublicKey *wrapping_key = public_key;
    wrapped_symmetric_key->len = slapd_SECKEY_PublicKeyStrength(public_key);
    wrapped_symmetric_key->data = (unsigned char *)slapi_ch_malloc(wrapped_symmetric_key->len);
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_wrap_key", "->\n");
    s = slapd_pk11_PubWrapSymKey(wrap_mechanism, wrapping_key, symmetric_key, wrapped_symmetric_key);
    if (SECSuccess != s) {
        ret = -1;
        slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_wrap_key",
                      "Failed to wrap key for cipher %s\n",
                      acs->ace->cipher_display_name);
    }
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_wrap_key", "<-\n");
    return ret;
}

/* Unwrap a key previously wrapped with the server's private key */
static int
attrcrypt_unwrap_key(attrcrypt_cipher_state *acs, SECKEYPrivateKey *private_key, SECItem *wrapped_symmetric_key, PK11SymKey **unwrapped_symmetric_key)
{
    int ret = 0;
    CK_MECHANISM_TYPE wrap_mechanism = acs->ace->wrap_mechanism;
    SECKEYPrivateKey *unwrapping_key = private_key;

    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_unwrap_key", "->\n");
    /*
     * NOTE: we are unwrapping one symmetric key with attribute both ENCRYPT
     *       and DECRYPT set.  Some hardware token might have a problem with
     *       it.  In such case, we need to generate 2 identiday keys, one for
     *       encryption  and another for decription.  When unwrapping them,
     *       set attribute ENCRYPT for the encryption key and DECRYPT for
     *       the decryption key.
     */
    *unwrapped_symmetric_key = slapd_pk11_PubUnwrapSymKeyWithFlagsPerm(
        unwrapping_key,
        wrapped_symmetric_key,
        wrap_mechanism,
        CKA_DECRYPT, 0,
        CKF_ENCRYPT,
        PR_FALSE);
    if (NULL == *unwrapped_symmetric_key) {
        ret = -1;
        slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_unwrap_key",
                      "Failed to unwrap key for cipher %s\n",
                      acs->ace->cipher_display_name);
    }
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_unwrap_key", "<-\n");
    return ret;
}

/* Generate a random key for a specified cipher */
static int
attrcrypt_generate_key(attrcrypt_cipher_state *acs, PK11SymKey **symmetric_key)
{
    int ret = 1;
    PK11SymKey *new_symmetric_key = NULL;
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_generate_key", "->\n");
    if (NULL == symmetric_key) {
        slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_generate_key", "NULL symmetric_key\n");
        goto bail;
    }
    *symmetric_key = NULL;

    if (!slapd_pk11_DoesMechanism(acs->slot, acs->ace->cipher_mechanism)) {
        slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_generate_key", "%s is not supported.\n",
                      acs->ace->cipher_display_name);
        ret = -1;
        goto bail;
    }

    /*
     * NOTE: we are generating one symmetric key with attribute both ENCRYPT
     *       and DECRYPT set.  Some hardware token might have a problem with
     *       it.  In such case, we need to generate 2 identiday keys, one for
     *       encryption (with an attribute ENCRYPT set) and another for
     *       decription (DECRYPT set).
     */
    new_symmetric_key = slapd_pk11_TokenKeyGenWithFlags(acs->slot,
                                                        acs->ace->key_gen_mechanism,
                                                        0 /*param*/,
                                                        acs->ace->key_size,
                                                        NULL /*keyid*/,
                                                        CKF_DECRYPT /*op*/,
                                                        CKF_ENCRYPT /*attr*/,
                                                        NULL);
    if (new_symmetric_key) {
        *symmetric_key = new_symmetric_key;
        ret = 0;
    }
bail:
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_generate_key", "<- (%d)\n", ret);
    return ret;
}

static int
attrcrypt_fetch_public_key(SECKEYPublicKey **public_key)
{
    int ret = 0;
    CERTCertificate *cert = NULL;
    SECKEYPublicKey *key = NULL;
    PRErrorCode errorCode = 0;
    char *default_cert_name = "server-cert";
    char *cert_name = NULL;
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_fetch_public_key", "->\n");
    *public_key = NULL;
    /* Try to grok the server cert name from the SSL config */
    ret = attrcrypt_get_ssl_cert_name(&cert_name);
    if (ret) {
        cert_name = default_cert_name;
    }
    /* We assume that the server core pin stuff is already enabled, via the SSL initialization done in the front-end */
    cert = slapd_pk11_findCertFromNickname(cert_name, NULL);
    if (cert == NULL) {
        errorCode = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_fetch_public_key", "Can't find certificate %s: %d - %s\n",
                      cert_name, errorCode, slapd_pr_strerror(errorCode));
        if (PR_FILE_NOT_FOUND_ERROR == errorCode) {
            slapd_cert_not_found_error_help(cert_name);
        }
    }
    if (cert != NULL) {
        key = slapd_CERT_ExtractPublicKey(cert);
    }
    if (key == NULL) {
        errorCode = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_fetch_public_key", "Can't get private key from cert %s: %d - %s\n",
                      cert_name, errorCode, slapd_pr_strerror(errorCode));
        ret = -1;
    }
    if (cert) {
        slapd_pk11_CERT_DestroyCertificate(cert);
    }
    if (key) {
        *public_key = key;
    } else {
        ret = -1;
    }
    if (cert_name != default_cert_name) {
        slapi_ch_free_string(&cert_name);
    }
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_fetch_public_key", "<-\n");
    return ret;
}

static int
attrcrypt_fetch_private_key(SECKEYPrivateKey **private_key)
{
    int ret = 0;
    CERTCertificate *cert = NULL;
    SECKEYPrivateKey *key = NULL;
    PRErrorCode errorCode = 0;
    char *default_cert_name = "server-cert";
    char *cert_name = NULL;
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_fetch_private_key", "->\n");
    *private_key = NULL;
    /* Try to grok the server cert name from the SSL config */
    ret = attrcrypt_get_ssl_cert_name(&cert_name);
    if (ret) {
        cert_name = default_cert_name;
    }
    /* We assume that the server core pin stuff is already enabled, via the SSL initialization done in the front-end */
    cert = slapd_pk11_findCertFromNickname(cert_name, NULL);
    if (cert == NULL) {
        errorCode = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_fetch_private_key", "Can't find certificate %s: %d - %s\n",
                      cert_name, errorCode, slapd_pr_strerror(errorCode));
        if (PR_FILE_NOT_FOUND_ERROR == errorCode) {
            slapd_cert_not_found_error_help(cert_name);
        }
    }
    if (cert != NULL) {
        key = slapd_get_unlocked_key_for_cert(cert, NULL);
    }
    if (key == NULL) {
        errorCode = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_fetch_private_key", "Can't get private key from cert %s: %d - %s\n",
                      cert_name, errorCode, slapd_pr_strerror(errorCode));
        ret = -1;
    }
    if (cert) {
        slapd_pk11_CERT_DestroyCertificate(cert);
    }
    if (key) {
        *private_key = key;
    } else {
        ret = -1;
    }
    if (cert_name != default_cert_name) {
        slapi_ch_free_string(&cert_name);
    }
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_fetch_private_key", "-> (%d)\n", ret);
    return ret;
}

/*
 CKM_AES_CBC_PAD
 CKM_DES3_CBC_PAD
 */

/* Initialize the structure for a single cipher */
static int
attrcrypt_cipher_init(ldbm_instance *li, attrcrypt_cipher_entry *ace, SECKEYPrivateKey *private_key, SECKEYPublicKey *public_key, attrcrypt_cipher_state *acs)
{
    int ret = 0;
    PK11SymKey *symmetric_key = NULL;
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_cipher_init", "->\n");
    acs->cipher_lock = PR_NewLock();
    /* Fill in some basic stuff */
    acs->ace = ace;
    acs->cipher_display_name = ace->cipher_display_name;
    if (NULL == acs->cipher_lock) {
        slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_cipher_init",
                      "Failed to create cipher lock\n");
    }
    acs->slot = slapd_pk11_GetInternalKeySlot();
    if (NULL == acs->slot) {
        slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_cipher_init",
                      "Failed to create a slot for cipher %s\n",
                      acs->cipher_display_name);
        goto error;
    }
    /* Try to get the symmetric key for this cipher */
    ret = attrcrypt_keymgmt_get_key(li, acs, private_key, &symmetric_key);
    if (KEYMGMT_ERR_NO_ENTRY == ret) {
        slapi_log_err(SLAPI_LOG_NOTICE, "attrcrypt_cipher_init",
                      "No symmetric key found for cipher %s in backend %s, "
                      "attempting to create one...\n",
                      acs->cipher_display_name, li->inst_name);
        ret = attrcrypt_generate_key(acs, &symmetric_key);
        if (ret) {
            slapi_log_err(SLAPI_LOG_WARNING, "attrcrypt_cipher_init",
                          "Failed to generate key for %s in attrcrypt_cipher_init\n",
                          acs->cipher_display_name);
            if ((ret < 0) && li->attrcrypt_configured) {
                slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_cipher_init",
                              "Cipher %s is not supported on the security device. "
                              "Do not configure attrcrypt with the cipher.\n",
                              ace->cipher_display_name);
            }
        }
        if (symmetric_key) {
            ret = attrcrypt_keymgmt_store_key(li, acs, public_key, symmetric_key);
            if (ret) {
                slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_cipher_init",
                              "Failed to store key for cipher %s\n", acs->cipher_display_name);
            } else {
                slapi_log_err(SLAPI_LOG_INFO, "attrcrypt_cipher_init",
                              "Key for cipher %s successfully generated and stored\n",
                              acs->cipher_display_name);
            }
        }
    } else if (KEYMGMT_ERR_CANT_UNWRAP == ret) {
        slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_cipher_init",
                      "Symmetric key failed to unwrap "
                      "with the private key; Cert might have been renewed since "
                      "the key is wrapped.  To recover the encrypted contents, "
                      "keep the wrapped symmetric key value.\n");
    } else if (ret) {
        slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_cipher_init",
                      "Failed to retrieve key for cipher %s (%d)\n",
                      acs->cipher_display_name, ret);
    }
    if (symmetric_key) {
        /* we loaded the symmetric key, store it in the acs */
        acs->key = symmetric_key;
    }
error:
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_cipher_init", "<- %d\n", ret);
    return ret;
}

static void
attrcrypt_acs_list_add(ldbm_instance *li, attrcrypt_cipher_state *acs)
{
    /* Realloc the existing list and add to the end */
    _back_crypt_acs_list_add(&(li->inst_attrcrypt_state_private), acs);
}

int
attrcrypt_init(ldbm_instance *li)
{
    int ret = 0;
    attrcrypt_cipher_entry *ace = NULL;
    SECKEYPrivateKey *private_key = NULL;
    SECKEYPublicKey *public_key = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_init", "->\n");
    if (slapd_security_library_is_initialized()) {
        /* In case the backend instance is restarted,
         * inst_attrcrypt_state_private in li could have memory containing
         * private keys.  The private data should be cleaned up first. */
        attrcrypt_cleanup_private(li);
        /* Get the server's private key, which is used to unwrap the stored symmetric keys */
        ret = attrcrypt_fetch_private_key(&private_key);
        if (!ret) {
            ret = attrcrypt_fetch_public_key(&public_key);
            if (!ret) {
                int cipher_is_available = 0;
                for (ace = attrcrypt_cipher_list;
                     ace && ace->cipher_number && !ret; ace++) {
                    /* Make a state object for this cipher */
                    attrcrypt_cipher_state *acs = (attrcrypt_cipher_state *)slapi_ch_calloc(sizeof(attrcrypt_cipher_state), 1);
                    ret = attrcrypt_cipher_init(li, ace, private_key, public_key, acs);
                    if (ret) {
                        slapi_ch_free((void **)&acs);
                        if (li->attrcrypt_configured) {
                            if ((ace + 1)->cipher_number) {
                                /* this is not the last cipher */
                                ret = 0;
                                continue;
                            }
                        } else {
                            /* if not using attrcrypt, just return success */
                            ret = 0;
                        }
                    } else {
                        /* Since we succeeded, add the acs to the backend instance list */
                        attrcrypt_acs_list_add(li, acs);
                        slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_init",
                                      "Initialized cipher %s in attrcrypt_init\n",
                                      ace->cipher_display_name);
                        cipher_is_available = 1; /* at least one is available */
                    }
                }
                if (!cipher_is_available) {
                    slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_init",
                                  "All prepared ciphers are not available. "
                                  "Please disable attribute encryption.\n");
                }
            }
            slapd_pk11_DestroyPublicKey(public_key);
            public_key = NULL;
        }
        slapd_pk11_DestroyPrivateKey(private_key);
        private_key = NULL;
    } else {
        if (li->attrcrypt_configured) {
            slapi_log_err(SLAPI_LOG_WARNING, "attrcrypt_init",
                          "Encryption is configured in backend %s, but because SSL is not enabled, "
                          "database encryption is not available and the configuration will be overridden.\n",
                          li->inst_name);
        }
    }
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_init", "<- %d\n", ret);
    return ret;
}

/*
 * Called by the config code when a new attribute is added,
 * to make sure that we already have the runtime state and key
 * stored for that cipher. If not, we attmept to make it.
 * If this function succeeds, then its ok to go on to use the
 * cipher.
 */
int
attrcrypt_check_enable_cipher(attrcrypt_cipher_entry *ace __attribute__((unused)))
{
    int ret = 0;
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_check_enable_cipher", "->\n");
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_check_enable_cipher", "<-\n");
    return ret;
}

int
attrcrypt_cleanup(attrcrypt_cipher_state *acs)
{
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_cleanup", "->\n");
    if (acs->key) {
        slapd_pk11_FreeSymKey(acs->key);
    }
    if (acs->slot) {
        slapd_pk11_FreeSlot(acs->slot);
    }
    if (acs->cipher_lock) {
        PR_DestroyLock(acs->cipher_lock);
    }
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_cleanup", "<-\n");
    return 0;
}

/*
 * This function cleans up the inst_attrcrypt_state_private in each backend
 * instance.
 */
int
attrcrypt_cleanup_private(ldbm_instance *li)
{
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_cleanup_private", "->\n");
    if (li && li->inst_attrcrypt_state_private) {
        _back_crypt_cleanup_private(&(li->inst_attrcrypt_state_private));
    }
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_cleanup_private", "<-\n");
    return 0;
}

static attrcrypt_cipher_state *
attrcrypt_get_acs(backend *be, attrcrypt_private *priv)
{
    /* Walk the list of acs objects looking for the one for our cipher */
    int cipher = priv->attrcrypt_cipher;
    ldbm_instance *li = (ldbm_instance *)be->be_instance_info;
    attrcrypt_state_private *iasp = li->inst_attrcrypt_state_private;
    if (iasp) {
        attrcrypt_cipher_state **current = &(iasp->acs_array[0]);
        while (current) {
            if ((*current)->ace->cipher_number == cipher) {
                return *current;
            }
            current++;
        }
    }
    return NULL;
}

#if defined(DEBUG_ATTRCRYPT)
static void
log_bytes(char *format_string, unsigned char *bytes, size_t length)
{
    size_t max_length = 40;
    size_t truncated_length = (length > max_length) ? max_length : length;
    size_t x = 0;
    char *print_buffer = NULL;
    char *print_ptr = NULL;

    if (bytes == NULL) {
        return;
    }

    print_buffer = (char *)slapi_ch_malloc((truncated_length * 3) + 1);
    print_ptr = print_buffer;

    for (x = 0; x < truncated_length; x++) {
        print_ptr += sprintf(print_ptr, "%02x ", bytes[x]);
    }

    slapi_log_err(SLAPI_LOG_DEBUG, "log_bytes", format_string, print_buffer, length);

    slapi_ch_free((void **)&print_buffer);
}
#endif

/* Either encipher or decipher an attribute value */
static int
attrcrypt_crypto_op(attrcrypt_private *priv, backend *be, struct attrinfo *ai, char *in_data, size_t in_size, char **out_data, size_t *out_size, int encrypt)
{
    int ret = -1;
    attrcrypt_cipher_state *acs = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_crypto_op", "->\n");
    acs = attrcrypt_get_acs(be, ai->ai_attrcrypt);
    if (NULL == acs) {
        /* This happens if SSL/NSS has not been enabled */
        return -1;
    }
#if defined(DEBUG_ATTRCRYPT)
    if (encrypt) {
        slapi_log_err(SLAPI_LOG_DEBUG, "attrcrypt_crypto_op", "encrypt '%s' (%d)\n", in_data, in_size);
    } else {
        log_bytes("attrcrypt_crypto_op decrypt '%s' (%d)\n", (unsigned char *)in_data, in_size);
    }
#endif
    ret = _back_crypt_crypto_op(priv, acs, in_data, in_size,
                                out_data, out_size, encrypt, be, ai);
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_crypto_op", "<- %d\n", ret);
    return ret;
}

static int
attrcrypt_crypto_op_value(attrcrypt_private *priv, backend *be, struct attrinfo *ai, Slapi_Value *invalue, Slapi_Value **outvalue, int encrypt)
{
    int ret = 0;
    char *in_data = NULL;
    size_t in_size = 0;
    char *out_data = NULL;
    size_t out_size = 0;
    struct berval *bval = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_crypto_op_value", "->\n");

    bval = (struct berval *)slapi_value_get_berval(invalue);
    in_data = bval->bv_val;
    in_size = bval->bv_len;

    ret = attrcrypt_crypto_op(priv, be, ai, in_data, in_size, &out_data, &out_size, encrypt);

    if (0 == ret) {
        struct berval outbervalue = {0};
        outbervalue.bv_len = out_size;
        outbervalue.bv_val = out_data;
        /* This call makes a copy of the payload data, so we need to free the original data after making the call */
        *outvalue = slapi_value_new_berval(&outbervalue);
        slapi_ch_free((void **)&out_data);
    }

    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_crypto_op_value", "<- %d\n", ret);
    return ret;
}

int
attrcrypt_crypto_op_value_replace(attrcrypt_private *priv, backend *be, struct attrinfo *ai, Slapi_Value *inoutvalue, int encrypt)
{
    int ret = 0;
    char *in_data = NULL;
    size_t in_size = 0;
    char *out_data = NULL;
    size_t out_size = 0;
    struct berval *bval = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_crypto_op_value_replace", "->\n");

    bval = (struct berval *)slapi_value_get_berval(inoutvalue);
    in_data = bval->bv_val;
    in_size = bval->bv_len;

    ret = attrcrypt_crypto_op(priv, be, ai, in_data, in_size, &out_data, &out_size, encrypt);

    if (0 == ret) {
        struct berval outbervalue = {0};
        outbervalue.bv_len = out_size;
        outbervalue.bv_val = out_data;
        /* This takes a copy of the payload, so we need to free it now */
        slapi_value_set_berval(inoutvalue, &outbervalue);
        slapi_ch_free((void **)&out_data);
    }

    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_crypto_op_value_replace", "<- %d\n", ret);
    return ret;
}

static int
attrcrypt_crypto_op_values(attrcrypt_private *priv, backend *be, struct attrinfo *ai, Slapi_Value **invalues, Slapi_Value ***outvalues, int encrypt)
{
    int ret = 0;
    int i = 0;
    Slapi_Value **encrypted_values = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_crypto_op_values", "->\n");
    encrypted_values = (Slapi_Value **)slapi_ch_calloc(sizeof(Slapi_Value *), local_valuearray_count(invalues) + 1);
    for (i = 0; (invalues[i] != NULL) && (ret == 0); i++) {
        Slapi_Value *encrypted_value = NULL;

        ret = attrcrypt_crypto_op_value(priv, be, ai, invalues[i], &encrypted_value, encrypt);
        if (ret) {                              /* If failed even once, free the entire Slapi_Value */
            valuearray_free(&encrypted_values); /* encrypted_values is set to NULL */
            break;
        } else {
            encrypted_values[i] = encrypted_value;
        }
    }
    *outvalues = encrypted_values;
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_crypto_op_values", "<- %d\n", ret);
    return ret;
}

static int
attrcrypt_crypto_op_values_replace(attrcrypt_private *priv, backend *be, struct attrinfo *ai, Slapi_Value **invalues, int encrypt)
{
    int ret = 0;
    int i = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_crypto_op_values_replace", "->\n");
    for (i = 0; (invalues[i] != NULL) && (ret == 0); i++) {

        ret = attrcrypt_crypto_op_value_replace(priv, be, ai, invalues[i], encrypt);
        if (ret) {
            break;
        }
    }
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_crypto_op_values_replace", "<-\n");
    return ret;
}

/* Modifies the entry in-place to decrypt any encrypted attributes */
int
attrcrypt_decrypt_entry(backend *be, struct backentry *e)
{
    int ret = 0;
    int rc = 0;
    Slapi_Attr *attr = NULL;
    char *type = NULL;
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;

    if (!inst->attrcrypt_configured) {
        /*
         * No encryption is enabled in this backend at all.
         * There's no need to scan the attributes to decrypt them.
         */
        return ret;
    }

    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_decrypt_entry", "<-\n");
    /* Scan through the entry's attributes, looking to see if any are configured for crypto */
    for (rc = slapi_entry_first_attr(e->ep_entry, &attr); rc == 0 && attr; rc = slapi_entry_next_attr(e->ep_entry, attr, &attr)) {

        struct attrinfo *ai = NULL;
        Slapi_Value *value = NULL;
        int i = 0;

        slapi_attr_get_type(attr, &type);
        ainfo_get(be, type, &ai);

        if (ai && ai->ai_attrcrypt) {
            i = slapi_attr_first_value(attr, &value);
            while (NULL != value && i != -1) {
                /* Now decrypt the attribute values in place on the original entry */
                ret = attrcrypt_crypto_op_value_replace(ai->ai_attrcrypt, be, ai, value, 0);
                if (ret) {
                    slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_decrypt_entry", "Decryption operation failed: %d\n", ret);
                    return ret;
                }
                i = slapi_attr_next_value(attr, i, &value);
            }
            /* Now do the same thing with deleted values */
            i = attr_first_deleted_value(attr, &value);
            while (NULL != value && i != -1) {
                /* Now decrypt the attribute values in place on the original entry */
                ret = attrcrypt_crypto_op_value_replace(ai->ai_attrcrypt, be, ai, value, 0);
                if (ret) {
                    slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_decrypt_entry", "Decryption operation 2 failed: %d\n", ret);
                    return ret;
                }
                i = attr_next_deleted_value(attr, i, &value);
            }
        }
    }
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_decrypt_entry", "<- %d\n", ret);
    return ret;
}

/* Encrypts attributes on this entry in-place (only changes the attribute data, nothing else)
 */
int
attrcrypt_encrypt_entry_inplace(backend *be, const struct backentry *inout)
{
    int ret = 0;
    int rc = 0;
    char *type = NULL;
    Slapi_Attr *attr = NULL;
    Slapi_Value **svals = NULL;
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;

    if (!inst->attrcrypt_configured) {
        /*
         * No encryption is enabled in this backend at all.
         * There's no need to scan the attributes to encrypt them.
         */
        return ret;
    }

    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_encrypt_entry_inplace", "->\n");
    /* Scan the entry's attributes looking for any that are configured for encryption */
    for (rc = slapi_entry_first_attr(inout->ep_entry, &attr); rc == 0;
         rc = slapi_entry_next_attr(inout->ep_entry, attr, &attr)) {

        struct attrinfo *ai = NULL;

        slapi_attr_get_type(attr, &type);

        ainfo_get(be, type, &ai);

        if (ai && ai->ai_attrcrypt) {
            svals = attr_get_present_values(attr);
            if (svals) {
                /* Now encrypt the attribute values in place on the new entry */
                ret = attrcrypt_crypto_op_values_replace(ai->ai_attrcrypt, be, ai, svals, 1);
            }
        }
    }
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_encrypt_entry_inplace", "<- %d\n", ret);
    return ret;
}

/* Makes a copy of the entry that has all necessary attributes encrypted
 * as a performance optimization, if there are no attributes configured
 * for encryption in the entry, then no copy is returned.
 */
int
attrcrypt_encrypt_entry(backend *be, const struct backentry *in, struct backentry **out)
{
    int ret = 0;
    int rc = 0;
    struct backentry *new_entry = NULL;
    char *type = NULL;
    Slapi_Attr *attr = NULL;
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;

    if (!inst->attrcrypt_configured) {
        /*
         * No encryption is enabled in this backend at all.
         * There's no need to scan the attributes to encrypt them.
         */
        return ret;
    }

    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_encrypt_entry", "->\n");
    *out = NULL;
    /* Scan the entry's attributes looking for any that are configured for encryption */
    for (rc = slapi_entry_first_attr(in->ep_entry, &attr); rc == 0;
         rc = slapi_entry_next_attr(in->ep_entry, attr, &attr)) {

        struct attrinfo *ai = NULL;

        slapi_attr_get_type(attr, &type);

        ainfo_get(be, type, &ai);

        if (ai && ai->ai_attrcrypt) {
            Slapi_Value **svals = attr_get_present_values(attr);
            if (svals) {
                Slapi_Value **new_vals = NULL;
                /* If we find one, did we make the new entry yet ? */
                if (NULL == new_entry) {
                    /* If not then make it now as a copy of the old entry */
                    new_entry = backentry_dup((struct backentry *)in);
                }
                /* Now encrypt the attribute values in place on the new entry */
                ret = attrcrypt_crypto_op_values(ai->ai_attrcrypt, be, ai, svals, &new_vals, 1);
                if (ret) {
                    slapi_log_err(SLAPI_LOG_ERR, "attrcrypt_encrypt_entry", "Failed to encrypt value, error %d\n",
                                  ret);
                    break;
                }
                /* DBDB does this call free the old value memory ? */
                /* yes, DBDB, but it does not free new_vals - new_vals is copied */
                slapi_entry_attr_replace_sv(new_entry->ep_entry, type, new_vals);
                valuearray_free(&new_vals);
            }
        }
    }
    *out = new_entry;
    slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_encrypt_entry", "<- %d\n", ret);
    return ret;
}

/*
 * Encrypt an index key. There is never any need to decrypt index keys since
 * we only ever look them up using plain text (except entryrdn).
 */
int
attrcrypt_encrypt_index_key(backend *be, struct attrinfo *ai, const struct berval *in, struct berval **out)
{
    int ret = 0;
    char *in_data = in->bv_val;
    size_t in_size = in->bv_len;
    char *out_data = NULL;
    size_t out_size = 0;
    struct berval *out_berval = NULL;
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;

    if (!inst->attrcrypt_configured) {
        /* No encryption is enabled in this backend at all. */
        return ret;
    }

    if (ai->ai_attrcrypt) {
        slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_encrypt_index_key", "->\n");
        ret = attrcrypt_crypto_op(ai->ai_attrcrypt, be, ai, in_data, in_size, &out_data, &out_size, 1);
        if (0 == ret) {
            out_berval = (struct berval *)ber_alloc();
            if (NULL == out_berval) {
                return ENOMEM;
            }
            out_berval->bv_len = out_size;
            /* Because we're making a new berval, we copy the payload pointer in */
            /* It's now the responsibility of our caller to free that data */
            out_berval->bv_val = out_data;
            *out = out_berval;
        }
        slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_encrypt_index_key", "<- %d\n", ret);
    }
    return ret;
}

/*
 * Decrypt index key
 * needed by entryrdn (subtree-rename)
 */
int
attrcrypt_decrypt_index_key(backend *be,
                            struct attrinfo *ai,
                            const struct berval *in,
                            struct berval **out)
{
    int rc = 0; /* success */
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;

    if (!inst->attrcrypt_configured) {
        /* No encryption is enabled in this backend at all. */
        return rc;
    }

    if (ai->ai_attrcrypt) {
        Slapi_Value *value = NULL;
        rc = -1;
        if (NULL == in || NULL == out) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "attrcrypt_decrypt_index_key", "Empty %s\n",
                          NULL == in ? "in" : NULL == out ? "out" : "unknown");
            return rc;
        }
        value = slapi_value_new_berval(in);
        slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_decrypt_index_key", "->\n");
        /* Decrypt the input values in place on the original entry */
        rc = attrcrypt_crypto_op_value_replace(ai->ai_attrcrypt, be, ai,
                                               value, 0 /* decrypt */);
        if (0 == rc) {
            const struct berval *out_bv =
                slapi_value_get_berval((const Slapi_Value *)value);
            if (NULL == out_bv) {
                rc = -1;
                goto bail;
            }
            (*out) = ber_bvdup((struct berval *)out_bv);
            if (NULL == *out) {
                rc = -1;
            }
        }
    bail:
        slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_decrypt_index_key", "<-\n");
        slapi_value_free(&value);
    }

    return rc;
}

/*
 * Hash an index key if it is too large.
 *
 * return values:  0 - success
 *              : -1 - error
 *
 * output value: out: non-NULL - hash successful
 *                  :     NULL - no hash or failure
 */
int
attrcrypt_hash_large_index_key(backend *be, char **prefix, struct attrinfo *ai, const struct berval *in, struct berval **out)
{
    int ret = 0;
    struct berval *out_berval = NULL;
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    char *new_prefix;

    /* If the index key is too long (i.e mdb case) we must hash it */
    if (in->bv_len >=  li->li_max_key_len) {
        PK11Context *c = PK11_CreateDigestContext(SEC_OID_MD5);
        if (c != NULL) {
            unsigned char hash[32];
            unsigned int hashLen = 0;
            char *hkey;
            int i;

            out_berval = (struct berval *)ber_alloc();
            if (NULL == out_berval) {
                PK11_DestroyContext(c, PR_TRUE);
                return ENOMEM;
            }
            slapi_log_err(SLAPI_LOG_TRACE, "attrcrypt_hash_large_index_key",
                          "Key lenght (%lu) >= max key lenght (%lu) so key must be hashed\n", in->bv_len, li->li_max_key_len);
            slapi_be_set_flag(be, SLAPI_BE_FLAG_DONT_BYPASS_FILTERTEST);
            PK11_DigestBegin(c);
            /* Compute hash for the key without the prefix */
            PK11_DigestOp(c, (unsigned char *)in->bv_val, in->bv_len);
            PK11_DigestFinal(c, hash, &hashLen, sizeof hash);
            /* Add HASH_PREFIX before the prefix */
            new_prefix = slapi_ch_smprintf("%c%s", HASH_PREFIX, *prefix);
            index_free_prefix(*prefix);
            *prefix = new_prefix;
            /* Build the key: hash value in hexa */
            hkey = slapi_ch_malloc(1+2*sizeof hash);
            out_berval->bv_val = hkey;
            out_berval->bv_len = 0;
            for (i=0; i<hashLen; i++) {
                sprintf(hkey, "%02X", hash[i]);
                out_berval->bv_len += 2;
                hkey += 2;
            }
            *out = out_berval;
            PK11_DestroyContext(c, PR_TRUE);
        } else {
            return ENODEV;
        }
    }

    return ret;
}


/******************************************************************************/
static int _back_crypt_cipher_init(Slapi_Backend *be, attrcrypt_state_private **state_priv, attrcrypt_cipher_entry *ace, SECKEYPrivateKey *private_key, SECKEYPublicKey *public_key, attrcrypt_cipher_state *acs, const char *dn_string);
static int _back_crypt_keymgmt_store_key(Slapi_Backend *be, attrcrypt_cipher_state *acs, SECKEYPublicKey *public_key, PK11SymKey *key_to_store, const char *dn_string);
static int _back_crypt_crypto_op_value(attrcrypt_state_private *state_priv, Slapi_Value *invalue, Slapi_Value **outvalue, int encrypt);

int
back_crypt_init(Slapi_Backend *be, const char *dn, const char *encAlgorithm, void **handle)
{
    int ret = 0;
    attrcrypt_cipher_entry *ace = NULL;
    SECKEYPrivateKey *private_key = NULL;
    SECKEYPublicKey *public_key = NULL;
    attrcrypt_state_private **state_priv = (attrcrypt_state_private **)handle;

    slapi_log_err(SLAPI_LOG_TRACE, "back_crypt_init", "->\n");
    /* Encryption is not specified */
    if (!encAlgorithm || !handle) {
        goto bail;
    }
    if (!slapd_security_library_is_initialized()) {
        goto bail;
    }
    _back_crypt_cleanup_private(state_priv);

    /* Get the server's private key,
     * which is used to unwrap the stored symmetric keys */
    ret = attrcrypt_fetch_private_key(&private_key);
    if (ret) {
        goto bail;
    }
    ret = attrcrypt_fetch_public_key(&public_key);
    if (ret) {
        goto bail;
    }
    for (ace = attrcrypt_cipher_list;
         ace && ace->cipher_number && !ret; ace++) {
        if (strcasecmp(ace->cipher_display_name, encAlgorithm)) {
            continue; /* did not match. next. */
        }
        /* Make a state object for this cipher */
        attrcrypt_cipher_state *acs = (attrcrypt_cipher_state *)slapi_ch_calloc(
            sizeof(attrcrypt_cipher_state), 1);
        ret = _back_crypt_cipher_init(be, state_priv, ace,
                                      private_key, public_key, acs, dn);
        if (ret) {
            slapi_log_err(SLAPI_LOG_ERR, "back_crypt_init",
                          "Failed to initialize cipher %s."
                          "Please choose other cipher or disable changelog "
                          "encryption.\n",
                          ace->cipher_display_name);
            slapi_ch_free((void **)&acs);
        } else {
            /* Since we succeeded, set acs to state_priv */
            _back_crypt_acs_list_add(state_priv, acs);
            slapi_log_err(SLAPI_LOG_BACKLDBM, "back_crypt_init",
                          "Initialized cipher %s\n",
                          ace->cipher_display_name);
        }
        break;
    }
    SECKEY_DestroyPublicKey(public_key);
    public_key = NULL;
    SECKEY_DestroyPrivateKey(private_key);
    private_key = NULL;
bail:
    slapi_log_err(SLAPI_LOG_TRACE, "back_crypt_init", "<- %d\n", ret);
    return ret;
}

int
back_crypt_destroy(void *handle)
{
    attrcrypt_state_private *state_priv = (attrcrypt_state_private *)handle;
    _back_crypt_cleanup_private(&state_priv);
    return 0;
}

/*
 * return values:  0 - success
 *              : -1 - error
 *
 * output value: out: non-NULL - encryption successful
 *                  :     NULL - no encryption or failure
 */
int
back_crypt_encrypt_value(void *handle, struct berval *in, struct berval **out)
{
    int ret = -1;
    Slapi_Value *invalue = NULL;
    Slapi_Value *outvalue = NULL;
    attrcrypt_state_private *state_priv = (attrcrypt_state_private *)handle;

    slapi_log_err(SLAPI_LOG_TRACE, "back_crypt_encrypt_value", "->\n");
    if (NULL == out) {
        goto bail;
    }
    *out = NULL;
    if (!state_priv) {
        goto bail;
    }
    invalue = slapi_value_new_berval(in);
    /* Now encrypt the attribute values in place on the new entry */
    ret = _back_crypt_crypto_op_value(state_priv, invalue, &outvalue, 1);
    if (0 == ret) {
        *out = slapi_ch_bvdup(slapi_value_get_berval(outvalue));
    }
bail:
    slapi_value_free(&invalue);
    slapi_value_free(&outvalue);
    slapi_log_err(SLAPI_LOG_TRACE, "back_crypt_encrypt_entry",
                  "<- (returning %d)\n", ret);
    return ret;
}

int
back_crypt_decrypt_value(void *handle, struct berval *in, struct berval **out)
{
    int ret = -1;
    Slapi_Value *invalue = NULL;
    Slapi_Value *outvalue = NULL;
    attrcrypt_state_private *state_priv = (attrcrypt_state_private *)handle;

    slapi_log_err(SLAPI_LOG_TRACE, "back_crypt_decrypt_value", "->\n");
    if (NULL == out) {
        goto bail;
    }
    *out = NULL;
    if (!state_priv) {
        goto bail;
    }
    invalue = slapi_value_new_berval(in);
    /* Now decrypt the value */
    ret = _back_crypt_crypto_op_value(state_priv, invalue, &outvalue, 0);
    if (0 == ret) {
        *out = slapi_ch_bvdup(slapi_value_get_berval(outvalue));
    }
bail:
    slapi_value_free(&invalue);
    slapi_value_free(&outvalue);
    slapi_log_err(SLAPI_LOG_TRACE, "back_crypt_decrypt_entry", "<- (returning %d)\n", ret);
    return ret;
}

static int
_back_crypt_crypto_op_value(attrcrypt_state_private *state_priv,
                            Slapi_Value *invalue,
                            Slapi_Value **outvalue,
                            int encrypt)
{
    int ret = -1;
    char *in_data = NULL;
    size_t in_size = 0;
    char *out_data = NULL;
    size_t out_size = 0;
    struct berval *bval = NULL;
    attrcrypt_cipher_state *acs = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "_back_crypt_crypto_op_value", "->\n");
    if (NULL == invalue || NULL == outvalue) {
        goto bail;
    }

    bval = (struct berval *)slapi_value_get_berval(invalue);
    if (NULL == bval) {
        goto bail;
    }
    in_data = bval->bv_val;
    in_size = bval->bv_len;

    acs = state_priv->acs_array[0];
    if (NULL == acs) {
        /* This happens if SSL/NSS has not been enabled */
        goto bail;
    }
    ret = _back_crypt_crypto_op(NULL, acs, in_data, in_size,
                                &out_data, &out_size, encrypt, NULL, NULL);
    if (0 == ret) {
        struct berval outbervalue = {0};
        outbervalue.bv_len = out_size;
        outbervalue.bv_val = out_data;
        /* This call makes a copy of the payload data,
         * so we need to free the original data after making the call */
        *outvalue = slapi_value_new_berval(&outbervalue);
        slapi_ch_free((void **)&out_data);
    }

bail:
    slapi_log_err(SLAPI_LOG_TRACE, "back_crypt_crypto_op_value", "<-(returning %d)\n", ret);
    return ret;
}


/* Initialize the structure for a single cipher */
static int
_back_crypt_cipher_init(Slapi_Backend *be,
                        attrcrypt_state_private **state_priv __attribute__((unused)),
                        attrcrypt_cipher_entry *ace,
                        SECKEYPrivateKey *private_key,
                        SECKEYPublicKey *public_key,
                        attrcrypt_cipher_state *acs,
                        const char *dn_string)
{
    int ret = 1; /* fail by default */
    PK11SymKey *symmetric_key = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "_back_crypt_cipher_init", "->\n");
    acs->cipher_lock = PR_NewLock();
    /* Fill in some basic stuff */
    acs->ace = ace;
    acs->cipher_display_name = ace->cipher_display_name;
    if (NULL == acs->cipher_lock) {
        slapi_log_err(SLAPI_LOG_ERR, "_back_crypt_cipher_init",
                      "Cipher lock not found.\n");
    }
    acs->slot = slapd_pk11_getInternalKeySlot();
    if (NULL == acs->slot) {
        slapi_log_err(SLAPI_LOG_ERR, "_back_crypt_cipher_init",
                      "Failed to create a slot for cipher %s\n",
                      acs->cipher_display_name);
        goto error;
    }
    /* Try to get the symmetric key for this cipher */
    ret = _back_crypt_keymgmt_get_key(acs, private_key,
                                      &symmetric_key, dn_string);
    if (KEYMGMT_ERR_NO_ENTRY == ret) {
        slapi_log_err(SLAPI_LOG_ERR, "_back_crypt_cipher_init",
                      "Entry storing key does not exist.\n");
    } else if (KEYMGMT_ERR_OTHER == ret) {
        slapi_log_err(SLAPI_LOG_ERR, "_back_crypt_cipher_init",
                      "Coding error.\n");
    } else if (KEYMGMT_ERR_CANT_UNWRAP == ret) {
        slapi_log_err(SLAPI_LOG_ERR, "_back_crypt_cipher_init",
                      "Symmetric key failed to unwrap "
                      "with the private key; Cert might have been renewed since "
                      "the key is wrapped.  To recover the encrypted contents, "
                      "keep the wrapped symmetric key value.\n");
    } else if (ret) {
        slapi_log_err(SLAPI_LOG_NOTICE, "_back_crypt_cipher_init",
                      "No symmetric key found for cipher "
                      "%s, attempting to create one...\n",
                      acs->cipher_display_name);
        ret = attrcrypt_generate_key(acs, &symmetric_key);
        if (ret) {
            slapi_log_err(SLAPI_LOG_ERR, "_back_crypt_cipher_init",
                          "Failed to generate key for %s\n",
                          acs->cipher_display_name);
            if (ret < 0) {
                slapi_log_err(SLAPI_LOG_ERR, "_back_crypt_cipher_init",
                              "Cipher %s is not supported on the security device.  "
                              "Do not configure changelog encryption with the cipher.\n",
                              ace->cipher_display_name);
            }
        }
        if (symmetric_key) {
            ret = _back_crypt_keymgmt_store_key(be, acs, public_key,
                                                symmetric_key, dn_string);
            if (ret) {
                slapi_log_err(SLAPI_LOG_ERR, "_back_crypt_cipher_init",
                              "Failed to store key for cipher %s\n", acs->cipher_display_name);
            } else {
                slapi_log_err(SLAPI_LOG_BACKLDBM, "_back_crypt_cipher_init",
                              "Key for cipher %s successfully generated and stored\n",
                              acs->cipher_display_name);
            }
        }
    }
    if (symmetric_key) {
        /* we loaded the symmetric key, store it in the acs */
        acs->key = symmetric_key;
    }
error:
    slapi_log_err(SLAPI_LOG_TRACE, "_back_crypt_cipher_init", "<- (returning %d\n", ret);
    return ret;
}

/*
 * This function cleans up the state_private in cl5Desc
 */
static int
_back_crypt_cleanup_private(attrcrypt_state_private **state_priv)
{
    attrcrypt_cipher_state **current = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "_back_crypt_cleanup_private", "->\n");
    if (state_priv && *state_priv) {
        for (current = &((*state_priv)->acs_array[0]); *current; current++) {
            attrcrypt_cleanup(*current);
            slapi_ch_free((void **)current);
        }
        slapi_ch_free((void **)state_priv);
    }
    slapi_log_err(SLAPI_LOG_TRACE, "_back_crypt_cleanup_private", "<-\n");
    return 0;
}

/* Retrieve a symmetric key from dse.ldif for a specified cipher */
/*
 * return values: 0 -- successfully retrieved
 *                KEYMGMT_ERR_NO_ENTRY     - Entry to store key does not exist
 *                KEYMGMT_ERR_NO_KEY_ATTR  - Entry has no key attribute
 *                KEYMGMT_ERR_NO_KEY_VALUE - Empty key
 *                KEYMGMT_ERR_CANT_UNWRAP  - Key failed to unwrap
 *                KEYMGMT_ERR_OTHER        - Other error
 */
static int
_back_crypt_keymgmt_get_key(attrcrypt_cipher_state *acs,
                            SECKEYPrivateKey *private_key,
                            PK11SymKey **key_from_store,
                            const char *dn_string)
{
    int ret = KEYMGMT_ERR_OTHER;
    Slapi_Entry *entry = NULL;
    Slapi_Attr *keyattr = NULL;

    if (NULL == key_from_store) {
        return ret;
    }
    slapi_log_err(SLAPI_LOG_TRACE, "_back_crypt_keymgmt_get_key", "->\n");
    *key_from_store = NULL;
    /* Fetch the entry */
    getConfigEntry(dn_string, &entry);
    /* Did we find the entry ? */
    if (entry) {
        SECItem key_to_unwrap = {0};
        /* If so then look for the attribute that contains the key */
        slapi_entry_attr_find(entry, KEY_ATTRIBUTE_NAME, &keyattr);
        if (keyattr) {
            Slapi_Value *v = NULL;
            ret = slapi_attr_first_value(keyattr, &v);
            if (ret < 0) {
                ret = KEYMGMT_ERR_NO_KEY_VALUE; /* Empty key */
                goto bail;
            }
            key_to_unwrap.len = slapi_value_get_length(v);
            key_to_unwrap.data = (void *)slapi_value_get_string(v);
            /* Unwrap it */
            ret = attrcrypt_unwrap_key(acs, private_key,
                                       &key_to_unwrap, key_from_store);
            if (ret) {
                ret = KEYMGMT_ERR_CANT_UNWRAP; /* Key failed to unwrap */
            }
        } else {
            ret = KEYMGMT_ERR_NO_KEY_ATTR; /* Entry has no key attribute */
        }
    } else {
        /* we didn't find the entry (which happens if the key has
         * never been generated) */
        ret = KEYMGMT_ERR_NO_ENTRY;
    }
bail:
    freeConfigEntry(&entry);
    slapi_log_err(SLAPI_LOG_TRACE, "_back_crypt_keymgmt_get_key", "<- (returning %d)\n", ret);
    return ret;
}

/* Store a symmetric key for a given cipher in dse.ldif */
static int
_back_crypt_keymgmt_store_key(Slapi_Backend *be,
                              attrcrypt_cipher_state *acs,
                              SECKEYPublicKey *public_key,
                              PK11SymKey *key_to_store,
                              const char *dn_string)
{
    int ret = 1;
    SECItem wrapped_symmetric_key = {0};
    ldbm_instance *li = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "_back_crypt_keymgmt_store_key", "->\n");
    if (!be || !be->be_instance_info) {
        goto bail;
    }
    li = (ldbm_instance *)be->be_instance_info;
    /* Wrap the key and then store it in the right place in dse.ldif */
    ret = attrcrypt_wrap_key(acs, key_to_store,
                             public_key, &wrapped_symmetric_key);
    if (!ret) {
        /* store the wrapped symmetric key to the specified entry (dn_string) */
        Slapi_PBlock *pb = slapi_pblock_new();
        Slapi_Value *key_value = NULL;
        struct berval key_as_berval = {0};
        Slapi_Mods *smods = slapi_mods_new();
        Slapi_Value *va[2];
        int rc = 0;

        /* Add the key as a binary attribute */
        key_as_berval.bv_val = (char *)wrapped_symmetric_key.data;
        key_as_berval.bv_len = wrapped_symmetric_key.len;
        key_value = slapi_value_new_berval(&key_as_berval);
        va[0] = key_value;
        va[1] = NULL;
        /* key_value is now a copy of key_as_berval
         * - free wrapped_symmetric_key */
        slapi_ch_free_string((char **)&wrapped_symmetric_key.data);

        slapi_mods_add_mod_values(smods, LDAP_MOD_REPLACE,
                                  KEY_ATTRIBUTE_NAME, va);
        slapi_modify_internal_set_pb(pb, dn_string,
                                     slapi_mods_get_ldapmods_byref(smods), NULL, NULL,
                                     li->inst_li->li_identity, 0);
        slapi_modify_internal_pb(pb);
        slapi_value_free(&key_value);
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
        if (rc) {
            char *resulttext = NULL;
            slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &resulttext);
            slapi_log_err(SLAPI_LOG_ERR, "_back_crypt_keymgmt_store_key",
                          "Failed to add config key to the DSE: %d: %s: %s\n",
                          rc, ldap_err2string(rc), resulttext ? resulttext : "unknown");
            ret = -1;
        }
        slapi_mods_free(&smods);
        slapi_pblock_destroy(pb);
    }
bail:
    slapi_log_err(SLAPI_LOG_TRACE, "_back_crypt_keymgmt_store_key", "<- (returning %d)\n", ret);
    return ret;
}

static void
_back_crypt_acs_list_add(attrcrypt_state_private **state_priv,
                         attrcrypt_cipher_state *acs)
{
    /* Realloc the existing list and add to the end */
    attrcrypt_cipher_state **current = NULL;
    size_t list_size = 0;

    if (NULL == state_priv) {
        return;
    }
    slapi_log_err(SLAPI_LOG_TRACE, "_back_crypt_acs_list_add", "->\n");

    /* Is the list already there ? */
    if (NULL == *state_priv) {
        /* If not, add it */
        /* 2 == The pointer and a NULL terminator */
        *state_priv = (attrcrypt_state_private *)slapi_ch_calloc(
            sizeof(attrcrypt_cipher_state *), 2);
    } else {
        /* Otherwise re-size it */
        for (current = &((*state_priv)->acs_array[0]); current && *current;
             current++) {
            list_size++;
        }
        *state_priv =
            (attrcrypt_state_private *)slapi_ch_realloc((char *)*state_priv,
                                                        sizeof(attrcrypt_cipher_state *) * (list_size + 2));
        (*state_priv)->acs_array[list_size + 1] = NULL;
    }
    (*state_priv)->acs_array[list_size] = acs;
    slapi_log_err(SLAPI_LOG_TRACE, "_back_crypt_acs_list_add", "<-\n");
    return;
}

/* Either encipher or decipher an attribute value */
static int
_back_crypt_crypto_op(attrcrypt_private *priv __attribute__((unused)),
                      attrcrypt_cipher_state *acs,
                      char *in_data,
                      size_t in_size,
                      char **out_data,
                      size_t *out_size,
                      int encrypt,
                      backend *be __attribute__((unused)),
                      struct attrinfo *ai __attribute__((unused)) /* just for debugging */)
{
    int rc = -1;
    SECStatus secret = 0;
    PK11Context *sec_context = NULL;
    SECItem iv_item = {0};
    SECItem *security_parameter = NULL;
    int output_buffer_length = 0;
    int output_buffer_size1 = 0;
    unsigned int output_buffer_size2 = 0;
    unsigned char *output_buffer = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "_back_crypt_crypto_op", "->\n");
    if (NULL == acs) {
        goto bail;
    }
    if (encrypt) {
        slapi_log_err(SLAPI_LOG_BACKLDBM, "_back_crypt_crypto_op",
                      "Encrypt '%s' (%lu)\n",
                      in_data, (long unsigned int)in_size);
    } else {
        slapi_log_err(SLAPI_LOG_BACKLDBM, "_back_crypt_crypto_op",
                      "Decrypt (%lu)\n", (long unsigned int)in_size);
    }
    /* Allocate the output buffer */
    output_buffer_length = in_size + BACK_CRYPT_OUTBUFF_EXTLEN;
    output_buffer = (unsigned char *)slapi_ch_malloc(output_buffer_length);
    /* Now call NSS to do the cipher op */
    iv_item.data = (unsigned char *)"aaaaaaaaaaaaaaaa"; /* ptr to an array
                                                           of IV bytes */
    iv_item.len = acs->ace->iv_length;                  /* length of the array of IV bytes */
    security_parameter = slapd_pk11_ParamFromIV(acs->ace->cipher_mechanism,
                                                &iv_item);
    if (NULL == security_parameter) {
        int errorCode = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "_back_crypt_crypto_op",
                      "Failed to make IV for cipher %s : %d - %s\n",
                      acs->ace->cipher_display_name, errorCode,
                      slapd_pr_strerror(errorCode));
        goto error;
    }
    sec_context = slapd_pk11_createContextBySymKey(acs->ace->cipher_mechanism,
                                                   (encrypt ? CKA_ENCRYPT : CKA_DECRYPT),
                                                   acs->key, security_parameter);
    if (NULL == sec_context) {
        int errorCode = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "_back_crypt_crypto_op",
                      "Failed on cipher %s : %d - %s\n",
                      acs->ace->cipher_display_name, errorCode,
                      slapd_pr_strerror(errorCode));
        goto error;
    }
    secret = slapd_pk11_cipherOp(sec_context, output_buffer,
                                 &output_buffer_size1, output_buffer_length,
                                 (unsigned char *)in_data, in_size);
    if (SECSuccess != secret) {
        int errorCode = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "_back_crypt_crypto_op",
                      "Failed on cipher %s : %d - %s\n",
                      acs->ace->cipher_display_name, errorCode,
                      slapd_pr_strerror(errorCode));
        goto error;
    }
    secret = slapd_pk11_DigestFinal(sec_context,
                                    output_buffer + output_buffer_size1,
                                    &output_buffer_size2,
                                    output_buffer_length - output_buffer_size1);
    if (SECSuccess != secret) {
        int errorCode = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "_back_crypt_crypto_op",
                      "Digest final failed on cipher %s : %d - %s\n",
                      acs->ace->cipher_display_name,
                      errorCode, slapd_pr_strerror(errorCode));
        goto error;
    } else {
#if defined(DEBUG_ATTRCRYPT)
        int recurse = 1;
        if (encrypt) {
            log_bytes("slapd_pk11_DigestFinal '%s' (%d)\n",
                      output_buffer, output_buffer_size1 + output_buffer_size2);
        } else {
            slapi_log_err(SLAPI_LOG_DEBUG, "DEBUG_ATTRCRYPT",
                          "slapd_pk11_DigestFinal '%s', %u\n",
                          output_buffer, output_buffer_size1 + output_buffer_size2);
        }
        if (*out_size == -1) {
            recurse = 0;
        }
#endif
        *out_size = output_buffer_size1 + output_buffer_size2;
        *out_data = (char *)output_buffer;
        rc = 0; /* success */
#if defined(DEBUG_ATTRCRYPT)
        if (recurse) {
            char *redo_data = NULL;
            size_t redo_size = -1;
            int redo_ret;

            slapi_log_err(SLAPI_LOG_DEBUG, "_back_crypt_crypto_op",
                          "------> check result of crypto op\n");
            if (priv && be && ai) {
                redo_ret = attrcrypt_crypto_op(priv, be, ai,
                                               *out_data, *out_size,
                                               &redo_data, &redo_size,
                                               !encrypt);
                slapi_log_err(SLAPI_LOG_DEBUG, "_back_crypt_crypto_op",
                              "attrcrypt_crypto_op returned (%d) "
                              "orig length %u redone length %u\n",
                              redo_ret, in_size, redo_size);
            } else {
                redo_ret = _back_crypt_crypto_op(NULL, acs,
                                                 *out_data, *out_size,
                                                 &redo_data, &redo_size,
                                                 !encrypt, NULL, NULL);
                slapi_log_err(SLAPI_LOG_DEBUG, "_back_crypt_crypto_op",
                              "_back_crypt_crypto_op returned (%d) "
                              "orig length %u redone length %u\n",
                              redo_ret, in_size, redo_size);
            }
            log_bytes("DEBUG_ATTRCRYPT orig bytes '%s' (%d)\n",
                      (unsigned char *)in_data, in_size);
            log_bytes("DEBUG_ATTRCRYPT redo bytes '%s' (%d)\n",
                      (unsigned char *)redo_data, redo_size);

            slapi_log_err(SLAPI_LOG_DEBUG, "_back_crypt_crypto_op",
                          "<------ check result of crypto op\n");
        }
#endif
    }
error:
    if (sec_context) {
        slapd_pk11_destroyContext(sec_context, PR_TRUE);
    }
    if (security_parameter) {
        SECITEM_FreeItem(security_parameter, PR_TRUE);
    }
    if (rc) {
        slapi_ch_free_string((char **)&output_buffer);
    }
bail:
    slapi_log_err(SLAPI_LOG_TRACE, "_back_crypt_crypto_op",
                  "<- (returning %d)\n", rc);
    return rc;
}

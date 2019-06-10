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

/* This file handles configuration information that is specific
 * to ldbm instance attribute encryption configuration.
 */

/* DBDB I left in the Sun copyright statement because some of the code
 * in this file is derived from an older file : ldbm_index_config.c
 */

#include "back-ldbm.h"
#include "attrcrypt.h"

/* Forward declarations for the callbacks */
int ldbm_instance_attrcrypt_config_add_callback(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *e, int *returncode, char *returntext, void *arg);
int ldbm_instance_attrcrypt_config_delete_callback(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *e, int *returncode, char *returntext, void *arg);

/*

Config entries look like this:

dn: cn=<attributeName>, cn=encrypted attributes, cn=databaseName, cn=ldbm database, cn=plugins, cn=config
objectclass: top
objectclass: nsAttributeEncryption
cn: <attributeName>
nsEncryptionAlgorithm: <cipherName>

*/

static int
ldbm_attrcrypt_parse_cipher(char *cipher_display_name)
{
    attrcrypt_cipher_entry *ce = attrcrypt_cipher_list;
    while (ce->cipher_number) {
        if (0 == strcmp(ce->cipher_display_name, cipher_display_name)) {
            return ce->cipher_number;
        }
        ce++;
    }
    return 0;
}

static int
ldbm_attrcrypt_parse_entry(ldbm_instance *inst __attribute__((unused)), Slapi_Entry *e, char **attribute_name, int *cipher)
{
    Slapi_Attr *attr;
    const struct berval *attrValue;
    Slapi_Value *sval;

    *cipher = 0;
    *attribute_name = NULL;

    /* Get the name of the attribute to index which will be the value
     * of the cn attribute. */
    if (slapi_entry_attr_find(e, "cn", &attr) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_attrcrypt_parse_entry - Malformed attribute encryption entry %s\n",
                      slapi_entry_get_dn(e), 0, 0);
        return LDAP_OPERATIONS_ERROR;
    }

    slapi_attr_first_value(attr, &sval);
    attrValue = slapi_value_get_berval(sval);
    *attribute_name = slapi_ch_strdup(attrValue->bv_val);

    /* Get the list of index types from the entry. */
    if (0 == slapi_entry_attr_find(e, "nsEncryptionAlgorithm", &attr)) {
        slapi_attr_first_value(attr, &sval);
        if (sval) {
            attrValue = slapi_value_get_berval(sval);
            *cipher = ldbm_attrcrypt_parse_cipher(attrValue->bv_val);
            if (0 == *cipher)
                slapi_log_err(SLAPI_LOG_WARNING, "ldbm_attrcrypt_parse_entry - "
                                                 "Attempt to configure unrecognized cipher %s in encrypted attribute config entry %s\n",
                              attrValue->bv_val, slapi_entry_get_dn(e), 0);
        }
    }
    return LDAP_SUCCESS;
}

static void
ldbm_instance_attrcrypt_enable(struct attrinfo *ai, int cipher)
{
    attrcrypt_private *priv = NULL;
    if (NULL == ai->ai_attrcrypt) {
        /* No existing private structure, allocate one */
        ai->ai_attrcrypt = (attrcrypt_private *)slapi_ch_calloc(1, sizeof(attrcrypt_private));
    }
    priv = ai->ai_attrcrypt;
    priv->attrcrypt_cipher = cipher;
}

static void
ldbm_instance_attrcrypt_disable(struct attrinfo *ai)
{
    if (NULL != ai->ai_attrcrypt) {
        /* Don't free the structure here, because other threads might be
         * concurrently referencing it.
         */
        ai->ai_attrcrypt = 0;
    }
}

/*
 * Config DSE callback for attribute encryption entry add.
 */
int
ldbm_instance_attrcrypt_config_add_callback(Slapi_PBlock *pb __attribute__((unused)),
                                            Slapi_Entry *e,
                                            Slapi_Entry *eAfter __attribute__((unused)),
                                            int *returncode,
                                            char *returntext,
                                            void *arg)
{
    ldbm_instance *inst = (ldbm_instance *)arg;
    char *attribute_name = NULL;
    int32_t cipher = 0;
    int32_t ret = SLAPI_DSE_CALLBACK_OK;

    returntext[0] = '\0';

    /* For add, we parse the entry, then check the attribute exists,
     * then check that indexing config does not preclude us encrypting it,
     * and finally we set the private structure in the attrinfo for the attribute.
     */

    *returncode = ldbm_attrcrypt_parse_entry(inst, e, &attribute_name, &cipher);

    if (*returncode == LDAP_SUCCESS) {

        struct attrinfo *ai = NULL;

        /* If the cipher was invalid, return unwilling to perform */
        if (0 == cipher) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "invalid cipher");
            *returncode = LDAP_UNWILLING_TO_PERFORM;
            ret = SLAPI_DSE_CALLBACK_ERROR;
        } else {
            ainfo_get(inst->inst_be, attribute_name, &ai);
            /* If we couldn't find a non-default attrinfo, then that means
             * that no indexing or encryption has yet been defined for this attribute
             * therefore , create a new attrinfo structure now.
             */
            if ((ai == NULL) || (0 == strcmp(LDBM_PSEUDO_ATTR_DEFAULT, ai->ai_type))) {
                /* If this attribute doesn't exist in the schema, then we DO NOT fail
                 * (this is because entensible objects and disabled schema checking allow
                 * non-schema attributes to exist.
                 */
                /* Make a new attrinfo object */
                attr_create_empty(inst->inst_be, attribute_name, &ai);
            }
            if (ai) {
                ldbm_instance_attrcrypt_enable(ai, cipher);
                /* Remember that we have some encryption enabled, so we can be intelligent about warning when SSL is not enabled */
                inst->attrcrypt_configured = 1;
            } else {
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_attrcrypt_config_add_callback - "
                                             "Attempt to encryption on a non-existent attribute: %s\n",
                              attribute_name, 0, 0);
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "attribute does not exist");
                *returncode = LDAP_UNWILLING_TO_PERFORM;
                ret = SLAPI_DSE_CALLBACK_ERROR;
            }
        }
    } else {
        ret = SLAPI_DSE_CALLBACK_ERROR;
    }
    if (attribute_name) {
        slapi_ch_free_string(&attribute_name);
    }
    return ret;
}

/*
 * Temp callback that gets called for each attribute encryption entry when a new
 * instance is starting up.
 */
int
ldbm_attrcrypt_init_entry_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg)
{
    return ldbm_instance_attrcrypt_config_add_callback(pb, e, entryAfter, returncode, returntext, arg);
}

/*
 * Config DSE callback for attribute encryption deletes.
 */
int
ldbm_instance_attrcrypt_config_delete_callback(Slapi_PBlock *pb __attribute__((unused)),
                                               Slapi_Entry *e,
                                               Slapi_Entry *entryAfter __attribute__((unused)),
                                               int *returncode,
                                               char *returntext,
                                               void *arg)
{
    ldbm_instance *inst = (ldbm_instance *)arg;
    char *attribute_name = NULL;
    int cipher = 0;
    int ret = SLAPI_DSE_CALLBACK_ERROR;

    returntext[0] = '\0';

    /* For add, we parse the entry, then check the attribute exists,
     * then check that indexing config does not preclude us encrypting it,
     * and finally we set the private structure in the attrinfo for the attribute.
     */

    *returncode = ldbm_attrcrypt_parse_entry(inst, e, &attribute_name, &cipher);

    if (*returncode == LDAP_SUCCESS) {

        struct attrinfo *ai = NULL;

        ainfo_get(inst->inst_be, attribute_name, &ai);
        if (ai == NULL || (0 == strcmp(LDBM_PSEUDO_ATTR_DEFAULT, ai->ai_type))) {
            slapi_log_err(SLAPI_LOG_WARNING, "ldbm_instance_attrcrypt_config_delete_callback - "
                                             "Attempt to delete encryption for non-existant attribute: %s\n",
                          attribute_name, 0, 0);
        } else {
            ldbm_instance_attrcrypt_disable(ai);
            ret = SLAPI_DSE_CALLBACK_OK;
        }
    }
    if (attribute_name) {
        slapi_ch_free((void **)&attribute_name);
    }
    return ret;
}

/*
 * Config DSE callback for index entry changes.
 *
 * this function is huge!
 */
int
ldbm_instance_attrcrypt_config_modify_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter __attribute__((unused)), int *returncode, char *returntext, void *arg)
{
    ldbm_instance *inst = (ldbm_instance *)arg;
    Slapi_Attr *attr;
    Slapi_Value *sval;
    const struct berval *attrValue;
    struct attrinfo *ainfo = NULL;
    LDAPMod **mods;
    int i = 0;
    int j = 0;

    returntext[0] = '\0';
    *returncode = LDAP_SUCCESS;
    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);

    slapi_entry_attr_find(e, "cn", &attr);
    slapi_attr_first_value(attr, &sval);
    attrValue = slapi_value_get_berval(sval);
    ainfo_get(inst->inst_be, attrValue->bv_val, &ainfo);
    if (NULL == ainfo) {
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    for (i = 0; (mods != NULL) && (mods[i] != NULL); i++) {

        char *config_attr = (char *)mods[i]->mod_type;

        /* There are basically three cases in the modify:
         * 1. The attribute was added
         * 2. The attribute was deleted
         * 3. The attribute was modified (deleted and added).
         * Now, of these three, only #3 is legal.
         * This is because the attribute is mandatory and single-valued in the schema.
         * We handle this as follows: an add will always replace what's there (if anything).
         * a delete will remove what's there as long as it matches what's being deleted.
         * this is to avoid ordering problems with the adds and deletes.
         */

        if (strcasecmp(config_attr, "nsEncryptionAlgorithm") == 0) {

            if (SLAPI_IS_MOD_ADD(mods[i]->mod_op)) {

                for (j = 0; mods[i]->mod_bvalues[j] != NULL; j++) {
                    int cipher = ldbm_attrcrypt_parse_cipher(mods[i]->mod_bvalues[j]->bv_val);
                    if (0 == cipher) {
                        /* Tried to configure an invalid cipher */
                    }
                    ldbm_instance_attrcrypt_enable(ainfo, cipher);
                }
                continue;
            }
            if (SLAPI_IS_MOD_DELETE(mods[i]->mod_op)) {
                if ((mods[i]->mod_bvalues == NULL) ||
                    (mods[i]->mod_bvalues[0] == NULL)) {
                    /* Not legal */
                    return SLAPI_DSE_CALLBACK_ERROR;
                } else {
                    for (j = 0; mods[i]->mod_bvalues[j] != NULL; j++) {
                        /* Code before here should ensure that we only ever delete something that was already here */
                        ldbm_instance_attrcrypt_disable(ainfo);
                    }
                }
                continue;
            }
        }
    }
    return SLAPI_DSE_CALLBACK_OK;
}

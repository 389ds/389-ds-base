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

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "rever.h"

static Slapi_PluginDesc pdesc_aes = {"aes-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "AES storage scheme plugin"};
static Slapi_PluginDesc pdesc_des = {"des-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "DES storage scheme plugin"};


static char *plugin_name = "ReverStoragePlugin";

#define AES_MECH 1
#define DES_MECH 2

int
aes_cmp(char *userpwd, char *dbpwd)
{
    char *cipher = NULL;
    int rc = 0;

    if (encode(userpwd, &cipher, AES_MECH) != 0) {
        rc = 1;
    } else {
        rc = strcmp(cipher, dbpwd);
    }
    slapi_ch_free_string(&cipher);

    return rc;
}

char *
aes_enc(char *pwd)
{
    char *cipher = NULL;

    if (encode(pwd, &cipher, AES_MECH) != 0) {
        return (NULL);
    } else {
        return (cipher);
    }
}

char *
aes_dec(char *pwd, char *alg)
{
    char *plain = NULL;

    if (decode(pwd, &plain, AES_MECH, alg) != 0) {
        return (NULL);
    } else {
        return (plain);
    }
}

int
aes_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> aes_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&pdesc_aes);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN, (void *)aes_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN, (void *)aes_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_DEC_FN, (void *)aes_dec);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME, AES_REVER_SCHEME_NAME);

    init_pbe_plugin();

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= aes_init %d\n", rc);

    return (rc);
}

int
des_cmp(char *userpwd, char *dbpwd)
{
    char *cipher = NULL;
    int rc = 0;

    if (encode(userpwd, &cipher, DES_MECH) != 0) {
        rc = 1;
    } else {
        rc = strcmp(cipher, dbpwd);
    }
    slapi_ch_free_string(&cipher);

    return rc;
}

char *
des_enc(char *pwd)
{
    char *cipher = NULL;

    if (encode(pwd, &cipher, DES_MECH) != 0) {
        return (NULL);
    } else {
        return (cipher);
    }
}

char *
des_dec(char *pwd)
{
    char *plain = NULL;

    if (decode(pwd, &plain, DES_MECH, NULL) != 0) {
        return (NULL);
    } else {
        return (plain);
    }
}

int
des_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> des_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&pdesc_des);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN, (void *)des_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN, (void *)des_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_DEC_FN, (void *)des_dec);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME, DES_REVER_SCHEME_NAME);

    init_pbe_plugin();

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= des_init %d\n", rc);

    return (rc);
}

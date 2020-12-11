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

#include "pwdstorage.h"

static Slapi_PluginDesc sha_pdesc = {"sha-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Secure Hashing Algorithm (SHA)"};

static Slapi_PluginDesc ssha_pdesc = {"ssha-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Salted Secure Hashing Algorithm (SSHA)"};

static Slapi_PluginDesc sha256_pdesc = {"sha256-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Secure Hashing Algorithm (SHA256)"};

static Slapi_PluginDesc ssha256_pdesc = {"ssha256-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Salted Secure Hashing Algorithm (SSHA256)"};

static Slapi_PluginDesc sha384_pdesc = {"sha384-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Secure Hashing Algorithm (SHA384)"};

static Slapi_PluginDesc ssha384_pdesc = {"ssha384-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Salted Secure Hashing Algorithm (SSHA384)"};

static Slapi_PluginDesc sha512_pdesc = {"sha512-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Secure Hashing Algorithm (SHA512)"};

static Slapi_PluginDesc ssha512_pdesc = {"ssha512-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Salted Secure Hashing Algorithm (SSHA512)"};

static Slapi_PluginDesc crypt_pdesc = {"crypt-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Unix crypt algorithm (CRYPT)"};

static Slapi_PluginDesc crypt_md5_pdesc = {"crypt-md5-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Unix crypt algorithm (CRYPT-MD5)"};

static Slapi_PluginDesc crypt_sha256_pdesc = {"crypt-sha256-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Unix crypt algorithm (CRYPT-SHA256)"};

static Slapi_PluginDesc crypt_sha512_pdesc = {"crypt-sha512-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Unix crypt algorithm (CRYPT-SHA512)"};

static Slapi_PluginDesc clear_pdesc = {"clear-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "No encryption (CLEAR)"};

static Slapi_PluginDesc ns_mta_md5_pdesc = {"NS-MTA-MD5-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Netscape MD5 (NS-MTA-MD5)"};

static Slapi_PluginDesc md5_pdesc = {"md5-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "MD5 hash algorithm (MD5)"};

static Slapi_PluginDesc smd5_pdesc = {"smd5-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Salted MD5 hash algorithm (SMD5)"};

static Slapi_PluginDesc pbkdf2_sha256_pdesc = {"pbkdf2-sha256-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Salted PBKDF2 SHA256 hash algorithm (PBKDF2_SHA256)"};

static Slapi_PluginDesc gost_yescrypt_pdesc = {"gost-yescrypt-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Yescrypt KDF algorithm (Streebog256)"};

static char *plugin_name = "NSPwdStoragePlugin";

int
sha_pwd_storage_scheme_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> sha_pwd_storage_scheme_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&sha_pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
                           (void *)sha1_pw_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
                           (void *)sha1_pw_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
                           "SHA");

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= sha_pwd_storage_scheme_init %d\n\n", rc);

    return (rc);
}

int
ssha_pwd_storage_scheme_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> ssha_pwd_storage_scheme_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&ssha_pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
                           (void *)salted_sha1_pw_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
                           (void *)sha1_pw_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
                           "SSHA");

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= ssha_pwd_storage_scheme_init %d\n\n", rc);
    return (rc);
}

int
sha256_pwd_storage_scheme_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> sha256_pwd_storage_scheme_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&sha256_pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
                           (void *)sha256_pw_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
                           (void *)sha256_pw_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
                           "SHA256");

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= sha256_pwd_storage_scheme_init %d\n\n", rc);

    return (rc);
}

int
ssha256_pwd_storage_scheme_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> ssha256_pwd_storage_scheme_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&ssha256_pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
                           (void *)salted_sha256_pw_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
                           (void *)sha256_pw_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
                           "SSHA256");

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= ssha256_pwd_storage_scheme_init %d\n\n", rc);
    return (rc);
}

int
sha384_pwd_storage_scheme_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> sha384_pwd_storage_scheme_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&sha384_pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
                           (void *)sha384_pw_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
                           (void *)sha384_pw_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
                           "SHA384");

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= sha384_pwd_storage_scheme_init %d\n\n", rc);

    return (rc);
}

int
ssha384_pwd_storage_scheme_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> ssha384_pwd_storage_scheme_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&ssha384_pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
                           (void *)salted_sha384_pw_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
                           (void *)sha384_pw_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
                           "SSHA384");

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= ssha384_pwd_storage_scheme_init %d\n\n", rc);
    return (rc);
}

int
sha512_pwd_storage_scheme_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> sha512_pwd_storage_scheme_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&sha512_pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
                           (void *)sha512_pw_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
                           (void *)sha512_pw_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
                           "SHA512");

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= sha512_pwd_storage_scheme_init %d\n\n", rc);

    return (rc);
}

int
ssha512_pwd_storage_scheme_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> ssha512_pwd_storage_scheme_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&ssha512_pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
                           (void *)salted_sha512_pw_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
                           (void *)sha512_pw_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
                           "SSHA512");

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= ssha512_pwd_storage_scheme_init %d\n\n", rc);
    return (rc);
}

int
crypt_pwd_storage_scheme_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> crypt_pwd_storage_scheme_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&crypt_pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
                           (void *)crypt_pw_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
                           (void *)crypt_pw_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
                           "CRYPT");

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= crypt_pwd_storage_scheme_init %d\n\n", rc);
    return (rc);
}

int
crypt_md5_pwd_storage_scheme_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> crypt_md5_pwd_storage_scheme_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&crypt_md5_pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
                           (void *)crypt_pw_md5_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
                           (void *)crypt_pw_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
                           "CRYPT-MD5");

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= crypt_md5_pwd_storage_scheme_init %d\n\n", rc);
    return (rc);
}

int
crypt_sha256_pwd_storage_scheme_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> crypt_sha256_pwd_storage_scheme_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&crypt_sha256_pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
                           (void *)crypt_pw_sha256_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
                           (void *)crypt_pw_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
                           "CRYPT-SHA256");

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= crypt_sha256_pwd_storage_scheme_init %d\n\n", rc);
    return (rc);
}

int
crypt_sha512_pwd_storage_scheme_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> crypt_sha512_pwd_storage_scheme_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&crypt_sha512_pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
                           (void *)crypt_pw_sha512_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
                           (void *)crypt_pw_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
                           "CRYPT-SHA512");

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= crypt_sha512_pwd_storage_scheme_init %d\n\n", rc);
    return (rc);
}

int
clear_pwd_storage_scheme_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> clear_pwd_storage_scheme_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&clear_pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
                           (void *)clear_pw_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
                           (void *)clear_pw_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
                           "CLEAR");

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= clear_pwd_storage_scheme_init %d\n\n", rc);
    return (rc);
}

int
ns_mta_md5_pwd_storage_scheme_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> ns_mta_md5_pwd_storage_scheme_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&ns_mta_md5_pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
                           (void *)NULL);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
                           (void *)ns_mta_md5_pw_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
                           "NS-MTA-MD5");

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= ns_mta_md5_pwd_storage_scheme_init %d\n\n", rc);
    return (rc);
}

int
md5_pwd_storage_scheme_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> md5_pwd_storage_scheme_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&md5_pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
                           (void *)md5_pw_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
                           (void *)md5_pw_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
                           "MD5");

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= md5_pwd_storage_scheme_init %d\n\n", rc);
    return (rc);
}

int
smd5_pwd_storage_scheme_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> smd5_pwd_storage_scheme_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&smd5_pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
                           (void *)smd5_pw_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
                           (void *)smd5_pw_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
                           "SMD5");

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= smd5_pwd_storage_scheme_init %d\n\n", rc);
    return (rc);
}

int
pbkdf2_sha256_pwd_storage_scheme_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> pbkdf2_sha256_pwd_storage_scheme_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&pbkdf2_sha256_pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN, (void *)&pbkdf2_sha256_start);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN, (void *)&pbkdf2_sha256_close);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN, (void *)pbkdf2_sha256_pw_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN, (void *)pbkdf2_sha256_pw_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME, PBKDF2_SHA256_SCHEME_NAME);

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= pbkdf2_sha256_pwd_storage_scheme_init %d\n", rc);
    return rc;
}

int
gost_yescrypt_pwd_storage_scheme_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "=> gost_yescrypt_pwd_storage_scheme_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&gost_yescrypt_pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN, (void *)gost_yescrypt_pw_enc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN, (void *)gost_yescrypt_pw_cmp);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME, GOST_YESCRYPT_SCHEME_NAME);

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "<= gost_yescrypt_pwd_storage_scheme_init %d\n", rc);
    return rc;
}

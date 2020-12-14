/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#pragma once

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <slapi-plugin.h>
#include <slapi-private.h>
#include <ssl.h>
#include <nspr.h>
#include <plbase64.h>
#include <ldif.h>
#include "md5.h"

#define PWD_HASH_PREFIX_START '{'
#define PWD_HASH_PREFIX_END   '}'

#define MAX_SHA_HASH_SIZE HASH_LENGTH_MAX

#define SHA1_SCHEME_NAME          "SHA"
#define SHA1_NAME_LEN              3
#define SALTED_SHA1_SCHEME_NAME   "SSHA"
#define SALTED_SHA1_NAME_LEN      4
#define SHA256_SCHEME_NAME        "SHA256"
#define SHA256_NAME_LEN           6
#define SALTED_SHA256_SCHEME_NAME "SSHA256"
#define SALTED_SHA256_NAME_LEN    7
#define SHA384_SCHEME_NAME        "SHA384"
#define SHA384_NAME_LEN           6
#define SALTED_SHA384_SCHEME_NAME "SSHA384"
#define SALTED_SHA384_NAME_LEN    7
#define SHA512_SCHEME_NAME        "SHA512"
#define SHA512_NAME_LEN           6
#define SALTED_SHA512_SCHEME_NAME "SSHA512"
#define SALTED_SHA512_NAME_LEN    7
#define CRYPT_SCHEME_NAME         "crypt"
#define CRYPT_NAME_LEN            5
#define NS_MTA_MD5_SCHEME_NAME    "NS-MTA-MD5"
#define NS_MTA_MD5_NAME_LEN       10
#define CLEARTEXT_SCHEME_NAME     "clear"
#define CLEARTEXT_NAME_LEN        5
#define MD5_SCHEME_NAME           "MD5"
#define MD5_NAME_LEN              3
#define SALTED_MD5_SCHEME_NAME    "SMD5"
#define SALTED_MD5_NAME_LEN       4
#define PBKDF2_SHA256_SCHEME_NAME "PBKDF2_SHA256"
#define PBKDF2_SHA256_NAME_LEN    13
#define GOST_YESCRYPT_SCHEME_NAME "GOST_YESCRYPT"


SECStatus sha_salted_hash(char *hash_out, const char *pwd, struct berval *salt, unsigned int secOID);
int sha_pw_cmp(const char *userpwd, const char *dbpwd, unsigned int shaLen);
char *sha_pw_enc(const char *pwd, unsigned int shaLen);
char *salted_sha_pw_enc(const char *pwd, unsigned int shaLen);
int sha1_pw_cmp(const char *userpwd, const char *dbpwd);
char *sha1_pw_enc(const char *pwd);
char *salted_sha1_pw_enc(const char *pwd);
int sha256_pw_cmp(const char *userpwd, const char *dbpwd);
char *sha256_pw_enc(const char *pwd);
char *salted_sha256_pw_enc(const char *pwd);
int sha384_pw_cmp(const char *userpwd, const char *dbpwd);
char *sha384_pw_enc(const char *pwd);
char *salted_sha384_pw_enc(const char *pwd);
int sha512_pw_cmp(const char *userpwd, const char *dbpwd);
char *sha512_pw_enc(const char *pwd);
char *salted_sha512_pw_enc(const char *pwd);
int clear_pw_cmp(const char *userpwd, const char *dbpwd);
char *clear_pw_enc(const char *pwd);
int crypt_pw_cmp(const char *userpwd, const char *dbpwd);
char *crypt_pw_enc(const char *pwd);
char *crypt_pw_md5_enc(const char *pwd);
char *crypt_pw_sha256_enc(const char *pwd);
char *crypt_pw_sha512_enc(const char *pwd);
int ns_mta_md5_pw_cmp(const char *userpwd, const char *dbpwd);
int md5_pw_cmp(const char *userpwd, const char *dbpwd);
char *md5_pw_enc(const char *pwd);
int smd5_pw_cmp(const char *userpwd, const char *dbpwd);
char *smd5_pw_enc(const char *pwd);
int gost_yescrypt_pw_cmp(const char *userpwd, const char *dbpwd);
char *gost_yescrypt_pw_enc(const char *pwd);

int pbkdf2_sha256_start(Slapi_PBlock *pb);
int pbkdf2_sha256_close(Slapi_PBlock *pb);
SECStatus pbkdf2_sha256_hash(char *hash_out, size_t hash_out_len, SECItem *pwd, SECItem *salt, PRUint32 iterations);
char *pbkdf2_sha256_pw_enc(const char *pwd);
int pbkdf2_sha256_pw_cmp(const char *userpwd, const char *dbpwd);

/* For testing pbkdf2 only */
uint64_t pbkdf2_sha256_benchmark_iterations();
PRUint32 pbkdf2_sha256_calculate_iterations(uint64_t time_nsec);

/* Utility functions */
PRUint32 pwdstorage_base64_decode_len(const char *encval, PRUint32 enclen);

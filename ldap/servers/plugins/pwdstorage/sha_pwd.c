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

#include <sechash.h>

#define SHA_SALT_LENGTH 8 /* number of bytes of data in salt */
#define OLD_SALT_LENGTH 8
#define NOT_FIRST_TIME (time_t)1 /* not the first logon */

static char *hasherrmsg = "pw_cmp: %s userPassword \"%s\" is the wrong length or is not properly encoded BASE64\n";

static char *plugin_name = "NSPwdStoragePlugin";

#define DS40B1_SALTED_SHA_LENGTH 18
/* Directory Server 4.0 Beta 1 implemented a scheme that stored
 * 8 bytes of salt plus the first 10 bytes of the SHA-1 digest.
 * It's obsolescent now, but we still handle such stored values.
 */

int
sha_pw_cmp(const char *userpwd, const char *dbpwd, unsigned int shaLen)
{
    /*
     * SHA passwords are stored in the database as shaLen bytes of
     * hash, followed by zero or more bytes of salt, all BASE64 encoded.
     */
    int result = 1; /* failure */
    char userhash[MAX_SHA_HASH_SIZE];
    char quick_dbhash[MAX_SHA_HASH_SIZE + SHA_SALT_LENGTH + 3];
    char *dbhash = quick_dbhash;
    struct berval salt;
    PRUint32 hash_len;
    unsigned int secOID;
    char *schemeName;
    char *hashresult = NULL;
    PRUint32 dbpwd_len;

    /* Determine which algorithm we're using */
    switch (shaLen) {
    case SHA1_LENGTH:
        schemeName = SHA1_SCHEME_NAME;
        secOID = SEC_OID_SHA1;
        break;
    case SHA256_LENGTH:
        schemeName = SHA256_SCHEME_NAME;
        secOID = SEC_OID_SHA256;
        break;
    case SHA384_LENGTH:
        schemeName = SHA384_SCHEME_NAME;
        secOID = SEC_OID_SHA384;
        break;
    case SHA512_LENGTH:
        schemeName = SHA512_SCHEME_NAME;
        secOID = SEC_OID_SHA512;
        break;
    default:
        /* An unknown shaLen was passed in.  We shouldn't get here. */
        goto loser;
    }

    /* in some cases, the password was stored incorrectly - the base64 dbpwd ends
       in a newline - we check for this case and remove the newline, if any -
       see bug 552421 */
    dbpwd_len = strlen(dbpwd);
    if ((dbpwd_len > 0) && (dbpwd[dbpwd_len - 1] == '\n')) {
        dbpwd_len--;
    }

    /*
     * Decode hash stored in database.
     */
    hash_len = pwdstorage_base64_decode_len(dbpwd, dbpwd_len);
    if (hash_len > sizeof(quick_dbhash)) { /* get more space: */
        dbhash = (char *)slapi_ch_calloc(hash_len, sizeof(char));
        if (dbhash == NULL)
            goto loser;
    } else {
        memset(quick_dbhash, 0, sizeof(quick_dbhash));
    }
    hashresult = PL_Base64Decode(dbpwd, dbpwd_len, dbhash);
    if (NULL == hashresult) {
        slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, hasherrmsg, schemeName, dbpwd);
        goto loser;
    } else if (hash_len >= shaLen) {             /* must be salted */
        salt.bv_val = (void *)(dbhash + shaLen); /* salt starts after hash value */
        salt.bv_len = hash_len - shaLen;         /* remaining bytes must be salt */
    } else if (hash_len >= DS40B1_SALTED_SHA_LENGTH) {
        salt.bv_val = (void *)dbhash;
        salt.bv_len = OLD_SALT_LENGTH;
    } else { /* unsupported, invalid BASE64 (hash_len < 0), or similar */
        slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, hasherrmsg, schemeName, dbpwd);
        goto loser;
    }

    /* hash the user's key */
    memset(userhash, 0, sizeof(userhash));
    if (sha_salted_hash(userhash, userpwd, &salt, secOID) != SECSuccess) {
        slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "sha_pw_cmp: sha_salted_hash() failed\n");
        goto loser;
    }

    /* the proof is in the comparison... */
    if (hash_len >= shaLen) {
        result = slapi_ct_memcmp(userhash, dbhash, shaLen);
    } else {
        result = slapi_ct_memcmp(userhash, dbhash + OLD_SALT_LENGTH, hash_len - OLD_SALT_LENGTH);
    }

loser:
    if (dbhash && dbhash != quick_dbhash) {
        slapi_ch_free_string(&dbhash);
    }
    return result;
}

char *
sha_pw_enc(const char *pwd, unsigned int shaLen)
{
    char hash[MAX_SHA_HASH_SIZE];
    char *enc;
    char *schemeName;
    unsigned int schemeNameLen;
    unsigned int secOID;
    size_t enclen;

    /* Determine which algorithm we're using */
    switch (shaLen) {
    case SHA1_LENGTH:
        schemeName = SHA1_SCHEME_NAME;
        schemeNameLen = SHA1_NAME_LEN;
        secOID = SEC_OID_SHA1;
        break;
    case SHA256_LENGTH:
        schemeName = SHA256_SCHEME_NAME;
        schemeNameLen = SHA256_NAME_LEN;
        secOID = SEC_OID_SHA256;
        break;
    case SHA384_LENGTH:
        schemeName = SHA384_SCHEME_NAME;
        schemeNameLen = SHA384_NAME_LEN;
        secOID = SEC_OID_SHA384;
        break;
    case SHA512_LENGTH:
        schemeName = SHA512_SCHEME_NAME;
        schemeNameLen = SHA512_NAME_LEN;
        secOID = SEC_OID_SHA512;
        break;
    default:
        /* An unknown shaLen was passed in.  We shouldn't get here. */
        return (NULL);
    }

    /* hash the user's key */
    memset(hash, 0, sizeof(hash));
    if (sha_salted_hash(hash, pwd, NULL, secOID) != SECSuccess) {
        return (NULL);
    }

    enclen = 3 + schemeNameLen + LDIF_BASE64_LEN(shaLen);
    if ((enc = slapi_ch_calloc(enclen, sizeof(char))) == NULL) {
        return (NULL);
    }

    sprintf(enc, "%c%s%c", PWD_HASH_PREFIX_START, schemeName,
            PWD_HASH_PREFIX_END);
    (void)PL_Base64Encode(hash, shaLen, enc + 2 + schemeNameLen);

    return (enc);
}

/*
 * Wrapper password comparison functions
 */
int
sha1_pw_cmp(const char *userpwd, const char *dbpwd)
{
    return sha_pw_cmp(userpwd, dbpwd, SHA1_LENGTH);
}

int
sha256_pw_cmp(const char *userpwd, const char *dbpwd)
{
    return sha_pw_cmp(userpwd, dbpwd, SHA256_LENGTH);
}

int
sha384_pw_cmp(const char *userpwd, const char *dbpwd)
{
    return sha_pw_cmp(userpwd, dbpwd, SHA384_LENGTH);
}

int
sha512_pw_cmp(const char *userpwd, const char *dbpwd)
{
    return sha_pw_cmp(userpwd, dbpwd, SHA512_LENGTH);
}

/*
 * Wrapper password encryption functions
 */
char *
sha1_pw_enc(const char *pwd)
{
    return sha_pw_enc(pwd, SHA1_LENGTH);
}

char *
sha256_pw_enc(const char *pwd)
{
    return sha_pw_enc(pwd, SHA256_LENGTH);
}

char *
sha384_pw_enc(const char *pwd)
{
    return sha_pw_enc(pwd, SHA384_LENGTH);
}

char *
sha512_pw_enc(const char *pwd)
{
    return sha_pw_enc(pwd, SHA512_LENGTH);
}

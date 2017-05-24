/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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
#include <sys/socket.h>
#if defined( hpux ) || defined (LINUX) || defined (__FreeBSD__)
#ifndef __USE_XOPEN
#define __USE_XOPEN     /* linux */
#endif /* __USE_XOPEN */
#include <unistd.h>
#else /* hpux */
#include <crypt.h>
#endif /* hpux */

#include "pwdstorage.h"

static PRLock *cryptlock = NULL; /* Some implementations of crypt are not thread safe.  ie. ours & Irix */

/* characters used in crypt encoding */
static unsigned char itoa64[] =         /* 0 ... 63 => ascii - 64 */
        "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

#define CRYPT_UNIX 0
#define CRYPT_MD5 1
#define CRYPT_SHA256 2
#define CRYPT_SHA512 3


int
crypt_start(Slapi_PBlock *pb __attribute__((unused)))
{
    if (!cryptlock) {
        cryptlock = PR_NewLock();
    }
    return 0;
}

int
crypt_close(Slapi_PBlock *pb __attribute__((unused)))
{
    if (cryptlock) {
        PR_DestroyLock(cryptlock);
        cryptlock = NULL;
    }
    return 0;
}

int
crypt_pw_cmp( const char *userpwd, const char *dbpwd )
{
    int rc;
    char *cp;
    PR_Lock(cryptlock);
    /* we use salt (first 2 chars) of encoded password in call to crypt() */
    cp = crypt( userpwd, dbpwd );
    if (cp) {
       rc= slapi_ct_memcmp( dbpwd, cp, strlen(dbpwd));
    } else {
       rc = -1;
    }
    PR_Unlock(cryptlock);
    return rc;
}

static char*
crypt_pw_enc_by_hash( const char *pwd, int hash_algo){
    char salt[3];
    char *algo_salt = NULL;
    char *cry;
    char *enc = NULL;
    long v;
	static unsigned int seed = 0;

	if ( seed == 0)
	{
		seed = (unsigned int)slapi_rand();
	}
    v = slapi_rand_r(&seed);

    salt[0] = itoa64[v & 0x3f];
    v >>= 6;
    salt[1] = itoa64[v & 0x3f];
    salt[2] = '\0';

    /* Prepare our salt based on the hashing algorithm */
    if (hash_algo == CRYPT_UNIX) {
        algo_salt = strdup(salt);
    } else if (hash_algo == CRYPT_MD5) {
        algo_salt = slapi_ch_smprintf("$1$%s", salt);
    } else if (hash_algo == CRYPT_SHA256) {
        algo_salt = slapi_ch_smprintf("$5$%s", salt);
    } else if (hash_algo == CRYPT_SHA512) {
        algo_salt = slapi_ch_smprintf("$6$%s", salt);
    } else {
        /* default to CRYPT_UNIX */
        algo_salt = strdup(salt);
    }

    PR_Lock(cryptlock);
    cry = crypt( pwd, algo_salt );
    if ( cry != NULL )
    {
        enc = slapi_ch_smprintf("%c%s%c%s", PWD_HASH_PREFIX_START, CRYPT_SCHEME_NAME, PWD_HASH_PREFIX_END, cry );
    }
    PR_Unlock(cryptlock);
    slapi_ch_free_string(&algo_salt);

    return( enc );

}

char *
crypt_pw_enc( const char *pwd )
{
    return crypt_pw_enc_by_hash(pwd, CRYPT_UNIX);
}

char *
crypt_pw_md5_enc( const char *pwd )
{
    return crypt_pw_enc_by_hash(pwd, CRYPT_MD5);
}
char *
crypt_pw_sha256_enc( const char *pwd )
{
    return crypt_pw_enc_by_hash(pwd, CRYPT_SHA256);
}
char *
crypt_pw_sha512_enc( const char *pwd )
{
    return crypt_pw_enc_by_hash(pwd, CRYPT_SHA512);
}

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
#if defined( hpux ) || defined (LINUX)
#ifndef __USE_XOPEN
#define __USE_XOPEN     /* linux */
#endif /* __USE_XOPEN */
#include <unistd.h>
#else /* hpux */
#include <crypt.h>
#endif /* hpux */

#include "pwdstorage.h"

static PRLock *cryptlock; /* Some implementations of crypt are not thread safe.  ie. ours & Irix */

/* characters used in crypt encoding */
static unsigned char itoa64[] =         /* 0 ... 63 => ascii - 64 */
        "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";



void
crypt_init()
{
    cryptlock = PR_NewLock();
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

char *
crypt_pw_enc( const char *pwd )
{
    char *cry, salt[3];
    char *enc= NULL;
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

    PR_Lock(cryptlock);
    cry = crypt( pwd, salt );
    if ( cry != NULL )
    {
        enc = slapi_ch_smprintf("%c%s%c%s", PWD_HASH_PREFIX_START, CRYPT_SCHEME_NAME, PWD_HASH_PREFIX_END, cry );
    }    
    PR_Unlock(cryptlock);
    return( enc );
}


/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * slapd hashed password routines
 *
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifdef _WIN32
char *crypt(char *key, char *salt);
#else
#include <sys/socket.h>
#if defined( hpux ) || defined ( AIX ) || defined (LINUX) || defined (OSF1)
#define __USE_XOPEN     /* linux */
#include <unistd.h>
#else /* hpux */
#include <crypt.h>
#endif /* hpux */
#endif /* _WIN32 */

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
crypt_pw_cmp( char *userpwd, char *dbpwd )
{
    int rc;
    char *cp;
    PR_Lock(cryptlock);
    /* we use salt (first 2 chars) of encoded password in call to crypt() */
    cp = crypt( userpwd, dbpwd );
    if (cp) {
       rc= strcmp( dbpwd, cp);
    } else {
       rc = -1;
    }
    PR_Unlock(cryptlock);
    return rc;
}

char *
crypt_pw_enc( char *pwd )
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
        enc = slapi_ch_malloc( 3 + CRYPT_NAME_LEN + strlen( cry ));
        if ( enc != NULL )
        {
            sprintf( enc, "%c%s%c%s", PWD_HASH_PREFIX_START, CRYPT_SCHEME_NAME, PWD_HASH_PREFIX_END, cry );
        }
    }    
    PR_Unlock(cryptlock);
    return( enc );
}


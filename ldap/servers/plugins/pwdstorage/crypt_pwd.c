/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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
        enc = slapi_ch_smprintf("%c%s%c%s", PWD_HASH_PREFIX_START, CRYPT_SCHEME_NAME, PWD_HASH_PREFIX_END, cry );
    }    
    PR_Unlock(cryptlock);
    return( enc );
}


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

#include "pwdstorage.h"

#if defined(NET_SSL)
#include <sechash.h>
#endif /* NET_SSL */

#define SHA1_SALT_LENGTH    8   /* number of bytes of data in salt */
#define NOT_FIRST_TIME (time_t)1 /* not the first logon */

static char *hasherrmsg = "pw_cmp: %s userPassword \"%s\" is the wrong length or is not properly encoded BASE64\n";

static char *plugin_name = "NSPwdStoragePlugin";

#define DS40B1_SALTED_SHA_LENGTH 18
/* Directory Server 4.0 Beta 1 implemented a scheme that stored
 * 8 bytes of salt plus the first 10 bytes of the SHA-1 digest.
 * It's obsolescent now, but we still handle such stored values.
 */
 
int
sha1_pw_cmp (char *userpwd, char *dbpwd )
{
    /*
     * SHA1 passwords are stored in the database as SHA1_LENGTH bytes of
     * hash, followed by zero or more bytes of salt, all BASE64 encoded.
     */
    int result = 1; /* failure */
    unsigned char userhash[SHA1_LENGTH];
    unsigned char quick_dbhash[SHA1_LENGTH + SHA1_SALT_LENGTH + 3];
    unsigned char *dbhash = quick_dbhash;
    struct berval salt;
    int hash_len;   /* must be a signed valued -- see below */
 
    /*
     * Decode hash stored in database.
     *
     * Note that ldif_base64_decode() returns a value less than zero to
     * indicate that a decoding error occurred, so it is critical that
     * hash_len be a signed value.
     */
    hash_len = (((strlen(dbpwd) + 3) / 4) * 3); /* maybe less */
    if ( hash_len > sizeof(quick_dbhash) ) { /* get more space: */
        dbhash = (unsigned char*) slapi_ch_malloc( hash_len );
        if ( dbhash == NULL ) goto loser;
    }
    hash_len = ldif_base64_decode( dbpwd, dbhash );
    if ( hash_len >= SHA1_LENGTH ) {
        salt.bv_val = (void*)(dbhash + SHA1_LENGTH);
        salt.bv_len = hash_len - SHA1_LENGTH;
    } else if ( hash_len == DS40B1_SALTED_SHA_LENGTH ) {
        salt.bv_val = (void*)dbhash;
        salt.bv_len = 8;
    } else { /* unsupported, invalid BASE64 (hash_len < 0), or similar */
		slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, hasherrmsg, SHA1_SCHEME_NAME, dbpwd );
        goto loser;
    }

    /* SHA1 hash the user's key */
    if ( sha1_salted_hash( userhash, userpwd, &salt ) != SECSuccess ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "sha1_pw_cmp: SHA1_Hash() failed\n");
        goto loser;
    }

    /* the proof is in the comparison... */
    result = ( hash_len == DS40B1_SALTED_SHA_LENGTH ) ?
         ( memcmp( userhash, dbhash + 8, hash_len - 8 )) :
         ( memcmp( userhash, dbhash, SHA1_LENGTH ));

    loser:
    if ( dbhash && dbhash != quick_dbhash ) slapi_ch_free( (void**)&dbhash );
    return result;
}
 
 
char *
sha1_pw_enc( char *pwd )
{
    unsigned char   hash[ SHA1_LENGTH ];
    char        *enc;
 
    /* SHA1 hash the user's key */
    if ( sha1_salted_hash( hash, pwd, NULL ) != SECSuccess ) {
        return( NULL );
    }
 
    if (( enc = slapi_ch_malloc( 3 + SHA1_NAME_LEN +
        LDIF_BASE64_LEN( SHA1_LENGTH ))) == NULL ) {
        return( NULL );
    }
 
    sprintf( enc, "%c%s%c", PWD_HASH_PREFIX_START, SHA1_SCHEME_NAME,
        PWD_HASH_PREFIX_END );
    (void)ldif_base64_encode( hash, enc + 2 + SHA1_NAME_LEN,
        SHA1_LENGTH, -1 );
 
    return( enc );
}

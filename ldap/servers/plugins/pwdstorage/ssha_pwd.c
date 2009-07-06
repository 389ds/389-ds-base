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
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
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

#include "pwdstorage.h"
#include "prtime.h"
#include "prlong.h"

#include <pk11func.h>
#include <pk11pqg.h>

#define SHA_SALT_LENGTH    8   /* number of bytes of data in salt */

static void ssha_rand_array(void *randx, size_t len);


/* ***************************************************
	Identical function to slapi_rand_array in util.c, but can't use
	that here since this module is included in libds_admin, which doesn't
	link to libslapd.
   *************************************************** */
static void
ssha_rand_array(void *randx, size_t len)
{
    PK11_RandomUpdate(randx, len);
    PK11_GenerateRandom((unsigned char *)randx, (int)len);
}

SECStatus
sha_salted_hash(char *hash_out, const char *pwd, struct berval *salt, unsigned int secOID)
{
    PK11Context *ctx;
    unsigned int outLen;
    unsigned int shaLen;
    SECStatus rc;
                                                                                                                            
    switch (secOID) {
        case SEC_OID_SHA1:
            shaLen = SHA1_LENGTH;
            break;
        case SEC_OID_SHA256:
            shaLen = SHA256_LENGTH;
            break;
        case SEC_OID_SHA384:
            shaLen = SHA384_LENGTH;
            break;
        case SEC_OID_SHA512:
            shaLen = SHA512_LENGTH;
            break;
        default:
            /* An unknown secOID was passed in.  We shouldn't get here. */
            rc = SECFailure;
            return rc;
    }

    if (salt && salt->bv_len) {
        ctx = PK11_CreateDigestContext(secOID);
        if (ctx == NULL) {
            rc = SECFailure;
        } else {
            PK11_DigestBegin(ctx);
            PK11_DigestOp(ctx, (unsigned char*)pwd, strlen(pwd));
            PK11_DigestOp(ctx, (unsigned char*)(salt->bv_val), salt->bv_len);
            PK11_DigestFinal(ctx, (unsigned char*)hash_out, &outLen, shaLen);
            PK11_DestroyContext(ctx, 1);
            if (outLen == shaLen)
                rc = SECSuccess;
            else
                rc = SECFailure;
        }
    }
    else {
        /*backward compatibility*/
        rc = PK11_HashBuf(secOID, (unsigned char*)hash_out, (unsigned char *)pwd, strlen(pwd));
    }
                                                                                                                            
    return rc;
}

char *
salted_sha_pw_enc( const char *pwd, unsigned int shaLen )
{
    char hash[ MAX_SHA_HASH_SIZE + SHA_SALT_LENGTH ];
    char *salt = hash + shaLen;
    struct berval saltval;
    char *enc;
    char *schemeName;
    unsigned int schemeNameLen;
    unsigned int secOID;
                                                                                                                            
    /* Determine which algorithm we're using */
    switch (shaLen) {
        case SHA1_LENGTH:
            schemeName = SALTED_SHA1_SCHEME_NAME;
            schemeNameLen = SALTED_SHA1_NAME_LEN;
            secOID = SEC_OID_SHA1;
            break;
        case SHA256_LENGTH:
            schemeName = SALTED_SHA256_SCHEME_NAME;
            schemeNameLen = SALTED_SHA256_NAME_LEN;
            secOID = SEC_OID_SHA256;
            break;
        case SHA384_LENGTH:
            schemeName = SALTED_SHA384_SCHEME_NAME;
            schemeNameLen = SALTED_SHA384_NAME_LEN;
            secOID = SEC_OID_SHA384;
            break;
        case SHA512_LENGTH:
            schemeName = SALTED_SHA512_SCHEME_NAME;
            schemeNameLen = SALTED_SHA512_NAME_LEN;
            secOID = SEC_OID_SHA512;
            break;
        default:
            /* An unknown shaLen was passed in.  We shouldn't get here. */
            return( NULL );
    }
                                                                                                                            
    saltval.bv_val = (void*)salt;
    saltval.bv_len = SHA_SALT_LENGTH;
                                                                                                                            
    /* generate a new random salt */
        /* Note: the uninitialized salt array provides a little extra entropy
         * to the random array generation, but it is not really needed since
         * PK11_GenerateRandom takes care of seeding. In any case, it doesn't
         * hurt. */
        ssha_rand_array( salt, SHA_SALT_LENGTH );
                                                                                                                            
    /* hash the user's key */
    if ( sha_salted_hash( hash, pwd, &saltval, secOID ) != SECSuccess ) {
        return( NULL );
    }
                                                                                                                            
    if (( enc = slapi_ch_malloc( 3 + schemeNameLen +
        LDIF_BASE64_LEN(shaLen + SHA_SALT_LENGTH))) == NULL ) {
        return( NULL );
    }
                                                                                                                            
    sprintf( enc, "%c%s%c", PWD_HASH_PREFIX_START, schemeName,
        PWD_HASH_PREFIX_END );
    (void)PL_Base64Encode( hash, (shaLen + SHA_SALT_LENGTH), enc + 2 + schemeNameLen );
                                                                                                                            
    return( enc );
}

/*
 * Wrapper functions for password encoding
 */
char *
salted_sha1_pw_enc( const char *pwd )
{
    return salted_sha_pw_enc( pwd, SHA1_LENGTH );
}

char *
salted_sha256_pw_enc( const char *pwd )
{
    return salted_sha_pw_enc( pwd, SHA256_LENGTH );
}

char *
salted_sha384_pw_enc( const char *pwd )
{
    return salted_sha_pw_enc( pwd, SHA384_LENGTH );
}

char *
salted_sha512_pw_enc( const char *pwd )
{
    return salted_sha_pw_enc( pwd, SHA512_LENGTH );
}

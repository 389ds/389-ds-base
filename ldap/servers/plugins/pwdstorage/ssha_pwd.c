/** BEGIN COPYRIGHT BLOCK
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
#include "prtime.h"
#include "prlong.h"

#if defined(NET_SSL)
#include <pk11func.h>
#include <pk11pqg.h>
#endif /* NET_SSL */

#define SHA1_SALT_LENGTH    8   /* number of bytes of data in salt */

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

/*
 * A salted SHA1 hash
 * if salt is null, no salt is used (this is for backward compatibility)
*/
SECStatus
sha1_salted_hash(unsigned char *hash_out, char *pwd, struct berval *salt)
{
    PK11Context *ctx;
    unsigned int outLen;
    SECStatus rc;

    if (salt && salt->bv_len) {
        ctx = PK11_CreateDigestContext(SEC_OID_SHA1);
		if (ctx == NULL) {
			rc = SECFailure;
		}
		else {
        	PK11_DigestBegin(ctx);
        	PK11_DigestOp(ctx, (unsigned char*)pwd, strlen(pwd));
        	PK11_DigestOp(ctx, (unsigned char*)(salt->bv_val), salt->bv_len);
        	PK11_DigestFinal(ctx, hash_out, &outLen, SHA1_LENGTH);
        	PK11_DestroyContext(ctx, 1);
        	if (outLen == SHA1_LENGTH)
            	rc = SECSuccess;
        	else
            	rc = SECFailure;
		}
    }
    else {
        /*backward compatibility*/
        rc = PK11_HashBuf(SEC_OID_SHA1, hash_out, (unsigned char *)pwd, strlen(pwd));
    }

    return rc;
}

char *
salted_sha1_pw_enc( char *pwd )
{
    unsigned char hash[ SHA1_LENGTH + SHA1_SALT_LENGTH ];
    unsigned char *salt = hash + SHA1_LENGTH;
    struct berval saltval;
    char *enc;

    saltval.bv_val = (void*)salt;
    saltval.bv_len = SHA1_SALT_LENGTH;

    /* generate a new random salt */
	/* Note: the uninitialized salt array provides a little extra entropy
	 * to the random array generation, but it is not really needed since
	 * PK11_GenerateRandom takes care of seeding. In any case, it doesn't
	 * hurt. */
	ssha_rand_array( salt, SHA1_SALT_LENGTH );

    /* SHA1 hash the user's key */
    if ( sha1_salted_hash( hash, pwd, &saltval ) != SECSuccess ) {
        return( NULL );
    }

    if (( enc = PR_Malloc( 3 + SALTED_SHA1_NAME_LEN +
        LDIF_BASE64_LEN(sizeof(hash)))) == NULL ) {
        return( NULL );
    }

    sprintf( enc, "%c%s%c", PWD_HASH_PREFIX_START, SALTED_SHA1_SCHEME_NAME,
        PWD_HASH_PREFIX_END );
    (void)ldif_base64_encode( hash, enc + 2 + SALTED_SHA1_NAME_LEN,
        sizeof(hash), -1 );

    return( enc );
}


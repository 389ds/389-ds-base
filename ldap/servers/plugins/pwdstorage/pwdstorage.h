/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _PWDSTORAGE_H
#define _PWDSTORAGE_H

#include "slapi-plugin.h"
#include <ssl.h>
#include "nspr.h"
#include "ldif.h"
#include "md5.h"

#include <dirlite_strings.h> /* PLUGIN_MAGIC_VENDOR_STR */

#define PWD_HASH_PREFIX_START   '{'
#define PWD_HASH_PREFIX_END '}'

#define SHA1_SCHEME_NAME    "SHA"
#define SHA1_NAME_LEN       3
#define SALTED_SHA1_SCHEME_NAME "SSHA"
#define SALTED_SHA1_NAME_LEN        4
#define CRYPT_SCHEME_NAME   "crypt"
#define CRYPT_NAME_LEN      5
#define NS_MTA_MD5_SCHEME_NAME  "NS-MTA-MD5"
#define NS_MTA_MD5_NAME_LEN 10
#define CLEARTEXT_SCHEME_NAME "clear"
#define CLEARTEXT_NAME_LEN  5

SECStatus sha1_salted_hash(unsigned char *hash_out, char *pwd, struct berval *salt);
int sha1_pw_cmp( char *userpwd, char *dbpwd );
char * sha1_pw_enc( char *pwd );
char * salted_sha1_pw_enc( char *pwd );
int clear_pw_cmp( char *userpwd, char *dbpwd );
char *clear_pw_enc( char *pwd );
void crypt_init();
int crypt_pw_cmp( char *userpwd, char *dbpwd );
char *crypt_pw_enc( char *pwd );
int ns_mta_md5_pw_cmp( char *userpwd, char *dbpwd );


#if !defined(NET_SSL)
/******************************************/
/*
 * Some of the stuff below depends on a definition for uint32, so
 * we include one here.  Other definitions appear in nspr/prtypes.h,
 * at least.  All the platforms we support use 32-bit ints.
 */
typedef unsigned int uint32;


/******************************************/
/*
 * The following is from ds.h, which the libsec sec.h stuff depends on (see
 * comment below).
 */
/*
** A status code. Status's are used by procedures that return status
** values. Again the motivation is so that a compiler can generate
** warnings when return values are wrong. Correct testing of status codes:
**
**      DSStatus rv;
**      rv = some_function (some_argument);
**      if (rv != DSSuccess)
**              do_an_error_thing();
**
*/
typedef enum DSStatusEnum {
    DSWouldBlock = -2,
    DSFailure = -1,
    DSSuccess = 0
} DSStatus;
 
 
/******************************************/
/*
 * All of the SHA1-related defines are from libsec's "sec.h" -- including
 * it directly pulls in way too much stuff that we conflict with.  Ugh.
 */
 
/*
 * Number of bytes each hash algorithm produces
 */
#define SHA1_LENGTH     20
 
/******************************************/
/*
** SHA-1 secure hash function
*/
 
/*
** Hash a null terminated string "src" into "dest" using SHA-1
*/
DSStatus SHA1_Hash(unsigned char *dest, char *src);
 
#endif /* !defined(NET_SSL) */

#endif /* _PWDSTORAGE_H */

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

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

#ifndef _PWDSTORAGE_H
#define _PWDSTORAGE_H

#include "slapi-plugin.h"
#include <ssl.h>
#include "nspr.h"
#include "plbase64.h"
#include "ldif.h"
#include "md5.h"


#define PWD_HASH_PREFIX_START   '{'
#define PWD_HASH_PREFIX_END '}'

#define MAX_SHA_HASH_SIZE  HASH_LENGTH_MAX

#define SHA1_SCHEME_NAME    "SHA"
#define SHA1_NAME_LEN       3
#define SALTED_SHA1_SCHEME_NAME "SSHA"
#define SALTED_SHA1_NAME_LEN        4
#define SHA256_SCHEME_NAME        "SHA256"
#define SHA256_NAME_LEN           6
#define SALTED_SHA256_SCHEME_NAME "SSHA256"
#define SALTED_SHA256_NAME_LEN    7
#define SHA384_SCHEME_NAME        "SHA384"
#define SHA384_NAME_LEN           6
#define SALTED_SHA384_SCHEME_NAME "SSHA384"
#define SALTED_SHA384_NAME_LEN    7
#define SHA512_SCHEME_NAME        "SHA512"
#define SHA512_NAME_LEN           6
#define SALTED_SHA512_SCHEME_NAME "SSHA512"
#define SALTED_SHA512_NAME_LEN    7
#define CRYPT_SCHEME_NAME   "crypt"
#define CRYPT_NAME_LEN      5
#define NS_MTA_MD5_SCHEME_NAME  "NS-MTA-MD5"
#define NS_MTA_MD5_NAME_LEN 10
#define CLEARTEXT_SCHEME_NAME "clear"
#define CLEARTEXT_NAME_LEN  5
#define MD5_SCHEME_NAME "MD5"
#define MD5_NAME_LEN 3

SECStatus sha_salted_hash(char *hash_out, const char *pwd, struct berval *salt, unsigned int secOID);
int sha_pw_cmp( const char *userpwd, const char *dbpwd, unsigned int shaLen );
char * sha_pw_enc( const char *pwd, unsigned int shaLen );
char * salted_sha_pw_enc( const char *pwd, unsigned int shaLen );
int sha1_pw_cmp( const char *userpwd, const char *dbpwd );
char * sha1_pw_enc( const char *pwd );
char * salted_sha1_pw_enc( const char *pwd );
int sha256_pw_cmp( const char *userpwd, const char *dbpwd );
char * sha256_pw_enc( const char *pwd );
char * salted_sha256_pw_enc( const char *pwd );
int sha384_pw_cmp( const char *userpwd, const char *dbpwd );
char * sha384_pw_enc( const char *pwd );
char * salted_sha384_pw_enc( const char *pwd );
int sha512_pw_cmp( const char *userpwd, const char *dbpwd );
char * sha512_pw_enc( const char *pwd );
char * salted_sha512_pw_enc( const char *pwd );
int clear_pw_cmp( const char *userpwd, const char *dbpwd );
char *clear_pw_enc( const char *pwd );
#ifndef _WIN32
void crypt_init();
int crypt_pw_cmp( const char *userpwd, const char *dbpwd );
char *crypt_pw_enc( const char *pwd );
#endif
int ns_mta_md5_pw_cmp( const char *userpwd, const char *dbpwd );
int md5_pw_cmp( const char *userpwd, const char *dbpwd );
char *md5_pw_enc( const char *pwd );

#endif /* _PWDSTORAGE_H */

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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * MD5 Password Encryption/Comparison routines by David Irving, Fred Brittain,
 * and Aaron Gagnon --  University of Maine Farmington
 * Donated to the RedHat Directory Server Project 2005-06-10 
 */

#include <string.h>
#include <sys/types.h>
#include <stdio.h>
#include <pk11func.h>
#include <nss.h>
#include <nssb64.h>
#include "pwdstorage.h"

#define MD5_HASH_LEN 20
#define MD5_SUBSYSTEM_NAME "MD5 password hash"

int
md5_pw_cmp( char *userpwd, char *dbpwd )
{
   int rc=-1;
   char * bver;
   PK11Context *ctx=NULL;
   unsigned int outLen;
   unsigned char hash_out[MD5_HASH_LEN];
   unsigned char b2a_out[MD5_HASH_LEN*2]; /* conservative */
   SECItem binary_item;

   ctx = PK11_CreateDigestContext(SEC_OID_MD5);
   if (ctx == NULL) {
	   slapi_log_error(SLAPI_LOG_PLUGIN, MD5_SUBSYSTEM_NAME,
					   "Could not create context for digest operation for password compare");
	   goto loser;
   }

   /* create the hash */
   PK11_DigestBegin(ctx);
   PK11_DigestOp(ctx, userpwd, strlen(userpwd));
   PK11_DigestFinal(ctx, hash_out, &outLen, sizeof hash_out);
   PK11_DestroyContext(ctx, 1);

   /* convert the binary hash to base64 */
   binary_item.data = hash_out;
   binary_item.len = outLen;
   bver = NSSBase64_EncodeItem(NULL, b2a_out, sizeof b2a_out, &binary_item);
   /* bver points to b2a_out upon success */
   if (bver) {
	   rc = strcmp(bver,dbpwd);
   } else {
	   slapi_log_error(SLAPI_LOG_PLUGIN, MD5_SUBSYSTEM_NAME,
					   "Could not base64 encode hashed value for password compare");
   }
loser:
   return rc;
}

char *
md5_pw_enc( char *pwd )
{
   char * bver, *enc=NULL;
   PK11Context *ctx=NULL;
   unsigned int outLen;
   unsigned char hash_out[MD5_HASH_LEN];
   unsigned char b2a_out[MD5_HASH_LEN*2]; /* conservative */
   SECItem binary_item;

   ctx = PK11_CreateDigestContext(SEC_OID_MD5);
   if (ctx == NULL) {
	   slapi_log_error(SLAPI_LOG_PLUGIN, MD5_SUBSYSTEM_NAME,
					   "Could not create context for digest operation for password encoding");
	   return NULL;
   }

   /* create the hash */
   PK11_DigestBegin(ctx);
   PK11_DigestOp(ctx, pwd, strlen(pwd));
   PK11_DigestFinal(ctx, hash_out, &outLen, sizeof hash_out);
   PK11_DestroyContext(ctx, 1);

   /* convert the binary hash to base64 */
   binary_item.data = hash_out;
   binary_item.len = outLen;
   bver = NSSBase64_EncodeItem(NULL, b2a_out, sizeof b2a_out, &binary_item);
   if (bver) {
	   enc = slapi_ch_smprintf("%c%s%c%s", PWD_HASH_PREFIX_START, MD5_SCHEME_NAME,
							   PWD_HASH_PREFIX_END, bver );
   } else {
	   slapi_log_error(SLAPI_LOG_PLUGIN, MD5_SUBSYSTEM_NAME,
					   "Could not base64 encode hashed value for password encoding");
   }
	   
   return( enc );
}


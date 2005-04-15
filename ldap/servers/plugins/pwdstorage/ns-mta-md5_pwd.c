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

#include "md5.h" /* JCM - This is a core server header... These functions could be made part of the slapi API. */


/*
 * Netscape Mail Server MD5 support (compare-only; no support for encoding)
 */

static char * ns_mta_hextab = "0123456789abcdef";

static void
ns_mta_hexify(char *buffer, char *str, int len)
{
  char *pch = str;
  char ch;
  int i;

  for(i = 0;i < len; i ++) {
    ch = pch[i];
    buffer[2*i] = ns_mta_hextab[(ch>>4)&15];
    buffer[2*i+1] = ns_mta_hextab[ch&15];
  }
 
  return;
}

static char *
ns_mta_hash_alg(char *buffer, char *salt, char *passwd)
{
  mta_MD5_CTX context;
  char *saltstr;
  unsigned char digest[16];

	
	if ( (saltstr = slapi_ch_malloc(strlen(salt)*2 + strlen(passwd) + 3))
		== NULL ) {
		return( NULL );
	}

  sprintf(saltstr,"%s%c%s%c%s",salt,89,passwd,247,salt);

  mta_MD5Init(&context);
  mta_MD5Update(&context,(unsigned char *)saltstr,strlen(saltstr));
  mta_MD5Final(digest,&context);
  ns_mta_hexify(buffer,(char*)digest,16);
  buffer[32] = '\0';
  slapi_ch_free((void**)&saltstr);
  return(buffer);

}

int
ns_mta_md5_pw_cmp(char * clear, char *mangled)
{
  char mta_hash[33];
  char mta_salt[33];
  char buffer[65];

  strncpy(mta_hash,mangled,32);
  strncpy(mta_salt,&mangled[32],32);

  mta_hash[32] = mta_salt[32] = 0;

  return( strcmp(mta_hash,ns_mta_hash_alg(buffer,mta_salt,clear)));
}


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


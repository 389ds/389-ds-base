/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * Adjust password policy management related variables.
 * 
 * Valerie Chu
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include "ldap.h"
#include "ldif.h"
#include "sechash.h"
#include "dsalib.h"
#include "dsalib_pw.h"

extern char * salted_sha1_pw_enc(char *);

DS_EXPORT_SYMBOL char *
ds_salted_sha1_pw_enc (char* pwd)
{
	return( salted_sha1_pw_enc(pwd) );
}

/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _REVER_H
#define _REVER_H

#include "slapi-plugin.h"
#include "nspr.h"
#include "base64.h"
#include "slap.h"
#include "ldaplog.h"

#include <dirlite_strings.h> /* PLUGIN_MAGIC_VENDOR_STR */

#define REVER_SCHEME_NAME	"DES"
#define PWD_HASH_PREFIX_START   '{'
#define PWD_HASH_PREFIX_END '}'


int rever_cmp( char *userpwd, char *dbpwd );
char *rever_enc( char *pwd );
char *rever_dec( char *pwd );
int rever_init( Slapi_PBlock *pb );
void init_des_plugin();

int encode(char *inPlain, char ** outCipher);
int decode(char *inCipher, char **outPlain);

char *migrateCredentials(char *oldpath, char *newpath, char *oldcred);
typedef char *(*migrate_fn_type)(char *, char *, char *);

#endif

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef _REVER_H
#define _REVER_H

#include "slapi-plugin.h"
#include "nspr.h"
#include "base64.h"
#include "slap.h"
#include <plbase64.h>

#define AES_MECH 1
#define DES_MECH 2
#define AES_REVER_SCHEME_NAME "AES"
#define DES_REVER_SCHEME_NAME "DES"
#define PWD_HASH_PREFIX_START '{'
#define PWD_HASH_PREFIX_END '}'


int rever_cmp(char *userpwd, char *dbpwd);
char *rever_enc(char *pwd);
char *rever_dec(char *pwd);
int rever_init(Slapi_PBlock *pb);
void init_pbe_plugin(void);

int encode(char *inPlain, char **outCipher, int mech);
int decode(char *inCipher, char **outPlain, int mech, char *algid);

char *migrateCredentials(char *oldpath, char *newpath, char *oldcred);
typedef char *(*migrate_fn_type)(char *, char *, char *);

#endif

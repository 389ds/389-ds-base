/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2010/ Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef _CLCRYPT_H_
#define _CLCRYPT_H_

#include "pk11func.h"
#include "keyhi.h"
#include "nss.h"
#include "cert.h"

int clcrypt_init(const CL5DBConfig *config, void **clcrypt_handle);
int clcrypt_destroy(void *clcrypt_handle);
int clcrypt_encrypt_value(void *clcrypt_handle, struct berval *in, struct berval **out);
int clcrypt_decrypt_value(void *state_priv, struct berval *in, struct berval **out);
#endif /* _CLCRYPT_H_ */

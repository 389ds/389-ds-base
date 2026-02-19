/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* Private structures and #defines used in the attribute encryption code. */

#ifndef _ATTRCRYPT_H_
#define _ATTRCRYPT_H_

/* structure which holds our stuff in the attrinfo objects */
struct attrcrypt_private
{
    int attrcrypt_cipher;
};

typedef struct _attrcrypt_cipher_entry
{
    int cipher_number;
    char *cipher_display_name;
    CK_MECHANISM_TYPE cipher_mechanism;
    CK_MECHANISM_TYPE wrap_mechanism;
    CK_MECHANISM_TYPE key_gen_mechanism;
    int key_size;
    int iv_length;
} attrcrypt_cipher_entry;

extern attrcrypt_cipher_entry attrcrypt_cipher_list[];

/* The ciphers we support (used in attrcrypt_cipher above) */
#define ATTRCRYPT_CIPHER_AES 1
#define ATTRCRYPT_CIPHER_DES3 2

#endif /* _ATTRCRYPT_H_ */

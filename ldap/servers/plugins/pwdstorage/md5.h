/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * MD5 algorithm used by Netscape Mail Server
 */

/* MD5 code taken from reference implementation published in RFC 1321 */

#ifndef _RFC1321_MD5_H_
#define _RFC1321_MD5_H_

/* Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
   rights reserved.

   License to copy and use this software is granted provided that it
   is identified as the "RSA Data Security, Inc. MD5 Message-Digest
   Algorithm" in all material mentioning or referencing this software
   or this function.

   License is also granted to make and use derivative works provided
   that such works are identified as "derived from the RSA Data
   Security, Inc. MD5 Message-Digest Algorithm" in all material
   mentioning or referencing the derived work.

   RSA Data Security, Inc. makes no representations concerning either
   the merchantability of this software or the suitability of this
   software for any particular purpose. It is provided "as is"
   without express or implied warranty of any kind.

   These notices must be retained in any copies of any part of this
   documentation and/or software.
 */

#include "nspr.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef unsigned char      * POINTER;
typedef PRUint16              UINT2;
typedef PRUint32              UINT4;

/* MD5 context. */
typedef struct {
  UINT4 state[4];                                   /* state (ABCD) */
  UINT4 count[2];        /* number of bits, modulo 2^64 (lsb first) */
  unsigned char buffer[64];                         /* input buffer */
} mta_MD5_CTX;

void mta_MD5Init   (mta_MD5_CTX *);
void mta_MD5Update (mta_MD5_CTX *, const unsigned char *, unsigned int);
void mta_MD5Final  (unsigned char [16], mta_MD5_CTX *);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* end of _RFC1321_MD5_H_ */


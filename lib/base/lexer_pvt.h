/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __LEXER_PVT_H
#define __LEXER_PVT_H

#ifndef _POOL_H_
#include "base/pool.h"
#endif /* _POOL_H_ */

typedef struct LEXClassTab_s LEXClassTab_t;
struct LEXClassTab_s {
    int lct_classc;			/* number of character classes */
    int lct_bvbytes;			/* number of bytes per bit vector */
    unsigned char * lct_bv;		/* pointer to bit vector area */
};

typedef struct LEXToken_s LEXToken_t;
struct LEXToken_s {
    char * lt_buf;			/* token buffer pointer */
    int lt_len;				/* length of token data */
    int lt_buflen;			/* current length of buffer */
    int lt_inclen;                      /* buffer length increment */
    int lt_initlen;                     /* initial length of token buffer */
    pool_handle_t * lt_mempool;         /* token memory pool */
};

#endif /* __LEXER_PVT_H */

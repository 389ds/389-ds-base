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

#ifndef __LEXER_PVT_H
#define __LEXER_PVT_H

#ifndef _POOL_H_
#include "base/pool.h"
#endif /* _POOL_H_ */

typedef struct LEXClassTab_s LEXClassTab_t;
struct LEXClassTab_s
{
    int lct_classc;        /* number of character classes */
    int lct_bvbytes;       /* number of bytes per bit vector */
    unsigned char *lct_bv; /* pointer to bit vector area */
};

typedef struct LEXToken_s LEXToken_t;
struct LEXToken_s
{
    char *lt_buf;              /* token buffer pointer */
    int lt_len;                /* length of token data */
    int lt_buflen;             /* current length of buffer */
    int lt_inclen;             /* buffer length increment */
    int lt_initlen;            /* initial length of token buffer */
    pool_handle_t *lt_mempool; /* token memory pool */
};

#endif /* __LEXER_PVT_H */

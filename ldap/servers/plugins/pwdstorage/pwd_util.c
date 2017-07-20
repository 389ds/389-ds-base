/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "pwdstorage.h"

/*
 * Utility functions for the Password Storage Scheme plugins.
 */

/*
 * calculate the number of bytes the base64 encoded encval
 * will have when decoded, taking into account padding
 */
PRUint32
pwdstorage_base64_decode_len(const char *encval, PRUint32 enclen)
{
    PRUint32 len = enclen;

    if (len == 0) {
        len = strlen(encval);
    }
    if (len && (0 == (len & 3))) {
        if ('=' == encval[len - 1]) {
            if ('=' == encval[len - 2]) {
                len -= 2;
            } else {
                len -= 1;
            }
        }
    }

    return ((len * 3) / 4);
}

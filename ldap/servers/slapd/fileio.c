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

/* fileio.c - layer to adjust EOL to use DOS format via PR_Read/Write on NT */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/param.h>
#include <unistd.h>
#include <pwd.h>
#include "slap.h"
#include "pw.h"
#include <prio.h>

PRInt32
slapi_read_buffer(PRFileDesc *fd, void *buf, PRInt32 amount)
{
    PRInt32 rval = 0;

    rval = PR_Read(fd, buf, amount);

    return rval;
}

/*
 * slapi_write_buffer -- same as PR_Write
 *                       except '\r' is added before '\n'.
 * Return value: written bytes not including '\r' characters.
 */
PRInt32
slapi_write_buffer(PRFileDesc *fd, void *buf, PRInt32 amount)
{
    PRInt32 rval = 0;

    rval = PR_Write(fd, buf, amount);

    return rval;
}

/*
 * This function renames a file to a new name.  Unlike PR_Rename or NT rename, this
 * function can be used if the destfilename exists, and it will overwrite the dest
 * file name
 */
int
slapi_destructive_rename(const char *srcfilename, const char *destfilename)
{
    int rv = 0;

    if (rename(srcfilename, destfilename) < 0) {
        rv = errno;
    }

    return rv;
}

/*
 * This function copies the source into the dest
 */
int
slapi_copy(const char *srcfilename, const char *destfilename)
{
    int rv = 0;

    unlink(destfilename);
    if (link(srcfilename, destfilename) < 0) {
        rv = errno;
    }

    return rv;
}

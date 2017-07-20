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

/* lock.c - routines to open and apply an advisory lock to a file */

#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/file.h>
#include <sys/socket.h>
#include "slap.h"
#ifdef USE_LOCKF
#include <unistd.h>
#endif

FILE *
lock_fopen(char *fname, char *type, FILE **lfp)
{
    FILE *fp;
    char buf[MAXPATHLEN];

    /* open the lock file */
    PR_snprintf(buf, MAXPATHLEN, "%s%s", fname, ".lock");
    if ((*lfp = fopen(buf, "w")) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "lock_fopen - Could not open \"%s\"\n", buf, 0, 0);
        return (NULL);
    }

/* acquire the lock */
#ifdef USE_LOCKF
    while (lockf(fileno(*lfp), F_LOCK, 0) != 0) {
#else
    while (flock(fileno(*lfp), LOCK_EX) != 0) {
#endif
        ; /* NULL */
    }

    /* open the log file */
    if ((fp = fopen(fname, type)) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "lock_fopen - Could not open \"%s\"\n", fname, 0, 0);
#ifdef USE_LOCKF
        lockf(fileno(*lfp), F_ULOCK, 0);
#else
        flock(fileno(*lfp), LOCK_UN);
#endif
        return (NULL);
    }

    return (fp);
}

int
lock_fclose(FILE *fp, FILE *lfp)
{
/* unlock */
#ifdef USE_LOCKF
    lockf(fileno(lfp), F_ULOCK, 0);
#else
    flock(fileno(lfp), LOCK_UN);
#endif
    fclose(lfp);

    return (fclose(fp));
}

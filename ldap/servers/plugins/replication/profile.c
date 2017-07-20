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


#include "slapi-plugin.h"


/* module: provide an interface to the profile file */

static FILE *profile_fd = NULL;

/* JCMREPL - Could build up in an AVL tree and dump out to disk at the end... */

void
profile_log(char *file, int line)
{
    if (profile_fd == NULL)
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "profile_log: profile file not open.");
    else {
        /* JCMREPL - Probably need a lock around here */
        fprintf(profile_fd, "%s %d\n", file, line);
    }
}

void
profile_open()
{
    char filename[MAX_FILENAME];
    PR_snprintf(filename, MAX_FILENAME, "%s%s", CFG_rootpath, CFG_profilefile);
    profile_fd = textfile_open(filename, "a");
}

void
profile_close()
{
    if (profile_fd == NULL)
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "profile_close: profile file not open.");
    else
        textfile_close(profile_fd);
}

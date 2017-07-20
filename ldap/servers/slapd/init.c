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

/* init.c - initialize various things */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "slap.h"
#include "fe.h"

void
slapd_init()
{
    /* We don't worry about free'ing this stuff
         * since the only time we want to do that is when
         * the process is exiting. */
    num_conns = slapi_counter_new();
    g_set_current_conn_count_mutex(PR_NewLock());

    if (g_get_current_conn_count_mutex() == NULL) {
        slapi_log_err(SLAPI_LOG_CRIT,
                      "slapd_init", "PR_NewLock failed\n");
        exit(-1);
    }

    /* Add PSEUDO_ATTR_UNHASHEDUSERPASSWORD to the protected attribute list */
    set_attr_to_protected_list(PSEUDO_ATTR_UNHASHEDUSERPASSWORD, 0);
#ifndef HAVE_TIME_R
    if ((time_func_mutex = PR_NewLock()) == NULL) {
        slapi_log_err(SLAPI_LOG_CRIT,
                      "slapd_init", "PR_NewLock failed\n");
        exit(-1);
    }

#endif /* HAVE_TIME_R */
}

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

/*
 *  Copyright (c) 1996 Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  SLAPD globals.c -- SLAPD library global variables
 */

#include "ldap.h"
#include <sslproto.h> /* cipher suite names */

#undef OFF
#undef LITTLE_ENDIAN

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "slap.h"
#include "fe.h"

int should_detach = 0;
time_t starttime;
PRThread *listener_tid;
Slapi_PBlock *repl_pb = NULL;

/*
 * global variables that need mutex protection
 */
Slapi_Counter *num_conns;
Slapi_Counter *max_threads_count;
Slapi_Counter *conns_in_maxthreads;
Connection_Table *the_connection_table = NULL;

char *pid_file = "/dev/null";
char *start_pid_file = NULL;

char *attr_dataversion = ATTR_DATAVERSION;

extern void set_dll_entry_points(slapdEntryPoints *sep);
void
set_entry_points()
{
    slapdEntryPoints *sep;

    sep = (slapdEntryPoints *)slapi_ch_malloc(sizeof(slapdEntryPoints));
    sep->sep_ps_wakeup_all = (caddr_t)ps_wakeup_all;
    sep->sep_ps_service = (caddr_t)ps_service_persistent_searches;
    sep->sep_disconnect_server = (caddr_t)disconnect_server;
    sep->sep_slapd_ssl_init = (caddr_t)slapd_ssl_init;
    sep->sep_slapd_ssl_init2 = (caddr_t)slapd_ssl_init2;
    set_dll_entry_points(sep);

    /* To apply the nsslapd-counters config value properly,
       these values are initialized here after config file is read */
    if (config_get_slapi_counters()) {
        max_threads_count = slapi_counter_new();
        conns_in_maxthreads = slapi_counter_new();
    } else {
        max_threads_count = NULL;
        conns_in_maxthreads = NULL;
    }
}

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * net.c: sockets abstraction and DNS related things
 * 
 * Note: sockets created with net_socket are placed in non-blocking mode,
 *       however this API simulates that the calls are blocking.
 *
 * Rob McCool
 */


#include "netsite.h"
#include <nspr.h>

#include "util.h"
#include <string.h>
#include <arpa/inet.h>  /* inet_ntoa */
#include <netdb.h>      /* hostent stuff */
#ifdef NEED_GHN_PROTO
extern "C" int gethostname (char *name, size_t namelen);
#endif
#ifdef LINUX
#include <sys/ioctl.h> /* ioctl */
#endif

#include "libadmin/libadmin.h"

/* ---------------------------- util_hostname ----------------------------- */


#include <sys/param.h>

/* Defined in dns.cpp */
char *net_find_fqdn(PRHostEnt *p);

NSAPI_PUBLIC char *util_hostname(void)
{
    char str[MAXHOSTNAMELEN];
    PRHostEnt   hent;
    char        buf[PR_NETDB_BUF_SIZE];
    PRStatus    err;

    gethostname(str, MAXHOSTNAMELEN);
    err = PR_GetHostByName(
                str,
                buf,
                PR_NETDB_BUF_SIZE,
                &hent);

    if (err == PR_FAILURE) 
        return NULL;
    return net_find_fqdn(&hent);
}


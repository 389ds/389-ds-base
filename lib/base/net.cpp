/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
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
#ifdef XP_UNIX
#include <arpa/inet.h>  /* inet_ntoa */
#include <netdb.h>      /* hostent stuff */
#ifdef NEED_GHN_PROTO
extern "C" int gethostname (char *name, size_t namelen);
#endif
#endif /* XP_UNIX */
#ifdef LINUX
#include <sys/ioctl.h> /* ioctl */
#endif

#include "libadmin/libadmin.h"

/* ---------------------------- util_hostname ----------------------------- */


#ifdef XP_UNIX
#include <sys/param.h>
#else /* WIN32 */
#define MAXHOSTNAMELEN 255
#endif /* XP_UNIX */

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


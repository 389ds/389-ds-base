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

/* stubrepl.c - stubs of functions required for the evil stuff in the tools directory  */

#include "slap.h"

int connection_type = -1;

void
ps_service_persistent_searches(Slapi_Entry *e, Slapi_Entry *eprev, ber_int_t chgtype, ber_int_t chgnum)
{
}

void
ps_wakeup_all(void)
{
}

int
slapd_ssl_init()
{
    return (-1);
}

int
slapd_ssl_init2(PRFileDesc **fd, int startTLS)
{
    return (-1);
}

void
connection_abandon_operations(Connection *conn)
{
}

void
disconnect_server(Connection *conn, PRUint64 opconnid, int opid, PRErrorCode reason, PRInt32 error)
{
}

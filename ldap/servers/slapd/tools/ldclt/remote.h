/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/*
        FILE :        remote.h
        AUTHOR :      Jean-Luc SCHWING
        VERSION :     1.0
        DATE :        04 May 1999
        DESCRIPTION :
            This file contains the definitions used by the remote
            control module of ldclt.
*/

/*
 * Network includes
 */

#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

typedef struct
{
    uint32_t type, res, dnSize;
    char dn[sizeof(uint32_t)];
} repconfirm;

extern int supplierPort;

/* End of file */

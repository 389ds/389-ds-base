#ident "ldclt @(#)workarounds.c    1.5 00/12/01"

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2006 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/*
        FILE :        workarounds.c
        AUTHOR :        Jean-Luc SCHWING
        VERSION :       1.0
        DATE :        15 December 1998
        DESCRIPTION :
            This file contains special work-arounds targeted to
            fix, or work-around, the various bugs that may appear
            in Solaris 2.7 libldap.
     LOCAL :        None.
        HISTORY :
---------+--------------+------------------------------------------------------
dd/mm/yy | Author    | Comments
---------+--------------+------------------------------------------------------
15/12/98 | JL Schwing    | Creation
---------+--------------+------------------------------------------------------
19/09/00 | JL Schwing    | 1.2: Port on Netscape's libldap. This is realized in
            |   such a way that this library become the default
            |   way so a ifdef for Solaris will be used...
---------+--------------+------------------------------------------------------
16/11/00 | JL Schwing    | 1.3 : lint cleanup.
-----------------------------------------------------------------------------
29/11/00 | JL Schwing    | 1.4 : Port on NT 4.
---------+--------------+------------------------------------------------------
01/12/00 | JL Schwing    | 1.5 : Port on Linux.
---------+--------------+------------------------------------------------------
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>    /* exit(), ... */ /*JLS 16-11-00*/
#include "lber.h"
#include "ldap.h"
#ifdef SOLARIS_LIBLDAP /*JLS 19-09-00*/
#include "ldap-private.h"
#else                                           /*JLS 19-09-00*/
#include <pthread.h>                            /*JLS 01-12-00*/
#include "port.h" /* Portability definitions */ /*JLS 29-11-00*/
#include "ldclt.h"                              /*JLS 19-09-00*/
#endif                                          /*JLS 19-09-00*/


/* ****************************************************************************
    FUNCTION :    getFdFromLdapSession
    PURPOSE :    This function is a work-around for the bug 4197228
            that is not expected to be fixed soon...
    INPUT :        ld    = ldap session to process.
    OUTPUT :    fd    = the corresponding fd.
    RETURN :    -1 if error, 0 else.
    DESCRIPTION :
 *****************************************************************************/
int
getFdFromLdapSession(
#ifdef SOLARIS_LIBLDAP
    LDAP *ld,
    int *fd
#else
    LDAP *ld __attribute__((unused)),
    int *fd __attribute__((unused))
#endif
    )
{
#ifdef SOLARIS_LIBLDAP /*JLS 19-09-00*/
    *fd = ld->ld_sb.sb_sd;
    return (0);
#else  /*JLS 19-09-00*/
    printf("Error : getFdFromLdapSession() not implemented...\n"); /*JLS 19-09-00*/
    exit(EXIT_OTHER); /*JLS 19-09-00*/
#endif /*JLS 19-09-00*/
}


/* End of file */

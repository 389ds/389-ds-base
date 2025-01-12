#ident "ldclt @(#)utils.h    1.3 01/01/11"

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
#include <sys/types.h>


/*
    FILE :        utils.h
    AUTHOR :        Jean-Luc SCHWING
    VERSION :       1.0
    DATE :        14 November 2000
    DESCRIPTION :
            This files contians the prototypes and other
            definitions related to utils.c, utilities functions
            that will be used as well by ldclt and by the genldif
            command.
    LOCAL :        None.
    HISTORY :
---------+--------------+------------------------------------------------------
dd/mm/yy | Author    | Comments
---------+--------------+------------------------------------------------------
14/11/00 | JL Schwing    | Creation
---------+--------------+------------------------------------------------------
16/11/00 | JL Schwing    | 1.2 : Fix typo.
---------+--------------+------------------------------------------------------
11/01/01 | JL Schwing    | 1.3 : Add new function rndlim().
---------+--------------+------------------------------------------------------
*/


/*
 * Functions exported by utils.c
 */
extern void rnd(char *buf, int low, int high, int ndigits);
extern int rndlim(int low, int high);
extern void rndstr(char *buf, int ndigits);
extern int utilsInit(void);
extern int incr_and_wrap(int val, int min, int max, int incr);
void *safe_malloc(size_t datalen);


/* End of file */

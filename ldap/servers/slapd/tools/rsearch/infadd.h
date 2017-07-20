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


#ifndef _INFADD_H
#define _INFADD_H

#include "nametable.h"

/* global data for the threads to share */
extern char *hostname;
extern PRUint16 port;
extern char *suffix;
extern char *username;
extern char *password;
extern PRUint32 blobsize;
extern NameTable *given_names;
extern NameTable *family_names;
extern int noDelay;

#endif

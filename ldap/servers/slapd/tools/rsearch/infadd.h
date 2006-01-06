/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

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

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2006 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#ifndef _RSEARCH_H
#define _RSEARCH_H

typedef enum { op_search, op_modify, op_idxmodify, op_add, op_delete, op_compare } Operation;
#include "nametable.h"
#include "sdattable.h"

/* global data for the threads to share */
extern char *hostname;
extern int port;
extern int numeric;
extern int searchTimelimit;
/**/ extern int threadCount;
/**/ extern int verbose;
/**/ extern int logging;
extern int doBind;
extern int setLinger;
/**/ extern int cool;
/**/ extern int quiet;
extern int noDelay;
extern int noUnBind;
extern int noOp;
extern int myScope;
extern char *suffix;
extern char *filter;
/**/ extern char *nameFile;
extern char *bindDN;
extern char *bindPW;
extern char *userPW;
extern char *uidFilter;
extern char **attrToReturn;
/**/ extern char *attrList;
extern Operation opType;
extern NameTable *ntable;
extern NameTable *attrTable;
extern SDatTable *sdattable;
/**/ extern int sampleInterval;
extern int reconnect;
extern int useBFile;

#endif

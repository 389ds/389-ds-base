/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef _RSEARCH_H
#define _RSEARCH_H

typedef enum { op_search, op_modify, op_idxmodify, op_add, op_delete, op_compare } Operation;
#include "nametable.h"
#include "sdattable.h"

/* global data for the threads to share */
extern char *hostname;
extern int port;
extern int numeric;
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

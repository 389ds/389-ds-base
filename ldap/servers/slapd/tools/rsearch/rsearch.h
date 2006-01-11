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
 * Copyright (C) 2006 Red Hat, Inc.
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

/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Internal functions for scanner.
 */


#ifndef ACLSCAN_H
#define ACLSCAN_H

NSPR_BEGIN_EXTERN_C 

extern int acl_InitScanner(NSErr_t *errp, char *filename, char *buffer);
extern int acl_EndScanner(void);
extern void aclerror(const char *s); 
extern int acllex(void); 

NSPR_END_EXTERN_C

#endif


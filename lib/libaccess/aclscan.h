/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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


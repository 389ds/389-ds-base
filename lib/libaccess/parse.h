/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * module private routines for handling the yacc based
 * ACL Parser.
 */

#ifndef PARSE_H
#define PARSE_H

NSPR_BEGIN_EXTERN_C 

extern int acl_PushListHandle(ACLListHandle_t *handle);
extern int acl_Parse(void);

NSPR_END_EXTERN_C

#endif

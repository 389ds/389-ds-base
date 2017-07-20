/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

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

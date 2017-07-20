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


#ifndef LDAPU_AUTH_H
#define LDAPU_AUTH_H

#include <ldap.h>

#ifndef NSAPI_PUBLIC
#define NSAPI_PUBLIC
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern int ldapu_find(LDAP *ld, const char *base, int scope, const char *filter, const char **attrs, int attrsonly, LDAPMessage **res);

int ldapu_find_entire_tree(LDAP *ld, int scope, const char *filter, const char **attrs, int attrsonly, LDAPMessage ***res);

#ifdef __cplusplus
}
#endif

#endif /* LDAPU_AUTH_H */

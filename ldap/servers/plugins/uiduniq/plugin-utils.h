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

/***********************************************************************
**
** NAME
**  plugin-utils.h
**
** DESCRIPTION
**
**
** AUTHOR
**   <rweltman@netscape.com>
**
***********************************************************************/

#ifndef _PLUGIN_UTILS_H_
#define _PLUGIN_UTILS_H_

/***********************************************************************
** Includes
***********************************************************************/

#include <slapi-plugin.h>
/*
 * slapi-plugin-compat4.h is needed because we use the following deprecated
 * functions:
 *
 * slapi_search_internal()
 * slapi_modify_internal()
 */
#include "slapi-plugin-compat4.h"
#include <stdio.h>
#include <string.h>
#include <slapi-private.h>

#ifdef LDAP_ERROR_LOGGING
#ifndef DEBUG
#define DEBUG
#endif
#endif

#define BEGIN do {
#define END   \
    }         \
    while (0) \
        ;

int op_error(int internal_error);
Slapi_PBlock *readPblockAndEntry(Slapi_DN *baseDN, const char *filter, char *attrs[]);
int entryHasObjectClass(Slapi_PBlock *pb, Slapi_Entry *e, const char *objectClass);
Slapi_PBlock *dnHasObjectClass(Slapi_DN *baseDN, const char *objectClass);
Slapi_PBlock *dnHasAttribute(const char *baseDN, const char *attrName);

#endif /* _PLUGIN_UTILS_H_ */

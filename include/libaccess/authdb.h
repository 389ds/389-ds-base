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

#ifndef AUTHDB_H
#define AUTHDB_H

#include <base/plist.h>
#include <libaccess/nserror.h>
#include <libaccess/las.h>

#define URL_PREFIX_LDAP "ldap"
#define URL_PREFIX_LDAP_LEN 4

typedef struct
{
    char *dbname;
    ACLDbType_t dbtype;
    void *dbinfo;
} AuthdbInfo_t;

extern int acl_num_databases();

#endif /* AUTHDB_H */

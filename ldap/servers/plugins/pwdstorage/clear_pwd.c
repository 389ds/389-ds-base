/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * slapd hashed password routines
 *
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "pwdstorage.h"

int
clear_pw_cmp( const char *userpwd, const char *dbpwd )
{
    return( strcmp( userpwd, dbpwd ));
}

char *
clear_pw_enc( const char *pwd )
{
    /* Just return NULL if pwd is NULL */
    if (!pwd)
        return NULL;

    /* If the modify operation specified the "{clear}" storage scheme
     * prefix, we should strip it off.
     */
    if ((*pwd == PWD_HASH_PREFIX_START) && (pwd == PL_strcasestr( pwd, "{clear}" ))) {
        return( slapi_ch_strdup( pwd + 7 ));
    } else {
        return( slapi_ch_strdup( pwd ));
    }
}

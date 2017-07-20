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
 * slapd hashed password routines
 *
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "pwdstorage.h"

int
clear_pw_cmp(const char *userpwd, const char *dbpwd)
{
    int result = 0;
    int len_user = strlen(userpwd);
    int len_dbp = strlen(dbpwd);
    if (len_user != len_dbp) {
        result = 1;
    }
    /* We have to do this comparison ANYWAY else we have a length timing attack. */
    if (len_user >= len_dbp) {
        /*
         * If they are the same length, result will be 0 here, and if we pass
         * the check, we don't update result either. IE we pass.
         * However, even if the first part of userpw matches dbpwd, but len !=, we
         * have already failed anyawy. This prevents substring matching.
         */
        if (slapi_ct_memcmp(userpwd, dbpwd, len_dbp) != 0) {
            result = 1;
        }
    } else {
        /*
         * If we stretched the userPassword, we'll allow a new timing attack, where
         * if we see a delay on a short pw, we know we are stretching.
         * when the delay goes away, it means we've found the length.
         * Instead, because we don't want to use the short pw for comp, we just compare
         * dbpwd to itself. We have already got result == 1 if we are here, so we are
         * just trying to take up time!
         */
        if (slapi_ct_memcmp(dbpwd, dbpwd, len_dbp)) {
            /* Do nothing, we have the if to fix a coverity check. */
        }
    }
    return result;
}

char *
clear_pw_enc(const char *pwd)
{
    /* Just return NULL if pwd is NULL */
    if (!pwd)
        return NULL;

    /* If the modify operation specified the "{clear}" storage scheme
     * prefix, we should strip it off.
     */
    if ((*pwd == PWD_HASH_PREFIX_START) && (pwd == PL_strcasestr(pwd, "{clear}"))) {
        return (slapi_ch_strdup(pwd + 7));
    } else {
        return (slapi_ch_strdup(pwd));
    }
}

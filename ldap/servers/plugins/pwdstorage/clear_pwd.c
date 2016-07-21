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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
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
    int result = 0;
    int len = 0;
    int len_user = strlen(userpwd);
    int len_dbp = strlen(dbpwd);
    if ( len_user != len_dbp ) {
        result = 1;
    }
    /* We have to do this comparison ANYWAY else we have a length timing attack. */
    if ( len_user >= len_dbp ) {
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

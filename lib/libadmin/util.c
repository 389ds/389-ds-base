/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * util.c:  Miscellaneous stuffs
 *
 * All blame to Mike McCool
 */

#include "libadmin/libadmin.h"
#include "base/util.h"
#include "private/pprio.h"

#include <base/file.h>
#include <dirent.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>


SYS_FILE lf;


char *
get_flock_path(void)
{
    char *result = "";
    char *port = getenv("SERVER_PORT");
    if (port == NULL) {
        port = "";
    }
    result = (char *)MALLOC(strlen("/tmp/lock.%%s.") + strlen(port) + 4);
    if (result != NULL) {
        sprintf(result, "/tmp/lock.%%s.%s", port);
    }
    return result;
}

char *
alert_word_wrap(char *str, int width, char *linefeed)
{
    char *ans = NULL;
    int counter = 0;
    int lsc = 0, lsa = 0;
    int strc = 0, ansc = 0;
    int x = 0;

    /* assume worst case */
    ans = (char *)MALLOC((strlen(str) * strlen(linefeed)) + 32);
    if (ans == NULL) {
        return NULL;
    }

    for (strc = 0, ansc = 0; str[strc]; /*none*/) {
        if (str[strc] == '\n') {
            counter = 0;
            lsc = 0, lsa = 0;
            for (x = 0; linefeed[x]; x++) {
                ans[ansc++] = linefeed[x];
            }
            strc++;
        } else if (str[strc] == '\r') {
            strc++;
        } else if (str[strc] == '\\') {
            ans[ansc++] = '\\';
            ans[ansc++] = strc++;
        } else {
            if (counter == width) {
                if (lsc && lsa) {
                    strc = lsc;
                    ansc = lsa;

                    counter = 0;
                    lsc = 0, lsa = 0;
                    for (x = 0; linefeed[x]; x++) {
                        ans[ansc++] = linefeed[x];
                    }
                    strc++;
                } else {
                    /* else, you're a loser, I'm breaking your big word anyway */
                    counter = 0;
                    lsc = 0, lsa = 0;
                    for (x = 0; linefeed[x]; x++) {
                        ans[ansc++] = linefeed[x];
                    }
                    strc++;
                }
            } else {
                if (str[strc] == ' ') {
                    lsc = strc;
                    lsa = ansc;
                }
                ans[ansc++] = str[strc++];
                counter++;
            }
        }
    }
    ans[ansc] = '\0';
    return ans;
}

/***********************************************************************
** FUNCTION:    cookieValue
** DESCRIPTION:
**   Get the current value of the cookie variable
** INPUTS:    var - the name of the cookie variable
**        val - if non-NULL, set the in-memory copy of the var
** OUTPUTS:    None
** RETURN:    NULL if the var doesn't exist, else the value
** SIDE EFFECTS:
**    Eats memory
** RESTRICTIONS:
**    Don't screw around with the returned string, if anything else wants
**    to use it.
** MEMORY:    This is a memory leak, so only use it in CGIs
** ALGORITHM:
**    If it's never been called, build a memory structure of the
**    cookie variables.
**    Look for the passed variable, and return its value, or NULL
***********************************************************************/

NSAPI_PUBLIC char *
cookieValue(char *var, char *val)
{
    static char **vars = NULL;
    static char **vals = NULL;
    static int numVars = -1;
    int i;

    if (numVars == -1) { /* first time, init the structure */
        char *cookie = getenv("HTTP_COOKIE");

        if (cookie && *cookie) {
            int len = strlen(cookie);
            int foundVal = 0;

            cookie = STRDUP(cookie);
            numVars = 0;
            vars = (char **)MALLOC(sizeof(char *));
            if (vars == NULL) {
                return NULL;
            }
            vals = (char **)MALLOC(sizeof(char *));
            if (vals == NULL) {
                FREE(vars);
                return NULL;
            }

            vars[0] = cookie;
            for (i = 0; i < len; ++i) {
                if ((!foundVal) && (cookie[i] == '=')) {
                    vals[numVars++] = cookie + i + 1;
                    cookie[i] = '\0';
                    foundVal = 1;
                } else if ((cookie[i] == ';') && (cookie[i + 1] == ' ')) {
                    cookie[i] = '\0';
                    vals = (char **)REALLOC(vals,
                                            sizeof(char *) * (numVars + 1));
                    vars = (char **)REALLOC(vars,
                                            sizeof(char *) * (numVars + 1));
                    vars[numVars] = cookie + i + 2;
                    i += 2;
                    foundVal = 0;
                }
            }
        } else { /* no cookie, no vars */
            numVars = 0;
        }
    }
    for (i = 0; i < numVars; ++i) {
        if (strcmp(vars[i], var) == 0) {
            if (val) {
                vals[i] = STRDUP(val);
            } else {
                return vals[i];
            }
        }
    }
    return NULL;
}

static int adm_initialized = 0;

/* Initialize NSPR for all the base functions we use */
NSAPI_PUBLIC int
ADM_Init(void)
{
    if (!adm_initialized) {
        NSPR_INIT("AdminPrograms");
        adm_initialized = 1;
    }
    return 0;
}

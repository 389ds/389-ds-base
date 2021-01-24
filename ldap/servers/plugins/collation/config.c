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

#include "collate.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include "slap.h"

#define MAXARGS 16

static char *
strtok_quote(char *line, char *sep)
{
    int inquote;
    char *tmp, *d;
    static char *next;

    if (line != NULL) {
        next = line;
    }
    while (*next && strchr(sep, *next)) {
        next++;
    }

    if (*next == '\0') {
        next = NULL;
        return (NULL);
    }

    d = tmp = next;
    for (inquote = 0; *next; next++) {
        switch (*next) {
        case '"':
            if (inquote) {
                inquote = 0;
            } else {
                inquote = 1;
            }
            break;

        case '\\':
            *d++ = *++next;
            break;

        default:
            if (!inquote) {
                if (strchr(sep, *next) != NULL) {
                    *d++ = '\0';
                    next++;
                    return (tmp);
                }
            }
            *d++ = *next;
            break;
        }
    }
    *d = '\0';

    return (tmp);
}

static void
fp_parse_line(
    char *line,
    int *argcp,
    char **argv)
{
    char *token;

    *argcp = 0;
    for (token = strtok_quote(line, " \t"); token != NULL;
         token = strtok_quote(NULL, " \t")) {
        if (*argcp == MAXARGS) {
            slapi_log_err(SLAPI_LOG_ERR, COLLATE_PLUGIN_SUBSYSTEM, "fp_parse_line - Too many tokens (max %d)\n",
                          MAXARGS);
            exit(1);
        }
        argv[(*argcp)++] = token;
    }
    argv[*argcp] = NULL;
}

static char buf[BUFSIZ];
static char *line;
static int lmax, lcur;

static void
fp_getline_init(int *lineno)
{
    *lineno = -1;
    buf[0] = '\0';
}

#define CATLINE(buf)                                     \
    {                                                    \
        int len;                                         \
        len = strlen(buf);                               \
        while (lcur + len + 1 > lmax) {                  \
            lmax += BUFSIZ;                              \
            line = (char *)slapi_ch_realloc(line, lmax); \
        }                                                \
        strcpy(line + lcur, buf);                        \
        lcur += len;                                     \
    }

static char *
fp_getline(FILE *fp, int *lineno)
{
    char *p;

    lcur = 0;
    CATLINE(buf);
    (*lineno)++;

    /* hack attack - keeps us from having to keep a stack of bufs... */
    if (strncasecmp(line, "include", 7) == 0) {
        buf[0] = '\0';
        return (line);
    }

    while (fgets(buf, sizeof(buf), fp) != NULL) {
        if ((p = strchr(buf, '\n')) != NULL) {
            *p = '\0';
        }
        if (!isspace(buf[0])) {
            return (line);
        }

        CATLINE(buf);
        (*lineno)++;
    }
    buf[0] = '\0';

    return (line[0] ? line : NULL);
}

void
collation_read_config(char *fname)
{
    FILE *fp;
    char *cfg_line;
    int cargc;
    char *cargv[MAXARGS];
    int lineno;

    fp = fopen(fname, "r");
    if (fp == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, COLLATE_PLUGIN_SUBSYSTEM,
                      "collation_read_config - Could not open config file \"%s\" - absolute path?\n",
                      fname);
        return; /* Do not exit */
    }

    slapi_log_err(SLAPI_LOG_CONFIG, "collation_read_config", "Reading config file %s\n", fname);

    fp_getline_init(&lineno);
    while ((cfg_line = fp_getline(fp, &lineno)) != NULL) {
        /* skip comments and blank lines */
        if (cfg_line[0] == '#' || cfg_line[0] == '\0') {
            continue;
        }
        slapi_log_err(SLAPI_LOG_CONFIG, COLLATE_PLUGIN_SUBSYSTEM,
                      "collation_read_config - line %d: %s\n", lineno, cfg_line);
        fp_parse_line(line, &cargc, cargv);
        if (cargc < 1) {
            slapi_log_err(SLAPI_LOG_ERR, COLLATE_PLUGIN_SUBSYSTEM,
                          "collation_read_config - %s: line %d: bad config line (ignored)\n",
                          fname, lineno);
            continue;
        }
        collation_config(cargc, cargv, fname, lineno);
    }
    fclose(fp);
}

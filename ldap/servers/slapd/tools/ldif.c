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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <memory.h>
#include <sys/types.h>
#include <unistd.h> /* for read() */
#include <sys/socket.h>
#include "ldap.h"
#include "ldif.h"

int ldap_syslog;
int ldap_syslog_level;

#if defined(USE_OPENLDAP)
static char *
ldif_type_and_value(const char *type, const char *val, int vlen)
{
    char *buf, *p;
    int tlen;

    tlen = strlen(type);
    if ((buf = (char *)malloc(LDIF_SIZE_NEEDED(tlen, vlen) + 1)) !=
        NULL) {
        p = buf;
        ldif_sput(&p, LDIF_PUT_VALUE, type, val, vlen);
        *p = '\0';
    }

    return (buf);
}
#endif

static void
display_usage(char *name)
{
    fprintf(stderr, "usage: %s [-b] <attrtype>\n", name);
}

int
main(int argc, char **argv)
{
    char *type, *out;
    int binary = 0;

    if (argc < 2 || argc > 3) {
        display_usage(argv[0]);
        return (1);
    }
    if (argc == 3) {
        if (strcmp(argv[1], "-b") != 0) {
            display_usage(argv[0]);
            return (1);
        } else {
            binary = 1;
            type = argv[2];
        }
    } else {
        if (strcmp(argv[1], "-b") == 0) {
            display_usage(argv[0]);
            return (1);
        }
        type = argv[1];
    }

    /* if the -b flag was used, read single binary value from stdin */
    if (binary) {
        char buf[BUFSIZ];
        char *val;
        int nread, max, cur;

        if ((val = (char *)malloc(BUFSIZ)) == NULL) {
            perror("malloc");
            return (1);
        }
        max = BUFSIZ;
        cur = 0;
        while ((nread = read(0, buf, BUFSIZ)) != 0) {
            if (nread < 0) {
                perror("read error");
                return (1);
            }
            if (nread + cur > max) {
                max += BUFSIZ;
                if ((val = (char *)realloc(val, max)) ==
                    NULL) {
                    perror("realloc");
                    return (1);
                }
            }
            memcpy(val + cur, buf, nread);
            cur += nread;
        }

        if ((out = ldif_type_and_value(type, val, cur)) == NULL) {
            perror("ldif_type_and_value");
            free(val);
            return (1);
        }

        fputs(out, stdout);
        free(out);
        free(val);
        return (0);
    } else {
        /* not binary:  one value per line... */
        char *buf;
        int curlen, maxlen = BUFSIZ;

        if ((buf = malloc(BUFSIZ)) == NULL) {
            perror("malloc");
            return (1);
        }
        while ((buf = fgets(buf, maxlen, stdin))) {
            /* if buffer was filled, expand and keep reading unless last char
            is linefeed, in which case it is OK for buffer to be full */
            while (((curlen = strlen(buf)) == (maxlen - 1)) && buf[curlen - 1] != '\n') {
                maxlen *= 2;
                if ((buf = (char *)realloc(buf, maxlen)) == NULL) {
                    perror("realloc");
                    free(buf);
                    return (1);
                }
                if (NULL == fgets(buf + curlen, maxlen / 2 + 1, stdin)) {
                    /* no more input to read. */
                    break;
                }
            }
            /* we have a full line, chop potential newline and turn into ldif */
            if (buf[curlen - 1] == '\n')
                buf[curlen - 1] = '\0';
            if ((out = ldif_type_and_value(type, buf, strlen(buf))) == NULL) {
                perror("ldif_type_and_value");
                free(buf);
                return (1);
            }
            fputs(out, stdout);
            free(out);
        }
        free(buf);
    }
    return (0);
}

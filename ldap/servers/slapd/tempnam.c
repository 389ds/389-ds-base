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

#if defined(nextstep)

#include <string.h>

char *tempnam(char *dir, char *pfx);

char *
tempnam(char *dir, char *pfx)
{
    char *s;

    if (dir == NULL) {
        dir = "/tmp";
    }

    /*
 * allocate space for dir + '/' + pfx (up to 5 chars) + 6 trailing 'X's + 0 byte
 */
    if ((s = (char *)slapi_ch_malloc(strlen(dir) + 14)) == NULL) {
        return (NULL);
    }

    strcpy(s, dir);
    strcat(s, "/");
    if (pfx != NULL) {
        strcat(s, pfx);
    }
    strcat(s, "XXXXXX");
    mktemp(s);

    if (*s == '\0') {
        slapi_ch_free((void **)&s);
    }

    return (s);
}

#else  /* nextstep */
typedef int SHUT_UP_DAMN_COMPILER;
#endif /* nextstep */

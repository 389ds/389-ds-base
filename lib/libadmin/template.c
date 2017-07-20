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
 * template.c:  The actual HTML templates in a static variable
 *
 * All blame to Mike McCool
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libadmin/libadmin.h"

NSAPI_PUBLIC char *
helpJavaScriptForTopic(char *topic)
{
    char *tmp;
    char line[BIG_LINE];
    char *server = "admserv";
    char *type;
    int typeLen;

    /* Get the server type, without the instance name into type */
    tmp = strchr(server, '-');
    typeLen = tmp - server;

    type = (char *)MALLOC(typeLen + 1);
    type[typeLen] = '\0';
    while (typeLen--) {
        type[typeLen] = server[typeLen];
    }
    util_snprintf(line, BIG_LINE,
                  "if ( top.helpwin ) {"
                  "  top.helpwin.focus();"
                  "  top.helpwin.infotopic.location='%s/%s/admin/tutor?!%s';"
                  "} else {"
                  "  window.open('%s/%s/admin/tutor?%s', '" INFO_IDX_NAME "_%s', " HELP_WIN_OPTIONS ");}",
                  getenv("SERVER_URL"), server, topic,
                  getenv("SERVER_URL"), server, topic,
                  type);

    FREE(type);
    return (STRDUP(line));
}

NSAPI_PUBLIC char *
helpJavaScript()
{
    char *tmp, *sn;

    tmp = STRDUP(getenv("SCRIPT_NAME"));
    if (strlen(tmp) > (unsigned)BIG_LINE)
        tmp[BIG_LINE - 2] = '\0';
    sn = strrchr(tmp, '/');
    if (sn)
        *sn++ = '\0';
    FREE(tmp);
    return helpJavaScriptForTopic(sn);
}

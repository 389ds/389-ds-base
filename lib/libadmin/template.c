/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* 
 * template.c:  The actual HTML templates in a static variable 
 *            
 * All blame to Mike McCool
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libadmin/libadmin.h"

NSAPI_PUBLIC char *helpJavaScriptForTopic( char *topic )
{
    char *tmp;
    char line[BIG_LINE];
    char *server="admserv";
    char *type;
    int	 typeLen;

    /* Get the server type, without the instance name into type */
    tmp = strchr( server, '-' );
    typeLen = tmp - server;

    type = (char *)MALLOC( typeLen + 1 );
    type[typeLen] = '\0';
    while ( typeLen-- ) {
      type[typeLen] = server[typeLen];
    }
    util_snprintf( line, BIG_LINE,
		   "if ( top.helpwin ) {"
		   "  top.helpwin.focus();"
		   "  top.helpwin.infotopic.location='%s/%s/admin/tutor?!%s';"
		   "} else {"
		   "  window.open('%s/%s/admin/tutor?%s', '"
		   INFO_IDX_NAME"_%s', "
		   HELP_WIN_OPTIONS");}",
		   getenv("SERVER_URL"), server, topic,
		   getenv("SERVER_URL"), server, topic,
		   type );
		   
    return(STRDUP(line));
}

NSAPI_PUBLIC char *helpJavaScript()
{
    char *tmp, *sn;

    tmp=STRDUP(getenv("SCRIPT_NAME"));
    if(strlen(tmp) > (unsigned)BIG_LINE)
        tmp[BIG_LINE-2]='\0';
    sn=strrchr(tmp, '/');
    if( sn )
        *sn++='\0';
    return helpJavaScriptForTopic( sn );
}

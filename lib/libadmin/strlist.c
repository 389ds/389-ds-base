/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* 
 * strlist.c:  Managing a handle to a list of strings
 *            
 * All blame to Mike McCool
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "netsite.h"
#include <libadmin/libadmin.h>

NSAPI_PUBLIC char **new_strlist(int size)
{
    char **new_list;
    register int x;

    new_list = (char **) MALLOC((size+1)*(sizeof(char *)));
/* <= so we get the one right after the given size as well */
    for(x=0; x<= size; x++)  
        new_list[x] = NULL;

    return new_list;
}

NSAPI_PUBLIC char **grow_strlist(char **strlist, int newsize)
{
    char **ans;

    ans = (char **) REALLOC(strlist, (newsize+1)*sizeof(char *));

    return ans;
}

NSAPI_PUBLIC void free_strlist(char **strlist)
{
    int x;

    for(x=0; (strlist[x]); x++) free(strlist[x]);
    free(strlist);
}

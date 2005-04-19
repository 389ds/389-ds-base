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
#if defined( XP_WIN32 )
#include <windows.h>
#endif
#include "dsalib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "nspr.h"

static char *
get_month_str(int month)
{
    static char *month_str[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct",
	"Nov", "Dec"};

    if ( (month < 1) || (month > 12) )
        return("Unknown month");
    return(month_str[month - 1]);
}

/*
 * Returns a string describing the meaning of the filename.
 * Two different formats are supported.
 */
DS_EXPORT_SYMBOL char *
ds_get_file_meaning(char *file)
{
    static char meaning[BIG_LINE];
#define	FILE_EXPECTED_SIZE1	14
#define	FILE_EXPECTED_SIZE2	17
    char	*name;
    char	*tmp;
    int		i;
    int		month;
    int		day;
    int		hour;
    int		minute;
    int		sec;
    int		year;

    /*
     * Expect a file name in format 06041996123401 (FILE_EXPECTED_SIZE1)
     * which should return "Jun  4 12:34:01 1996"
     * OR 			    1996_06_04_123401
     * which should return "Jun  4 12:34:01 1996"
     */
 
    if ( file == NULL )
        return(NULL);
    name = strdup(file);
    if ( name == NULL )
        return(NULL);
    if ( (tmp = strrchr(name, '.')) != NULL )
	*tmp = '\0';
    if ( strlen(name) == FILE_EXPECTED_SIZE1 ) {
        for ( i = 0; i < FILE_EXPECTED_SIZE1; i++ )
	    if ( !isdigit(name[i]) )
                return(NULL);
        if ( (sscanf(name, "%2d%2d%4d%2d%2d%2d", &month, &day, &year, &hour,
	    &minute, &sec)) == -1 )
            return(NULL);
    } else if ( strlen(name) == FILE_EXPECTED_SIZE2 ) {
        for ( i = 0; i < FILE_EXPECTED_SIZE2; i++ )
	    if ( !isdigit(name[i]) )
	        if ( name[i] != '_' )
                    return(NULL);
        if ( (sscanf(name, "%4d_%2d_%2d_%2d%2d%2d", &year, &month, &day, &hour,
	    &minute, &sec)) == -1 )
            return(NULL);
    } else
        return(NULL);

    if ( (month < 1) || (month > 12) )
        return(NULL);
    if ( (day < 1) || (day > 31) )
        return(NULL);
    if ( (year < 1000) || (year > 2100) )
        return(NULL);
    if ( (hour < 0) || (hour > 24) )
        return(NULL);
    if ( (minute < 0) || (minute > 60) )
        return(NULL);
    if ( (sec < 0) || (sec > 60) )
        return(NULL);
    PR_snprintf(meaning, sizeof(meaning), "%s % 2d %02d:%02d:%02d %4d", get_month_str(month), 
	day, hour, minute, sec, year);
    return(meaning);
}

/** BEGIN COPYRIGHT BLOCK
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
    name = malloc(strlen(file) + 1);
    if ( name == NULL )
        return(NULL);
    strcpy(name, file);
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

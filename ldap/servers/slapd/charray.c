/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* charray.c - routines for dealing with char * arrays */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include "slap.h"

void
charray_add(
    char    ***a,
    char    *s
)
{
    int    n;

    if ( *a == NULL ) {
        *a = (char **) slapi_ch_malloc( 2 * sizeof(char *) );
        n = 0;
    } else {
        for ( n = 0; *a != NULL && (*a)[n] != NULL; n++ ) {
            ;    /* NULL */
        }

        *a = (char **) slapi_ch_realloc( (char *) *a,
            (n + 2) * sizeof(char *) );
    }

    /* At this point, *a may be different from the value it had when this 
     * function is called.  Furthermore, *a[n] may contain an arbitrary 
     * value, such as a pointer to the middle of a unallocated area.
     */
    
#ifdef TEST_BELLATON
    (*a)[n+1] = NULL;
    (*a)[n] = s;
#endif

    /* Putting code back so that thread conflict can be made visible */

    (*a)[n++] = s;
    (*a)[n] = NULL;

}

void
charray_merge(
    char    ***a,
    char    **s,
    int        copy_strs
)
{
    int    i, n, nn;

    if ( (s == NULL) || (s[0] == NULL) )
        return;

    for ( n = 0; *a != NULL && (*a)[n] != NULL; n++ ) {
        ;    /* NULL */
    }
    for ( nn = 0; s[nn] != NULL; nn++ ) {
        ;    /* NULL */
    }

    *a = (char **) slapi_ch_realloc( (char *) *a, (n + nn + 1) * sizeof(char *) );

    for ( i = 0; i < nn; i++ ) {
        if ( copy_strs ) {
            (*a)[n + i] = slapi_ch_strdup( s[i] );
        } else {
            (*a)[n + i] = s[i];
        }
    }
    (*a)[n + nn] = NULL;
}

/* Routines which don't pound on malloc. Don't interchange the arrays with the
 * regular calls---they can end up freeing non-heap memory, which is wrong */

void
cool_charray_free( char **array )
{
    slapi_ch_free((void**)&array);
}

/* Like strcpy, but returns a pointer to the next byte after the last one written to */
static char *strcpy_len(char *dest, char *source)
{
    if('\0' == (*source)) {
        return(dest);
    }
    do {
        *dest++ = *source++;
    } while (*source);
    return dest;
}

char **
cool_charray_dup( char **a )
{
    int    i,size, num_strings;
    char    **newa;
    char *p;

    if ( a == NULL ) {
        return( NULL );
    }

    for ( i = 0; a[i] != NULL; i++ )
        ;    /* NULL */

    num_strings = i;
    size = (i + 1) * sizeof(char *);

    for ( i = 0; a[i] != NULL; i++ ) {
         size += strlen( a[i] ) + 1;
    }

    newa = (char **) slapi_ch_malloc( size );

    p = (char *) &(newa[num_strings + 1]);

    for ( i = 0; a[i] != NULL; i++ ) {
        newa[i] = p;
        p = strcpy_len(p, a[i] );
        *p++ = '\0';
    }
    newa[i] = NULL;

    return( newa );
}

void
charray_free( char **array )
{
    char    **a;

    if ( array == NULL ) {
        return;
    }

    for ( a = array; *a != NULL; a++ )
    {
        char *tmp= *a;
        slapi_ch_free((void**)&tmp);
    }
    slapi_ch_free( (void**)&array );
}

/* 
 * charray_free version for plugins: there is a need for plugins to free
 * the ch_arrays returned by functions like:
 * slapi_get_supported_extended_ops_copy
 * slapi_get_supported_saslmechanisms_copy
 * slapi_get_supported_controls_copy
 */
void
slapi_ch_array_free( char **array )
{
    charray_free (array);
}


/* case insensitive search */
int
charray_inlist(
    char    **a,
    char    *s
)
{
    int    i;

    if ( a == NULL ) {
        return( 0 );
    }

    for ( i = 0; a[i] != NULL; i++ ) {
        if ( strcasecmp( s, a[i] ) == 0 ) {
            return( 1 );
        }
    }

    return( 0 );
}

/* case insensitive search covering non-ascii */
int
charray_utf8_inlist(
    char    **a,
    char    *s
)
{
    int    i;

    if ( a == NULL ) {
        return( 0 );
    }

    for ( i = 0; a[i] != NULL; i++ ) {
        if (!slapi_UTF8CASECMP(a[i], s)) {
            return( 1 );
        }
    }

    return( 0 );
}

char **
charray_dup( char **a )
{
    int    i;
    char    **newa;

    if ( a == NULL ) {
        return( NULL );
    }

    for ( i = 0; a[i] != NULL; i++ )
        ;    /* NULL */

    newa = (char **) slapi_ch_malloc( (i + 1) * sizeof(char *) );

    for ( i = 0; a[i] != NULL; i++ ) {
        newa[i] = slapi_ch_strdup( a[i] );
    }
    newa[i] = NULL;

    return( newa );
}

char **
str2charray( char *str, char *brkstr )
{
    char    **res;
    char    *s;
    int    i;
    char * iter = NULL;

    i = 1;
    for ( s = str; *s; s++ ) {
        if ( strchr( brkstr, *s ) != NULL ) {
            i++;
        }
    }

    res = (char **) slapi_ch_malloc( (i + 1) * sizeof(char *) );
    i = 0;
    for ( s = ldap_utf8strtok_r( str, brkstr , &iter); s != NULL; 
            s = ldap_utf8strtok_r( NULL, brkstr , &iter) ) {
        res[i++] = slapi_ch_strdup( s );
    }
    res[i] = NULL;

    return( res );
}

void
charray_print( char **a )
{
    int    i;

    printf( "charray_print:\n");
    for ( i = 0; a!= NULL && a[i] != NULL; i++ ) {
        printf( "\t%s\n", a[i]);
    }
}

/*
 * Remove the char string from the array of char strings.
 * Performs a case *insensitive* comparison!
 * Just shunts the strings down to cover the deleted string.
 * Doesn't free up the unused memory.
 * Returns 1 if the entry found and removed, 0 if not.
 */
int
charray_remove(
    char **a,
    const char *s
)
{
    int i;
    int found= 0;
      for ( i=0; a!= NULL && a[i] != NULL; i++ )
      {
        if ( !found && strcasecmp (a[i],s) == 0 )
        {
            found= 1;
        }
        if (found)
        {
            a[i]= a[i+1];
        }
       }
    return found;
}

/*
 * if c == NULL, a = a - b
 * if c != NULL, *c = a - b
 */
#define SUBTRACT_DEL    (char *)(-1)
void
charray_subtract(char **a, char **b, char ***c)
{
    char **bp, **cp, **tmp;
    char **p;

    if (c)
        tmp = *c = cool_charray_dup(a);
    else
        tmp = a;

    for (cp = tmp; cp && *cp; cp++) {
        for (bp = b; bp && *bp; bp++) {
            if (!slapi_UTF8CASECMP(*cp, *bp)) {
                slapi_ch_free((void **)&*cp);
                *cp = SUBTRACT_DEL;
                break;
            }
        }
    }

    for (cp = tmp; cp && *cp; cp++) {
        if (*cp == SUBTRACT_DEL) {
            for (p = cp+1; *p && *p == (char *)SUBTRACT_DEL; p++)
                ;
            *cp = *p;    
            if (*p == NULL)
                break;
            else
                *p = SUBTRACT_DEL;
        }
    }
}

int
charray_get_index(char **array, char *s)
{
    int i;

    for (i = 0; array && array[i]; i++)
    {
        if (!slapi_UTF8CASECMP(array[i], s))
            return i;
    }
    return -1;
}

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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

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
    slapi_ch_array_add_ext(a, s);
}

/* return the total number of elements that are now in the array */
int
slapi_ch_array_add_ext(char ***a, char *s)
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

    return n;
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

/*
 * charray_merge_nodup:
 *     merge a string array (second arg) into the first string array 
 *     unless the each string is in the first string array.
 */
void
charray_merge_nodup(
    char    ***a,
    char    **s,
    int        copy_strs
)
{
    int  i, j, n, nn;
    char **dupa;

    if ( (s == NULL) || (s[0] == NULL) )
        return;

    for ( n = 0; *a != NULL && (*a)[n] != NULL; n++ ) {
        ;    /* NULL */
    }
    for ( nn = 0; s[nn] != NULL; nn++ ) {
        ;    /* NULL */
    }

    dupa = (char **)slapi_ch_calloc(1, (n+nn+1) * sizeof(char *));
    memcpy(dupa, *a, sizeof(char *) * n);
    slapi_ch_free((void **)a);

    for ( i = 0, j = 0; i < nn; i++ ) {
        if (!charray_inlist(dupa, s[i])) { /* skip if s[i] is already in *a */
            if ( copy_strs ) {
                dupa[n+j] = slapi_ch_strdup( s[i] );
            } else {
                dupa[n+j] = s[i];
            }
            j++;
        }
    }
    *a = dupa;
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

void
slapi_ch_array_add( char    ***a, char    *s )
{
    charray_add(a, s);
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

int slapi_ch_array_utf8_inlist(char **a, char *s)
{
	return charray_utf8_inlist(a,s);
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
slapi_ch_array_dup( char **array )
{
    return charray_dup(array);
}

char **
slapi_str2charray( char *str, char *brkstr )
{
    return( slapi_str2charray_ext( str, brkstr, 1 ));
}

/*
 * extended version of str2charray lets you disallow
 * duplicate values into the array.
 */
char **
slapi_str2charray_ext( char *str, char *brkstr, int allow_dups )
{
    char    **res;
    char    *s;
    int    i, j;
    int dup_found = 0;
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
        dup_found = 0;
        /* Always copy the first value into the array */
        if ( (!allow_dups) && (i != 0) ) {
            /* Check for duplicates */
            for ( j = 0; j < i; j++ ) {
                if ( strncmp( res[j], s, strlen( s ) ) == 0 ) {
                    dup_found = 1;
                    break;
                }
            }
        }

        if ( !dup_found )
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
 * freeit: none zero -> free the found string
 *       :      zero -> Doesn't free up the unused memory.
 * Returns 1 if the entry found and removed, 0 if not.
 */
int
charray_remove(
    char **a,
    const char *s,
    int freeit
)
{
    int i;
    int found= 0;
      for ( i=0; a!= NULL && a[i] != NULL; i++ )
      {
        if ( !found && strcasecmp (a[i],s) == 0 )
        {
            found= 1;
            if (freeit)
            {
                slapi_ch_free_string(&a[i]);
            }
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

int
charray_normdn_add(char ***chararray, char *dn, char *errstr)
{
    int rc = 0;
    size_t len = 0;
    char *normdn = NULL;
    rc = slapi_dn_normalize_ext(dn, 0, &normdn, &len);
    if (rc < 0) {
        LDAPDebug2Args(LDAP_DEBUG_ANY, "Invalid dn: \"%s\" %s\n",
                       dn, errstr?errstr:"");
        return rc;
    } else if (0 == rc) {
        /* rc == 0; optarg_extawdn is passed in; 
         * not null terminated */
        *(dn + len) = '\0';
        normdn = slapi_ch_strdup(dn);
    }
    charray_add(chararray, slapi_dn_ignore_case(normdn));
    return rc;
}

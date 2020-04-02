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

/* charray.c - routines for dealing with char * arrays */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "slap.h"

void
charray_add(
    char ***a,
    char *s)
{
    slapi_ch_array_add_ext(a, s);
}

/* return the total number of elements that are now in the array */
int
slapi_ch_array_add_ext(char ***a, char *s)
{
    int n;

    if (*a == NULL) {
        *a = (char **)slapi_ch_malloc(2 * sizeof(char *));
        n = 0;
    } else {
        for (n = 0; *a != NULL && (*a)[n] != NULL; n++) {
            ; /* NULL */
        }

        *a = (char **)slapi_ch_realloc((char *)*a,
                                       (n + 2) * sizeof(char *));
    }

/* At this point, *a may be different from the value it had when this
     * function is called.  Furthermore, *a[n] may contain an arbitrary
     * value, such as a pointer to the middle of a unallocated area.
     */

#ifdef TEST_BELLATON
    (*a)[n + 1] = NULL;
    (*a)[n] = s;
#endif

    /* Putting code back so that thread conflict can be made visible */
    (*a)[n++] = s;
    (*a)[n] = NULL;

    return n;
}

void
charray_merge(
    char ***a,
    char **s,
    int copy_strs)
{
    int i, n, nn;

    if ((s == NULL) || (s[0] == NULL))
        return;

    for (n = 0; *a != NULL && (*a)[n] != NULL; n++) {
        ; /* NULL */
    }
    for (nn = 0; s[nn] != NULL; nn++) {
        ; /* NULL */
    }

    *a = (char **)slapi_ch_realloc((char *)*a, (n + nn + 1) * sizeof(char *));

    for (i = 0; i < nn; i++) {
        if (copy_strs) {
            (*a)[n + i] = slapi_ch_strdup(s[i]);
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
    char ***a,
    char **s,
    int copy_strs)
{
    int i, j, n, nn;
    char **dupa;

    if ((s == NULL) || (s[0] == NULL))
        return;

    for (n = 0; *a != NULL && (*a)[n] != NULL; n++) {
        ; /* NULL */
    }
    for (nn = 0; s[nn] != NULL; nn++) {
        ; /* NULL */
    }

    dupa = (char **)slapi_ch_calloc(1, (n + nn + 1) * sizeof(char *));
    memcpy(dupa, *a, sizeof(char *) * n);
    slapi_ch_free((void **)a);

    for (i = 0, j = 0; i < nn; i++) {
        if (!charray_inlist(dupa, s[i])) { /* skip if s[i] is already in *a */
            if (copy_strs) {
                dupa[n + j] = slapi_ch_strdup(s[i]);
            } else {
                dupa[n + j] = s[i];
            }
            j++;
        }
    }
    *a = dupa;
}

/* Routines which don't pound on malloc. Don't interchange the arrays with the
 * regular calls---they can end up freeing non-heap memory, which is wrong */

void
cool_charray_free(char **array)
{
    slapi_ch_free((void **)&array);
}

/* Like strcpy, but returns a pointer to the next byte after the last one written to */
static char *
strcpy_len(char *dest, char *source)
{
    if ('\0' == (*source)) {
        return (dest);
    }
    do {
        *dest++ = *source++;
    } while (*source);
    return dest;
}

char **
cool_charray_dup(char **a)
{
    int i, size, num_strings;
    char **newa;
    char *p;

    if (a == NULL) {
        return (NULL);
    }

    for (i = 0; a[i] != NULL; i++)
        ; /* NULL */

    num_strings = i;
    size = (i + 1) * sizeof(char *);

    for (i = 0; a[i] != NULL; i++) {
        size += strlen(a[i]) + 1;
    }

    newa = (char **)slapi_ch_malloc(size);

    p = (char *)&(newa[num_strings + 1]);

    for (i = 0; a[i] != NULL; i++) {
        newa[i] = p;
        p = strcpy_len(p, a[i]);
        *p++ = '\0';
    }
    newa[i] = NULL;

    return (newa);
}

void
charray_free(char **array)
{
    char **a;

    if (array == NULL) {
        return;
    }

    for (a = array; *a != NULL; a++) {
        char *tmp = *a;
        slapi_ch_free((void **)&tmp);
    }
    slapi_ch_free((void **)&array);
}

/*
 * charray_free version for plugins: there is a need for plugins to free
 * the ch_arrays returned by functions like:
 * slapi_get_supported_extended_ops_copy
 * slapi_get_supported_saslmechanisms_copy
 * slapi_get_supported_controls_copy
 */
void
slapi_ch_array_free(char **array)
{
    charray_free(array);
}

void
slapi_ch_array_add(char ***a, char *s)
{
    charray_add(a, s);
}

/* case insensitive search */
int
charray_inlist(
    char **a,
    char *s)
{
    int i;

    if (a == NULL) {
        return (0);
    }

    for (i = 0; a[i] != NULL; i++) {
        if (strcasecmp(s, a[i]) == 0) {
            return (1);
        }
    }

    return (0);
}

/* case insensitive search covering non-ascii */
int
charray_utf8_inlist(
    char **a,
    char *s)
{
    int i;

    if (a == NULL) {
        return (0);
    }

    for (i = 0; a[i] != NULL; i++) {
        if (!slapi_UTF8CASECMP(a[i], s)) {
            return (1);
        }
    }

    return (0);
}

/*
 * Assert that some str s is in the charray, or add it.
 */
void
charray_assert_present(char ***a, char *s)
{
    int result = charray_utf8_inlist(*a, s);
    /* Not in the list */
    if (result == 0) {
        char *sdup = slapi_ch_strdup(s);
        slapi_ch_array_add_ext(a, sdup);
    }
}

int
slapi_ch_array_utf8_inlist(char **a, char *s)
{
    return charray_utf8_inlist(a, s);
}

char **
charray_dup(char **a)
{
    int i;
    char **newa;

    if (a == NULL) {
        return (NULL);
    }

    for (i = 0; a[i] != NULL; i++)
        ; /* NULL */

    newa = (char **)slapi_ch_malloc((i + 1) * sizeof(char *));

    for (i = 0; a[i] != NULL; i++) {
        newa[i] = slapi_ch_strdup(a[i]);
    }
    newa[i] = NULL;

    return (newa);
}

char **
slapi_ch_array_dup(char **array)
{
    return charray_dup(array);
}

char **
slapi_str2charray(char *str, char *brkstr)
{
    return (slapi_str2charray_ext(str, brkstr, 1));
}

/*
 * extended version of str2charray lets you disallow
 * duplicate values into the array.
 * Also, "char *str" should be a temporary string which is freed afterwards.
 * the string is changed during this function execution
 */
char **
slapi_str2charray_ext(char *str, char *brkstr, int allow_dups)
{
    char **res;
    char *s;
    int i, j;
    int dup_found = 0;
    char *iter = NULL;

    i = 1;
    for (s = str; *s; s++) {
        if (strchr(brkstr, *s) != NULL) {
            i++;
        }
    }

    res = (char **)slapi_ch_malloc((i + 1) * sizeof(char *));
    i = 0;
    for (s = ldap_utf8strtok_r(str, brkstr, &iter); s != NULL;
         s = ldap_utf8strtok_r(NULL, brkstr, &iter)) {
        dup_found = 0;
        /* Always copy the first value into the array */
        if ((!allow_dups) && (i != 0)) {
            /* Check for duplicates */
            for (j = 0; j < i; j++) {
                if (strncmp(res[j], s, strlen(s)) == 0) {
                    dup_found = 1;
                    break;
                }
            }
        }

        if (!dup_found) {
            res[i++] = slapi_ch_strdup(s);
        }
    }
    res[i] = NULL;

    return (res);
}

void
charray_print(char **a)
{
    int i;

    printf("charray_print:\n");
    for (i = 0; a != NULL && a[i] != NULL; i++) {
        printf("\t%s\n", a[i]);
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
    int freeit)
{
    int i;
    int found = 0;
    for (i = 0; a != NULL && a[i] != NULL; i++) {
        if (!found && strcasecmp(a[i], s) == 0) {
            found = 1;
            if (freeit) {
                slapi_ch_free_string(&a[i]);
            }
        }
        if (found) {
            a[i] = a[i + 1];
        }
    }
    return found;
}

/*
 * if c == NULL, a = a - b
 * if c != NULL, *c = a - b
 */
#define SUBTRACT_DEL (char *)(-1)
void
charray_subtract(char **a, char **b, char ***c)
{
    char **bp, **cp, **tmp;
    char **p;

    if (c) {
        tmp = *c = cool_charray_dup(a);
    } else {
        tmp = a;
    }

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
            for (p = cp + 1; *p && *p == (char *)SUBTRACT_DEL; p++)
                ;
            *cp = *p;
            if (*p == NULL) {
                break;
            } else {
                *p = SUBTRACT_DEL;
            }
        }
    }
}

/*
 * Provides the intersection of two arrays.
 * IE if you have:
 * (A, B, C)
 * (B, D, E)
 * result is (B,)
 * a and b are NOT consumed in the process.
 */
char **
charray_intersection(char **a, char **b)
{
    char **result;
    size_t rp = 0;

    if (a == NULL || b == NULL) {
        return NULL;
    }

    size_t a_len = 0;
    /* Find how long A is. */
    for (; a[a_len] != NULL; a_len++)
        ;

    /* Allocate our result, it can't be bigger than A */
    result = (char **)slapi_ch_calloc(1, sizeof(char *) * (a_len + 1));

    /* For each in A, see if it's in b */
    for (size_t i = 0; a[i] != NULL; i++) {
        if (charray_get_index(b, a[i]) != -1) {
            result[rp] = slapi_ch_strdup(a[i]);
            rp++;
        }
    }

    return result;
}

int
charray_get_index(char **array, char *s)
{
    int i;

    for (i = 0; array && array[i]; i++) {
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
        slapi_log_err(SLAPI_LOG_ERR, "charray_normdn_add - Invalid dn: \"%s\" %s\n",
                      dn, errstr ? errstr : "");
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

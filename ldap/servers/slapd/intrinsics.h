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


/* Header file used to declare functions which we beat on heavily as intrinsic */

#if defined(HPUX)
#define INLINE_DIRECTIVE __inline
#elif defined(LINUX)
#define INLINE_DIRECTIVE __inline__
#else
#define INLINE_DIRECTIVE
#endif

__attribute__((nonnull (1, 2))) INLINE_DIRECTIVE static int
strcmpi_fast(const char *dst, const char *src)
{
    int f, l;
    do {
        if (((f = (unsigned char)(*(dst++))) >= 'A') && (f <= 'Z'))
            f -= ('A' - 'a');
        if (((l = (unsigned char)(*(src++))) >= 'A') && (l <= 'Z'))
            l -= ('A' - 'a');
    } while (f && (f == l));
    return (f - l);
}
#ifdef strcasecmp
#undef strcasecmp
#endif
#define strcasecmp(x, y) strcmpi_fast(x, y)
#ifdef strcmpi
#undef strcmpi
#endif
#define strcmpi(x, y) strcmpi_fast(x, y)

INLINE_DIRECTIVE static int
tolower_fast(int c)
{
    if ((c >= 'A') && (c <= 'Z'))
        c = c + ('a' - 'A');
    return c;
}
#ifdef tolower
#undef tolower
#endif
#define tolower(x) tolower_fast(x)

INLINE_DIRECTIVE static int
toupper_fast(int c)
{
    if ((c >= 'a') && (c <= 'z'))
        c = c - ('a' - 'A');
    return c;
}
#ifdef toupper
#undef toupper
#endif
#define toupper(x) toupper_fast(x)

__attribute__((nonnull (1, 2))) INLINE_DIRECTIVE static int
strncasecmp_fast(const char *dst, const char *src, int n)
{
    int f, l, x = 0;
    do {
        if (((f = (unsigned char)(*(dst++))) >= 'A') && (f <= 'Z'))
            f -= ('A' - 'a');
        if (((l = (unsigned char)(*(src++))) >= 'A') && (l <= 'Z'))
            l -= ('A' - 'a');
    } while (f && (f == l) && ++x < n);
    return (f - l);
}

#ifdef strncasecmp
#undef strncasecmp
#endif
#define strncasecmp(x, y, z) strncasecmp_fast(x, y, z)

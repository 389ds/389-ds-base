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

#ifndef BASE_UTIL_H
#define BASE_UTIL_H

#ifndef NOINTNSAPI
#define INTNSAPI
#endif /* !NOINTNSAPI */

/*
 * util.h: A hodge podge of utility functions and standard functions which
 *         are unavailable on certain systems
 *
 * Rob McCool
 */

#ifndef PUBLIC_NSAPI_H
#include "public/nsapi.h"
#endif /* !PUBLIC_NSAPI_H */


/* --- Begin common function prototypes --- */

#ifdef INTNSAPI

NSPR_BEGIN_EXTERN_C

NSAPI_PUBLIC int INTutil_itoa(int i, char *a);

NSAPI_PUBLIC
int INTutil_vsprintf(char *s, const char *fmt, va_list args);

NSAPI_PUBLIC int INTutil_sprintf(char *s, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 2, 3)));
#else
    ;
#endif

NSAPI_PUBLIC int INTutil_vsnprintf(char *s, int n, const char *fmt, va_list args);

NSAPI_PUBLIC int INTutil_snprintf(char *s, int n, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 3, 4)));
#else
    ;
#endif

NSAPI_PUBLIC int INTutil_strftime(char *s, const char *format, const struct tm *t);

NSAPI_PUBLIC struct tm *INTutil_localtime(const time_t *clock, struct tm *res);

#ifdef NEED_STRCASECMP
NSAPI_PUBLIC int INTutil_strcasecmp(CASECMPARG_T char *one, CASECMPARG_T char *two);
#endif /* NEED_STRCASECMP */

#ifdef NEED_STRNCASECMP
NSAPI_PUBLIC int INTutil_strncasecmp(CASECMPARG_T char *one, CASECMPARG_T char *two, int n);
#endif /* NEED_STRNCASECMP */

/* --- End common function prototypes --- */

NSPR_END_EXTERN_C

#define util_itoa INTutil_itoa
#define util_vsprintf INTutil_vsprintf
#define util_sprintf INTutil_sprintf
#define util_vsnprintf INTutil_vsnprintf
#define util_snprintf INTutil_snprintf
#define util_strftime INTutil_strftime
#define util_strcasecmp INTutil_strcasecmp
#define util_strncasecmp INTutil_strncasecmp
#define util_localtime INTutil_localtime

#ifdef NEED_STRINGS_H /* usually for strcasecmp */
#include <strings.h>
#endif

#endif /* INTNSAPI */

#endif /* !BASE_UTIL_H */

/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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

#ifndef PUBLIC_BASE_UTIL_H
#include "public/base/util.h"
#endif /* !PUBLIC_BASE_UTIL_H */

/* --- Begin common function prototypes --- */

#ifdef INTNSAPI

NSPR_BEGIN_EXTERN_C

NSAPI_PUBLIC char *INTutil_hostname(void);

NSAPI_PUBLIC int INTutil_itoa(int i, char *a);

NSAPI_PUBLIC
int INTutil_vsprintf(char *s, register const char *fmt, va_list args);

NSAPI_PUBLIC int INTutil_sprintf(char *s, const char *fmt, ...);

NSAPI_PUBLIC int INTutil_vsnprintf(char *s, int n, register const char *fmt, 
                                  va_list args);

NSAPI_PUBLIC int INTutil_snprintf(char *s, int n, const char *fmt, ...);

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

#define util_hostname INTutil_hostname
#define util_itoa INTutil_itoa
#define util_vsprintf INTutil_vsprintf
#define util_sprintf INTutil_sprintf
#define util_vsnprintf INTutil_vsnprintf
#define util_snprintf INTutil_snprintf
#define util_strftime INTutil_strftime
#define util_strcasecmp INTutil_strcasecmp
#define util_strncasecmp INTutil_strncasecmp
#define util_localtime INTutil_localtime

#ifdef NEED_STRCASECMP
#define util_strcasecmp INTutil_strcasecmp
#define strcasecmp INTutil_strcasecmp
#endif /* NEED_STRCASECMP */

#ifdef NEED_STRINGS_H /* usually for strcasecmp */
#include <strings.h>
#endif

#ifdef NEED_STRNCASECMP
#define util_strncasecmp INTutil_strncasecmp
#define strncasecmp INTutil_strncasecmp
#endif /* NEED_STRNCASECMP */

#endif /* INTNSAPI */

#endif /* !BASE_UTIL_H */

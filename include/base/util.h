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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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

#ifndef PUBLIC_NSAPI_H
#include "public/nsapi.h"
#endif /* !PUBLIC_NSAPI_H */


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

#ifdef NEED_STRINGS_H /* usually for strcasecmp */
#include <strings.h>
#endif

#endif /* INTNSAPI */

#endif /* !BASE_UTIL_H */

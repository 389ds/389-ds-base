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

/* Needed for various reentrant functions */
#define DEF_CTIMEBUF 26
#define DEF_ERRBUF 256
#define DEF_PWBUF 256

#ifndef BASE_BUFFER_H
#include "buffer.h"    /* filebuf for getline */
#endif /* !BASE_BUFFER_H */

#ifndef PUBLIC_BASE_UTIL_H
#include "public/base/util.h"
#endif /* !PUBLIC_BASE_UTIL_H */

/* --- Begin common function prototypes --- */

#ifdef INTNSAPI

NSPR_BEGIN_EXTERN_C

NSAPI_PUBLIC
int INTutil_getline(filebuffer *buf, int lineno, int maxlen, char *l);

NSAPI_PUBLIC char **INTutil_env_create(char **env, int n, int *pos);

NSAPI_PUBLIC char *INTutil_env_str(char *name, char *value);

NSAPI_PUBLIC void INTutil_env_replace(char **env, char *name, char *value);

NSAPI_PUBLIC void INTutil_env_free(char **env);

NSAPI_PUBLIC char **INTutil_env_copy(char **src, char **dst);

NSAPI_PUBLIC char *INTutil_env_find(char **env, char *name);

NSAPI_PUBLIC char *INTutil_hostname(void);

NSAPI_PUBLIC int INTutil_chdir2path(char *path);

NSAPI_PUBLIC int INTutil_is_mozilla(char *ua, char *major, char *minor);

NSAPI_PUBLIC int INTutil_is_url(char *url);

NSAPI_PUBLIC int INTutil_later_than(struct tm *lms, char *ims);

NSAPI_PUBLIC int INTutil_time_equal(struct tm *lms, char *ims);

NSAPI_PUBLIC int INTutil_str_time_equal(char *t1, char *t2);

NSAPI_PUBLIC int INTutil_uri_is_evil(char *t);

NSAPI_PUBLIC void INTutil_uri_parse(char *uri);

NSAPI_PUBLIC void INTutil_uri_unescape(char *s);

NSAPI_PUBLIC char *INTutil_uri_escape(char *d, char *s);

NSAPI_PUBLIC char *INTutil_url_escape(char *d, char *s);

NSAPI_PUBLIC char *INTutil_sh_escape(char *s);

NSAPI_PUBLIC int INTutil_mime_separator(char *sep);

NSAPI_PUBLIC int INTutil_itoa(int i, char *a);

NSAPI_PUBLIC
int INTutil_vsprintf(char *s, register const char *fmt, va_list args);

NSAPI_PUBLIC int INTutil_sprintf(char *s, const char *fmt, ...);

NSAPI_PUBLIC int INTutil_vsnprintf(char *s, int n, register const char *fmt, 
                                  va_list args);

NSAPI_PUBLIC int INTutil_snprintf(char *s, int n, const char *fmt, ...);

NSAPI_PUBLIC int INTutil_strftime(char *s, const char *format, const struct tm *t);

NSAPI_PUBLIC char *INTutil_strtok(char *s1, const char *s2, char **lasts);

NSAPI_PUBLIC struct tm *INTutil_localtime(const time_t *clock, struct tm *res);

NSAPI_PUBLIC char *INTutil_ctime(const time_t *clock, char *buf, int buflen);

NSAPI_PUBLIC char *INTutil_strerror(int errnum, char *msg, int buflen);

NSAPI_PUBLIC struct tm *INTutil_gmtime(const time_t *clock, struct tm *res);

NSAPI_PUBLIC char *INTutil_asctime(const struct tm *tm,char *buf, int buflen);

#ifdef NEED_STRCASECMP
NSAPI_PUBLIC int INTutil_strcasecmp(CASECMPARG_T char *one, CASECMPARG_T char *two);
#endif /* NEED_STRCASECMP */

#ifdef NEED_STRNCASECMP
NSAPI_PUBLIC int INTutil_strncasecmp(CASECMPARG_T char *one, CASECMPARG_T char *two, int n);
#endif /* NEED_STRNCASECMP */

/* --- End common function prototypes --- */

/* --- Begin Unix-only function prototypes --- */

#ifdef XP_UNIX

NSAPI_PUBLIC int INTutil_can_exec(struct stat *finfo, uid_t uid, gid_t gid);

NSAPI_PUBLIC
struct passwd *INTutil_getpwnam(const char *name, struct passwd *result,
                               char *buffer,  int buflen);

NSAPI_PUBLIC pid_t INTutil_waitpid(pid_t pid, int *statptr, int options);

#endif /* XP_UNIX */

/* --- End Unix-only function prototypes --- */

/* --- Begin Windows-only function prototypes --- */

#ifdef XP_WIN32

NSAPI_PUBLIC
VOID INTutil_delete_directory(char *FileName, BOOL delete_directory);

#endif /* XP_WIN32 */

/* --- End Windows-only function prototypes --- */

NSPR_END_EXTERN_C

#define util_getline INTutil_getline
#define util_env_create INTutil_env_create
#define util_env_str INTutil_env_str
#define util_env_replace INTutil_env_replace
#define util_env_free INTutil_env_free
#define util_env_copy INTutil_env_copy
#define util_env_find INTutil_env_find
#define util_hostname INTutil_hostname
#define util_chdir2path INTutil_chdir2path
#define util_is_mozilla INTutil_is_mozilla
#define util_is_url INTutil_is_url
#define util_later_than INTutil_later_than
#define util_time_equal INTutil_time_equal
#define util_str_time_equal INTutil_str_time_equal
#define util_uri_is_evil INTutil_uri_is_evil
#define util_uri_parse INTutil_uri_parse
#define util_uri_unescape INTutil_uri_unescape
#define util_uri_escape INTutil_uri_escape
#define util_url_escape INTutil_url_escape
#define util_sh_escape INTutil_sh_escape
#define util_mime_separator INTutil_mime_separator
#define util_itoa INTutil_itoa
#define util_vsprintf INTutil_vsprintf
#define util_sprintf INTutil_sprintf
#define util_vsnprintf INTutil_vsnprintf
#define util_snprintf INTutil_snprintf
#define util_strftime INTutil_strftime
#define util_strcasecmp INTutil_strcasecmp
#define util_strncasecmp INTutil_strncasecmp
#define util_strtok INTutil_strtok
#define util_localtime INTutil_localtime
#define util_ctime INTutil_ctime
#define util_strerror INTutil_strerror
#define util_gmtime INTutil_gmtime
#define util_asctime INTutil_asctime

#ifdef XP_UNIX
#define util_can_exec INTutil_can_exec
#define util_getpwnam INTutil_getpwnam
#define util_waitpid INTutil_waitpid
#endif /* XP_UNIX */

#ifdef XP_WIN32
#define util_delete_directory INTutil_delete_directory
#endif /* XP_WIN32 */

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


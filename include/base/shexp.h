/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef BASE_SHEXP_H
#define BASE_SHEXP_H

#ifndef NOINTNSAPI
#define INTNSAPI
#endif /* !NOINTNSAPI */

/*
 * shexp.h: Defines and prototypes for shell exp. match routines
 * 
 *
 * This routine will match a string with a shell expression. The expressions
 * accepted are based loosely on the expressions accepted by zsh.
 * 
 * o * matches anything
 * o ? matches one character
 * o \ will escape a special character
 * o $ matches the end of the string
 * o [abc] matches one occurence of a, b, or c. The only character that needs
 *         to be escaped in this is ], all others are not special.
 * o [a-z] matches any character between a and z
 * o [^az] matches any character except a or z
 * o ~ followed by another shell expression will remove any pattern
 *     matching the shell expression from the match list
 * o (foo|bar) will match either the substring foo, or the substring bar.
 *             These can be shell expressions as well.
 * 
 * The public interface to these routines is documented in
 * public/base/shexp.h.
 * 
 * Rob McCool
 * 
 */

/*
 * Requires that the macro MALLOC be set to a "safe" malloc that will 
 * exit if no memory is available. If not under MCC httpd, define MALLOC
 * to be the real malloc and play with fire, or make your own function.
 */

#ifndef NETSITE_H
#include "../netsite.h"
#endif /* !NETSITE_H */

#ifndef OS_CTYPE_H
#include <ctype.h>  /* isalnum */
#define OS_CTYPE_H
#endif /* !OS_CTYPE_H */

#ifndef OS_STRING_H
#include <string.h> /* strlen */
#define OS_STRING_H
#endif /* !OS_STRING_H */

/* See public/base/shexp.h or public/base/regexp.h concerning USE_REGEX */

/*
 * This little bit of nonsense is because USE_REGEX is currently
 * supposed to be recognized only by the proxy.  If that's the
 * case, only the proxy should define USE_REGEX, but I'm playing
 * it safe.  XXXHEP 12/96
 */
#ifndef MCC_PROXY
#ifdef USE_REGEX
#define SAVED_USE_REGEX USE_REGEX
#undef USE_REGEX
#endif /* USE_REGEX */
#endif /* !MCC_PROXY */

#ifndef PUBLIC_BASE_SHEXP_H
#include "public/base/shexp.h"
#endif /* !PUBLIC_BASE_SHEXP_H */

/* --- Begin function prototypes --- */

#ifdef INTNSAPI

NSPR_BEGIN_EXTERN_C

NSAPI_PUBLIC int INTshexp_valid(char *exp);

NSAPI_PUBLIC int INTshexp_match(char *str, char *exp);

NSAPI_PUBLIC int INTshexp_cmp(char *str, char *exp);

NSAPI_PUBLIC int INTshexp_casecmp(char *str, char *exp);

NSPR_END_EXTERN_C

/* --- End function prototypes --- */

#define shexp_valid INTshexp_valid
#define shexp_match INTshexp_match
#define shexp_cmp INTshexp_cmp
#define shexp_casecmp INTshexp_casecmp

#endif /* INTNSAPI */

/* Restore USE_REGEX definition for non-proxy.  See above. */
#ifdef SAVED_USE_REGEX
#define USE_REGEX SAVED_USE_REGEX
#undef SAVED_USE_REGEX
#endif /* SAVED_USE_REGEX */

#ifdef USE_REGEX
#include "base/regexp.h"
#endif /* USE_REGEX */

#endif /* !BASE_SHEXP_H */

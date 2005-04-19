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

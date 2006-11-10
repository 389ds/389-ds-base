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
 * The public interface to these routines is documented below.
 * 
 * Rob McCool
 * 
 */

#ifndef SHEXP_H
#define SHEXP_H

/*
 * Requires that the macro MALLOC be set to a "safe" malloc that will 
 * exit if no memory is available. If not under MCC httpd, define MALLOC
 * to be the real malloc and play with fire, or make your own function.
 */

#include "../netsite.h"

#include <ctype.h>  /* isalnum */
#include <string.h> /* strlen */


/*
 * Wrappers for shexp/regexp
 *
 * Portions of code that explicitly want to have either shexp's
 * or regexp's should call those functions directly.
 *
 * Common code bases for multiple products should use the following
 * macros instead to use either shell or regular expressions,
 * depending on the flavor chosen for a given server.
 *
 */
#if defined(MCC_PROXY) && defined(USE_REGEX)

#include "base/regexp.h"

#define WILDPAT_VALID(exp)		regexp_valid(exp)
#define WILDPAT_MATCH(str, exp)		regexp_match(str, exp)
#define WILDPAT_CMP(str, exp)		regexp_cmp(str, exp)
#define WILDPAT_CASECMP(str, exp)	regexp_casecmp(str, exp)

#else	/* HTTP servers */

#define WILDPAT_VALID(exp)		shexp_valid(exp)
#define WILDPAT_MATCH(str, exp)		shexp_match(str, exp)
#define WILDPAT_CMP(str, exp)		shexp_cmp(str, exp)
#define WILDPAT_CASECMP(str, exp)	shexp_casecmp(str, exp)

#endif


/* --------------------------- Public routines ---------------------------- */

NSPR_BEGIN_EXTERN_C

/*
 * shexp_valid takes a shell expression exp as input. It returns:
 * 
 *  NON_SXP      if exp is a standard string
 *  INVALID_SXP  if exp is a shell expression, but invalid
 *  VALID_SXP    if exp is a valid shell expression
 */

#define NON_SXP -1
#define INVALID_SXP -2
#define VALID_SXP 1

/* and generic shexp/regexp versions */
#define NON_WILDPAT     NON_SXP
#define INVALID_WILDPAT INVALID_SXP
#define VALID_WILDPAT   VALID_SXP

/* and regexp versions */
#define NON_REGEXP      NON_SXP
#define INVALID_REGEXP  INVALID_SXP
#define VALID_REGEXP    VALID_SXP


NSAPI_PUBLIC int shexp_valid(char *exp);

/*
 * shexp_match 
 * 
 * Takes a prevalidated shell expression exp, and a string str.
 *
 * Returns 0 on match and 1 on non-match.
 */

NSAPI_PUBLIC int shexp_match(char *str, char *exp);


/*
 * shexp_cmp
 * 
 * Same as above, but validates the exp first. 0 on match, 1 on non-match,
 * -1 on invalid exp. shexp_casecmp does the same thing but is case 
 * insensitive.
 */

NSAPI_PUBLIC int shexp_cmp(char *str, char *exp);
NSAPI_PUBLIC int shexp_casecmp(char *str, char *exp);

NSPR_END_EXTERN_C

#endif


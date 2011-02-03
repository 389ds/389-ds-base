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


/* Header file used to declare functions which we beat on heavily as intrinsic */

/* For NT ...*/

#ifdef _WIN32
__inline static int  strcmpi_fast(const char * dst, const char * src)
{
	int f,l;
	do {
		if ( ((f = (unsigned char)(*(dst++))) >= 'A') && (f <= 'Z') )
			f -= ('A' - 'a');
		if ( ((l = (unsigned char)(*(src++))) >= 'A') && (l <= 'Z') )
			l -= ('A' - 'a');
	} while ( f && (f == l) );
	return(f - l);
}
#ifdef strcasecmp
#undef strcasecmp
#endif
#define strcasecmp(x,y) strcmpi_fast(x,y)
#ifdef strcmpi
#undef strcmpi
#endif
#define strcmpi(x,y) strcmpi_fast(x,y)

__inline static int tolower_fast(int c)
{
	if ( (c >= 'A') && (c <= 'Z') )
		c = c + ('a' - 'A');
	return c;
}
#ifdef tolower
#undef tolower
#endif
#define tolower(x) tolower_fast(x)

#else

#if defined(HPUX)
#define INLINE_DIRECTIVE __inline
#elif defined(LINUX)
#define INLINE_DIRECTIVE __inline__
#else
#define INLINE_DIRECTIVE
#endif

INLINE_DIRECTIVE static int strcmpi_fast(const char * dst, const char * src)
{
	int f,l;
	do {
		if ( ((f = (unsigned char)(*(dst++))) >= 'A') && (f <= 'Z') )
			f -= ('A' - 'a');
		if ( ((l = (unsigned char)(*(src++))) >= 'A') && (l <= 'Z') )
			l -= ('A' - 'a');
	} while ( f && (f == l) );
	return(f - l);
}
#ifdef strcasecmp
#undef strcasecmp
#endif
#define strcasecmp(x,y) strcmpi_fast(x,y)
#ifdef strcmpi
#undef strcmpi
#endif
#define strcmpi(x,y) strcmpi_fast(x,y)

INLINE_DIRECTIVE static int tolower_fast(int c)
{
	if ( (c >= 'A') && (c <= 'Z') )
		c = c + ('a' - 'A');
	return c;
}
#ifdef tolower
#undef tolower
#endif
#define tolower(x) tolower_fast(x)

INLINE_DIRECTIVE static int toupper_fast(int c)
{
    if ( (c >= 'a') && (c <= 'z') )
	c = c - ('a' - 'A');
    return c;
}
#ifdef toupper
#undef toupper
#endif
#define toupper(x) toupper_fast(x)

INLINE_DIRECTIVE static int strncasecmp_fast(const char * dst, const char * src, int n)
{
	int f,l,x=0;
	do {
		if ( ((f = (unsigned char)(*(dst++))) >= 'A') && (f <= 'Z') )
			f -= ('A' - 'a');
		if ( ((l = (unsigned char)(*(src++))) >= 'A') && (l <= 'Z') )
			l -= ('A' - 'a');
	} while ( f && (f == l) && ++x < n );
	return(f - l);
}

#ifdef strncasecmp
#undef strncasecmp
#endif
#define strncasecmp(x,y,z) strncasecmp_fast(x,y,z)
#endif /* NT */

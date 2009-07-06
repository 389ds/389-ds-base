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
 * END COPYRIGHT BLOCK **/
/* ***** BEGIN LICENSE BLOCK *****
 * 
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 * 
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998-1999
 * the Initial Developer. All Rights Reserved.
 * 
 * Contributor(s):
 * 
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * June 25, 2009 - copied from Mozilla LDAP C SDK - relicensed to use GPLv2
 * with directory server plug-in exception as per the above paragraph
 * 
 * ***** END LICENSE BLOCK ***** */

/* the openldap library has utf8 string handling functions, but they
   are somewhat different, and not exposed/exported for use outside
   of the library - therefore, we just copy these from mozldap when
   using openldap
*/
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#if defined(USE_OPENLDAP)

/* uft8.c - misc. utf8 "string" functions. */
#include "slapi-plugin.h"

static char UTF8len[64]
= {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 6};

int
ldap_utf8len (const char* s)
     /* Return the number of char's in the character at *s. */
{
    return ldap_utf8next((char*)s) - s;
}

char*
ldap_utf8next (char* s)
     /* Return a pointer to the character immediately following *s.
	Handle any valid UTF-8 character, including '\0' and ASCII.
	Try to handle a misaligned pointer or a malformed character.
     */
{
    register unsigned char* next = (unsigned char*)s;
    switch (UTF8len [(*next >> 2) & 0x3F]) {
      case 0: /* erroneous: s points to the middle of a character. */
      case 6: if ((*++next & 0xC0) != 0x80) break;
      case 5: if ((*++next & 0xC0) != 0x80) break;
      case 4: if ((*++next & 0xC0) != 0x80) break;
      case 3: if ((*++next & 0xC0) != 0x80) break;
      case 2: if ((*++next & 0xC0) != 0x80) break;
      case 1: ++next;
    }
    return (char*) next;
}

char*
ldap_utf8prev (char* s)
     /* Return a pointer to the character immediately preceding *s.
	Handle any valid UTF-8 character, including '\0' and ASCII.
	Try to handle a misaligned pointer or a malformed character.
     */
{
    register unsigned char* prev = (unsigned char*)s;
    unsigned char* limit = prev - 6;
    while (((*--prev & 0xC0) == 0x80) && (prev != limit)) {
    	;
    }
    return (char*) prev;
}

int
ldap_utf8copy (char* dst, const char* src)
     /* Copy a character from src to dst; return the number of char's copied.
	Handle any valid UTF-8 character, including '\0' and ASCII.
	Try to handle a misaligned pointer or a malformed character.
     */
{
    register const unsigned char* s = (const unsigned char*)src;
    switch (UTF8len [(*s >> 2) & 0x3F]) {
      case 0: /* erroneous: s points to the middle of a character. */
      case 6: *dst++ = *s++; if ((*s & 0xC0) != 0x80) break;
      case 5: *dst++ = *s++; if ((*s & 0xC0) != 0x80) break;
      case 4: *dst++ = *s++; if ((*s & 0xC0) != 0x80) break;
      case 3: *dst++ = *s++; if ((*s & 0xC0) != 0x80) break;
      case 2: *dst++ = *s++; if ((*s & 0xC0) != 0x80) break;
      case 1: *dst   = *s++;
    }
    return s - (const unsigned char*)src;
}

size_t
ldap_utf8characters (const char* src)
     /* Return the number of UTF-8 characters in the 0-terminated array s. */
{
    register char* s = (char*)src;
    size_t n;
    for (n = 0; *s; LDAP_UTF8INC(s)) ++n;
    return n;
}

unsigned long
ldap_utf8getcc( const char** src )
{
    register unsigned long c = 0;
    register const unsigned char* s = (const unsigned char*)*src;
    switch (UTF8len [(*s >> 2) & 0x3F]) {
      case 0: /* erroneous: s points to the middle of a character. */
	      c = (*s++) & 0x3F; goto more5;
      case 1: c = (*s++); break;
      case 2: c = (*s++) & 0x1F; goto more1;
      case 3: c = (*s++) & 0x0F; goto more2;
      case 4: c = (*s++) & 0x07; goto more3;
      case 5: c = (*s++) & 0x03; goto more4;
      case 6: c = (*s++) & 0x01; goto more5;
      more5: if ((*s & 0xC0) != 0x80) break; c = (c << 6) | ((*s++) & 0x3F);
      more4: if ((*s & 0xC0) != 0x80) break; c = (c << 6) | ((*s++) & 0x3F);
      more3: if ((*s & 0xC0) != 0x80) break; c = (c << 6) | ((*s++) & 0x3F);
      more2: if ((*s & 0xC0) != 0x80) break; c = (c << 6) | ((*s++) & 0x3F);
      more1: if ((*s & 0xC0) != 0x80) break; c = (c << 6) | ((*s++) & 0x3F);
	break;
    }
    *src = (const char*)s;
    return c;
}

char*
ldap_utf8strtok_r( char* sp, const char* brk, char** next)
{
    const char *bp;
    unsigned long sc, bc;
    char *tok;

    if (sp == NULL && (sp = *next) == NULL)
      return NULL;

    /* Skip leading delimiters; roughly, sp += strspn(sp, brk) */
  cont:
    sc = LDAP_UTF8GETC(sp);
    for (bp = brk; (bc = LDAP_UTF8GETCC(bp)) != 0;) {
	if (sc == bc)
	  goto cont;
    }

    if (sc == 0) { /* no non-delimiter characters */
	*next = NULL;
	return NULL;
    }
    tok = LDAP_UTF8PREV(sp);

    /* Scan token; roughly, sp += strcspn(sp, brk)
     * Note that brk must be 0-terminated; we stop if we see that, too.
     */
    while (1) {
	sc = LDAP_UTF8GETC(sp);
	bp = brk;
	do {
	    if ((bc = LDAP_UTF8GETCC(bp)) == sc) {
		if (sc == 0) {
		    *next = NULL;
		} else {
		    *next = sp;
		    *(LDAP_UTF8PREV(sp)) = 0;
		}
		return tok;
	    }
	} while (bc != 0);
    }
    /* NOTREACHED */
}

int
ldap_utf8isalnum( char* s )
{
    register unsigned char c = *(unsigned char*)s;
    if (0x80 & c) return 0;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= '0' && c <= '9') return 1;
    return 0;
}

int
ldap_utf8isalpha( char* s )
{
    register unsigned char c = *(unsigned char*)s;
    if (0x80 & c) return 0;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= 'a' && c <= 'z') return 1;
    return 0;
}

int
ldap_utf8isdigit( char* s )
{
    register unsigned char c = *(unsigned char*)s;
    if (0x80 & c) return 0;
    if (c >= '0' && c <= '9') return 1;
    return 0;
}

int
ldap_utf8isxdigit( char* s )
{
    register unsigned char c = *(unsigned char*)s;
    if (0x80 & c) return 0;
    if (c >= '0' && c <= '9') return 1;
    if (c >= 'A' && c <= 'F') return 1;
    if (c >= 'a' && c <= 'f') return 1;
    return 0;
}

int
ldap_utf8isspace( char* s )
{
    register unsigned char *c = (unsigned char*)s;
    int len = ldap_utf8len(s);

    if (len == 0) {
	return 0;
    } else if (len == 1) {
	switch (*c) {
	    case 0x09:
	    case 0x0A:
	    case 0x0B:
	    case 0x0C:
	    case 0x0D:
	    case 0x20:
		return 1;
	    default:
		return 0;
	}
    } else if (len == 2) {
	if (*c == 0xc2) {
		return *(c+1) == 0x80;
	}
    } else if (len == 3) {
	if (*c == 0xE2) {
	    c++;
	    if (*c == 0x80) {
		c++;
		return (*c>=0x80 && *c<=0x8a);
	    }
	} else if (*c == 0xE3) {
	    return (*(c+1)==0x80) && (*(c+2)==0x80);
	} else if (*c==0xEF) {
	    return (*(c+1)==0xBB) && (*(c+2)==0xBF);
	}
	return 0;
    }

    /* should never reach here */
    return 0;
}

#endif /* USE_OPENLDAP */

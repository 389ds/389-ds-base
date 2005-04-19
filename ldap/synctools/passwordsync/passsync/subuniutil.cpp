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

#include "subuniutil.h"

// Copied: 2-8-2005
// From: secuniutil.c
unsigned long
utf8getcc( const char** src )
{
    register unsigned long c;
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
//
wchar_t *
ASCIIToUnicode( const char *buf, wchar_t *uni, int inUnilen )
     /* Convert the 0-terminated UTF-8 string 'buf' to 0-terminated UCS-2;
        write the result into uni, truncated (if necessary) to fit in 0..unilen-1. */
     /* XXX This function should be named UTF8ToUnicode */
     /* XXX unilen should be size_t, not int */
{
	auto size_t unilen = (size_t)inUnilen; /* to get rid of warnings for now */
    auto size_t i;
    if (unilen > 0 && buf && uni) {
		for (i = 0; i < unilen; ++i) {
			register unsigned long c = utf8getcc( &buf );
			if (c >= 0xfffeUL) c = 0xfffdUL; /* REPLACEMENT CHARACTER */
			if (0 == (uni[i] = (wchar_t)c)) break;
		}
		if (i >= unilen && unilen > 0) {
			uni[unilen-1] = 0;
		}
	}
    return uni;
}

wchar_t *
StrToUnicode( const char *buf )
{
	wchar_t unibuf[1024];
	ASCIIToUnicode( buf, unibuf, sizeof(unibuf) );
	return _wcsdup( unibuf );
}
// End Copy
/** BEGIN COPYRIGHT BLOCK
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
#ifndef _SUBUNIUTIL_H_
#define _SUBUNIUTIL_H_

#include <windows.h>

// Copied: 2-8-2005
// From: secuniutil.c
/* From ns/netsite/ldap/libraries/libldap/utf8.c */
static char UTF8len[64]
= {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 6};
// End Copy

unsigned long utf8getcc( const char** src );
wchar_t * ASCIIToUnicode( const char *buf, wchar_t *uni, int inUnilen );
wchar_t * StrToUnicode( const char *buf );

#endif

/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* syntax.h - string syntax definitions */

#ifndef _LIBSYNTAX_H_
#define _LIBSYNTAX_H_

#define SLAPD_LOGGING	1

#include "slap.h"
#include "slapi-plugin.h"
#include <dirlite_strings.h> /* PLUGIN_MAGIC_VENDOR_STR */

#define SYNTAX_CIS		1
#define SYNTAX_CES		2
#define SYNTAX_TEL		4	/* telephone number: used with SYNTAX_CIS */
#define SYNTAX_DN		8	/* distinguished name: used with SYNTAX_CIS */
#define SYNTAX_SI		16	/* space insensitive: used with SYNTAX_CIS */

#define SUBLEN	3

#ifndef MIN
#define MIN( a, b )	(a < b ? a : b )
#endif

int string_filter_sub( Slapi_PBlock *pb, char *initial, char **any, char *final,Slapi_Value **bvals, int syntax );
int string_filter_ava( struct berval *bvfilter, Slapi_Value **bvals, int syntax,int ftype, Slapi_Value **retVal );
int string_values2keys( Slapi_PBlock *pb, Slapi_Value **bvals,Slapi_Value ***ivals, int syntax, int ftype );
int string_assertion2keys_ava(Slapi_PBlock *pb,Slapi_Value *val,Slapi_Value ***ivals,int syntax,int ftype  );
int string_assertion2keys_sub(Slapi_PBlock *pb,char *initial,char **any,char *final,Slapi_Value ***ivals,int syntax);
int value_cmp(struct berval	*v1,struct berval *v2,int syntax,int normalize);
void value_normalize(char *s,int syntax,int trim_leading_blanks);

char *first_word( char *s );
char *next_word( char *s );
char *phonetic( char *s );


#endif

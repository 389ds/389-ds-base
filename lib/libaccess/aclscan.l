/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

/*
 * Lexical analyzer for ACL
 */

%{
#include "acl.tab.h"	/* token codes */
#include <base/file.h>
#include <libaccess/acl.h>
#include <libaccess/nserror.h>
#include "aclpriv.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <libaccess/aclerror.h>
#include "plstr.h"
#include "parse.h"
#include "aclscan.h"

static int acl_scanner_input(char *buffer, int max_size);

#define YY_NEVER_INTERACTIVE 1
#undef YY_INPUT
#define YY_INPUT(buf, result, max) (result = acl_scanner_input(buf, max))

static int acl_lineno;
static int acl_tokenpos;
static char acl_filename[500];
static NSErr_t *acl_errp;
static int acl_use_buffer;
static char *acl_buffer;
static int acl_buffer_length;
static int acl_buffer_offset;
static char *last_string;
static SYS_FILE acl_prfd;


%}

ws		[ \t]+
comment		#.*
qstring		\"[^\"\n]*[\"\n]
variable	[\*a-zA-Z0-9\.\-\_][\*a-zA-Z0-9\.\-\_]*

%%

\n.*		{ 
			acl_lineno++;
			acl_tokenpos = 0;
			yyless(1);
		}

{ws}		;

{comment}	;

<<EOF>>		{	
			yylval.string = NULL;
			if (last_string) {
				PERM_FREE(last_string);
				last_string = NULL;
			}
			return(0);
		}

{qstring}	{ 
			if (last_string) {
				PERM_FREE(last_string);
				last_string = NULL;
			}
			yylval.string = PERM_STRDUP( yytext+1 );
			last_string = yylval.string;
			if ( yylval.string[yyleng-2] != '"' )
				fprintf(stderr, "Unterminated string\n") ;
			else
				yylval.string[yyleng-2] = '\0';
			acl_tokenpos += yyleng;
			return ACL_QSTRING_TOK;
		}


absolute	{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			acl_tokenpos += yyleng; 
			return ACL_ABSOLUTE_TOK; 
		}

acl		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			acl_tokenpos += yyleng; 
			return ACL_ACL_TOK; 
		}

allow		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			acl_tokenpos += yyleng; 
			return ACL_ALLOW_TOK; 
		}

always		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			acl_tokenpos += yyleng; 
			return ACL_ALWAYS_TOK; 
		}

at		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			acl_tokenpos += yyleng; 
			return ACL_AT_TOK; 
		}

authenticate	{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			acl_tokenpos += yyleng; 
			return ACL_AUTHENTICATE_TOK; 
		}

content		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			acl_tokenpos += yyleng; 
			return ACL_CONTENT_TOK; 
		}

default		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			acl_tokenpos += yyleng; 
			return ACL_DEFAULT_TOK; 
		}

deny		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			acl_tokenpos += yyleng; 
			return ACL_DENY_TOK; 
		}

in		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			acl_tokenpos += yyleng; 
			return ACL_IN_TOK; 
		}

inherit		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			acl_tokenpos += yyleng; 
			return ACL_INHERIT_TOK; 
		}

terminal	{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			acl_tokenpos += yyleng; 
			return ACL_TERMINAL_TOK; 
		}

version		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			acl_tokenpos += yyleng; 
			return ACL_VERSION_TOK; 
		}

with		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			acl_tokenpos += yyleng; 
			return ACL_WITH_TOK; 
		}

not		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			acl_tokenpos += yyleng; 
			return ACL_NOT_TOK; 
		}

and 		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			yylval.ival = ACL_EXPR_OP_AND;
			acl_tokenpos += yyleng; 
			return ACL_AND_TOK; 
		}

or		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			yylval.ival = ACL_EXPR_OP_OR;
			acl_tokenpos += yyleng; 
			return ACL_OR_TOK; 
		}

"="		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			yylval.ival = CMP_OP_EQ;
			acl_tokenpos += yyleng; 
			return ACL_EQ_TOK; 
		}

">="		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			yylval.ival = CMP_OP_GE; 
			acl_tokenpos += yyleng; 
			return ACL_GE_TOK; 
		}

">"		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			yylval.ival = CMP_OP_GT; 
			acl_tokenpos += yyleng; 
			return ACL_GT_TOK; 
		}

"<"		{ 	
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			yylval.ival = CMP_OP_LT; 
			acl_tokenpos += yyleng; 
			return ACL_LT_TOK; 
		}

"<="		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			yylval.ival = CMP_OP_LE; 
			acl_tokenpos += yyleng; 
			return ACL_LE_TOK; 
		}

"!="		{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			yylval.ival = CMP_OP_NE;
			acl_tokenpos += yyleng; 
			return ACL_NE_TOK; 
		}

[(){},;]	{ 
			if (last_string) {
				PERM_FREE(last_string);
			}
			last_string = NULL;
			acl_tokenpos += yyleng; 
			return yytext[0]; 
		}

{variable}	{ 
			acl_tokenpos += yyleng; 
			if (last_string) {
				PERM_FREE(last_string);
				last_string = NULL;
			}
			yylval.string = PERM_STRDUP( yytext );
			last_string = yylval.string;
			return ACL_VARIABLE_TOK;
		}
%%

void
acl_detab(char *t, char *s)
{
	int	ii;
	int	pos;
	int	len;

	if (s == NULL || t == NULL) 
		return;

	len = strlen(s);
	pos = 0;
	for (ii = 0; ii < len; ii++) {
		if (s[ii] == '\t') {
			t[pos] = ' ';
		} else {
			t[pos] = s[ii];
		}
		pos++;
	}
	t[pos] = '\0';
	return;
}

void
yyerror(const char *s)
{
char	errorStr[256];

#if defined(UTEST) || defined(ACL_COMPILER)
	printf("ACL file: %s\n", acl_filename);
	printf("Syntax error at line: %d, token: %s\n", acl_lineno, yytext);
	if ( last_string ) {
		PERM_FREE(last_string);
		last_string = NULL;
	}
#else
	sprintf(errorStr, "%d", acl_lineno);
	if (yytext) {
		nserrGenerate(acl_errp, ACLERRPARSE, ACLERR1780, ACL_Program,
                      3, acl_filename, errorStr, yytext);
	} else {
		nserrGenerate(acl_errp, ACLERRPARSE, ACLERR1780, ACL_Program,
                      2, acl_filename, errorStr);
	}
	if ( last_string ) {
		PERM_FREE(last_string);
		last_string = NULL;
	}
#endif

}

int
acl_InitScanner(NSErr_t *errp, char *filename, char *buffer)
{
	acl_errp = errp;
	acl_lineno = 1;
	last_string = NULL;
	acl_use_buffer = (filename == NULL) ? 1 : 0 ;
	if ( filename != NULL ) {
		PL_strncpyz(acl_filename, filename, sizeof(acl_filename));
#ifdef UTEST
		yyin = fopen(filename, "r");
		if ( yyin == NULL ) {
			return(-1);
		}
#else
		acl_prfd = system_fopenRO(filename);
		if ( acl_prfd == NULL ) {
			return(-1);
		}
		yyin = (FILE *) acl_prfd;
#endif
		yyrestart(yyin);
	} else if ( buffer != NULL ) {
		strcpy(acl_filename, "internal-buffer");
		acl_buffer_offset = 0;
		acl_buffer_length = strlen(buffer);
		acl_buffer = PERM_STRDUP(buffer);
		if (acl_buffer == NULL)
			return(-1);
                yyrestart(NULL);
        } else {
		return(-1);
        }
	return(0);
}

int
acl_EndScanner()
{
	acl_lineno = 0;
	if ( acl_use_buffer) {
		if ( acl_buffer )
			PERM_FREE(acl_buffer);
        } else if ( yyin ) {
#ifdef UTEST
		fclose(yyin);
#else
		if ( acl_prfd ) {
			system_fclose(acl_prfd);
			acl_prfd = NULL;
		}
#endif
		yyin = NULL ;
	}

	if ( last_string ) {
		PERM_FREE(last_string);
		last_string = NULL;
	}

	return(0);
}

int
yywrap()
{
	return(1);
}

static int
acl_scanner_input(char *buffer, int max_size)
{
	int len = 0;

        if ( acl_use_buffer ) {
		len = (acl_buffer_length > max_size) ? max_size : 
                                                       acl_buffer_length;
		memcpy(buffer, (const void *) &acl_buffer[acl_buffer_offset], 
                       len);
		acl_buffer_length -= len;
		acl_buffer_offset += len;
        }
#ifdef UTEST
        else if ( ((len = fread( buffer, 1, max_size, aclin )) == 0) 
                    && ferror( aclin ) ) { 
                yyerror( "scanner input failed" );
	}
#else
        else if ( (len = system_fread( acl_prfd, buffer, max_size)) < 0 ) { 
                yyerror( "scanner input failed" );
	}
#endif

	return(len);
}

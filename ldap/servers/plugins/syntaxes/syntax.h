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

/* syntax.h - string syntax definitions */

#ifndef _LIBSYNTAX_H_
#define _LIBSYNTAX_H_

#include "slap.h"
#include "slapi-plugin.h"

#define SYNTAX_CIS		1
#define SYNTAX_CES		2
#define SYNTAX_TEL		4	/* telephone number: used with SYNTAX_CIS */
#define SYNTAX_DN		8	/* distinguished name: used with SYNTAX_CIS */
#define SYNTAX_SI		16	/* space insensitive: used with SYNTAX_CIS */
#define SYNTAX_INT		32	/* INTEGER */
#define SYNTAX_NORM_FILT	64	/* filter already normalized */

#define SUBBEGIN	3
#define SUBMIDDLE	3
#define SUBEND		3

#define SYNTAX_PLUGIN_SUBSYSTEM "syntax-plugin"

/* The following are derived from RFC 4512, section 1.4. */
#define IS_LEADKEYCHAR(c)	( isalpha(c) )
#define IS_KEYCHAR(c)		( isalnum(c) || (c == '-') )
#define IS_SPACE(c)		( (c == ' ') )
#define IS_LDIGIT(c)		( (c != '0') && isdigit(c) )
#define IS_SHARP(c)		( (c == '#') )
#define IS_DOLLAR(c)		( (c == '$') )
#define IS_SQUOTE(c)		( (c == '\'') )
#define IS_ESC(c)		( (c == '\\') )
#define IS_LPAREN(c)		( (c == '(') )
#define IS_RPAREN(c)		( (c == ')') )
#define IS_COLON(c)		( (c == ':') )
#define IS_UTF0(c)		( ((unsigned char)(c) >= (unsigned char)'\x80') && ((unsigned char)(c) <= (unsigned char)'\xBF') )
#define IS_UTF1(c)		( !((unsigned char)(c) & 128) )
/* These are only checking the first byte of the multibyte character.  They
 * do not verify that the entire multibyte character is correct. */
#define IS_UTF2(c)		( ((unsigned char)(c) >= (unsigned char)'\xC2') && ((unsigned char)(c) <= (unsigned char)'\xDF') )
#define IS_UTF3(c)		( ((unsigned char)(c) >= (unsigned char)'\xE0') && ((unsigned char)(c) <= (unsigned char)'\xEF') )
#define IS_UTF4(c)		( ((unsigned char)(c) >= (unsigned char)'\xF0') && ((unsigned char)(c) <= (unsigned char)'\xF4') )
#define IS_UTFMB(c)		( IS_UTF2(c) || IS_UTF3(c) || IS_UTF4(c) )
#define IS_UTF8(c)		( IS_UTF1(c) || IS_UTFMB(c) )

/* The following are derived from RFC 4514, section 3. */
#define IS_ESCAPED(c)		( (c == '"') || (c == '+') || (c == ',') || \
	(c == ';') || (c == '<') || (c == '>') )
#define IS_SPECIAL(c)		( IS_ESCAPED(c) || IS_SPACE(c) || \
	IS_SHARP(c) || (c == '=') )
#define IS_LUTF1(c)		( IS_UTF1(c) && !IS_ESCAPED(c) && !IS_SPACE(c) && \
	!IS_SHARP(c) && !IS_ESC(c) )
#define IS_TUTF1(c)		( IS_UTF1(c) && !IS_ESCAPED(c) && !IS_SPACE(c) && \
	!IS_ESC(c) )
#define IS_SUTF1(c)		( IS_UTF1(c) && !IS_ESCAPED(c) && !IS_ESC(c) )

/* Per RFC 4517:
 *
 *   PrintableCharacter = ALPHA / DIGIT / SQUOTE / LPAREN / RPAREN /
 *                        PLUS / COMMA / HYPHEN / DOT / EQUALS /
 *                        SLASH / COLON / QUESTION / SPACE
 */
#define IS_PRINTABLE(c)	( isalnum(c) || (c == '\'') || IS_LPAREN(c) || \
	IS_RPAREN(c) || (c == '+') || (c == ',') || (c == '-') || (c == '.') || \
	(c == '=') || (c == '/') || (c == ':') || (c == '?') || IS_SPACE(c) )

int string_filter_sub( Slapi_PBlock *pb, char *initial, char **any, char *final,Slapi_Value **bvals, int syntax );
int string_filter_ava( struct berval *bvfilter, Slapi_Value **bvals, int syntax,int ftype, Slapi_Value **retVal );
int string_values2keys( Slapi_PBlock *pb, Slapi_Value **bvals,Slapi_Value ***ivals, int syntax, int ftype );
int string_assertion2keys_ava(Slapi_PBlock *pb,Slapi_Value *val,Slapi_Value ***ivals,int syntax,int ftype  );
int string_assertion2keys_sub(Slapi_PBlock *pb,char *initial,char **any,char *final,Slapi_Value ***ivals,int syntax);
int value_cmp(struct berval	*v1,struct berval *v2,int syntax,int normalize);
void value_normalize(char *s,int syntax,int trim_leading_blanks);
void value_normalize_ext(char *s,int syntax,int trim_leading_blanks, char **alt);

char *first_word( char *s );
char *next_word( char *s );
char *phonetic( char *s );

/* Validation helper functions */
int keystring_validate( const char *begin, const char *end );
int numericoid_validate( const char *begin, const char *end );
int utf8char_validate( const char *begin, const char *end, const char **last );
int utf8string_validate( const char *begin, const char *end, const char **last );
int distinguishedname_validate( const char *begin, const char *end );
int rdn_validate( const char *begin, const char *end, const char **last );
int bitstring_validate_internal(const char *begin, const char *end);

struct mr_plugin_def {
    Slapi_MatchingRuleEntry mr_def_entry; /* for slapi_matchingrule_register */
    Slapi_PluginDesc mr_plg_desc; /* for SLAPI_PLUGIN_DESCRIPTION */
    const char **mr_names; /* list of oid and names, NULL terminated SLAPI_PLUGIN_MR_NAMES */
    /* these are optional for new style mr plugins */
    IFP	mr_filter_create; /* old style factory function SLAPI_PLUGIN_MR_FILTER_CREATE_FN */
    IFP	mr_indexer_create; /* old style factory function SLAPI_PLUGIN_MR_INDEXER_CREATE_FN */
    /* new style syntax plugin functions */
    /* not all functions will apply to all matching rule types */
    /* e.g. a SUBSTR rule will not have a filter_ava func */
    IFP	mr_filter_ava; /* SLAPI_PLUGIN_MR_FILTER_AVA */
    IFP	mr_filter_sub; /* SLAPI_PLUGIN_MR_FILTER_SUB */
    IFP	mr_values2keys; /* SLAPI_PLUGIN_MR_VALUES2KEYS */
    IFP	mr_assertion2keys_ava; /* SLAPI_PLUGIN_MR_ASSERTION2KEYS_AVA */
    IFP	mr_assertion2keys_sub; /* SLAPI_PLUGIN_MR_ASSERTION2KEYS_SUB */
    IFP	mr_compare; /* SLAPI_PLUGIN_MR_COMPARE - only for ORDERING */
};

int syntax_register_matching_rule_plugins(struct mr_plugin_def mr_plugin_table[], size_t mr_plugin_table_size, IFP matching_rule_plugin_init);
int syntax_matching_rule_plugin_init(Slapi_PBlock *pb, struct mr_plugin_def mr_plugin_table[], size_t mr_plugin_table_size);

#endif

#ifdef UNSUPPORTED_MATCHING_RULES
/* list of names/oids/aliases for each matching rule */
static const char *keywordMatch_names[] = {"keywordMatch", "2.5.13.33", NULL};
static const char *wordMatch_names[] = {"wordMatch", "2.5.13.32", NULL};
/* table of matching rule plugin defs for mr register and plugin register */
static struct mr_plugin_def mr_plugin_table[] = {
{{"2.5.13.33", NULL, "keywordMatch", "The keywordMatch rule compares an assertion value of the Directory"
"String syntax to an attribute value of a syntax (e.g., the Directory"
"String syntax) whose corresponding ASN.1 type is DirectoryString."
"The rule evaluates to TRUE if and only if the assertion value"
"character string matches any keyword in the attribute value.  The"
"identification of keywords in the attribute value and the exactness"
"of the match are both implementation specific.", "1.3.6.1.4.1.1466.115.121.1.15", 0}, /* matching rule desc */
 {"keywordMatch-mr", VENDOR, DS_PACKAGE_VERSION, "keywordMatch matching rule plugin"}, /* plugin desc */
   keywordMatch_names, /* matching rule name/oid/aliases */
   NULL, NULL, mr_filter_ava, mr_filter_sub, mr_values2keys,
   mr_assertion2keys_ava, mr_assertion2keys_sub, mr_compare, keywordMatch_syntaxes},,
{{"2.5.13.32", NULL, "wordMatch", "The wordMatch rule compares an assertion value of the Directory"
"String syntax to an attribute value of a syntax (e.g., the Directory"
"String syntax) whose corresponding ASN.1 type is DirectoryString."
"The rule evaluates to TRUE if and only if the assertion value word"
"matches, according to the semantics of caseIgnoreMatch, any word in"
"the attribute value.  The precise definition of a word is"
"implementation specific.", "1.3.6.1.4.1.1466.115.121.1.15", 0}, /* matching rule desc */
 {"wordMatch-mr", VENDOR, DS_PACKAGE_VERSION, "wordMatch matching rule plugin"}, /* plugin desc */
   wordMatch_names, /* matching rule name/oid/aliases */
   NULL, NULL, mr_filter_ava, mr_filter_sub, mr_values2keys,
   mr_assertion2keys_ava, mr_assertion2keys_sub, mr_compare, wordMatch_syntaxes},
};
#endif /* UNSUPPORTED_MATCHING_RULES */

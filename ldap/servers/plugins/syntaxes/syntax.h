/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* syntax.h - string syntax definitions */

#ifndef _LIBSYNTAX_H_
#define _LIBSYNTAX_H_

#include "slap.h"
#include "slapi-plugin.h"

#define SYNTAX_CIS        1
#define SYNTAX_CES        2
#define SYNTAX_TEL        4 /* telephone number: used with SYNTAX_CIS */
#define SYNTAX_DN         8 /* distinguished name: used with SYNTAX_CIS */
#define SYNTAX_SI        16 /* space insensitive: used with SYNTAX_CIS */
#define SYNTAX_INT       32 /* INTEGER */
#define SYNTAX_NORM_FILT 64 /* filter already normalized */

#define SUBBEGIN  3
#define SUBMIDDLE 3
#define SUBEND    3

#define SYNTAX_PLUGIN_SUBSYSTEM "syntax-plugin"

/* The following are derived from RFC 4512, section 1.4. */
#define IS_LEADKEYCHAR(c) (isalpha(c))
#define IS_KEYCHAR(c)     (isalnum(c) || (c == '-'))
#define IS_SPACE(c)       ((c == ' '))
#define IS_LDIGIT(c)      ((c != '0') && isdigit(c))
#define IS_SHARP(c)       ((c == '#'))
#define IS_DOLLAR(c)      ((c == '$'))
#define IS_SQUOTE(c)      ((c == '\''))
#define IS_ESC(c)         ((c == '\\'))
#define IS_LPAREN(c)      ((c == '('))
#define IS_RPAREN(c)      ((c == ')'))
#define IS_COLON(c)       ((c == ':'))
#define IS_UTF0(c) (((unsigned char)(c) >= (unsigned char)'\x80') && ((unsigned char)(c) <= (unsigned char)'\xBF'))
#define IS_UTF1(c) (!((unsigned char)(c)&128))
/* These are only checking the first byte of the multibyte character.  They
 * do not verify that the entire multibyte character is correct. */
#define IS_UTF2(c)  (((unsigned char)(c) >= (unsigned char)'\xC2') && ((unsigned char)(c) <= (unsigned char)'\xDF'))
#define IS_UTF3(c)  (((unsigned char)(c) >= (unsigned char)'\xE0') && ((unsigned char)(c) <= (unsigned char)'\xEF'))
#define IS_UTF4(c)  (((unsigned char)(c) >= (unsigned char)'\xF0') && ((unsigned char)(c) <= (unsigned char)'\xF4'))
#define IS_UTFMB(c) (IS_UTF2(c) || IS_UTF3(c) || IS_UTF4(c))
#define IS_UTF8(c)  (IS_UTF1(c) || IS_UTFMB(c))

/* The following are derived from RFC 4514, section 3. */
#define IS_ESCAPED(c) ((c == '"') || (c == '+') || (c == ',') || \
                       (c == ';') || (c == '<') || (c == '>'))
#define IS_SPECIAL(c) (IS_ESCAPED(c) || IS_SPACE(c) || \
                       IS_SHARP(c) || (c == '='))
#define IS_LUTF1(c) (IS_UTF1(c) && !IS_ESCAPED(c) && !IS_SPACE(c) && \
                     !IS_SHARP(c) && !IS_ESC(c))
#define IS_TUTF1(c) (IS_UTF1(c) && !IS_ESCAPED(c) && !IS_SPACE(c) && \
                     !IS_ESC(c))
#define IS_SUTF1(c) (IS_UTF1(c) && !IS_ESCAPED(c) && !IS_ESC(c))

/* Per RFC 4517:
 *
 *   PrintableCharacter = ALPHA / DIGIT / SQUOTE / LPAREN / RPAREN /
 *                        PLUS / COMMA / HYPHEN / DOT / EQUALS /
 *                        SLASH / COLON / QUESTION / SPACE
 */
#define IS_PRINTABLE(c) (isalnum(c) || (c == '\'') || IS_LPAREN(c) ||                            \
                         IS_RPAREN(c) || (c == '+') || (c == ',') || (c == '-') || (c == '.') || \
                         (c == '=') || (c == '/') || (c == ':') || (c == '?') || IS_SPACE(c))

int string_filter_sub(Slapi_PBlock *pb, char *initial, char **any, char * final, Slapi_Value **bvals, int syntax);
int string_filter_ava(struct berval *bvfilter, Slapi_Value **bvals, int syntax, int ftype, Slapi_Value **retVal);
int string_values2keys(Slapi_PBlock *pb, Slapi_Value **bvals, Slapi_Value ***ivals, int syntax, int ftype);
int string_assertion2keys_ava(Slapi_PBlock *pb, Slapi_Value *val, Slapi_Value ***ivals, int syntax, int ftype);
int string_assertion2keys_sub(Slapi_PBlock *pb, char *initial, char **any, char * final, Slapi_Value ***ivals, int syntax);
int value_cmp(struct berval *v1, struct berval *v2, int syntax, int normalize);
void value_normalize(char *s, int syntax, int trim_leading_blanks);
void value_normalize_ext(char *s, int syntax, int trim_leading_blanks, char **alt);

char *first_word(char *s);
char *next_word(char *s);
char *phonetic(char *s);

/* Validation helper functions */
int keystring_validate(const char *begin, const char *end);
int numericoid_validate(const char *begin, const char *end);
int utf8char_validate(const char *begin, const char *end, const char **last);
int utf8string_validate(const char *begin, const char *end, const char **last);
int distinguishedname_validate(const char *begin, const char *end);
int rdn_validate(const char *begin, const char *end, const char **last);
int bitstring_validate_internal(const char *begin, const char *end);

struct mr_plugin_def
{
    Slapi_MatchingRuleEntry mr_def_entry; /* for slapi_matchingrule_register */
    Slapi_PluginDesc mr_plg_desc;         /* for SLAPI_PLUGIN_DESCRIPTION */
    const char **mr_names;                /* list of oid and names, NULL terminated SLAPI_PLUGIN_MR_NAMES */
    /* these are optional for new style mr plugins */
    int32_t (*mr_filter_create)(Slapi_PBlock *);  /* old style factory function SLAPI_PLUGIN_MR_FILTER_CREATE_FN */
    int32_t (*mr_indexer_create)(Slapi_PBlock *); /* old style factory function SLAPI_PLUGIN_MR_INDEXER_CREATE_FN */
    /* new style syntax plugin functions */
    /* not all functions will apply to all matching rule types */
    /* e.g. a SUBSTR rule will not have a filter_ava func */
    int32_t (*mr_filter_ava)(Slapi_PBlock *, struct berval *, Slapi_Value **, int32_t, Slapi_Value **); /* SLAPI_PLUGIN_MR_FILTER_AVA */
    int32_t (*mr_filter_sub)(Slapi_PBlock *, char *, char **, char *, Slapi_Value **); /* SLAPI_PLUGIN_MR_FILTER_SUB */
    int32_t (*mr_values2keys)(Slapi_PBlock *, Slapi_Value **, Slapi_Value ***, int32_t);  /* SLAPI_PLUGIN_MR_VALUES2KEYS */
    int32_t (*mr_assertion2keys_ava)(Slapi_PBlock *, Slapi_Value *, Slapi_Value ***, int32_t);
    int32_t (*mr_assertion2keys_sub)(Slapi_PBlock *, char *, char **, char *, Slapi_Value ***); /* SLAPI_PLUGIN_MR_ASSERTION2KEYS_SUB */
    int32_t (*mr_compare)(struct berval *, struct berval *); /* SLAPI_PLUGIN_MR_COMPARE - only for ORDERING */
    void (*mr_normalize)(Slapi_PBlock *, char *, int32_t, char **);
};

int syntax_register_matching_rule_plugins(struct mr_plugin_def mr_plugin_table[], size_t mr_plugin_table_size,
                                          int32_t (*matching_rule_plugin_init)(Slapi_PBlock *));
int syntax_matching_rule_plugin_init(Slapi_PBlock *pb, struct mr_plugin_def mr_plugin_table[], size_t mr_plugin_table_size);

#endif

#ifdef UNSUPPORTED_MATCHING_RULES
/* list of names/oids/aliases for each matching rule */
static const char *keywordMatch_names[] = {"keywordMatch", "2.5.13.33", NULL};
static const char *wordMatch_names[] = {"wordMatch", "2.5.13.32", NULL};
/* table of matching rule plugin defs for mr register and plugin register */
static struct mr_plugin_def mr_plugin_table[] = {
    {{"2.5.13.33",
      NULL,
      "keywordMatch",
      "The keywordMatch rule compares an assertion value of the Directory"
      "String syntax to an attribute value of a syntax (e.g., the Directory"
      "String syntax) whose corresponding ASN.1 type is DirectoryString."
      "The rule evaluates to TRUE if and only if the assertion value"
      "character string matches any keyword in the attribute value.  The"
      "identification of keywords in the attribute value and the exactness"
      "of the match are both implementation specific.",
      "1.3.6.1.4.1.1466.115.121.1.15",
      0}, /* matching rule desc */
     {
         "keywordMatch-mr",
         VENDOR,
         DS_PACKAGE_VERSION,
         "keywordMatch matching rule plugin"}, /* plugin desc */
     keywordMatch_names,                       /* matching rule name/oid/aliases */
     NULL,
     NULL,
     mr_filter_ava,
     mr_filter_sub,
     mr_values2keys,
     mr_assertion2keys_ava,
     mr_assertion2keys_sub,
     mr_compare,
     keywordMatch_syntaxes},
    {{"2.5.13.32",
      NULL,
      "wordMatch",
      "The wordMatch rule compares an assertion value of the Directory"
      "String syntax to an attribute value of a syntax (e.g., the Directory"
      "String syntax) whose corresponding ASN.1 type is DirectoryString."
      "The rule evaluates to TRUE if and only if the assertion value word"
      "matches, according to the semantics of caseIgnoreMatch, any word in"
      "the attribute value.  The precise definition of a word is"
      "implementation specific.",
      "1.3.6.1.4.1.1466.115.121.1.15",
      0}, /* matching rule desc */
     {
         "wordMatch-mr",
         VENDOR,
         DS_PACKAGE_VERSION,
         "wordMatch matching rule plugin"}, /* plugin desc */
     wordMatch_names,                       /* matching rule name/oid/aliases */
     NULL,
     NULL,
     mr_filter_ava,
     mr_filter_sub,
     mr_values2keys,
     mr_assertion2keys_ava,
     mr_assertion2keys_sub,
     mr_compare,
     wordMatch_syntaxes},
};
#endif /* UNSUPPORTED_MATCHING_RULES */

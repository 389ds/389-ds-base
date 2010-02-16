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

/* ces.c - caseexactstring syntax routines.  Implements support for:
 * 	- IA5String
 * 	- URI (DEPRECATED - This is non-standard and isn't used in the default schema.) */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

/* this is used in proposed schema, but there is no official
   OID yet - so for now, use our private MR OID namespace */
#define CASEEXACTIA5SUBSTRINGSMATCH_OID "2.16.840.1.113730.3.3.1"

static int ces_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
		Slapi_Value **bvals, int ftype, Slapi_Value **retVal );
static int ces_filter_sub( Slapi_PBlock *pb, char *initial, char **any,
		char *final, Slapi_Value **bvals );
static int ces_values2keys( Slapi_PBlock *pb, Slapi_Value **val,
		Slapi_Value ***ivals, int ftype );
static int ces_assertion2keys_ava( Slapi_PBlock *pb, Slapi_Value *val,
		Slapi_Value ***ivals, int ftype );
static int ces_assertion2keys_sub( Slapi_PBlock *pb, char *initial, char **any,
		char *final, Slapi_Value ***ivals );
static int ces_compare(struct berval	*v1, struct berval	*v2);
static int ia5_validate(struct berval *val);

/* the first name is the official one from RFC 2252 */
static char *ia5_names[] = { "IA5String", "ces", "caseexactstring",
			     IA5STRING_SYNTAX_OID, 0 };

/* the first name is the official one from RFC 2252 */
static char *uri_names[] = { "URI", "1.3.6.1.4.1.4401.1.1.1",0};

static Slapi_PluginDesc ia5_pdesc = { "ces-syntax", VENDOR,
	DS_PACKAGE_VERSION, "caseExactString attribute syntax plugin" };

static Slapi_PluginDesc uri_pdesc = { "uri-syntax", VENDOR,
	DS_PACKAGE_VERSION, "uri attribute syntax plugin" };

static const char *caseExactIA5Match_names[] = {"caseExactIA5Match", "1.3.6.1.4.1.1466.109.114.1", NULL};
static const char *caseExactMatch_names[] = {"caseExactMatch", "2.5.13.5", NULL};
static const char *caseExactOrderingMatch_names[] = {"caseExactOrderingMatch", "2.5.13.6", NULL};
static const char *caseExactSubstringsMatch_names[] = {"caseExactSubstringsMatch", "2.5.13.7", NULL};
static const char *caseExactIA5SubstringsMatch_names[] = {"caseExactIA5SubstringsMatch", CASEEXACTIA5SUBSTRINGSMATCH_OID, NULL};

static char *dirString_syntaxes[] = {COUNTRYSTRING_SYNTAX_OID,
                                           DIRSTRING_SYNTAX_OID,
                                           PRINTABLESTRING_SYNTAX_OID,NULL};
static char *dirStringCompat_syntaxes[] = {COUNTRYSTRING_SYNTAX_OID,
                                                 PRINTABLESTRING_SYNTAX_OID,NULL};
static char *ia5String_syntaxes[] = {IA5STRING_SYNTAX_OID,NULL};

/* for some reason vendorName and vendorVersion are dirstring but want
   to use EQUALITY caseExactIA5Match ???? RFC 3045
   also the old definition of automountInformation from 60autofs.ldif
   does the same thing */
static char *caseExactIA5Match_syntaxes[] = {DIRSTRING_SYNTAX_OID, NULL};

static struct mr_plugin_def mr_plugin_table[] = {
{{"1.3.6.1.4.1.1466.109.114.1", NULL, "caseExactIA5Match", "The caseExactIA5Match rule compares an assertion value of the IA5 "
"String syntax to an attribute value of a syntax (e.g., the IA5 String "
"syntax) whose corresponding ASN.1 type is IA5String. "
"The rule evaluates to TRUE if and only if the prepared attribute "
"value character string and the prepared assertion value character "
"string have the same number of characters and corresponding "
"characters have the same code point. "
"In preparing the attribute value and assertion value for comparison, "
"characters are not case folded in the Map preparation step, and only "
"Insignificant Space Handling is applied in the Insignificant "
"Character Handling step.",
IA5STRING_SYNTAX_OID, 0, caseExactIA5Match_syntaxes}, /* matching rule desc */
 {"caseExactIA5Match-mr", VENDOR, DS_PACKAGE_VERSION, "caseExactIA5Match matching rule plugin"}, /* plugin desc */
   caseExactIA5Match_names, /* matching rule name/oid/aliases */
   NULL, NULL, ces_filter_ava, NULL, ces_values2keys,
   ces_assertion2keys_ava, NULL, ces_compare},
{{"2.5.13.5", NULL, "caseExactMatch", "The caseExactMatch rule compares an assertion value of the Directory "
"String syntax to an attribute value of a syntax (e.g., the Directory "
"String, Printable String, Country String, or Telephone Number syntax) "
"whose corresponding ASN.1 type is DirectoryString or one of the "
"alternative string types of DirectoryString, such as PrintableString "
"(the other alternatives do not correspond to any syntax defined in "
"this document). "
"The rule evaluates to TRUE if and only if the prepared attribute "
"value character string and the prepared assertion value character "
"string have the same number of characters and corresponding "
"characters have the same code point. "
"In preparing the attribute value and assertion value for comparison, "
"characters are not case folded in the Map preparation step, and only "
"Insignificant Space Handling is applied in the Insignificant "
"Character Handling step.",
DIRSTRING_SYNTAX_OID, 0, dirStringCompat_syntaxes}, /* matching rule desc */
 {"caseExactMatch-mr", VENDOR, DS_PACKAGE_VERSION, "caseExactMatch matching rule plugin"}, /* plugin desc */
   caseExactMatch_names, /* matching rule name/oid/aliases */
   NULL, NULL, ces_filter_ava, NULL, ces_values2keys,
   ces_assertion2keys_ava, NULL, ces_compare},
{{"2.5.13.6", NULL, "caseExactOrderingMatch", "The caseExactOrderingMatch rule compares an assertion value of the "
"Directory String syntax to an attribute value of a syntax (e.g., the "
"Directory String, Printable String, Country String, or Telephone "
"Number syntax) whose corresponding ASN.1 type is DirectoryString or "
"one of its alternative string types. "
"The rule evaluates to TRUE if and only if, in the code point "
"collation order, the prepared attribute value character string "
"appears earlier than the prepared assertion value character string; "
"i.e., the attribute value is \"less than\" the assertion value. "
"In preparing the attribute value and assertion value for comparison, "
"characters are not case folded in the Map preparation step, and only "
"Insignificant Space Handling is applied in the Insignificant "
"Character Handling step.",
DIRSTRING_SYNTAX_OID, 0, dirStringCompat_syntaxes}, /* matching rule desc */
 {"caseExactOrderingMatch-mr", VENDOR, DS_PACKAGE_VERSION, "caseExactOrderingMatch matching rule plugin"}, /* plugin desc */
   caseExactOrderingMatch_names, /* matching rule name/oid/aliases */
   NULL, NULL, ces_filter_ava, NULL, ces_values2keys,
   ces_assertion2keys_ava, NULL, ces_compare},
{{"2.5.13.7", NULL, "caseExactSubstringsMatch", "The caseExactSubstringsMatch rule compares an assertion value of the "
"Substring Assertion syntax to an attribute value of a syntax (e.g., "
"the Directory String, Printable String, Country String, or Telephone "
"Number syntax) whose corresponding ASN.1 type is DirectoryString or "
"one of its alternative string types. "
"The rule evaluates to TRUE if and only if (1) the prepared substrings "
"of the assertion value match disjoint portions of the prepared "
"attribute value character string in the order of the substrings in "
"the assertion value, (2) an <initial> substring, if present, matches "
"the beginning of the prepared attribute value character string, and "
"(3) a <final> substring, if present, matches the end of the prepared "
"attribute value character string.  A prepared substring matches a "
"portion of the prepared attribute value character string if "
"corresponding characters have the same code point. "
"In preparing the attribute value and assertion value substrings for "
"comparison, characters are not case folded in the Map preparation "
"step, and only Insignificant Space Handling is applied in the "
"Insignificant Character Handling step.",
"1.3.6.1.4.1.1466.115.121.1.58", 0, dirString_syntaxes}, /* matching rule desc */
 {"caseExactSubstringsMatch-mr", VENDOR, DS_PACKAGE_VERSION, "caseExactSubstringsMatch matching rule plugin"}, /* plugin desc */
 caseExactSubstringsMatch_names, /* matching rule name/oid/aliases */
 NULL, NULL, NULL, ces_filter_sub, ces_values2keys,
 NULL, ces_assertion2keys_sub, ces_compare},
{{CASEEXACTIA5SUBSTRINGSMATCH_OID, NULL, "caseExactIA5SubstringsMatch", "The caseExactIA5SubstringsMatch rule compares an assertion value of the "
"Substring Assertion syntax to an attribute value of a syntax (e.g., "
"the IA5 syntax) whose corresponding ASN.1 type is IA5 String or "
"one of its alternative string types. "
"The rule evaluates to TRUE if and only if (1) the prepared substrings "
"of the assertion value match disjoint portions of the prepared "
"attribute value character string in the order of the substrings in "
"the assertion value, (2) an <initial> substring, if present, matches "
"the beginning of the prepared attribute value character string, and "
"(3) a <final> substring, if present, matches the end of the prepared "
"attribute value character string.  A prepared substring matches a "
"portion of the prepared attribute value character string if "
"corresponding characters have the same code point. "
"In preparing the attribute value and assertion value substrings for "
"comparison, characters are not case folded in the Map preparation "
"step, and only Insignificant Space Handling is applied in the "
"Insignificant Character Handling step.",
"1.3.6.1.4.1.1466.115.121.1.58", 0, ia5String_syntaxes}, /* matching rule desc */
 {"caseExactIA5SubstringsMatch-mr", VENDOR, DS_PACKAGE_VERSION, "caseExactIA5SubstringsMatch matching rule plugin"}, /* plugin desc */
   caseExactIA5SubstringsMatch_names, /* matching rule name/oid/aliases */
   NULL, NULL, NULL, ces_filter_sub, ces_values2keys,
   NULL, ces_assertion2keys_sub, ces_compare}
};

static size_t mr_plugin_table_size = sizeof(mr_plugin_table)/sizeof(mr_plugin_table[0]);

static int
matching_rule_plugin_init(Slapi_PBlock *pb)
{
	return syntax_matching_rule_plugin_init(pb, mr_plugin_table, mr_plugin_table_size);
}

static int
register_matching_rule_plugins()
{
	return syntax_register_matching_rule_plugins(mr_plugin_table, mr_plugin_table_size, matching_rule_plugin_init);
}

/*
 * register_ces_like_plugin():  register all items for a cis-like plugin.
 */
static int
register_ces_like_plugin( Slapi_PBlock *pb, Slapi_PluginDesc *pdescp,
		char **names, char *oid, void *validate_fn )
{
	int	rc, flags;

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *) pdescp );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
	    (void *) ces_filter_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_SUB,
	    (void *) ces_filter_sub );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
	    (void *) ces_values2keys );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
	    (void *) ces_assertion2keys_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB,
	    (void *) ces_assertion2keys_sub );
	flags = SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING;
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FLAGS,
	    (void *) &flags );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_NAMES,
	    (void *) names );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_OID,
	    (void *) oid );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_COMPARE,
	    (void *) ces_compare );
	if (validate_fn != NULL) {
		rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALIDATE,
		    (void *)validate_fn );
	}

	return( rc );
}

int
ces_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> ces_init\n", 0, 0, 0 );

	rc = register_ces_like_plugin(pb,&ia5_pdesc,ia5_names,IA5STRING_SYNTAX_OID, ia5_validate);
	rc |= register_matching_rule_plugins();

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= ces_init %d\n", rc, 0, 0 );
	return( rc );
}

int
uri_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> uri_init\n", 0, 0, 0 );

	rc = register_ces_like_plugin(pb,&uri_pdesc,uri_names,
				      "1.3.6.1.4.1.4401.1.1.1", NULL);

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= uri_init %d\n", rc, 0, 0 );
	return( rc );
}

static int
ces_filter_ava(
    Slapi_PBlock		*pb,
    struct berval	*bvfilter,
    Slapi_Value	**bvals,
    int			ftype,
	Slapi_Value **retVal
)
{
	return( string_filter_ava( bvfilter, bvals, SYNTAX_CES, ftype,
									  retVal) );
}

static int
ces_filter_sub(
    Slapi_PBlock		*pb,
    char		*initial,
    char		**any,
    char		*final,
    Slapi_Value	**bvals
)
{
	return( string_filter_sub( pb, initial, any, final, bvals, SYNTAX_CES ) );
}

static int
ces_values2keys(
    Slapi_PBlock		*pb,
    Slapi_Value	**vals,
    Slapi_Value	***ivals,
    int			ftype
)
{
	return( string_values2keys( pb, vals, ivals, SYNTAX_CES, ftype ) );
}

static int
ces_assertion2keys_ava(
    Slapi_PBlock		*pb,
    Slapi_Value	*val,
    Slapi_Value	***ivals,
    int			ftype
)
{
	return(string_assertion2keys_ava( pb, val, ivals, SYNTAX_CES, ftype ));
}

static int
ces_assertion2keys_sub(
    Slapi_PBlock		*pb,
    char		*initial,
    char		**any,
    char		*final,
    Slapi_Value	***ivals
)
{
	return( string_assertion2keys_sub( pb, initial, any, final, ivals,
	    SYNTAX_CES ) );
}

static int ces_compare(    
	struct berval	*v1,
    struct berval	*v2
)
{
	return value_cmp(v1,v2,SYNTAX_CES,3 /* Normalise both values */);
}

static int
ia5_validate(
    struct berval *val
)
{
	int	rc = 0;    /* assume the value is valid */
	int	i = 0;

	if (val == NULL) {
		rc = 1;
		goto exit;
	}

	/* Per RFC 4517:
	 *
	 * IA5String = *(%x00-7F)
	 */
	for (i=0; i < val->bv_len; i++) {
		if (!IS_UTF1(val->bv_val[i])) {
			rc = 1;
			goto exit;
		}
	}

exit:
	return rc;
}

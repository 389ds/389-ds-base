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

/* cis.c - caseignorestring syntax routines */

/*
 * This file actually implements numerous syntax plugins:
 *
 *		Boolean
 *		CountryString
 *		DirectoryString
 *		GeneralizedTime
 *		OID
 *		PostalAddress
 *		PrintableString
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

static int cis_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
		Slapi_Value **bvals, int ftype, Slapi_Value **retVal );
static int cis_filter_sub( Slapi_PBlock *pb, char *initial, char **any,
		char *final, Slapi_Value **bvals );
static int cis_values2keys( Slapi_PBlock *pb, Slapi_Value **val,
		Slapi_Value ***ivals, int ftype );
static int cis_assertion2keys_ava( Slapi_PBlock *pb, Slapi_Value *val,
		Slapi_Value ***ivals, int ftype );
static int cis_assertion2keys_sub( Slapi_PBlock *pb, char *initial, char **any,
		char *final, Slapi_Value ***ivals );
static int cis_compare(struct berval	*v1, struct berval	*v2);
static int dirstring_validate(struct berval *val);
static int boolean_validate(struct berval *val);
static int time_validate(struct berval *val);
static int country_validate(struct berval *val);
static int postal_validate(struct berval *val);
static int oid_validate(struct berval *val);
static int printable_validate(struct berval *val);

/*
  Even though the official RFC 4517 says that the postal syntax
  line values must contain at least 1 character (i.e. no $$), it
  seems that most, if not all, address book and other applications that
  use postal address syntax values expect to be able to store empty
  lines/values - so for now, allow it
*/
static const int postal_allow_empty_lines = 1;

/*
 * Attribute syntaxes. We treat all of these the same for now, even though
 * the specifications (e.g., RFC 2252) impose various constraints on the
 * the format for each of these.
 *
 * Note: the first name is the official one from RFC 2252.
 */
static char *dirstring_names[] = { "DirectoryString", "cis",
		"caseignorestring", DIRSTRING_SYNTAX_OID, 0 };

static char *boolean_names[] = { "Boolean", BOOLEAN_SYNTAX_OID, 0 };

static char *time_names[] = { "GeneralizedTime", "time",
		GENERALIZEDTIME_SYNTAX_OID, 0 };

#define GENERALIZEDTIMEMATCH_OID "2.5.13.27"
#define GENERALIZEDTIMEORDERINGMATCH_OID "2.5.13.28"
static Slapi_MatchingRuleEntry
generalizedTimeMatch = { GENERALIZEDTIMEMATCH_OID, NULL /* no alias? */,
                         "generalizedTimeMatch", "The rule evaluates to TRUE if and only if the attribute value represents the same universal coordinated time as the assertion value.",
                         GENERALIZEDTIME_SYNTAX_OID, 0 /* not obsolete */ };

static Slapi_MatchingRuleEntry
generalizedTimeOrderingMatch = { GENERALIZEDTIMEORDERINGMATCH_OID, NULL /* no alias? */,
                                 "generalizedTimeOrderingMatch", "The rule evaluates to TRUE if and only if the attribute value represents a universal coordinated time that is earlier than the universal coordinated time represented by the assertion value.",
                                 GENERALIZEDTIME_SYNTAX_OID, 0 /* not obsolete */ };


static char *country_names[] = { "Country String",
		COUNTRYSTRING_SYNTAX_OID, 0};

static char *postal_names[] = { "Postal Address",
		POSTALADDRESS_SYNTAX_OID, 0};

static char *oid_names[] = { "OID",
		OID_SYNTAX_OID, 0};

static char *printable_names[] = { "Printable String",
		PRINTABLESTRING_SYNTAX_OID, 0};


/*
  TBD (XXX)

                   "1.3.6.1.4.1.1466.115.121.1.16 \"DIT Content Rule Description
\" "
                   "1.3.6.1.4.1.1466.115.121.1.17 \"DIT Structure Rule Descripti
on\" "
                   "1.3.6.1.4.1.1466.115.121.1.20 \"DSE Type\" "
                   "1.3.6.1.4.1.1466.115.121.1.30 \"Matching Rule Description\" 
"
                   "1.3.6.1.4.1.1466.115.121.1.31 \"Matching Rule Use Descriptio
n\" "
                   "1.3.6.1.4.1.1466.115.121.1.35 \"Name Form Description\" "

                   "1.3.6.1.4.1.1466.115.121.1.44 \"Printable String\" "
                   "1.3.6.1.4.1.1466.115.121.1.45 \"Subtree Specification\" "
                   "1.3.6.1.4.1.1466.115.121.1.54 \"LDAP Syntax Description\" "
                   "1.3.6.1.4.1.1466.115.121.1.55 \"Modify Rights\" "
                   "1.3.6.1.4.1.1466.115.121.1.56 \"LDAP Schema Description\" "
                   "1.3.6.1.4.1.1466.115.121.1.25 \"Guide\" "
                   "1.3.6.1.4.1.1466.115.121.1.52 \"Telex Number\" "
                   "1.3.6.1.4.1.1466.115.121.1.51 \"Teletex Terminal Identifier\
" "
                   "1.3.6.1.4.1.1466.115.121.1.14 \"Delivery Method\" "
                   "1.3.6.1.4.1.1466.115.121.1.43 \"Presentation Address\" "
                   "1.3.6.1.4.1.1466.115.121.1.21 \"Enhanced Guide\" "
                   "1.3.6.1.4.1.1466.115.121.1.34 \"Name and Optional UID\" "
                   "1.2.840.113556.1.4.905 \"CaseIgnoreString\" "
                   "1.3.6.1.1.1.0.0 \"nisNetgroupTripleSyntax\" "
                   "1.3.6.1.1.1.0.1 \"bootParameterSyntax\" ");
 */


static Slapi_PluginDesc dirstring_pdesc = { "directorystring-syntax",
		VENDOR, DS_PACKAGE_VERSION,
		"DirectoryString attribute syntax plugin" };

static Slapi_PluginDesc boolean_pdesc = { "boolean-syntax",
		VENDOR, DS_PACKAGE_VERSION,
		"Boolean attribute syntax plugin" };

static Slapi_PluginDesc time_pdesc = { "time-syntax",
		VENDOR, DS_PACKAGE_VERSION,
		"GeneralizedTime attribute syntax plugin" };

static Slapi_PluginDesc country_pdesc = { "countrystring-syntax",
		VENDOR, DS_PACKAGE_VERSION,
		"Country String attribute syntax plugin" };

static Slapi_PluginDesc postal_pdesc = { "postaladdress-syntax",
		VENDOR, DS_PACKAGE_VERSION,
		"Postal Address attribute syntax plugin" };

static Slapi_PluginDesc oid_pdesc = { "oid-syntax",
		VENDOR, DS_PACKAGE_VERSION,
		"OID attribute syntax plugin" };

static Slapi_PluginDesc printable_pdesc = { "printablestring-syntax",
		VENDOR, DS_PACKAGE_VERSION,
		"Printable String attribtue syntax plugin" };


/*
 * register_cis_like_plugin():  register all items for a cis-like plugin.
 */
static int
register_cis_like_plugin( Slapi_PBlock *pb, Slapi_PluginDesc *pdescp,
		char **names, char *oid, void *validate_fn )
{
	int	rc, flags;

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *) pdescp );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
	    (void *) cis_filter_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_SUB,
	    (void *) cis_filter_sub );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
	    (void *) cis_values2keys );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
	    (void *) cis_assertion2keys_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB,
	    (void *) cis_assertion2keys_sub );
	flags = SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING;
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FLAGS,
	    (void *) &flags );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_NAMES,
	    (void *) names );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_OID,
	    (void *) oid );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_COMPARE,
	    (void *) cis_compare );
	if (validate_fn != NULL) {
		rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALIDATE,
		    (void *)validate_fn );
	}

	return( rc );
}

int
cis_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> cis_init\n", 0, 0, 0 );
	rc = register_cis_like_plugin( pb, &dirstring_pdesc, dirstring_names,
		 	DIRSTRING_SYNTAX_OID, dirstring_validate );
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= cis_init %d\n", rc, 0, 0 );
	return( rc );
}


int
boolean_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> boolean_init\n", 0, 0, 0 );
	rc = register_cis_like_plugin( pb, &boolean_pdesc, boolean_names,
			BOOLEAN_SYNTAX_OID, boolean_validate );
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= boolean_init %d\n", rc, 0, 0 );
	return( rc );
}

int
time_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> time_init\n", 0, 0, 0 );
	rc = register_cis_like_plugin( pb, &time_pdesc, time_names,
			GENERALIZEDTIME_SYNTAX_OID, time_validate );
	/* also register this plugin for matching rules */
	rc |= slapi_matchingrule_register(&generalizedTimeMatch);
	rc |= slapi_matchingrule_register(&generalizedTimeOrderingMatch);
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= time_init %d\n", rc, 0, 0 );
	return( rc );
}

int
country_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> country_init\n", 0, 0, 0 );
	rc = register_cis_like_plugin( pb, &country_pdesc, country_names,
				       COUNTRYSTRING_SYNTAX_OID, country_validate );
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= country_init %d\n", rc, 0, 0 );
	return( rc );
}

int
postal_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> postal_init\n", 0, 0, 0 );
	rc = register_cis_like_plugin( pb, &postal_pdesc, postal_names,
				       POSTALADDRESS_SYNTAX_OID, postal_validate );
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= postal_init %d\n", rc, 0, 0 );
	return( rc );
}


int
oid_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> oid_init\n", 0, 0, 0 );
	rc = register_cis_like_plugin( pb, &oid_pdesc, oid_names, OID_SYNTAX_OID, oid_validate );
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= oid_init %d\n", rc, 0, 0 );
	return( rc );
}

int
printable_init( Slapi_PBlock *pb )
{
	int     rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> printable_init\n", 0, 0, 0 );
	rc = register_cis_like_plugin( pb, &printable_pdesc, printable_names,
					PRINTABLESTRING_SYNTAX_OID, printable_validate );
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= printable_init %d\n", rc, 0, 0 );
	return( rc );
}

static int
cis_filter_ava(
    Slapi_PBlock		*pb,
    struct berval	*bvfilter,
    Slapi_Value	**bvals,
    int			ftype,
	Slapi_Value **retVal
)
{
	return( string_filter_ava( bvfilter, bvals, SYNTAX_CIS, ftype,
									  retVal ) );
}


static int
cis_filter_sub(
    Slapi_PBlock		*pb,
    char		*initial,
    char		**any,
    char		*final,
    Slapi_Value	**bvals
)
{
	return( string_filter_sub( pb, initial, any, final, bvals, SYNTAX_CIS ) );
}

static int
cis_values2keys(
    Slapi_PBlock		*pb,
    Slapi_Value	**vals,
    Slapi_Value	***ivals,
    int			ftype
)
{
	return( string_values2keys( pb, vals, ivals, SYNTAX_CIS, ftype ) );
}

static int
cis_assertion2keys_ava(
    Slapi_PBlock		*pb,
    Slapi_Value	*val,
    Slapi_Value	***ivals,
    int			ftype
)
{
	return(string_assertion2keys_ava( pb, val, ivals, SYNTAX_CIS, ftype ));
}

static int
cis_assertion2keys_sub(
    Slapi_PBlock		*pb,
    char		*initial,
    char		**any,
    char		*final,
    Slapi_Value	***ivals
)
{
	return( string_assertion2keys_sub( pb, initial, any, final, ivals,
	    SYNTAX_CIS ) );
}

static int cis_compare(    
	struct berval	*v1,
    struct berval	*v2
)
{
	return value_cmp(v1,v2,SYNTAX_CIS,3 /* Normalise both values */);
}

static int dirstring_validate(
	struct berval *val
)
{
	int     rc = 0;    /* assume the value is valid */
	char *p = NULL;
	char *end = NULL;

	/* Per RFC4517:
	 *
	 * DirectoryString = 1*UTF8
	 */
	if ((val != NULL) && (val->bv_len > 0)) {
		p = val->bv_val;
		end = &(val->bv_val[val->bv_len - 1]);
		rc = utf8string_validate(p, end, NULL);
	} else {
		rc = 1;
		goto exit;
	}

exit:
	return( rc );
}

static int boolean_validate(
	struct berval *val
)
{
	int     rc = 0;    /* assume the value is valid */

	/* Per RFC4517:
	 *
	 * Boolean =  "TRUE" / "FALSE"
	 */
	if (val != NULL) {
		if (val->bv_len == 4) {
			if (strncmp(val->bv_val, "TRUE", 4) != 0) {
				rc = 1;
				goto exit;
			}
		} else if (val->bv_len == 5) {
			if (strncmp(val->bv_val, "FALSE", 5) != 0) {
				rc = 1;
				goto exit;
			}
		} else {
			rc = 1;
			goto exit;
		}
	} else {
		rc = 1;
	}

exit:
	return(rc);
}

static int time_validate(
	struct berval *val
)
{
	int     rc = 0;    /* assume the value is valid */
	int	i = 0;
        const char *p = NULL;
	char *end = NULL;

	/* Per RFC4517:
	 *
	 * GeneralizedTime = century year month day hour
	 *                      [ minute [ second / leap-second ] ]
	 *                      [ fraction ]
	 *                      g-time-zone
	 *
	 * century = 2(%x30-39) ; "00" to "99"
	 * year    = 2(%x30-39) ; "00" to "99"
	 * month   =   ( %x30 %x31-39 ) ; "01" (January) to "09"
	 *           / ( %x31 %x30-32 ) ; "10 to "12"
	 * day     =   ( %x30 %x31-39 )     ; "01" to "09"
	 *           / ( %x31-x32 %x30-39 ) ; "10" to "29"
	 *           / ( %x33 %x30-31 )     ; "30" to "31"
	 * hour    = ( %x30-31 %x30-39 ) / ( %x32 %x30-33 ) ; "00" to "23"
	 * minute  = %x30-35 %x30-39                        ; "00" to "59"
	 *
	 * second      = ( %x30-35 - %x30-39 ) ; "00" to "59"
	 * leap-second = ( %x36 %x30 )         ; "60"
	 *
	 * fraction        = ( DOT / COMMA ) 1*(%x30-39)
	 * g-time-zone     = %x5A  ; "Z"
	 *                   / g-differential
	 * g-differential  = ( MINUS / PLUS ) hour [ minute ]
	 */
	if (val != NULL) {
		/* A valid GeneralizedTime should be at least 11 characters.  There
		 * is no upper bound due to the variable length of "fraction". */
		if (val->bv_len < 11) {
			rc = 1;
			goto exit;
		}

		/* We're guaranteed that the value is at least 11 characters, so we
		 * don't need to bother checking if we're at the end of the value
		 * until we start processing the "minute" part of the value. */
		p = val->bv_val;
		end = &(val->bv_val[val->bv_len - 1]);

		/* Process "century year". First 4 characters can be any valid digit. */
		for (i=0; i<4; i++) {
			if (!isdigit(*p)) {
				rc = 1;
				goto exit;
			}
			p++;
		}

		/* Process "month". Next character can be "0" or "1". */
		if (*p == '0') {
			p++;
			/* any LDIGIT is valid now */
			if (!IS_LDIGIT(*p)) {
				rc = 1;
				goto exit;
			}
			p++;
		} else if (*p == '1') {
			p++;
			/* only "0"-"2" are valid now */
			if ((*p < '0') || (*p > '2')) {
				rc = 1;
				goto exit;
			}
			p++;
		} else {
			rc = 1;
			goto exit;
		}

		/* Process "day".  Next character can be "0"-"3". */
		if (*p == '0') {
			p++;
			/* any LDIGIT is valid now */
			if (!IS_LDIGIT(*p)) {
				rc = 1;
				goto exit;
			}
			p++;
		} else if ((*p == '1') || (*p == '2')) {
			p++;
			/* any digit is valid now */
			if (!isdigit(*p)) {
				rc = 1;
				goto exit;
			}
			p++;
		} else if (*p == '3') {
			p++;
			/* only "0"-"1" are valid now */
			if ((*p != '0') && (*p != '1')) {
				rc = 1;
				goto exit;
			}
			p++;
		} else {
			rc = 1;
			goto exit;
		}

		/* Process "hour".  Next character can be "0"-"2". */
		if ((*p == '0') || (*p == '1')) {
			p++;
			/* any digit is valid now */
			if (!isdigit(*p)) {
				rc = 1;
				goto exit;
			}
			p++;
		} else if (*p == '2') {
			p++;
			/* only "0"-"3" are valid now */
			if ((*p < '0') || (*p > '3')) {
				rc = 1;
				goto exit;
			}
			p++;
		} else {
			rc = 1;
			goto exit;
		}

		/* Time for the optional stuff.  We know we have at least one character here, but
		 * we need to start checking for the end of the string afterwards.
		 *
		 * See if a "minute" was specified. */
		if ((*p >= '0') && (*p <= '5')) {
			p++;
			/* any digit is valid for the second char of a minute */
			if ((p > end) || (!isdigit(*p))) {
				rc = 1;
				goto exit;
			}
			p++;

			/* At this point, there has to at least be a "g-time-zone" left.
			 * Make sure we're not at the end of the string. */
			if (p > end) {
				rc = 1;
				goto exit;
			}

			/* See if a "second" or "leap-second" was specified. */
			if ((*p >= '0') && (*p <= '5')) {
				p++;
				/* any digit is valid now */
				if ((p > end) || (!isdigit(*p))) {
					rc = 1;
					goto exit;
				}
				p++;
			} else if (*p == '6') {
				p++;
				/* only a '0' is valid now */
				if ((p > end) || (*p != '0')) {
					rc = 1;
					goto exit;
				}
				p++;
			}

			/* At this point, there has to at least be a "g-time-zone" left.
			 * Make sure we're not at the end of the string. */
			if (p > end) {
				rc = 1;
				goto exit;
			}
		}

		/* See if a fraction was specified. */
		if ((*p == '.') || (*p == ',')) {
			p++;
			/* An arbitrary length string of digit chars is allowed here.
			 * Ensure we have at least one digit character. */
			if ((p >= end) || (!isdigit(*p))) {
				rc = 1;
				goto exit;
			}

			/* Just loop through the rest of the fraction until we encounter a non-digit */
			p++;
			while ((p < end) && (isdigit(*p))) {
				p++;
			}
		}

		/* Process "g-time-zone".  We either end with 'Z', or have a differential. */
		if (p == end) {
			if (*p != 'Z') {
				rc = 1;
				goto exit;
			}
		} else if (p < end) {
			if ((*p != '-') && (*p != '+')) {
				rc = 1;
				goto exit;
			} else {
				/* A "g-differential" was specified. An "hour" must be present now. */
				p++;
				if ((*p == '0') || (*p == '1')) {
					p++;
					/* any digit is valid now */
					if ((p > end) || !isdigit(*p)) {
						rc = 1;
						goto exit;
					}
					p++;
				} else if (*p == '2') {
					p++;
					/* only "0"-"3" are valid now */
					if ((p > end) || (*p < '0') || (*p > '3')) {
						rc = 1;
						goto exit;
					}
					p++;
				} else {
					rc = 1;
					goto exit;
				}

				/* See if an optional minute is present ("00"-"59"). */
				if (p <= end) {
					/* "0"-"5" are valid now */
					if ((*p < '0') || (*p > '5')) {
						rc = 1;
						goto exit;
					}
					p++;

					/* We should be at the last character of the string
					 * now, which must be a valid digit. */
					if ((p != end) || !isdigit(*p)) {
						rc = 1;
						goto exit;
					}
				}
			}
		} else {
			/* Premature end of string */
			rc = 1;
			goto exit;
		}
	} else {
		rc = 1;
		goto exit;
	}

exit:
	return( rc );
}

static int country_validate(
	struct berval *val
)
{
	int     rc = 0;    /* assume the value is valid */

	/* Per RFC4517:
	 *
	 *   CountryString = 2(PrintableCharacter)
	 */
	if (val != NULL) {
		if ((val->bv_len != 2) || !IS_PRINTABLE(val->bv_val[0]) || !IS_PRINTABLE(val->bv_val[1])) {
			rc = 1;
			goto exit;
		}


	} else {
		rc = 1;
	}

exit:
	return(rc);
}

static int postal_validate( 
	struct berval *val
)
{
	int     rc = 0;    /* assume the value is valid */
	const char *p = NULL;
	const char *start = NULL;
	char *end = NULL;

	/* Per RFC4517:
	 *   PostalAddress = line *( DOLLAR line )
	 *   line          = 1*line-char
	 *   line-char     = %x00-23
	 *                   / (%x5C "24")  ; escaped "$"
	 *                   / %x25-5B
	 *                   / (%x5C "5C")  ; escaped "\"
	 *                   / %x5D-7F
	 *                   / UTFMB
	 */
	if (val != NULL) {
		start = val->bv_val;
		end = &(val->bv_val[val->bv_len - 1]);
		for (p = start; p <= end; p++) {
			/* look for a '\' and make sure it's only used to escape a '$' or a '\' */
			if (*p == '\\') {
				p++;
				/* ensure that we're not at the end of the value */
				if ((p > end) || ((strncmp(p, "24", 2) != 0) && (strncasecmp(p, "5C", 2) != 0))) {
					rc = 1;
					goto exit;
				} else {
					/* advance the pointer to point to the end
					 * of the hex code for the escaped character */
					p++;
				}
			} else if (*p == '$') {
				/* This signifies the end of a line.  We need
				 * to ensure that the line is not empty. */
				/* make sure the value doesn't end with a '$' */
				if ((p == start) || (p == end)) {
					if (!postal_allow_empty_lines) {
						rc = 1;
						goto exit;
					} /* else allow it */
				} else if ((rc = utf8string_validate(start, p, NULL)) != 0) {
					/* Make sure the line (start to p) is valid UTF-8. */
					goto exit;
				}

				/* make the start pointer point to the
				 * beginning of the next line */
				start = p + 1;
			}
		}
	} else {
		rc = 1;
	}

exit:
	return(rc);
}

static int oid_validate(
	struct berval *val
)
{
	int     rc = 0;    /* assume the value is valid */
	const char *p = NULL;
	const char *end = NULL;

	/* Per RFC4512:
	 *
	 *   oid = descr / numericoid
	 *   descr = keystring
	 */
	if ((val != NULL) && (val->bv_len > 0)) {
		p = val->bv_val;
		end = &(val->bv_val[val->bv_len - 1]);

		/* check if the value matches the descr form */
		if (IS_LEADKEYCHAR(*p)) {
			rc = keystring_validate(p, end);
		/* check if the value matches the numericoid form */
		} else if (isdigit(*p)) {
			rc = numericoid_validate(p, end);
		} else {
			rc = 1;
			goto exit;
		}
	} else {
		rc = 1;
	}

exit:
	return( rc );
}

static int printable_validate(
	struct berval *val
)
{
	int rc = 0;    /* assume the value is valid */
        int i = 0;

	/* Per RFC4517:
	 *
	 * PrintableString = 1*PrintableCharacter
	 */
	if ((val != NULL) && (val->bv_len > 0)) {
		/* Make sure all chars are a PrintableCharacter */
		for (i=0; i < val->bv_len; i++) {
			if (!IS_PRINTABLE(val->bv_val[i])) {
				rc = 1;
				goto exit;
			}
		}
	} else {
		rc = 1;
	}

exit:
	return( rc );
}

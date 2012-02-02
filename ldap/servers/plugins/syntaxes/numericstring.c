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
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* numericstring.c - Numeric String syntax routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

static int numstr_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
		Slapi_Value **bvals, int ftype, Slapi_Value **retVal );
static int numstr_filter_sub( Slapi_PBlock *pb, char *initial, char **any,
		char *final, Slapi_Value **bvals );
static int numstr_values2keys( Slapi_PBlock *pb, Slapi_Value **val,
		Slapi_Value ***ivals, int ftype );
static int numstr_assertion2keys( Slapi_PBlock *pb, Slapi_Value *val,
		Slapi_Value ***ivals, int ftype );
static int numstr_assertion2keys_sub( Slapi_PBlock *pb, char *initial, char **any,
		char *final, Slapi_Value ***ivals );
static int numstr_compare(struct berval	*v1, struct berval	*v2);
static int numstr_validate(struct berval *val);
static void numstr_normalize(
	Slapi_PBlock *pb,
	char    *s,
	int     trim_spaces,
	char    **alt
);

/* the first name is the official one from RFC 4517 */
static char *names[] = { "Numeric String", "numstr", NUMERICSTRING_SYNTAX_OID, 0 };

#define NUMERICSTRINGMATCH_OID		"2.5.13.8"
#define NUMERICSTRINGORDERINGMATCH_OID	"2.5.13.9"
#define NUMERICSTRINGSUBSTRINGSMATCH_OID	"2.5.13.10"

static Slapi_PluginDesc pdesc = { "numstr-syntax", VENDOR,
	DS_PACKAGE_VERSION, "numeric string attribute syntax plugin" };

static const char *numericStringMatch_names[] = {"numericStringMatch", NUMERICSTRINGMATCH_OID, NULL};
static const char *numericStringOrderingMatch_names[] = {"numericStringOrderingMatch", NUMERICSTRINGORDERINGMATCH_OID, NULL};
static const char *numericStringSubstringsMatch_names[] = {"numericStringSubstringsMatch", NUMERICSTRINGSUBSTRINGSMATCH_OID, NULL};

static char *numericStringSubstringsMatch_syntaxes[] = {NUMERICSTRING_SYNTAX_OID,NULL};

static struct mr_plugin_def mr_plugin_table[] = {
{{NUMERICSTRINGMATCH_OID, NULL /* no alias? */,
  "numericStringMatch", "The rule evaluates to TRUE if and only if the prepared "
  "attribute value character string and the prepared assertion value character "
  "string have the same number of characters and corresponding characters have "
  "the same code point.",
  NUMERICSTRING_SYNTAX_OID, 0 /* not obsolete */, NULL /* numstr syntax only for now */ },
 {"numericStringMatch-mr", VENDOR, DS_PACKAGE_VERSION, "numericStringMatch matching rule plugin"}, /* plugin desc */
 numericStringMatch_names, /* matching rule name/oid/aliases */
 NULL, NULL, numstr_filter_ava, NULL, numstr_values2keys,
 numstr_assertion2keys, NULL, numstr_compare},
{{NUMERICSTRINGORDERINGMATCH_OID, NULL /* no alias? */,
  "numericStringOrderingMatch", "The rule evaluates to TRUE if and only if, "
  "in the code point collation order, the prepared attribute value character "
  "string appears earlier than the prepared assertion value character string; "
  "i.e., the attribute value is less than the assertion value.",
  NUMERICSTRING_SYNTAX_OID, 0 /* not obsolete */, NULL /* numstr syntax only for now */ },
 {"numericStringOrderingMatch-mr", VENDOR, DS_PACKAGE_VERSION, "numericStringOrderingMatch matching rule plugin"}, /* plugin desc */
 numericStringOrderingMatch_names, /* matching rule name/oid/aliases */
 NULL, NULL, numstr_filter_ava, NULL, numstr_values2keys,
 numstr_assertion2keys, NULL, numstr_compare},
{{NUMERICSTRINGSUBSTRINGSMATCH_OID, NULL /* no alias? */,
  "numericStringSubstringsMatch", "The rule evaluates to TRUE if and only if (1) "
  "the prepared substrings of the assertion value match disjoint portions of "
  "the prepared attribute value, (2) an initial substring, if present, matches "
  "the beginning of the prepared attribute value character string, and (3) a "
  "final substring, if present, matches the end of the prepared attribute value "
  "character string.",
  "1.3.6.1.4.1.1466.115.121.1.58", 0 /* not obsolete */, numericStringSubstringsMatch_syntaxes}, /* matching rule desc */
 {"numericStringSubstringsMatch-mr", VENDOR, DS_PACKAGE_VERSION, "numericStringSubstringsMatch matching rule plugin"}, /* plugin desc */
 numericStringSubstringsMatch_names, /* matching rule name/oid/aliases */
 NULL, NULL, NULL, numstr_filter_sub, numstr_values2keys,
 NULL, numstr_assertion2keys_sub, numstr_compare},
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

int
numstr_init( Slapi_PBlock *pb )
{
	int	rc, flags;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> numstr_init\n", 0, 0, 0 );

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
	    (void *) numstr_filter_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
	    (void *) numstr_values2keys );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
	    (void *) numstr_assertion2keys );
	flags = SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING;
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FLAGS,
	    (void *) &flags );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_NAMES,
	    (void *) names );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_OID,
	    (void *) NUMERICSTRING_SYNTAX_OID );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_COMPARE,
	    (void *) numstr_compare );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALIDATE,
	    (void *) numstr_validate );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_NORMALIZE,
	    (void *) numstr_normalize );

	rc |= register_matching_rule_plugins();
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= numstr_init %d\n", rc, 0, 0 );
	return( rc );
}

static int
numstr_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
	Slapi_Value **bvals, int ftype, Slapi_Value **retVal )
{
	int filter_normalized = 0;
	int syntax = SYNTAX_SI | SYNTAX_CES;
	if (pb) {
		slapi_pblock_get( pb, SLAPI_PLUGIN_SYNTAX_FILTER_NORMALIZED,
		                  &filter_normalized );
		if (filter_normalized) {
			syntax |= SYNTAX_NORM_FILT;
		}
	}
	return( string_filter_ava( bvfilter, bvals, syntax,
                               ftype, retVal ) );
}

static int
numstr_filter_sub(
    Slapi_PBlock		*pb,
    char		*initial,
    char		**any,
    char		*final,
    Slapi_Value	**bvals
)
{
	return( string_filter_sub( pb, initial, any, final, bvals, SYNTAX_SI | SYNTAX_CES ) );
}

static int
numstr_values2keys( Slapi_PBlock *pb, Slapi_Value **vals, Slapi_Value ***ivals, int ftype )
{
	return( string_values2keys( pb, vals, ivals, SYNTAX_SI | SYNTAX_CES,
                                ftype ) );
}

static int
numstr_assertion2keys( Slapi_PBlock *pb, Slapi_Value *val, Slapi_Value ***ivals, int ftype )
{
	return(string_assertion2keys_ava( pb, val, ivals,
                                      SYNTAX_SI | SYNTAX_CES, ftype ));
}

static int
numstr_assertion2keys_sub(
    Slapi_PBlock		*pb,
    char		*initial,
    char		**any,
    char		*final,
    Slapi_Value	***ivals
)
{
	return( string_assertion2keys_sub( pb, initial, any, final, ivals,
	    SYNTAX_SI | SYNTAX_CES ) );
}

static int numstr_compare(    
	struct berval	*v1,
	struct berval	*v2
)
{
	return value_cmp(v1, v2, SYNTAX_SI | SYNTAX_CES, 3 /* Normalise both values */);
}

/* return 0 if valid, non-0 if invalid */
static int numstr_validate(
	struct berval *val
)
{
	int	rc = 0;    /* assume the value is valid */
	const char	*p = NULL;

	/* Per RFC4517:
	 *
	 *   NumericString = 1*(DIGIT / SPACE)
	 */
	if (val != NULL) {
		for (p = val->bv_val; p < &(val->bv_val[val->bv_len]); p++) {
			if (!isdigit(*p) && !IS_SPACE(*p)) {
				rc = 1;
				goto exit;
			}
		}
	} else {
		rc = 1;
	}

exit:
	return(rc);
}

static void numstr_normalize(
	Slapi_PBlock	*pb,
	char	*s,
	int		trim_spaces,
	char	**alt
)
{
	value_normalize_ext(s, SYNTAX_SI|SYNTAX_CES, trim_spaces, alt);
	return;
}

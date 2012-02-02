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

/* int.c - integer syntax routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

static int int_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
		Slapi_Value **bvals, int ftype, Slapi_Value **retVal );
static int int_values2keys( Slapi_PBlock *pb, Slapi_Value **val,
		Slapi_Value ***ivals, int ftype );
static int int_assertion2keys( Slapi_PBlock *pb, Slapi_Value *val,
		Slapi_Value ***ivals, int ftype );
static int int_compare(struct berval	*v1, struct berval	*v2);
static int int_validate(struct berval *val);
static void int_normalize(
	Slapi_PBlock *pb,
	char    *s,
	int     trim_spaces,
	char    **alt
);

/* the first name is the official one from RFC 2252 */
static char *names[] = { "INTEGER", "int", INTEGER_SYNTAX_OID, 0 };

#define INTEGERMATCH_OID "2.5.13.14"
#define INTEGERORDERINGMATCH_OID "2.5.13.15"

static Slapi_PluginDesc pdesc = { "int-syntax", VENDOR,
	DS_PACKAGE_VERSION, "integer attribute syntax plugin" };

static const char *integerMatch_names[] = {"integerMatch", INTEGERMATCH_OID, NULL};
static const char *integerOrderingMatch_names[] = {"integerOrderingMatch", INTEGERORDERINGMATCH_OID, NULL};
static const char *integerFirstComponentMatch_names[] = {"integerFirstComponentMatch", "2.5.13.29", NULL};

/* hack for now until we can support all of the rfc4517 syntaxes */
static char *integerFirstComponentMatch_syntaxes[] = {DIRSTRING_SYNTAX_OID, NULL};

static struct mr_plugin_def mr_plugin_table[] = {
{{INTEGERMATCH_OID, NULL /* no alias? */,
  "integerMatch", "The rule evaluates to TRUE if and only if the attribute value and the assertion value are the same integer value.",
  INTEGER_SYNTAX_OID, 0 /* not obsolete */, NULL /* no other compatible syntaxes */ },
 {"integerMatch-mr", VENDOR, DS_PACKAGE_VERSION, "integerMatch matching rule plugin" },
 integerMatch_names, /* matching rule name/oid/aliases */
 NULL, NULL, int_filter_ava, NULL, int_values2keys,
 int_assertion2keys, NULL, int_compare},
{{INTEGERORDERINGMATCH_OID, NULL /* no alias? */,
  "integerOrderingMatch", "The rule evaluates to TRUE if and only if the integer value of the attribute value is less than the integer value of the assertion value.",
  INTEGER_SYNTAX_OID, 0 /* not obsolete */, NULL /* no other compatible syntaxes */ },
 {"integerOrderingMatch-mr", VENDOR, DS_PACKAGE_VERSION, "integerOrderingMatch matching rule plugin" },
 integerOrderingMatch_names, /* matching rule name/oid/aliases */
 NULL, NULL, int_filter_ava, NULL, int_values2keys,
 int_assertion2keys, NULL, int_compare},
/* NOTE: THIS IS BROKEN - WE DON'T SUPPORT THE FIRSTCOMPONENT match */
{{"2.5.13.29", NULL, "integerFirstComponentMatch", "The integerFirstComponentMatch rule compares an assertion value of "
"the Integer syntax to an attribute value of a syntax (e.g., the DIT "
"Structure Rule Description syntax) whose corresponding ASN.1 type is "
"a SEQUENCE with a mandatory first component of the INTEGER ASN.1 "
"type.  "
"Note that the assertion syntax of this matching rule differs from the "
"attribute syntax of attributes for which this is the equality "
"matching rule.  "
"The rule evaluates to TRUE if and only if the assertion value and the "
"first component of the attribute value are the same integer value.",
INTEGER_SYNTAX_OID, 0, integerFirstComponentMatch_syntaxes}, /* matching rule desc */
 {"integerFirstComponentMatch-mr", VENDOR, DS_PACKAGE_VERSION, "integerFirstComponentMatch matching rule plugin"}, /* plugin desc */
   integerFirstComponentMatch_names, /* matching rule name/oid/aliases */
   NULL, NULL, int_filter_ava, NULL, int_values2keys,
   int_assertion2keys, NULL, int_compare},
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
int_init( Slapi_PBlock *pb )
{
	int	rc, flags;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> int_init\n", 0, 0, 0 );

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
	    (void *) int_filter_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
	    (void *) int_values2keys );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
	    (void *) int_assertion2keys );
	flags = SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING;
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FLAGS,
	    (void *) &flags );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_NAMES,
	    (void *) names );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_OID,
	    (void *) INTEGER_SYNTAX_OID );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_COMPARE,
	    (void *) int_compare );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALIDATE,
	    (void *) int_validate );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_NORMALIZE,
	    (void *) int_normalize );

	rc |= register_matching_rule_plugins();
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= int_init %d\n", rc, 0, 0 );
	return( rc );
}

static int
int_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
    Slapi_Value **bvals, int ftype, Slapi_Value **retVal )
{
	int filter_normalized = 0;
	int syntax = SYNTAX_INT | SYNTAX_CES;
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
int_values2keys( Slapi_PBlock *pb, Slapi_Value **vals, Slapi_Value ***ivals, int ftype )
{
	return( string_values2keys( pb, vals, ivals, SYNTAX_INT | SYNTAX_CES,
                                ftype ) );
}

static int
int_assertion2keys( Slapi_PBlock *pb, Slapi_Value *val, Slapi_Value ***ivals, int ftype )
{
	return(string_assertion2keys_ava( pb, val, ivals,
                                      SYNTAX_INT | SYNTAX_CES, ftype ));
}

static int int_compare(    
	struct berval	*v1,
    struct berval	*v2
)
{
	return value_cmp(v1, v2, SYNTAX_INT|SYNTAX_CES, 3 /* Normalise both values */);
}

/* return 0 if valid, non-0 if invalid */
static int int_validate(
        struct berval *val
)
{
	int     rc = 0;    /* assume the value is valid */
	char    *p = NULL;
	char	*end = NULL;

	/* Per RFC4517:
	 *
	 *   Integer = (HYPHEN LDIGIT *DIGIT) / number
	 *   number  = DIGIT / (LDIGIT 1*DIGIT)
	 */
        if ((val != NULL) && (val->bv_len > 0)) {
		p = val->bv_val;
		end = &(val->bv_val[val->bv_len - 1]);

		/* If the first character is HYPHEN, we need
		 * to make sure the next char is a LDIGIT. */
		if (*p == '-') {
			p++;
			if ((p > end) || !IS_LDIGIT(*p)) {
				rc = 1;
				goto exit;
			}
			p++;
		} else if (*p == '0') {
			/* 0 is allowed by itself, but not as
			 * a leading 0 before other digits */
			if (p != end) {
				rc = 1;
			}

			/* We're done here */
			goto exit;
		}

		/* Now we can simply allow the rest to be DIGIT */
                for (; p <= end; p++) {
                        if (!isdigit(*p)) {
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

static void int_normalize(
	Slapi_PBlock	*pb,
	char	*s,
	int		trim_spaces,
	char	**alt
)
{
	value_normalize_ext(s, SYNTAX_INT|SYNTAX_CES, trim_spaces, alt);
	return;
}

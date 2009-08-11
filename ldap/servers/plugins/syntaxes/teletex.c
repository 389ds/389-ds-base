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

/* teletex.c - Teletex Terminal Identifier syntax routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

static int teletex_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
		Slapi_Value **bvals, int ftype, Slapi_Value **retVal );
static int teletex_filter_sub( Slapi_PBlock *pb, char *initial, char **any,
		char *final, Slapi_Value **bvals );
static int teletex_values2keys( Slapi_PBlock *pb, Slapi_Value **val,
		Slapi_Value ***ivals, int ftype );
static int teletex_assertion2keys_ava( Slapi_PBlock *pb, Slapi_Value *val,
		Slapi_Value ***ivals, int ftype );
static int teletex_assertion2keys_sub( Slapi_PBlock *pb, char *initial, char **any,
		char *final, Slapi_Value ***ivals );
static int teletex_compare(struct berval	*v1, struct berval	*v2);
static int teletex_validate(struct berval *val);
static int ttx_param_validate(const char *start, const char *end);

/* the first name is the official one from RFC 4517 */
static char *names[] = { "Teletex Terminal Identifier", "teletextermid", TELETEXTERMID_SYNTAX_OID, 0 };

static Slapi_PluginDesc pdesc = { "teletextermid-syntax", VENDOR, PACKAGE_VERSION,
	"Teletex Terminal Identifier attribute syntax plugin" };

int
teletex_init( Slapi_PBlock *pb )
{
	int	rc, flags;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> teletex_init\n", 0, 0, 0 );

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
	    (void *) teletex_filter_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_SUB,
	    (void *) teletex_filter_sub );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
	    (void *) teletex_values2keys );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
	    (void *) teletex_assertion2keys_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB,
	    (void *) teletex_assertion2keys_sub );
	flags = SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING;
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FLAGS,
	    (void *) &flags );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_NAMES,
	    (void *) names );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_OID,
	    (void *) TELETEXTERMID_SYNTAX_OID );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_COMPARE,
	    (void *) teletex_compare );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALIDATE,
	    (void *) teletex_validate );

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= teletex_init %d\n", rc, 0, 0 );
	return( rc );
}

static int
teletex_filter_ava(
    Slapi_PBlock		*pb,
    struct berval	*bvfilter,
    Slapi_Value	**bvals,
    int			ftype,
	Slapi_Value **retVal
)
{
	return( string_filter_ava( bvfilter, bvals, SYNTAX_CIS,
	    ftype, retVal ) );
}


static int
teletex_filter_sub(
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
teletex_values2keys(
    Slapi_PBlock		*pb,
    Slapi_Value	**vals,
    Slapi_Value	***ivals,
    int			ftype
)
{
	return( string_values2keys( pb, vals, ivals, SYNTAX_CIS,
	    ftype ) );
}

static int
teletex_assertion2keys_ava(
    Slapi_PBlock		*pb,
    Slapi_Value	*val,
    Slapi_Value	***ivals,
    int			ftype
)
{
	return(string_assertion2keys_ava( pb, val, ivals,
	    SYNTAX_CIS, ftype ));
}

static int
teletex_assertion2keys_sub(
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

static int teletex_compare(    
	struct berval	*v1,
    struct berval	*v2
)
{
	return value_cmp(v1, v2, SYNTAX_CIS, 3 /* Normalise both values */);
}

static int
teletex_validate(
	struct berval	*val
)
{
	int rc = 0;    /* assume the value is valid */
	const char *start = NULL;
	const char *end = NULL;
	const char *p = NULL;
	int got_ttx_term = 0;

	/* Per RFC4517:
	 *
	 * teletex-id = ttx-term *(DOLLAR ttx-param)
	 * ttx-term   = PrintableString
	 * ttx-param  = ttx-key COLON ttx-value
	 * tty-key    = "graphic" / "control" / "misc" / "page" / "private"
	 * ttx-value  = *ttx-value-octet
	 *
	 * ttx-value-octet = %x00-23
	 *                   / (%x5C "24")  ; escaped "$"
	 *                   / %x25-5B
	 *                   / (%x5C "5C")  ; escaped "\"
	 *                   / %x5D-FF
	 */

	/* Don't allow a 0 length string */
	if ((val == NULL) || (val->bv_len == 0)) {
		rc = 1;
		goto exit;
	}

	start = &(val->bv_val[0]);
	end = &(val->bv_val[val->bv_len - 1]);

	/* Look for a DOLLAR separator. */
	for (p = start; p <= end; p++) {
		if (IS_DOLLAR(*p)) {
			/* Ensure we don't have an empty element. */
			if ((p == start) || (p == end)) {
				rc = 1;
				goto exit;
			}

			if (!got_ttx_term) {
				/* Validate the ttx-term. */
				while (start < p) {
					if (!IS_PRINTABLE(*start)) {
						rc = 1;
						goto exit;
					}
					start++;
				}

				got_ttx_term = 1;
			} else {
				/* Validate the ttx-param. */
				if ((rc = ttx_param_validate(start, p - 1)) != 0) {
					rc = 1;
					goto exit;
				}
			}

			/* Reset start to point at the
			 * next ttx-param.  We're
			 * guaranteed to have at least
			 * one more char after p. */
			start = p + 1;
		}
	}

	/* If we didn't find the ttx-term, validate
	 * the whole value as the ttx-term. */
	if (!got_ttx_term) {
		for (p = start; p <= end; p++) {
			if (!IS_PRINTABLE(*p)) {
				rc = 1;
				goto exit;
			}
		}
	} else {
		/* Validate the final ttx-param. */
		rc = ttx_param_validate(start, end);
	}

exit:
	return rc;
}

static int
ttx_param_validate(
	const char *start,
	const char *end)
{
	int rc = 0;
	const char *p = NULL;
	int found_colon = 0;

	for (p = start; p <= end; p++) {
		if (IS_COLON(*p)) {
			found_colon = 1;

			/* Validate the ttx-key before the COLON. */
			switch (p - start) {
				case 4:
					/* "misc" / "page" */
					if ((strncmp(start, "misc", 4) != 0) &&
					    (strncmp(start, "page", 4) != 0)) {
						rc = 1;
						goto exit;
					}
					break;
				case 7:
					/* "graphic" / "control" / "private" */
					if ((strncmp(start, "graphic", 7) != 0) &&
					    (strncmp(start, "control", 7) != 0) &&
					    (strncmp(start, "private", 7) != 0)) {
						rc = 1;
						goto exit;
					}
					break;
				default:
					rc = 1;
					goto exit;
			}

			/* Validate the ttx-value after the COLON.
			 * It is allowed to be 0 length. */
			if (p != end) {
				for (++p; p <= end; p++) {
					/* Ensure that '\' is only used
					 * to escape a '$' or a '\'. */
					if (*p == '\\') {
						p++;
						/* Ensure that we're not at the end of the value */
						if ((p > end) || ((strncmp(p, "24", 2) != 0)
						    && (strncasecmp(p, "5C", 2) != 0))) {
							rc = 1;
							goto exit;
						} else {
							/* advance the pointer to point to the end
							 * of the hex code for the escaped character */
							p++;
						}
					} else if (*p == '$') {
						/* This should be escaped.  Fail. */
						rc = 1;
						goto exit;
					}
				}
			}

			/* We're done. */
			break;
		}
	}

	/* If we didn't find a COLON, fail. */
	if (!found_colon) {
		rc = 1;
	}

exit:
	return rc;
}

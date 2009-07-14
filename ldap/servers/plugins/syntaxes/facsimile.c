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

/* facsimile.c - Facsimile Telephone Number syntax routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

static int facsimile_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
		Slapi_Value **bvals, int ftype, Slapi_Value **retVal );
static int facsimile_filter_sub( Slapi_PBlock *pb, char *initial, char **any,
		char *final, Slapi_Value **bvals );
static int facsimile_values2keys( Slapi_PBlock *pb, Slapi_Value **val,
		Slapi_Value ***ivals, int ftype );
static int facsimile_assertion2keys_ava( Slapi_PBlock *pb, Slapi_Value *val,
		Slapi_Value ***ivals, int ftype );
static int facsimile_assertion2keys_sub( Slapi_PBlock *pb, char *initial, char **any,
		char *final, Slapi_Value ***ivals );
static int facsimile_compare(struct berval	*v1, struct berval	*v2);
static int facsimile_validate(struct berval *val);
static int fax_parameter_validate(const char *start, const char *end);

/* the first name is the official one from RFC 4517 */
static char *names[] = { "Facsimile Telephone Number", "facsimile", FACSIMILE_SYNTAX_OID, 0 };

static Slapi_PluginDesc pdesc = { "facsimile-syntax", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT,
	"Facsimile Telephone Number attribute syntax plugin" };

int
facsimile_init( Slapi_PBlock *pb )
{
	int	rc, flags;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> facsimile_init\n", 0, 0, 0 );

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
	    (void *) facsimile_filter_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_SUB,
	    (void *) facsimile_filter_sub );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
	    (void *) facsimile_values2keys );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
	    (void *) facsimile_assertion2keys_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB,
	    (void *) facsimile_assertion2keys_sub );
	flags = SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING;
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FLAGS,
	    (void *) &flags );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_NAMES,
	    (void *) names );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_OID,
	    (void *) FACSIMILE_SYNTAX_OID );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_COMPARE,
	    (void *) facsimile_compare );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALIDATE,
	    (void *) facsimile_validate );

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= facsimile_init %d\n", rc, 0, 0 );
	return( rc );
}

static int
facsimile_filter_ava(
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
facsimile_filter_sub(
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
facsimile_values2keys(
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
facsimile_assertion2keys_ava(
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
facsimile_assertion2keys_sub(
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

static int facsimile_compare(    
	struct berval	*v1,
    struct berval	*v2
)
{
	return value_cmp(v1, v2, SYNTAX_CIS, 3 /* Normalise both values */);
}

static int
facsimile_validate(
	struct berval	*val
)
{
	int     rc = 0;    /* assume the value is valid */
	int	i = 0;

	/* Per RFC4517:
	 *
	 * fax-number       = telephone-number *( DOLLAR fax-parameter )
	 * telephone-number = PrintableString
	 * fax-parameter    = "twoDimensional" /
	 *                    "fineResolution" /
	 *                    "unlimitedLength" /
	 *                    "b4Length" /
	 *                    "a3Width" /
	 *                    "b4Width" /
	 *                    "uncompressed"
	 */

	/* Don't allow a 0 length string */
	if ((val == NULL) || (val->bv_len == 0)) {
		rc = 1;
		goto exit;
	}

	/* Make sure all chars are a PrintableCharacter */
	for (i=0; i < val->bv_len; i++) {
		if (!IS_PRINTABLE(val->bv_val[i])) {
			if (!IS_DOLLAR(val->bv_val[i])) {
				rc = 1;
				goto exit;
			} else {
				/* Process the fax-parameters */
				const char *start = NULL;
				const char *end = &(val->bv_val[val->bv_len - 1]);
				const char *p = &(val->bv_val[i]);

				/* The value must have a printable string first,
				 * so we can't allow it to start with a '$'.  We
				 * also need to ensure that the string does not
				 * end with this '$'. */
				if ((i == 0) || (p == end)) {
					rc = 1;
					goto exit;
				}

				/* We're guaranteed to have at least one character
				 * past p.  This is where the fax-paramter should
				 * start. */
				start = p + 1;

				for (p = start; p <= end; p++) {
					if (p == end) {
						/* Ensure start to p is a valid fax-parameter, then
						 * exit since we're at the end. */
						rc = fax_parameter_validate(start, p);
						goto exit;
					} else if (*p == '$') { 
						/* Ensure start to p-1 is a valid fax-parameter */
						if ((rc = fax_parameter_validate(start, p - 1)) != 0) {
							goto exit;
						}

						/* We're guaranteed to have another character, which
						 * should be the beginning of the next fax-paramter.
						 * Adjust the start pointer to point to the beginning
						 * of this fax-paramter. */
						start = p + 1;
					}
				}
			}
		}
	}

exit:
	return rc;
}

/*
 * fax_parameter_validate()
 *
 * Returns 0 if the string from start to end is a valid
 * fax-parameter, otherwise returns 1.
 */
static int
fax_parameter_validate(const char *start, const char *end)
{
	int rc = 0; /* Assume string is valid */
	size_t length = 0;
	const char *p = NULL;

	if ((start == NULL) || (end == NULL)) {
		rc = 1;
		goto exit;
	}

	/* Per RFC4517:
	 *
	 * fax-parameter    = "twoDimensional" /
	 *                    "fineResolution" /
	 *                    "unlimitedLength" /
	 *                    "b4Length" /
	 *                    "a3Width" /
	 *                    "b4Width" /
	 *                    "uncompressed"
	 */

	/* Check length first for efficiency. */
	length = end - start + 1;
	switch (length) {
		case 7:
			if ((strncmp(start, "a3Width", length) != 0) &&
			    (strncmp(start, "b4Width", length) != 0)) {
				rc = 1;
			}
			break;
		case 8:
			if (strncmp(start, "b4Length", length) != 0) {
				rc = 1;
			}
			break;
		case 12:
			if (strncmp(start, "uncompressed", length) != 0) {
				rc = 1;
			}
			break;
		case 14:
			if ((strncmp(start, "twoDimensional", length) != 0) &&
			    (strncmp(start, "fineResolution", length) != 0)) {
				rc = 1;
			}
			break;
		case 15:
			if (strncmp(start, "unlimitedLength", length) != 0) {
				rc = 1;
			}
			break;
		default:
			rc = 1;
			break;
	}

exit:
	return rc;
}

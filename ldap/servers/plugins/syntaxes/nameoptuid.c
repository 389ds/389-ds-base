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

/* nameoptuid.c - Name And Optional UID syntax routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

static int nameoptuid_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
		Slapi_Value **bvals, int ftype, Slapi_Value **retVal );
static int nameoptuid_filter_sub( Slapi_PBlock *pb, char *initial, char **any,
		char *final, Slapi_Value **bvals );
static int nameoptuid_values2keys( Slapi_PBlock *pb, Slapi_Value **val,
		Slapi_Value ***ivals, int ftype );
static int nameoptuid_assertion2keys_ava( Slapi_PBlock *pb, Slapi_Value *val,
		Slapi_Value ***ivals, int ftype );
static int nameoptuid_assertion2keys_sub( Slapi_PBlock *pb, char *initial, char **any,
		char *final, Slapi_Value ***ivals );
static int nameoptuid_compare(struct berval	*v1, struct berval	*v2);
static int nameoptuid_validate(struct berval *val);

/* the first name is the official one from RFC 4517 */
static char *names[] = { "Name And Optional UID", "nameoptuid", NAMEANDOPTIONALUID_SYNTAX_OID, 0 };

static Slapi_PluginDesc pdesc = { "nameoptuid-syntax", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT,
	"Name And Optional UID attribute syntax plugin" };

int
nameoptuid_init( Slapi_PBlock *pb )
{
	int	rc, flags;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> nameoptuid_init\n", 0, 0, 0 );

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
	    (void *) nameoptuid_filter_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_SUB,
	    (void *) nameoptuid_filter_sub );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
	    (void *) nameoptuid_values2keys );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
	    (void *) nameoptuid_assertion2keys_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB,
	    (void *) nameoptuid_assertion2keys_sub );
	flags = SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING;
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FLAGS,
	    (void *) &flags );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_NAMES,
	    (void *) names );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_OID,
	    (void *) NAMEANDOPTIONALUID_SYNTAX_OID );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_COMPARE,
	    (void *) nameoptuid_compare );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALIDATE,
	    (void *) nameoptuid_validate );

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= nameoptuid_init %d\n", rc, 0, 0 );
	return( rc );
}

static int
nameoptuid_filter_ava(
    Slapi_PBlock		*pb,
    struct berval	*bvfilter,
    Slapi_Value	**bvals,
    int			ftype,
	Slapi_Value **retVal
)
{
	return( string_filter_ava( bvfilter, bvals, SYNTAX_CIS | SYNTAX_DN,
	    ftype, retVal ) );
}


static int
nameoptuid_filter_sub(
    Slapi_PBlock		*pb,
    char		*initial,
    char		**any,
    char		*final,
    Slapi_Value	**bvals
)
{
	return( string_filter_sub( pb, initial, any, final, bvals, 
            SYNTAX_CIS | SYNTAX_DN ) );
}

static int
nameoptuid_values2keys(
    Slapi_PBlock		*pb,
    Slapi_Value	**vals,
    Slapi_Value	***ivals,
    int			ftype
)
{
	return( string_values2keys( pb, vals, ivals, SYNTAX_CIS | SYNTAX_DN,
	    ftype ) );
}

static int
nameoptuid_assertion2keys_ava(
    Slapi_PBlock		*pb,
    Slapi_Value	*val,
    Slapi_Value	***ivals,
    int			ftype
)
{
	return(string_assertion2keys_ava( pb, val, ivals,
	    SYNTAX_CIS | SYNTAX_DN, ftype ));
}

static int
nameoptuid_assertion2keys_sub(
    Slapi_PBlock		*pb,
    char		*initial,
    char		**any,
    char		*final,
    Slapi_Value	***ivals
)
{
	return( string_assertion2keys_sub( pb, initial, any, final, ivals,
	    SYNTAX_CIS | SYNTAX_DN ) );
}

static int nameoptuid_compare(    
	struct berval	*v1,
    struct berval	*v2
)
{
	return value_cmp(v1, v2, SYNTAX_CIS | SYNTAX_DN, 3 /* Normalise both values */);
}

static int
nameoptuid_validate(
	struct berval	*val
)
{
	int rc = 0;    /* assume the value is valid */
	int got_sharp = 0;
	const char *p = NULL;
	const char *start = NULL;
	const char *end = NULL;

	/* Per RFC4517:
	 *
	 * NameAndOptionalUID = distinguishedName [ SHARP BitString ]
	 */

	/* Don't allow a 0 length string */
	if ((val == NULL) || (val->bv_len == 0)) {
		rc = 1;
		goto exit;
	}

	start = &(val->bv_val[0]);
	end = &(val->bv_val[val->bv_len - 1]);

	/* Find the last SHARP in the value that may be separating
	 * the distinguishedName from the optional BitString. */
	for (p = end; p >= start + 1; p--) {
		if (IS_SHARP(*p)) {
			got_sharp = 1;
			break;
		}
	}

	if (got_sharp) {
		/* Try to validate everything after the sharp as
		 * a BitString.  If this fails, we may still have
		 * a valid value since a sharp is allowed in a
		 * distinguishedName.  If we don't find a valid
		 * BitString, just validate the entire value as
		 * a distinguishedName. */
		if ((rc = bitstring_validate_internal(p + 1, end)) != 0) {
			rc = distinguishedname_validate(start, end);
		} else {
			rc = distinguishedname_validate(start, p - 1);
		}
	} else {
		/* No optional BitString is present, so validate
		 * the entire value as a distinguishedName. */
		rc = distinguishedname_validate(start, end);
	}

exit:
	return rc;
}


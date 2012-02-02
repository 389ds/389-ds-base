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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


/*
 * sicis.c - space insensitive string syntax routines.
 *           these strings are also case insensitive.
 *
 * This is a non-standard syntax.  It is only used by the presence plug-in.
 * It will be disabled by default unless the presence plug-in is compiled.
 */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

static int sicis_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
		Slapi_Value **bvals, int ftype, Slapi_Value **retVal );
static int sicis_filter_sub( Slapi_PBlock *pb, char *initial, char **any,
		char *final, Slapi_Value **bvals );
static int sicis_values2keys( Slapi_PBlock *pb, Slapi_Value **val,
		Slapi_Value ***ivals, int ftype );
static int sicis_assertion2keys_ava( Slapi_PBlock *pb, Slapi_Value *val,
		Slapi_Value ***ivals, int ftype );
static int sicis_assertion2keys_sub( Slapi_PBlock *pb, char *initial,
		char **any, char *final, Slapi_Value ***ivals );
static int sicis_compare(struct berval	*v1, struct berval	*v2);
static void sicis_normalize(
	Slapi_PBlock *pb,
	char    *s,
	int     trim_spaces,
	char    **alt
);

/* the first name is the official one from RFC 2252 */
static char *names[] = { "SpaceInsensitiveString",
		SPACE_INSENSITIVE_STRING_SYNTAX_OID, 0 };

static Slapi_PluginDesc pdesc = { "spaceinsensitivestring-syntax",
	VENDOR, DS_PACKAGE_VERSION,
	"space insensitive string attribute syntax plugin" };

int
sicis_init( Slapi_PBlock *pb )
{
	int	rc, flags;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> sicis_init\n", 0, 0, 0 );

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
	    (void *) sicis_filter_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_SUB,
	    (void *) sicis_filter_sub );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
	    (void *) sicis_values2keys );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
	    (void *) sicis_assertion2keys_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB,
	    (void *) sicis_assertion2keys_sub );
	flags = SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING;
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FLAGS,
	    (void *) &flags );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_NAMES,
	    (void *) names );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_OID,
	    (void *) SPACE_INSENSITIVE_STRING_SYNTAX_OID );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_COMPARE,
	    (void *) sicis_compare );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_NORMALIZE,
	    (void *) sicis_normalize );

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= sicis_init %d\n", rc, 0, 0 );
	return( rc );
}

static int
sicis_filter_ava(
    Slapi_PBlock		*pb,
    struct berval	*bvfilter,
    Slapi_Value	**bvals,
    int			ftype,
    Slapi_Value **retVal
)
{
	int filter_normalized = 0;
	int syntax = SYNTAX_SI | SYNTAX_CIS;
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
sicis_filter_sub(
    Slapi_PBlock		*pb,
    char		*initial,
    char		**any,
    char		*final,
    Slapi_Value	**bvals
)
{
	return( string_filter_sub( pb, initial, any, final, bvals, SYNTAX_SI | SYNTAX_CIS ) );
}

static int
sicis_values2keys(
    Slapi_PBlock		*pb,
    Slapi_Value	**vals,
    Slapi_Value	***ivals,
    int			ftype
)
{
	return( string_values2keys( pb, vals, ivals, SYNTAX_SI | SYNTAX_CIS,
	    ftype ) );
}

static int
sicis_assertion2keys_ava(
    Slapi_PBlock		*pb,
    Slapi_Value	*val,
    Slapi_Value	***ivals,
    int			ftype
)
{
	return(string_assertion2keys_ava( pb, val, ivals,
	    SYNTAX_SI | SYNTAX_CIS, ftype ));
}

static int
sicis_assertion2keys_sub(
    Slapi_PBlock		*pb,
    char		*initial,
    char		**any,
    char		*final,
    Slapi_Value	***ivals
)
{
	return( string_assertion2keys_sub( pb, initial, any, final, ivals,
	    SYNTAX_SI | SYNTAX_CIS ) );
}

static int sicis_compare(    
	struct berval	*v1,
    struct berval	*v2
)
{
	return value_cmp(v1, v2, SYNTAX_SI|SYNTAX_CIS, 3 /* Normalise both values */);
}

static void sicis_normalize(
	Slapi_PBlock	*pb,
	char	*s,
	int		trim_spaces,
	char	**alt
)
{
	value_normalize_ext(s, SYNTAX_SI|SYNTAX_CIS, trim_spaces, alt);
	return;
}

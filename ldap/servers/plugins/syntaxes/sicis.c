/** BEGIN COPYRIGHT BLOCK
 * Copyright 2002 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * sicis.c - space insensitive string syntax routines.
 *           these strings are also case insensitive.
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

/* the first name is the official one from RFC 2252 */
static char *names[] = { "SpaceInsensitiveString",
		SPACE_INSENSITIVE_STRING_SYNTAX_OID, 0 };

static Slapi_PluginDesc pdesc = { "spaceinsensitivestring-syntax",
	PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT,
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
	return( string_filter_ava( bvfilter, bvals, SYNTAX_SI | SYNTAX_CIS,
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

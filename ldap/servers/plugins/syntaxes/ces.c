/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* ces.c - caseexactstring syntax routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

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

/* the first name is the official one from RFC 2252 */
static char *ia5_names[] = { "IA5String", "ces", "caseexactstring",
			     IA5STRING_SYNTAX_OID, 0 };

/* the first name is the official one from RFC 2252 */
static char *uri_names[] = { "URI", "1.3.6.1.4.1.4401.1.1.1",0};

static Slapi_PluginDesc ia5_pdesc = { "ces-syntax", PLUGIN_MAGIC_VENDOR_STR,
	PRODUCTTEXT, "caseExactString attribute syntax plugin" };

static Slapi_PluginDesc uri_pdesc = { "uri-syntax", PLUGIN_MAGIC_VENDOR_STR,
	PRODUCTTEXT, "uri attribute syntax plugin" };


/*
 * register_ces_like_plugin():  register all items for a cis-like plugin.
 */
static int
register_ces_like_plugin( Slapi_PBlock *pb, Slapi_PluginDesc *pdescp,
		char **names, char *oid )
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

	return( rc );
}

int
ces_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> ces_init\n", 0, 0, 0 );

	rc = register_ces_like_plugin(pb,&ia5_pdesc,ia5_names,IA5STRING_SYNTAX_OID);

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= ces_init %d\n", rc, 0, 0 );
	return( rc );
}

int
uri_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> uri_init\n", 0, 0, 0 );

	rc = register_ces_like_plugin(pb,&uri_pdesc,uri_names,
				      "1.3.6.1.4.1.4401.1.1.1");

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

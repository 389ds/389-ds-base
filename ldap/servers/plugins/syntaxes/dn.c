/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* dn.c - dn syntax routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

static int dn_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
	Slapi_Value **bvals, int ftype, Slapi_Value **retVal );
static int dn_filter_sub( Slapi_PBlock *pb, char *initial, char **any,
	char *final, Slapi_Value **bvals );
static int dn_values2keys( Slapi_PBlock *pb, Slapi_Value **vals,
	Slapi_Value ***ivals, int ftype );
static int dn_assertion2keys_ava( Slapi_PBlock *pb, Slapi_Value *val,
	Slapi_Value ***ivals, int ftype );
static int dn_assertion2keys_sub( Slapi_PBlock *pb, char *initial, char **any,
	char *final, Slapi_Value ***ivals );

/* the first name is the official one from RFC 2252 */
static char *names[] = { "DN", DN_SYNTAX_OID, 0 };

static Slapi_PluginDesc pdesc = { "dn-syntax", PLUGIN_MAGIC_VENDOR_STR,
	PRODUCTTEXT, "distinguished name attribute syntax plugin" };

int
dn_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> dn_init\n", 0, 0, 0 );

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
	    (void *) dn_filter_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_SUB,
	    (void *) dn_filter_sub );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
	    (void *) dn_values2keys );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
	    (void *) dn_assertion2keys_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB,
	    (void *) dn_assertion2keys_sub );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_NAMES,
	    (void *) names );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_OID,
	    (void *) DN_SYNTAX_OID );

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= dn_init %d\n", rc, 0, 0 );
	return( rc );
}

static int
dn_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
    Slapi_Value **bvals, int ftype, Slapi_Value **retVal )
{
	return( string_filter_ava( bvfilter, bvals, SYNTAX_CIS | SYNTAX_DN,
	    ftype, retVal ) );
}

static int
dn_filter_sub( Slapi_PBlock *pb, char *initial, char **any, char *final,
    Slapi_Value **bvals )
{
	return( string_filter_sub( pb, initial, any, final, bvals,
	    SYNTAX_CIS | SYNTAX_DN ) );
}

static int
dn_values2keys( Slapi_PBlock *pb, Slapi_Value **vals, Slapi_Value ***ivals,
    int ftype )
{
	return( string_values2keys( pb, vals, ivals, SYNTAX_CIS | SYNTAX_DN,
	    ftype ) );
}

static int
dn_assertion2keys_ava( Slapi_PBlock *pb, Slapi_Value *val,
    Slapi_Value ***ivals, int ftype )
{
	return( string_assertion2keys_ava( pb, val, ivals,
	    SYNTAX_CIS | SYNTAX_DN, ftype ) );
}

static int
dn_assertion2keys_sub( Slapi_PBlock *pb, char *initial, char **any, char *final,
    Slapi_Value ***ivals )
{
	return( string_assertion2keys_sub( pb, initial, any, final, ivals,
	    SYNTAX_CIS | SYNTAX_DN ) );
}

/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* cis.c - caseignorestring syntax routines */

/*
 * This file actually implements three syntax plugins:
 *		DirectoryString
 *		Boolean
 *		GeneralizedTime
 *
 * We treat them identically for now.  XXXmcs: we could do some validation on
 *		Boolean and GeneralizedTime values (someday, maybe).
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

static char *country_names[] = { "Country String",
		COUNTRYSTRING_SYNTAX_OID, 0};

static char *postal_names[] = { "Postal Address",
		POSTALADDRESS_SYNTAX_OID, 0};

static char *oid_names[] = { "OID",
		OID_SYNTAX_OID, 0};


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
		PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT,
		"DirectoryString attribute syntax plugin" };

static Slapi_PluginDesc boolean_pdesc = { "boolean-syntax",
		PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT,
		"Boolean attribute syntax plugin" };

static Slapi_PluginDesc time_pdesc = { "time-syntax",
		PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT,
		"GeneralizedTime attribute syntax plugin" };

static Slapi_PluginDesc country_pdesc = { "countrystring-syntax",
		PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT,
		"Country String attribute syntax plugin" };

static Slapi_PluginDesc postal_pdesc = { "postaladdress-syntax",
		PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT,
		"Postal Address attribute syntax plugin" };

static Slapi_PluginDesc oid_pdesc = { "oid-syntax",
		PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT,
		"OID attribute syntax plugin" };


/*
 * register_cis_like_plugin():  register all items for a cis-like plugin.
 */
static int
register_cis_like_plugin( Slapi_PBlock *pb, Slapi_PluginDesc *pdescp,
		char **names, char *oid )
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

	return( rc );
}


int
cis_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> cis_init\n", 0, 0, 0 );
	rc = register_cis_like_plugin( pb, &dirstring_pdesc, dirstring_names,
		 	DIRSTRING_SYNTAX_OID );
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= cis_init %d\n", rc, 0, 0 );
	return( rc );
}


int
boolean_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> boolean_init\n", 0, 0, 0 );
	rc = register_cis_like_plugin( pb, &boolean_pdesc, boolean_names,
			BOOLEAN_SYNTAX_OID );
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= boolean_init %d\n", rc, 0, 0 );
	return( rc );
}


int
time_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> time_init\n", 0, 0, 0 );
	rc = register_cis_like_plugin( pb, &time_pdesc, time_names,
			GENERALIZEDTIME_SYNTAX_OID );
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= time_init %d\n", rc, 0, 0 );
	return( rc );
}

int
country_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> country_init\n", 0, 0, 0 );
	rc = register_cis_like_plugin( pb, &country_pdesc, country_names,
				       COUNTRYSTRING_SYNTAX_OID );
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= country_init %d\n", rc, 0, 0 );
	return( rc );
}

int
postal_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> postal_init\n", 0, 0, 0 );
	rc = register_cis_like_plugin( pb, &postal_pdesc, postal_names,
				       POSTALADDRESS_SYNTAX_OID );
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= postal_init %d\n", rc, 0, 0 );
	return( rc );
}


int
oid_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> oid_init\n", 0, 0, 0 );
	rc = register_cis_like_plugin( pb, &oid_pdesc, oid_names, OID_SYNTAX_OID );
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= oid_init %d\n", rc, 0, 0 );
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

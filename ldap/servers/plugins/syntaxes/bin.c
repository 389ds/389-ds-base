/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* bin.c - bin syntax routines */

/*
 * This file actually implements two syntax plugins: OctetString and Binary.
 * We treat them identically for now.  XXXmcs: check if that is correct.
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

static int bin_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
			Slapi_Value **bvals, int ftype, Slapi_Value **retVal );
static int bin_values2keys( Slapi_PBlock *pb, Slapi_Value **bvals,
	Slapi_Value ***ivals, int ftype );
static int bin_assertion2keys_ava( Slapi_PBlock *pb, Slapi_Value *bval,
	Slapi_Value ***ivals, int ftype );

/*
 * Attribute syntaxes. We treat all of these the same for now, even though
 * the specifications (e.g., RFC 2252) impose various constraints on the
 * the format for each of these.
 *
 * Note: the first name is the official one from RFC 2252.
 */
static char *bin_names[] = { "Binary", "bin", BINARY_SYNTAX_OID, 0 };

static char *octetstring_names[] = { "OctetString", OCTETSTRING_SYNTAX_OID, 0 };

static char *jpeg_names[] = { "JPEG", JPEG_SYNTAX_OID, 0 };


static Slapi_PluginDesc bin_pdesc = {
	"bin-syntax", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT,
	"binary attribute syntax plugin"
};

static Slapi_PluginDesc octetstring_pdesc = {
	"octetstring-syntax", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT,
	"octet string attribute syntax plugin"
};

static Slapi_PluginDesc jpeg_pdesc = {
	"jpeg-syntax", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT,
	"JPEG attribute syntax plugin"
};


/*
 * register_bin_like_plugin():  register all items for a bin-like plugin.
 */
static int
register_bin_like_plugin( Slapi_PBlock *pb, Slapi_PluginDesc *pdescp,
		char **names, char *oid )
{
	int	rc;

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)pdescp );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
	    (void *) bin_filter_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
	    (void *) bin_values2keys );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
	    (void *) bin_assertion2keys_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_NAMES,
	    (void *) names );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_OID,
	    (void *) oid );

	return( rc );
}


int
bin_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> bin_init\n", 0, 0, 0 );
	rc = register_bin_like_plugin( pb, &bin_pdesc, bin_names,
		 	BINARY_SYNTAX_OID );
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= bin_init %d\n", rc, 0, 0 );
	return( rc );
}


int
octetstring_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> octetstring_init\n", 0, 0, 0 );
	rc = register_bin_like_plugin( pb, &octetstring_pdesc, octetstring_names,
		 	OCTETSTRING_SYNTAX_OID );
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= octetstring_init %d\n", rc, 0, 0 );
	return( rc );
}


int
jpeg_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> jpeg_init\n", 0, 0, 0 );
	rc = register_bin_like_plugin( pb, &jpeg_pdesc, jpeg_names,
		 	JPEG_SYNTAX_OID );
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= jpeg_init %d\n", rc, 0, 0 );
	return( rc );
}


static int
bin_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
    Slapi_Value **bvals, int ftype, Slapi_Value **retVal )
{
	int	i;

	for ( i = 0; bvals[i] != NULL; i++ ) {
        if ( slapi_value_get_length(bvals[i]) == bvfilter->bv_len &&
            0 == memcmp( slapi_value_get_string(bvals[i]), bvfilter->bv_val, bvfilter->bv_len ))
        {
			if(retVal!=NULL)
			{
				*retVal= bvals[i];
			}
            return( 0 );
        }
	}
	if(retVal!=NULL)
	{
		*retVal= NULL;
	}
	return( -1 );
}

static int
bin_values2keys( Slapi_PBlock *pb, Slapi_Value **bvals,
					Slapi_Value ***ivals, int ftype )
{
	int	i;

	if ( ftype != LDAP_FILTER_EQUALITY ) {
		return( LDAP_PROTOCOL_ERROR );
	}

	for ( i = 0; bvals[i] != NULL; i++ ) {
		/* NULL */
	}
	(*ivals) = (Slapi_Value **) slapi_ch_malloc(( i + 1 ) *
	    sizeof(Slapi_Value *) );

	for ( i = 0; bvals[i] != NULL; i++ )
	{
		(*ivals)[i] = slapi_value_dup(bvals[i]);
	}
	(*ivals)[i] = NULL;

	return( 0 );
}

static int
bin_assertion2keys_ava( Slapi_PBlock *pb, Slapi_Value *bval,
    Slapi_Value ***ivals, int ftype )
{
    Slapi_Value *tmpval=NULL;
    size_t len;

    if (( ftype != LDAP_FILTER_EQUALITY ) &&
        ( ftype != LDAP_FILTER_EQUALITY_FAST))
    {
		return( LDAP_PROTOCOL_ERROR );
	}
    if(ftype == LDAP_FILTER_EQUALITY_FAST) {
                /* With the fast option, we are trying to avoid creating and freeing
         * a bunch of structures - we just do one malloc here - see
         * ava_candidates in filterentry.c
         */
        len=slapi_value_get_length(bval);
        tmpval=(*ivals)[0];
        if (len > tmpval->bv.bv_len) {
            tmpval->bv.bv_val=(char *)slapi_ch_malloc(len);
        }
        tmpval->bv.bv_len=len;
        memcpy(tmpval->bv.bv_val,slapi_value_get_string(bval),len);
    } else {
	    (*ivals) = (Slapi_Value **) slapi_ch_malloc( 2 * sizeof(Slapi_Value *) );
	    (*ivals)[0] = slapi_value_dup( bval );
	    (*ivals)[1] = NULL;
    }
	return( 0 );
}

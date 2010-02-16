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

/* bin.c - bin syntax routines */

/*
 * This file actually implements four syntax plugins: OctetString, JPEG,
 * Fax, and Binary.
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

#define CERTIFICATE_SYNTAX_OID "1.3.6.1.4.1.1466.115.121.1.8"
#define CERTIFICATELIST_SYNTAX_OID "1.3.6.1.4.1.1466.115.121.1.9"
#define CERTIFICATEPAIR_SYNTAX_OID "1.3.6.1.4.1.1466.115.121.1.10"
#define SUPPORTEDALGORITHM_SYNTAX_OID "1.3.6.1.4.1.1466.115.121.1.49"

static int bin_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
			Slapi_Value **bvals, int ftype, Slapi_Value **retVal );
static int bin_values2keys( Slapi_PBlock *pb, Slapi_Value **bvals,
	Slapi_Value ***ivals, int ftype );
static int bin_assertion2keys_ava( Slapi_PBlock *pb, Slapi_Value *bval,
	Slapi_Value ***ivals, int ftype );
static int bin_compare(struct berval *v1, struct berval *v2);

/*
 * Attribute syntaxes. We treat all of these the same since the
 * LDAP-specific encoding for all of them are simply strings of octets
 * with no real content restrictions (even though the content is supposed
 * to represent something specific).  For this reason, we do no
 * validation of the values for these syntaxes.
 */
static char *bin_names[] = { "Binary", "bin", BINARY_SYNTAX_OID, 0 };

static char *octetstring_names[] = { "OctetString", OCTETSTRING_SYNTAX_OID, 0 };

static char *jpeg_names[] = { "JPEG", JPEG_SYNTAX_OID, 0 };

static char *fax_names[] = { "FAX", FAX_SYNTAX_OID, 0 };


/* This syntax has "gone away" in RFC 4517, however we still use it for
 * a number of attributes in our default schema.  We should try to eliminate
 * it's use and remove support for it. */
static Slapi_PluginDesc bin_pdesc = {
	"bin-syntax", VENDOR, DS_PACKAGE_VERSION,
	"binary attribute syntax plugin"
};

static Slapi_PluginDesc octetstring_pdesc = {
	"octetstring-syntax", VENDOR, DS_PACKAGE_VERSION,
	"octet string attribute syntax plugin"
};

static Slapi_PluginDesc jpeg_pdesc = {
	"jpeg-syntax", VENDOR, DS_PACKAGE_VERSION,
	"JPEG attribute syntax plugin"
};

static Slapi_PluginDesc fax_pdesc = {
	"fax-syntax", VENDOR, DS_PACKAGE_VERSION,
	"Fax attribute syntax plugin"
};

static const char *octetStringMatch_names[] = {"octetStringMatch", "2.5.13.17", NULL};
static const char *octetStringOrderingMatch_names[] = {"octetStringOrderingMatch", "2.5.13.18", NULL};

static char *octetStringCompat_syntaxes[] = {BINARY_SYNTAX_OID, JPEG_SYNTAX_OID, FAX_SYNTAX_OID, CERTIFICATE_SYNTAX_OID, CERTIFICATELIST_SYNTAX_OID, CERTIFICATEPAIR_SYNTAX_OID, SUPPORTEDALGORITHM_SYNTAX_OID, NULL};

static struct mr_plugin_def mr_plugin_table[] = {
{{"2.5.13.17", NULL, "octetStringMatch", "The octetStringMatch rule compares an assertion value of the Octet "
"String syntax to an attribute value of a syntax (e.g., the Octet "
"String or JPEG syntax) whose corresponding ASN.1 type is the OCTET "
"STRING ASN.1 type.  "
"The rule evaluates to TRUE if and only if the attribute value and the "
"assertion value are the same length and corresponding octets (by "
"position) are the same.", OCTETSTRING_SYNTAX_OID, 0, octetStringCompat_syntaxes}, /* matching rule desc */
 {"octetStringMatch-mr", VENDOR, DS_PACKAGE_VERSION, "octetStringMatch matching rule plugin"}, /* plugin desc */
   octetStringMatch_names, /* matching rule name/oid/aliases */
   NULL, NULL, bin_filter_ava, NULL, bin_values2keys,
   bin_assertion2keys_ava, NULL, bin_compare},
{{"2.5.13.18", NULL, "octetStringOrderingMatch", "The octetStringOrderingMatch rule compares an assertion value of the "
"Octet String syntax to an attribute value of a syntax (e.g., the "
"Octet String or JPEG syntax) whose corresponding ASN.1 type is the "
"OCTET STRING ASN.1 type.  "
"The rule evaluates to TRUE if and only if the attribute value appears "
"earlier in the collation order than the assertion value.  The rule "
"compares octet strings from the first octet to the last octet, and "
"from the most significant bit to the least significant bit within the "
"octet.  The first occurrence of a different bit determines the "
"ordering of the strings.  A zero bit precedes a one bit.  If the "
"strings contain different numbers of octets but the longer string is "
"identical to the shorter string up to the length of the shorter "
"string, then the shorter string precedes the longer string.",
OCTETSTRING_SYNTAX_OID, 0, octetStringCompat_syntaxes}, /* matching rule desc */
 {"octetStringOrderingMatch-mr", VENDOR, DS_PACKAGE_VERSION, "octetStringOrderingMatch matching rule plugin"}, /* plugin desc */
 octetStringOrderingMatch_names, /* matching rule name/oid/aliases */
 NULL, NULL, bin_filter_ava, NULL, bin_values2keys,
 bin_assertion2keys_ava, NULL, bin_compare}
};
/*
certificateExactMatch
certificateListExactMatch
certificatePairExactMatch
algorithmIdentifierMatch
certificateMatch
certificatePairMatch
certificateListMatch
*/

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
	rc |= register_matching_rule_plugins();
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


int
fax_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> fax_init\n", 0, 0, 0 );
	rc = register_bin_like_plugin( pb, &fax_pdesc, fax_names,
			FAX_SYNTAX_OID );
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= fax_init %d\n", rc, 0, 0 );
	return( rc );
}


static int
bin_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
    Slapi_Value **bvals, int ftype, Slapi_Value **retVal )
{
	int	i;

	for ( i = 0; bvals[i] != NULL; i++ ) {
        const struct berval *bv = slapi_value_get_berval(bvals[i]);

        if ( ( bv->bv_len == bvfilter->bv_len ) &&
             ( 0 == memcmp( bv->bv_val, bvfilter->bv_val, bvfilter->bv_len ) ) )
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

	if (NULL == ivals) {
		return 1;
	}
	*ivals = NULL;
	if (NULL == bvals) {
		return 1;
	}

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

#define BV_EMPTY(bv) ((!bv || !bv->bv_len || !bv->bv_val))

static int
bin_compare(    
	struct berval	*v1,
    struct berval	*v2
)
{
    int rc = 0;

    if (BV_EMPTY(v1) && BV_EMPTY(v2)) {
        rc = 0; /* empty == empty */
    } else if (BV_EMPTY(v1) && !BV_EMPTY(v2)) {
        rc = 1; /* something in v2 always greater than empty v1 */
    } else if (!BV_EMPTY(v1) && BV_EMPTY(v2)) {
        rc = -1; /* something in v1 always greater than empty v2 */
    } else { /* both have actual data */
        rc = slapi_berval_cmp(v1, v2);
    }

    return rc;
}

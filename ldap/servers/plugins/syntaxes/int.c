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

/* int.c - integer syntax routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

static int int_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
		Slapi_Value **bvals, int ftype, Slapi_Value **retVal );
static int int_values2keys( Slapi_PBlock *pb, Slapi_Value **val,
		Slapi_Value ***ivals );
static int int_assertion2keys( Slapi_PBlock *pb, Slapi_Value *val,
		Slapi_Value ***ivals, int ftype );
static int int_compare(struct berval	*v1, struct berval	*v2);
static long int_to_canonical( long num );

/* the first name is the official one from RFC 2252 */
static char *names[] = { "INTEGER", "int", INTEGER_SYNTAX_OID, 0 };

static Slapi_PluginDesc pdesc = { "int-syntax", PLUGIN_MAGIC_VENDOR_STR,
	PRODUCTTEXT, "integer attribute syntax plugin" };

int
int_init( Slapi_PBlock *pb )
{
	int	rc, flags;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> int_init\n", 0, 0, 0 );

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
	    (void *) int_filter_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
	    (void *) int_values2keys );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
	    (void *) int_assertion2keys );
	flags = SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING;
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FLAGS,
	    (void *) &flags );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_NAMES,
	    (void *) names );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_OID,
	    (void *) INTEGER_SYNTAX_OID );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_COMPARE,
	    (void *) int_compare );

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= int_init %d\n", rc, 0, 0 );
	return( rc );
}

static int
int_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
    Slapi_Value **bvals, int ftype, Slapi_Value **retVal )
{
        int     i, rc;
	long	flong, elong;

	if ( ftype == LDAP_FILTER_APPROX ) {
		return( LDAP_PROTOCOL_ERROR );
	}
	if(retVal) {
		*retVal=NULL;
	}
	flong = atol( bvfilter->bv_val );
        for ( i = 0; bvals[i] != NULL; i++ ) {
		elong = atol ( slapi_value_get_string(bvals[i]) );
		rc = elong - flong;
                switch ( ftype ) {
                case LDAP_FILTER_GE:
                        if ( rc >= 0 ) {
							    if(retVal) {
									*retVal = bvals[i];
								}
                                return( 0 );
                        }
                        break;
                case LDAP_FILTER_LE:
                        if ( rc <= 0 ) {
							    if(retVal) {
									*retVal = bvals[i];
								}
                                return( 0 );
                        }
                        break;
                case LDAP_FILTER_EQUALITY:
                        if ( rc == 0 ) {
							    if(retVal) {
									*retVal = bvals[i];
								}
                                return( 0 );
                        }
                        break;
                }
        }

        return( -1 );
}

static int
int_values2keys( Slapi_PBlock *pb, Slapi_Value **vals, Slapi_Value ***ivals )
{
	long		num;
	int		i;

	for ( i = 0; vals[i] != NULL; i++ ) {
		/* NULL */
	}

	*ivals = (Slapi_Value **) slapi_ch_malloc(( i + 1 ) * sizeof(Slapi_Value *) );

	for ( i = 0; vals[i] != NULL; i++ )
	{
		num = atol( slapi_value_get_string(vals[i]) );
		num = int_to_canonical( num );
		(*ivals)[i] = slapi_value_new();
		slapi_value_set((*ivals)[i],&num,sizeof(long));
	}
	(*ivals)[i] = NULL;

	return( 0 );
}

static int
int_assertion2keys( Slapi_PBlock *pb, Slapi_Value *val, Slapi_Value ***ivals, int ftype )
{
	long num;
    size_t len;
    unsigned char *b;
    Slapi_Value *tmpval=NULL;

	num = atol( slapi_value_get_string(val) );
	num = int_to_canonical( num );
    /* similar to string.c to optimize equality path: avoid malloc/free */
    if(ftype == LDAP_FILTER_EQUALITY_FAST) {
        len=sizeof(long);
        tmpval=(*ivals)[0];
        if ( len > tmpval->bv.bv_len) {
            tmpval->bv.bv_val=(char *)slapi_ch_malloc(len);
        }
        tmpval->bv.bv_len=len;
        b = (unsigned char *)&num;
        memcpy(tmpval->bv.bv_val,b,len);
    } else {
        *ivals = (Slapi_Value **) slapi_ch_malloc( 2 * sizeof(Slapi_Value *) );
        (*ivals)[0] = (Slapi_Value *) slapi_ch_malloc( sizeof(Slapi_Value) );
        /* XXXSD initialize memory */
        memset((*ivals)[0],0,sizeof(Slapi_Value));	
        slapi_value_set((*ivals)[0],&num,sizeof(long));
        (*ivals)[1] = NULL;
    }
	return( 0 );
}

static int int_compare(    
	struct berval	*v1,
    struct berval	*v2
)
{
	long value1 = atol(v1->bv_val);
	long value2 = atol(v2->bv_val);

	if (value1 == value2) {
		return 0;
	}
	return ( ((value1 - value2) > 0) ? 1 : -1); 
}

static long
int_to_canonical( long num )
{
	long ret = 0L;
	unsigned char *b = (unsigned char *)&ret;

	b[0] = (unsigned char)(num >> 24);
	b[1] = (unsigned char)(num >> 16);
	b[2] = (unsigned char)(num >> 8);
	b[3] = (unsigned char)num;

	return ret;
}

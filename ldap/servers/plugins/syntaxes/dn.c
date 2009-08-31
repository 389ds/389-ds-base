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
static int dn_validate( struct berval *val );

/* the first name is the official one from RFC 2252 */
static char *names[] = { "DN", DN_SYNTAX_OID, 0 };

static Slapi_PluginDesc pdesc = { "dn-syntax", VENDOR,
	PACKAGE_VERSION, "distinguished name attribute syntax plugin" };

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
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALIDATE,
	    (void *) dn_validate );

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

static int dn_validate( struct berval *val )
{
	int rc = 0; /* Assume value is valid */

	/* A 0 length value is valid for the DN syntax. */
	if (val == NULL) {
		rc = 1;
	} else if (val->bv_len > 0) {
		rc = distinguishedname_validate(val->bv_val, &(val->bv_val[val->bv_len - 1]));
	}

	return rc;
}


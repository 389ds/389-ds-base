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

/* ava.c - routines for dealing with attribute value assertions */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include "slap.h"

int
get_ava(
    BerElement	*ber,
    struct ava	*ava
)
{
	char	*type = NULL;

	if ( ber_scanf( ber, "{ao}", &type, &ava->ava_value )
	    == LBER_ERROR ) {
        slapi_ch_free_string( &type );
        ava_done(ava);
		LDAPDebug( LDAP_DEBUG_ANY, "  get_ava ber_scanf\n", 0, 0, 0 );
		return( LDAP_PROTOCOL_ERROR );
	}
	ava->ava_type = slapi_attr_syntax_normalize(type);
	slapi_ch_free_string( &type );
    ava->ava_private = NULL;

	return( 0 );
}

void
ava_done(
    struct ava *ava
)
{
	slapi_ch_free( (void**)&(ava->ava_type) );
	slapi_ch_free( (void**)&(ava->ava_value.bv_val) );
}

int
rdn2ava(
    char	*rdn,
    struct ava	*ava
)
{
	char	*s;

	if ( (s = strchr( rdn, '=' )) == NULL ) {
		return( -1 );
	}
	*s++ = '\0';

	ava->ava_type = rdn;
	strcpy_unescape_value( s, s );
	ava->ava_value.bv_val = s;
	ava->ava_value.bv_len = strlen( s );
	ava->ava_private = NULL;

	return( 0 );
}

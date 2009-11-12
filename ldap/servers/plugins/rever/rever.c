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

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "rever.h"

static Slapi_PluginDesc pdesc = { "des-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "DES storage scheme plugin" };

static char *plugin_name = "ReverStoragePlugin";

int
des_cmp( char *userpwd, char *dbpwd )
{
	char *cipher = NULL;

	if ( encode(userpwd, &cipher) != 0 )
		return 1;
	else
		return( strcmp(cipher, dbpwd) );
}

char *
des_enc( char *pwd )
{
	char *cipher = NULL;
	
	if ( encode(pwd, &cipher) != 0 )
		return(NULL);
	else
		return( cipher );
}

char *
des_dec( char *pwd )
{
	char *plain = NULL;
	
	if ( decode(pwd, &plain) != 0 )
		return(NULL);
	else
		return( plain );
}

int
des_init( Slapi_PBlock *pb )
{
	int	rc;
	char *name;

	slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "=> des_init\n" );

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
	    (void *) des_enc);
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
	    (void *) des_cmp );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_DEC_FN,
	    (void *) des_dec );
	name = slapi_ch_strdup(REVER_SCHEME_NAME);
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
	    name );

	init_des_plugin();

	slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "<= des_init %d\n\n", rc );

	return( rc );
}

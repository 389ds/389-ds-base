/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "dirver.h"

#include "rever.h"

static Slapi_PluginDesc pdesc = { "des-storage-scheme", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT, "DES storage scheme plugin" };

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

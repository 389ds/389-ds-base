/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "pwdstorage.h"
#include "dirver.h"

static Slapi_PluginDesc sha_pdesc = { "sha-password-storage-scheme", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT, "Secure Hashing Algorithm (SHA)" };

static Slapi_PluginDesc ssha_pdesc = { "ssha-password-storage-scheme", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT, "Salted Secure Hashing Algorithm (SSHA)" };

static Slapi_PluginDesc crypt_pdesc = { "crypt-password-storage-scheme", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT, "Unix crypt algorithm (CRYPT)" };

static Slapi_PluginDesc clear_pdesc = { "clear-password-storage-scheme", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT, "No encryption (CLEAR)" };

static Slapi_PluginDesc ns_mta_md5_pdesc = { "NS-MTA-MD5-password-storage-scheme", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT, "Netscape MD5 (NS-MTA-MD5)" };

static char *plugin_name = "NSPwdStoragePlugin";

int
sha_pwd_storage_scheme_init( Slapi_PBlock *pb )
{
	int	rc;
	char *name;

	slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "=> sha_pwd_storage_scheme_init\n" );

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&sha_pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
	    (void *) sha1_pw_enc);
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
	    (void *) sha1_pw_cmp );
	name = slapi_ch_strdup("SHA");
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
	    name );

	slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "<= sha_pwd_storage_scheme_init %d\n\n", rc );

	return( rc );
}

int
ssha_pwd_storage_scheme_init( Slapi_PBlock *pb )
{
	int	rc;
	char *name;

	slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "=> ssha_pwd_storage_scheme_init\n" );

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&ssha_pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
	    (void *) salted_sha1_pw_enc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
	    (void *) sha1_pw_cmp );
	name = slapi_ch_strdup("SSHA");
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
	    name );

	slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "<= ssha_pwd_storage_scheme_init %d\n\n", rc );
	return( rc );
}

int
crypt_pwd_storage_scheme_init( Slapi_PBlock *pb )
{
	int	rc;
	char *name;

	slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "=> crypt_pwd_storage_scheme_init\n" );

	crypt_init();
	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&crypt_pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
	    (void *) crypt_pw_enc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
	    (void *) crypt_pw_cmp );
	name = slapi_ch_strdup("CRYPT");
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
	    name );

	slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "<= crypt_pwd_storage_scheme_init %d\n\n", rc );
	return( rc );
}

int
clear_pwd_storage_scheme_init( Slapi_PBlock *pb )
{
	int	rc;
	char *name;

	slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "=> clear_pwd_storage_scheme_init\n" );

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&clear_pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
	    (void *) clear_pw_enc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
	    (void *) clear_pw_cmp );
	name = slapi_ch_strdup("CLEAR");
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
	    name );

	slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "<= clear_pwd_storage_scheme_init %d\n\n", rc );
	return( rc );
}

int
ns_mta_md5_pwd_storage_scheme_init( Slapi_PBlock *pb )
{
	int	rc;
	char *name;

	slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "=> ns_mta_md5_pwd_storage_scheme_init\n" );

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&ns_mta_md5_pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
	    (void *) NULL );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
	    (void *) ns_mta_md5_pw_cmp );
	name = slapi_ch_strdup("NS-MTA-MD5");
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
	    name );

	slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "<= ns_mta_md5_pwd_storage_scheme_init %d\n\n", rc );
	return( rc );
}

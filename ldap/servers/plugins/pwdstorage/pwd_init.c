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

#include "pwdstorage.h"

static Slapi_PluginDesc sha_pdesc = { "sha-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Secure Hashing Algorithm (SHA)" };

static Slapi_PluginDesc ssha_pdesc = { "ssha-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Salted Secure Hashing Algorithm (SSHA)" };

static Slapi_PluginDesc sha256_pdesc = { "sha256-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Secure Hashing Algorithm (SHA256)" };

static Slapi_PluginDesc ssha256_pdesc = { "ssha256-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Salted Secure Hashing Algorithm (SSHA256)" };

static Slapi_PluginDesc sha384_pdesc = { "sha384-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Secure Hashing Algorithm (SHA384)" };

static Slapi_PluginDesc ssha384_pdesc = { "ssha384-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Salted Secure Hashing Algorithm (SSHA384)" };

static Slapi_PluginDesc sha512_pdesc = { "sha512-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Secure Hashing Algorithm (SHA512)" };

static Slapi_PluginDesc ssha512_pdesc = { "ssha512-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Salted Secure Hashing Algorithm (SSHA512)" };

#ifndef _WIN32
static Slapi_PluginDesc crypt_pdesc = { "crypt-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Unix crypt algorithm (CRYPT)" };
#endif

static Slapi_PluginDesc clear_pdesc = { "clear-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "No encryption (CLEAR)" };

static Slapi_PluginDesc ns_mta_md5_pdesc = { "NS-MTA-MD5-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Netscape MD5 (NS-MTA-MD5)" };

static Slapi_PluginDesc md5_pdesc = { "md5-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "MD5 hash algorithm (MD5)" };

static Slapi_PluginDesc smd5_pdesc = { "smd5-password-storage-scheme", VENDOR, DS_PACKAGE_VERSION, "Salted MD5 hash algorithm (SMD5)" };

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
sha256_pwd_storage_scheme_init( Slapi_PBlock *pb )
{
        int     rc;
        char *name;

        slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "=> sha256_pwd_storage_scheme_init\n" );

        rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
            (void *) SLAPI_PLUGIN_VERSION_01 );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
            (void *)&sha256_pdesc );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
            (void *) sha256_pw_enc);
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
            (void *) sha256_pw_cmp );
        name = slapi_ch_strdup("SHA256");
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
            name );

        slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "<= sha256_pwd_storage_scheme_init %d\n\n", rc );

        return( rc );
}

int
ssha256_pwd_storage_scheme_init( Slapi_PBlock *pb )
{
        int     rc;
        char *name;

        slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "=> ssha256_pwd_storage_scheme_init\n" );

        rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
            (void *) SLAPI_PLUGIN_VERSION_01 );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
            (void *)&ssha256_pdesc );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
            (void *) salted_sha256_pw_enc );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
            (void *) sha256_pw_cmp );
        name = slapi_ch_strdup("SSHA256");
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
            name );

        slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "<= ssha256_pwd_storage_scheme_init %d\n\n", rc );
        return( rc );
}

int
sha384_pwd_storage_scheme_init( Slapi_PBlock *pb )
{
        int     rc;
        char *name;

        slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "=> sha384_pwd_storage_scheme_init\n" );

        rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
            (void *) SLAPI_PLUGIN_VERSION_01 );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
            (void *)&sha384_pdesc );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
            (void *) sha384_pw_enc);
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
            (void *) sha384_pw_cmp );
        name = slapi_ch_strdup("SHA384");
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
            name );

        slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "<= sha384_pwd_storage_scheme_init %d\n\n", rc );

        return( rc );
}

int
ssha384_pwd_storage_scheme_init( Slapi_PBlock *pb )
{
        int     rc;
        char *name;

        slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "=> ssha384_pwd_storage_scheme_init\n" );

        rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
            (void *) SLAPI_PLUGIN_VERSION_01 );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
            (void *)&ssha384_pdesc );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
            (void *) salted_sha384_pw_enc );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
            (void *) sha384_pw_cmp );
        name = slapi_ch_strdup("SSHA384");
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
            name );

        slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "<= ssha384_pwd_storage_scheme_init %d\n\n", rc );
        return( rc );
}

int
sha512_pwd_storage_scheme_init( Slapi_PBlock *pb )
{
        int     rc;
        char *name;

        slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "=> sha512_pwd_storage_scheme_init\n" );

        rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
            (void *) SLAPI_PLUGIN_VERSION_01 );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
            (void *)&sha512_pdesc );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
            (void *) sha512_pw_enc);
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
            (void *) sha512_pw_cmp );
        name = slapi_ch_strdup("SHA512");
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
            name );

        slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "<= sha512_pwd_storage_scheme_init %d\n\n", rc );

        return( rc );
}

int
ssha512_pwd_storage_scheme_init( Slapi_PBlock *pb )
{
        int     rc;
        char *name;

        slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "=> ssha512_pwd_storage_scheme_init\n" );

        rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
            (void *) SLAPI_PLUGIN_VERSION_01 );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
            (void *)&ssha512_pdesc );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
            (void *) salted_sha512_pw_enc );
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
            (void *) sha512_pw_cmp );
        name = slapi_ch_strdup("SSHA512");
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
            name );

        slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "<= ssha512_pwd_storage_scheme_init %d\n\n", rc );
        return( rc );
}

#ifndef _WIN32
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
#endif

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

int
md5_pwd_storage_scheme_init( Slapi_PBlock *pb )
{
	int     rc;
	char *name;

	slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "=> md5_pwd_storage_scheme_init\n" );

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
						   (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
							(void *)&md5_pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
							(void *) md5_pw_enc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
							(void *) md5_pw_cmp );
	name = slapi_ch_strdup("MD5");
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
							name );

	slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "<= md5_pwd_storage_scheme_init %d\n\n", rc );
	return( rc );
}

int
smd5_pwd_storage_scheme_init( Slapi_PBlock *pb )
{
	int     rc;
	char *name;

	slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "=> smd5_pwd_storage_scheme_init\n" );

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
							(void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
							(void *)&smd5_pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN,
							(void *) smd5_pw_enc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN,
							(void *) smd5_pw_cmp );
	name = slapi_ch_strdup("SMD5");
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME,
							name );

	slapi_log_error( SLAPI_LOG_PLUGIN, plugin_name, "<= smd5_pwd_storage_scheme_init %d\n\n", rc );
	return( rc );
}

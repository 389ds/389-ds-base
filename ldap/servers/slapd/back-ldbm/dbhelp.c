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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* 
 * File for helper functions related to BerkeleyDB.
 * This exists because dblayer.c is 5k+ lines long, 
 * so it seems time to move code to a new file.
 */

#include "back-ldbm.h"
#include "dblayer.h"

static int dblayer_copy_file_keybykey(DB_ENV *env, char *source_file_name, char *destination_file_name, int overwrite, dblayer_private *priv)
{
	int retval = 0;
	int retval_cleanup = 0;
	DB *source_file = NULL;
	DB *destination_file = NULL;
	DBC *source_cursor = NULL;
	int dbtype = 0;
	int dbflags = 0;
	int dbpagesize = 0;
	int cursor_flag = 0;
	int finished = 0;
	int mode = 0;

	if (priv->dblayer_file_mode)
		mode = priv->dblayer_file_mode;
	dblayer_set_env_debugging(env, priv);

	LDAPDebug( LDAP_DEBUG_TRACE, "=> dblayer_copy_file_keybykey\n", 0, 0, 0 );
	/* Open the source file */
	retval = db_create(&source_file, env, 0);
	if (retval) {
		LDAPDebug(LDAP_DEBUG_ANY, "dblayer_copy_file_keybykey, Create error %d: %s\n", retval, db_strerror(retval), 0);
		goto error;
	}
	retval = source_file->open(source_file, NULL, source_file_name, NULL, DB_UNKNOWN, DB_RDONLY, 0);
	if (retval) {
		LDAPDebug(LDAP_DEBUG_ANY, "dblayer_copy_file_keybykey, Open error %d: %s\n", retval, db_strerror(retval), 0);
		goto error;
	}
	/* Get the info we need from the source file */
	retval = source_file->get_flags(source_file, &dbflags);
	if (retval) {
		LDAPDebug(LDAP_DEBUG_ANY, "dblayer_copy_file_keybykey, get_flags error %d: %s\n", retval, db_strerror(retval), 0);
		goto error;
	}
	retval = source_file->get_type(source_file, &dbtype);
	if (retval) {
		LDAPDebug(LDAP_DEBUG_ANY, "dblayer_copy_file_keybykey, get_type error %d: %s\n", retval, db_strerror(retval), 0);
		goto error;
	}
	retval = source_file->get_pagesize(source_file, &dbpagesize);
	if (retval) {
		LDAPDebug(LDAP_DEBUG_ANY, "dblayer_copy_file_keybykey, get_pagesize error %d: %s\n", retval, db_strerror(retval), 0);
		goto error;
	}
	/* Open the destination file
	 * and make sure that it has the correct page size, the correct access method, and the correct flags (dup etc)
	 */
	retval = db_create(&destination_file, env, 0);
	if (retval) {
		LDAPDebug(LDAP_DEBUG_ANY, "dblayer_copy_file_keybykey, Create error %d: %s\n", retval, db_strerror(retval), 0);
		goto error;
	}
	retval = destination_file->set_flags(destination_file,dbflags);
	if (retval) {
		LDAPDebug(LDAP_DEBUG_ANY, "dblayer_copy_file_keybykey, set_flags error %d: %s\n", retval, db_strerror(retval), 0);
		goto error;
	}
	retval = destination_file->set_pagesize(destination_file,dbpagesize);
	if (retval) {
		LDAPDebug(LDAP_DEBUG_ANY, "dblayer_copy_file_keybykey, set_pagesize error %d: %s\n", retval, db_strerror(retval), 0);
		goto error;
	}
	retval = destination_file->open(destination_file, NULL, destination_file_name, NULL, dbtype, DB_CREATE | DB_EXCL, mode);
	if (retval) {
		LDAPDebug(LDAP_DEBUG_ANY, "dblayer_copy_file_keybykey, Open error %d: %s\n", retval, db_strerror(retval), 0);
		goto error;
	}
	/* Open a cursor on the source file */
	retval = source_file->cursor(source_file,NULL,&source_cursor,0);
	if (retval) {
		LDAPDebug(LDAP_DEBUG_ANY, "dblayer_copy_file_keybykey, Create cursor error %d: %s\n", retval, db_strerror(retval), 0);
		goto error;
	}
	/* Seek to the first key */
	cursor_flag = DB_FIRST;
	/* Loop seeking to the next key until they're all done */
	while (!finished) {
		DBT key = {0};
		DBT data = {0};
		retval = source_cursor->c_get(source_cursor, &key, &data, cursor_flag);
		if (retval) {
			/* DB_NOTFOUND is expected when we find the end, log a message for any other error.
			 * In either case, set finished=1 so we can hop down and close the cursor. */
			if ( DB_NOTFOUND != retval )
			{
				LDAPDebug(LDAP_DEBUG_ANY, "dblayer_copy_file_keybykey, c_get error %d: %s\n", retval, db_strerror(retval), 0);
				goto error;
			}
			retval = 0; /* DB_NOTFOUND was OK... */
			finished = 1;
		} else {
			/* For each key, insert into the destination file */
			retval = destination_file->put(destination_file, NULL, &key, &data, 0);
			if (retval) {
				LDAPDebug(LDAP_DEBUG_ANY, "dblayer_copy_file_keybykey, put error %d: %s\n", retval, db_strerror(retval), 0);
				goto error;
			}
			cursor_flag = DB_NEXT;
		}
	}

error:
	/* Close the cursor */
	if (source_cursor) {
		retval_cleanup = source_cursor->c_close(source_cursor);
		if (retval_cleanup) {
			LDAPDebug(LDAP_DEBUG_ANY, "dblayer_copy_file_keybykey, Close cursor error %d: %s\n", retval_cleanup, db_strerror(retval_cleanup), 0);
			retval += retval_cleanup;
		}
	}
	/* Close the source file */
	if (source_file) {
		retval_cleanup = source_file->close(source_file,0);
		source_file = NULL;
		if (retval_cleanup) {
			LDAPDebug(LDAP_DEBUG_ANY, "dblayer_copy_file_keybykey, Close error %d: %s\n", retval_cleanup, db_strerror(retval_cleanup), 0); 
			retval += retval_cleanup;
		}
	}
	/* Close the destination file */
	if (destination_file) {
		retval_cleanup = destination_file->close(destination_file,0);
		destination_file = NULL;
		if (retval_cleanup) {
			LDAPDebug(LDAP_DEBUG_ANY, "dblayer_copy_file_keybykey, Close error %d: %s\n", retval_cleanup, db_strerror(retval_cleanup), 0);
			retval += retval_cleanup;
		}
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<= dblayer_copy_file_keybykey\n", 0, 0, 0 );
	return retval;
}

int dblayer_copy_file_resetlsns(char *home_dir ,char *source_file_name, char *destination_file_name, int overwrite, dblayer_private *priv)
{
	int retval = 0;
	int mode = 0;
	DB_ENV *env = NULL;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> dblayer_copy_file_resetlsns\n", 0, 0, 0 );
	/* Make the environment */

	if (priv->dblayer_file_mode)
		mode = priv->dblayer_file_mode;
	retval = dblayer_make_private_simple_env(home_dir,&env);
	if (retval) {
		LDAPDebug(LDAP_DEBUG_ANY, "dblayer_copy_file_resetlsns: Call to dblayer_make_private_simple_env failed!\n" 
			"Unable to open an environment.", 0, 0, 0);
	}
	/* Do the copy */
	retval = dblayer_copy_file_keybykey(env, source_file_name, destination_file_name, overwrite, priv);
	if (retval) {
		LDAPDebug(LDAP_DEBUG_ANY, "dblayer_copy_file_resetlsns: Copy not completed successfully.", 0, 0, 0);
	}
	/* Close the environment */
	if (env) {
		int retval2 = 0;
		retval2 = env->close(env,0);
		if (retval2) {
			if (0 == retval) {
				retval = retval2;
				LDAPDebug(LDAP_DEBUG_ANY, "dblayer_copy_file_resetlsns, error %d: %s\n", retval, db_strerror(retval), 0);
			}
		}
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<= dblayer_copy_file_resetlsns\n", 0, 0, 0 );
	return retval;
}

void dblayer_set_env_debugging(DB_ENV *pEnv, dblayer_private *priv)
{
	pEnv->set_errpfx(pEnv, "ns-slapd");
    if (priv->dblayer_verbose) {
        pEnv->set_verbose(pEnv, DB_VERB_CHKPOINT, 1);    /* 1 means on */
        pEnv->set_verbose(pEnv, DB_VERB_DEADLOCK, 1);    /* 1 means on */
        pEnv->set_verbose(pEnv, DB_VERB_RECOVERY, 1);    /* 1 means on */
        pEnv->set_verbose(pEnv, DB_VERB_WAITSFOR, 1);    /* 1 means on */
    }
    if (priv->dblayer_debug) {
        pEnv->set_errcall(pEnv, dblayer_log_print);
    }

}

/* Make an environment to be used for isolated recovery (e.g. during a partial restore operation) */
int dblayer_make_private_recovery_env(char *db_home_dir, dblayer_private *priv, DB_ENV **env)
{
	int retval = 0;
	DB_ENV *ret_env = NULL;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> dblayer_make_private_recovery_env\n", 0, 0, 0 );
	if (NULL == env) {
		LDAPDebug(LDAP_DEBUG_ANY, "dblayer_make_private_recovery_env: Null environment.  Cannot continue.", 0, 0, 0);
		return -1;
	}
	*env = NULL;

	retval = db_env_create(&ret_env,0);
	if (retval) {
		LDAPDebug(LDAP_DEBUG_ANY, "dblayer_make_private_recovery_env, Create error %d: %s\n", retval, db_strerror(retval), 0);
		goto error;
	}
	dblayer_set_env_debugging(ret_env, priv);

	retval = ret_env->open(ret_env,db_home_dir, DB_INIT_TXN | DB_RECOVER_FATAL | DB_CREATE | DB_INIT_MPOOL | DB_PRIVATE,0);
	if (0 == retval) {
		*env = ret_env;
	} else {
		LDAPDebug(LDAP_DEBUG_ANY, "dblayer_make_private_recovery_env, Open error %d: %s\n", retval, db_strerror(retval), 0);
		goto error;
	}

error:
	LDAPDebug( LDAP_DEBUG_TRACE, "<= dblayer_make_private_recovery_env\n", 0, 0, 0 );
	return retval;
}

/* Make an environment to be used for simple non-transacted database operations, e.g. fixup during upgrade */
int dblayer_make_private_simple_env(char *db_home_dir, DB_ENV **env)
{
	int retval = 0;
	DB_ENV *ret_env = NULL;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> dblayer_make_private_simple_env\n", 0, 0, 0 );
	if (NULL == env) {
		LDAPDebug(LDAP_DEBUG_ANY, "dblayer_make_private_simple_env: Null environment.  Cannot continue.", 0, 0, 0);
		return -1;
	}
	*env = NULL;

	retval = db_env_create(&ret_env,0);
	if (retval) {
		LDAPDebug(LDAP_DEBUG_ANY, "dblayer_make_private_simple_env, error %d: %s\n", retval, db_strerror(retval), 0);
		goto error;
	}

	retval = ret_env->open(ret_env,db_home_dir,DB_CREATE | DB_INIT_MPOOL | DB_PRIVATE,0);
	if (0 == retval) {
		*env = ret_env;
	} else {
		LDAPDebug(LDAP_DEBUG_ANY, "dblayer_make_private_simple_env, error %d: %s\n", retval, db_strerror(retval), 0);
		goto error;
	}

error:
	LDAPDebug( LDAP_DEBUG_TRACE, "<= dblayer_make_private_simple_env\n", 0, 0, 0 );
	return retval;
}

char* last_four_chars(const char* s)
{
    size_t l = strlen(s);
    return ((char*)s + (l - 4));
}

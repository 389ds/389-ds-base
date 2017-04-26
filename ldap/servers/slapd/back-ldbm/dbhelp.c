/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


/* 
 * File for helper functions related to BerkeleyDB.
 * This exists because dblayer.c is 5k+ lines long, 
 * so it seems time to move code to a new file.
 */

#include "back-ldbm.h"
#include "dblayer.h"

static int
dblayer_copy_file_keybykey(DB_ENV *env,
                           char *source_file_name,
                           char *destination_file_name,
                           int overwrite __attribute__((unused)),
                           dblayer_private *priv,
                           ldbm_instance *inst)
{
	int retval = 0;
	int retval_cleanup = 0;
	DB *source_file = NULL;
	DB *destination_file = NULL;
	DBC *source_cursor = NULL;
	DBTYPE dbtype = 0;
	PRUint32 dbflags = 0;
	PRUint32 dbpagesize = 0;
	int cursor_flag = 0;
	int finished = 0;
	int mode = 0;
	char *p = NULL;

	slapi_log_err(SLAPI_LOG_TRACE, "dblayer_copy_file_keybykey", "=>\n");

	if (!env) {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_keybykey", "Out of memory\n");
		goto error;
	}

	if (priv->dblayer_file_mode)
		mode = priv->dblayer_file_mode;
	dblayer_set_env_debugging(env, priv);

	/* Open the source file */
	retval = db_create(&source_file, env, 0);
	if (retval) {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_keybykey", "Create error %d: %s\n",
			retval, db_strerror(retval));
		goto error;
	}
	retval = (source_file->open)(source_file, NULL, source_file_name, NULL, DB_UNKNOWN, DB_RDONLY, 0);
	if (retval) {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_keybykey", "Open error %d: %s\n",
			retval, db_strerror(retval));
		goto error;
	}
	/* Get the info we need from the source file */
	retval = source_file->get_flags(source_file, &dbflags);
	if (retval) {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_keybykey", "get_flags error %d: %s\n",
			retval, db_strerror(retval));
		goto error;
	}
	retval = source_file->get_type(source_file, &dbtype);
	if (retval) {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_keybykey",
			"get_type error %d: %s\n", retval, db_strerror(retval));
		goto error;
	}
	retval = source_file->get_pagesize(source_file, &dbpagesize);
	if (retval) {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_keybykey",
			"get_pagesize error %d: %s\n", retval, db_strerror(retval));
		goto error;
	}
	/* Open the destination file
	 * and make sure that it has the correct page size, the correct access method, and the correct flags (dup etc)
	 */
	retval = db_create(&destination_file, env, 0);
	if (retval) {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_keybykey",
			"Create error %d: %s\n", retval, db_strerror(retval));
		goto error;
	}
	retval = destination_file->set_flags(destination_file,dbflags);
	if (retval) {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_keybykey",
			"Set_flags error %d: %s\n", retval, db_strerror(retval));
		goto error;
	}
	retval = destination_file->set_pagesize(destination_file,dbpagesize);
	if (retval) {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_keybykey",
			"set_pagesize error %d: %s\n", retval, db_strerror(retval));
		goto error;
	}

	/* TEL 20130412: Make sure to set the dup comparison function if needed.  
	 * We key our decision off of the presence of new IDL and dup flags on
	 * the source database.  This is similar dblayer_open_file, except that
	 * we don't have the attribute info index mask for VLV.  That should be OK
	 * since the DB_DUP and DB_DUPSORT flags wouldn't have been toggled on
	 * unless they passed the check on the source.
	 */
	/* Entryrdn index has its own dup compare function */
	if ((p = PL_strcasestr(source_file_name, LDBM_ENTRYRDN_STR)) &&
		(*(p + sizeof(LDBM_ENTRYRDN_STR) - 1) == '.')) {
		/* entryrdn.db */
		struct attrinfo *ai = NULL;
		if (NULL == inst) {
			slapi_log_err(SLAPI_LOG_ERR,
			               "dblayer_copy_file_keybykey", "(entryrdn) - "
			               "dup_cmp_fn cannot be retrieved since inst is NULL.\n");
			goto error;
		}
		ainfo_get(inst->inst_be, LDBM_ENTRYRDN_STR, &ai);
		if (ai->ai_dup_cmp_fn) {
			/* If set, use the special dup compare callback */
			retval = destination_file->set_dup_compare(destination_file, ai->ai_dup_cmp_fn);
			if (retval) {
				slapi_log_err(SLAPI_LOG_ERR,
				               "dblayer_copy_file_keybykey", "(entryrdn) - set_dup_compare error %d: %s\n",
				               retval, db_strerror(retval));
				goto error;
			}
		}
	} else if (idl_get_idl_new() && (dbflags & DB_DUP) && (dbflags & DB_DUPSORT)) {
		retval = destination_file->set_dup_compare(destination_file, idl_new_compare_dups); 
		if (retval) {
			slapi_log_err(SLAPI_LOG_ERR,
			               "dblayer_copy_file_keybykey", "set_dup_compare error %d: %s\n",
			               retval, db_strerror(retval));
			goto error;
		}
	}

	retval = (destination_file->open)(destination_file, NULL, destination_file_name, NULL, dbtype, DB_CREATE | DB_EXCL, mode);
	if (retval) {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_keybykey", "Open error %d: %s\n",
			retval, db_strerror(retval));
		goto error;
	}
	/* Open a cursor on the source file */
	retval = source_file->cursor(source_file,NULL,&source_cursor,0);
	if (retval) {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_keybykey",
			"Create cursor error %d: %s\n", retval, db_strerror(retval));
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
				slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_keybykey", "c_get error %d: %s\n",
					retval, db_strerror(retval));
				goto error;
			}
			retval = 0; /* DB_NOTFOUND was OK... */
			finished = 1;
		} else {
			/* For each key, insert into the destination file */
			retval = destination_file->put(destination_file, NULL, &key, &data, 0);
			if (retval) {
				slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_keybykey", "put error %d: %s\n",
						retval, db_strerror(retval));
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
			slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_keybykey",
				"Close cursor error %d: %s\n", retval_cleanup, db_strerror(retval_cleanup));
			retval += retval_cleanup;
		}
	}
	/* Close the source file */
	if (source_file) {
		retval_cleanup = source_file->close(source_file,0);
		source_file = NULL;
		if (retval_cleanup) {
			slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_keybykey",
				"Close error %d: %s\n", retval_cleanup, db_strerror(retval_cleanup)); 
			retval += retval_cleanup;
		}
	}
	/* Close the destination file */
	if (destination_file) {
		retval_cleanup = destination_file->close(destination_file,0);
		destination_file = NULL;
		if (retval_cleanup) {
			slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_keybykey",
				"Close error %d: %s\n", retval_cleanup, db_strerror(retval_cleanup));
			retval += retval_cleanup;
		}
	}

	slapi_log_err(SLAPI_LOG_TRACE, "dblayer_copy_file_keybykey", "<=\n");
	return retval;
}

int
dblayer_copy_file_resetlsns(char *home_dir,
                            char *source_file_name,
                            char *destination_file_name,
                            int overwrite,
                            dblayer_private *priv,
                            ldbm_instance *inst)
{
	int retval = 0;
	DB_ENV *env = NULL;

	slapi_log_err(SLAPI_LOG_TRACE, "dblayer_copy_file_resetlsns", "=>\n");
	/* Make the environment */

	retval = dblayer_make_private_simple_env(home_dir,&env);
	if (retval || !env) {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_resetlsns", "Call to dblayer_make_private_simple_env failed!\n" 
			"Unable to open an environment.");
		goto out;
	}
	/* Do the copy */
	retval = dblayer_copy_file_keybykey(env, source_file_name, destination_file_name, overwrite, priv, inst);
	if (retval) {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_resetlsns", "Copy did not complete successfully.");
	}
out:
	/* Close the environment */
	if (env) {
		int retval2 = 0;
		retval2 = env->close(env,0);
		if (retval2) {
			if (0 == retval) {
				retval = retval2;
				slapi_log_err(SLAPI_LOG_ERR, "dblayer_copy_file_resetlsns",
					"error %d: %s\n", retval, db_strerror(retval));
			}
		}
	}

	slapi_log_err(SLAPI_LOG_TRACE, "dblayer_copy_file_resetlsns", "<=\n");
	return retval;
}

void dblayer_set_env_debugging(DB_ENV *pEnv, dblayer_private *priv)
{
	pEnv->set_errpfx(pEnv, "ns-slapd");
    if (priv->dblayer_verbose) {
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

	slapi_log_err(SLAPI_LOG_TRACE, "dblayer_make_private_recovery_env", "=>\n");
	if (NULL == env) {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_make_private_recovery_env",
			"Null environment.  Cannot continue.");
		return -1;
	}
	*env = NULL;

	retval = db_env_create(&ret_env,0);
	if (retval) {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_make_private_recovery_env",
			"Create error %d: %s\n", retval, db_strerror(retval));
		goto error;
	}
	dblayer_set_env_debugging(ret_env, priv);

	retval = (ret_env->open)(ret_env,db_home_dir, DB_INIT_TXN | DB_RECOVER_FATAL | DB_CREATE | DB_INIT_MPOOL | DB_PRIVATE,0);
	if (0 == retval) {
		*env = ret_env;
	} else {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_make_private_recovery_env",
			"Open error %d: %s\n", retval, db_strerror(retval));
		goto error;
	}

error:
	slapi_log_err(SLAPI_LOG_TRACE, "dblayer_make_private_recovery_env", "<=\n");
	return retval;
}

/* Make an environment to be used for simple non-transacted database operations, e.g. fixup during upgrade */
int dblayer_make_private_simple_env(char *db_home_dir, DB_ENV **env)
{
	int retval = 0;
	DB_ENV *ret_env = NULL;

	slapi_log_err(SLAPI_LOG_TRACE, "dblayer_make_private_simple_env", "=>\n");
	if (NULL == env) {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_make_private_simple_env",
			"Null environment.  Cannot continue.");
		return -1;
	}
	*env = NULL;

	retval = db_env_create(&ret_env,0);
	if (retval) {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_make_private_simple_env",
			"Error %d: %s\n", retval, db_strerror(retval));
		goto error;
	}

	retval = (ret_env->open)(ret_env,db_home_dir,DB_CREATE | DB_INIT_MPOOL | DB_PRIVATE,0);
	if (0 == retval) {
		*env = ret_env;
	} else {
		slapi_log_err(SLAPI_LOG_ERR, "dblayer_make_private_simple_env",
			"Error %d: %s\n", retval, db_strerror(retval));
		goto error;
	}

error:
	slapi_log_err(SLAPI_LOG_TRACE, "dblayer_make_private_simple_env","<=\n");
	return retval;
}

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* This file handles configuration information that is specific
 * to ldbm instances.
 */

#include "mdb_layer.h"


/*------------------------------------------------------------------------
 * Get and set functions for mdb instance variables
 *----------------------------------------------------------------------*/

static void *
mdb_instance_config_instance_dir_get(void *arg)
{
#ifdef TODO
    ldbm_instance *inst = (ldbm_instance *)arg;

    if (inst->inst_dir_name == NULL)
        return slapi_ch_strdup("");
    else if (inst->inst_parent_dir_name) {
        int len = strlen(inst->inst_parent_dir_name) +
                  strlen(inst->inst_dir_name) + 2;
        char *full_inst_dir = (char *)slapi_ch_malloc(len);
        PR_snprintf(full_inst_dir, len, "%s%c%s",
                    inst->inst_parent_dir_name, get_sep(inst->inst_parent_dir_name),
                    inst->inst_dir_name);
        return full_inst_dir;
    } else
        return slapi_ch_strdup(inst->inst_dir_name);
#endif /* TODO */
}

static int
mdb_instance_config_instance_dir_set(void *arg,
                                      void *value,
                                      char *errorbuf __attribute__((unused)),
                                      int phase __attribute__((unused)),
                                      int apply)
{
#ifdef TODO
    ldbm_instance *inst = (ldbm_instance *)arg;

    if (!apply) {
        return LDAP_SUCCESS;
    }

    if ((value == NULL) || (strlen(value) == 0)) {
        inst->inst_dir_name = NULL;
        inst->inst_parent_dir_name = NULL;
    } else {
        char *dir = (char *)value;
        if (is_fullpath(dir)) {
            char sep = get_sep(dir);
            char *p = strrchr(dir, sep);
            if (NULL == p) /* should not happens, tho */
            {
                inst->inst_parent_dir_name = NULL;
                inst->inst_dir_name = rel2abspath(dir); /* normalize dir;
                                                           strdup'ed in
                                                           rel2abspath */
            } else {
                *p = '\0';
                inst->inst_parent_dir_name = rel2abspath(dir); /* normalize dir;
                                                                  strdup'ed in
                                                                  rel2abspath */
                inst->inst_dir_name = slapi_ch_strdup(p + 1);
                *p = sep;
            }
        } else {
            inst->inst_parent_dir_name = NULL;
            inst->inst_dir_name = slapi_ch_strdup(dir);
        }
    }
    return LDAP_SUCCESS;
#endif /* TODO */
}

/*------------------------------------------------------------------------
 * mdb instance configuration array
 *
 * BDB allows tp specify data directories for each instance database
 *----------------------------------------------------------------------*/
static config_info mdb_instance_config[] = {
    {CONFIG_INSTANCE_DIR, CONFIG_TYPE_STRING, NULL, &mdb_instance_config_instance_dir_get, &mdb_instance_config_instance_dir_set, CONFIG_FLAG_ALWAYS_SHOW},
    {NULL, 0, NULL, NULL, NULL, 0}};

void
mdb_instance_config_setup_default(ldbm_instance *inst)
{
#ifdef TODO
    config_info *config;

    for (config = mdb_instance_config; config->config_name != NULL; config++) {
        mdb_config_set((void *)inst, config->config_name, mdb_instance_config, NULL /* use default */, NULL, CONFIG_PHASE_INITIALIZATION, 1 /* apply */, LDAP_MOD_REPLACE);
    }
#endif /* TODO */
}
/* Returns LDAP_SUCCESS on success */
int
mdb_instance_config_set(ldbm_instance *inst, char *attrname, int mod_apply, int mod_op, int phase, struct berval *value)
{
#ifdef TODO
    config_info *config = config_info_get(mdb_instance_config, attrname);

    if (config == NULL) {
        /* ignore unknown attr */
        return LDAP_SUCCESS;
    } else {
        return mdb_config_set((void *)inst, config->config_name, mdb_instance_config, value, NULL, phase, mod_apply, mod_op);
    }
#endif /* TODO */
}


/*------------------------------------------------------------------------
 * callback for instence entry handling in the mdb layer
 * so far only used for post delete operations, but for
 * completeness all potential callbacks are defined
 *----------------------------------------------------------------------*/
int
mdb_instance_postadd_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst)
{
#ifdef TODO

    /* callback to be defined, does nothing for now */

    return SLAPI_DSE_CALLBACK_OK;
#endif /* TODO */
}

int
mdb_instance_add_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst)
{
#ifdef TODO

    /* callback to be defined, does nothing for now */

    return SLAPI_DSE_CALLBACK_OK;
#endif /* TODO */
}

int
mdb_instance_post_delete_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst)
{
#ifdef TODO
    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;
    struct mdb_db_env *pEnv = priv->dblayer_env;
    if (pEnv) {
        PRDir *dirhandle = NULL;
        char inst_dir[MAXPATHLEN * 2];
        char *inst_dirp = NULL;

        if (inst->inst_dir_name == NULL) {
            dblayer_get_instance_data_dir(inst->inst_be);
        }
        inst_dirp = dblayer_get_full_inst_dir(li, inst,
                                              inst_dir, MAXPATHLEN * 2);
        if (NULL != inst_dirp) {
            dirhandle = PR_OpenDir(inst_dirp);
            /* the db dir instance may have been removed already */
            if (dirhandle) {
                PRDirEntry *direntry = NULL;
                char *dbp = NULL;
                char *p = NULL;
                while (NULL != (direntry = PR_ReadDir(dirhandle,
                                                      PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
                    int rc;
                    if (!direntry->name)
                        break;

                    dbp = PR_smprintf("%s/%s", inst_dirp, direntry->name);
                    if (NULL == dbp) {
                        slapi_log_err(SLAPI_LOG_ERR,
                                      "mdb_instance_post_delete_instance_entry_callback",
                                      "Failed to generate db path: %s/%s\n",
                                      inst_dirp, direntry->name);
                        break;
                    }

                    p = strstr(dbp, LDBM_FILENAME_SUFFIX);
                    if (NULL != p &&
                        strlen(p) == strlen(LDBM_FILENAME_SUFFIX)) {
                        rc = mdb_db_remove(pEnv, dbp, 0);
                    } else {
                        rc = PR_Delete(dbp);
                    }
                    PR_ASSERT(rc == 0);
                    if (rc != 0) {
                        slapi_log_err(SLAPI_LOG_ERR,
                                      "mdb_instance_post_delete_instance_entry_callback",
                                      "Failed to delete %s, error %d\n", dbp, rc);
                    }
                    PR_smprintf_free(dbp);
                }
                PR_CloseDir(dirhandle);
            }
        } /* non-null dirhandle */
        if (inst_dirp != inst_dir) {
            slapi_ch_free_string(&inst_dirp);
        }
        /* unregister the monitor */
        mdb_instance_unregister_monitor(inst);
    } /* non-null pEnv */
    return SLAPI_DSE_CALLBACK_OK;
#endif /* TODO */
}

int
mdb_instance_delete_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst)
{
#ifdef TODO

    /* callback to be defined, does nothing for now */

    return SLAPI_DSE_CALLBACK_OK;
#endif /* TODO */
}

/* adding mdb instance specific attributes, instance lock must be held */
int
mdb_instance_search_callback(Slapi_Entry *e, int *returncode, char *returntext, ldbm_instance *inst)
{
#ifdef TODO
    char buf[BUFSIZ];
    struct berval *vals[2];
    struct berval val;
    config_info *config;

    vals[0] = &val;
    vals[1] = NULL;

    for (config = mdb_instance_config; config->config_name != NULL; config++) {
        /* Go through the ldbm_config table and fill in the entry. */

        if (!(config->config_flags & (CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_PREVIOUSLY_SET))) {
            /* This config option shouldn't be shown */
            continue;
        }

        mdb_config_get((void *)inst, config, buf);

        val.bv_val = buf;
        val.bv_len = strlen(buf);
        slapi_entry_attr_replace(e, config->config_name, vals);
    }

    return LDAP_SUCCESS;
#endif /* TODO */
}

int
mdb_instance_cleanup(struct ldbm_instance *inst)
{
#ifdef TODO
    int return_value = 0;
    /* ignore the value of env, close, because at this point,
    * work is done with import env by calling env.close,
    * env and all the associated db handles will be closed, ignore,
    * if sleepycat complains, that db handles are open at env close time */
    mdb_db_env *inst_env = (mdb_db_env *)inst->inst_db;
    DB_ENV *env = 0;
    return_value |= ((mdb_db_env *)inst->inst_db)->mdb_DB_ENV->close(((mdb_db_env *)inst->inst_db)->mdb_DB_ENV, 0);
    return_value = db_env_create(&env, 0);
    if (return_value == 0) {
        char inst_dir[MAXPATHLEN];
        char *inst_dirp = dblayer_get_full_inst_dir(inst->inst_li, inst,
                                                    inst_dir, MAXPATHLEN);
        if (inst_dirp && *inst_dir) {
            return_value = env->remove(env, inst_dirp, 0);
        } else {
            return_value = -1;
        }
        if (return_value == EBUSY) {
            return_value = 0; /* something else is using the env so ignore */
        }
        if (inst_dirp != inst_dir)
            slapi_ch_free_string(&inst_dirp);
    }
    slapi_destroy_rwlock(inst_env->mdb_env_lock);
    pthread_mutex_destroy(&(inst_env->mdb_thread_count_lock));
    pthread_cond_destroy(&(inst_env->mdb_thread_count_cv));
    slapi_ch_free((void **)&inst->inst_db);
    /*
    slapi_destroy_rwlock(((mdb_db_env *)inst->inst_db)->mdb_env_lock);
    slapi_ch_free((void **)&inst->inst_db);
    */

    return return_value;
#endif /* TODO */
}

int
mdb_instance_create(struct ldbm_instance *inst)
{
#ifdef TODO
    int return_value = 0;

    /* Initialize the fields with some default values. */
    mdb_instance_config_setup_default(inst);

    return return_value;
#endif /* TODO */
}

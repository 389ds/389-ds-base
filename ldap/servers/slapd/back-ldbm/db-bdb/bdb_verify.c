/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2019 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* dbverify.c - verify database files */

#include "bdb_layer.h"

static int
dbverify_ext(ldbm_instance *inst, int verbose)
{
    char dbdir[MAXPATHLEN];
    char *filep = NULL;
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    DB *dbp = NULL;
    size_t tmplen = 0;
    size_t filelen = 0;
    int rval = 1;
    int rval_main = 0;
    struct ldbminfo *li = inst->inst_li;
    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;
    bdb_config *conf = (bdb_config *)li->li_dblayer_config;
    struct bdb_db_env *pEnv = priv->dblayer_env;

    dbdir[sizeof(dbdir) - 1] = '\0';
    PR_snprintf(dbdir, sizeof(dbdir), "%s/%s", inst->inst_parent_dir_name,
                inst->inst_dir_name);
    if ('\0' != dbdir[sizeof(dbdir) - 1]) /* overflown */
    {
        slapi_log_err(SLAPI_LOG_ERR, "dbverify_ext",
                      "db path too long: %s/%s\n",
                      inst->inst_parent_dir_name, inst->inst_dir_name);
        return 1;
    }
    tmplen = strlen(dbdir);
    filep = dbdir + tmplen;
    filelen = sizeof(dbdir) - tmplen;

    /* run dbverify on each each db file */
    dirhandle = PR_OpenDir(dbdir);
    if (!dirhandle) {
        slapi_log_err(SLAPI_LOG_ERR, "dbverify_ext",
                      "PR_OpenDir (%s) failed (%d): %s\n",
                      dbdir, PR_GetError(), slapd_pr_strerror(PR_GetError()));
        return 1;
    }
    while (NULL !=
           (direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
        /* struct attrinfo *ai = NULL; */
        dbp = NULL;

        if (!direntry->name) {
            break;
        }
        if (!strstr(direntry->name, LDBM_FILENAME_SUFFIX)) /* non db file */
        {
            continue;
        }
        if (sizeof(direntry->name) + 2 > filelen) {
            slapi_log_err(SLAPI_LOG_ERR, "dbverify_ext",
                          "db path too long: %s/%s\n",
                          dbdir, direntry->name);
            continue;
        }
        PR_snprintf(filep, filelen, "/%s", direntry->name);
        rval = db_create(&dbp, pEnv->bdb_DB_ENV, 0);
        if (0 != rval) {
            slapi_log_err(SLAPI_LOG_ERR, "dbverify_ext",
                          "Unable to create id2entry db file %d\n", rval);
            return rval;
        }

#define VLVPREFIX "vlv#"
        if (0 != strncmp(direntry->name, ID2ENTRY, strlen(ID2ENTRY))) {
            struct attrinfo *ai = NULL;
            char *p = NULL;
            p = strstr(filep, LDBM_FILENAME_SUFFIX); /* since already checked,
                                                        it must have it */
            if (p)
                *p = '\0';
            ainfo_get(inst->inst_be, filep + 1, &ai);
            if (p)
                *p = '.';
            if (ai->ai_key_cmp_fn) {
                dbp->app_private = (void *)ai->ai_key_cmp_fn;
                dbp->set_bt_compare(dbp, bdb_bt_compare);
            }
            if (idl_get_idl_new()) {
                rval = dbp->set_pagesize(dbp,
                                         (conf->bdb_index_page_size == 0) ? DBLAYER_INDEX_PAGESIZE : conf->bdb_index_page_size);
            } else {
                rval = dbp->set_pagesize(dbp,
                                         (conf->bdb_page_size == 0) ? DBLAYER_PAGESIZE : conf->bdb_page_size);
            }
            if (0 != rval) {
                slapi_log_err(SLAPI_LOG_ERR, "DB verify",
                              "Unable to set pagesize flags to db (%d)\n", rval);
                return rval;
            }
            if (0 == strncmp(direntry->name, VLVPREFIX, strlen(VLVPREFIX))) {
                rval = dbp->set_flags(dbp, DB_RECNUM);
                if (0 != rval) {
                    slapi_log_err(SLAPI_LOG_ERR, "dbverify_ext",
                                  "Unable to set RECNUM flag to vlv index (%d)\n", rval);
                    return rval;
                }
            } else if (idl_get_idl_new()) {
                rval = dbp->set_flags(dbp, DB_DUP | DB_DUPSORT);
                if (0 != rval) {
                    slapi_log_err(SLAPI_LOG_ERR, "dbverify_ext",
                                  "Unable to set DUP flags to db (%d)\n", rval);
                    return rval;
                }

                if (ai->ai_dup_cmp_fn) {
                    /* If set, use the special dup compare callback */
                    rval = dbp->set_dup_compare(dbp, ai->ai_dup_cmp_fn);
                } else {
                    rval = dbp->set_dup_compare(dbp, idl_new_compare_dups);
                }

                if (0 != rval) {
                    slapi_log_err(SLAPI_LOG_ERR, "dbverify_ext",
                                  "Unable to set dup_compare to db (%d)\n", rval);
                    return rval;
                }
            }
        }
#undef VLVPREFIX
        rval = dbp->verify(dbp, dbdir, NULL, NULL, 0);
        if (0 == rval) {
            if (verbose) {
                slapi_log_err(SLAPI_LOG_INFO, "dbverify_ext",
                              "%s: ok\n", dbdir);
            }
        } else {
            slapi_log_err(SLAPI_LOG_ERR, "dbverify_ext",
                          "verify failed(%d): %s\n", rval, dbdir);
        }
        rval_main |= rval;
        *filep = '\0';
    }
    PR_CloseDir(dirhandle);

    return rval_main;
}

int
bdb_verify(Slapi_PBlock *pb)
{
    struct ldbminfo *li = NULL;
    Object *inst_obj = NULL;
    ldbm_instance *inst = NULL;
    int verbose = 0;
    int rval = 1;
    int rval_main = 0;
    char **instance_names = NULL;
    char *dbdir = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "bdb_verify", "Verifying db files...\n");
    slapi_pblock_get(pb, SLAPI_BACKEND_INSTANCE_NAME, &instance_names);
    slapi_pblock_get(pb, SLAPI_SEQ_TYPE, &verbose);
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_DBVERIFY_DBDIR, &dbdir);
    bdb_config_load_dse_info(li);
    bdb_config_internal_set(li, CONFIG_DB_TRANSACTION_LOGGING, "off");

    /* no write needed; choose EXPORT MODE */
    if (0 != bdb_start(li, DBLAYER_EXPORT_MODE)) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_verify", "dbverify: Failed to init database\n");
        return rval;
    }

    /* server is up */
    slapi_log_err(SLAPI_LOG_TRACE, "bdb_verify", "server is up\n");
    if (instance_names) /* instance is specified */
    {
        char **inp = NULL;
        for (inp = instance_names; inp && *inp; inp++) {
            inst = ldbm_instance_find_by_name(li, *inp);
            if (inst) {
                if (dbdir) {
                    /* verifying backup */
                    slapi_ch_free_string(&inst->inst_parent_dir_name);
                    inst->inst_parent_dir_name = slapi_ch_strdup(dbdir);
                }
                rval_main |= dbverify_ext(inst, verbose);
            } else {
                rval_main |= 1; /* no such instance */
            }
        }
    } else /* all instances */
    {
        for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
             inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
            inst = (ldbm_instance *)object_get_data(inst_obj);
            /* check if an import/restore is already ongoing... */
            if (instance_set_busy(inst) != 0) {
                /* standalone, only.  never happens */
                slapi_log_err(SLAPI_LOG_WARNING, "bdb_verify",
                              "Backend '%s' is already in the middle of "
                              "another task and cannot be disturbed.\n",
                              inst->inst_name);
                continue; /* skip this instance and go to the next*/
            }
            if (dbdir) {
                /* verifying backup */
                slapi_ch_free_string(&inst->inst_parent_dir_name);
                inst->inst_parent_dir_name = slapi_ch_strdup(dbdir);
            }
            rval_main |= dbverify_ext(inst, verbose);
        }
    }

    /* close the database down again */
    rval = bdb_post_close(li, DBLAYER_EXPORT_MODE);
    if (0 != rval) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_verify", "Failed to close database\n");
    }

    return rval_main;
}

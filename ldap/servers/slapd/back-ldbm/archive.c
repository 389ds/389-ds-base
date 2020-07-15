/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* archive.c - ldap ldbm back-end archive and restore entry points */

#include "back-ldbm.h"

int
ldbm_back_archive2ldbm(Slapi_PBlock *pb)
{
    struct ldbminfo *li;
    Slapi_Task *task;
    ldbm_instance *inst = NULL;
    char *rawdirectory = NULL; /* -a <directory> */
    char *directory = NULL;    /* normalized */
    int32_t return_value = -1;
    int32_t task_flags = 0;
    int32_t run_from_cmdline = 0;
    int32_t is_old_to_new = 0;

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_SEQ_VAL, &rawdirectory);
    slapi_pblock_get(pb, SLAPI_BACKEND_TASK, &task);
    slapi_pblock_get(pb, SLAPI_TASK_FLAGS, &task_flags);
    li->li_flags = run_from_cmdline = (task_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE);

    if (!rawdirectory || !*rawdirectory) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_archive2ldbm", "No archive name\n");
        return -1;
    }

    directory = rel2abspath(rawdirectory);
    /* skip check for version and idl upgrade
    return_value = dbversion_read(li, directory, &dbversion, &dataversion);
    if (return_value) {
        if (ENOENT == return_value) {
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_archive2ldbm", "No back up \"%s\" exists.\n",
                          directory);
            return -1;
        }
        slapi_log_err(SLAPI_LOG_WARNING, "ldbm_back_archive2ldbm",
                      "Unable to read dbversion file in %s\n", directory);
    }
*/
    /* check the current idl format vs backup DB version
    if (idl_get_idl_new()) {
        value = lookup_dbversion(dbversion, DBVERSION_TYPE);
        if (value & DBVERSION_OLD_IDL) {
            is_old_to_new = 1;
        }
    }
    slapi_ch_free_string(&dbversion);
    slapi_ch_free_string(&dataversion);
*/

    /* No ldbm be's exist until we process the config information. */
    if (run_from_cmdline) {
        mapping_tree_init();

        if (dblayer_setup(li)) {
            slapi_log_err(SLAPI_LOG_CRIT, "ldbm_back_init", "dblayer_setup failed\n");
            return -1;
        }

        /* initialize a restore file to be able to detect a startup after restore */
        if (dblayer_restore_file_init(li)) {
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_archive2ldbm", "Failed to write restore file.\n");
            return -1;
        }
    }
    if (!run_from_cmdline) {
        Object *inst_obj, *inst_obj2;

        /* task does not support restore old idl onto new idl server */
        if (is_old_to_new) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "ldbm_back_archive2ldbm", "Backup has old idl format; "
                                                    "to restore old formated backup onto the new server, "
                                                    "please use command line utility \"bak2db\" .\n");
            if (task) {
                slapi_task_log_notice(task,
                                      "Backup has old idl format; "
                                      "to restore old formated backup onto the new server, "
                                      "please use command line utility \"bak2db\" .");
            }
            goto out;
        }
        /* server is up -- mark all backends busy */
        for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
             inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
            inst = (ldbm_instance *)object_get_data(inst_obj);

            /* check if an import/restore is already ongoing... */
            if (instance_set_busy(inst) != 0) {
                slapi_log_err(SLAPI_LOG_WARNING,
                              "ldbm_back_archive2ldbm", "'%s' is already in the middle of "
                                                        "another task and cannot be disturbed.\n",
                              inst->inst_name);
                if (task) {
                    slapi_task_log_notice(task,
                                          "Backend '%s' is already in the middle of "
                                          "another task and cannot be disturbed.",
                                          inst->inst_name);
                }

                /* painfully, we have to clear the BUSY flags on the
                 * backends we'd already marked...
                 */
                for (inst_obj2 = objset_first_obj(li->li_instance_set);
                     inst_obj2 && (inst_obj2 != inst_obj);
                     inst_obj2 = objset_next_obj(li->li_instance_set,
                                                 inst_obj2)) {
                    inst = (ldbm_instance *)object_get_data(inst_obj2);
                    instance_set_not_busy(inst);
                }
                if (inst_obj2 && inst_obj2 != inst_obj)
                    object_release(inst_obj2);
                object_release(inst_obj);
                goto out;
            }
        }

        /* now take down ALL BACKENDS and changelog */
        for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
             inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
            inst = (ldbm_instance *)object_get_data(inst_obj);
            slapi_log_err(SLAPI_LOG_INFO, "ldbm_back_archive2ldbm", "Bringing %s offline...\n",
                          inst->inst_name);
            if (task) {
                slapi_task_log_notice(task, "Bringing %s offline...",
                                      inst->inst_name);
            }
            slapi_mtn_be_disable(inst->inst_be);
            cache_clear(&inst->inst_cache, CACHE_TYPE_ENTRY);
            if (entryrdn_get_switch()) {
                cache_clear(&inst->inst_dncache, CACHE_TYPE_DN);
            }
        }
        plugin_call_plugins(pb, SLAPI_PLUGIN_BE_PRE_CLOSE_FN);
        /* now we know nobody's using any of the backend instances, so we
         * can shutdown the dblayer -- this closes all instances too.
         * Use DBLAYER_RESTORE_MODE to prevent loss of perfctr memory.
         */
        dblayer_close(li, DBLAYER_RESTORE_MODE);
    }

    /* tell the database to restore */
    return_value = dblayer_restore(li, directory, task);
    if (0 != return_value) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "ldbm_back_archive2ldbm", "Failed to read backup file set. "
                                                "Either the directory specified doesn't exist, "
                                                "or it exists but doesn't contain a valid backup set, "
                                                "or file permissions prevent the server reading "
                                                "the backup set.  error=%d (%s)\n",
                      return_value, dblayer_strerror(return_value));
        if (task) {
            slapi_task_log_notice(task, "Failed to read the backup file set from %s",
                                  directory);
        }
    }

    if (run_from_cmdline) {
        if (is_old_to_new) {
            /* does not exist */
            char *p;
            char c;
            char *bakup_dir = NULL;
            int32_t skipinit = SLAPI_UPGRADEDB_SKIPINIT;

            p = strrchr(directory, '/');
            if (NULL == p) {
                p = strrchr(directory, '\\');
            }

            if (NULL == p) /* never happen, I guess */
            {
                slapi_ch_free_string(&directory);
                directory = slapi_ch_smprintf(".");
                c = '/';
            } else {
                c = *p;
                *p = '\0';
            }
            bakup_dir = slapi_ch_smprintf("%s%ctmp_%010ld", directory, c, time(0));
            slapi_log_err(SLAPI_LOG_INFO,
                          "ldbm_back_archive2ldbm", "Backup dir: %s\n", bakup_dir);
            if (p)
                *p = c;

            slapi_pblock_set(pb, SLAPI_SEQ_VAL, bakup_dir);
            slapi_pblock_set(pb, SLAPI_SEQ_TYPE, &skipinit);
            return_value = ldbm_back_upgradedb(pb);
        }
    } else {
        Object *inst_obj;
        int32_t ret;

        if (0 != return_value) {
            /*
             * error case (607331)
             * just to go back to the previous state if possible (preserve
             * original error for now)
             */
            if ((ret = dblayer_start(li, DBLAYER_NORMAL_MODE))) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "ldbm_back_archive2ldbm", "Unable to to start database in [%s]\n",
                              li->li_directory);
                if (task) {
                    slapi_task_log_notice(task, "Failed to start the database in %s",
                                          li->li_directory);
                }
            }
        }

        /* bring all backends and changelog back online */
        plugin_call_plugins(pb, SLAPI_PLUGIN_BE_POST_OPEN_FN);
        for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
             inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
            inst = (ldbm_instance *)object_get_data(inst_obj);
            ret = dblayer_instance_start(inst->inst_be, DBLAYER_NORMAL_MODE);
            if (ret != 0) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "ldbm_back_archive2ldbm", "Unable to restart '%s'\n",
                              inst->inst_name);
                if (task) {
                    slapi_task_log_notice(task, "Unable to restart '%s'", inst->inst_name);
                }
            } else {
                slapi_mtn_be_enable(inst->inst_be);
                instance_set_not_busy(inst);
            }
        }
    }

out:
    if (run_from_cmdline && (0 == return_value)) {
        dblayer_restore_file_update(li, directory);
    }
    slapi_ch_free_string(&directory);
    return return_value;
}

int
ldbm_back_ldbm2archive(Slapi_PBlock *pb)
{
    struct ldbminfo *li;
    char *rawdirectory = NULL; /* -a <directory> */
    char *directory = NULL;    /* normalized */
    char *dir_bak = NULL;
    int return_value = -1;
    int task_flags = 0;
    int run_from_cmdline = 0;
    Slapi_Task *task;
    struct stat sbuf;

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_SEQ_VAL, &rawdirectory);
    slapi_pblock_get(pb, SLAPI_TASK_FLAGS, &task_flags);
    li->li_flags = run_from_cmdline = (task_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE);

    slapi_pblock_get(pb, SLAPI_BACKEND_TASK, &task);

    if (!rawdirectory || !*rawdirectory) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_ldbm2archive", "No archive name\n");
        return -1;
    }

    /* start the database code up, do not attempt to perform recovery */
    if (run_from_cmdline) {
        /* No ldbm be's exist until we process the config information. */

    /* copied here, need better solution */
    /* initialize dblayer  */
        if (dblayer_setup(li)) {
            slapi_log_err(SLAPI_LOG_CRIT, "ldbm_back_init", "dblayer_setup failed\n");
            goto out;
        }

        mapping_tree_init();

        if (0 != (return_value =
                      dblayer_start(li,
                                    DBLAYER_ARCHIVE_MODE | DBLAYER_NO_DBTHREADS_MODE))) {
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_ldbm2archive", "Failed to init database\n");
            if (task) {
                slapi_task_log_notice(task, "Failed to init database");
            }
            return -1;
        }
    }

    /* Initialize directory */
    directory = rel2abspath(rawdirectory);

    if (stat(directory, &sbuf) == 0) {
        if (slapd_comp_path(directory, li->li_directory) == 0) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "ldbm_back_ldbm2archive", "Cannot archive to the db directory.\n");
            if (task) {
                slapi_task_log_notice(task,
                                      "Cannot archive to the db directory.");
            }
            return_value = -1;
            goto out;
        }

        dir_bak = slapi_ch_smprintf("%s.bak", directory);
        slapi_log_err(SLAPI_LOG_INFO, "ldbm_back_ldbm2archive", "%s exists. Renaming to %s\n",
                      directory, dir_bak);
        if (task) {
            slapi_task_log_notice(task, "%s exists. Renaming to %s",
                                  directory, dir_bak);
        }
        if (stat(dir_bak, &sbuf) == 0) {
            return_value = ldbm_delete_dirs(dir_bak);
            if (0 != return_value) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "ldbm_back_ldbm2archive", "%s exists and failed to delete it.\n",
                              dir_bak);
                if (task) {
                    slapi_task_log_notice(task,
                                          "%s exists and failed to delete it.", dir_bak);
                }
                return_value = -1;
                goto out;
            }
        }
        return_value = PR_Rename(directory, dir_bak);
        if (return_value != PR_SUCCESS) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR,
                          "ldbm_back_ldbm2archive", "Failed to rename \"%s\" to \"%s\".\n",
                          directory, dir_bak);
            slapi_log_err(SLAPI_LOG_ERR,
                          "ldbm_back_ldbm2archive", SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          prerr, slapd_pr_strerror(prerr));
            if (task) {
                slapi_task_log_notice(task,
                                      "Failed to rename \"%s\" to \"%s\".",
                                      directory, dir_bak);
                slapi_task_log_notice(task,
                                      SLAPI_COMPONENT_NAME_NSPR " error %d (%s)",
                                      prerr, slapd_pr_strerror(prerr));
            }
            return_value = -1;
            goto out;
        }
    }
    if (0 != MKDIR(directory, SLAPD_DEFAULT_DIR_MODE) && EEXIST != errno) {
        char *msg = dblayer_strerror(errno);

        slapi_log_err(SLAPI_LOG_ERR,
                      "ldbm_back_ldbm2archive", "mkdir(%s) failed; errno %i (%s)\n",
                      directory, errno, msg ? msg : "unknown");
        if (task) {
            slapi_task_log_notice(task, "mkdir(%s) failed; errno %i (%s)",
                                  directory, errno, msg ? msg : "unknown");
        }
        return_value = -1;
        goto err;
    }

    /* to avoid conflict w/ import, do this check for commandline, as well */
    {
        Object *inst_obj, *inst_obj2;
        ldbm_instance *inst = NULL;

        /* server is up -- mark all backends busy */
        for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
             inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
            inst = (ldbm_instance *)object_get_data(inst_obj);

            /* check if an import/restore is already ongoing... */
            if (instance_set_busy(inst) != 0 || dblayer_in_import(inst) != 0) {
                slapi_log_err(SLAPI_LOG_WARNING,
                              "ldbm_back_ldbm2archive", "Backend '%s' is already in the middle of "
                                                        "another task and cannot be disturbed.\n",
                              inst->inst_name);
                if (task) {
                    slapi_task_log_notice(task,
                                          "Backend '%s' is already in the middle of "
                                          "another task and cannot be disturbed.",
                                          inst->inst_name);
                }

                /* painfully, we have to clear the BUSY flags on the
                 * backends we'd already marked...
                 */
                for (inst_obj2 = objset_first_obj(li->li_instance_set);
                     inst_obj2 && (inst_obj2 != inst_obj);
                     inst_obj2 = objset_next_obj(li->li_instance_set,
                                                 inst_obj2)) {
                    inst = (ldbm_instance *)object_get_data(inst_obj2);
                    instance_set_not_busy(inst);
                }
                if (inst_obj2 && inst_obj2 != inst_obj)
                    object_release(inst_obj2);
                object_release(inst_obj);
                goto err;
            }
        }
    }

    return_value = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_PRE_BACKUP_FN);
    if (return_value) {
        slapi_log_err(SLAPI_LOG_BACKLDBM,
                      "ldbm_back_ldbm2archive", "pre-backup-plugin failed (%d).\n", return_value);
        if (is_slapd_running() && run_from_cmdline) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "ldbm_back_ldbm2archive", "Standalone db2bak is not supported when a "
                                                    "multimaster replication enabled server is "
                                                    "coexisting.\nPlease use db2bak.pl, instead.\n");
            goto err;
        }
    }

    /* tell it to archive */
    return_value = dblayer_backup(li, directory, task);
    if (return_value) {
        slapi_log_err(SLAPI_LOG_BACKLDBM,
                      "ldbm_back_ldbm2archive", "dblayer_backup failed (%d).\n", return_value);
        goto err;
    }

    return_value = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_POST_BACKUP_FN);
    if (return_value) {
        slapi_log_err(SLAPI_LOG_BACKLDBM,
                      "ldbm_back_ldbm2archive", "post-backup-plugin failed (%d).\n", return_value);
    }

    if (!run_from_cmdline) {
        ldbm_instance *inst;
        Object *inst_obj;

        /* none of these backends are busy anymore */
        for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
             inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
            inst = (ldbm_instance *)object_get_data(inst_obj);
            instance_set_not_busy(inst);
        }
    }
err:
    if (return_value) {
        if (dir_bak) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "ldbm_back_ldbm2archive", "Failed renaming %s back to %s\n",
                          dir_bak, directory);
            if (task) {
                slapi_task_log_notice(task,
                                      "db2archive failed: renaming %s back to %s",
                                      dir_bak, directory);
            }
        } else {
            slapi_log_err(SLAPI_LOG_ERR,
                          "ldbm_back_ldbm2archive", "Failed removing %s\n", directory);
            if (task) {
                slapi_task_log_notice(task, "db2archive failed: removing %s",
                                      directory);
            }
        }
        ldbm_delete_dirs(directory);
        if (dir_bak && (PR_SUCCESS != PR_Rename(dir_bak, directory))) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR,
                          "ldbm_back_ldbm2archive", "Failed to rename \"%s\" to \"%s\".\n",
                          dir_bak, directory);
            slapi_log_err(SLAPI_LOG_ERR,
                          "ldbm_back_ldbm2archive", SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          prerr, slapd_pr_strerror(prerr));
            if (task) {
                slapi_task_log_notice(task,
                                      "Failed to rename \"%s\" to \"%s\".",
                                      dir_bak, directory);
                slapi_task_log_notice(task,
                                      SLAPI_COMPONENT_NAME_NSPR " error %d (%s)",
                                      prerr, slapd_pr_strerror(prerr));
            }
        }
    }
out:
    /* close the database down again */
    if (run_from_cmdline &&
        0 != dblayer_close(li, DBLAYER_ARCHIVE_MODE | DBLAYER_NO_DBTHREADS_MODE)) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_ldbm2archive", "Failed to close database\n");
        if (task) {
            slapi_task_log_notice(task, "Failed to close database");
        }
    }

    slapi_ch_free_string(&dir_bak);
    slapi_ch_free_string(&directory);
    return return_value;
}

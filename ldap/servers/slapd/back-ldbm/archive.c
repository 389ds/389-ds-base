/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2023 Red Hat, Inc.
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
#include "dblayer.h"

#define NO_OBJECT ((Object*)-1)

int
ldbm_temporary_close_all_instances(Slapi_PBlock *pb)
{
    Object *inst_obj, *inst_obj2;
    int32_t return_value = -1;
    ldbm_instance *inst = NULL;
    struct ldbminfo *li;
    Slapi_Task *task;

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_BACKEND_TASK, &task);

    /* server is up -- mark all backends busy */
    for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
         inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
        inst = (ldbm_instance *)object_get_data(inst_obj);

        /* check if an import/restore is already ongoing... */
        if (instance_set_busy(inst) != 0) {
            slapi_log_err(SLAPI_LOG_WARNING,
                          "ldbm_temporary_close_all_instances", "'%s' is already in the middle of "
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
        slapi_log_err(SLAPI_LOG_INFO, "ldbm_temporary_close_all_instances", "Bringing %s offline...\n",
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
    return_value = 0;
out:
    return return_value;
}

int
ldbm_restart_temporary_closed_instances(Slapi_PBlock *pb)
{
    Object *inst_obj;
    int32_t ret;
    int32_t return_value = -1;
    ldbm_instance *inst = NULL;
    struct ldbminfo *li;
    Slapi_Task *task;

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_BACKEND_TASK, &task);

    if (0 != return_value) {
        /*
         * error case (607331)
         * just to go back to the previous state if possible (preserve
         * original error for now)
         */
        if ((dblayer_start(li, DBLAYER_NORMAL_MODE))) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "ldbm_restart_temporary_closed_instances", "Unable to to start database in [%s]\n",
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
                          "ldbm_restart_temporary_closed_instances", "Unable to restart '%s'\n",
                          inst->inst_name);
            if (task) {
                slapi_task_log_notice(task, "Unable to restart '%s'", inst->inst_name);
            }
        } else {
            slapi_mtn_be_enable(inst->inst_be);
            instance_set_not_busy(inst);
        }
    }
    return_value = 0;
    return return_value;
}

int
ldbm_back_archive2ldbm(Slapi_PBlock *pb)
{
    struct ldbminfo *li;
    dblayer_private *priv = NULL;
    Slapi_Task *task;
    ldbm_instance *inst = NULL;
    char *rawdirectory = NULL; /* -a <directory> */
    char *directory = NULL;    /* normalized */
    int32_t return_value = -1;
    int32_t task_flags = 0;
    int32_t run_from_cmdline = 0;

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

    /* No ldbm be's exist until we process the config information. */
    if (run_from_cmdline) {
        mapping_tree_init();

        if (dblayer_setup(li)) {
            slapi_log_err(SLAPI_LOG_CRIT, "ldbm_back_archive2ldbm", "dblayer_setup failed\n");
            slapi_ch_free_string(&directory);
            return -1;
        }
        priv = (dblayer_private *)li->li_dblayer_private;

        /* initialize a restore file to be able to detect a startup after restore */
        if (priv->dblayer_restore_file_init_fn(li)) {
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_archive2ldbm", "Failed to write restore file.\n");
            slapi_ch_free_string(&directory);
            return -1;
        }
    }
    if (!run_from_cmdline) {
        return_value = ldbm_temporary_close_all_instances(pb);
        if (0 != return_value) {
            goto out;
        }
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

    if (!run_from_cmdline) {
        Object *inst_obj;
        int32_t ret;

        if (0 != return_value) {
            /*
             * error case (607331)
             * just to go back to the previous state if possible (preserve
             * original error for now)
             */
            if (dblayer_start(li, DBLAYER_NORMAL_MODE)) {
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
    if (priv && run_from_cmdline && (0 == return_value)) {
        priv->dblayer_restore_file_update_fn(li, directory);
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
    Object *last_busy_inst_obj = NO_OBJECT;

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
            slapi_log_err(SLAPI_LOG_CRIT, "ldbm_back_ldbm2archive", "dblayer_setup failed\n");
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
        const char *msg = dblayer_strerror(errno);

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
        ldbm_instance *inst = NULL;

        /* server is up -- mark all backends busy */
        for (last_busy_inst_obj = objset_first_obj(li->li_instance_set); last_busy_inst_obj;
             last_busy_inst_obj = objset_next_obj(li->li_instance_set, last_busy_inst_obj)) {
            inst = (ldbm_instance *)object_get_data(last_busy_inst_obj);

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
                goto err;
            }
        }
    }

    /* tell it to archive */
    return_value = dblayer_backup(li, directory, task);
    if (return_value) {
        slapi_log_err(SLAPI_LOG_BACKLDBM,
                      "ldbm_back_ldbm2archive", "dblayer_backup failed (%d).\n", return_value);
        goto err;
    }

err:
    /* Clear all BUSY flags that have been previously set */
    if (last_busy_inst_obj != NO_OBJECT) {
        ldbm_instance *inst;
        Object *inst_obj;

        for (inst_obj = objset_first_obj(li->li_instance_set);
             inst_obj && (inst_obj != last_busy_inst_obj);
             inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
            inst = (ldbm_instance *)object_get_data(inst_obj);
            instance_set_not_busy(inst);
        }
        if (last_busy_inst_obj != NULL) {
            /* release last seen object for aborted objset_next_obj iterations */
            if (inst_obj != NULL) {
                object_release(inst_obj);
            }
            object_release(last_busy_inst_obj);
        }
    }
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

#define CPRETRY 4
/* Copy a file to the backup */
static int32_t
archive_copyfile(char *source, char *destination, char *filename, mode_t mode, Slapi_Task *task)
{
    PRFileDesc *source_fd = NULL;
    PRFileDesc *dest_fd = NULL;
    char *buffer = NULL;
    int return_value = -1;
    int bytes_to_write = 0;
    char *dest_file = NULL;

    if (PR_Access(source, PR_ACCESS_EXISTS) != 0) {
        PRErrorCode prerr = PR_GetError();
        if (task) {
            slapi_task_log_notice(task,
                    "archive_copyfile - Source file (%s) could not be accessed - error %d (%s)",
                    source, prerr, slapd_pr_strerror(prerr));
        }
        slapi_log_err(SLAPI_LOG_ERR, "archive_copyfile",
                      "Source file (%s) could not be accessed - error %d (%s)\n",
                      source, prerr, slapd_pr_strerror(prerr));
        return -1;
    }

    /* malloc the buffer */
    buffer = slapi_ch_malloc(64 * 1024);

    /* Open source file */
    source_fd = PR_Open(source, PR_RDONLY, SLAPD_DEFAULT_FILE_MODE);
    if (source_fd == NULL) {
        PRErrorCode prerr = PR_GetError();
        if (task) {
            slapi_task_log_notice(task,
                    "archive_copyfile - Source file (%s) could not be opened - error %d (%s)",
                    source, prerr, slapd_pr_strerror(prerr));
        }
        slapi_log_err(SLAPI_LOG_ERR, "archive_copyfile",
                      "Source file (%s) could not be opened - error %d (%s)\n",
                      source, prerr, slapd_pr_strerror(prerr));
        goto error;
    }

    /* Open destination file */
    dest_file = slapi_ch_smprintf("%s/%s", destination, filename);
    dest_fd = PR_Open(dest_file, PR_WRONLY | PR_CREATE_FILE, mode);
    if (dest_fd == NULL) {
        PRErrorCode prerr = PR_GetError();
        if (task) {
            slapi_task_log_notice(task,
                    "archive_copyfile - Destination file (%s) could not be opened - error %d (%s)",
                    dest_file, prerr, slapd_pr_strerror(prerr));
        }
        slapi_log_err(SLAPI_LOG_ERR, "archive_copyfile",
                      "Destination file (%s) could not be opened - error %d (%s)\n",
                      dest_file, prerr, slapd_pr_strerror(prerr));
        goto error;
    }

    slapi_log_err(SLAPI_LOG_INFO, "archive_copyfile",
                  "Copying %s to %s\n", source, dest_file);
    if (task) {
        slapi_task_log_notice(task,
                "archive_copyfile - Copying %s to %s",
                source, dest_file);
    }

    /* Loop round reading data and writing it */
    while (1) {
        char *ptr = NULL;
        size_t i = 0;
        return_value = PR_Read(source_fd, buffer, 64 * 1024);
        if (return_value <= 0) {
            /* means error or EOF */
            if (return_value < 0) {
                PRErrorCode prerr = PR_GetError();
                if (task) {
                    slapi_task_log_notice(task,
                            "archive_copyfile - Failed to read (%s) error %d (%s) - rc %d",
                            source, prerr, slapd_pr_strerror(prerr), return_value);
                }
                slapi_log_err(SLAPI_LOG_ERR, "archive_copyfile",
                              "Failed to read (%s) error %d (%s) - rc %d\n",
                              source, prerr, slapd_pr_strerror(prerr), return_value);
            }
            break;
        }
        bytes_to_write = return_value;
        ptr = buffer;
        for (i = 0; i < CPRETRY; i++) {
            return_value = PR_Write(dest_fd, ptr, bytes_to_write);
            if (return_value == bytes_to_write) {
                break;
            } else {
                /* means error */
                PRErrorCode prerr = PR_GetError();
                if (task) {
                    slapi_task_log_notice(task,
                            "archive_copyfile - Failed to write (%s) error %d (%s) - real bytes %d, expected bytes: %d",
                            dest_file, prerr, slapd_pr_strerror(prerr), return_value, bytes_to_write);
                }
                slapi_log_err(SLAPI_LOG_ERR, "archive_copyfile",
                              "Failed to write (%s) error %d (%s) - real bytes %d, expected bytes: %d\n",
                              dest_file, prerr, slapd_pr_strerror(prerr), return_value, bytes_to_write);

                if (return_value > 0) {
                    bytes_to_write -= return_value;
                    ptr += return_value;
                    slapi_log_err(SLAPI_LOG_NOTICE, "archive_copyfile",
                                  "Retrying to write %d bytes\n", bytes_to_write);
                    if (task) {
                        slapi_task_log_notice(task,
                                "archive_copyfile - Retrying to write %d bytes",
                                bytes_to_write);
                    }
                } else {
                    break;
                }
            }
        }
        if ((CPRETRY == i) || (return_value < 0)) {
            return_value = -1;
            break;
        }
    }

error:
    if (source_fd) {
        PR_Close(source_fd);
    }
    if (dest_fd) {
        PR_Close(dest_fd);
    }
    slapi_ch_free_string(&buffer);
    slapi_ch_free_string(&dest_file);

    return return_value;
}

static char *cert_files_600[] = {
    "key4.db", "cert9.db", "pin.txt", "pwdfile.txt", NULL
};
static char *config_files_440[] = {"certmap.conf", "slapd-collations.conf", NULL};

/* Archive nss files, 99user.ldif, and config files */
int32_t
ldbm_archive_config(char *bakdir, Slapi_Task *task)
{
    slapdFrontendConfig_t *config = getFrontendConfig();
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    char *schema_dir = config->schemadir;
    char *cert_dir = config->certdir;
    char *config_dir = config->configdir;
    char *backup_config_dir = slapi_ch_smprintf("%s/config_files", bakdir);
    char *dse_file = slapi_ch_smprintf("%s/dse.ldif", config_dir);
    char *schema_backup_file = slapi_ch_smprintf("%s/schema", backup_config_dir);
    int32_t rc = 0;

    dse_backup_lock();

    /* Create config_files directory */
    if (PR_MkDir(backup_config_dir, 0770) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_archive_config",
                "Failed to create directory %s - Error %d\n",
                backup_config_dir, errno);
        if (task) {
            slapi_task_log_notice(task,
                    "Failed to create directory %s - Error %d",
                    backup_config_dir, errno);
        }
        rc = -1;
        goto error;
    }

    /* Create config_files directory */
    if (PR_MkDir(schema_backup_file, 0770) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_archive_config",
                "Failed to create directory %s - Error %d\n",
                schema_backup_file, errno);
        if (task) {
            slapi_task_log_notice(task,
                    "Failed to create directory %s - Error %d",
                    schema_backup_file, errno);
        }
        rc = -1;
        goto error;
    }

    /* Config dir - dse.ldif */
    if (archive_copyfile(dse_file, backup_config_dir, "dse.ldif", 0600, task) != 0) {
        rc = -1;
        goto error;
    }

    /* Schema files */
    dirhandle = PR_OpenDir(schema_dir);
    if (NULL == dirhandle) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "ldbm_archive_config", "Failed to open dir %s\n", schema_dir);
        rc = -1;
        goto error;
    }

    while (NULL != (direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
        char *schema_file = (char *)direntry->name;
        char *source_file = slapi_ch_smprintf("%s/%s", schema_dir, schema_file);
        if (archive_copyfile(source_file, schema_backup_file, schema_file, 0644, task) != 0) {
            slapi_ch_free_string(&source_file);
            rc = -1;
            goto error;
        }
        slapi_ch_free_string(&source_file);
    }

    /* nsslapd-certdir - Certificate files */
    for (size_t i = 0; cert_files_600[i]; i++) {
        char *cert_file = slapi_ch_smprintf("%s/%s", cert_dir, cert_files_600[i]);
        if (archive_copyfile(cert_file, backup_config_dir, cert_files_600[i], 0600, task) != 0) {
            slapi_ch_free_string(&cert_file);
            rc = -1;
            goto error;
        }
        slapi_ch_free_string(&cert_file);
    }

    /* certmap & collations config files */
    for (size_t i = 0; config_files_440[i]; i++) {
        char *conf_file = slapi_ch_smprintf("%s/%s", config_dir, config_files_440[i]);
        if (archive_copyfile(conf_file, backup_config_dir, config_files_440[i], 0440, task) != 0) {
            rc = -1;
        }
        slapi_ch_free_string(&conf_file);
    }

error:
    if (NULL != dirhandle) {
        PR_CloseDir(dirhandle);
        dirhandle = NULL;
    }
    dse_backup_unlock();
    slapi_ch_free_string(&backup_config_dir);
    slapi_ch_free_string(&dse_file);
    slapi_ch_free_string(&schema_backup_file);

    return rc;
}

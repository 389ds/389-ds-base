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

/* archive.c - ldap ldbm back-end archive and restore entry points */

#include "back-ldbm.h"

int ldbm_back_archive2ldbm( Slapi_PBlock *pb )
{
    struct ldbminfo    *li;
    char *rawdirectory = NULL;    /* -a <directory> */
    char *directory = NULL;       /* normalized */
    char *backendname = NULL;
    int return_value = -1;
    int task_flags = 0;
    int run_from_cmdline = 0;
    Slapi_Task *task;
    int is_old_to_new = 0;

    slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
    slapi_pblock_get( pb, SLAPI_SEQ_VAL, &rawdirectory );
    slapi_pblock_get( pb, SLAPI_BACKEND_INSTANCE_NAME, &backendname);
    slapi_pblock_get( pb, SLAPI_BACKEND_TASK, &task );
    slapi_pblock_get( pb, SLAPI_TASK_FLAGS, &task_flags );
    li->li_flags = run_from_cmdline = (task_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE);

    if ( !rawdirectory || !*rawdirectory ) {
        LDAPDebug( LDAP_DEBUG_ANY, "archive2db: no archive name\n",
                   0, 0, 0 );
        return( -1 );
    }

    directory = rel2abspath(rawdirectory);

    /* check the current idl format vs backup DB version */
    if (idl_get_idl_new())
    {
        char *dbversion = NULL;
        char *dataversion = NULL;
        int value = 0;

        if (dbversion_read(li, directory, &dbversion, &dataversion) != 0)
        {
            LDAPDebug(LDAP_DEBUG_ANY, "Warning: Unable to read dbversion "
                      "file in %s\n", directory, 0, 0);
        }
        value = lookup_dbversion(dbversion, DBVERSION_TYPE);
        if (value & DBVERSION_OLD_IDL)
        {
            is_old_to_new = 1;
        }
        slapi_ch_free_string(&dbversion);
        slapi_ch_free_string(&dataversion);
    }

    /* No ldbm be's exist until we process the config information. */
    if (run_from_cmdline) {
        mapping_tree_init();
        ldbm_config_load_dse_info(li);
    } else {
        ldbm_instance *inst;
        Object *inst_obj, *inst_obj2;

        /* task does not support restore old idl onto new idl server */
        if (is_old_to_new)
        {
            LDAPDebug(LDAP_DEBUG_ANY,
                      "backup has old idl format; "
                      "to restore old formated backup onto the new server, "
                      "please use command line utility \"bak2db\" .\n",
                      0, 0, 0);
            if (task)
            {
                slapi_task_log_notice(task,
                      "backup has old idl format; "
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
                LDAPDebug(LDAP_DEBUG_ANY,
                          "ldbm: '%s' is already in the middle of "
                          "another task and cannot be disturbed.\n",
                          inst->inst_name, 0, 0);
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
                object_release(inst_obj2);
                object_release(inst_obj);
                goto out;
            }
        }

        /* now take down ALL BACKENDS */
        for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
             inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
            inst = (ldbm_instance *)object_get_data(inst_obj);
            LDAPDebug(LDAP_DEBUG_ANY, "Bringing %s offline...\n",
                      inst->inst_name, 0, 0);
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
        /* now we know nobody's using any of the backend instances, so we
         * can shutdown the dblayer -- this closes all instances too.
         * Use DBLAYER_RESTORE_MODE to prevent loss of perfctr memory.
         */
        dblayer_close(li, DBLAYER_RESTORE_MODE);
    }

    /* tell the database to restore */
    return_value = dblayer_restore(li, directory, task, backendname);
    if (0 != return_value) {
        LDAPDebug( LDAP_DEBUG_ANY,
                  "archive2db: Failed to read backup file set. "
                  "Either the directory specified doesn't exist, "
                  "or it exists but doesn't contain a valid backup set, "
                  "or file permissions prevent the server reading "
                  "the backup set.  error=%d (%s)\n",
                  return_value, dblayer_strerror(return_value), 0 );
        if (task) {
            slapi_task_log_notice(task, "Failed to read the backup file set "
                                  "from %s", directory);
        }
    }

    if (run_from_cmdline)
    {
        if (is_old_to_new)
        {
            /* does not exist */
            char *p;
            char c;
            char *bakup_dir = NULL;
            int skipinit = SLAPI_UPGRADEDB_SKIPINIT;

            p = strrchr(directory, '/');
            if (NULL == p)
            {
                p = strrchr(directory, '\\');
            }

            if (NULL == p)    /* never happen, I guess */
            {
                directory = ".";
                c = '/';
            }
            else
            {
                c = *p;
                *p = '\0';
            }
            bakup_dir = slapi_ch_smprintf("%s%ctmp_%010ld", directory, c, time(0));
            LDAPDebug( LDAP_DEBUG_ANY,
                      "archive2db: backup dir: %s\n", bakup_dir, 0, 0);
            *p = c;

            slapi_pblock_set( pb, SLAPI_SEQ_VAL, bakup_dir );
            slapi_pblock_set( pb, SLAPI_SEQ_TYPE, &skipinit );
            return_value = ldbm_back_upgradedb( pb );
        }
    }
    else
    {
        ldbm_instance *inst;
        Object *inst_obj;
        int ret;

        if (0 != return_value) {
            /* error case (607331)
             * just to go back to the previous state if possible */
            if ((return_value = dblayer_start(li, DBLAYER_NORMAL_MODE))) {
                LDAPDebug1Arg(LDAP_DEBUG_ANY,
                          "archive2db: Unable to to start database in [%s]\n", li->li_directory);
                if (task) {
                    slapi_task_log_notice(task, "Failed to start the database in "
                                          "%s", li->li_directory);
                }
                goto out;
            }
        }
        /* bring all backends back online */
        for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
             inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
            inst = (ldbm_instance *)object_get_data(inst_obj);
            ret = dblayer_instance_start(inst->inst_be, DBLAYER_NORMAL_MODE);
            if (ret != 0) {
                LDAPDebug(LDAP_DEBUG_ANY,
                          "archive2db: Unable to restart '%s'\n",
                          inst->inst_name, 0, 0);
                if (task) {
                    slapi_task_log_notice(task, "Unable to restart '%s'",
                                          inst->inst_name);
                }
            } else {
                slapi_mtn_be_enable(inst->inst_be);
                instance_set_not_busy(inst);
            }
        }
    }
out:
    slapi_ch_free_string(&directory);
    return return_value;
}

int ldbm_back_ldbm2archive( Slapi_PBlock *pb )
{
    struct ldbminfo    *li;
    char *rawdirectory = NULL;   /* -a <directory> */
    char *directory = NULL;   /* normalized */
    char *dir_bak = NULL;
    int return_value = -1;
    int task_flags = 0;
    int run_from_cmdline = 0;
    Slapi_Task *task;
    struct stat sbuf;

    slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
    slapi_pblock_get( pb, SLAPI_SEQ_VAL, &rawdirectory );
    slapi_pblock_get( pb, SLAPI_TASK_FLAGS, &task_flags );
    li->li_flags = run_from_cmdline = (task_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE);

    slapi_pblock_get( pb, SLAPI_BACKEND_TASK, &task );

    if ( !rawdirectory || !*rawdirectory ) {
        LDAPDebug( LDAP_DEBUG_ANY, "db2archive: no archive name\n",
                   0, 0, 0 );
        return -1;
    }

    /* start the database code up, do not attempt to perform recovery */
    if (run_from_cmdline) {
        /* No ldbm be's exist until we process the config information. */
        mapping_tree_init();
        ldbm_config_load_dse_info(li);
        if (0 != (return_value = 
                  dblayer_start(li,
                            DBLAYER_ARCHIVE_MODE|DBLAYER_NO_DBTHREADS_MODE))) {
            LDAPDebug(LDAP_DEBUG_ANY, "db2archive: Failed to init database\n",
                      0, 0, 0);
            if (task) {
                slapi_task_log_notice(task, "Failed to init database");
            }
            return -1;
        }
    }

    /* Initialize directory */
    directory = rel2abspath(rawdirectory);

    if (stat(directory, &sbuf) == 0) {
        int baklen = 0;

        if (slapd_comp_path(directory, li->li_directory) == 0) {
            LDAPDebug(LDAP_DEBUG_ANY,
                "db2archive: Cannot archive to the db directory.\n", 0, 0, 0);
            if (task) {
                slapi_task_log_notice(task, 
                                "Cannot archive to the db directory.");
            }
            return_value = -1;
            goto out;
        }

        baklen = strlen(directory) + 5; /* ".bak\0" */
        dir_bak = slapi_ch_malloc(baklen);
        PR_snprintf(dir_bak, baklen, "%s.bak", directory);
        LDAPDebug(LDAP_DEBUG_ANY, "db2archive: %s exists. Renaming to %s\n",
                                  directory, dir_bak, 0);
        if (task) {
            slapi_task_log_notice(task, "%s exists. Renaming to %s",
                                        directory, dir_bak);
        }
        if (stat(dir_bak, &sbuf) == 0) {
            return_value = ldbm_delete_dirs(dir_bak);
            if (0 != return_value) {
                LDAPDebug(LDAP_DEBUG_ANY,
                            "db2archive: %s exists and failed to delete it.\n",
                            dir_bak, 0, 0);
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
            LDAPDebug(LDAP_DEBUG_ANY,
                            "db2archive: Failed to rename \"%s\" to \"%s\".\n",
                            directory, dir_bak, 0);
            LDAPDebug(LDAP_DEBUG_ANY,
                            SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                            prerr, slapd_pr_strerror(prerr), 0);
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
    if (0 != MKDIR(directory,SLAPD_DEFAULT_DIR_MODE) && EEXIST != errno) {
        char *msg = dblayer_strerror(errno);

        LDAPDebug(LDAP_DEBUG_ANY,
                  "db2archive: mkdir(%s) failed; errno %i (%s)\n",
                  directory, errno, msg ? msg : "unknown");
        if (task) {
            slapi_task_log_notice(task,
                                  "mkdir(%s) failed; errno %i (%s)",
                                  directory, errno, msg ? msg : "unknown");
        }
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
                LDAPDebug(LDAP_DEBUG_ANY,
                          "ldbm: '%s' is already in the middle of "
                          "another task and cannot be disturbed.\n",
                          inst->inst_name, 0, 0);
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
                object_release(inst_obj2);
                object_release(inst_obj);
                goto err;
            }
        }
    }

    /* tell it to archive */
    return_value = dblayer_backup(li, directory, task);

    if (! run_from_cmdline) {
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
    if (return_value != 0) {
        LDAPDebug(LDAP_DEBUG_ANY, "db2archive: Rename %s back to %s\n",
                                  dir_bak, directory, 0);
        if (task) {
            slapi_task_log_notice(task, "Rename %s back to %s",
                                        dir_bak, directory);
        }
        ldbm_delete_dirs(directory);
        if (PR_SUCCESS != PR_Rename(dir_bak, directory)) {
            PRErrorCode prerr = PR_GetError();
            LDAPDebug(LDAP_DEBUG_ANY,
                            "db2archive: Failed to rename \"%s\" to \"%s\".\n",
                            dir_bak, directory, 0);
            LDAPDebug(LDAP_DEBUG_ANY,
                            SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                            prerr, slapd_pr_strerror(prerr), 0);
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
        0 != dblayer_close(li,DBLAYER_ARCHIVE_MODE|DBLAYER_NO_DBTHREADS_MODE)) {
        LDAPDebug(LDAP_DEBUG_ANY, "db2archive: Failed to close database\n",
                  0, 0, 0);
        if (task) {
            slapi_task_log_notice(task, "Failed to close database");
        }
    }

    slapi_ch_free_string(&dir_bak);
    slapi_ch_free_string(&directory);
    return return_value;
}

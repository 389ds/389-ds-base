/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* archive.c - ldap ldbm back-end archive and restore entry points */

#include "back-ldbm.h"

int ldbm_back_archive2ldbm( Slapi_PBlock *pb )
{
    struct ldbminfo    *li;
    char *directory = NULL;
	char *backendname = NULL;
    int return_value = -1;
    int task_flags = 0;
    int run_from_cmdline = 0;
    Slapi_Task *task;
    int is_old_to_new = 0;

    slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
    slapi_pblock_get( pb, SLAPI_SEQ_VAL, &directory );
	slapi_pblock_get( pb, SLAPI_BACKEND_INSTANCE_NAME, &backendname);
    slapi_pblock_get( pb, SLAPI_BACKEND_TASK, &task );
    slapi_pblock_get( pb, SLAPI_TASK_FLAGS, &task_flags );
    li->li_flags = run_from_cmdline = (task_flags & TASK_RUNNING_FROM_COMMANDLINE);

    /* check the current idl format vs backup DB version */
    if (idl_get_idl_new())
    {
        char dbversion[LDBM_VERSION_MAXBUF];
        char dataversion[LDBM_VERSION_MAXBUF];
        int value = 0;

        if (dbversion_read(li, directory, dbversion, dataversion) != 0)
        {
            LDAPDebug(LDAP_DEBUG_ANY, "Warning: Unable to read dbversion "
                      "file in %s\n", directory, 0, 0);
        }
        value = lookup_dbversion(dbversion, DBVERSION_TYPE);
        if (value & DBVERSION_OLD_IDL)
        {
            is_old_to_new = 1;
        }
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
                      "please use command line utility \"bak2db\" .\n");
            }
            return -1;
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
                        "another task and cannot be disturbed.\n",
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
                return -1;
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
            cache_clear(&inst->inst_cache);
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
            bakup_dir = (char *)slapi_ch_malloc(strlen(directory) + 
                                                 sizeof("tmp") + 13);
            sprintf(bakup_dir, "%s%ctmp_%010d", directory, c, time(0));
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
            dblayer_start(li, DBLAYER_NORMAL_MODE); 
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

    return return_value;
}

int ldbm_back_ldbm2archive( Slapi_PBlock *pb )
{
    struct ldbminfo    *li;
    char *directory = NULL;
    int return_value = -1;
    int task_flags = 0;
    int run_from_cmdline = 0;
    Slapi_Task *task;

    slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
    slapi_pblock_get( pb, SLAPI_SEQ_VAL, &directory );
    slapi_pblock_get( pb, SLAPI_TASK_FLAGS, &task_flags );
    li->li_flags = run_from_cmdline = (task_flags & TASK_RUNNING_FROM_COMMANDLINE);

    slapi_pblock_get( pb, SLAPI_BACKEND_TASK, &task );

    /* No ldbm be's exist until we process the config information. */
    if (run_from_cmdline) {
        mapping_tree_init();
        ldbm_config_load_dse_info(li);
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
                        "another task and cannot be disturbed.\n",
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
                return -1;
            }
        }
    }

    if ( !directory || !*directory ) {
        LDAPDebug( LDAP_DEBUG_ANY, "db2archive: no archive name\n",
                   0, 0, 0 );
        return( -1 );
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
    }

    /* start the database code up, do not attempt to perform recovery */
    if (run_from_cmdline &&
        0 != dblayer_start(li,DBLAYER_ARCHIVE_MODE|DBLAYER_CMDLINE_MODE)) {
        LDAPDebug(LDAP_DEBUG_ANY, "db2archive: Failed to init database\n",
                  0, 0, 0);
        if (task) {
            slapi_task_log_notice(task, "Failed to init database");
        }
        return( -1 );
    }

    /* tell it to archive */
    return_value = dblayer_backup(li, directory, task);

    /* close the database down again */
    if (run_from_cmdline &&
        0 != dblayer_close(li,DBLAYER_ARCHIVE_MODE|DBLAYER_CMDLINE_MODE)) {
        LDAPDebug(LDAP_DEBUG_ANY, "db2archive: Failed to close database\n",
                  0, 0, 0);
        if (task) {
            slapi_task_log_notice(task, "Failed to close database");
        }
        
        /* The backup succeeded, so a failed close is not really a
           total error... */
        /*return( -1 );*/
    }

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

    return return_value;
}

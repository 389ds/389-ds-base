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

/* dbmdb_ldif2db.c
 *
 * common functions for import (old and new) and export
 * the export code (db2ldif)
 * code for db2index (is this still in use?)
 */

#include "mdb_layer.h"
#include "../vlv_srch.h"

#define DB2INDEX_ANCESTORID 0x1   /* index ancestorid */
#define DB2INDEX_ENTRYRDN 0x2     /* index entryrdn */
#define DB2LDIF_ENTRYRDN 0x4      /* export entryrdn */
#define DB2INDEX_OBJECTCLASS 0x10 /* for reindexing "objectclass: nstombstone" */

#define LDIF2LDBM_EXTBITS(x) ((x)&0xf)

typedef struct _export_args
{
    struct backentry *ep;
    int decrypt;
    int options;
    int printkey;
    IDList *idl;
    NIDS idindex;
    ID lastid;
    int fd;
    Slapi_Task *task;
    char **include_suffix;
    char **exclude_suffix;
    int *cnt;
    int *lastcnt;
    IDList *pre_exported_idl; /* exported IDList, which ID is larger than
                                 its children's ID.  It happens when an entry
                                 is added and existing entries are moved under
                                 the newly added entry. */
} export_args;

/* static functions */

static int dbmdb_ldbm_exclude_attr_from_export(struct ldbminfo *li,
                                         const char *attr,
                                         int dump_uniqueid);

static int _get_and_add_parent_rdns(backend *be, dbmdb_cursor_t *cur, ID id, Slapi_RDN *srdn, ID *pid, int index_ext, int run_from_cmdline, export_args *eargs);
static int _export_or_index_parents(ldbm_instance *inst, dbmdb_cursor_t *cur, ID currentid, char *rdn, ID id, ID pid, int run_from_cmdline, struct _export_args *eargs, int type, Slapi_RDN *psrdn);

/**********  common routines for classic/deluxe import code **********/

static size_t import_config_index_buffer_size = DEFAULT_IMPORT_INDEX_BUFFER_SIZE;

void
dbmdb_import_configure_index_buffer_size(size_t size)
{
    import_config_index_buffer_size = size;
}

size_t
dbmdb_import_get_index_buffer_size()
{
    return import_config_index_buffer_size;
}


void
dbmdb_back_free_incl_excl(char **include, char **exclude)
{
    if (include) {
        charray_free(include);
    }
    if (exclude) {
        charray_free(exclude);
    }
}


/*
 * dbmdb_add_op_attrs - add the parentid, entryid, dncomp,
 * and entrydn operational attributes to an entry.
 * Also---new improved washes whiter than white version
 * now removes any bogus operational attributes you're not
 * allowed to specify yourself on entries.
 * Currenty the list of these is: numSubordinates, hasSubordinates
 */
int
dbmdb_add_op_attrs(Slapi_PBlock *pb, struct ldbminfo *li __attribute__((unused)), struct backentry *ep, int *status)
{
    backend *be;
    char *pdn;
    ID pid = 0;
    int save_old_pid = 0;
    int is_tombstone = 0;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);

    /*
     * add the parentid and entryid operational attributes
     */

    if (NULL != status) {
        if (IMPORT_ADD_OP_ATTRS_SAVE_OLD_PID == *status) {
            save_old_pid = 1;
        }
        *status = IMPORT_ADD_OP_ATTRS_OK;
    }

    is_tombstone = slapi_entry_flag_is_set(ep->ep_entry,
                                           SLAPI_ENTRY_FLAG_TOMBSTONE);
    /* parentid */
    if ((pdn = slapi_dn_parent_ext(backentry_get_ndn(ep), is_tombstone)) != NULL) {
        int err = 0;

        /*
         * read the entrydn/entryrdn index to get the id of the parent
         * If this entry's parent is not present in the index,
         * we'll get a MDB_NOTFOUND error here.
         * In olden times, we just ignored this, but now...
         * we see this as meaning that the entry is either a
         * suffix entry, or its erroneous. So, we signal this to the
         * caller via the status parameter.
         */
        if (entryrdn_get_switch()) { /* subtree-rename: on */
            Slapi_DN sdn;
            slapi_sdn_init(&sdn);
            slapi_sdn_set_dn_byval(&sdn, pdn);
            err = entryrdn_index_read_ext(be, &sdn, &pid,
                                          TOMBSTONE_INCLUDED, NULL);
            slapi_sdn_done(&sdn);
            if (DBI_RC_NOTFOUND == err) {
                /*
                 * Could be a tombstone. E.g.,
                 * nsuniqueid=042d8081-..-ca8fe9f7,uid=tuser,o=abc,com
                 * If so, need to get the grandparent of the leaf.
                 */
                if (slapi_entry_flag_is_set(ep->ep_entry,
                                            SLAPI_ENTRY_FLAG_TOMBSTONE) &&
                    (0 == strncasecmp(pdn, SLAPI_ATTR_UNIQUEID,
                                      sizeof(SLAPI_ATTR_UNIQUEID) - 1))) {
                    char *ppdn = slapi_dn_parent(pdn);
                    slapi_ch_free_string(&pdn);
                    if (NULL == ppdn) {
                        if (NULL != status) {
                            *status = IMPORT_ADD_OP_ATTRS_NO_PARENT;
                            goto next;
                        }
                    }
                    pdn = ppdn;
                    slapi_sdn_set_dn_byval(&sdn, pdn);
                    err = entryrdn_index_read(be, &sdn, &pid, NULL);
                    slapi_sdn_done(&sdn);
                }
            }
            if (err) {
                if (DBI_RC_NOTFOUND != err) {
                    slapi_log_err(SLAPI_LOG_ERR, "dbmdb_add_op_attrs", "database error %d\n", err);
                    slapi_ch_free_string(&pdn);
                    return (-1);
                }
                if (NULL != status) {
                    *status = IMPORT_ADD_OP_ATTRS_NO_PARENT;
                }
            }
        } else {
            struct berval bv;
            IDList *idl = NULL;
            bv.bv_val = pdn;
            bv.bv_len = strlen(pdn);
            if ((idl = index_read(be, LDBM_ENTRYDN_STR, indextype_EQUALITY,
                                  &bv, NULL, &err)) != NULL) {
                pid = idl_firstid(idl);
                idl_free(&idl);
            } else {
                /* empty idl */
                if (0 != err && DBI_RC_NOTFOUND != err) {
                    slapi_log_err(SLAPI_LOG_ERR, "dbmdb_add_op_attrs", "database error %d\n", err);
                    slapi_ch_free_string(&pdn);
                    return (-1);
                }
                if (NULL != status) {
                    *status = IMPORT_ADD_OP_ATTRS_NO_PARENT;
                }
            }
        }
        slapi_ch_free_string(&pdn);
    } else {
        if (NULL != status) {
            *status = IMPORT_ADD_OP_ATTRS_NO_PARENT;
        }
    }
next:
    /* Get rid of attributes you're not allowed to specify yourself */
    slapi_entry_delete_values(ep->ep_entry, hassubordinates, NULL);
    slapi_entry_delete_values(ep->ep_entry, numsubordinates, NULL);

    /* Upgrade DN format only */
    /* Set current parentid to e_aux_attrs to remove it from the index file. */
    if (save_old_pid) {
        Slapi_Attr *pid_attr = NULL;
        pid_attr = attrlist_remove(&ep->ep_entry->e_attrs, "parentid");
        if (pid_attr) {
            attrlist_add(&ep->ep_entry->e_aux_attrs, pid_attr);
        }
    }

    /* Add the entryid, parentid and entrydn operational attributes */
    /* Note: This function is provided by the Add code */
    add_update_entry_operational_attributes(ep, pid);

    return (0);
}


/**********  ldif2db entry point  **********/

/*
    Some notes about this stuff:

    The front-end does call our init routine before calling us here.
    So, we get the regular chance to parse the config file etc.
    However, it does _NOT_ call our start routine, so we need to
    do whatever work that did and which we need for this work , here.
    Furthermore, the front-end simply exits after calling us, so we need
    to do any cleanup work here also.
 */

/*
 * dbmdb_ldif2db - backend routine to convert an ldif file to
 * a database.
 */
int
dbmdb_ldif2db(Slapi_PBlock *pb)
{
    struct ldbminfo *li;
    ldbm_instance *inst = NULL;
    char *instance_name;
    Slapi_Task *task = NULL;
    int ret, task_flags;

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_BACKEND_INSTANCE_NAME, &instance_name);
    slapi_pblock_get(pb, SLAPI_TASK_FLAGS, &task_flags);
    slapi_pblock_get(pb, SLAPI_BACKEND_TASK, &task);

    /* BEGIN complex dependencies of various initializations. */
    /* hopefully this will go away once import is not run standalone... */


    /* Find the instance that the ldif2db will be done on. */
    inst = ldbm_instance_find_by_name(li, instance_name);
    if (NULL == inst) {
        slapi_task_log_notice(task, "Unknown ldbm instance %s", instance_name);
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ldif2db", "Unknown ldbm instance %s\n",
                      instance_name);
        return -1;
    }

    /* check if an import/restore is already ongoing... */
    if ((instance_set_busy(inst) != 0)) {
        slapi_task_log_notice(task,
                "Backend instance '%s' already in the middle of  another task",
                inst->inst_name);
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ldif2db", "ldbm: '%s' is already in the middle of "
                                                            "another task and cannot be disturbed.\n",
                      inst->inst_name);
        return -1;
    } else {
        uint64_t refcnt;
        refcnt = slapi_counter_get_value(inst->inst_ref_count);
        if (refcnt > 0) {
            slapi_task_log_notice(task,
                    "Backend instance '%s': there are %" PRIu64 " pending operation(s)."
                    " Import can not proceed until they are completed.\n",
                    inst->inst_name, refcnt);
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ldif2db",
                    "ldbm: '%s' there are %" PRIu64 " pending operation(s)."
                     " Import can not proceed until they are completed.\n",
                    inst->inst_name, refcnt);
            instance_set_not_busy(inst);
            return -1;
        }
    }

    if ((task_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE)) {
        if ((ret = dbmdb_import_file_init(inst))) {
            slapi_task_log_notice(task,
                    "Backend instance '%s' Failed to write import file, error %d: %s",
                    inst->inst_name, ret, slapd_pr_strerror(ret));
            slapi_log_err(SLAPI_LOG_ERR,
                    "dbmdb_ldif2db", "%s: Failed to write import file, error %d: %s\n",
                    inst->inst_name, ret, slapd_pr_strerror(ret));
            return -1;
        }
    }

    /***** prepare & init lmdb and dblayer *****/

    if (!(task_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE)) {
        /* shutdown this instance of the db */
        slapi_log_err(SLAPI_LOG_INFO, "dbmdb_ldif2db", "Bringing %s offline...\n",
                      instance_name);
        slapi_mtn_be_disable(inst->inst_be);

        cache_clear(&inst->inst_cache, CACHE_TYPE_ENTRY);
        if (entryrdn_get_switch()) {
            cache_clear(&inst->inst_dncache, CACHE_TYPE_DN);
        }
        dblayer_instance_close(inst->inst_be);
        dbmdb_delete_indices(inst);
    } else {
        if (dbmdb_ctx_t_internal_set(li, CONFIG_DB_TRANSACTION_LOGGING, "off")) {
            goto fail;
        }

        /* If USN plugin is enabled,
         * initialize the USN counter to get the next USN */
        if (plugin_enabled("USN", li->li_identity)) {
            /* close immediately; no need to run db threads */
            ret = dbmdb_start(li,
                                DBLAYER_NORMAL_MODE | DBLAYER_NO_DBTHREADS_MODE);
            if (ret) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "dbmdb_ldif2db", "dbmdb_start failed! %s (%d)\n",
                              dblayer_strerror(ret), ret);
                goto fail;
            }
            /* initialize the USN counter */
            ldbm_usn_init(li);
            ret = dblayer_close(li, DBLAYER_NORMAL_MODE);
            if (ret != 0) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "dbmdb_ldif2db", "dblayer_close failed! %s (%d)\n",
                              dblayer_strerror(ret), ret);
            }
        }

        if (0 != (ret = dbmdb_start(li, DBLAYER_IMPORT_MODE))) {
            if (LDBM_OS_ERR_IS_DISKFULL(ret)) {
                slapi_log_err(SLAPI_LOG_ALERT, "dbmdb_ldif2db", "Failed to init database.  "
                                                                      "There is either insufficient disk space or "
                                                                      "insufficient memory available to initialize the "
                                                                      "database.\n");
                slapi_log_err(SLAPI_LOG_ALERT, "dbmdb_ldif2db", "Please check that\n"
                                                                      "1) disks are not full,\n"
                                                                      "2) no file exceeds the file size limit,\n"
                                                                      "3) the configured dbcachesize is not too large for the available memory on this machine.\n");
            } else {
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ldif2db", "Failed to init database "
                                                                    "(error %d: %s)\n",
                              ret, dblayer_strerror(ret));
            }
            goto fail;
        }
    }

    /* Delete old database files */
    dbmdb_delete_instance_dir(inst->inst_be);
    /* it's okay to fail -- the directory might have already been deleted */

    /* dbmdb_instance_start will init the id2entry index. */
    /* it also (finally) fills in inst_dir_name */
    ret = dbmdb_instance_start(inst->inst_be, DBLAYER_IMPORT_MODE);
    if (ret != 0) {
        goto fail;
    }

    vlv_init(inst);

    /***** done init limdb and dblayer *****/

    /* always use "new" import code now */
    slapi_pblock_set(pb, SLAPI_BACKEND, inst->inst_be);
    ret = dbmdb_back_ldif2db(pb);
    if (task_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE) {
        dblayer_close(li, DBLAYER_IMPORT_MODE);
    }
    if (ret == 0) {
        if (task_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE) {
            dbmdb_import_file_update(inst);
        } else {
            slapi_be_set_flag(inst->inst_be, SLAPI_BE_FLAG_POST_IMPORT);
        }
    }
    return ret;

fail:
    /* DON'T enable the backend -- leave it offline */
    instance_set_not_busy(inst);
    return ret;
}


/**********  db2ldif, db2index  **********/


/* fetch an IDL for the series of subtree specs */
/* (used for db2ldif) */
static IDList *
dbmdb_fetch_subtrees(backend *be, char **include, int *err)
{
    int i;
    ID id;
    IDList *idltotal = NULL, *idltmp;
    back_txn *txn = NULL;
    struct berval bv;
    Slapi_DN sdn; /* Used only if entryrdn_get_switch is true */

    *err = 0;
    slapi_sdn_init(&sdn);
    /* for each subtree spec... */
    for (i = 0; include[i]; i++) {
        IDList *idl = NULL;
        const char *suffix = slapi_sdn_get_ndn(slapi_be_getsuffix(be, 0));
        char *parentdn = slapi_ch_strdup(suffix);
        char *nextdn = NULL;
        int matched = 0;
        int issubsuffix = 0;

        /*
         * avoid a case that an include suffix is applied to the backend of
         * its sub suffix
         * e.g., suffix: dc=example,dc=com (backend userRoot)
         *       sub suffix: ou=sub,dc=example,dc=com (backend subUserRoot)
         * When this CLI db2ldif -s "dc=example,dc=com" is executed,
         * skip checking "dc=example,dc=com" in entrydn of subUserRoot.
         */
        while (NULL != parentdn &&
               NULL != (nextdn = slapi_dn_parent(parentdn))) {
            slapi_ch_free_string(&parentdn);
            if (0 == slapi_UTF8CASECMP(nextdn, include[i])) {
                issubsuffix = 1; /* suffix of be is a subsuffix of include[i] */
                break;
            }
            parentdn = nextdn;
        }
        slapi_ch_free_string(&parentdn);
        slapi_ch_free_string(&nextdn);
        if (issubsuffix) {
            continue;
        }

        /*
         * avoid a case that an include suffix is applied to the unrelated
         * backend.
         * e.g., suffix: dc=example,dc=com (backend userRoot)
         *       suffix: dc=test,dc=com (backend testRoot))
         * When this CLI db2ldif -s "dc=example,dc=com" is executed,
         * skip checking "dc=example,dc=com" in entrydn of testRoot.
         */
        parentdn = slapi_ch_strdup(include[i]);
        while (NULL != parentdn &&
               NULL != (nextdn = slapi_dn_parent(parentdn))) {
            slapi_ch_free_string(&parentdn);
            if (0 == slapi_UTF8CASECMP(nextdn, (char *)suffix)) {
                matched = 1;
                break;
            }
            parentdn = nextdn;
        }
        slapi_ch_free_string(&parentdn);
        slapi_ch_free_string(&nextdn);
        if (!matched) {
            continue;
        }

        /*
         * First map the suffix to its entry ID.
         * Note that the suffix is already normalized.
         */
        if (entryrdn_get_switch()) { /* subtree-rename: on */
            slapi_sdn_set_dn_byval(&sdn, include[i]);
            *err = entryrdn_index_read(be, &sdn, &id, NULL);
            if (*err) {
                if (MDB_NOTFOUND == *err) {
                    slapi_log_err(SLAPI_LOG_INFO,
                                  "dbmdb_fetch_subtrees", "entryrdn not indexed on '%s'; "
                                                         "entry %s may not be added to the database yet.\n",
                                  include[i], include[i]);
                    *err = 0; /* not a problem */
                } else {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "dbmdb_fetch_subtrees", "Reading %s failed on entryrdn; %d\n",
                                  include[i], *err);
                }
                slapi_sdn_done(&sdn);
                continue;
            }
        } else {
            bv.bv_val = include[i];
            bv.bv_len = strlen(include[i]);
            idl = index_read(be, LDBM_ENTRYDN_STR, indextype_EQUALITY, &bv, txn, err);
            if (idl == NULL) {
                if (MDB_NOTFOUND == *err) {
                    slapi_log_err(SLAPI_LOG_INFO,
                                  "dbmdb_fetch_subtrees", "entrydn not indexed on '%s'; "
                                                         "entry %s may not be added to the database yet.\n",
                                  include[i], include[i]);
                    *err = 0; /* not a problem */
                } else {
                    slapi_log_err(SLAPI_LOG_ERR, "dbmdb_fetch_subtrees",
                                  "Reading %s failed on entrydn; %d\n",
                                  include[i], *err);
                }
                continue;
            }
            id = idl_firstid(idl);
            idl_free(&idl);
        }

        /*
         * Now get all the descendants of that suffix.
         */
        if (entryrdn_get_noancestorid()) {
            /* subtree-rename: on && no ancestorid */
            *err = entryrdn_get_subordinates(be, &sdn, id, &idl, txn, 0);
        } else {
            *err = ldbm_ancestorid_read(be, txn, id, &idl);
        }
        slapi_sdn_done(&sdn);
        if (idl == NULL) {
            if (MDB_NOTFOUND == *err) {
                slapi_log_err(SLAPI_LOG_BACKLDBM,
                              "dbmdb_fetch_subtrees", "Entry id %u has no descendants according to %s. "
                                                     "Index file created by this reindex will be empty.\n",
                              id, entryrdn_get_noancestorid() ? "entryrdn" : "ancestorid");
                *err = 0; /* not a problem */
            } else {
                slapi_log_err(SLAPI_LOG_WARNING,
                              "dbmdb_fetch_subtrees", "%s not indexed on %u\n",
                              entryrdn_get_noancestorid() ? "entryrdn" : "ancestorid", id);
            }
            continue;
        }

        /* Insert the suffix itself */
        idl_insert(&idl, id);

        /* Merge the idlists */
        if (!idltotal) {
            idltotal = idl;
        } else if (idl) {
            idltmp = idl_union(be, idltotal, idl);
            idl_free(&idltotal);
            idl_free(&idl);
            idltotal = idltmp;
        }
    } /* for (i = 0; include[i]; i++) */

    return idltotal;
}


static int
dbmdb_export_one_entry(struct ldbminfo *li,
                 ldbm_instance *inst,
                 export_args *expargs)
{
    backend *be = inst->inst_be;
    int rc = 0;
    Slapi_Attr *this_attr = NULL, *next_attr = NULL;
    char *type = NULL;
    MDB_val data = {0};
    int len = 0;

    if (!dbmdb_back_ok_to_dump(backentry_get_ndn(expargs->ep),
                              expargs->include_suffix,
                              expargs->exclude_suffix)) {
        goto bail; /* go to next loop */
    }
    if (!(expargs->options & SLAPI_DUMP_STATEINFO) &&
        slapi_entry_flag_is_set(expargs->ep->ep_entry,
                                SLAPI_ENTRY_FLAG_TOMBSTONE)) {
        /* We only dump the tombstones if the user needs to create
         * a replica from the ldif */
        goto bail; /* go to next loop */
    }
    (*expargs->cnt)++;

    /* do not output attributes that are in the "exclude" list */
    /* Also, decrypt any encrypted attributes, if we're asked to */
    rc = slapi_entry_first_attr(expargs->ep->ep_entry, &this_attr);
    while (0 == rc) {
        int dump_uniqueid = (expargs->options & SLAPI_DUMP_UNIQUEID) ? 1 : 0;
        rc = slapi_entry_next_attr(expargs->ep->ep_entry,
                                   this_attr, &next_attr);
        slapi_attr_get_type(this_attr, &type);
        if (dbmdb_ldbm_exclude_attr_from_export(li, type, dump_uniqueid)) {
            slapi_entry_delete_values(expargs->ep->ep_entry, type, NULL);
        }
        this_attr = next_attr;
    }
    if (expargs->decrypt) {
        /* Decrypt in place */
        rc = attrcrypt_decrypt_entry(be, expargs->ep);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_export_one_entry", "Failed to decrypt entry [%s] : %d\n",
                          slapi_sdn_get_dn(&expargs->ep->ep_entry->e_sdn), rc);
        }
    }
    /*
     * Check if userPassword value is hashed or not.
     * If it is not, put "{CLEAR}" in front of the password value.
     */
    {
        char *pw = slapi_entry_attr_get_charptr(expargs->ep->ep_entry,
                                                "userpassword");
        if (pw && !slapi_is_encoded(pw)) {
            /* clear password does not have {CLEAR} storage scheme */
            struct berval *vals[2];
            struct berval val;
            val.bv_val = slapi_ch_smprintf("{CLEAR}%s", pw);
            val.bv_len = strlen(val.bv_val);
            vals[0] = &val;
            vals[1] = NULL;
            rc = slapi_entry_attr_replace(expargs->ep->ep_entry,
                                          "userpassword", vals);
            if (rc) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "dbmdb_export_one_entry", "%s: Failed to add clear password storage scheme: %d\n",
                              slapi_sdn_get_dn(&expargs->ep->ep_entry->e_sdn), rc);
            }
            slapi_ch_free_string(&val.bv_val);
        }
        slapi_ch_free_string(&pw);
    }
    data.mv_data = slapi_entry2str_with_options(expargs->ep->ep_entry,
                                             &len, expargs->options);
    data.mv_size = len + 1;

    if (expargs->printkey & EXPORT_PRINTKEY) {
        char idstr[32];

        sprintf(idstr, "# entry-id: %lu\n", (u_long)expargs->ep->ep_id);
        rc = write(expargs->fd, idstr, strlen(idstr));
        PR_ASSERT(rc > 0);
    }
    rc = write(expargs->fd, data.mv_data, len);
    PR_ASSERT(rc > 0);
    rc = write(expargs->fd, "\n", 1);
    PR_ASSERT(rc > 0);
    rc = 0;
    if ((*expargs->cnt) % 1000 == 0) {
        int percent;

        if (expargs->idl) {
            percent = (expargs->idindex * 100 / expargs->idl->b_nids);
        } else {
            percent = (expargs->ep->ep_id * 100 / expargs->lastid);
        }
        if (expargs->task) {
            slapi_task_log_status(expargs->task,
                                  "%s: Processed %d entries (%d%%).",
                                  inst->inst_name, *expargs->cnt, percent);
            slapi_task_log_notice(expargs->task,
                                  "%s: Processed %d entries (%d%%).",
                                  inst->inst_name, *expargs->cnt, percent);
        }
        slapi_log_err(SLAPI_LOG_INFO, "dbmdb_export_one_entry", "export %s: Processed %d entries (%d%%).\n",
                      inst->inst_name, *expargs->cnt, percent);
        *expargs->lastcnt = *expargs->cnt;
    }
bail:
    return rc;
}

/*
 * dbmdb_db2ldif - backend routine to convert database to an
 * ldif file.
 * (reunified at last)
 */
#define LDBM2LDIF_BUSY (-2)
#define RUVRDN SLAPI_ATTR_UNIQUEID "=" RUV_STORAGE_ENTRY_UNIQUEID
int
dbmdb_db2ldif(Slapi_PBlock *pb)
{
    backend *be = NULL;
    struct ldbminfo *li = NULL;
    dbi_db_t *db = NULL;
    struct backentry *ep;
    struct backentry *pending_ruv = NULL;
    MDB_val key = {0};
    MDB_val data = {0};
    char *fname = NULL;
    int printkey, rc, ok_index;
    int return_value = 0;
    int nowrap = 0;
    int nobase64 = 0;
    NIDS idindex = 0;
    ID temp_id;
    char **exclude_suffix = NULL;
    char **include_suffix = NULL;
    int decrypt = 0;
    int32_t dump_replica = 0;
    int dump_uniqueid = 1;
    int dump_changelog = 0;
    int fd = STDOUT_FILENO;
    IDList *idl = NULL; /* optimization for -s include lists */
    int cnt = 0, lastcnt = 0;
    int options = 0;
    int keepgoing = 1;
    int isfirst = 1;
    int appendmode = 0;
    int appendmode_1 = 0;
    int noversion = 0;
    ID lastid = 0;
    int task_flags;
    Slapi_Task *task;
    int run_from_cmdline = 0;
    char *instance_name = NULL;
    ldbm_instance *inst = NULL;
    int str2entry_options = 0;
    int we_start_the_backends = 0;
    int server_startcfg;
    export_args eargs = {0};
    int32_t suffix_written = 0;
    int32_t skip_ruv = 0;
    dbmdb_cursor_t cur = {0};
    uint size = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_db2ldif", "=>\n");

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_TASK_FLAGS, &task_flags);
    slapi_pblock_get(pb, SLAPI_DB2LDIF_DECRYPT, &decrypt);
    slapi_pblock_get(pb, SLAPI_DB2LDIF_SERVER_RUNNING, &server_startcfg);
    slapi_pblock_get(pb, SLAPI_BACKEND_TASK, &task);
    run_from_cmdline = (task_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE);

    dump_replica = slapi_pblock_get_ldif_dump_replica(pb);
    if (run_from_cmdline) {
        li->li_flags |= SLAPI_TASK_RUNNING_FROM_COMMANDLINE;
        if (!dump_replica) {
            we_start_the_backends = 1;
        }
    }

    if (run_from_cmdline && server_startcfg) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_db2ldif", "Cannot export the database while the server "
                      "is startcfg, please use dsconf\n");
        return_value = -1;
        goto bye;
    }

    slapi_pblock_get(pb, SLAPI_BACKEND_INSTANCE_NAME, &instance_name);
    if (run_from_cmdline) {

        /* Now that we have processed the config information, we look for
         * the be that should do the db2ldif. */
        inst = ldbm_instance_find_by_name(li, instance_name);
        if (NULL == inst) {
            slapi_task_log_notice(task, "Unknown backend instance: %s", instance_name);
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2ldif", "Unknown backend instance: %s\n",
                          instance_name);
            return_value = -1;
            goto bye;
        }
        /* [605974] command db2ldif should not be able to run when on-line
         * import is startcfg */
        if (dblayer_in_import(inst)) {
            slapi_task_log_notice(task, "Backend instance '%s' is busy", instance_name);
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2ldif", "Backend instance '%s' is busy\n",
                          instance_name);
            return_value = -1;
            goto bye;
        }

        /* store the be in the pb */
        be = inst->inst_be;
        slapi_pblock_set(pb, SLAPI_BACKEND, be);
    } else {
        slapi_pblock_get(pb, SLAPI_BACKEND, &be);
        if (!be) {
            slapi_task_log_notice(task, "No backend for: %s", instance_name);
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2ldif", "No backend for: %s\n", instance_name);
            return_value = -1;
            goto bye;
        }
        inst = (ldbm_instance *)be->be_instance_info;
        if (!inst) {
            slapi_task_log_notice(task, "Unknown backend instance: %s",instance_name);
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2ldif", "Unknown backend instance: %s\n",instance_name);
            return_value = -1;
            goto bye;
        }

        /* check if an import/restore is already ongoing... */
        if (instance_set_busy(inst) != 0) {
            slapi_task_log_notice(task,
                    "Backend instance '%s' is already in the middle of another task and cannot be disturbed.",
                    inst->inst_name);
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2ldif",
                          "Backend instance '%s' is already in the middle of another task and cannot be disturbed.\n",
                          inst->inst_name);
            return_value = LDBM2LDIF_BUSY;
            goto bye;
        }
    }

    dbmdb_back_fetch_incl_excl(pb, &include_suffix, &exclude_suffix);

    str2entry_options = (dump_replica ? 0 : SLAPI_STR2ENTRY_TOMBSTONE_CHECK);

    slapi_pblock_get(pb, SLAPI_DB2LDIF_FILE, &fname);
    slapi_pblock_get(pb, SLAPI_DB2LDIF_PRINTKEY, &printkey);
    slapi_pblock_get(pb, SLAPI_DB2LDIF_DUMP_UNIQUEID, &dump_uniqueid);
    slapi_pblock_get(pb, SLAPI_LDIF_CHANGELOG, &dump_changelog);

    /* tsk, overloading printkey.  shame on me. */
    ok_index = !(printkey & EXPORT_ID2ENTRY_ONLY);
    printkey &= ~EXPORT_ID2ENTRY_ONLY;

    nobase64 = (printkey & EXPORT_MINIMAL_ENCODING);
    printkey &= ~EXPORT_MINIMAL_ENCODING;
    nowrap = (printkey & EXPORT_NOWRAP);
    printkey &= ~EXPORT_NOWRAP;
    appendmode = (printkey & EXPORT_APPENDMODE);
    printkey &= ~EXPORT_APPENDMODE;
    appendmode_1 = (printkey & EXPORT_APPENDMODE_1);
    printkey &= ~EXPORT_APPENDMODE_1;
    noversion = (printkey & EXPORT_NOVERSION);
    printkey &= ~EXPORT_NOVERSION;

    /* decide whether to dump uniqueid */
    if (dump_uniqueid)
        options |= SLAPI_DUMP_UNIQUEID;
    if (nowrap)
        options |= SLAPI_DUMP_NOWRAP;
    if (nobase64)
        options |= SLAPI_DUMP_MINIMAL_ENCODING;
    if (dump_replica)
        options |= SLAPI_DUMP_STATEINFO;

    if (fname == NULL) {
        slapi_task_log_notice(task, "%s: no LDIF filename supplied.", inst->inst_name);
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2ldif", "db2ldif: no LDIF filename supplied\n");
        return_value = -1;
        goto bye;
    }

    if (strcmp(fname, "-")) { /* not '-' */
        if (appendmode) {
            if (appendmode_1) {
                fd = dbmdb_open_huge_file(fname, O_WRONLY | O_CREAT | O_TRUNC,
                                            SLAPD_DEFAULT_FILE_MODE);
            } else {
                fd = dbmdb_open_huge_file(fname, O_WRONLY | O_CREAT | O_APPEND,
                                            SLAPD_DEFAULT_FILE_MODE);
            }
        } else {
            /* open it */
            fd = dbmdb_open_huge_file(fname, O_WRONLY | O_CREAT | O_TRUNC,
                                        SLAPD_DEFAULT_FILE_MODE);
        }
        if (fd < 0) {
            slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
            slapi_task_log_notice(task,
                    "Backend %s: can't open %s: error %d (%s) while startcfg as user \"%s\"",
                    inst->inst_name, fname, errno, slapi_system_strerror(errno), slapdFrontendConfig->localuserinfo->pw_name);
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2ldif",
                    "db2ldif: %s: can't open %s: error %d (%s) while startcfg as user \"%s\"\n",
                    inst->inst_name, fname, errno, slapi_system_strerror(errno), slapdFrontendConfig->localuserinfo->pw_name);
            we_start_the_backends = 0;
            return_value = -1;
            goto bye;
        }
    } else { /* '-' */
        fd = STDOUT_FILENO;
    }

    if (we_start_the_backends) {
        if (0 != dbmdb_start(li, DBLAYER_EXPORT_MODE)) {
            slapi_task_log_notice(task, "Failed to init database for: %s", inst->inst_name);
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2ldif",
                    "db2ldif: Failed to init database: %s\n",
                    inst->inst_name);
            return_value = -1;
            goto bye;
        }
        /* dbmdb_instance_start will init the id2entry index. */
        if (0 != dbmdb_instance_start(be, DBLAYER_EXPORT_MODE)) {
            slapi_task_log_notice(task, "Failed to start database instance: %s", inst->inst_name);
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2ldif",
                    "db2ldif: Failed to start database instance: %s\n",
                    inst->inst_name);
            return_value = -1;
            goto bye;
        }
    }

    /* idl manipulation requires nextid to be init'd now */
    if (include_suffix && ok_index)
        get_ids_from_disk(be);

    if (((dblayer_get_id2entry(be, &db)) != 0) || (db == NULL)) {
        slapi_task_log_notice(task,
                "Backend instance '%s' Unable to open/create database(id2entry)",
                inst->inst_name);
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2ldif",
                "Could not open/create id2entry for: %s\n",
                inst->inst_name);
        return_value = -1;
        goto bye;
    }

    /* if an include_suffix was given (and we're pretty sure the
     * entrydn and ancestorid indexes are valid), we try to
     * assemble an id-list of candidates instead of plowing thru
     * the whole database.  this is a big performance improvement
     * when exporting config info (which is usually on the order
     * of 100 entries) from a database that may be on the order of
     * GIGS in size.
     */
    {
        /* Here, we assume that the table is ordered in EID-order,
         * which it is !
         */
        /* get a cursor to we can walk over the table */
        return_value = dbmdb_open_cursor(&cur, MDB_CONFIG(li), db, MDB_RDONLY);
        if (0 != return_value) {
            slapi_task_log_notice(task,
                    "Backend instance '%s' Failed to get database cursor: %s (%d)",
                    inst->inst_name, dblayer_strerror(return_value), return_value);
            slapi_log_err(SLAPI_LOG_ERR,
                          "dbmdb_db2ldif", "Backend instance '%s'  Failed to get cursor for db2ldif: %s (%d)\n",
                          inst->inst_name, dblayer_strerror(return_value), return_value);
            return_value = -1;
            goto bye;
        }
        return_value = MDB_CURSOR_GET(cur.cur, &key, &data, MDB_LAST);
        if (0 != return_value) {
            keepgoing = 0;
        } else {
            lastid = id_stored_to_internal((char *)key.mv_data);
            isfirst = 1;
        }
    }
    if (include_suffix && ok_index && !dump_replica) {
        int err;

        idl = dbmdb_fetch_subtrees(be, include_suffix, &err);
        if (NULL == idl) {
            if (err) {
                /* most likely, indexes are bad. */
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2ldif",
                              "Backend %s: Failed to fetch subtree lists (error %d) %s\n",
                              inst->inst_name, err, dblayer_strerror(err));
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2ldif",
                              "Possibly the entrydn/entryrdn or ancestorid index is "
                              "corrupted or does not exist.\n");
                slapi_log_err(SLAPI_LOG_ERR,
                              "dbmdb_db2ldif", "Attempting direct unindexed export instead.\n");
            }
            ok_index = 0;
            idl = NULL;
        } else if (ALLIDS(idl)) {
            /* allids list is no help at all -- revert to trawling
             * the whole list. */
            ok_index = 0;
            idl_free(&idl);
        }
        idindex = 0;
    }

    /* When user has specifically asked not to print the version
     * or when this is not the first backend that is append into
     * this file : don't print the version
     */
    if ((!noversion) && ((!appendmode) || (appendmode_1))) {
        char vstr[64];
        int myversion = 1; /* XXX: ldif version;
                 * needs to be modified when version
                 * control begins.
                 */

        sprintf(vstr, "version: %d\n\n", myversion);
        rc = write(fd, vstr, strlen(vstr));
        PR_ASSERT(rc > 0);
        rc = 0;
    }

    eargs.decrypt = decrypt;
    eargs.options = options;
    eargs.printkey = printkey;
    eargs.idl = idl;
    eargs.lastid = lastid;
    eargs.fd = fd;
    eargs.task = task;
    eargs.include_suffix = include_suffix;
    eargs.exclude_suffix = exclude_suffix;

    while (keepgoing) {
        /*
         * All database operations in a transactional environment,
         * including non-transactional reads can receive a return of
         * DB_LOCK_DEADLOCK. Which operation gets aborted depends
         * on the deadlock detection policy, but can include
         * non-transactional reads (in which case the single
         * operation should just be retried).
         */

        if (idl) {
            /* exporting from an ID list */
            if (idindex >= idl->b_nids)
                break;
            id_internal_to_stored(idl->b_ids[idindex], (char *)&temp_id);
            key.mv_data = (char *)&temp_id;
            key.mv_size = sizeof(temp_id);

            return_value = MDB_CURSOR_GET(cur.cur,  &key, &data, MDB_SET);
            if (return_value) {
                slapi_task_log_notice(task, "Backend %s: Failed to read entry %lu, err %d\n",
                        inst->inst_name, (u_long)idl->b_ids[idindex], return_value);
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2ldif",
                        "db2ldif: Backend %s: failed to read entry %lu, err %d\n",
                        inst->inst_name, (u_long)idl->b_ids[idindex],
                        return_value);
                return_value = -1;
                break;
            }
            /* back to internal format: */
            temp_id = idl->b_ids[idindex];
            idindex++;
        } else {
            /* follow the cursor */
            if (isfirst) {
                return_value = MDB_CURSOR_GET(cur.cur,  &key, &data, MDB_FIRST);
                isfirst = 0;
            } else {
                return_value = MDB_CURSOR_GET(cur.cur,  &key, &data, MDB_NEXT);
            }

            if (MDB_NOTFOUND == return_value) {
                /* reached the end of the database,
                 * check if ruv is pending and write it
                 */
                if (pending_ruv) {
                    eargs.ep = pending_ruv;
                    eargs.idindex = idindex;
                    eargs.cnt = &cnt;
                    eargs.lastcnt = &lastcnt;
                    rc = dbmdb_export_one_entry(li, inst, &eargs);
                    backentry_free(&pending_ruv);
                }
                break;
            }

            if (0 != return_value) {
                /* error reading database */
                 break;
            }

            /* back to internal format */
            temp_id = id_stored_to_internal((char *)key.mv_data);
        }
        if (idl_id_is_in_idlist(eargs.pre_exported_idl, temp_id)) {
            /* it's already exported */
            continue;
        }

        /* call post-entry plugin */
        plugin_call_entryfetch_plugins((char **)&data.mv_data, &size);
        data.mv_size = size;

        ep = backentry_alloc();
        if (entryrdn_get_switch()) {
            char *rdn = NULL;

            /* rdn is allocated in get_value_from_string */
            rc = get_value_from_string((const char *)data.mv_data, "rdn", &rdn);
            if (rc) {
                /* data.mv_data may not include rdn: ..., try "dn: ..." */
                ep->ep_entry = slapi_str2entry(data.mv_data,
                                               str2entry_options | SLAPI_STR2ENTRY_NO_ENTRYDN);
            } else {
                char *pid_str = NULL;
                char *pdn = NULL;
                ID pid = NOID;
                char *dn = NULL;
                struct backdn *bdn = NULL;
                Slapi_RDN psrdn = {0};

                /* get a parent pid */
                rc = get_value_from_string((const char *)data.mv_data,
                                           LDBM_PARENTID_STR, &pid_str);
                if (rc) {
                    /* this could be a suffix or the RUV entry.
                     * If it is the ruv and the suffix is not written
                     * keep the ruv and export as last entry.
                     *
                     * The reason for this is that if the RUV entry is in the
                     * ldif before the suffix entry then at an attempt to import
                     * that ldif the RUV entry would be skipped because the parent
                     * does not exist. Later a new RUV would be generated with
                     * a different database generation and replication is broken
                     */
                    if (suffix_written) {
                        /* this must be the RUV, just continue and write it */
                        rc = 0;
                    } else if (0 == strcasecmp(rdn, RUVRDN)) {
                        /* this is the RUV and the suffix is not yet written
                         * make it pending and continue with next entry
                         */
                        skip_ruv = 1;
                        rc = 0;
                    } else {
                        /* this has to be the suffix */
                        suffix_written = 1;
                        rc = 0;
                    }
                } else {
                    pid = (ID)strtol(pid_str, (char **)NULL, 10);
                    slapi_ch_free_string(&pid_str);
                    /* if pid is larger than the current pid temp_id,
                     * the parent entry has to be exported first. */
                    if (temp_id < pid &&
                        !idl_id_is_in_idlist(eargs.pre_exported_idl, pid)) {

                        eargs.idindex = idindex;
                        eargs.cnt = &cnt;
                        eargs.lastcnt = &lastcnt;

                        rc = _export_or_index_parents(inst, &cur, temp_id,
                                                      rdn, temp_id, pid, run_from_cmdline,
                                                      &eargs, DB2LDIF_ENTRYRDN, &psrdn);
                        if (rc) {
                            slapi_rdn_done(&psrdn);
                            backentry_free(&ep);
                            continue;
                        }
                    }
                }

                bdn = dncache_find_id(&inst->inst_dncache, temp_id);
                if (bdn) {
                    /* don't free dn */
                    dn = (char *)slapi_sdn_get_dn(bdn->dn_sdn);
                    CACHE_RETURN(&inst->inst_dncache, &bdn);
                    slapi_rdn_done(&psrdn);
                } else {
                    int myrc = 0;
                    Slapi_DN *sdn = NULL;
                    rc = entryrdn_lookup_dn(be, rdn, temp_id, &dn, NULL, NULL);
                    if (rc) {
                        /* We cannot use the entryrdn index;
                         * Compose dn from the entries in id2entry */
                        slapi_log_err(SLAPI_LOG_TRACE,
                                      "dbmdb_db2ldif", "entryrdn is not available; "
                                                             "composing dn (rdn: %s, ID: %d)\n",
                                      rdn, temp_id);
                        if (NOID != pid) { /* if not a suffix */
                            if (NULL == slapi_rdn_get_rdn(&psrdn)) {
                                /* This time just to get the parents' rdn
                                 * most likely from dn cache. */
                                rc = _get_and_add_parent_rdns(be, &cur, pid,
                                                              &psrdn, NULL, 0,
                                                              run_from_cmdline, NULL);
                                if (rc) {
                                    slapi_log_err(SLAPI_LOG_WARNING,
                                                  "dbmdb_db2ldif", "Skip ID %d\n", pid);
                                    slapi_ch_free_string(&rdn);
                                    slapi_rdn_done(&psrdn);
                                    backentry_free(&ep);
                                    continue;
                                }
                            }
                            /* Generate DN string from Slapi_RDN */
                            rc = slapi_rdn_get_dn(&psrdn, &pdn);
                            if (rc) {
                                slapi_log_err(SLAPI_LOG_WARNING,
                                              "dbmdb_db2ldif", "Failed to compose dn for "
                                                                     "(rdn: %s, ID: %d) from Slapi_RDN\n",
                                              rdn, temp_id);
                                slapi_ch_free_string(&rdn);
                                slapi_rdn_done(&psrdn);
                                backentry_free(&ep);
                                continue;
                            }
                        }
                        dn = slapi_ch_smprintf("%s%s%s",
                                               rdn, pdn ? "," : "", pdn ? pdn : "");
                        slapi_ch_free_string(&pdn);
                    }
                    slapi_rdn_done(&psrdn);
                    /* dn is not dup'ed in slapi_sdn_new_dn_passin.
                     * It's set to bdn and put in the dn cache. */
                    /* don't free dn */
                    sdn = slapi_sdn_new_dn_passin(dn);
                    bdn = backdn_init(sdn, temp_id, 0);
                    myrc = CACHE_ADD(&inst->inst_dncache, bdn, NULL);
                    if (myrc) {
                        backdn_free(&bdn);
                        slapi_log_err(SLAPI_LOG_CACHE, "dbmdb_db2ldif",
                                      "%s is already in the dn cache (%d)\n",
                                      dn, myrc);
                    } else {
                        CACHE_RETURN(&inst->inst_dncache, &bdn);
                        slapi_log_err(SLAPI_LOG_CACHE, "dbmdb_db2ldif",
                                      "entryrdn_lookup_dn returned: %s, "
                                      "and set to dn cache\n",
                                      dn);
                    }
                }
                ep->ep_entry = slapi_str2entry_ext(dn, NULL, data.mv_data,
                                                   str2entry_options | SLAPI_STR2ENTRY_NO_ENTRYDN);
                slapi_ch_free_string(&rdn);
            }
        } else {
            ep->ep_entry = slapi_str2entry(data.mv_data, str2entry_options);
        }

        if ((ep->ep_entry) != NULL) {
            ep->ep_id = temp_id;
        } else {
            slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_db2ldif",
                          "Skipping badly formatted entry with id %lu\n",
                          (u_long)temp_id);
            backentry_free(&ep);
            continue;
        }

        if (skip_ruv) {
            /* now we keep a copy of the ruv entry
             * and continue with the next entry
             */
            pending_ruv = ep;
            ep = NULL;
            skip_ruv = 0;
            continue;
        }

        eargs.ep = ep;
        eargs.idindex = idindex;
        eargs.cnt = &cnt;
        eargs.lastcnt = &lastcnt;
        rc = dbmdb_export_one_entry(li, inst, &eargs);
        backentry_free(&ep);
    }
    /* MDB_NOTFOUND -> successful end */
    if (return_value == MDB_NOTFOUND)
        return_value = 0;

    /* done cycling thru entries to write */
    if (lastcnt != cnt) {
        if (task) {
            slapi_task_log_status(task,
                                  "%s: Processed %d entries (100%%).",
                                  inst->inst_name, cnt);
            slapi_task_log_notice(task,
                                  "%s: Processed %d entries (100%%).",
                                  inst->inst_name, cnt);
        }
        slapi_log_err(SLAPI_LOG_INFO, "dbmdb_db2ldif",
                      "export %s: Processed %d entries (100%%).\n",
                      inst->inst_name, cnt);
    }
    if (run_from_cmdline && dump_changelog) {
        return_value = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_POST_EXPORT_FN);
        slapi_log_err(SLAPI_LOG_INFO, "ldbm_back_ldbm2ldif",
                      "export changelog for %s\n", inst->inst_name);
    }

bye:
    if (idl) {
        idl_free(&idl);
    }
    dbmdb_close_cursor(&cur, 1);

    dblayer_release_id2entry(be, db);

    if (fd > STDERR_FILENO) {
        close(fd);
    }

    slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_db2ldif", "<=\n");

    if (we_start_the_backends && NULL != li) {
        if (0 != dblayer_close(li, DBLAYER_EXPORT_MODE)) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "dbmdb_db2ldif", "db2ldif: Failed to close database\n");
        }
    }

    if (!run_from_cmdline && inst && (LDBM2LDIF_BUSY != return_value)) {
        instance_set_not_busy(inst);
    }

    dbmdb_back_free_incl_excl(include_suffix, exclude_suffix);
    idl_free(&(eargs.pre_exported_idl));

    return (return_value);
}

static void
dbmdb_mdb2index_bad_vlv(Slapi_Task *task, ldbm_instance *inst, char *index)
{
    char *text = vlv_getindexnames(inst->inst_be);

    if (task) {
        slapi_task_log_status(task, "%s: Unknown VLV index '%s'",
                              inst->inst_name, index);
        slapi_task_log_notice(task, "%s: Unknown VLV index '%s'",
                              inst->inst_name, index);
        slapi_task_log_notice(task, "%s: Known VLV indexes are: %s",
                              inst->inst_name, text);
    }
    slapi_log_err(SLAPI_LOG_ERR,
                  "dbmdb_mdb2index_bad_vlv", "Unknown VLV Index named '%s'\n", index);
    slapi_log_err(SLAPI_LOG_ERR,
                  "dbmdb_mdb2index_bad_vlv", "Known VLV Indexes are: %s\n", text);
    slapi_ch_free_string(&text);
}

int
dbmdb_db2index(Slapi_PBlock *pb)
{
    char *instance_name;
    struct ldbminfo *li;
    int task_flags, run_from_cmdline;
    ldbm_instance *inst;
    backend *be;
    dbi_db_t *db = NULL; /* MDB_dbihandle for id2entry */
    char **indexAttrs = NULL;
    struct vlvIndex **pvlv = NULL;
    MDB_val key = {0};
    MDB_val data = {0};
    IDList *idl = NULL; /* optimization for vlv index creation */
    int numvlv = 0;
    int return_value = -1;
    int rc = -1;
    ID temp_id;
    int i, j, vlvidx;
    ID lastid;
    struct backentry *ep = NULL;
    char *type;
    NIDS idindex = 0;
    int count = 0;
    Slapi_Attr *attr;
    Slapi_Task *task;
    int isfirst = 1;
    int index_ext = 0;
    struct vlvIndex *vlvip = NULL;
    back_txn txn;
    ID suffixid = NOID; /* holds the id of the suffix entry */
    Slapi_Value **nstombstone_vals = NULL;
    int istombstone = 0;
    dbmdb_cursor_t cur = {0};
    uint size = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_db2index", "=>\n");
    if (g_get_shutdown() || c_get_shutdown()) {
        return return_value;
    }

    slapi_pblock_get(pb, SLAPI_BACKEND_INSTANCE_NAME, &instance_name);
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_TASK_FLAGS, &task_flags);
    run_from_cmdline = (task_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE);
    slapi_pblock_get(pb, SLAPI_BACKEND_TASK, &task);

    inst = ldbm_instance_find_by_name(li, instance_name);
    if (NULL == inst) {
        slapi_task_log_notice(task, "Unknown ldbm instance %s", instance_name);
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2index", "Unknown ldbm instance %s\n",
                      instance_name);
        return return_value;
    }
    be = inst->inst_be;
    slapi_pblock_set(pb, SLAPI_BACKEND, be);

    /* would love to be able to turn off transactions here, but i don't
     * think it's in the cards...
     */
    if (run_from_cmdline) {
        /* Turn off transactions */
        ldbm_config_internal_set(li, CONFIG_DB_TRANSACTION_LOGGING, "off");

        if (0 != dblayer_start(li, DBLAYER_INDEX_MODE)) {
            slapi_task_log_notice(task, "Failed to init database: %s", instance_name);
            slapi_log_err(SLAPI_LOG_ERR,
                          "ldbm2index", "Failed to init database: %s\n", instance_name);
            return return_value;
        }

        /* dblayer_instance_start will init the id2entry index. */
        if (0 != dblayer_instance_start(be, DBLAYER_INDEX_MODE)) {
            slapi_task_log_notice(task, "Failed to start instance: %s", instance_name);
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2index", "db2ldif: Failed to start instance\n");
            return return_value;
        }

        /* Initialise the Virtual List View code */
        vlv_init(inst);
    }

    /* make sure no other tasks are going, and set the backend readonly */
    if (instance_set_busy_and_readonly(inst) != 0) {
        slapi_task_log_notice(task,
                "%s: is already in the middle of another task and cannot be disturbed.",
                inst->inst_name);
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2index", "ldbm: '%s' is already in the middle of "
                                                             "another task and cannot be disturbed.\n",
                      inst->inst_name);
        return return_value;
    }

    if (((dblayer_get_id2entry(be, &db)) != 0) || (db == NULL)) {
        slapi_task_log_notice(task,
                "%s: Could not open/create database (id2entry)",
                inst->inst_name);
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2index", "Could not open/create database(id2entry)\n");
        goto err_min;
    }

    /* get a cursor to we can walk over the table */
    rc = dbmdb_open_cursor(&cur, MDB_CONFIG(li), db, 0);
    if (0 != rc) {
        slapi_task_log_notice(task,
                "%s: Failed to get database cursor for ldbm2index",
                inst->inst_name);
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_db2index", "%s: Failed to get cursor for ldbm2index\n",
                      inst->inst_name);
        goto err_min;
    }

    /* ask for the last id so we can give cute percentages */
    rc = MDB_CURSOR_GET(cur.cur, &key, &data, MDB_LAST);
    if (rc == MDB_NOTFOUND) {
        lastid = 0;
        isfirst = 0; /* neither a first nor a last */
    } else if (rc == 0) {
        lastid = id_stored_to_internal((char *)key.mv_data);
        isfirst = 1;
    } else {
        slapi_task_log_notice(task, "%s: Failed to seek within id2entry (BAD %d)",
                    inst->inst_name, return_value);
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_db2index", "%s: Failed to seek within id2entry (BAD %d)\n",
                      inst->inst_name, return_value);
        goto err_out;
    }

    /* Work out which indexes we should build */
    /* explanation: for archaic reasons, the list of indexes is passed to
     * ldif2index as a string list, where each string either starts with a
     * 't' (normal index) or a 'T' (vlv index).
     * example: "tcn" (normal index cn)
     */
    /* NOTE (LK): This part in determining the attrs to reindex belongs to the layer above
     * the selection of attributes is independent of the backend implementation.
     * butit requires a method to pass the selection to this lkower level indexing function
     * either by extension of the pblock or the argument list
     * TBD
     */
    {
        char **attrs = NULL;
        struct attrinfo *ai = NULL;

        slapi_pblock_get(pb, SLAPI_DB2INDEX_ATTRS, &attrs);
        for (i = 0; attrs && attrs[i]; i++) {
            if (g_get_shutdown() || c_get_shutdown()) {
                goto err_out;
            }
            switch (attrs[i][0]) {
            case 't': /* attribute type to index */
                db2index_add_indexed_attr(be, attrs[i]);
                ainfo_get(be, attrs[i] + 1, &ai);
                /* the ai was added above, if it didn't already exist */
                PR_ASSERT(ai != NULL);
                if (strcasecmp(attrs[i] + 1, LDBM_ANCESTORID_STR) == 0) {
                    slapi_task_log_notice(task, "%s: Indexing %s",
                        inst->inst_name, LDBM_ANCESTORID_STR);
                    slapi_log_err(SLAPI_LOG_INFO, "dbmdb_db2index", "%s: Indexing %s\n",
                                  inst->inst_name, LDBM_ANCESTORID_STR);
                    index_ext |= DB2INDEX_ANCESTORID;
                } else if (strcasecmp(attrs[i] + 1, LDBM_ENTRYRDN_STR) == 0) {
                    if (entryrdn_get_switch()) { /* subtree-rename: on */
                        slapi_task_log_notice(task, "%s: Indexing %s",
                             inst->inst_name, LDBM_ENTRYRDN_STR);
                        slapi_log_err(SLAPI_LOG_INFO, "dbmdb_db2index", "%s: Indexing %s\n",
                                      inst->inst_name, LDBM_ENTRYRDN_STR);
                        index_ext |= DB2INDEX_ENTRYRDN;
                    } else {
                        slapi_task_log_notice(task, "%s: Requested to index %s, but %s is off",
                                inst->inst_name, LDBM_ENTRYRDN_STR, CONFIG_ENTRYRDN_SWITCH);
                        slapi_log_err(SLAPI_LOG_WARNING,
                                      "dbmdb_db2index", "%s: Requested to index %s, but %s is off\n",
                                      inst->inst_name, LDBM_ENTRYRDN_STR,
                                      CONFIG_ENTRYRDN_SWITCH);
                        goto err_out;
                    }
                } else if (strcasecmp(attrs[i] + 1, LDBM_ENTRYDN_STR) == 0) {
                    if (entryrdn_get_switch()) { /* subtree-rename: on */
                        slapi_task_log_notice(task, "%s: Requested to index %s, but %s is on",
                                inst->inst_name, LDBM_ENTRYDN_STR, CONFIG_ENTRYRDN_SWITCH);
                        slapi_log_err(SLAPI_LOG_WARNING,
                                      "dbmdb_db2index", "%s: Requested to index %s, but %s is on\n",
                                      inst->inst_name, LDBM_ENTRYDN_STR,
                                      CONFIG_ENTRYRDN_SWITCH);
                        goto err_out;
                    } else {
                        charray_add(&indexAttrs, attrs[i] + 1);
                        ai->ai_indexmask |= INDEX_OFFLINE;
                        slapi_task_log_notice(task, "%s: Indexing attribute: %s",
                                inst->inst_name, attrs[i] + 1);
                        slapi_log_err(SLAPI_LOG_INFO,
                                      "dbmdb_db2index", "%s: Indexing attribute: %s\n",
                                      inst->inst_name, attrs[i] + 1);
                    }
                } else {
                    if (strcasecmp(attrs[i] + 1, SLAPI_ATTR_OBJECTCLASS) == 0) {
                        index_ext |= DB2INDEX_OBJECTCLASS;
                    }
                    charray_add(&indexAttrs, attrs[i] + 1);
                    ai->ai_indexmask |= INDEX_OFFLINE;
                    slapi_task_log_notice(task, "%s: Indexing attribute: %s",
                            inst->inst_name, attrs[i] + 1);
                    slapi_log_err(SLAPI_LOG_INFO,
                                  "dbmdb_db2index", "%s: Indexing attribute: %s\n",
                                  inst->inst_name, attrs[i] + 1);
                }
                dblayer_erase_index_file(be, ai, PR_TRUE, i /* chkpt; 1st time only */);
                break;
            case 'T': /* VLV Search to index */
                vlvip = vlv_find_searchname((attrs[i]) + 1, be);
                if (vlvip == NULL) {
                    dbmdb_mdb2index_bad_vlv(task, inst, attrs[i] + 1);
                } else {
                    vlvIndex_go_offline(vlvip, be);
                    if (pvlv == NULL) {
                        pvlv = (struct vlvIndex **)slapi_ch_calloc(1,
                                                                   sizeof(struct vlvIndex *));
                    } else {
                        pvlv = (struct vlvIndex **)slapi_ch_realloc((char *)pvlv,
                                                                    (numvlv + 1) * sizeof(struct vlvIndex *));
                    }
                    pvlv[numvlv] = vlvip;
                    numvlv++;
                    /* Get rid of the index if it already exists */
                    PR_Delete(vlvIndex_filename(vlvip));
                    slapi_task_log_notice(task, "%s: Indexing VLV: %s", inst->inst_name, attrs[i] + 1);
                    slapi_log_err(SLAPI_LOG_INFO, "dbmdb_db2index", "%s: Indexing VLV: %s\n",
                                  inst->inst_name, attrs[i] + 1);
                }
                break;
            }
        }
    }

    /* if we're only doing vlv indexes, we can accomplish this with an
     * idl composed from the ancestorid list, instead of traversing the
     * entire database.
     */
    if (!indexAttrs && !LDIF2LDBM_EXTBITS(index_ext) && pvlv) {
        int err;
        char **suffix_list = NULL;

        /* create suffix list */
        for (vlvidx = 0; vlvidx < numvlv; vlvidx++) {
            char *s =
                slapi_ch_strdup(slapi_sdn_get_ndn(vlvIndex_getBase(pvlv[vlvidx])));
            /* 's' is passed in */
            charray_add(&suffix_list, s);
        }
        idl = dbmdb_fetch_subtrees(be, suffix_list, &err);
        charray_free(suffix_list);
        if (!idl) {
            /* most likely, indexes are bad if err is set. */
            if (0 != err) {
                slapi_task_log_notice(task,
                          "%s: WARNING: Failed to fetch subtree lists (err %d) -- "
                          "attempting brute-force method instead.",
                          inst->inst_name, err);
                slapi_log_err(SLAPI_LOG_WARNING,
                              "dbmdb_db2index", "%s: Failed to fetch subtree lists: (%d) %s\n",
                              inst->inst_name, err, dblayer_strerror(err));
                slapi_log_err(SLAPI_LOG_WARNING,
                              "dbmdb_db2index", "%s: Possibly the entrydn/entryrdn or ancestorid index "
                                                      "is corrupted or does not exist.\n",
                              inst->inst_name);
                slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_db2index",
                              "%s: Attempting brute-force method instead.\n",
                              inst->inst_name);
            }
        } else if (ALLIDS(idl)) {
            /* that's no help. */
            idl_free(&idl);
        }
    }

    if (idl) {
        /* don't need that cursor, we have a shopping list. */
        dbmdb_close_cursor(&cur, 0);
    }

    dblayer_txn_init(li, &txn);

    while (1) {
        if (g_get_shutdown() || c_get_shutdown()) {
            goto err_out;
        }
        if (idl) {
            if (idindex >= idl->b_nids)
                break;
            id_internal_to_stored(idl->b_ids[idindex], (char *)&temp_id);
            key.mv_data = (char *)&temp_id;
            key.mv_size = sizeof(temp_id);

            rc = MDB_CURSOR_GET(cur.cur, &key, &data, MDB_SET);
            if (rc) {
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2index", "%s: Failed "
                                                                     "to read database, errno=%d (%s)\n",
                              inst->inst_name, rc, dblayer_strerror(rc));
                slapi_task_log_notice(task, "%s: Failed to read database, err %d (%s)",
                        inst->inst_name, rc, dblayer_strerror(rc));
                break;
            }
            /* back to internal format: */
            temp_id = idl->b_ids[idindex];
        } else {
            if (isfirst) {
                rc = MDB_CURSOR_GET(cur.cur, &key, &data, MDB_FIRST);
                isfirst = 0;
            } else {
                rc = MDB_CURSOR_GET(cur.cur, &key, &data, MDB_NEXT);
            }

            if (MDB_NOTFOUND == rc) {
                break;
            } else if (0 != rc) {
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2index",
                        "%s: Failed to read database, errno=%d (%s)\n",
                        inst->inst_name, rc, dblayer_strerror(rc));
                slapi_task_log_notice(task, "%s: Failed to read database, err %d (%s)",
                        inst->inst_name, rc, dblayer_strerror(rc));
                break;
            }
            temp_id = id_stored_to_internal((char *)key.mv_data);
        }
        idindex++;

        /* call post-entry plugin */
        plugin_call_entryfetch_plugins((char **)&data.mv_data, &size);
        data.mv_size = size;

        ep = backentry_alloc();
        if (entryrdn_get_switch()) {
            char *rdn = NULL;
            int rc = 0;

            /* rdn is allocated in get_value_from_string */
            rc = get_value_from_string((const char *)data.mv_data, "rdn", &rdn);
            if (rc) {
                /* data.mv_data may not include rdn: ..., try "dn: ..." */
                ep->ep_entry = slapi_str2entry(data.mv_data,
                                               SLAPI_STR2ENTRY_NO_ENTRYDN);
            } else {
                char *pid_str = NULL;
                char *pdn = NULL;
                ID pid = NOID;
                char *dn = NULL;
                struct backdn *bdn = NULL;
                Slapi_RDN psrdn = {0};

                /* get a parent pid */
                rc = get_value_from_string((const char *)data.mv_data,
                                           LDBM_PARENTID_STR, &pid_str);
                if (rc || !pid_str) {
                    /* see if this is a suffix or some entry without a parent id
                       e.g. a tombstone entry */
                    Slapi_DN sufdn;

                    slapi_sdn_init_dn_byref(&sufdn, rdn);
                    if (slapi_be_issuffix(be, &sufdn)) {
                        rc = 0;             /* is a suffix */
                        suffixid = temp_id; /* this is the ID of a suffix entry */
                    } else {
                        /* assume the parent entry is the suffix entry for this backend
                           set pid to the id of that entry */
                        pid = suffixid;
                    }
                    slapi_sdn_done(&sufdn);
                }
                if (pid_str) {
                    pid = (ID)strtol(pid_str, (char **)NULL, 10);
                    slapi_ch_free_string(&pid_str);
                    /* if pid is larger than the current pid temp_id,
                     * the parent entry has to be exported first. */
                    if (temp_id < pid) {
                        rc = _export_or_index_parents(inst, &cur, temp_id,
                                                      rdn, temp_id, pid, run_from_cmdline,
                                                      NULL, index_ext, &psrdn);
                        if (rc) {
                            backentry_free(&ep);
                            continue;
                        }
                    }
                }

                bdn = dncache_find_id(&inst->inst_dncache, temp_id);
                if (bdn) {
                    /* don't free dn */
                    dn = (char *)slapi_sdn_get_dn(bdn->dn_sdn);
                    CACHE_RETURN(&inst->inst_dncache, &bdn);
                } else {
                    int myrc = 0;
                    Slapi_DN *sdn = NULL;
                    rc = entryrdn_lookup_dn(be, rdn, temp_id, &dn, NULL, NULL);
                    if (rc) {
                        /* We cannot use the entryrdn index;
                         * Compose dn from the entries in id2entry */
                        slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_db2index",
                                      "entryrdn is not available; "
                                      "composing dn (rdn: %s, ID: %d)\n",
                                      rdn, temp_id);
                        if (NOID != pid) { /* if not a suffix */
                            if (NULL == slapi_rdn_get_rdn(&psrdn)) {
                                /* This time just to get the parents' rdn
                                 * most likely from dn cache. */
                                rc = _get_and_add_parent_rdns(be, &cur, pid,
                                                              &psrdn, NULL, 0,
                                                              run_from_cmdline, NULL);
                                if (rc) {
                                    slapi_log_err(SLAPI_LOG_WARNING,
                                                  "dbmdb_db2index", "Skip ID %d\n", pid);
                                    slapi_log_err(SLAPI_LOG_WARNING,
                                                  "dbmdb_db2index", "Parent entry (ID %d) of entry. "
                                                                          "(ID %d, rdn: %s) does not exist.\n",
                                                  pid, temp_id, rdn);
                                    slapi_log_err(SLAPI_LOG_WARNING,
                                                  "dbmdb_db2index", "We recommend to export the backend "
                                                                          "instance %s and reimport it.\n",
                                                  instance_name);
                                    slapi_ch_free_string(&rdn);
                                    slapi_rdn_done(&psrdn);
                                    backentry_free(&ep);
                                    continue;
                                }
                            }
                            /* Generate DN string from Slapi_RDN */
                            rc = slapi_rdn_get_dn(&psrdn, &pdn);
                            if (rc) {
                                slapi_log_err(SLAPI_LOG_ERR,
                                              "dbmdb_db2index", "Failed to compose dn for "
                                                                      "(rdn: %s, ID: %d) from Slapi_RDN\n",
                                              rdn, temp_id);
                                slapi_ch_free_string(&rdn);
                                slapi_rdn_done(&psrdn);
                                backentry_free(&ep);
                                continue;
                            }
                        }
                        dn = slapi_ch_smprintf("%s%s%s",
                                               rdn, pdn ? "," : "", pdn ? pdn : "");
                        slapi_ch_free_string(&pdn);
                    }
                    /* dn is not dup'ed in slapi_sdn_new_dn_passin.
                     * It's set to bdn and put in the dn cache. */
                    /* don't free dn */
                    sdn = slapi_sdn_new_dn_passin(dn);
                    bdn = backdn_init(sdn, temp_id, 0);
                    myrc = CACHE_ADD(&inst->inst_dncache, bdn, NULL);
                    if (myrc) {
                        backdn_free(&bdn);
                        slapi_log_err(SLAPI_LOG_CACHE, "dbmdb_db2index",
                                      "%s is already in the dn cache (%d)\n",
                                      dn, myrc);
                    } else {
                        CACHE_RETURN(&inst->inst_dncache, &bdn);
                        slapi_log_err(SLAPI_LOG_CACHE, "dbmdb_db2index",
                                      "entryrdn_lookup_dn returned: %s, "
                                      "and set to dn cache\n",
                                      dn);
                    }
                }
                slapi_rdn_done(&psrdn);
                ep->ep_entry = slapi_str2entry_ext(dn, NULL, data.mv_data,
                                                   SLAPI_STR2ENTRY_NO_ENTRYDN);
                slapi_ch_free_string(&rdn);
            }
        } else {
            ep->ep_entry = slapi_str2entry(data.mv_data, 0);
        }

        if (ep->ep_entry != NULL) {
            ep->ep_id = temp_id;
        } else {
            slapi_task_log_notice(task, "%s: WARNING: skipping badly formatted entry (id %lu)",
                    inst->inst_name, (u_long)temp_id);
            slapi_log_err(SLAPI_LOG_WARNING,
                          "dbmdb_db2index", "%s: Skipping badly formatted entry (id %lu)\n",
                          inst->inst_name, (u_long)temp_id);
            backentry_free(&ep);
            continue;
        }

        /*
         * If this entry is a tombstone, update the 'nstombstonecsn' index
         */
        if (ep->ep_entry->e_flags & SLAPI_ENTRY_FLAG_TOMBSTONE) {
            const CSN *tombstone_csn = entry_get_deletion_csn(ep->ep_entry);
            char deletion_csn_str[CSN_STRSIZE];

            istombstone = 1;
            if (!nstombstone_vals) {
                nstombstone_vals = (Slapi_Value **)slapi_ch_calloc(2, sizeof(Slapi_Value *));
                *nstombstone_vals = slapi_value_new_string(SLAPI_ATTR_VALUE_TOMBSTONE);
            }
            if (tombstone_csn) {
                if (!run_from_cmdline) {
                    rc = dblayer_txn_begin(be, NULL, &txn);
                    if (0 != rc) {
                        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2index",
                                      "%s: Failed to begin txn for update index '%s' (err %d: %s)\n",
                                      inst->inst_name, SLAPI_ATTR_TOMBSTONE_CSN, rc, dblayer_strerror(rc));
                        slapi_task_log_notice(task,
                                "%s: ERROR: failed to begin txn for update index '%s' (err %d: %s)",
                                inst->inst_name, SLAPI_ATTR_TOMBSTONE_CSN, rc, dblayer_strerror(rc));
                        return_value = -2;
                        goto err_out;
                    }
                }

                csn_as_string(tombstone_csn, PR_FALSE, deletion_csn_str);
                rc = index_addordel_string(be, SLAPI_ATTR_TOMBSTONE_CSN, deletion_csn_str,
                                           ep->ep_id, BE_INDEX_ADD, &txn);
                if (rc != 0) {
                    slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2index",
                                  "%s: Failed to update index '%s' (err %d: %s)\n",
                                  inst->inst_name, SLAPI_ATTR_TOMBSTONE_CSN, rc, dblayer_strerror(rc));
                    slapi_task_log_notice(task,
                            "%s: ERROR: failed to update index '%s' (err %d: %s)",
                            inst->inst_name, SLAPI_ATTR_TOMBSTONE_CSN, rc, dblayer_strerror(rc));
                    if (!run_from_cmdline) {
                        dblayer_txn_abort(be, &txn);
                    }
                    return_value = -2;
                    goto err_out;
                }
                if (!run_from_cmdline) {
                    rc = dblayer_txn_commit(be, &txn);
                    if (0 != rc) {
                        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2index",
                                      "%s: Failed to commit txn for update index '%s' (err %d: %s)\n",
                                      inst->inst_name, SLAPI_ATTR_TOMBSTONE_CSN, rc, dblayer_strerror(rc));
                        slapi_task_log_notice(task,
                                "%s: ERROR: failed to commit txn for update index '%s' (err %d: %s)",
                                inst->inst_name, SLAPI_ATTR_TOMBSTONE_CSN, rc, dblayer_strerror(rc));
                        return_value = -2;
                        goto err_out;
                    }
                }
            }
        } else {
            istombstone = 0;
        }

        /*
         * Update the attribute indexes
         */
        if (indexAttrs) {
            if (istombstone && !(index_ext & (DB2INDEX_ENTRYRDN | DB2INDEX_OBJECTCLASS))) {
                /* if it is a tombstone entry, just entryrdn or "objectclass: nstombstone"
                 * need to be reindexed.  the to-be-indexed list does not contain them. */
                backentry_free(&ep);
                continue;
            }
            for (i = slapi_entry_first_attr(ep->ep_entry, &attr); i == 0;
                 i = slapi_entry_next_attr(ep->ep_entry, attr, &attr)) {
                Slapi_Value **svals;

                slapi_attr_get_type(attr, &type);
                for (j = 0; indexAttrs[j] != NULL; j++) {
                    int is_tombstone_obj = 0;
                    if (g_get_shutdown() || c_get_shutdown()) {
                        goto err_out;
                    }
                    if (slapi_attr_type_cmp(indexAttrs[j], type, SLAPI_TYPE_CMP_SUBTYPE) == 0) {
                        if (istombstone) {
                            if (!slapi_attr_type_cmp(indexAttrs[j], SLAPI_ATTR_OBJECTCLASS, SLAPI_TYPE_CMP_SUBTYPE)) {
                                is_tombstone_obj = 1; /* is tombstone && is objectclass. need to index "nstombstone"*/
                            } else if (slapi_attr_type_cmp(indexAttrs[j], LDBM_ENTRYRDN_STR, SLAPI_TYPE_CMP_SUBTYPE)) {
                                /* Entry is a tombstone && this index is not an entryrdn. */
                                continue;
                            }
                        }
                        svals = attr_get_present_values(attr);

                        if (!run_from_cmdline) {
                            rc = dblayer_txn_begin(be, NULL, &txn);
                            if (0 != rc) {
                                slapi_log_err(SLAPI_LOG_ERR,
                                              "dbmdb_db2index", "%s: Failed to begin txn for update index '%s'\n",
                                              inst->inst_name, indexAttrs[j]);
                                slapi_log_err(SLAPI_LOG_ERR,
                                              "dbmdb_db2index", "%s: Error %d: %s\n", inst->inst_name, rc,
                                              dblayer_strerror(rc));
                                slapi_task_log_notice(task,
                                        "%s: ERROR: failed to begin txn for update index '%s' (err %d: %s)",
                                        inst->inst_name, indexAttrs[j], rc, dblayer_strerror(rc));
                                return_value = -2;
                                goto err_out;
                            }
                        }
                        if (is_tombstone_obj) {
                            rc = index_addordel_values_sv(be, indexAttrs[j], nstombstone_vals, NULL, ep->ep_id, BE_INDEX_ADD, &txn);
                        } else {
                            rc = index_addordel_values_sv(be, indexAttrs[j], svals, NULL, ep->ep_id, BE_INDEX_ADD, &txn);
                        }
                        if (rc) {
                            slapi_log_err(SLAPI_LOG_ERR,
                                          "dbmdb_db2index", "%s: Failed to update index '%s'\n",
                                          inst->inst_name, indexAttrs[j]);
                            slapi_log_err(SLAPI_LOG_ERR,
                                          "dbmdb_db2index", "%s: Error %d: %s\n", inst->inst_name, rc,
                                          dblayer_strerror(rc));
                            slapi_task_log_notice(task,
                                    "%s: ERROR: failed to update index '%s' (err %d: %s)",
                                    inst->inst_name, indexAttrs[j], rc, dblayer_strerror(rc));
                            if (!run_from_cmdline) {
                                dblayer_txn_abort(be, &txn);
                            }
                            return_value = -2;
                            goto err_out;
                        }
                        if (!run_from_cmdline) {
                            rc = dblayer_txn_commit(be, &txn);
                            if (0 != rc) {
                                slapi_log_err(SLAPI_LOG_ERR,
                                              "dbmdb_db2index", "%s: Failed to commit txn for "
                                                                      "update index '%s'\n",
                                              inst->inst_name, indexAttrs[j]);
                                slapi_log_err(SLAPI_LOG_ERR,
                                              "dbmdb_db2index", "%s: Error %d: %s\n", inst->inst_name, rc,
                                              dblayer_strerror(rc));
                                slapi_task_log_notice(task,
                                        "%s: ERROR: failed to commit txn for  update index '%s' (err %d: %s)",
                                        inst->inst_name, indexAttrs[j], rc, dblayer_strerror(rc));
                                return_value = -2;
                                goto err_out;
                            }
                        }
                    }
                }
            }
        }

        /*
         * If it is NOT a tombstone entry, update the Virtual List View indexes.
         */
        for (vlvidx = 0; !istombstone && (vlvidx < numvlv); vlvidx++) {
            char *ai = "Unknown index";

            if (g_get_shutdown() || c_get_shutdown()) {
                goto err_out;
            }
            if (indexAttrs && indexAttrs[vlvidx]) {
                ai = indexAttrs[vlvidx];
            }
            if (!run_from_cmdline) {
                rc = dblayer_txn_begin(be, NULL, &txn);
                if (0 != rc) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "dbmdb_db2index", "%s: Failed to begin txn for update index '%s'\n",
                                  inst->inst_name, ai);
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "dbmdb_db2index", "%s: Error %d: %s\n", inst->inst_name, rc,
                                  dblayer_strerror(rc));
                    slapi_task_log_notice(task,
                            "%s: ERROR: failed to begin txn for update index '%s' (err %d: %s)",
                            inst->inst_name, ai, rc, dblayer_strerror(rc));
                    return_value = -2;
                    goto err_out;
                }
            }
            /*
             * lock is needed around vlv_update_index to protect the
             * vlv structure.
             */
            vlv_acquire_lock(be);
            vlv_update_index(pvlv[vlvidx], &txn, li, pb, NULL, ep);
            vlv_release_lock(be);
            if (!run_from_cmdline) {
                rc = dblayer_txn_commit(be, &txn);
                if (0 != rc) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "dbmdb_db2index", "%s: Failed to commit txn for update index '%s'\n",
                                  inst->inst_name, ai);
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "dbmdb_db2index", "%s: Error %d: %s\n", inst->inst_name, rc,
                                  dblayer_strerror(rc));
                    slapi_task_log_notice(task,
                            "%s: ERROR: failed to commit txn for update index '%s' (err %d: %s)",
                            inst->inst_name, ai, rc, dblayer_strerror(rc));
                    return_value = -2;
                    goto err_out;
                }
            }
        }

        /*
         * Update the ancestorid and entryrdn index
         */
        if (!entryrdn_get_noancestorid() && (index_ext & DB2INDEX_ANCESTORID)) {
            rc = ldbm_ancestorid_index_entry(be, ep, BE_INDEX_ADD, NULL);
            if (rc != 0) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "dbmdb_db2index", "%s: Failed to update index 'ancestorid'\n",
                              inst->inst_name);
                slapi_log_err(SLAPI_LOG_ERR,
                              "dbmdb_db2index", "%s: Error %d: %s\n", inst->inst_name, rc,
                              dblayer_strerror(rc));
                slapi_task_log_notice(task,
                        "%s: ERROR: failed to update index 'ancestorid' (err %d: %s)",
                        inst->inst_name, rc, dblayer_strerror(rc));
                return_value = -2;
                goto err_out;
            }
        }
        if (index_ext & DB2INDEX_ENTRYRDN) {
            if (entryrdn_get_switch()) { /* subtree-rename: on */
                if (!run_from_cmdline) {
                    rc = dblayer_txn_begin(be, NULL, &txn);
                    if (0 != rc) {
                        slapi_log_err(SLAPI_LOG_ERR,
                                      "dbmdb_db2index", "%s: ERROR: failed to begin txn for update index 'entryrdn'\n",
                                      inst->inst_name);
                        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2index", "%s: Error %d: %s\n",
                                      inst->inst_name, rc, dblayer_strerror(rc));
                        slapi_task_log_notice(task,
                                "%s: ERROR: failed to begin txn for update index 'entryrdn' (err %d: %s)",
                                inst->inst_name, rc, dblayer_strerror(rc));
                        return_value = -2;
                        goto err_out;
                    }
                }
                rc = entryrdn_index_entry(be, ep, BE_INDEX_ADD, &txn);
                if (rc) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "dbmdb_db2index", "%s: Failed to update index 'entryrdn'\n",
                                  inst->inst_name);
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "dbmdb_db2index", "%s: Error %d: %s\n", inst->inst_name, rc,
                                  dblayer_strerror(rc));
                    slapi_task_log_notice(task,
                            "%s: ERROR: failed to update index 'entryrdn' (err %d: %s)",
                            inst->inst_name, rc, dblayer_strerror(rc));
                    if (!run_from_cmdline) {
                        dblayer_txn_abort(be, &txn);
                    }
                    return_value = -2;
                    goto err_out;
                }
                if (!run_from_cmdline) {
                    rc = dblayer_txn_commit(be, &txn);
                    if (0 != rc) {
                        slapi_log_err(SLAPI_LOG_ERR,
                                      "dbmdb_db2index", "%s: Failed to commit txn for "
                                                              "update index 'entryrdn'\n",
                                      inst->inst_name);
                        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_db2index", "%s: Error %d: %s\n",
                                      inst->inst_name, rc, dblayer_strerror(rc));
                        slapi_task_log_notice(task,
                                "%s: ERROR: failed to commit txn for update index 'entryrdn' (err %d: %s)",
                                inst->inst_name, rc, dblayer_strerror(rc));
                        return_value = -2;
                        goto err_out;
                    }
                }
            }
        }

        count++;
        if ((count % 1000) == 0) {
            int percent;

            if (idl) {
                percent = (idindex * 100 / (idl->b_nids ? idl->b_nids : 1));
            } else {
                percent = (ep->ep_id * 100 / (lastid ? lastid : 1));
            }
            if (task) {
                /* NGK - This should eventually be cleaned up to use the
                 * public task API */
                task->task_progress = (idl ? idindex : ep->ep_id);
                task->task_work = (idl ? idl->b_nids : lastid);
                slapi_task_status_changed(task);
                slapi_task_log_status(task, "%s: Indexed %d entries (%d%%).",
                                      inst->inst_name, count, percent);
                slapi_task_log_notice(task, "%s: Indexed %d entries (%d%%).",
                                      inst->inst_name, count, percent);
            }
            slapi_log_err(SLAPI_LOG_INFO, "dbmdb_db2index", "%s: Indexed %d entries (%d%%).\n",
                          inst->inst_name, count, percent);
        }

        backentry_free(&ep);
    }

    /* if we got here, we finished successfully */

    /* activate all the indexes we added */
    for (i = 0; indexAttrs && indexAttrs[i]; i++) {
        struct attrinfo *ai = NULL;

        ainfo_get(be, indexAttrs[i], &ai);
        PR_ASSERT(ai != NULL);
        ai->ai_indexmask &= ~INDEX_OFFLINE;
    }
    for (vlvidx = 0; vlvidx < numvlv; vlvidx++) {
        vlvIndex_go_online(pvlv[vlvidx], be);
    }

    /* if it was a task, its status will be updated later after backend is ready for update */
    slapi_log_err(SLAPI_LOG_INFO, "dbmdb_db2index", "%s: Finished indexing.\n",
                  inst->inst_name);
    return_value = 0; /* success */
err_out:
    backentry_free(&ep); /* if ep or *ep is NULL, it does nothing */
    if (idl) {
        idl_free(&idl);
    } else {
        dbmdb_close_cursor(&cur, 0);
    }
    if (return_value < 0) { /* error case: undo vlv indexing */
        /* if jumped to out due to an error, vlv lock has not been released */
        for (vlvidx = 0; vlvidx < numvlv; vlvidx++) {
            vlvIndex_go_offline(pvlv[vlvidx], be);
            vlv_acquire_lock(be);
            vlvIndex_delete(&pvlv[vlvidx]);
            vlv_release_lock(be);
        }
    }
err_min:
    dblayer_release_id2entry(be, db); /* nope */
    instance_set_not_busy(inst);

    if (return_value == 0) {
        if (task) {
            slapi_task_log_status(task, "%s: Finished indexing.",
                    inst->inst_name);
            slapi_task_log_notice(task, "%s: Finished indexing.",
                    inst->inst_name);
        }
    }

    if (run_from_cmdline) {
        dblayer_instance_close(be);
        if (0 != dblayer_close(li, DBLAYER_INDEX_MODE)) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "dbmdb_db2index", "%s: Failed to close database\n", inst->inst_name);
        }
    }

    valuearray_free(&nstombstone_vals);
    if (indexAttrs) {
        slapi_ch_free((void **)&indexAttrs);
    }
    if (pvlv) {
        slapi_ch_free((void **)&pvlv);
    }

    slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_db2index", "<=\n");

    return return_value;
}

/*
 * Determine if the given normalized 'attr' is to be excluded from LDIF
 * exports.
 *
 * Returns a non-zero value if:
 *    1) The 'attr' is in the configured list of attribute types that
 *       are to be excluded.
 * OR 2) dump_uniqueid is non-zero and 'attr' is the unique ID attribute.
 *
 * Return 0 if the attribute is not to be excluded.
 */
static int
dbmdb_ldbm_exclude_attr_from_export(struct ldbminfo *li, const char *attr, int dump_uniqueid)
{
    int i, rc = 0;

    if (!dump_uniqueid && 0 == strcasecmp(SLAPI_ATTR_UNIQUEID, attr)) {
        rc = 1; /* exclude */

    } else if (NULL != li && NULL != li->li_attrs_to_exclude_from_export) {
        for (i = 0; li->li_attrs_to_exclude_from_export[i] != NULL; ++i) {
            if (0 == strcasecmp(li->li_attrs_to_exclude_from_export[i],
                                attr)) {
                rc = 1; /* exclude */
                break;
            }
        }
    }

    return (rc);
}

/*
 * dbmdb_upgradedb -
 *
 * functions to convert idl from the old format to the new one
 * (604921) Support a database uprev process any time post-install
 */

int dbmdb_upgradedb_core(Slapi_PBlock *pb, ldbm_instance *inst);
int dbmdb_upgradedb_copy_logfiles(struct ldbminfo *li, char *destination_dir, int restore);
int dbmdb_upgradedb_delete_indices_4cmd(ldbm_instance *inst, int flags);

/*
 * dbmdb_upgradedb -
 *    check the MDB_dbiversion and if it's old idl'ed index,
 *    then reindex using new idl.
 *
 * standalone only -- not allowed to run while DS is up.
 */
int
dbmdb_upgradedb(Slapi_PBlock *pb)
{
#ifdef TODO

    struct ldbminfo *li;
    Object *inst_obj = NULL;
    ldbm_instance *inst = NULL;
    int run_from_cmdline = 0;
    int task_flags = 0;
    int server_startcfg = 0;
    int rval = 0;
    int backup_rval = 0;
    int upgrade_rval = 0;
    char *dest_dir = NULL;
    char *orig_dest_dir = NULL;
    char *home_dir = NULL;
    char *src_dbversion = NULL;
    char *dest_dbversion = NULL;
    int up_flags;
    Slapi_Task *task;
    char inst_dir[MAXPATHLEN];
    char *inst_dirp = NULL;
    int cnt = 0;
    PRFileInfo64 info = {0};
    PRUint32 dbversion_flags = DBVERSION_ALL;

    slapi_pblock_get(pb, SLAPI_SEQ_TYPE, &up_flags);
    slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_upgradedb", "Reindexing all...\n");
    slapi_pblock_get(pb, SLAPI_TASK_FLAGS, &task_flags);
    slapi_pblock_get(pb, SLAPI_BACKEND_TASK, &task);
    slapi_pblock_get(pb, SLAPI_DB2LDIF_SERVER_RUNNING, &server_startcfg);

    run_from_cmdline = (task_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE);
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);

    if (run_from_cmdline) {
        if (dbmdb_check_and_set_import_cache(li) < 0) {
            return -1;
        }
    } else {
        Object *inst_obj, *inst_obj2;
        ldbm_instance *inst = NULL;

        /* server is up -- mark all backends busy */
        slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_upgradedb",
                      "server is up -- marking all LDBM backends busy\n");
        for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
             inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
            inst = (ldbm_instance *)object_get_data(inst_obj);
            /* check if an import/restore is already ongoing... */
            /* BUSY flag is cleared at the end of dbmdb_import_main (join thread);
               it should not cleared in this thread [610347] */
            if (instance_set_busy(inst) != 0) {
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedb",
                              "ldbm: '%s' is already in the middle of "
                              "another task and cannot be disturbed.\n",
                              inst->inst_name);
                slapi_task_log_notice(task,
                        "Backend '%s' is already in the middle of another task and cannot be disturbed.\n",
                        inst->inst_name);

                /* painfully, we have to clear the BUSY flags on the
                 * backends we'd already marked...
                 */
                for (inst_obj2 = objset_first_obj(li->li_instance_set);
                     inst_obj2 && (inst_obj2 != inst_obj);
                     inst_obj2 = objset_next_obj(li->li_instance_set, inst_obj2)) {
                    inst = (ldbm_instance *)object_get_data(inst_obj2);
                    instance_set_not_busy(inst);
                }
                if (inst_obj2 && inst_obj2 != inst_obj)
                    object_release(inst_obj2);
                object_release(inst_obj);
                return -1;
            }
        }
    }
    if ((up_flags & SLAPI_UPGRADEDB_DN2RDN) && !entryrdn_get_switch()) {
        /*
         * DN2RDN option (-r) is given, but subtree-rename is off.
         * Print an error and back off.
         */
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedb",
                      "DN2RDN option (-r) is given, but %s is off in "
                      "dse.ldif.  Please change the value to on.\n",
                      CONFIG_ENTRYRDN_SWITCH);
        return -1;
    }

    inst_obj = objset_first_obj(li->li_instance_set);
    if (inst_obj) {
        inst = (ldbm_instance *)object_get_data(inst_obj);
        if (!(up_flags & SLAPI_UPGRADEDB_FORCE)) { /* upgrade idl to new */
            int need_upgrade = 0;
            li->li_flags |= LI_FORCE_MOD_CONFIG;
            /* set new idl */
            ldbm_config_internal_set(li, CONFIG_IDL_SWITCH, "new");
            /* First check the dbversion */
            rval = dbmdb_check_db_inst_version(inst);
            need_upgrade = (DBVERSION_NEED_IDL_OLD2NEW & rval);
            if (!need_upgrade && (up_flags & SLAPI_UPGRADEDB_DN2RDN)) {
                need_upgrade = (rval & DBVERSION_NEED_DN2RDN);
            }
            if (!need_upgrade) {
                need_upgrade = (rval & (DBVERSION_UPGRADE_3_4 | DBVERSION_UPGRADE_4_4));
            }
            if (!need_upgrade) {
                slapi_log_err(SLAPI_LOG_INFO, "dbmdb_upgradedb",
                              "Index version is up-to-date\n");
                return 0;
            }
        }
    } else {
        slapi_log_err(SLAPI_LOG_WARNING,
                      "dbmdb_upgradedb", "No instance to be upgraded\n");
        return -1;
    }

    /* we are going to go forward */
    /*
     * First, backup index files and checkpoint log files
     * since the server is not up and startcfg, we can just copy them.
     */
    slapi_pblock_get(pb, SLAPI_SEQ_VAL, &dest_dir);
    if (NULL == dest_dir) {
        slapi_log_err(SLAPI_LOG_ERR, "upgrade DB",
                      "Backup directory is not specified.\n");
        return -1;
    }

    orig_dest_dir = dest_dir;
    normalize_dir(dest_dir);
    /* clean up the backup dir first, then create it */
    rval = PR_GetFileInfo64(dest_dir, &info);
    if (PR_SUCCESS == rval) {
        if (PR_FILE_DIRECTORY == info.type) /* directory exists */
        {
            time_t tm = slapi_current_utc_time();

            char *tmpname = slapi_ch_smprintf("%s/%ld", dest_dir, tm);
            dest_dir = tmpname;
        } else /* not a directory */
            PR_Delete(dest_dir);
    }

    if (mkdir_p(dest_dir, 0700) < 0)
        goto fail0;

    if (run_from_cmdline)
        if (dbmdb_ctx_t_internal_set(li, CONFIG_DB_TRANSACTION_LOGGING, "off")) {
            goto fail1;
        }

    for (inst_obj = objset_first_obj(li->li_instance_set);
         inst_obj;
         inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
        if (run_from_cmdline) {
            /* need to call dbmdb_start for each instance,
               since dblayer_close is called in dbmdb_upgradedb_core =>
               dbmdb_back_ldif2db */
            if (0 != dbmdb_start(li, DBLAYER_IMPORT_MODE)) {
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedb",
                              "Failed to init database\n");
                goto fail1;
            }
        }

        inst = (ldbm_instance *)object_get_data(inst_obj);
        slapi_pblock_set(pb, SLAPI_BACKEND, inst->inst_be);
        slapi_pblock_set(pb, SLAPI_BACKEND_INSTANCE_NAME, inst->inst_name);

        /* Back up */
        inst_dirp = dblayer_get_full_inst_dir(inst->inst_li, inst,
                                              inst_dir, MAXPATHLEN);
        backup_rval = dbmdb_copy_directory(li, NULL /* task */,
                                             inst_dirp, dest_dir, 0 /*backup*/,
                                             &cnt, 0, 0);
        if (inst_dirp != inst_dir)
            slapi_ch_free_string(&inst_dirp);
        if (backup_rval < 0) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedb",
                          "Failed to backup index files (instance %s).\n", inst_dirp);
            goto fail1;
        }

        /* delete index files to be reindexed */
        if (run_from_cmdline) {
            rval = dbmdb_upgradedb_delete_indices_4cmd(inst, up_flags);
            if (rval) {
                upgrade_rval += rval;
                slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_upgradedb",
                              "Can't clean up indices in %s\n", inst->inst_dir_name);
                continue; /* Need to make all backups; continue */
            }
        } else {
            rval = dbmdb_delete_indices(inst);
            if (rval) {
                upgrade_rval += rval;
                slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_upgradedb",
                              "Can't clean up indices in %s\n", inst->inst_dir_name);
                continue; /* Need to make all backups; continue */
            }
        }

        rval = dbmdb_upgradedb_core(pb, inst);
        if (rval) {
            upgrade_rval += rval;
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedb",
                          "upgradedb: Failed to upgrade database %s\n",
                          inst->inst_name);
            if (run_from_cmdline) {
                continue; /* Need to make all backups; continue */
            }
        }
    }

    /* copy transaction logs */
    backup_rval += dbmdb_upgradedb_copy_logfiles(li, dest_dir, 0);

    /* copy DBVERSION */
    home_dir = dbmdb_get_home_dir(li, NULL);
    src_dbversion = slapi_ch_smprintf("%s/%s", home_dir, DBVERSION_FILENAME);
    dest_dbversion = slapi_ch_smprintf("%s/%s", dest_dir, DBVERSION_FILENAME);
    backup_rval += dbmdb_copyfile(src_dbversion, dest_dbversion, 0, 0600);

    if (upgrade_rval) {
        goto fail1;
    }

    /* upgrade idl to new; otherwise no need to modify idl-switch */
    if (!(up_flags & SLAPI_UPGRADEDB_FORCE)) {
        replace_ldbm_config_value(CONFIG_IDL_SWITCH, "new", li);
    }

    /* write db version files */
    dbmdb_version_write(li, home_dir, NULL, DBVERSION_ALL);

    if ((up_flags & SLAPI_UPGRADEDB_DN2RDN) && entryrdn_get_switch()) {
        /* exclude dnformat to allow upgradednformat later */
        dbversion_flags = DBVERSION_ALL ^ DBVERSION_DNFORMAT;
        ;
    }
    inst_obj = objset_first_obj(li->li_instance_set);
    while (NULL != inst_obj) {
        char *inst_dirp = NULL;
        inst_dirp = dblayer_get_full_inst_dir(li, inst, inst_dir, MAXPATHLEN);
        inst = (ldbm_instance *)object_get_data(inst_obj);
        dbmdb_version_write(li, inst_dirp, NULL, dbversion_flags);
        inst_obj = objset_next_obj(li->li_instance_set, inst_obj);
        if (inst_dirp != inst_dir)
            slapi_ch_free_string(&inst_dirp);
    }

    /* close the database down again */
    if (run_from_cmdline) {
        if (0 != dblayer_close(li, DBLAYER_IMPORT_MODE)) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedb",
                          "Failed to close database\n");
            goto fail1;
        }
    }

    /* delete backup */
    if (NULL != dest_dir)
        ldbm_delete_dirs(dest_dir);

    if (dest_dir != orig_dest_dir)
        slapi_ch_free_string(&dest_dir);

    slapi_ch_free_string(&src_dbversion);
    slapi_ch_free_string(&dest_dbversion);

    return 0;

fail1:

    /* we started dblayer with DBLAYER_IMPORT_MODE
     * We just want not to generate a guardian file...
     */
    if (0 != dblayer_close(li, DBLAYER_ARCHIVE_MODE))
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedb",
                      "Failed to close database\n");

    /* restore from the backup, if possible */
    if (NULL != dest_dir) {
        /* If the backup was successfull and ugrade failed... */
        if ((0 == backup_rval) && upgrade_rval) {
            backup_rval = dbmdb_restore(li, dest_dir, NULL);
        }
        /* restore is done; clean up the backup dir */
        if (0 == backup_rval) {
            ldbm_delete_dirs(dest_dir);
        }
    }
    slapi_ch_free_string(&src_dbversion);
    slapi_ch_free_string(&dest_dbversion);

fail0:
    if (dest_dir != orig_dest_dir)
        slapi_ch_free_string(&dest_dir);

    return rval + upgrade_rval;
#endif /* TODO */
}

#define LOG "log."
#define LOGLEN 4
int
dbmdb_upgradedb_copy_logfiles(struct ldbminfo *li, char *destination_dir, int restore)
{
#ifdef TODO
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    char *src;
    char *dest;
    int srclen;
    int destlen;
    int rval = 0;
    int len0 = 0;
    int len1 = 0;
    char *from = NULL;
    char *to = NULL;

    if (restore) {
        src = destination_dir;
        dest = li->li_directory;
    } else {
        src = li->li_directory;
        dest = destination_dir;
    }
    if (NULL == src || '\0' == *src) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedb_copy_logfiles",
                      "NULL src directory\n");
        return -1;
    }
    if (NULL == dest || '\0' == *dest) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedb_copy_logfiles",
                      "NULL dest directory\n");
        return -1;
    }
    srclen = strlen(src);
    destlen = strlen(dest);

    /* Open the instance dir so we can look what's in it. */
    dirhandle = PR_OpenDir(src);
    if (NULL == dirhandle)
        return -1;

    while (NULL != (direntry =
                        PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
        if (NULL == direntry->name)
            break;

        if (0 == strncmp(direntry->name, LOG, 4)) {
            int filelen = strlen(direntry->name);
            char *p, *endp;
            int fromlen, tolen;
            int notalog = 0;

            endp = (char *)direntry->name + filelen;
            for (p = (char *)direntry->name + LOGLEN; p < endp; p++) {
                if (!isdigit(*p)) {
                    notalog = 1;
                    break;
                }
            }
            if (notalog)
                continue; /* go to next file */

            fromlen = srclen + filelen + 2;
            if (len0 < fromlen) {
                slapi_ch_free_string(&from);
                from = slapi_ch_calloc(1, fromlen);
                len0 = fromlen;
            }
            PR_snprintf(from, len0, "%s/%s", src, direntry->name);
            tolen = destlen + filelen + 2;
            if (len1 < tolen) {
                slapi_ch_free_string(&to);
                to = slapi_ch_calloc(1, tolen);
                len1 = tolen;
            }
            PR_snprintf(to, len1, "%s/%s", dest, direntry->name);
            rval = dbmdb_copyfile(from, to, 1, DEFAULT_MODE);
            if (rval < 0)
                break;
        }
    }
    slapi_ch_free_string(&from);
    slapi_ch_free_string(&to);
    PR_CloseDir(dirhandle);

    return rval;
#endif /* TODO */
}

int
dbmdb_upgradedb_delete_indices_4cmd(ldbm_instance *inst, int flags __attribute__((unused)))
{
#ifdef TODO
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    int rval = 0;
    char fullpath[MAXPATHLEN];
    char *fullpathp = fullpath;
    char inst_dir[MAXPATHLEN];
    char *inst_dirp = dblayer_get_full_inst_dir(inst->inst_li, inst,
                                                inst_dir, MAXPATHLEN);

    slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_upgradedb_delete_indices_4cmd",
                  "%s\n", inst_dir);
    dirhandle = PR_OpenDir(inst_dirp);
    if (!dirhandle) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedb_delete_indices_4cmd",
                      "PR_OpenDir failed\n");
        if (inst_dirp != inst_dir)
            slapi_ch_free_string(&inst_dirp);
        return -1;
    }

    while (NULL != (direntry =
                        PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
        PRFileInfo64 info;
        int len;

        if (!direntry->name)
            break;

        if (0 == strcmp(direntry->name, ID2ENTRY LDBM_FILENAME_SUFFIX))
            continue;

        len = strlen(inst_dirp) + strlen(direntry->name) + 2;
        if (len > MAXPATHLEN) {
            fullpathp = (char *)slapi_ch_malloc(len);
        }
        sprintf(fullpathp, "%s/%s", inst_dirp, direntry->name);
        rval = PR_GetFileInfo64(fullpathp, &info);
        if (PR_SUCCESS == rval && PR_FILE_DIRECTORY != info.type) {
            PR_Delete(fullpathp);
            slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_upgradedb_delete_indices_4cmd",
                          "%s deleted\n", fullpath);
        }
        if (fullpathp != fullpath)
            slapi_ch_free_string(&fullpathp);
    }
    PR_CloseDir(dirhandle);
    if (inst_dirp != inst_dir)
        slapi_ch_free_string(&inst_dirp);
    return rval;
#endif /* TODO */
}

/*
 * upgradedb_core
 */
int
dbmdb_upgradedb_core(Slapi_PBlock *pb, ldbm_instance *inst)
{
#ifdef TODO
    backend *be = NULL;
    int task_flags = 0;
    int run_from_cmdline = 0;

    slapi_pblock_get(pb, SLAPI_TASK_FLAGS, &task_flags);
    run_from_cmdline = (task_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE);

    be = inst->inst_be;
    slapi_log_err(SLAPI_LOG_INFO, "dbmdb_upgradedb_core",
                  "%s: Start upgradedb.\n", inst->inst_name);

    if (!run_from_cmdline) {
        /* shutdown this instance of the db */
        slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_upgradedb_core",
                      "Bringing %s offline...\n", inst->inst_name);
        slapi_mtn_be_disable(inst->inst_be);

        cache_clear(&inst->inst_cache, CACHE_TYPE_ENTRY);
        if (entryrdn_get_switch()) {
            cache_clear(&inst->inst_dncache, CACHE_TYPE_DN);
        }
        dblayer_instance_close(be);
    }

    /* dbmdb_instance_start will init the id2entry index. */
    if (0 != dbmdb_instance_start(be, DBLAYER_IMPORT_MODE)) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedb_core",
                      "Failed to init instance %s\n", inst->inst_name);
        return -1;
    }

    if (run_from_cmdline)
        vlv_init(inst); /* Initialise the Virtual List View code */

    return dbmdb_back_ldif2db(pb);
#endif /* TODO */
}

/* Used by the reindex and export (subtree rename must be on)*/
/* Note: If DB2LDIF_ENTRYRDN or DB2INDEX_ENTRYRDN is set to index_ext,
 *       the specified operation is executed.
 *       If 0 is passed, just Slapi_RDN srdn is filled and returned.
 */
static int
_get_and_add_parent_rdns(backend *be,
                         dbmdb_cursor_t *cur, /* Contains dbi and txn (cur->cursor should be ignored) */
                         ID id,               /* input */
                         Slapi_RDN *srdn,     /* output */
                         ID *pid,             /* output */
                         int index_ext,       /* DB2LDIF_ENTRYRDN | DB2INDEX_ENTRYRDN | 0 */
                         int run_from_cmdline,
                         export_args *eargs)
{
    int rc = -1;
    Slapi_RDN mysrdn = {0};
    struct backdn *bdn = NULL;
    ldbm_instance *inst = NULL;
    struct ldbminfo *li = NULL;
    struct backentry *ep = NULL;
    char *rdn = NULL;
    MDB_val key, data;
    char *pid_str = NULL;
    ID storedid;
    ID temp_pid = NOID;

    if (!entryrdn_get_switch()) { /* entryrdn specific code */
        return rc;
    }

    if (NULL == be || NULL == srdn) {
        slapi_log_err(SLAPI_LOG_ERR, "_get_and_add_parent_rdns",
                      "Empty %s\n", NULL == be ? "be" : "srdn");
        return rc;
    }

    inst = (ldbm_instance *)be->be_instance_info;
    li = inst->inst_li;
    memset(&data, 0, sizeof(data));

    /* first, try the dn cache */
    bdn = dncache_find_id(&inst->inst_dncache, id);
    if (bdn) {
        /* Luckily, found the parent in the dn cache!  */
        if (slapi_rdn_get_rdn(srdn)) { /* srdn is already in use */
            rc = slapi_rdn_init_all_dn(&mysrdn, slapi_sdn_get_dn(bdn->dn_sdn));
            if (rc) {
                slapi_log_err(SLAPI_LOG_ERR, "_get_and_add_parent_rdns",
                              "Failed to convert DN %s to RDN\n",
                              slapi_rdn_get_rdn(&mysrdn));
                slapi_rdn_done(&mysrdn);
                CACHE_RETURN(&inst->inst_dncache, &bdn);
                goto bail;
            }
            rc = slapi_rdn_add_srdn_to_all_rdns(srdn, &mysrdn);
            if (rc) {
                slapi_log_err(SLAPI_LOG_ERR, "_get_and_add_parent_rdns",
                              "Failed to merge Slapi_RDN %s to RDN\n",
                              slapi_sdn_get_dn(bdn->dn_sdn));
            }
            slapi_rdn_done(&mysrdn);
        } else { /* srdn is empty */
            rc = slapi_rdn_init_all_dn(srdn, slapi_sdn_get_dn(bdn->dn_sdn));
            if (rc) {
                slapi_log_err(SLAPI_LOG_ERR, "_get_and_add_parent_rdns",
                              "Failed to convert DN %s to RDN\n",
                              slapi_sdn_get_dn(bdn->dn_sdn));
                CACHE_RETURN(&inst->inst_dncache, &bdn);
                goto bail;
            }
        }
        CACHE_RETURN(&inst->inst_dncache, &bdn);
    }

    if (!bdn || (index_ext & (DB2LDIF_ENTRYRDN | DB2INDEX_ENTRYRDN)) || pid) {
        /* not in the dn cache or DB2LDIF or caller is expecting the parent ID;
         * read id2entry */
        id_internal_to_stored(id, (char *)&storedid);
        key.mv_size = sizeof(ID);
        key.mv_data = &storedid;

        memset(&data, 0, sizeof(data));
        rc = mdb_get(TXN(cur->txn), cur->dbi.dbi, &key, &data);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "_get_and_add_parent_rdns",
                          "Failed to position cursor at ID " ID_FMT "\n", id);
            goto bail;
        }
        /* rdn is allocated in get_value_from_string */
        rc = get_value_from_string((const char *)data.mv_data, "rdn", &rdn);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "_get_and_add_parent_rdns",
                          "Failed to get rdn of entry " ID_FMT "\n", id);
            goto bail;
        }
        /* rdn is going to be set to srdn */
        rc = slapi_rdn_init_all_dn(&mysrdn, rdn);
        if (rc < 0) { /* expect rc == 1 since we are setting "rdn" not "dn" */
            slapi_log_err(SLAPI_LOG_ERR, "_get_and_add_parent_rdns",
                          "Failed to add rdn %s of entry " ID_FMT "\n", rdn, id);
            goto bail;
        }
        /* pid */
        rc = get_value_from_string((const char *)data.mv_data,
                                   LDBM_PARENTID_STR, &pid_str);
        if (rc) {
            rc = 0; /* assume this is a suffix */
            temp_pid = NOID;
        } else {
            temp_pid = (ID)strtol(pid_str, (char **)NULL, 10);
            slapi_ch_free_string(&pid_str);
        }
        if (pid) {
            *pid = temp_pid;
        }
    }
    if (!bdn) {
        if (NOID != temp_pid) {
            rc = _get_and_add_parent_rdns(be, cur, temp_pid, &mysrdn, NULL,
                                          id < temp_pid ? index_ext : 0, run_from_cmdline, eargs);
            if (rc) {
                goto bail;
            }
        }
        rc = slapi_rdn_add_srdn_to_all_rdns(srdn, &mysrdn);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "_get_and_add_parent_rdns",
                          "Failed to merge Slapi_RDN %s to RDN\n",
                          slapi_rdn_get_rdn(&mysrdn));
            goto bail;
        }
    }

    if (index_ext & (DB2LDIF_ENTRYRDN | DB2INDEX_ENTRYRDN)) {
        char *dn = NULL;
        ep = backentry_alloc();
        rc = slapi_rdn_get_dn(srdn, &dn);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "_get_and_add_parent_rdns",
                          "Failed to compose dn for "
                          "(rdn: %s, ID: %d) from Slapi_RDN\n",
                          rdn, id);
            goto bail;
        }
        ep->ep_entry = slapi_str2entry_ext(dn, NULL, data.mv_data,
                                           SLAPI_STR2ENTRY_NO_ENTRYDN);
        ep->ep_id = id;
        slapi_ch_free_string(&dn);
    }

    if (index_ext & DB2INDEX_ENTRYRDN) {
        rc = entryrdn_index_entry(be, ep, BE_INDEX_ADD, cur->txn);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "_get_and_add_parent_rdns",
                          "%s: Failed to update index 'entryrdn'\n",
                          inst->inst_name);
            slapi_log_err(SLAPI_LOG_ERR, "_get_and_add_parent_rdns",
                          "%s: Error %d: %s\n", inst->inst_name, rc,
                          dblayer_strerror(rc));
            goto bail;
        }
    } else if (index_ext & DB2LDIF_ENTRYRDN) {
        if (NULL == eargs) {
            slapi_log_err(SLAPI_LOG_ERR, "_get_and_add_parent_rdns",
                          "Empty export args\n");
            rc = -1;
            goto bail;
        }
        eargs->ep = ep;
        rc = dbmdb_export_one_entry(li, inst, eargs);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "_get_and_add_parent_rdns",
                          "Failed to export an entry %s\n",
                          slapi_sdn_get_dn(slapi_entry_get_sdn(ep->ep_entry)));
            goto bail;
        }
        rc = idl_append_extend(&(eargs->pre_exported_idl), id);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "_get_and_add_parent_rdns",
                          "Failed add %d to exported idl\n", id);
        }
    }

bail:
    backentry_free(&ep);
    slapi_rdn_done(&mysrdn);
    slapi_ch_free_string(&rdn);
    return rc;
}

/* Used by the reindex and export (subtree rename must be on)*/
static int
_export_or_index_parents(ldbm_instance *inst,
                         dbmdb_cursor_t *cur, /* Contains dbi and txn (cur->cursor should be ignored) */
                         ID currentid, /* current id to compare with */
                         char *rdn,    /* my rdn */
                         ID id,        /* my id */
                         ID pid,       /* parent id */
                         int run_from_cmdline,
                         export_args *eargs,
                         int type, /* DB2LDIF_ENTRYRDN or DB2INDEX_ENTRYRDN */
                         Slapi_RDN *psrdn /* output */)
{
    int rc = -1;
    ID temp_pid = 0;
    char *prdn = NULL;
    Slapi_DN *psdn = NULL;
    ID ppid = 0;
    char *pprdn = NULL;
    backend *be = inst->inst_be;

    if (!entryrdn_get_switch()) { /* entryrdn specific code */
        return rc;
    }

    /* in case the parent is not already exported */
    rc = entryrdn_get_parent(be, rdn, id, &prdn, &temp_pid, NULL);
    if (rc) { /* entryrdn is not available. */
        /* get the parent info from the id2entry (no add) */
        rc = _get_and_add_parent_rdns(be, cur, pid, psrdn, &ppid, 0,
                                      run_from_cmdline, NULL);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "_export_or_index_parents",
                          "Failed to get the DN of ID %d\n", pid);
            goto bail;
        }
        prdn = slapi_ch_strdup(slapi_rdn_get_rdn(psrdn));
    } else { /* we have entryrdn */
        if (pid != temp_pid) {
            slapi_log_err(SLAPI_LOG_WARNING, "_export_or_index_parents",
                          "parentid conflict found between entryrdn (%d) and "
                          "id2entry (%d)\n",
                          temp_pid, pid);
            slapi_log_err(SLAPI_LOG_WARNING, "_export_or_index_parents",
                          "Ignoring entryrdn\n");
        } else {
            struct backdn *bdn = NULL;
            char *pdn = NULL;

            bdn = dncache_find_id(&inst->inst_dncache, pid);
            if (!bdn) {
                /* we put pdn to dn cache, which could be used
                 * in _get_and_add_parent_rdns */
                rc = entryrdn_lookup_dn(be, prdn, pid, &pdn, NULL, NULL);
                if (0 == rc) {
                    int myrc = 0;
                    /* pdn is put in DN cache.  No need to free it here,
                     * since it'll be free'd when evicted from the cache. */
                    psdn = slapi_sdn_new_dn_passin(pdn);
                    bdn = backdn_init(psdn, pid, 0);
                    myrc = CACHE_ADD(&inst->inst_dncache, bdn, NULL);
                    if (myrc) {
                        backdn_free(&bdn);
                        slapi_log_err(SLAPI_LOG_CACHE,
                                      "_export_or_index_parents",
                                      "%s is already in the dn cache (%d)\n",
                                      pdn, myrc);
                    } else {
                        CACHE_RETURN(&inst->inst_dncache, &bdn);
                        slapi_log_err(SLAPI_LOG_CACHE,
                                      "_export_or_index_parents",
                                      "entryrdn_lookup_dn returned: %s, "
                                      "and set to dn cache\n",
                                      pdn);
                    }
                }
            }
        }
    }

    /* check one more upper level */
    if (0 == ppid) {
        rc = entryrdn_get_parent(be, prdn, pid, &pprdn, &ppid, NULL);
        slapi_ch_free_string(&pprdn);
        if (rc) { /* entryrdn is not available */
            slapi_log_err(SLAPI_LOG_ERR, "_export_or_index_parents",
                          "Failed to get the parent of ID %d\n", pid);
            goto bail;
        }
    }
    if (ppid > currentid &&
        (!eargs || !idl_id_is_in_idlist(eargs->pre_exported_idl, ppid))) {
        Slapi_RDN ppsrdn = {0};
        rc = _export_or_index_parents(inst, cur, currentid, prdn, pid,
                                      ppid, run_from_cmdline, eargs, type, &ppsrdn);
        if (rc) {
            goto bail;
        }
        slapi_rdn_done(&ppsrdn);
    }
    slapi_rdn_done(psrdn);
    rc = _get_and_add_parent_rdns(be, cur, pid, psrdn, NULL,
                                  type, run_from_cmdline, eargs);
    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "_export_or_index_parents", "Failed to get rdn for ID: %d\n", pid);
        slapi_rdn_done(psrdn);
    }
bail:
    slapi_ch_free_string(&prdn);
    return rc;
}

/*
 * dbmdb_upgradednformat
 *
 * Update old DN format in entrydn and the leaf attr value to the new one
 *
 * The implementation would be similar to the upgradedb for new idl.
 * Scan each entry, checking the entrydn value with the normalized dn.
 * If they don't match,
 *   replace the old entrydn value with the new one in the entry
 *   in id2entry.db4.
 *   also get the leaf RDN attribute value, unescape it, and check
 *   if it is in the entry.  If not, add it.
 * Then, update the key in the entrydn index and the leaf RDN attribute
 * (if need it).
 *
 * Return value:  0: success (the backend instance includes update
 *                   candidates for DRYRUN mode)
 *                1: the backend instance is up-to-date (DRYRUN mode only)
 *               -1: error
 *
 * standalone only -- not allowed to run while DS is up.
 */
int
dbmdb_upgradednformat(Slapi_PBlock *pb)
{
#ifdef TODO
    int rc = -1;
    struct ldbminfo *li = NULL;
    int run_from_cmdline = 0;
    int task_flags = 0;
    int server_startcfg = 0;
    Slapi_Task *task;
    ldbm_instance *inst = NULL;
    char *instance_name = NULL;
    backend *be = NULL;
    PRStatus prst = 0;
    PRFileInfo64 prfinfo = {0};
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    size_t id2entrylen = 0;
    int found = 0;
    char *rawworkdbdir = NULL;
    char *workdbdir = NULL;
    char *origdbdir = NULL;
    char *origlogdir = NULL;
    char *originstparentdir = NULL;
    char *sep = NULL;
    char *ldbmversion = NULL;
    char *dataversion = NULL;
    int ud_flags = 0;
    int result = 0;

    slapi_pblock_get(pb, SLAPI_TASK_FLAGS, &task_flags);
    slapi_pblock_get(pb, SLAPI_BACKEND_TASK, &task);
    slapi_pblock_get(pb, SLAPI_DB2LDIF_SERVER_RUNNING, &server_startcfg);
    slapi_pblock_get(pb, SLAPI_BACKEND_INSTANCE_NAME, &instance_name);
    slapi_pblock_get(pb, SLAPI_SEQ_TYPE, &ud_flags);

    run_from_cmdline = (task_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE);
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    if (run_from_cmdline) {
        dbmdb_ctx_t_load_dse_info(li);
        if (dbmdb_check_and_set_import_cache(li) < 0) {
            return -1;
        }
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradednformat",
                      " Online mode is not supported. "
                      "Shutdown the server and run the tool\n");
        goto bail;
    }

    /* Find the instance that the ldif2db will be done on. */
    inst = ldbm_instance_find_by_name(li, instance_name);
    if (NULL == inst) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradednformat",
                      "Unknown ldbm instance %s\n", instance_name);
        goto bail;
    }
    slapi_log_err(SLAPI_LOG_INFO, "dbmdb_upgradednformat",
                  "%s: Start upgrade dn format.\n", inst->inst_name);

    slapi_pblock_set(pb, SLAPI_BACKEND, inst->inst_be);
    slapi_pblock_get(pb, SLAPI_SEQ_VAL, &rawworkdbdir);
    normalize_dir(rawworkdbdir); /* remove trailing spaces and slashes */

    prst = PR_GetFileInfo64(rawworkdbdir, &prfinfo);
    if (PR_FAILURE == prst || PR_FILE_DIRECTORY != prfinfo.type) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradednformat",
                      "Working MDB_dbiinstance dir %s is not a directory\n",
                      rawworkdbdir);
        goto bail;
    }
    dirhandle = PR_OpenDir(rawworkdbdir);
    if (!dirhandle) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradednformat",
                      "Failed to open working MDB_dbiinstance dir %s\n",
                      rawworkdbdir);
        goto bail;
    }
    id2entrylen = strlen(ID2ENTRY);
    while ((direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
        if (!direntry->name)
            break;
        if (0 == strncasecmp(ID2ENTRY, direntry->name, id2entrylen)) {
            found = 1;
            break;
        }
    }
    PR_CloseDir(dirhandle);

    if (!found) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradednformat",
                      "Working MDB_dbiinstance dir %s does not include %s file\n",
                      rawworkdbdir, ID2ENTRY);
        goto bail;
    }

    if (run_from_cmdline) {
        if (dbmdb_ctx_t_internal_set(li, CONFIG_DB_TRANSACTION_LOGGING, "off")){
            goto bail;
        }
    }

    /* We have to work on the copied db.  So, the path should be set here. */
    origdbdir = li->li_directory;
    origlogdir = MDB_CONFIG(li)->dbmdb_log_directory;
    originstparentdir = inst->inst_parent_dir_name;

    workdbdir = rel2abspath(rawworkdbdir);

    result = dbmdb_version_read(li, workdbdir, &ldbmversion, &dataversion);
    if (result == 0 && ldbmversion) {
        char *ptr = PL_strstr(ldbmversion, BDB_DNFORMAT);
        if (ptr) {
            /* DN format is RFC 4514 compliant */
            if (strlen(ptr) == strlen(BDB_DNFORMAT)) { /* no version */
                /*
                 * DN format is RFC 4514 compliant.
                 * But it hasn't taken care of the multiple spaces yet.
                 */
                ud_flags &= ~SLAPI_UPGRADEDNFORMAT;
                ud_flags |= SLAPI_UPGRADEDNFORMAT_V1;
                slapi_pblock_set(pb, SLAPI_SEQ_TYPE, &ud_flags);
                rc = 3; /* 0: need upgrade (dn norm sp, only) */
            } else {
                /* DN format already takes care of the multiple spaces */
                slapi_log_err(SLAPI_LOG_INFO, "dbmdb_upgradednformat",
                              "Instance %s in %s is up-to-date\n",
                              instance_name, workdbdir);
                rc = 0; /* 0: up-to-date */
                goto bail;
            }
        } else {
            /* DN format is not RFC 4514 compliant */
            ud_flags |= SLAPI_UPGRADEDNFORMAT | SLAPI_UPGRADEDNFORMAT_V1;
            slapi_pblock_set(pb, SLAPI_SEQ_TYPE, &ud_flags);
            rc = 1; /* 0: need upgrade (both) */
        }
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradednformat",
                      "Failed to get DBVERSION (Instance name: %s, dir %s)\n",
                      instance_name, workdbdir);
        rc = -1; /* error */
        goto bail;
    }

    sep = PL_strrchr(workdbdir, '/');
    if (!sep) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradednformat",
                      "Working MDB_dbiinstance dir %s does not include %s file\n",
                      workdbdir, ID2ENTRY);
        goto bail;
    }
    *sep = '\0';
    li->li_directory = workdbdir;
    MDB_CONFIG(li)->dbmdb_log_directory = workdbdir;
    inst->inst_parent_dir_name = workdbdir;

    if (run_from_cmdline) {
        if (0 != dbmdb_start(li, DBLAYER_IMPORT_MODE)) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradednformat",
                          "Failed to init database\n");
            goto bail;
        }
    }

    /* dbmdb_instance_start will init the id2entry index. */
    be = inst->inst_be;
    if (0 != dbmdb_instance_start(be, DBLAYER_IMPORT_MODE)) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradednformat",
                      "Failed to init instance %s\n", inst->inst_name);
        goto bail;
    }

    if (run_from_cmdline) {
        vlv_init(inst); /* Initialise the Virtual List View code */
    }

    rc = dbmdb_back_ldif2db(pb);

    /* close the database */
    if (run_from_cmdline) {
        if (0 != dblayer_close(li, DBLAYER_IMPORT_MODE)) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradednformat",
                          "Failed to close database\n");
            goto bail;
        }
    }
    *sep = '/';
    if (((0 == rc) && !(ud_flags & SLAPI_DRYRUN)) ||
        ((rc == 0) && (ud_flags & SLAPI_DRYRUN))) {
        /* modify the DBVERSION files if the DN upgrade was successful OR
         * if DRYRUN, the backend instance is up-to-date. */
        dbmdb_version_write(li, workdbdir, NULL, DBVERSION_ALL); /* inst db dir */
    }
    /* Remove the MDB_dbienv files */
    dbmdb_remove_env(li);

    li->li_directory = origdbdir;
    MDB_CONFIG(li)->dbmdb_log_directory = origlogdir;
    inst->inst_parent_dir_name = originstparentdir;

bail:
    slapi_ch_free_string(&workdbdir);
    slapi_ch_free_string(&ldbmversion);
    slapi_ch_free_string(&dataversion);
    return rc;
#endif /* TODO */
}

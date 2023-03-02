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

/* bdb_config.c - Handles configuration information that is specific to a BDB backend instance. */

#include "bdb_layer.h"

/* Forward declarations */
static int bdb_parse_bdb_config_entry(struct ldbminfo *li, Slapi_Entry *e, config_info *config_array);
static void bdb_split_bdb_config_entry(struct ldbminfo *li, Slapi_Entry *ldbm_conf_e,Slapi_Entry *bdb_conf_e, config_info *config_array, Slapi_Mods *smods);

/* Forward callback declarations */
int bdb_config_search_entry_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int bdb_config_modify_entry_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);

static dblayer_private bdb_fake_priv;   /* A copy of the callback array used by bdb_be() */

static int
_bdb_log_version(bdb_config *priv)
{
    int major, minor = 0;
    char *string = 0;
    int ret = 0;

    string = db_version(&major, &minor, NULL);
    priv->bdb_lib_version = DBLAYER_LIB_VERSION_POST_24;
    slapi_log_err(SLAPI_LOG_TRACE, "_dblayer_check_version", "version check: %s (%d.%d)\n", string, major, minor);
    return ret;
}

backend *bdb_be(void)
{
    static backend be = {0};
    static struct slapdplugin plg = {0};
    static struct ldbminfo li = {0};

    if (be.be_database == NULL) {
        be.be_database = &plg;
        plg.plg_private = &li;
        li.li_dblayer_private = &bdb_fake_priv;
    }
    return &be;
}

int bdb_init(struct ldbminfo *li, config_info *config_array)
{
    bdb_config *conf = (bdb_config *)slapi_ch_calloc(1, sizeof(bdb_config));
    if (NULL == conf) {
        /* Memory allocation failed */
        return -1;
    }

    li->li_dblayer_config = conf;
    bdb_config_setup_default(li);
    _bdb_log_version(conf);

    /* write DBVERSION file if one does not exist
    char *home_dir = bdb_get_home_dir(li, NULL);
    if (!bdb_version_exists(li, home_dir)) {
        bdb_version_write(li, home_dir, NULL, DBVERSION_ALL);
    }
    */

    dblayer_private *priv = li->li_dblayer_private;
    priv->dblayer_start_fn = &bdb_start;
    priv->dblayer_close_fn = &bdb_close;
    priv->dblayer_instance_start_fn = &bdb_instance_start;
    priv->dblayer_backup_fn = &bdb_backup;
    priv->dblayer_verify_fn = &bdb_verify;
    priv->dblayer_db_size_fn = &bdb_db_size;
    priv->dblayer_ldif2db_fn = &bdb_ldif2db;
    priv->dblayer_db2ldif_fn = &bdb_db2ldif;
    priv->dblayer_db2index_fn = &bdb_db2index;
    priv->dblayer_cleanup_fn = &bdb_cleanup;
    priv->dblayer_upgradedn_fn = &bdb_upgradednformat;
    priv->dblayer_upgradedb_fn = &bdb_upgradedb;
    priv->dblayer_restore_fn = &bdb_restore;
    priv->dblayer_txn_begin_fn = &bdb_txn_begin;
    priv->dblayer_txn_commit_fn = &bdb_txn_commit;
    priv->dblayer_txn_abort_fn = &bdb_txn_abort;
    priv->dblayer_get_info_fn = &bdb_get_info;
    priv->dblayer_set_info_fn = &bdb_set_info;
    priv->dblayer_back_ctrl_fn = &bdb_back_ctrl;
    priv->dblayer_get_db_fn = &bdb_get_db;
    priv->dblayer_rm_db_file_fn = &bdb_rm_db_file;
    priv->dblayer_delete_db_fn = &bdb_delete_db;
    priv->dblayer_import_fn = &bdb_public_bdb_import_main;
    priv->dblayer_load_dse_fn = &bdb_config_load_dse_info;
    priv->dblayer_config_get_fn = &bdb_public_config_get;
    priv->dblayer_config_set_fn = &bdb_public_config_set;
    priv->instance_config_set_fn = &bdb_instance_config_set;
    priv->instance_add_config_fn = &bdb_instance_add_instance_entry_callback;
    priv->instance_postadd_config_fn = &bdb_instance_postadd_instance_entry_callback;
    priv->instance_del_config_fn = &bdb_instance_delete_instance_entry_callback;
    priv->instance_postdel_config_fn = &bdb_instance_post_delete_instance_entry_callback;
    priv->instance_cleanup_fn = &bdb_instance_cleanup;
    priv->instance_create_fn = &bdb_instance_create;
    priv->instance_register_monitor_fn = &bdb_instance_register_monitor;
    priv->instance_search_callback_fn = &bdb_instance_search_callback;
    priv->dblayer_auto_tune_fn = &bdb_start_autotune;
    priv->dblayer_get_db_filename_fn = &bdb_public_get_db_filename;
    priv->dblayer_bulk_free_fn = &bdb_public_bulk_free;
    priv->dblayer_bulk_nextdata_fn = &bdb_public_bulk_nextdata;
    priv->dblayer_bulk_nextrecord_fn = &bdb_public_bulk_nextrecord;
    priv->dblayer_bulk_init_fn = &bdb_public_bulk_init;
    priv->dblayer_bulk_start_fn = &bdb_public_bulk_start;
    priv->dblayer_cursor_bulkop_fn = &bdb_public_cursor_bulkop;
    priv->dblayer_cursor_op_fn = &bdb_public_cursor_op;
    priv->dblayer_db_op_fn = &bdb_public_db_op;
    priv->dblayer_new_cursor_fn = &bdb_public_new_cursor;
    priv->dblayer_value_free_fn = &bdb_public_value_free;
    priv->dblayer_value_init_fn = &bdb_public_value_init;
    priv->dblayer_set_dup_cmp_fn = &bdb_public_set_dup_cmp_fn;
    priv->dblayer_dbi_txn_begin_fn = &bdb_dbi_txn_begin;
    priv->dblayer_dbi_txn_commit_fn = &bdb_dbi_txn_commit;
    priv->dblayer_dbi_txn_abort_fn = &bdb_dbi_txn_abort;
    priv->dblayer_get_entries_count_fn = &bdb_get_entries_count;
    priv->dblayer_cursor_get_count_fn = &bdb_public_cursor_get_count;
    priv->dblayer_private_open_fn = &bdb_public_private_open;
    priv->dblayer_private_close_fn = &bdb_public_private_close;
    priv->ldbm_back_wire_import_fn = &bdb_ldbm_back_wire_import;
    priv->dblayer_restore_file_init_fn = &bdb_restore_file_init;
    priv->dblayer_restore_file_update_fn = &bdb_restore_file_update;
    priv->dblayer_import_file_check_fn = &bdb_import_file_check;
    priv->dblayer_list_dbs_fn = &bdb_list_dbs;
    priv->dblayer_in_import_fn = &bdb_public_in_import;
    priv->dblayer_get_db_suffix_fn = &bdb_public_get_db_suffix;
    priv->dblayer_compact_fn = &bdb_public_dblayer_compact;
    priv->dblayer_dbi_db_remove_fn = &bdb_public_delete_db;
    priv->dblayer_cursor_iterate_fn = &bdb_dblayer_cursor_iterate;

    bdb_fake_priv = *priv; /* Copy the callbaks for bdb_be() */
    return 0;
}

/* Used to add an array of entries, like the one above and
 * bdb_instance_skeleton_entries in bdb_instance_config.c, to the dse.
 * Returns 0 on success.
 */
int
bdb_config_add_dse_entries(struct ldbminfo *li, char **entries, char *string1, char *string2, char *string3, int flags)
{
    int x;
    Slapi_Entry *e;
    Slapi_PBlock *util_pb = NULL;
    int rc;
    int result;
    char entry_string[512];
    int dont_write_file = 0;
    char ebuf[BUFSIZ];

    if (flags & LDBM_INSTANCE_CONFIG_DONT_WRITE) {
        dont_write_file = 1;
    }

    for (x = 0; strlen(entries[x]) > 0; x++) {
        util_pb = slapi_pblock_new();
        PR_snprintf(entry_string, 512, entries[x], string1, string2, string3);
        e = slapi_str2entry(entry_string, 0);
        PL_strncpyz(ebuf, slapi_entry_get_dn_const(e), sizeof(ebuf)); /* for logging */
        slapi_add_entry_internal_set_pb(util_pb, e, NULL, li->li_identity, 0);
        slapi_pblock_set(util_pb, SLAPI_DSE_DONT_WRITE_WHEN_ADDING,
                         &dont_write_file);
        rc = slapi_add_internal_pb(util_pb);
        slapi_pblock_get(util_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);
        if (!rc && (result == LDAP_SUCCESS)) {
            slapi_log_err(SLAPI_LOG_CONFIG, "bdb_config_add_dse_entries", "Added database config entry [%s]\n", ebuf);
        } else if (result == LDAP_ALREADY_EXISTS) {
            slapi_log_err(SLAPI_LOG_TRACE, "bdb_config_add_dse_entries", "Database config entry [%s] already exists - skipping\n", ebuf);
        } else {
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_add_dse_entries",
                          "Unable to add config entry [%s] to the DSE: %d %d\n",
                          ebuf, result, rc);
        }
        slapi_pblock_destroy(util_pb);
    }

    return 0;
}

/* used to add a single entry, special case of above */
int
bdb_config_add_dse_entry(struct ldbminfo *li, char *entry, int flags)
{
    char *entries[] = {"%s", ""};

    return bdb_config_add_dse_entries(li, entries, entry, NULL, NULL, flags);
}


/*------------------------------------------------------------------------
 * Get and set functions for bdb variables
 *----------------------------------------------------------------------*/

static void *
bdb_config_db_lock_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)li->li_new_dblock);
}


static int
bdb_config_db_lock_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    uint64_t val = (uint64_t)((uintptr_t)value);

    if (val < BDB_LOCK_NB_MIN) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: Invalid value for %s (%d). Must be greater than %d\n",
                              CONFIG_DB_LOCK, val, BDB_LOCK_NB_MIN);
        slapi_log_err(SLAPI_LOG_ERR, "bdb_config_db_lock_set", "Invalid value for %s (%" PRIu64 ")\n",
                      CONFIG_DB_LOCK, val);
        return LDAP_UNWILLING_TO_PERFORM;
    }
    if (apply) {
        if (CONFIG_PHASE_RUNNING == phase) {
            li->li_new_dblock = val;
            slapi_log_err(SLAPI_LOG_NOTICE, "bdb_config_db_lock_set",
                          "New db max lock count will not take affect until the server is restarted\n");
        } else {
            li->li_new_dblock = val;
            li->li_dblock = val;
        }
    }

    return retval;
}

static void *
bdb_config_db_lock_monitoring_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((intptr_t)(li->li_new_dblock_monitoring));
}

static int
bdb_config_db_lock_monitoring_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int32_t)((intptr_t)value);

    if (apply) {
        if (CONFIG_PHASE_RUNNING == phase) {
            li->li_new_dblock_monitoring = val;
            slapi_log_err(SLAPI_LOG_NOTICE, "bdb_config_db_lock_monitoring_set",
                          "New nsslapd-db-lock-monitoring value will not take affect until the server is restarted\n");
        } else {
            li->li_new_dblock_monitoring = val;
            li->li_dblock_monitoring = val;
        }
    }

    return retval;
}

static void *
bdb_config_db_lock_pause_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(slapi_atomic_load_32((int32_t *)&(li->li_dblock_monitoring_pause), __ATOMIC_RELAXED)));
}

static int
bdb_config_db_lock_pause_set(void *arg, void *value, char *errorbuf, int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    u_int32_t val = (u_int32_t)((uintptr_t)value);

    if (val == 0) {
        slapi_log_err(SLAPI_LOG_NOTICE, "bdb_config_db_lock_pause_set",
                      "%s was set to '0'. The default value will be used (%s)",
                      CONFIG_DB_LOCKS_PAUSE, DEFAULT_DBLOCK_PAUSE_STR);
        val = DEFAULT_DBLOCK_PAUSE;
    }

    if (apply) {
        slapi_atomic_store_32((int32_t *)&(li->li_dblock_monitoring_pause), val, __ATOMIC_RELAXED);
    }
    return retval;
}

static void *
bdb_config_db_lock_threshold_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(li->li_new_dblock_threshold));
}

static int
bdb_config_db_lock_threshold_set(void *arg, void *value, char *errorbuf, int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    u_int32_t val = (u_int32_t)((uintptr_t)value);

    if (val < 70 || val > 95) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: \"%d\" is invalid, threshold is indicated as a percentage and it must lie in range of 70 and 95",
                              CONFIG_DB_LOCKS_THRESHOLD, val);
        slapi_log_err(SLAPI_LOG_ERR, "bdb_config_db_lock_threshold_set",
                      "%s: \"%d\" is invalid, threshold is indicated as a percentage and it must lie in range of 70 and 95",
                      CONFIG_DB_LOCKS_THRESHOLD, val);
        retval = LDAP_OPERATIONS_ERROR;
        return retval;
    }

    if (apply) {
        if (CONFIG_PHASE_RUNNING == phase) {
            li->li_new_dblock_threshold = val;
            slapi_log_err(SLAPI_LOG_NOTICE, "bdb_config_db_lock_threshold_set",
                          "New nsslapd-db-lock-monitoring-threshold value will not take affect until the server is restarted\n");
        } else {
            li->li_new_dblock_threshold = val;
            li->li_dblock_threshold = val;
        }
    }
    return retval;
}

static void *
bdb_config_dbcachesize_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)li->li_new_dbcachesize);
}

static int
bdb_config_dbcachesize_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    uint64_t val = (size_t)value;
    uint64_t delta = (size_t)value;

    /* There is an error here. We check the new val against our current mem-alloc
     * Issue is that we already are using system pages, so while our value *might*
     * be valid, we may reject it here due to the current procs page usage.
     *
     * So how do we solve this? If we are setting a SMALLER value than we
     * currently have ALLOW it, because we already passed the cache sanity.
     * If we are setting a LARGER value, we check the delta of the two, and make
     * sure that it is sane.
     */

/* Stop the user configuring a stupidly small cache */
/* min: 8KB (page size) * def thrd cnts (threadnumber==20). */
#define DBDEFMINSIZ 500000
    /* We allow a value of 0, because the autotuning in start.c will
     * register that, and trigger the recalculation of the dbcachesize as
     * needed on the next start up.
     */
    if (val < DBDEFMINSIZ && val > 0) {
        slapi_log_err(SLAPI_LOG_NOTICE, "bdb_config_dbcachesize_set", "cache too small, increasing to %dK bytes\n",
                      DBDEFMINSIZ / 1000);
        val = DBDEFMINSIZ;
    } else if (val > li->li_dbcachesize) {
        delta = val - li->li_dbcachesize;

        util_cachesize_result sane;
        slapi_pal_meminfo *mi = spal_meminfo_get();
        sane = util_is_cachesize_sane(mi, &delta);
        spal_meminfo_destroy(mi);

        if (sane != UTIL_CACHESIZE_VALID) {
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: nsslapd-dbcachesize value is too large.");
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_dbcachesize_set",
                          "nsslapd-dbcachesize value is too large.\n");
            return LDAP_UNWILLING_TO_PERFORM;
        }
    }

    if (CONFIG_PHASE_RUNNING == phase) {
        if (val > 0 && li->li_cache_autosize) {
            /* We are auto-tuning the cache, so this change would be overwritten - return an error */
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                                  "Error: \"nsslapd-dbcachesize\" can not be updated while \"nsslapd-cache-autosize\" is set "
                                  "in \"cn=config,cn=ldbm database,cn=plugins,cn=config\".");
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_dbcachesize_set",
                          "\"nsslapd-dbcachesize\" can not be set while \"nsslapd-cache-autosize\" is set "
                          "in \"cn=config,cn=ldbm database,cn=plugins,cn=config\".\n");
            return LDAP_UNWILLING_TO_PERFORM;
        }
    }

    if (apply) {
        if (CONFIG_PHASE_RUNNING == phase) {
            li->li_new_dbcachesize = val;
            if (val == 0) {
                slapi_log_err(SLAPI_LOG_NOTICE, "bdb_config_dbcachesize_set", "cache size reset to 0, will be autosized on next startup.\n");
            } else {
                slapi_log_err(SLAPI_LOG_NOTICE, "bdb_config_dbcachesize_set", "New db cache size will not take affect until the server is restarted\n");
            }
        } else {
            li->li_new_dbcachesize = val;
            li->li_dbcachesize = val;
        }
    }

    return retval;
}

static void *
bdb_config_maxpassbeforemerge_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(li->li_maxpassbeforemerge));
}

static int
bdb_config_maxpassbeforemerge_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (val < 0) {
        slapi_log_err(SLAPI_LOG_NOTICE, "bdb_config_maxpassbeforemerge_set",
                      "maxpassbeforemerge will not take negative value - setting to 100\n");
        val = 100;
    }

    if (apply) {
        li->li_maxpassbeforemerge = val;
    }

    return retval;
}


static void *
bdb_config_dbncache_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(li->li_new_dbncache));
}

static int
bdb_config_dbncache_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase, int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    size_t val = (size_t)((uintptr_t)value);

    if (apply) {

        if (CONFIG_PHASE_RUNNING == phase) {
            li->li_new_dbncache = val;
            slapi_log_err(SLAPI_LOG_NOTICE, "bdb_config_dbncache_set",
                          "New nsslapd-dbncache will not take affect until the server is restarted\n");
        } else {
            li->li_new_dbncache = val;
            li->li_dbncache = val;
        }
    }

    return retval;
}

void *
bdb_config_db_logdirectory_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    /* Remember get functions of type string need to return
     * alloced memory. */
    /* if bdb_log_directory is set to a string different from ""
     * then it has been set, return this variable
     * otherwise it is set to default, use the instance home directory
     */
    if (strlen(BDB_CONFIG(li)->bdb_log_directory) > 0)
        return (void *)slapi_ch_strdup(BDB_CONFIG(li)->bdb_log_directory);
    else
        return (void *)slapi_ch_strdup(li->li_new_directory);
}

/* Does not return a copy of the string - used by disk space monitoring feature */
void *
bdb_config_db_logdirectory_get_ext(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    if (strlen(BDB_CONFIG(li)->bdb_log_directory) > 0)
        return (void *)BDB_CONFIG(li)->bdb_log_directory;
    else
        return (void *)li->li_new_directory;
}

static int
bdb_config_db_logdirectory_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    char *val = (char *)value;

    if (apply) {
        slapi_ch_free((void **)&(BDB_CONFIG(li)->bdb_log_directory));
        BDB_CONFIG(li)->bdb_log_directory = slapi_ch_strdup(val);
    }

    return retval;
}

static void *
bdb_config_db_durable_transactions_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_durable_transactions));
}

static int
bdb_config_db_durable_transactions_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_durable_transactions = val;
    }

    return retval;
}

static void *
bdb_config_db_lockdown_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_lockdown));
}

static int
bdb_config_db_lockdown_set(
    void *arg,
    void *value,
    char *errorbuf __attribute__((unused)),
    int phase __attribute__((unused)),
    int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_lockdown = val;
    }

    return retval;
}

static void *
bdb_config_db_circular_logging_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_circular_logging));
}

static int
bdb_config_db_circular_logging_set(void *arg,
                                    void *value,
                                    char *errorbuf __attribute__((unused)),
                                    int phase __attribute__((unused)),
                                    int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_circular_logging = val;
    }

    return retval;
}

static void *
bdb_config_db_transaction_logging_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)BDB_CONFIG(li)->bdb_enable_transactions);
}

static int
bdb_config_db_transaction_logging_set(void *arg,
                                       void *value,
                                       char *errorbuf __attribute__((unused)),
                                       int phase __attribute__((unused)),
                                       int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_enable_transactions = val;
    }

    return retval;
}


static void *
bdb_config_db_transaction_wait_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_txn_wait));
}

static int
bdb_config_db_transaction_wait_set(void *arg,
                                    void *value,
                                    char *errorbuf __attribute__((unused)),
                                    int phase __attribute__((unused)),
                                    int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_txn_wait = val;
    }

    return retval;
}

static void *
bdb_config_db_logbuf_size_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_logbuf_size));
}

static int
bdb_config_db_logbuf_size_set(void *arg,
                               void *value,
                               char *errorbuf __attribute__((unused)),
                               int phase __attribute__((unused)),
                               int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    uint64_t val = (uint64_t)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_logbuf_size = val;
    }

    return retval;
}

static void *
bdb_config_db_checkpoint_interval_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_checkpoint_interval));
}

static int
bdb_config_db_checkpoint_interval_set(void *arg,
                                       void *value,
                                       char *errorbuf __attribute__((unused)),
                                       int phase __attribute__((unused)),
                                       int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_checkpoint_interval = val;
    }

    return retval;
}

static void *
bdb_config_db_compactdb_interval_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_compactdb_interval));
}

static int
bdb_config_db_compactdb_interval_set(void *arg,
                                      void *value,
                                      char *errorbuf __attribute__((unused)),
                                      int phase __attribute__((unused)),
                                      int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_compactdb_interval = val;
    }

    return retval;
}

static void *
bdb_config_db_compactdb_time_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    return (void *)slapi_ch_strdup(BDB_CONFIG(li)->bdb_compactdb_time);
}

static int
bdb_config_db_compactdb_time_set(void *arg,
                                 void *value,
                                 char *errorbuf __attribute__((unused)),
                                 int phase __attribute__((unused)),
                                 int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    char *val = slapi_ch_strdup((char *)value);
    char *endp = NULL;
    char *hour_str = NULL;
    char *min_str = NULL;
    char *default_time = "23:59";
    int32_t hour, min;
    int retval = LDAP_SUCCESS;
    errno = 0;

    if (strstr(val, ":")) {
        /* Get the hour and minute */
        hour_str = ldap_utf8strtok_r(val, ":", &min_str);

        /* Validate hour */
        hour = strtoll(hour_str, &endp, 10);
        if (*endp != '\0' || errno == ERANGE || hour < 0 || hour > 23 || strlen(hour_str) != 2) {
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                    "Invalid hour set (%s), must be a two digit number between 00 and 23",
                    hour_str);
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_db_compactdb_interval_set",
                    "Invalid minute set (%s), must be a two digit number between 00 and 59.  "
                    "Using default of 23:59\n", hour_str);
            retval = LDAP_OPERATIONS_ERROR;
            goto done;
        }

        /* Validate minute */
        min = strtoll(min_str, &endp, 10);
        if (*endp != '\0' || errno == ERANGE || min < 0 || min > 59 || strlen(min_str) != 2) {
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                    "Invalid minute set (%s), must be a two digit number between 00 and 59",
                    hour_str);
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_db_compactdb_interval_set",
                    "Invalid minute set (%s), must be a two digit number between 00 and 59.  "
                    "Using default of 23:59\n", min_str);
            retval = LDAP_OPERATIONS_ERROR;
            goto done;
        }
    } else {
        /* Wrong format */
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                "Invalid setting (%s), must have a time format of HH:MM", val);
        slapi_log_err(SLAPI_LOG_ERR, "bdb_config_db_compactdb_interval_set",
                "Invalid setting (%s), must have a time format of HH:MM\n", val);
        retval = LDAP_OPERATIONS_ERROR;
        goto done;
    }

done:
    if (apply) {
        slapi_ch_free((void **)&(BDB_CONFIG(li)->bdb_compactdb_time));
        if (retval) {
            /* Something went wrong, use the default */
            BDB_CONFIG(li)->bdb_compactdb_time = slapi_ch_strdup(default_time);
        } else {
            BDB_CONFIG(li)->bdb_compactdb_time = slapi_ch_strdup((char *)value);
        }
    }
    slapi_ch_free_string(&val);

    return retval;
}

static void *
bdb_config_db_page_size_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_page_size));
}

static int
bdb_config_db_page_size_set(void *arg,
                             void *value,
                             char *errorbuf __attribute__((unused)),
                             int phase __attribute__((unused)),
                             int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    uint32_t val = (uint32_t)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_page_size = val;
    }

    return retval;
}

static void *
bdb_config_db_index_page_size_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_index_page_size));
}

static int
bdb_config_db_index_page_size_set(void *arg,
                                   void *value,
                                   char *errorbuf __attribute__((unused)),
                                   int phase __attribute__((unused)),
                                   int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    uint32_t val = (uint32_t)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_index_page_size = val;
    }

    return retval;
}

static void *
bdb_config_db_old_idl_maxids_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)li->li_old_idl_maxids);
}

static int
bdb_config_db_old_idl_maxids_set(void *arg,
                                  void *value,
                                  char *errorbuf,
                                  int phase __attribute__((unused)),
                                  int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        if (val >= 0) {
            li->li_old_idl_maxids = val;
        } else {
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                                  "Error: Invalid value for %s (%d). Value must be equal or greater than zero.",
                                  CONFIG_DB_OLD_IDL_MAXIDS, val);
            return LDAP_UNWILLING_TO_PERFORM;
        }
    }

    return retval;
}

static void *
bdb_config_db_logfile_size_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_logfile_size));
}

static int
bdb_config_db_logfile_size_set(void *arg,
                                void *value,
                                char *errorbuf __attribute__((unused)),
                                int phase __attribute__((unused)),
                                int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    uint64_t val = (uint64_t)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_logfile_size = val;
    }

    return retval;
}

static void *
bdb_config_db_spin_count_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_spin_count));
}

static int
bdb_config_db_spin_count_set(void *arg,
                              void *value,
                              char *errorbuf __attribute__((unused)),
                              int phase __attribute__((unused)),
                              int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_spin_count = val;
    }

    return retval;
}

static void *
bdb_config_db_trickle_percentage_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_trickle_percentage));
}

static int
bdb_config_db_trickle_percentage_set(void *arg,
                                      void *value,
                                      char *errorbuf,
                                      int phase __attribute__((unused)),
                                      int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (val < 0 || val > 100) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: Invalid value for %s (%d). Must be between 0 and 100\n",
                              CONFIG_DB_TRICKLE_PERCENTAGE, val);
        slapi_log_err(SLAPI_LOG_ERR, "bdb_config_db_trickle_percentage_set",
                      "Invalid value for %s (%d). Must be between 0 and 100\n",
                      CONFIG_DB_TRICKLE_PERCENTAGE, val);
        return LDAP_UNWILLING_TO_PERFORM;
    }

    if (apply) {
        BDB_CONFIG(li)->bdb_trickle_percentage = val;
    }

    return retval;
}

static void *
bdb_config_db_debug_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_debug));
}

static int
bdb_config_db_debug_set(void *arg,
                         void *value,
                         char *errorbuf __attribute__((unused)),
                         int phase __attribute__((unused)),
                         int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_debug = val;
    }

    return retval;
}

static void *
bdb_config_db_verbose_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_debug_verbose));
}

static int
bdb_config_db_verbose_set(void *arg,
                           void *value,
                           char *errorbuf __attribute__((unused)),
                           int phase __attribute__((unused)),
                           int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_debug_verbose = val;
    }

    return retval;
}
static void *
bdb_config_db_named_regions_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_named_regions));
}

static int
bdb_config_db_named_regions_set(void *arg,
                                 void *value,
                                 char *errorbuf __attribute__((unused)),
                                 int phase __attribute__((unused)),
                                 int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_named_regions = val;
    }

    return retval;
}

static void *
bdb_config_db_private_mem_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_private_mem));
}

static int
bdb_config_db_private_mem_set(void *arg,
                               void *value,
                               char *errorbuf __attribute__((unused)),
                               int phase __attribute__((unused)),
                               int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_private_mem = val;
    }

    return retval;
}

static void *
bdb_config_db_online_import_encrypt_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)li->li_online_import_encrypt);
}

static int
bdb_config_db_online_import_encrypt_set(void *arg,
                                         void *value,
                                         char *errorbuf __attribute__((unused)),
                                         int phase __attribute__((unused)),
                                         int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        li->li_online_import_encrypt = val;
    }

    return retval;
}

static void *
bdb_config_db_private_import_mem_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_private_import_mem));
}

static int
bdb_config_db_private_import_mem_set(void *arg,
                                      void *value,
                                      char *errorbuf __attribute__((unused)),
                                      int phase __attribute__((unused)),
                                      int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_private_import_mem = val;
    }

    return retval;
}

static void *
bdb_config_db_shm_key_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)BDB_CONFIG(li)->bdb_shm_key;
}

static int
bdb_config_db_shm_key_set(
    void *arg,
    void *value,
    char *errorbuf __attribute__((unused)),
    int phase __attribute__((unused)),
    int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_shm_key = val;
    }

    return retval;
}

static void *
bdb_config_db_cache_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_cache_config));
}

static int
bdb_config_db_cache_set(void *arg,
                         void *value,
                         char *errorbuf,
                         int phase __attribute__((unused)),
                         int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = ((uintptr_t)value);
    uint64_t delta = 0;

    /* There is an error here. We check the new val against our current mem-alloc
     * Issue is that we already are using system pages, so while our value *might*
     * be valid, we may reject it here due to the current procs page usage.
     *
     * So how do we solve this? If we are setting a SMALLER value than we
     * currently have ALLOW it, because we already passed the cache sanity.
     * If we are setting a LARGER value, we check the delta of the two, and make
     * sure that it is sane.
     */

    if (val > BDB_CONFIG(li)->bdb_cache_config) {
        delta = val - BDB_CONFIG(li)->bdb_cache_config;
        util_cachesize_result sane;

        slapi_pal_meminfo *mi = spal_meminfo_get();
        sane = util_is_cachesize_sane(mi, &delta);
        spal_meminfo_destroy(mi);

        if (sane != UTIL_CACHESIZE_VALID) {
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: db cachesize value is too large");
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_db_cache_set", "db cachesize value is too large.\n");
            return LDAP_UNWILLING_TO_PERFORM;
        }
    }
    if (apply) {
        BDB_CONFIG(li)->bdb_cache_config = val;
    }

    return retval;
}

static void *
bdb_config_db_debug_checkpointing_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_debug_checkpointing));
}

static int
bdb_config_db_debug_checkpointing_set(void *arg,
                                       void *value,
                                       char *errorbuf __attribute__((unused)),
                                       int phase __attribute__((unused)),
                                       int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_debug_checkpointing = val;
    }

    return retval;
}

static void *
bdb_config_db_home_directory_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    /* Remember get functions of type string need to return
     * alloced memory. */
    return (void *)slapi_ch_strdup(BDB_CONFIG(li)->bdb_dbhome_directory);
}

static int
bdb_config_db_home_directory_set(void *arg,
                                  void *value,
                                  char *errorbuf __attribute__((unused)),
                                  int phase __attribute__((unused)),
                                  int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    char *val = (char *)value;

    if (apply) {
        slapi_ch_free((void **)&(BDB_CONFIG(li)->bdb_dbhome_directory));
        BDB_CONFIG(li)->bdb_dbhome_directory = slapi_ch_strdup(val);
    }

    return retval;
}

static void *
bdb_config_import_cache_autosize_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(li->li_import_cache_autosize));
}

static int
bdb_config_import_cache_autosize_set(void *arg,
                                      void *value,
                                      char *errorbuf __attribute__((unused)),
                                      int phase __attribute__((unused)),
                                      int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    if (apply)
        li->li_import_cache_autosize = (int)((uintptr_t)value);
    return LDAP_SUCCESS;
}

static void *
bdb_config_cache_autosize_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(li->li_cache_autosize));
}

static int
bdb_config_cache_autosize_set(void *arg,
                               void *value,
                               char *errorbuf,
                               int phase __attribute__((unused)),
                               int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    int val = (int)((uintptr_t)value);
    if (val < 0 || val > 100) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "Error: Invalid value for %s (%d). The value must be between \"0\" and \"100\"\n",
                              CONFIG_CACHE_AUTOSIZE, val);
        slapi_log_err(SLAPI_LOG_ERR, "bdb_config_cache_autosize_set",
                      "Invalid value for %s (%d). The value must be between \"0\" and \"100\"\n",
                      CONFIG_CACHE_AUTOSIZE, val);
        return LDAP_UNWILLING_TO_PERFORM;
    }
    if (apply) {
        li->li_cache_autosize = val;
    }
    return LDAP_SUCCESS;
}

static void *
bdb_config_cache_autosize_split_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(li->li_cache_autosize_split));
}

static int
bdb_config_cache_autosize_split_set(void *arg,
                                     void *value,
                                     char *errorbuf,
                                     int phase __attribute__((unused)),
                                     int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    int val = (int)((uintptr_t)value);
    if (val < 0 || val > 100) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "Error: Invalid value for %s (%d). The value must be between \"0\" and \"100\"\n",
                              CONFIG_CACHE_AUTOSIZE_SPLIT, val);
        slapi_log_err(SLAPI_LOG_ERR, "bdb_config_cache_autosize_split_set",
                      "Invalid value for %s (%d). The value must be between \"0\" and \"100\"\n",
                      CONFIG_CACHE_AUTOSIZE_SPLIT, val);
        return LDAP_UNWILLING_TO_PERFORM;
    }
    if (apply) {
        li->li_cache_autosize_split = val;
    }
    return LDAP_SUCCESS;
}

static void *
bdb_config_import_cachesize_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)li->li_import_cachesize);
}

static int
bdb_config_import_cachesize_set(void *arg,
                                 void *value,
                                 char *errorbuf,
                                 int phase __attribute__((unused)),
                                 int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    uint64_t val = (uint64_t)((uintptr_t)value);
    uint64_t delta;
    /* There is an error here. We check the new val against our current mem-alloc
     * Issue is that we already are using system pages, so while our value *might*
     * be valid, we may reject it here due to the current procs page usage.
     *
     * So how do we solve this? If we are setting a SMALLER value than we
     * currently have ALLOW it, because we already passed the cache sanity.
     * If we are setting a LARGER value, we check the delta of the two, and make
     * sure that it is sane.
     */
    if (apply) {
        if (val > li->li_import_cachesize) {
            delta = val - li->li_import_cachesize;

            util_cachesize_result sane;
            slapi_pal_meminfo *mi = spal_meminfo_get();
            sane = util_is_cachesize_sane(mi, &delta);
            spal_meminfo_destroy(mi);

            if (sane != UTIL_CACHESIZE_VALID) {
                slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: import cachesize value is too large.");
                slapi_log_err(SLAPI_LOG_ERR, "bdb_config_import_cachesize_set",
                              "Import cachesize value is too large.\n");
                return LDAP_UNWILLING_TO_PERFORM;
            }
        }
        li->li_import_cachesize = val;
    }
    return LDAP_SUCCESS;
}

static void *
bdb_config_index_buffer_size_get(void *arg __attribute__((unused)))
{
    return (void *)bdb_import_get_index_buffer_size();
}

static int
bdb_config_index_buffer_size_set(void *arg __attribute__((unused)),
                                  void *value,
                                  char *errorbuf __attribute__((unused)),
                                  int phase __attribute__((unused)),
                                  int apply)
{
    if (apply) {
        bdb_import_configure_index_buffer_size((size_t)value);
    }
    return LDAP_SUCCESS;
}

static void *
bdb_config_serial_lock_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)li->li_fat_lock);
}

static int
bdb_config_serial_lock_set(void *arg,
                            void *value,
                            char *errorbuf __attribute__((unused)),
                            int phase __attribute__((unused)),
                            int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    if (apply) {
        li->li_fat_lock = (int)((uintptr_t)value);
    }

    return LDAP_SUCCESS;
}

static void *
bdb_config_legacy_errcode_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)li->li_legacy_errcode);
}

static int
bdb_config_legacy_errcode_set(void *arg,
                               void *value,
                               char *errorbuf __attribute__((unused)),
                               int phase __attribute__((unused)),
                               int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    if (apply) {
        li->li_legacy_errcode = (int)((uintptr_t)value);
    }

    return LDAP_SUCCESS;
}

static int
bdb_config_set_bypass_filter_test(void *arg,
                                   void *value,
                                   char *errorbuf __attribute__((unused)),
                                   int phase __attribute__((unused)),
                                   int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    if (apply) {
        char *myvalue = (char *)value;

        if (0 == strcasecmp(myvalue, "on")) {
            li->li_filter_bypass = 1;
            li->li_filter_bypass_check = 0;
        } else if (0 == strcasecmp(myvalue, "verify")) {
            li->li_filter_bypass = 1;
            li->li_filter_bypass_check = 1;
        } else {
            li->li_filter_bypass = 0;
            li->li_filter_bypass_check = 0;
        }
    }
    return LDAP_SUCCESS;
}

static void *
bdb_config_get_bypass_filter_test(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    char *retstr = NULL;

    if (li->li_filter_bypass) {
        if (li->li_filter_bypass_check) {
            /* meaningful only if is bypass filter test called */
            retstr = slapi_ch_strdup("verify");
        } else {
            retstr = slapi_ch_strdup("on");
        }
    } else {
        retstr = slapi_ch_strdup("off");
    }
    return (void *)retstr;
}

static void *
bdb_config_db_tx_max_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_tx_max));
}

static int
bdb_config_db_tx_max_set(
    void *arg,
    void *value,
    char *errorbuf __attribute__((unused)),
    int phase __attribute__((unused)),
    int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        BDB_CONFIG(li)->bdb_tx_max = val;
    }

    return retval;
}

static void *
bdb_config_db_deadlock_policy_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(BDB_CONFIG(li)->bdb_deadlock_policy));
}

static int
bdb_config_db_deadlock_policy_set(void *arg,
                                   void *value,
                                   char *errorbuf,
                                   int phase __attribute__((unused)),
                                   int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    u_int32_t val = (u_int32_t)((uintptr_t)value);

    if (val > DB_LOCK_YOUNGEST) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "Error: Invalid value for %s (%d). Must be between %d and %d inclusive\n",
                              CONFIG_DB_DEADLOCK_POLICY, val, DB_LOCK_DEFAULT, DB_LOCK_YOUNGEST);
        slapi_log_err(SLAPI_LOG_ERR, "bdb_config_db_deadlock_policy_set",
                      "Invalid value for deadlock policy (%d). Must be between %d and %d inclusive\n",
                      val, DB_LOCK_DEFAULT, DB_LOCK_YOUNGEST);
        return LDAP_UNWILLING_TO_PERFORM;
    }
    if (val == DB_LOCK_NORUN) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "Warning: Setting value for %s to (%d) will disable deadlock detection\n",
                              CONFIG_DB_DEADLOCK_POLICY, val);
        slapi_log_err(SLAPI_LOG_WARNING, "bdb_config_db_deadlock_policy_set",
                      "Setting value for %s to (%d) will disable deadlock detection\n",
                      CONFIG_DB_DEADLOCK_POLICY, val);
    }

    if (apply) {
        BDB_CONFIG(li)->bdb_deadlock_policy = val;
    }

    return retval;
}


/*------------------------------------------------------------------------
 * Configuration array for bdb variables
 *----------------------------------------------------------------------*/
static config_info bdb_config_param[] = {
    {CONFIG_DB_LOCK, CONFIG_TYPE_INT, "10000", &bdb_config_db_lock_get, &bdb_config_db_lock_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_DBCACHESIZE, CONFIG_TYPE_UINT64, DEFAULT_CACHE_SIZE_STR, &bdb_config_dbcachesize_get, &bdb_config_dbcachesize_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_DBNCACHE, CONFIG_TYPE_INT, "0", &bdb_config_dbncache_get, &bdb_config_dbncache_set, CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_MAXPASSBEFOREMERGE, CONFIG_TYPE_INT, "100", &bdb_config_maxpassbeforemerge_get, &bdb_config_maxpassbeforemerge_set, 0},
    {CONFIG_DB_LOGDIRECTORY, CONFIG_TYPE_STRING, "", &bdb_config_db_logdirectory_get, &bdb_config_db_logdirectory_set, CONFIG_FLAG_ALWAYS_SHOW},
    {CONFIG_DB_DURABLE_TRANSACTIONS, CONFIG_TYPE_ONOFF, "on", &bdb_config_db_durable_transactions_get, &bdb_config_db_durable_transactions_set, CONFIG_FLAG_ALWAYS_SHOW},
    {CONFIG_DB_CIRCULAR_LOGGING, CONFIG_TYPE_ONOFF, "on", &bdb_config_db_circular_logging_get, &bdb_config_db_circular_logging_set, 0},
    {CONFIG_DB_TRANSACTION_LOGGING, CONFIG_TYPE_ONOFF, "on", &bdb_config_db_transaction_logging_get, &bdb_config_db_transaction_logging_set, 0},
    {CONFIG_DB_TRANSACTION_WAIT, CONFIG_TYPE_ONOFF, "off", &bdb_config_db_transaction_wait_get, &bdb_config_db_transaction_wait_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_DB_CHECKPOINT_INTERVAL, CONFIG_TYPE_INT, "60", &bdb_config_db_checkpoint_interval_get, &bdb_config_db_checkpoint_interval_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_DB_COMPACTDB_INTERVAL, CONFIG_TYPE_INT, "2592000" /*30days*/, &bdb_config_db_compactdb_interval_get, &bdb_config_db_compactdb_interval_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_DB_COMPACTDB_TIME, CONFIG_TYPE_STRING, "23:59", &bdb_config_db_compactdb_time_get, &bdb_config_db_compactdb_time_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_DB_TRANSACTION_BATCH, CONFIG_TYPE_INT, "0", &bdb_get_batch_transactions, &bdb_set_batch_transactions, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_DB_TRANSACTION_BATCH_MIN_SLEEP, CONFIG_TYPE_INT, "50", &bdb_get_batch_txn_min_sleep, &bdb_set_batch_txn_min_sleep, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_DB_TRANSACTION_BATCH_MAX_SLEEP, CONFIG_TYPE_INT, "50", &bdb_get_batch_txn_max_sleep, &bdb_set_batch_txn_max_sleep, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_DB_LOGBUF_SIZE, CONFIG_TYPE_SIZE_T, "0", &bdb_config_db_logbuf_size_get, &bdb_config_db_logbuf_size_set, CONFIG_FLAG_ALWAYS_SHOW},
    {CONFIG_DB_PAGE_SIZE, CONFIG_TYPE_SIZE_T, "0", &bdb_config_db_page_size_get, &bdb_config_db_page_size_set, 0},
    {CONFIG_DB_INDEX_PAGE_SIZE, CONFIG_TYPE_SIZE_T, "0", &bdb_config_db_index_page_size_get, &bdb_config_db_index_page_size_set, 0},
    {CONFIG_DB_OLD_IDL_MAXIDS, CONFIG_TYPE_INT, "0", &bdb_config_db_old_idl_maxids_get, &bdb_config_db_old_idl_maxids_set, 0},
    {CONFIG_DB_LOGFILE_SIZE, CONFIG_TYPE_UINT64, "0", &bdb_config_db_logfile_size_get, &bdb_config_db_logfile_size_set, 0},
    {CONFIG_DB_TRICKLE_PERCENTAGE, CONFIG_TYPE_INT, "5", &bdb_config_db_trickle_percentage_get, &bdb_config_db_trickle_percentage_set, 0},
    {CONFIG_DB_SPIN_COUNT, CONFIG_TYPE_INT, "0", &bdb_config_db_spin_count_get, &bdb_config_db_spin_count_set, 0},
    {CONFIG_DB_DEBUG, CONFIG_TYPE_ONOFF, "on", &bdb_config_db_debug_get, &bdb_config_db_debug_set, 0},
    {CONFIG_DB_VERBOSE, CONFIG_TYPE_ONOFF, "off", &bdb_config_db_verbose_get, &bdb_config_db_verbose_set, 0},
    {CONFIG_DB_NAMED_REGIONS, CONFIG_TYPE_ONOFF, "off", &bdb_config_db_named_regions_get, &bdb_config_db_named_regions_set, 0},
    {CONFIG_DB_LOCK, CONFIG_TYPE_INT, "10000", &bdb_config_db_lock_get, &bdb_config_db_lock_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_DB_PRIVATE_MEM, CONFIG_TYPE_ONOFF, "off", &bdb_config_db_private_mem_get, &bdb_config_db_private_mem_set, 0},
    {CONFIG_DB_PRIVATE_IMPORT_MEM, CONFIG_TYPE_ONOFF, "on", &bdb_config_db_private_import_mem_get, &bdb_config_db_private_import_mem_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONDIF_DB_ONLINE_IMPORT_ENCRYPT, CONFIG_TYPE_ONOFF, "on", &bdb_config_db_online_import_encrypt_get, &bdb_config_db_online_import_encrypt_set, 0},
    {CONFIG_DB_SHM_KEY, CONFIG_TYPE_LONG, "389389", &bdb_config_db_shm_key_get, &bdb_config_db_shm_key_set, 0},
    {CONFIG_DB_CACHE, CONFIG_TYPE_INT, "0", &bdb_config_db_cache_get, &bdb_config_db_cache_set, 0},
    {CONFIG_DB_DEBUG_CHECKPOINTING, CONFIG_TYPE_ONOFF, "off", &bdb_config_db_debug_checkpointing_get, &bdb_config_db_debug_checkpointing_set, 0},
    {CONFIG_DB_HOME_DIRECTORY, CONFIG_TYPE_STRING, "", &bdb_config_db_home_directory_get, &bdb_config_db_home_directory_set, 0},
    {CONFIG_IMPORT_CACHE_AUTOSIZE, CONFIG_TYPE_INT, "-1", &bdb_config_import_cache_autosize_get, &bdb_config_import_cache_autosize_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_CACHE_AUTOSIZE, CONFIG_TYPE_INT, "25", &bdb_config_cache_autosize_get, &bdb_config_cache_autosize_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_CACHE_AUTOSIZE_SPLIT, CONFIG_TYPE_INT, "25", &bdb_config_cache_autosize_split_get, &bdb_config_cache_autosize_split_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_IMPORT_CACHESIZE, CONFIG_TYPE_UINT64, "16777216", &bdb_config_import_cachesize_get, &bdb_config_import_cachesize_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_BYPASS_FILTER_TEST, CONFIG_TYPE_STRING, "on", &bdb_config_get_bypass_filter_test, &bdb_config_set_bypass_filter_test, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_DB_LOCKDOWN, CONFIG_TYPE_ONOFF, "off", &bdb_config_db_lockdown_get, &bdb_config_db_lockdown_set, 0},
    {CONFIG_INDEX_BUFFER_SIZE, CONFIG_TYPE_INT, "0", &bdb_config_index_buffer_size_get, &bdb_config_index_buffer_size_set, 0},
    {CONFIG_DB_TX_MAX, CONFIG_TYPE_INT, "200", &bdb_config_db_tx_max_get, &bdb_config_db_tx_max_set, 0},
    {CONFIG_SERIAL_LOCK, CONFIG_TYPE_ONOFF, "on", &bdb_config_serial_lock_get, &bdb_config_serial_lock_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_USE_LEGACY_ERRORCODE, CONFIG_TYPE_ONOFF, "off", &bdb_config_legacy_errcode_get, &bdb_config_legacy_errcode_set, 0},
    {CONFIG_DB_DEADLOCK_POLICY, CONFIG_TYPE_INT, STRINGIFYDEFINE(DB_LOCK_YOUNGEST), &bdb_config_db_deadlock_policy_get, &bdb_config_db_deadlock_policy_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_DB_LOCKS_MONITORING, CONFIG_TYPE_ONOFF, "on", &bdb_config_db_lock_monitoring_get, &bdb_config_db_lock_monitoring_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_DB_LOCKS_THRESHOLD, CONFIG_TYPE_INT, "90", &bdb_config_db_lock_threshold_get, &bdb_config_db_lock_threshold_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_DB_LOCKS_PAUSE, CONFIG_TYPE_INT, DEFAULT_DBLOCK_PAUSE_STR, &bdb_config_db_lock_pause_get, &bdb_config_db_lock_pause_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {NULL, 0, NULL, NULL, NULL, 0}};

void
bdb_config_setup_default(struct ldbminfo *li)
{
    config_info *config;
    char err_buf[SLAPI_DSE_RETURNTEXT_SIZE];

    for (config = bdb_config_param; config->config_name != NULL; config++) {
        bdb_config_set((void *)li, config->config_name, bdb_config_param, NULL /* use default */, err_buf, CONFIG_PHASE_INITIALIZATION, 1 /* apply */, LDAP_MOD_REPLACE);
    }
}

static int
bdb_config_upgrade_dse_info(struct ldbminfo *li)
{
    Slapi_PBlock *search_pb;
    Slapi_PBlock *add_pb;
    Slapi_Entry *bdb_config = NULL;
    Slapi_Entry **entries = NULL;
    char *bdb_config_dn = NULL;
    char *config_dn = NULL;
    int rval = 0;
    Slapi_Mods smods;

    slapi_log_err(SLAPI_LOG_INFO, "bdb_config_upgrade_dse_info", "create config entry from old config\n");

    /* first get the existing ldbm config entry, if it fails
     * nothing can be done
     */

    config_dn = slapi_create_dn_string("cn=config,cn=%s,cn=plugins,cn=config",
                                li->li_plugin->plg_name);

    search_pb = slapi_pblock_new();
    if (!search_pb) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_config_load_dse_info", "Out of memory\n");
        rval = 1;
        goto bail;
    }

    slapi_search_internal_set_pb(search_pb, config_dn, LDAP_SCOPE_BASE,
                                 "objectclass=*", NULL, 0, NULL, NULL, li->li_identity, 0);
    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rval);
    if (rval == LDAP_SUCCESS) {
        /* Need to parse the configuration information for the ldbm
         * plugin that is held in the DSE. */
        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                         &entries);
        if (NULL == entries || entries[0] == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_load_dse_info", "Error accessing the ldbm config DSE 2\n");
            rval = 1;
            goto bail;
        }
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_config_load_dse_info",
                      "Error accessing the ldbm config DSE 1\n");
        rval = 1;
        goto bail;
    }


    /* next create an new specifc bdb config entry,
     * look for attributes in the general config antry which
     * have to go to the bdb entry.
     * - add them to cn=bdb,cn=config,cn=ldbm database
     * - remove them from cn=config,cn=ldbm database
     */
    /* The new and changed config entry need to be kept independent of
     * the slapd exec mode leading here
     */
    dse_unset_dont_ever_write_dse_files();

    bdb_config = slapi_entry_alloc();
    bdb_config_dn = slapi_create_dn_string("cn=bdb,cn=config,cn=%s,cn=plugins,cn=config",
                                li->li_plugin->plg_name);
    slapi_entry_init(bdb_config, bdb_config_dn, NULL);

    slapi_entry_add_string(bdb_config, SLAPI_ATTR_OBJECTCLASS, "extensibleobject");

    slapi_mods_init(&smods, 1);
    bdb_split_bdb_config_entry(li, entries[0], bdb_config, bdb_config_param, &smods);
    add_pb = slapi_pblock_new();
    slapi_pblock_init(add_pb);

    slapi_add_entry_internal_set_pb(add_pb,
                                    bdb_config,
                                    NULL,
                                    li->li_identity,
                                    0);
    slapi_add_internal_pb(add_pb);
    slapi_pblock_get(add_pb, SLAPI_PLUGIN_INTOP_RESULT, &rval);

    if (rval != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_config_upgrade_dse_info", "failed to add bdb config_entry, err= %d\n", rval);
    } else {
        /* the new bdb config entry was successfully added
         * now strip the attrs from the general config entry
         */
        Slapi_PBlock *mod_pb = slapi_pblock_new();
        slapi_modify_internal_set_pb(mod_pb, config_dn,
                                    slapi_mods_get_ldapmods_byref(&smods),
                                    NULL, NULL, li->li_identity, 0);
        slapi_modify_internal_pb(mod_pb);
        slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &rval);
        if (rval != LDAP_SUCCESS) {
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_upgrade_dse_info", "failed to modify  config_entry, err= %d\n", rval);
        }
        slapi_pblock_destroy(mod_pb);
    }
    slapi_pblock_destroy(add_pb);
    slapi_mods_done(&smods);
    slapi_free_search_results_internal(search_pb);

bail:
    slapi_ch_free_string(&config_dn);
    if (search_pb) {
        slapi_pblock_destroy(search_pb);
    }
    return rval;
}

/* Reads in any config information held in the dse for the bdb
 * implementation of the ldbm plugin.
 * Creates dse entries used to configure the ldbm plugin and dblayer
 * if they don't already exist.  Registers dse callback functions to
 * maintain those dse entries.  Returns 0 on success.
 */
int
bdb_config_load_dse_info(struct ldbminfo *li)
{
    Slapi_PBlock *search_pb;
    Slapi_Entry **entries = NULL;
    char *dn = NULL;
    int rval = 0;

    /* We try to read the entry
     * cn=bdb, cn=config, cn=ldbm database, cn=plugins, cn=config.  If the entry is
     * there, then we process the config information it stores.
     */
    dn = slapi_create_dn_string("cn=bdb,cn=config,cn=%s,cn=plugins,cn=config",
                                li->li_plugin->plg_name);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_config_load_dse_info",
                      "failed create config dn for %s\n",
                      li->li_plugin->plg_name);
        rval = 1;
        goto bail;
    }

    search_pb = slapi_pblock_new();
    if (!search_pb) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_config_load_dse_info", "Out of memory\n");
        rval = 1;
        goto bail;
    }

retry:
    slapi_search_internal_set_pb(search_pb, dn, LDAP_SCOPE_BASE,
                                 "objectclass=*", NULL, 0, NULL, NULL, li->li_identity, 0);
    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rval);

    if (rval == LDAP_SUCCESS) {
        /* Need to parse the configuration information for the bdb config
         * entry that is held in the DSE. */
        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                         &entries);
        if (NULL == entries || entries[0] == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_load_dse_info", "Error accessing the bdb config DSE entry\n");
            rval = 1;
            goto bail;
        }
        if (0 != bdb_parse_bdb_config_entry(li, entries[0], bdb_config_param)) {
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_load_dse_info", "Error parsing the bdb config DSE entry\n");
            rval = 1;
            goto bail;
        }
    } else if (rval == LDAP_NO_SUCH_OBJECT) {
    /* The specific bdb entry does not exist,
     * create it from the old config dse entry */
        if (bdb_config_upgrade_dse_info(li)) {
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_load_dse_info",
                          "Error accessing the bdb config DSE entry 1\n");
            rval = 1;
            goto bail;
        } else {
            slapi_free_search_results_internal(search_pb);
            slapi_pblock_init(search_pb);
            goto retry;
        }
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_config_load_dse_info",
                      "Error accessing the bdb config DSE entry 2\n");
        rval = 1;
        goto bail;
    }

    if (search_pb) {
        slapi_free_search_results_internal(search_pb);
        slapi_pblock_destroy(search_pb);
    }

    /* setup the dse callback functions for the ldbm backend config entry */
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", bdb_config_search_entry_callback,
                                   (void *)li);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", bdb_config_modify_entry_callback,
                                   (void *)li);
    slapi_config_register_callback(DSE_OPERATION_WRITE, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", bdb_config_search_entry_callback,
                                   (void *)li);
    slapi_ch_free_string(&dn);

    /* setup the dse callback functions for the ldbm backend monitor entry */
    dn = slapi_create_dn_string("cn=monitor,cn=%s,cn=plugins,cn=config",
                                li->li_plugin->plg_name);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_config_load_dse_info",
                      "failed to create monitor dn for %s\n",
                      li->li_plugin->plg_name);
        rval = 1;
        goto bail;
    }

    /* NOTE (LK): still needs to investigate and clarify the monitoring split between db layers.
     * Now still using ldbm functions
     */
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", bdb_monitor_search,
                                   (void *)li);
    slapi_ch_free_string(&dn);

    /* And the ldbm backend database monitor entry */
    dn = slapi_create_dn_string("cn=database,cn=monitor,cn=%s,cn=plugins,cn=config",
                                li->li_plugin->plg_name);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_config_load_dse_info",
                      "failed create monitor database dn for %s\n",
                      li->li_plugin->plg_name);
        rval = 1;
        goto bail;
    }
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", bdb_dbmonitor_search,
                                   (void *)li);

bail:
    slapi_ch_free_string(&dn);
    return rval;
}

/* general-purpose callback to deny an operation */
static int
bdb_deny_config(Slapi_PBlock *pb __attribute__((unused)),
                Slapi_Entry *e __attribute__((unused)),
                Slapi_Entry *entryAfter __attribute__((unused)),
                int *returncode,
                char *returntext __attribute__((unused)),
                void *arg __attribute__((unused)))
{
    *returncode = LDAP_UNWILLING_TO_PERFORM;
    return SLAPI_DSE_CALLBACK_ERROR;
}

int
bdb_instance_register_monitor(ldbm_instance *inst)
{
    struct ldbminfo *li = inst->inst_li;
    char *dn = NULL;

    dn = slapi_create_dn_string("cn=monitor,cn=%s,cn=%s,cn=plugins,cn=config",
                                inst->inst_name, li->li_plugin->plg_name);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_instance_register_monitor",
                      "failed create monitor instance dn for plugin %s, "
                      "instance %s\n",
                      inst->inst_li->li_plugin->plg_name, inst->inst_name);
        return 1;
    }
    /* make callback on search; deny add/modify/delete */
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", bdb_monitor_instance_search,
                                   (void *)inst);
    slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_SUBTREE, "(objectclass=*)", bdb_deny_config,
                                   (void *)inst);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", bdb_deny_config,
                                   (void *)inst);
    slapi_ch_free_string(&dn);

    return 0;
}

void
bdb_instance_unregister_monitor(ldbm_instance *inst)
{
    struct ldbminfo *li = inst->inst_li;
    char *dn = NULL;

    dn = slapi_create_dn_string("cn=monitor,cn=%s,cn=%s,cn=plugins,cn=config",
                                inst->inst_name, li->li_plugin->plg_name);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_instance_unregister_monitor",
                      "Failed create monitor instance dn for plugin %s, "
                      "instance %s\n",
                      inst->inst_li->li_plugin->plg_name, inst->inst_name);
        return;
    }
    slapi_config_remove_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, dn,
                                 LDAP_SCOPE_BASE, "(objectclass=*)", bdb_monitor_instance_search);
    slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn,
                                 LDAP_SCOPE_SUBTREE, "(objectclass=*)", bdb_deny_config);
    slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
                                 LDAP_SCOPE_BASE, "(objectclass=*)", bdb_deny_config);
    slapi_ch_free_string(&dn);
}

/* Utility function used in creating config entries.  Using the
 * config_info, this function gets info and formats in the correct
 * way.
 * buf is char[BUFSIZ]
 */
void
bdb_config_get(void *arg, config_info *config, char *buf)
{
    void *val = NULL;

    if (config == NULL) {
        buf[0] = '\0';
        return;
    }

    val = config->config_get_fn(arg);
    config_info_print_val(val, config->config_type, buf);

    if (config->config_type == CONFIG_TYPE_STRING) {
        slapi_ch_free((void **)&val);
    }
}

/*
 * Returns:
 *   SLAPI_DSE_CALLBACK_ERROR on failure
 *   SLAPI_DSE_CALLBACK_OK on success
 */
int
bdb_config_search_entry_callback(Slapi_PBlock *pb __attribute__((unused)),
                                  Slapi_Entry *e,
                                  Slapi_Entry *entryAfter __attribute__((unused)),
                                  int *returncode,
                                  char *returntext,
                                  void *arg)
{
    char buf[BUFSIZ];
    struct berval *vals[2];
    struct berval val;
    struct ldbminfo *li = (struct ldbminfo *)arg;
    config_info *config;

    vals[0] = &val;
    vals[1] = NULL;

    returntext[0] = '\0';

    PR_Lock(li->li_config_mutex);

    for (config = bdb_config_param; config->config_name != NULL; config++) {
        /* Go through the bdb_config table and fill in the entry. */

        if (!(config->config_flags & (CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_PREVIOUSLY_SET))) {
            /* This config option shouldn't be shown */
            continue;
        }

        bdb_config_get((void *)li, config, buf);

        val.bv_val = buf;
        val.bv_len = strlen(buf);
        slapi_entry_attr_replace(e, config->config_name, vals);
    }

    PR_Unlock(li->li_config_mutex);

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}


int
bdb_config_ignored_attr(char *attr_name)
{
    /* These are the names of attributes that are in the
     * config entries but are not config attributes. */
    if (!strcasecmp("objectclass", attr_name) ||
        !strcasecmp("cn", attr_name) ||
        !strcasecmp("creatorsname", attr_name) ||
        !strcasecmp("createtimestamp", attr_name) ||
        !strcasecmp(LDBM_NUMSUBORDINATES_STR, attr_name) ||
        slapi_attr_is_last_mod(attr_name)) {
        return 1;
    } else {
        return 0;
    }
}

/* Returns LDAP_SUCCESS on success */
int
bdb_config_set(void *arg, char *attr_name, config_info *config_array, struct berval *bval, char *err_buf, int phase, int apply_mod, int mod_op)
{
    config_info *config;
    int use_default;
    int int_val;
    long long_val;
    size_t sz_val;
    PRInt64 llval;
    int maxint = (int)(((unsigned int)~0) >> 1);
    int minint = ~maxint;
    PRInt64 llmaxint;
    PRInt64 llminint;
    int err = 0;
    char *str_val;
    int retval = 0;

    LL_I2L(llmaxint, maxint);
    LL_I2L(llminint, minint);

    config = config_info_get(config_array, attr_name);
    if (NULL == config) {
        slapi_log_err(SLAPI_LOG_CONFIG, "bdb_config_set", "Unknown config attribute %s\n", attr_name);
        slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Unknown config attribute %s\n", attr_name);
        return LDAP_SUCCESS; /* Ignore unknown attributes */
    }

    /* Some config attrs can't be changed while the server is running. */
    if (phase == CONFIG_PHASE_RUNNING &&
        !(config->config_flags & CONFIG_FLAG_ALLOW_RUNNING_CHANGE)) {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_config_set", "%s can't be modified while the server is running.\n", attr_name);
        slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "%s can't be modified while the server is running.\n", attr_name);
        return LDAP_UNWILLING_TO_PERFORM;
    }

    /* If the config phase is initialization or if bval is NULL or if we are deleting
       the value, we will use the default value for the attribute. */
    if ((CONFIG_PHASE_INITIALIZATION == phase) || (NULL == bval) || SLAPI_IS_MOD_DELETE(mod_op)) {
        if (CONFIG_FLAG_SKIP_DEFAULT_SETTING & config->config_flags) {
            return LDAP_SUCCESS; /* Skipping the default config setting */
        }
        use_default = 1;
    } else {
        use_default = 0;

        /* cannot use mod add on a single valued attribute if the attribute was
           previously set to a non-default value */
        if (SLAPI_IS_MOD_ADD(mod_op) && apply_mod &&
            (config->config_flags & CONFIG_FLAG_PREVIOUSLY_SET)) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "cannot add a value to single valued attribute %s.\n", attr_name);
            return LDAP_OBJECT_CLASS_VIOLATION;
        }
    }

    /* if delete, and a specific value was provided to delete, the existing value must
       match that value, or return LDAP_NO_SUCH_ATTRIBUTE */
    if (SLAPI_IS_MOD_DELETE(mod_op) && bval && bval->bv_len && bval->bv_val) {
        char buf[BUFSIZ];
        bdb_config_get(arg, config, buf);
        if (PL_strncmp(buf, bval->bv_val, bval->bv_len)) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE,
                                  "value [%s] for attribute %s does not match existing value [%s].\n", bval->bv_val, attr_name, buf);
slapi_log_err(SLAPI_LOG_ERR, (char*)__FUNCTION__, "%s:%d returns LDAP_NO_SUCH_ATTRIBUTE\n", __FILE__, __LINE__);
            return LDAP_NO_SUCH_ATTRIBUTE;
        }
    }

    switch (config->config_type) {
    case CONFIG_TYPE_INT:
        if (use_default || bval == NULL) {
            str_val = config->config_default_value;
        } else {
            str_val = bval->bv_val;
        }
        /* get the value as a 64 bit value */
        llval = db_atoi(str_val, &err);
        /* check for parsing error (e.g. not a number) */
        if (err) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is not a number\n", str_val, attr_name);
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_set", "Value %s for attr %s is not a number\n", str_val, attr_name);
            return LDAP_UNWILLING_TO_PERFORM;
            /* check for overflow */
        } else if (LL_CMP(llval, >, llmaxint)) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is greater than the maximum %d\n",
                                  str_val, attr_name, maxint);
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_set", "Value %s for attr %s is greater than the maximum %d\n",
                          str_val, attr_name, maxint);
            return LDAP_UNWILLING_TO_PERFORM;
            /* check for underflow */
        } else if (LL_CMP(llval, <, llminint)) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is less than the minimum %d\n",
                                  str_val, attr_name, minint);
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_set", "Value %s for attr %s is less than the minimum %d\n",
                          str_val, attr_name, minint);
            return LDAP_UNWILLING_TO_PERFORM;
        }
        /* convert 64 bit value to 32 bit value */
        LL_L2I(int_val, llval);
        retval = config->config_set_fn(arg, (void *)((uintptr_t)int_val), err_buf, phase, apply_mod);
        break;
    case CONFIG_TYPE_INT_OCTAL:
        if (use_default || bval == NULL) {
            int_val = (int)strtol(config->config_default_value, NULL, 8);
        } else {
            int_val = (int)strtol((char *)bval->bv_val, NULL, 8);
        }
        retval = config->config_set_fn(arg, (void *)((uintptr_t)int_val), err_buf, phase, apply_mod);
        break;
    case CONFIG_TYPE_LONG:
        if (use_default || bval == NULL) {
            str_val = config->config_default_value;
        } else {
            str_val = bval->bv_val;
        }
        /* get the value as a 64 bit value */
        llval = db_atoi(str_val, &err);
        /* check for parsing error (e.g. not a number) */
        if (err) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is not a number\n",
                                  str_val, attr_name);
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_set", "Value %s for attr %s is not a number\n",
                          str_val, attr_name);
            return LDAP_UNWILLING_TO_PERFORM;
            /* check for overflow */
        } else if (LL_CMP(llval, >, llmaxint)) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is greater than the maximum %d\n",
                                  str_val, attr_name, maxint);
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_set", "Value %s for attr %s is greater than the maximum %d\n",
                          str_val, attr_name, maxint);
            return LDAP_UNWILLING_TO_PERFORM;
            /* check for underflow */
        } else if (LL_CMP(llval, <, llminint)) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is less than the minimum %d\n",
                                  str_val, attr_name, minint);
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_set", "Value %s for attr %s is less than the minimum %d\n",
                          str_val, attr_name, minint);
            return LDAP_UNWILLING_TO_PERFORM;
        }
        /* convert 64 bit value to 32 bit value */
        LL_L2I(long_val, llval);
        retval = config->config_set_fn(arg, (void *)long_val, err_buf, phase, apply_mod);
        break;
    case CONFIG_TYPE_SIZE_T:
        if (use_default || bval == NULL) {
            str_val = config->config_default_value;
        } else {
            str_val = bval->bv_val;
        }

        /* get the value as a size_t value */
        sz_val = db_strtoul(str_val, &err);

        /* check for parsing error (e.g. not a number) */
        if (err == EINVAL) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is not a number\n",
                                  str_val, attr_name);
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_set", "Value %s for attr %s is not a number\n",
                          str_val, attr_name);
            return LDAP_UNWILLING_TO_PERFORM;
            /* check for overflow */
        } else if (err == ERANGE) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is outside the range of representable values\n",
                                  str_val, attr_name);
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_set", "Value %s for attr %s is outside the range of representable values\n",
                          str_val, attr_name);
            return LDAP_UNWILLING_TO_PERFORM;
        }
        retval = config->config_set_fn(arg, (void *)sz_val, err_buf, phase, apply_mod);
        break;


    case CONFIG_TYPE_UINT64:
        if (use_default || bval == NULL) {
            str_val = config->config_default_value;
        } else {
            str_val = bval->bv_val;
        }
        /* get the value as a size_t value */
        sz_val = db_strtoull(str_val, &err);

        /* check for parsing error (e.g. not a number) */
        if (err == EINVAL) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is not a number\n",
                                  str_val, attr_name);
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_set", "Value %s for attr %s is not a number\n",
                          str_val, attr_name);
            return LDAP_UNWILLING_TO_PERFORM;
        /* check for overflow */
        } else if (err == ERANGE) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is outside the range of representable values\n",
                                  str_val, attr_name);
            slapi_log_err(SLAPI_LOG_ERR, "bdb_config_set", "Value %s for attr %s is outside the range of representable values\n",
                          str_val, attr_name);
            return LDAP_UNWILLING_TO_PERFORM;
        }
        retval = config->config_set_fn(arg, (void *)sz_val, err_buf, phase, apply_mod);
        break;
    case CONFIG_TYPE_STRING:
        if (use_default || bval == NULL) {
            retval = config->config_set_fn(arg, config->config_default_value, err_buf, phase, apply_mod);
        } else {
            retval = config->config_set_fn(arg, bval->bv_val, err_buf, phase, apply_mod);
        }
        break;
    case CONFIG_TYPE_ONOFF:
        if (use_default || bval == NULL) {
            int_val = !strcasecmp(config->config_default_value, "on");
        } else {
            int_val = !strcasecmp((char *)bval->bv_val, "on");
        }
        retval = config->config_set_fn(arg, (void *)((uintptr_t)int_val), err_buf, phase, apply_mod);
        break;
    }

    /* operation was successful and we applied the value? */
    if (!retval && apply_mod) {
        /* Since we are setting the value for the config attribute, we
         * need to turn on the CONFIG_FLAG_PREVIOUSLY_SET flag to make
         * sure this attribute is shown. */
        if (use_default) {
            /* attr deleted or we are using the default value */
            config->config_flags &= ~CONFIG_FLAG_PREVIOUSLY_SET;
        } else {
            /* attr set explicitly */
            config->config_flags |= CONFIG_FLAG_PREVIOUSLY_SET;
        }
    }

    return retval;
}

static void
bdb_split_bdb_config_entry(struct ldbminfo *li, Slapi_Entry *ldbm_conf_e,Slapi_Entry *bdb_conf_e, config_info *config_array, Slapi_Mods *smods)
{
    Slapi_Attr *attr = NULL;

    for (slapi_entry_first_attr(ldbm_conf_e, &attr); attr; slapi_entry_next_attr(ldbm_conf_e, attr, &attr)) {
        char *attr_name = NULL;
        Slapi_Value *sval = NULL;

        slapi_attr_get_type(attr, &attr_name);

        /* There are some attributes that we don't care about, like objectclass. */
        if (bdb_config_ignored_attr(attr_name)) {
            continue;
        }
        if (NULL == config_info_get(config_array, attr_name)) {
            /* this attr is not bdb specific */
            continue;
        }
        slapi_attr_first_value(attr, &sval);
        slapi_entry_add_string(bdb_conf_e, attr_name, slapi_value_get_string(sval));
        slapi_mods_add(smods, LDAP_MOD_DELETE, attr_name, 0, NULL);
    }
}

static int
bdb_parse_bdb_config_entry(struct ldbminfo *li, Slapi_Entry *e, config_info *config_array)
{
    Slapi_Attr *attr = NULL;

    for (slapi_entry_first_attr(e, &attr); attr; slapi_entry_next_attr(e, attr, &attr)) {
        char *attr_name = NULL;
        Slapi_Value *sval = NULL;
        struct berval *bval;
        char err_buf[SLAPI_DSE_RETURNTEXT_SIZE];

        slapi_attr_get_type(attr, &attr_name);

        /* There are some attributes that we don't care about, like objectclass. */
        if (bdb_config_ignored_attr(attr_name)) {
            continue;
        }
        slapi_attr_first_value(attr, &sval);
        bval = (struct berval *)slapi_value_get_berval(sval);

        if (bdb_config_set(li, attr_name, config_array, bval, err_buf, CONFIG_PHASE_STARTUP, 1 /* apply */, LDAP_MOD_REPLACE) != LDAP_SUCCESS) {
            slapi_log_err(SLAPI_LOG_ERR, "bdb_parse_bdb_config_entry", "Error with config attribute %s : %s\n", attr_name, err_buf);
            return 1;
        }
    }
    return 0;
}

/* helper for deleting mods (we do not want to be applied) from the mods array */
static void
bdb_mod_free(LDAPMod *mod)
{
    ber_bvecfree(mod->mod_bvalues);
    slapi_ch_free((void **)&(mod->mod_type));
    slapi_ch_free((void **)&mod);
}

/*
 * Returns:
 *   SLAPI_DSE_CALLBACK_ERROR on failure
 *   SLAPI_DSE_CALLBACK_OK on success
 */
int
bdb_config_modify_entry_callback(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *e, int *returncode, char *returntext, void *arg)
{
    int i;
    char *attr_name;
    LDAPMod **mods;
    int rc = LDAP_SUCCESS;
    int apply_mod = 0;
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int reapply_mods = 0;
    int idx = 0;

    /* This lock is probably way too conservative, but we don't expect much
     * contention for it. */
    PR_Lock(li->li_config_mutex);

    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);

    returntext[0] = '\0';

    /*
     * First pass: set apply mods to 0 so only input validation will be done;
     * 2nd pass: set apply mods to 1 to apply changes to internal storage
     */
    for (apply_mod = 0; apply_mod <= 1 && LDAP_SUCCESS == rc; apply_mod++) {
        for (i = 0; mods && mods[i] && LDAP_SUCCESS == rc; i++) {
            attr_name = mods[i]->mod_type;

            /* There are some attributes that we don't care about, like modifiersname. */
            if (bdb_config_ignored_attr(attr_name)) {
                if (apply_mod) {
                    Slapi_Attr *origattr = NULL;
                    Slapi_ValueSet *origvalues = NULL;
                    mods[idx++] = mods[i];
                    /* we also need to restore the entryAfter e to its original
                       state, because the dse code will attempt to reapply
                       the mods again */
                    slapi_entry_attr_find(entryBefore, attr_name, &origattr);
                    if (NULL != origattr) {
                        slapi_attr_get_valueset(origattr, &origvalues);
                        if (NULL != origvalues) {
                            slapi_entry_add_valueset(e, attr_name, origvalues);
                            slapi_valueset_free(origvalues);
                        }
                    }
                    reapply_mods = 1; /* there is at least one mod we removed */
                }
                continue;
            }

            /* when deleting a value, and this is the last or only value, set
               the config param to its default value
               when adding a value, if the value is set to its default value, replace
               it with the new value - otherwise, if it is single valued, reject the
               operation with TYPE_OR_VALUE_EXISTS */
            /* This assumes there is only one bval for this mod. */
            rc = bdb_config_set((void *)li, attr_name, bdb_config_param,
                                 (mods[i]->mod_bvalues == NULL) ? NULL
                                                                : mods[i]->mod_bvalues[0],
                                 returntext,
                                 ((li->li_flags & LI_FORCE_MOD_CONFIG) ? CONFIG_PHASE_INTERNAL : CONFIG_PHASE_RUNNING),
                                 apply_mod, mods[i]->mod_op);
            if (apply_mod) {
                bdb_mod_free(mods[i]);
                mods[i] = NULL;
            }
        }
    }

    PR_Unlock(li->li_config_mutex);

    if (reapply_mods) {
        mods[idx] = NULL;
        slapi_pblock_set(pb, SLAPI_DSE_REAPPLY_MODS, &reapply_mods);
    }

    *returncode = rc;
    if (LDAP_SUCCESS == rc) {
        return SLAPI_DSE_CALLBACK_OK;
    } else {
        return SLAPI_DSE_CALLBACK_ERROR;
    }
}


/* This function is used to set config attributes. It can be used as a
 * shortcut to doing an internal modify operation on the config DSE.
 */
int
bdb_config_internal_set(struct ldbminfo *li, char *attrname, char *value)
{
    char err_buf[SLAPI_DSE_RETURNTEXT_SIZE];
    struct berval bval;

    bval.bv_val = value;
    bval.bv_len = strlen(value);

    if (bdb_config_set((void *)li, attrname, bdb_config_param, &bval,
                        err_buf, CONFIG_PHASE_INTERNAL, 1 /* apply */,
                        LDAP_MOD_REPLACE) != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_config_internal_set", "Error setting instance config attr %s to %s: %s\n",
                      attrname, value, err_buf);
        exit(1);
    }
    return LDAP_SUCCESS;
}

void
bdb_public_config_get(struct ldbminfo *li, char *attrname, char *value)
{
    config_info *config = config_info_get(bdb_config_param, attrname);
    if (NULL == config) {
        slapi_log_err(SLAPI_LOG_CONFIG, "bdb_public_config_get", "Unknown config attribute %s\n", attrname);
        value[0] = '\0';
    } else {
        bdb_config_get(li, config, value);
    }
}
int
bdb_public_config_set(struct ldbminfo *li, char *attrname, int apply_mod, int mod_op, int phase, char *value)
{
    char err_buf[SLAPI_DSE_RETURNTEXT_SIZE];
    int rc = LDAP_SUCCESS;

    if (!value && SLAPI_IS_MOD_ADD(mod_op)) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_public_internal_set", "Error: no value for config attr: %s\n",
                      attrname);
        return -1;
    }

    if (value) {
        struct berval bval;
        bval.bv_val = value;
        bval.bv_len = strlen(value);

        rc = bdb_config_set((void *)li, attrname, bdb_config_param, &bval,
                            err_buf, phase, apply_mod, mod_op);
    } else {
        rc = bdb_config_set((void *)li, attrname, bdb_config_param, NULL,
                            err_buf, phase, apply_mod, mod_op);
    }
    if (rc != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_public_config_set", "Error setting instance config attr %s to %s: %s\n",
                      attrname, value, err_buf);
    }
    return rc;
}

/* Callback function for libdb to spit error info into our log */
void
bdb_log_print(const DB_ENV *dbenv __attribute__((unused)), const char *prefix __attribute__((unused)), const char *buffer)
{
    /* We ignore the prefix since we know who we are anyway */
    slapi_log_err(SLAPI_LOG_ERR, "libdb", "%s\n", (char *)(buffer ? buffer : "(NULL)"));
}

void
bdb_set_env_debugging(DB_ENV *pEnv, bdb_config *conf)
{
    pEnv->set_errpfx(pEnv, "ns-slapd");
    if (conf->bdb_debug_verbose) {
        pEnv->set_verbose(pEnv, DB_VERB_DEADLOCK, 1); /* 1 means on */
        pEnv->set_verbose(pEnv, DB_VERB_RECOVERY, 1); /* 1 means on */
        pEnv->set_verbose(pEnv, DB_VERB_WAITSFOR, 1); /* 1 means on */
    }
    if (conf->bdb_debug) {
        pEnv->set_errcall(pEnv, bdb_log_print);
    }
}

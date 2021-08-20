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

/* dbmdb_ctx_t.c - Handles configuration information that is specific to a MDB backend instance. */

#include "mdb_layer.h"
#include <sys/statvfs.h>


/* Forward declarations */
static int dbmdb_parse_dbmdb_ctx_t_entry(struct ldbminfo *li, Slapi_Entry *e, config_info *config_array);
static void dbmdb_split_dbmdb_ctx_t_entry(struct ldbminfo *li, Slapi_Entry *ldbm_conf_e,Slapi_Entry *dbmdb_conf_e, config_info *config_array, Slapi_Mods *smods);

/* Forward callback declarations */
int dbmdb_ctx_t_search_entry_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int dbmdb_ctx_t_modify_entry_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);

static dblayer_private dbmdb_fake_priv;   /* A copy of the callback array used by dbmdb_be() */

backend *dbmdb_be(void)
{
    static backend be = {0};
    static struct slapdplugin plg = {0};
    static struct ldbminfo li = {0};

    if (be.be_database == NULL) {
        be.be_database = &plg;
        plg.plg_private = &li;
        li.li_dblayer_private = &dbmdb_fake_priv;
    }
    return &be;
}

int
dbmdb_compute_limits(struct ldbminfo *li)
{
    dbmdb_limits_t *limits = &MDB_CONFIG(li)->limits;
    dbmdb_info_t *info = &MDB_CONFIG(li)->info;
    char *home_dir = MDB_CONFIG(li)->home;
    uint64_t total_space = 0;
    uint64_t avail_space = 0;
    uint64_t cur_dbsize = 0;
    int nbchangelogs = 0;
    int nbsuffixes = 0;
    int nbindexes = 0;
    int nbagmt = 0;
    int dirmode = 0;
    int tmpmode = 0500;
    int v1 = 0, v2 = 0, v3 = 0;
    struct statvfs buf = {0};


    /*
     * There is no db cache with mdb (Or rather
     *  the OS memory management mechanism acts as the cache)
     *  But some tunable may be autotuned.
     */
    if (dbmdb_count_config_entries("(objectClass=nsMappingTree)", &nbsuffixes) ||
        dbmdb_count_config_entries("(objectClass=nsIndex)", &nbsuffixes) ||
        dbmdb_count_config_entries("(&(objectClass=nsds5Replica)(nsDS5Flags=1))", &nbchangelogs) ||
        dbmdb_count_config_entries("(objectClass=nsds5replicationagreement)", &nbagmt)) {
        /* error message is already logged */
        return 1;
    }
    /* li_mode is for file so fo directory lets add x mode for each r mode */
    for (dirmode = li->li_mode; tmpmode; tmpmode>>=3) {
        if (dirmode & tmpmode & 0444) {
            dirmode |= tmpmode;
        }
    }
    mkdir_p(home_dir, dirmode);
    if (statvfs(home_dir, &buf)) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_compute_limits", "Unable to get db home device size. errno=%d\n", errno);
        return 1;
    }
    cur_dbsize = dbmdb_database_size(li);

    info->pagesize = sysconf(_SC_PAGE_SIZE);
    limits->min_readers = config_get_threadnumber() + nbagmt + DBMDB_READERS_MARGIN;
    /* Default indexes are counted in "nbindexes" so we should always have enough resource to add 1 new suffix */
    limits->min_dbs = nbsuffixes + nbindexes + nbchangelogs + DBMDB_DBS_MARGIN;

    total_space = ((uint64_t)(buf.f_blocks)) * ((uint64_t)(buf.f_bsize));
    avail_space = ((uint64_t)(buf.f_bavail)) * ((uint64_t)(buf.f_bsize));

    limits->disk_reserve = DBMDB_DISK_RESERVE(total_space);
    limits->min_size = DBMDB_DB_MINSIZE;
    limits->max_size = (avail_space + cur_dbsize) * 9 / 10;
    info->strversion = mdb_version(&v1,&v2, &v3);
    info->libversion = DBMDB_LIBVERSION(v1, v2, v3);
    info->dataversion = DBMDB_CURRENT_DATAVERSION;
    return 0;
}

/* dbmdb plugin  entry point */
int mdb_init(struct ldbminfo *li, config_info *config_array)
{
    dbmdb_ctx_t *conf = (dbmdb_ctx_t *)slapi_ch_calloc(1, sizeof(dbmdb_ctx_t));
    dbmdb_componentid = generate_componentid(NULL, "db-mdb");

    li->li_dblayer_config = conf;
    strncpy(conf->home, li->li_directory, MAXPATHLEN);
    pthread_mutex_init(&conf->dbis_lock, NULL);
    pthread_mutex_init(&conf->rcmutex, NULL);
    pthread_rwlock_init(&conf->dbmdb_env_lock, NULL);

    dbmdb_ctx_t_setup_default(li);
    /* Do not compute limit if dse.ldif is not taken in account (i.e. dbscan) */
    if (li->li_config_mutex) {
        dbmdb_compute_limits(li);
    }

    dblayer_private *priv = li->li_dblayer_private;
    priv->dblayer_start_fn = &dbmdb_start;
    priv->dblayer_close_fn = &dbmdb_close;
    priv->dblayer_instance_start_fn = &dbmdb_instance_start;
    priv->dblayer_backup_fn = &dbmdb_backup;
    priv->dblayer_verify_fn = &dbmdb_verify;
    priv->dblayer_db_size_fn = &dbmdb_db_size;
    priv->dblayer_ldif2db_fn = &dbmdb_ldif2db;
    priv->dblayer_db2ldif_fn = &dbmdb_db2ldif;
    priv->dblayer_db2index_fn = &dbmdb_db2index;
    priv->dblayer_cleanup_fn = &dbmdb_cleanup;
    priv->dblayer_upgradedn_fn = &dbmdb_upgradednformat;
    priv->dblayer_upgradedb_fn = &dbmdb_upgradedb;
    priv->dblayer_restore_fn = &dbmdb_restore;
    priv->dblayer_txn_begin_fn = &dbmdb_txn_begin;
    priv->dblayer_txn_commit_fn = &dbmdb_txn_commit;
    priv->dblayer_txn_abort_fn = &dbmdb_txn_abort;
    priv->dblayer_get_info_fn = &dbmdb_get_info;
    priv->dblayer_set_info_fn = &dbmdb_set_info;
    priv->dblayer_back_ctrl_fn = &dbmdb_back_ctrl;
    priv->dblayer_get_db_fn = &dbmdb_get_db;
    priv->dblayer_rm_db_file_fn = &dbmdb_rm_db_file;
    priv->dblayer_delete_db_fn = &dbmdb_delete_db;
    priv->dblayer_import_fn = &dbmdb_public_dbmdb_import_main;
    priv->dblayer_load_dse_fn = &dbmdb_ctx_t_load_dse_info;
    priv->dblayer_config_get_fn = &dbmdb_public_config_get;
    priv->dblayer_config_set_fn = &dbmdb_public_config_set;
    priv->instance_config_set_fn = &dbmdb_instance_config_set;
    priv->instance_add_config_fn = &dbmdb_instance_add_instance_entry_callback;
    priv->instance_postadd_config_fn = &dbmdb_instance_postadd_instance_entry_callback;
    priv->instance_del_config_fn = &dbmdb_instance_delete_instance_entry_callback;
    priv->instance_postdel_config_fn = &dbmdb_instance_post_delete_instance_entry_callback;
    priv->instance_cleanup_fn = &dbmdb_instance_cleanup;
    priv->instance_create_fn = &dbmdb_instance_create;
    priv->instance_register_monitor_fn = &dbmdb_instance_register_monitor;
    priv->instance_search_callback_fn = &dbmdb_instance_search_callback;
    priv->dblayer_auto_tune_fn = &dbmdb_start_autotune;
    priv->dblayer_get_db_filename_fn = &dbmdb_public_get_db_filename;
    priv->dblayer_bulk_free_fn = &dbmdb_public_bulk_free;
    priv->dblayer_bulk_nextdata_fn = &dbmdb_public_bulk_nextdata;
    priv->dblayer_bulk_nextrecord_fn = &dbmdb_public_bulk_nextrecord;
    priv->dblayer_bulk_init_fn = &dbmdb_public_bulk_init;
    priv->dblayer_bulk_start_fn = &dbmdb_public_bulk_start;
    priv->dblayer_cursor_bulkop_fn = &dbmdb_public_cursor_bulkop;
    priv->dblayer_cursor_op_fn = &dbmdb_public_cursor_op;
    priv->dblayer_db_op_fn = &dbmdb_public_db_op;
    priv->dblayer_new_cursor_fn = &dbmdb_public_new_cursor;
    priv->dblayer_value_free_fn = &dbmdb_public_value_free;
    priv->dblayer_value_init_fn = &dbmdb_public_value_init;
    priv->dblayer_set_dup_cmp_fn = &dbmdb_public_set_dup_cmp_fn;
    priv->dblayer_dbi_txn_begin_fn = &dbmdb_dbi_txn_begin;
    priv->dblayer_dbi_txn_commit_fn = &dbmdb_dbi_txn_commit;
    priv->dblayer_dbi_txn_abort_fn = &dbmdb_dbi_txn_abort;
    priv->dblayer_get_entries_count_fn = &dbmdb_get_entries_count;
    priv->dblayer_cursor_get_count_fn = &dbmdb_public_cursor_get_count;
    priv->dblayer_private_open_fn = &dbmdb_public_private_open;
    priv->dblayer_private_close_fn = &dbmdb_public_private_close;
    priv->ldbm_back_wire_import_fn = &dbmdb_ldbm_back_wire_import;
    priv->dblayer_restore_file_init_fn = &dbmdb_restore_file_init;
    priv->dblayer_restore_file_update_fn = &dbmdb_restore_file_update;
    priv->dblayer_import_file_check_fn = &dbmdb_import_file_check;
    priv->dblayer_list_dbs_fn = &dbmdb_list_dbs;
    priv->dblayer_in_import_fn = &dbmdb_public_in_import;
    priv->dblayer_get_db_suffix_fn = &dbmdb_public_get_db_suffix;
    priv->dblayer_compact_fn = &dbmdb_public_dblayer_compact;
    priv->dblayer_clear_vlv_cache_fn = &dbmdb_public_clear_vlv_cache;

    dbmdb_fake_priv = *priv; /* Copy the callbaks for dbmdb_be() */
    return 0;
}

/* Used to add an array of entries, like the one above and
 * dbmdb_instance_skeleton_entries in dbmdb_instance_config.c, to the dse.
 * Returns 0 on success.
 */
int
dbmdb_ctx_t_add_dse_entries(struct ldbminfo *li, char **entries, char *string1, char *string2, char *string3, int flags)
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
            slapi_log_err(SLAPI_LOG_CONFIG, "dbmdb_ctx_t_add_dse_entries", "Added database config entry [%s]\n", ebuf);
        } else if (result == LDAP_ALREADY_EXISTS) {
            slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_ctx_t_add_dse_entries", "Database config entry [%s] already exists - skipping\n", ebuf);
        } else {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_add_dse_entries",
                          "Unable to add config entry [%s] to the DSE: %d %d\n",
                          ebuf, result, rc);
        }
        slapi_pblock_destroy(util_pb);
    }

    return 0;
}

/* used to add a single entry, special case of above */
int
dbmdb_ctx_t_add_dse_entry(struct ldbminfo *li, char *entry, int flags)
{
    char *entries[] = {"%s", ""};

    return dbmdb_ctx_t_add_dse_entries(li, entries, entry, NULL, NULL, flags);
}


/*------------------------------------------------------------------------
 * Get and set functions for mdb variables
 *----------------------------------------------------------------------*/

static void *
dbmdb_ctx_t_db_max_size_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    dbmdb_ctx_t *conf = li->li_dblayer_config;

    return  (void *)((uintptr_t)(conf->dsecfg.max_size));
}

static int
dbmdb_ctx_t_db_max_size_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    dbmdb_ctx_t *conf = li->li_dblayer_config;
    int retval = LDAP_SUCCESS;
    uint64_t val = (uint64_t)((uintptr_t)value);
    uint64_t curval = val;

    if (conf->limits.max_size < conf->limits.min_size) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_db_max_size_set",
                "Not enough space on %s home directory to host a database.\n", conf->home);
        return LDAP_UNWILLING_TO_PERFORM;
    }
    if (val != 0) {
        /* Let check the limits */
        if (curval < conf->limits.min_size) {
            curval = conf->limits.min_size;
        }
        if (curval > conf->limits.max_size) {
            curval = conf->limits.max_size;
        }
        if (curval > val) {
            slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_ctx_t_db_max_size_set",
                "nsslapd-mdb-max-size value is too small."
                " Increasing the value from %lud to %lud\n", val, curval);
        } else if (curval < val) {
            slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_ctx_t_db_max_size_set",
                "nsslapd-mdb-max-size value is not compatible with current partition size."
                " Decreasing the value from %lud to %lud\n", val, curval);
       }
        val = curval;
    }

    if (apply) {
        conf->dsecfg.max_size = val;
        if (CONFIG_PHASE_RUNNING == phase) {
            slapi_log_err(SLAPI_LOG_NOTICE, "dbmdb_ctx_t_db_max_size_set",
                          "New nsslapd-mdb-max-size will not take affect until the server is restarted\n");
        }
    }

    return retval;
}

static void *
dbmdb_ctx_t_db_max_readers_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    dbmdb_ctx_t *conf = li->li_dblayer_config;

    return  (void *)((uintptr_t)(conf->dsecfg.max_readers));
}

static int
dbmdb_ctx_t_db_max_readers_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    dbmdb_ctx_t *conf = li->li_dblayer_config;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);
    int curval = val;

    if (curval < conf->limits.min_readers) {
        curval = conf->limits.min_readers;
    }
    if (val && curval != val) {
        slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_ctx_t_db_max_readers_set",
                "nsslapd-mdb-max-readers value is not compatible with current configuration."
                " Increasing the value from %d to %d\n", val, curval);
        val = curval;
    }

    if (apply) {
        conf->dsecfg.max_readers = val;
        if (CONFIG_PHASE_RUNNING == phase) {
            slapi_log_err(SLAPI_LOG_NOTICE, "dbmdb_ctx_t_db_max_dbs_set",
                "New nsslapd-mdb-max-dbs will not take affect until the server is restarted\n");
        }
    }

    return retval;
}

static void *
dbmdb_ctx_t_db_max_dbs_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    dbmdb_ctx_t *conf = li->li_dblayer_config;

    return  (void *)((uintptr_t)(conf->dsecfg.max_dbs));
}

static int
dbmdb_ctx_t_db_max_dbs_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    dbmdb_ctx_t *conf = li->li_dblayer_config;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);
    int curval = val;

    if (curval < conf->limits.min_dbs) {
        curval = conf->limits.min_dbs;
    }
    if (val && curval != val) {
        slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_ctx_t_db_max_dbs_set",
                "nsslapd-mdb-max-dbs value is not compatible with current configuration."
                " Increasing the value from %d to %d\n", val, curval);
        val = curval;
    }

    if (apply) {
        conf->dsecfg.max_dbs = val;
        if (CONFIG_PHASE_RUNNING == phase) {
            slapi_log_err(SLAPI_LOG_NOTICE, "dbmdb_ctx_t_db_max_dbs_set",
                "New nsslapd-mdb-max-dbs will not take affect until the server is restarted\n");
        }
    }

    return retval;
}
static void *
dbmdb_ctx_t_maxpassbeforemerge_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(li->li_maxpassbeforemerge));
}

static int
dbmdb_ctx_t_maxpassbeforemerge_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (val < 0) {
        slapi_log_err(SLAPI_LOG_NOTICE, "dbmdb_ctx_t_maxpassbeforemerge_set",
                      "maxpassbeforemerge will not take negative value - setting to 100\n");
        val = 100;
    }

    if (apply) {
        li->li_maxpassbeforemerge = val;
    }

    return retval;
}

static void *
dbmdb_ctx_t_db_durable_transactions_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(MDB_CONFIG(li)->dsecfg.durable_transactions));
}

static int
dbmdb_ctx_t_db_durable_transactions_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        MDB_CONFIG(li)->dsecfg.durable_transactions = val;
    }

    return retval;
}

static int
dbmdb_ctx_t_set_bypass_filter_test(void *arg,
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
dbmdb_ctx_t_get_bypass_filter_test(void *arg)
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
dbmdb_ctx_t_serial_lock_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)li->li_fat_lock);
}

static int
dbmdb_ctx_t_serial_lock_set(void *arg,
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




/*------------------------------------------------------------------------
 * Configuration array for mdb variables
 *----------------------------------------------------------------------*/
static config_info dbmdb_ctx_t_param[] = {
    {CONFIG_MDB_MAX_SIZE, CONFIG_TYPE_UINT64, "0", &dbmdb_ctx_t_db_max_size_get, &dbmdb_ctx_t_db_max_size_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_MDB_MAX_READERS, CONFIG_TYPE_INT, "0", &dbmdb_ctx_t_db_max_readers_get, &dbmdb_ctx_t_db_max_readers_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_MDB_MAX_DBS, CONFIG_TYPE_INT, "128", &dbmdb_ctx_t_db_max_dbs_get, &dbmdb_ctx_t_db_max_dbs_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_MAXPASSBEFOREMERGE, CONFIG_TYPE_INT, "100", &dbmdb_ctx_t_maxpassbeforemerge_get, &dbmdb_ctx_t_maxpassbeforemerge_set, 0},
    {CONFIG_DB_DURABLE_TRANSACTIONS, CONFIG_TYPE_ONOFF, "on", &dbmdb_ctx_t_db_durable_transactions_get, &dbmdb_ctx_t_db_durable_transactions_set, CONFIG_FLAG_ALWAYS_SHOW},
    {CONFIG_BYPASS_FILTER_TEST, CONFIG_TYPE_STRING, "on", &dbmdb_ctx_t_get_bypass_filter_test, &dbmdb_ctx_t_set_bypass_filter_test, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_SERIAL_LOCK, CONFIG_TYPE_ONOFF, "on", &dbmdb_ctx_t_serial_lock_get, &dbmdb_ctx_t_serial_lock_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {NULL, 0, NULL, NULL, NULL, 0}};

void
dbmdb_ctx_t_setup_default(struct ldbminfo *li)
{
    config_info *config;
    char err_buf[SLAPI_DSE_RETURNTEXT_SIZE];

    for (config = dbmdb_ctx_t_param; config->config_name != NULL; config++) {
        dbmdb_ctx_t_set((void *)li, config->config_name, dbmdb_ctx_t_param, NULL /* use default */, err_buf, CONFIG_PHASE_INITIALIZATION, 1 /* apply */, LDAP_MOD_REPLACE);
    }
}

static int
dbmdb_ctx_t_upgrade_dse_info(struct ldbminfo *li)
{
    Slapi_PBlock *search_pb;
    Slapi_PBlock *add_pb;
    Slapi_Entry *dbmdb_ctx_t = NULL;
    Slapi_Entry **entries = NULL;
    char *dbmdb_ctx_t_dn = NULL;
    char *config_dn = NULL;
    int rval = 0;
    Slapi_Mods smods;

    slapi_log_err(SLAPI_LOG_INFO, "dbmdb_ctx_t_upgrade_dse_info", "create config entry from old config\n");

    /* first get the existing ldbm config entry, if it fails
     * nothing can be done
     */

    config_dn = slapi_create_dn_string("cn=config,cn=%s,cn=plugins,cn=config",
                                li->li_plugin->plg_name);

    search_pb = slapi_pblock_new();
    if (!search_pb) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_load_dse_info", "Out of memory\n");
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
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_load_dse_info", "Error accessing the ldbm config DSE 2\n");
            rval = 1;
            goto bail;
        }
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_load_dse_info",
                      "Error accessing the ldbm config DSE 1\n");
        rval = 1;
        goto bail;
    }


    /* next create an new specifc mdb config entry,
     * look for attributes in the general config antry which
     * have to go to the mdb entry.
     * - add them to cn=mdb,cn=config,cn=ldbm database
     * - remove them from cn=config,cn=ldbm database
     */
    /* The new and changed config entry need to be kept independent of
     * the slapd exec mode leading here
     */
    dse_unset_dont_ever_write_dse_files();

    dbmdb_ctx_t = slapi_entry_alloc();
    dbmdb_ctx_t_dn = slapi_create_dn_string("cn=mdb,cn=config,cn=%s,cn=plugins,cn=config",
                                li->li_plugin->plg_name);
    slapi_entry_init(dbmdb_ctx_t, dbmdb_ctx_t_dn, NULL);

    slapi_entry_add_string(dbmdb_ctx_t, SLAPI_ATTR_OBJECTCLASS, "extensibleobject");

    slapi_mods_init(&smods, 1);
    dbmdb_split_dbmdb_ctx_t_entry(li, entries[0], dbmdb_ctx_t, dbmdb_ctx_t_param, &smods);
    add_pb = slapi_pblock_new();
    slapi_pblock_init(add_pb);

    slapi_add_entry_internal_set_pb(add_pb,
                                    dbmdb_ctx_t,
                                    NULL,
                                    li->li_identity,
                                    0);
    slapi_add_internal_pb(add_pb);
    slapi_pblock_get(add_pb, SLAPI_PLUGIN_INTOP_RESULT, &rval);

    if (rval != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_upgrade_dse_info", "failed to add mdb config_entry, err= %d\n", rval);
    } else {
        /* the new mdb config entry was successfully added
         * now strip the attrs from the general config entry
         */
        Slapi_PBlock *mod_pb = slapi_pblock_new();
        slapi_modify_internal_set_pb(mod_pb, config_dn,
                                    slapi_mods_get_ldapmods_byref(&smods),
                                    NULL, NULL, li->li_identity, 0);
        slapi_modify_internal_pb(mod_pb);
        slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &rval);
        if (rval != LDAP_SUCCESS) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_upgrade_dse_info", "failed to modify  config_entry, err= %d\n", rval);
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

/* Reads in any config information held in the dse for the mdb
 * implementation of the ldbm plugin.
 * Creates dse entries used to configure the ldbm plugin and dblayer
 * if they don't already exist.  Registers dse callback functions to
 * maintain those dse entries.  Returns 0 on success.
 */
int
dbmdb_ctx_t_load_dse_info(struct ldbminfo *li)
{
    Slapi_PBlock *search_pb;
    Slapi_Entry **entries = NULL;
    char *dn = NULL;
    int rval = 0;

    /* We try to read the entry
     * cn=mdb, cn=config, cn=ldbm database, cn=plugins, cn=config.  If the entry is
     * there, then we process the config information it stores.
     */
    dn = slapi_create_dn_string("cn=mdb,cn=config,cn=%s,cn=plugins,cn=config",
                                li->li_plugin->plg_name);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_ctx_t_load_dse_info",
                      "failed create config dn for %s\n",
                      li->li_plugin->plg_name);
        rval = 1;
        goto bail;
    }

    search_pb = slapi_pblock_new();
    if (!search_pb) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_load_dse_info", "Out of memory\n");
        rval = 1;
        goto bail;
    }

retry:
    slapi_search_internal_set_pb(search_pb, dn, LDAP_SCOPE_BASE,
                                 "objectclass=*", NULL, 0, NULL, NULL, li->li_identity, 0);
    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rval);

    if (rval == LDAP_SUCCESS) {
        /* Need to parse the configuration information for the mdb config
         * entry that is held in the DSE. */
        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                         &entries);
        if (NULL == entries || entries[0] == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_load_dse_info", "Error accessing the mdb config DSE entry\n");
            rval = 1;
            goto bail;
        }
        if (0 != dbmdb_parse_dbmdb_ctx_t_entry(li, entries[0], dbmdb_ctx_t_param)) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_load_dse_info", "Error parsing the mdb config DSE entry\n");
            rval = 1;
            goto bail;
        }
    } else if (rval == LDAP_NO_SUCH_OBJECT) {
    /* The specific mdb entry does not exist,
     * create it from the old config dse entry */
        if (dbmdb_ctx_t_upgrade_dse_info(li)) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_load_dse_info",
                          "Error accessing the mdb config DSE entry 1\n");
            rval = 1;
            goto bail;
        } else {
            slapi_free_search_results_internal(search_pb);
            slapi_pblock_init(search_pb);
            goto retry;
        }
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_load_dse_info",
                      "Error accessing the mdb config DSE entry 2\n");
        rval = 1;
        goto bail;
    }

    if (search_pb) {
        slapi_free_search_results_internal(search_pb);
        slapi_pblock_destroy(search_pb);
    }

    /* setup the dse callback functions for the ldbm backend config entry */
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", dbmdb_ctx_t_search_entry_callback,
                                   (void *)li);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", dbmdb_ctx_t_modify_entry_callback,
                                   (void *)li);
    slapi_config_register_callback(DSE_OPERATION_WRITE, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", dbmdb_ctx_t_search_entry_callback,
                                   (void *)li);
    slapi_ch_free_string(&dn);

    /* setup the dse callback functions for the ldbm backend monitor entry */
    dn = slapi_create_dn_string("cn=monitor,cn=%s,cn=plugins,cn=config",
                                li->li_plugin->plg_name);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_ctx_t_load_dse_info",
                      "failed to create monitor dn for %s\n",
                      li->li_plugin->plg_name);
        rval = 1;
        goto bail;
    }

    /* NOTE (LK): still needs to investigate and clarify the monitoring split between db layers.
     * Now still using ldbm functions
     */
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", dbmdb_monitor_search,
                                   (void *)li);
    slapi_ch_free_string(&dn);

    /* And the ldbm backend database monitor entry */
    dn = slapi_create_dn_string("cn=database,cn=monitor,cn=%s,cn=plugins,cn=config",
                                li->li_plugin->plg_name);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_ctx_t_load_dse_info",
                      "failed create monitor database dn for %s\n",
                      li->li_plugin->plg_name);
        rval = 1;
        goto bail;
    }
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", dbmdb_dbmonitor_search,
                                   (void *)li);
    MDB_CONFIG(li)->dsecfg.dseloaded = 1;

bail:
    slapi_ch_free_string(&dn);
    return rval;
}

/* general-purpose callback to deny an operation */
static int
dbmdb_deny_config(Slapi_PBlock *pb __attribute__((unused)),
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
dbmdb_instance_register_monitor(ldbm_instance *inst)
{
    struct ldbminfo *li = inst->inst_li;
    char *dn = NULL;

    dn = slapi_create_dn_string("cn=monitor,cn=%s,cn=%s,cn=plugins,cn=config",
                                inst->inst_name, li->li_plugin->plg_name);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_instance_register_monitor",
                      "failed create monitor instance dn for plugin %s, "
                      "instance %s\n",
                      inst->inst_li->li_plugin->plg_name, inst->inst_name);
        return 1;
    }
    /* make callback on search; deny add/modify/delete */
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", dbmdb_monitor_instance_search,
                                   (void *)inst);
    slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_SUBTREE, "(objectclass=*)", dbmdb_deny_config,
                                   (void *)inst);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", dbmdb_deny_config,
                                   (void *)inst);
    slapi_ch_free_string(&dn);

    return 0;
}

void
dbmdb_instance_unregister_monitor(ldbm_instance *inst)
{
    struct ldbminfo *li = inst->inst_li;
    char *dn = NULL;

    dn = slapi_create_dn_string("cn=monitor,cn=%s,cn=%s,cn=plugins,cn=config",
                                inst->inst_name, li->li_plugin->plg_name);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_instance_unregister_monitor",
                      "Failed create monitor instance dn for plugin %s, "
                      "instance %s\n",
                      inst->inst_li->li_plugin->plg_name, inst->inst_name);
        return;
    }
    slapi_config_remove_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, dn,
                                 LDAP_SCOPE_BASE, "(objectclass=*)", dbmdb_monitor_instance_search);
    slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn,
                                 LDAP_SCOPE_SUBTREE, "(objectclass=*)", dbmdb_deny_config);
    slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
                                 LDAP_SCOPE_BASE, "(objectclass=*)", dbmdb_deny_config);
    slapi_ch_free_string(&dn);
}

/* Utility function used in creating config entries.  Using the
 * config_info, this function gets info and formats in the correct
 * way.
 * buf is char[BUFSIZ]
 */
void
dbmdb_ctx_t_get(void *arg, config_info *config, char *buf)
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
dbmdb_ctx_t_search_entry_callback(Slapi_PBlock *pb __attribute__((unused)),
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

    for (config = dbmdb_ctx_t_param; config->config_name != NULL; config++) {
        /* Go through the dbmdb_ctx_t table and fill in the entry. */

        if (!(config->config_flags & (CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_PREVIOUSLY_SET))) {
            /* This config option shouldn't be shown */
            continue;
        }

        dbmdb_ctx_t_get((void *)li, config, buf);

        val.bv_val = buf;
        val.bv_len = strlen(buf);
        slapi_entry_attr_replace(e, config->config_name, vals);
    }

    PR_Unlock(li->li_config_mutex);

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}


int
dbmdb_ctx_t_ignored_attr(char *attr_name)
{
    /* These are the names of attributes that are in the
     * config entries but are not config attributes. */
    if (!strcasecmp("objectclass", attr_name) ||
        !strcasecmp("cn", attr_name) ||
        !strcasecmp("nsuniqueid", attr_name) ||
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
dbmdb_ctx_t_set(void *arg, char *attr_name, config_info *config_array, struct berval *bval, char *err_buf, int phase, int apply_mod, int mod_op)
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
        slapi_log_err(SLAPI_LOG_CONFIG, "dbmdb_ctx_t_set", "Unknown config attribute %s\n", attr_name);
        slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Unknown config attribute %s\n", attr_name);
        return LDAP_SUCCESS; /* Ignore unknown attributes */
    }

    /* Some config attrs can't be changed while the server is startcfg. */
    if (phase == CONFIG_PHASE_RUNNING &&
        !(config->config_flags & CONFIG_FLAG_ALLOW_RUNNING_CHANGE)) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_set", "%s can't be modified while the server is startcfg.\n", attr_name);
        slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "%s can't be modified while the server is startcfg.\n", attr_name);
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
        dbmdb_ctx_t_get(arg, config, buf);
        if (PL_strncmp(buf, bval->bv_val, bval->bv_len)) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE,
                                  "value [%s] for attribute %s does not match existing value [%s].\n", bval->bv_val, attr_name, buf);
            return LDAP_NO_SUCH_ATTRIBUTE;
        }
    }

    switch (config->config_type) {
    case CONFIG_TYPE_INT:
        if (use_default) {
            str_val = config->config_default_value;
        } else {
            str_val = bval->bv_val;
        }
        /* get the value as a 64 bit value */
        llval = db_atoi(str_val, &err);
        /* check for parsing error (e.g. not a number) */
        if (err) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is not a number\n", str_val, attr_name);
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_set", "Value %s for attr %s is not a number\n", str_val, attr_name);
            return LDAP_UNWILLING_TO_PERFORM;
            /* check for overflow */
        } else if (LL_CMP(llval, >, llmaxint)) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is greater than the maximum %d\n",
                                  str_val, attr_name, maxint);
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_set", "Value %s for attr %s is greater than the maximum %d\n",
                          str_val, attr_name, maxint);
            return LDAP_UNWILLING_TO_PERFORM;
            /* check for underflow */
        } else if (LL_CMP(llval, <, llminint)) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is less than the minimum %d\n",
                                  str_val, attr_name, minint);
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_set", "Value %s for attr %s is less than the minimum %d\n",
                          str_val, attr_name, minint);
            return LDAP_UNWILLING_TO_PERFORM;
        }
        /* convert 64 bit value to 32 bit value */
        LL_L2I(int_val, llval);
        retval = config->config_set_fn(arg, (void *)((uintptr_t)int_val), err_buf, phase, apply_mod);
        break;
    case CONFIG_TYPE_INT_OCTAL:
        if (use_default) {
            int_val = (int)strtol(config->config_default_value, NULL, 8);
        } else {
            int_val = (int)strtol((char *)bval->bv_val, NULL, 8);
        }
        retval = config->config_set_fn(arg, (void *)((uintptr_t)int_val), err_buf, phase, apply_mod);
        break;
    case CONFIG_TYPE_LONG:
        if (use_default) {
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
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_set", "Value %s for attr %s is not a number\n",
                          str_val, attr_name);
            return LDAP_UNWILLING_TO_PERFORM;
            /* check for overflow */
        } else if (LL_CMP(llval, >, llmaxint)) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is greater than the maximum %d\n",
                                  str_val, attr_name, maxint);
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_set", "Value %s for attr %s is greater than the maximum %d\n",
                          str_val, attr_name, maxint);
            return LDAP_UNWILLING_TO_PERFORM;
            /* check for underflow */
        } else if (LL_CMP(llval, <, llminint)) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is less than the minimum %d\n",
                                  str_val, attr_name, minint);
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_set", "Value %s for attr %s is less than the minimum %d\n",
                          str_val, attr_name, minint);
            return LDAP_UNWILLING_TO_PERFORM;
        }
        /* convert 64 bit value to 32 bit value */
        LL_L2I(long_val, llval);
        retval = config->config_set_fn(arg, (void *)long_val, err_buf, phase, apply_mod);
        break;
    case CONFIG_TYPE_SIZE_T:
        if (use_default) {
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
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_set", "Value %s for attr %s is not a number\n",
                          str_val, attr_name);
            return LDAP_UNWILLING_TO_PERFORM;
            /* check for overflow */
        } else if (err == ERANGE) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is outside the range of representable values\n",
                                  str_val, attr_name);
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_set", "Value %s for attr %s is outside the range of representable values\n",
                          str_val, attr_name);
            return LDAP_UNWILLING_TO_PERFORM;
        }
        retval = config->config_set_fn(arg, (void *)sz_val, err_buf, phase, apply_mod);
        break;


    case CONFIG_TYPE_UINT64:
        if (use_default) {
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
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_set", "Value %s for attr %s is not a number\n",
                          str_val, attr_name);
            return LDAP_UNWILLING_TO_PERFORM;
        /* check for overflow */
        } else if (err == ERANGE) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is outside the range of representable values\n",
                                  str_val, attr_name);
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ctx_t_set", "Value %s for attr %s is outside the range of representable values\n",
                          str_val, attr_name);
            return LDAP_UNWILLING_TO_PERFORM;
        }
        retval = config->config_set_fn(arg, (void *)sz_val, err_buf, phase, apply_mod);
        break;
    case CONFIG_TYPE_STRING:
        if (use_default) {
            retval = config->config_set_fn(arg, config->config_default_value, err_buf, phase, apply_mod);
        } else {
            retval = config->config_set_fn(arg, bval->bv_val, err_buf, phase, apply_mod);
        }
        break;
    case CONFIG_TYPE_ONOFF:
        if (use_default) {
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
dbmdb_split_dbmdb_ctx_t_entry(struct ldbminfo *li, Slapi_Entry *ldbm_conf_e,Slapi_Entry *dbmdb_conf_e, config_info *config_array, Slapi_Mods *smods)
{
    Slapi_Attr *attr = NULL;

    for (slapi_entry_first_attr(ldbm_conf_e, &attr); attr; slapi_entry_next_attr(ldbm_conf_e, attr, &attr)) {
        char *attr_name = NULL;
        Slapi_Value *sval = NULL;

        slapi_attr_get_type(attr, &attr_name);

        /* There are some attributes that we don't care about, like objectclass. */
        if (dbmdb_ctx_t_ignored_attr(attr_name)) {
            continue;
        }
        if (NULL == config_info_get(config_array, attr_name)) {
            /* this attr is not mdb specific */
            continue;
        }
        slapi_attr_first_value(attr, &sval);
        slapi_entry_add_string(dbmdb_conf_e, attr_name, slapi_value_get_string(sval));
        slapi_mods_add(smods, LDAP_MOD_DELETE, attr_name, 0, NULL);
    }
}

static int
dbmdb_parse_dbmdb_ctx_t_entry(struct ldbminfo *li, Slapi_Entry *e, config_info *config_array)
{
    Slapi_Attr *attr = NULL;

    for (slapi_entry_first_attr(e, &attr); attr; slapi_entry_next_attr(e, attr, &attr)) {
        char *attr_name = NULL;
        Slapi_Value *sval = NULL;
        struct berval *bval;
        char err_buf[SLAPI_DSE_RETURNTEXT_SIZE];

        slapi_attr_get_type(attr, &attr_name);

        /* There are some attributes that we don't care about, like objectclass. */
        if (dbmdb_ctx_t_ignored_attr(attr_name)) {
            continue;
        }
        slapi_attr_first_value(attr, &sval);
        bval = (struct berval *)slapi_value_get_berval(sval);

        if (dbmdb_ctx_t_set(li, attr_name, config_array, bval, err_buf, CONFIG_PHASE_STARTUP, 1 /* apply */, LDAP_MOD_REPLACE) != LDAP_SUCCESS) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_parse_dbmdb_ctx_t_entry", "Error with config attribute %s : %s\n", attr_name, err_buf);
            return 1;
        }
    }
    return 0;
}

/* helper for deleting mods (we do not want to be applied) from the mods array */
static void
dbmdb_mod_free(LDAPMod *mod)
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
dbmdb_ctx_t_modify_entry_callback(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *e, int *returncode, char *returntext, void *arg)
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
            if (dbmdb_ctx_t_ignored_attr(attr_name)) {
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
            rc = dbmdb_ctx_t_set((void *)li, attr_name, dbmdb_ctx_t_param,
                                 (mods[i]->mod_bvalues == NULL) ? NULL
                                                                : mods[i]->mod_bvalues[0],
                                 returntext,
                                 ((li->li_flags & LI_FORCE_MOD_CONFIG) ? CONFIG_PHASE_INTERNAL : CONFIG_PHASE_RUNNING),
                                 apply_mod, mods[i]->mod_op);
            if (apply_mod) {
                dbmdb_mod_free(mods[i]);
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
dbmdb_ctx_t_internal_set(struct ldbminfo *li, char *attrname, char *value)
{
    char err_buf[SLAPI_DSE_RETURNTEXT_SIZE];
    struct berval bval;

    bval.bv_val = value;
    bval.bv_len = strlen(value);

    if (dbmdb_ctx_t_set((void *)li, attrname, dbmdb_ctx_t_param, &bval,
                        err_buf, CONFIG_PHASE_INTERNAL, 1 /* apply */,
                        LDAP_MOD_REPLACE) != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_ctx_t_internal_set", "Error setting instance config attr %s to %s: %s\n",
                      attrname, value, err_buf);
        exit(1);
    }
    return LDAP_SUCCESS;
}

void
dbmdb_public_config_get(struct ldbminfo *li, char *attrname, char *value)
{
    config_info *config = config_info_get(dbmdb_ctx_t_param, attrname);
    if (NULL == config) {
        slapi_log_err(SLAPI_LOG_CONFIG, "dbmdb_public_config_get", "Unknown config attribute %s\n", attrname);
        value[0] = '\0';
    } else {
        dbmdb_ctx_t_get(li, config, value);
    }
}
int
dbmdb_public_config_set(struct ldbminfo *li, char *attrname, int apply_mod, int mod_op, int phase, char *value)
{
    char err_buf[SLAPI_DSE_RETURNTEXT_SIZE];
    int rc = LDAP_SUCCESS;

    if (!value && SLAPI_IS_MOD_ADD(mod_op)) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_public_internal_set", "Error: no value for config attr: %s\n",
                      attrname);
        return -1;
    }

    if (value) {
        struct berval bval;
        bval.bv_val = value;
        bval.bv_len = strlen(value);

        rc = dbmdb_ctx_t_set((void *)li, attrname, dbmdb_ctx_t_param, &bval,
                            err_buf, phase, apply_mod, mod_op);
    } else {
        rc = dbmdb_ctx_t_set((void *)li, attrname, dbmdb_ctx_t_param, NULL,
                            err_buf, phase, apply_mod, mod_op);
    }
    if (rc != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_public_config_set", "Error setting instance config attr %s to %s: %s\n",
                      attrname, value, err_buf);
    }
    return rc;
}


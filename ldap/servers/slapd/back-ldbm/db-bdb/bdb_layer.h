/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/


#include "../back-ldbm.h"
#include "../dblayer.h"

#define BDB_CONFIG(li) ((bdb_config *)(li)->li_dblayer_config)

typedef struct bdb_db_env
{
    DB_ENV *bdb_DB_ENV;
    Slapi_RWLock *bdb_env_lock;
    int bdb_openflags;
    int bdb_priv_flags;
    pthread_mutex_t bdb_thread_count_lock; /* lock for thread_count_cv */
    pthread_cond_t bdb_thread_count_cv;    /* condition variable for housekeeping thread shutdown */
    PRInt32 bdb_thread_count;              /* Tells us how many threads are running,
                                            * used to figure out when they're all stopped */
} bdb_db_env;

/* structure which holds our stuff */
typedef struct bdb_config
{
    char *bdb_home_directory;
    char *bdb_log_directory;
    char *bdb_dbhome_directory;  /* default path for relative inst paths */
    char **bdb_data_directories; /* passed to set_data_dir
                                      * including bdb_dbhome_directory */
    char **bdb_db_config;
    int bdb_ncache;
    int bdb_previous_ncache;
    int bdb_tx_max;
    uint64_t bdb_cachesize;
    uint64_t bdb_previous_cachesize; /* Cache size when we last shut down--
                                        * used to determine if we delete
                                        * the mpool */
    int bdb_recovery_required;
    int bdb_txn_wait; /* Default is "off" (DB_TXN_NOWAIT) but for
                                     * support purpose it could be helpful to set
                                     * "on" so that backend hang on deadlock */
    int bdb_enable_transactions;
    int bdb_durable_transactions;
    int bdb_checkpoint_interval;
    int bdb_circular_logging;
    uint32_t bdb_page_size;       /* db page size if configured,
                                     * otherwise default to DBLAYER_PAGESIZE */
    uint32_t bdb_index_page_size; /* db index page size if configured,
                                     * otherwise default to
                                     * DBLAYER_INDEX_PAGESIZE */
    uint64_t bdb_logfile_size;    /* How large can one logfile be ? */
    uint64_t bdb_logbuf_size;     /* how large log buffer can be */
    int bdb_trickle_percentage;
    int bdb_cache_config; /* Special cache configurations
                                     * e.g. force file-based mpool */
    int bdb_lib_version;
    int bdb_spin_count;         /* DB Mutex spin count, 0 == use default */
    int bdb_named_regions;      /* Should the regions be named sections,
                                     * or backed by files ? */
    int bdb_private_mem;        /* private memory will be used for
                                     * allocation of regions and mutexes */
    int bdb_private_import_mem; /* private memory will be used for
                                     * allocation of regions and mutexes for
                                     * import */
    long bdb_shm_key;           /* base segment ID for named regions */
    int bdb_debug;                /* Will libdb emit debugging info into our log ? */
    int bdb_debug_verbose;              /* Get libdb to exhale debugging info */
    int bdb_debug_checkpointing;     /* Enable debugging messages from checkpointing */
    perfctrs_private *perf_private; /* Private data for performance counters code */
    int bdb_stop_threads;       /* Used to signal to threads that they should stop ASAP */
    int bdb_lockdown;           /* use DB_LOCKDOWN */
#define BDB_LOCK_NB_MIN 10000
    int bdb_lock_config;
    int bdb_previous_lock_config;  /* Max lock count when we last shut down--
                                      * used to determine if we delete the mpool */
    u_int32_t bdb_deadlock_policy; /* i.e. the atype to DB_ENV->lock_detect in deadlock_threadmain */
    int32_t bdb_compactdb_interval; /* interval to execute compact id2entry dbs */
    char *bdb_compactdb_time;       /* time of day to execute compact id2entry dbs */
} bdb_config;

int bdb_init(struct ldbminfo *li, config_info *config_array);

int bdb_close(struct ldbminfo *li, int flags);
int bdb_start(struct ldbminfo *li, int flags);
int bdb_instance_start(backend *be, int flags);
int bdb_backup(struct ldbminfo *li, char *dest_dir, Slapi_Task *task);
int bdb_verify(Slapi_PBlock *pb);
int bdb_db2ldif(Slapi_PBlock *pb);
int bdb_db2index(Slapi_PBlock *pb);
int bdb_ldif2db(Slapi_PBlock *pb);
int bdb_db_size(Slapi_PBlock *pb);
int bdb_upgradedb(Slapi_PBlock *pb);
int bdb_upgradednformat(Slapi_PBlock *pb);
int bdb_upgradeddformat(Slapi_PBlock *pb);
int32_t bdb_compact(struct ldbminfo *li, PRBool just_changelog);
int bdb_restore(struct ldbminfo *li, char *src_dir, Slapi_Task *task);
int bdb_cleanup(struct ldbminfo *li);
int bdb_txn_begin(struct ldbminfo *li, back_txnid parent_txn, back_txn *txn, PRBool use_lock);
int bdb_txn_commit(struct ldbminfo *li, back_txn *txn, PRBool use_lock);
int bdb_txn_abort(struct ldbminfo *li, back_txn *txn, PRBool use_lock);
int bdb_get_db(backend *be, char *indexname, int open_flag, struct attrinfo *ai, DB **ppDB);
int bdb_rm_db_file(backend *be, struct attrinfo *a, PRBool use_lock, int no_force_chkpt);
int bdb_delete_db(struct ldbminfo *li);
int bdb_import_main(void *arg);
int bdb_get_info(Slapi_Backend *be, int cmd, void **info);
int bdb_set_info(Slapi_Backend *be, int cmd, void **info);
int bdb_back_ctrl(Slapi_Backend *be, int cmd, void *info);
int bdb_config_load_dse_info(struct ldbminfo *li);
int bdb_config_internal_set(struct ldbminfo *li, char *attrname, char *value);
void bdb_public_config_get(struct ldbminfo *li, char *attrname, char *value);
int bdb_public_config_set(struct ldbminfo *li, char *attrname, int apply_mod, int mod_op, int phase, char *value);

/* instance functions */
int bdb_instance_cleanup(struct ldbm_instance *inst);
int bdb_instance_config_set(ldbm_instance *inst, char *attrname, int mod_apply, int mod_op, int phase, struct berval *value);
int bdb_instance_create(struct ldbm_instance *inst);
int bdb_instance_search_callback(Slapi_Entry *e, int *returncode, char *returntext, ldbm_instance *inst);

/* function for autotuning */
int bdb_start_autotune(struct ldbminfo *li);

/* helper functions */
int bdb_get_aux_id2entry(backend *be, DB **ppDB, DB_ENV **ppEnv, char **path);
int bdb_get_aux_id2entry_ext(backend *be, DB **ppDB, DB_ENV **ppEnv, char **path, int flags);
int bdb_release_aux_id2entry(backend *be, DB *pDB, DB_ENV *pEnv);
char *bdb_get_home_dir(struct ldbminfo *li, int *dbhome);
char *bdb_get_db_dir(struct ldbminfo *li);
int bdb_copy_directory(struct ldbminfo *li, Slapi_Task *task, char *src_dir, char *dest_dir, int restore, int *cnt, int indexonly, int is_changelog);
int bdb_remove_env(struct ldbminfo *li);
int bdb_bt_compare(DB *db, const DBT *dbt1, const DBT *dbt2);
int bdb_open_huge_file(const char *path, int oflag, int mode);
int bdb_check_and_set_import_cache(struct ldbminfo *li);
int bdb_close_file(DB **db);
int bdb_post_close(struct ldbminfo *li, int dbmode);
int bdb_config_set(void *arg, char *attr_name, config_info *config_array, struct berval *bval, char *err_buf, int phase, int apply_mod, int mod_op);
void bdb_config_get(void *arg, config_info *config, char *buf);
int add_op_attrs(Slapi_PBlock *pb, struct ldbminfo *li, struct backentry *ep, int *status);
int bdb_back_ldif2db(Slapi_PBlock *pb);
void bdb_set_recovery_required(struct ldbminfo *li);
void *bdb_config_db_logdirectory_get_ext(void *arg);
int bdb_db_remove(bdb_db_env *env, char const path[], char const dbName[]);
int bdb_memp_stat(struct ldbminfo *li, DB_MPOOL_STAT **gsp, DB_MPOOL_FSTAT ***fsp);
int bdb_memp_stat_instance(ldbm_instance *inst, DB_MPOOL_STAT **gsp, DB_MPOOL_FSTAT ***fsp);
void bdb_set_env_debugging(DB_ENV *pEnv, bdb_config *conf);
void bdb_back_free_incl_excl(char **include, char **exclude);
int bdb_back_ok_to_dump(const char *dn, char **include, char **exclude);
int bdb_back_fetch_incl_excl(Slapi_PBlock *pb, char ***include, char ***exclude);
PRUint64 bdb_get_id2entry_size(ldbm_instance *inst);

/* bdb version functions */
int bdb_version_write(struct ldbminfo *li, const char *directory, const char *dataversion, PRUint32 flags);
int bdb_version_read(struct ldbminfo *li, const char *directory, char **ldbmversion, char **dataversion);
int bdb_version_exists(struct ldbminfo *li, const char *directory);

/* config functions */
int bdb_instance_delete_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst);
int bdb_instance_post_delete_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst);
int bdb_instance_add_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst);
int bdb_instance_postadd_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst);
void bdb_config_setup_default(struct ldbminfo *li);

/* monitor functions */
int bdb_monitor_instance_search(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int bdb_monitor_search(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int bdb_dbmonitor_search(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int bdb_instance_register_monitor(ldbm_instance *inst);
void bdb_instance_unregister_monitor(ldbm_instance *inst);

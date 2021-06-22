/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/


#include "../back-ldbm.h"
#include "../dblayer.h"
#include "../import.h"
#include <lmdb.h>

#define START_TXN(ptxn, parent_txn, txnflags)        dbmdb_start_txn(__FUNCTION__, parent_txn, txnflags, ptxn)
#define END_TXN(ptxn, rc)                dbmdb_end_txn(__FUNCTION__, rc, ptxn)
#define TXN(txn)                         dbmdb_txn(txn)
#define DB(dbidb)                          ((dbmdb_dbi_t*)(dbidb))->dbi

#define MDB_CONFIG(li) ((dbmdb_ctx_t *)(li)->li_dblayer_config)

#define DBMDB_LIBVERSION(v1,v2,v3) ((v3)+1000*(v2)+1000000*(v1))
#define DBMDB_CURRENT_DATAVERSION   0
/* Data Versioning should be handled like that:
 * #define DBMDB_DATAVERSION_FEATURE_1 0x01
 * #define DBMDB_DATAVERSION_FEATURE_2 0x02
 * #define DBMDB_DATAVERSION_FEATURE_3 0x04
 * ...
 * #define DBMDB_CURRENT_DATAVERSION   ( DBMDB_DATAVERSION_FEATURE_1 | DBMDB_DATAVERSION_FEATURE_2 | DBMDB_DATAVERSION_FEATURE_3 | ... )
 *
 * Then we can compare what must be done to upgrade each feature (or a set of feature)
 *     globally in dbmdb_global_upgrade  (called when opening the db env)
 *     at be instance level in dbmdb_ldbm_upgrade (called when opening id2entry instance)
 */


/* mdb config parameters */

#define CONFIG_MDB_MAX_SIZE       "nsslapd-mdb-max-size"
#define CONFIG_MDB_MAX_READERS    "nsslapd-mdb-max-readers"
#define CONFIG_MDB_MAX_DBS        "nsslapd-mdb-max-dbs"

#define DBMDB_DB_MINSIZE             ( 4LL * MEGABYTE )
#define DBMDB_DISK_RESERVE(disksize) ((disksize)*2ULL/1000ULL)
#define DBMDB_READERS_MARGIN         10
#define DBMDB_READERS_DEFAULT        50
#define DBMDB_DBS_MARGIN             10
#define DBMDB_DBS_DEFAULT            128

/* txn flags */
#define TXNFL_DBI                    1
#define TXNFL_RDONLY                 2

/* dbmdb_open_dbname flags  Includes mdb_dbi_open flags plus the following */
#define MDB_OPEN_DIRTY_DBI           0x10000000     /* Allow to open dirty flags */
#define MDB_MARK_DIRTY_DBI           0x20000000     /* create/open a dbi in dirty mode (import/reindex case) */
#define MDB_TRUNCATE_DBI             0x40000000     /* create/open a dbi and insure it is empty */

/* Files and database names */
#define DSE_INSTANCE        "dse_instance.ldif"		/* dse file in backup */
#define DSE_INDEX           "dse_index.ldif"		/* dse file in backup */
#define DBMAPFILE           "data.mdb"
#define INFOFILE            "INFO.mdb"
#define DBNAMES             "__DBNAMES"
#define CHANGELOG_PATTERN   "changelog"   /* pattern in changelog dbi name */


/* config parameters */
typedef struct
{
    int dseloaded;
    int durable_transactions;
    int max_readers;
    int max_dbs;
    uint64_t max_size;
} dbmdb_cfg_t;

/* config parameters limits */
typedef struct
{
    int min_readers;
    int min_dbs;
    uint64_t min_size;
    uint64_t max_size;
    int disk_reserve;
} dbmdb_limits_t;

/* other information */
typedef struct
{
    int key_maxsize;
    int pagesize;
    char *strversion;
    int libversion;
    int dataversion;
} dbmdb_info_t;

#define DBIST_CLEAN     0
#define DBIST_DIRTY     1         /* Import / Reindex in progress */

typedef struct
{
    int flags;                    /* dbi open flag */
    int state;                    /* DBIST_ flags */
    int dataversion;              /* DBVERSION_ flags */
} dbistate_t;                     /* Data stored in __DBNAMES database */

/*
 * in dbmdb_ctx_t, dbilist array contains startcfg.dbmdb_max_dbs slots
 *  nbdbis first slots are used and sorted according to the dbname
 */

/* database instance context (on which dbi_db_t is mapped) */
typedef struct
{
    MDB_env *env;                 /* Database environment */
    const char *dbname;           /* database name (for example userroot/entryid.db) */
    dbistate_t state;             /* state (also stored in __DBNAMES database) */
    MDB_dbi dbi;                  /* The handle */
} dbmdb_dbi_t;



/* structure which holds our stuff */
typedef struct dbmdb_ctx_t
{
    dbmdb_cfg_t dsecfg;            /* Config parameters in dse.ldif */
    dbmdb_cfg_t startcfg;          /* Config parameters at startup */
    dbmdb_limits_t limits;         /* Limits */
    dbmdb_info_t info;             /* Other information */
    char home[MAXPATHLEN];         /* Home directory */
    pthread_mutex_t dbis_lock;     /* protects dbis access */
    dbmdb_dbi_t *dbis;             /* sorted by name instances array with startcfg.dbmdb_max_dbs slots */
    int nbdbis;                    /* number of used slots in dbilist */
    MDB_dbi dbinames_dbi;          /* __DBNAMES database handler */
    MDB_env *env;
    int readonly;                  /* Tells that env is open in readonly mode */
    pthread_rwlock_t dbmdb_env_lock; /* txn global lock */
    perfctrs_private *perf_private;  /* Performance counter data */
} dbmdb_ctx_t;

/*
 * structure containing all that is needed to handle an db instance, a txn or a cursor
 * Note: dbi_db_t is mapped on this struct
 */
typedef struct dbmdb_cursor_t
{
    dbmdb_dbi_t dbi;
    dbi_txn_t *txn;
    MDB_cursor *cur;
} dbmdb_cursor_t;

/* Writer thread dbi slot ID */
typedef struct dbmdb_wid_t
{
    int id;
} dbmdb_wid_t;

/* Writer thread actions */
typedef enum {
    IMPORT_WRITE_ACTION_RMDIR = 1,
    IMPORT_WRITE_ACTION_OPEN,           /* create the db instance or reset it in dirty mode */
    IMPORT_WRITE_ACTION_ADD_INDEX,
    IMPORT_WRITE_ACTION_DEL_INDEX,
    IMPORT_WRITE_ACTION_ADD_ENTRYRDN,
    IMPORT_WRITE_ACTION_DEL_ENTRYRDN,
    IMPORT_WRITE_ACTION_ADD,
    IMPORT_WRITE_ACTION_CLOSE,
} dbmdb_waction_t;

typedef enum {
    WCTX_ENTRYRDN,
    WCTX_ENTRYDN,
    WCTX_ENTRYID,
    WCTX_PARENTID,
    WCTX_GENERIC    /* Must be last one */
} dbmdb_wctx_id_t;  /* Allow to identify predefined writer context for some well known index */

#include "mdb_debug.h"

extern Slapi_ComponentId *dbmdb_componentid;

int mdb_init(struct ldbminfo *li, config_info *config_array);

int dbmdb_close(struct ldbminfo *li, int flags);
int dbmdb_start(struct ldbminfo *li, int flags);
int dbmdb_instance_start(backend *be, int flags);
int dbmdb_backup(struct ldbminfo *li, char *dest_dir, Slapi_Task *task);
int dbmdb_verify(Slapi_PBlock *pb);
int dbmdb_db2ldif(Slapi_PBlock *pb);
int dbmdb_db2index(Slapi_PBlock *pb);
int dbmdb_ldif2db(Slapi_PBlock *pb);
int dbmdb_db_size(Slapi_PBlock *pb);
int dbmdb_upgradedb(Slapi_PBlock *pb);
int dbmdb_upgradednformat(Slapi_PBlock *pb);
int dbmdb_upgradeddformat(Slapi_PBlock *pb);
int dbmdb_restore(struct ldbminfo *li, char *src_dir, Slapi_Task *task);
int dbmdb_cleanup(struct ldbminfo *li);
int dbmdb_txn_begin(struct ldbminfo *li, back_txnid parent_txn, back_txn *txn, PRBool use_lock);
int dbmdb_txn_commit(struct ldbminfo *li, back_txn *txn, PRBool use_lock);
int dbmdb_txn_abort(struct ldbminfo *li, back_txn *txn, PRBool use_lock);
int dbmdb_get_db(backend *be, char *indexname, int open_flag, struct attrinfo *ai, dbi_db_t **ppDB);
int dbmdb_rm_db_file(backend *be, struct attrinfo *a, PRBool use_lock, int no_force_chkpt);
int dbmdb_delete_db(struct ldbminfo *li);
int dbmdb_public_dbmdb_import_main(void *arg);
int dbmdb_get_info(Slapi_Backend *be, int cmd, void **info);
int dbmdb_set_info(Slapi_Backend *be, int cmd, void **info);
int dbmdb_back_ctrl(Slapi_Backend *be, int cmd, void *info);
int dbmdb_ctx_t_load_dse_info(struct ldbminfo *li);
int dbmdb_ctx_t_internal_set(struct ldbminfo *li, char *attrname, char *value);
void dbmdb_public_config_get(struct ldbminfo *li, char *attrname, char *value);
int dbmdb_public_config_set(struct ldbminfo *li, char *attrname, int apply_mod, int mod_op, int phase, char *value);

/* dbimpl callbacks */
dblayer_get_db_filename_fn_t dbmdb_public_get_db_filename;
dblayer_bulk_free_fn_t dbmdb_public_bulk_free;
dblayer_bulk_nextdata_fn_t dbmdb_public_bulk_nextdata;
dblayer_bulk_nextrecord_fn_t dbmdb_public_bulk_nextrecord;
dblayer_bulk_init_fn_t dbmdb_public_bulk_init;
dblayer_bulk_start_fn_t dbmdb_public_bulk_start;
dblayer_cursor_bulkop_fn_t dbmdb_public_cursor_bulkop;
dblayer_cursor_op_fn_t dbmdb_public_cursor_op;
dblayer_db_op_fn_t dbmdb_public_db_op;
dblayer_new_cursor_fn_t dbmdb_public_new_cursor;
dblayer_value_free_fn_t dbmdb_public_value_free;
dblayer_value_init_fn_t dbmdb_public_value_init;
dblayer_set_dup_cmp_fn_t dbmdb_public_set_dup_cmp_fn;
dblayer_dbi_txn_begin_fn_t dbmdb_dbi_txn_begin;
dblayer_dbi_txn_commit_fn_t dbmdb_dbi_txn_commit;
dblayer_dbi_txn_abort_fn_t dbmdb_dbi_txn_abort;
dblayer_get_entries_count_fn_t dbmdb_get_entries_count;
dblayer_cursor_get_count_fn_t dbmdb_public_cursor_get_count;
dblayer_private_open_fn_t dbmdb_public_private_open;
dblayer_private_close_fn_t dbmdb_public_private_close;

/* instance functions */
int dbmdb_instance_cleanup(struct ldbm_instance *inst);
int dbmdb_instance_config_set(ldbm_instance *inst, char *attrname, int mod_apply, int mod_op, int phase, struct berval *value);
int dbmdb_instance_create(struct ldbm_instance *inst);
int dbmdb_instance_search_callback(Slapi_Entry *e, int *returncode, char *returntext, ldbm_instance *inst);

/* function for autotuning */
int dbmdb_start_autotune(struct ldbminfo *li);

/* helper functions */
char *dbmdb_get_home_dir(struct ldbminfo *li, int *dbhome);
char *dbmdb_get_db_dir(struct ldbminfo *li);
int dbmdb_copy_directory(struct ldbminfo *li, Slapi_Task *task, char *src_dir, char *dest_dir, int restore, int *cnt, int indexonly, int is_changelog);
int dbmdb_remove_env(struct ldbminfo *li);
int dbmdb_bt_compare(dbmdb_dbi_t *db, const MDB_val *dbt1, const MDB_val *dbt2);
int dbmdb_open_huge_file(const char *path, int oflag, int mode);
int dbmdb_check_and_set_import_cache(struct ldbminfo *li);
int dbmdb_close_file(dbmdb_dbi_t **db);
int dbmdb_post_close(struct ldbminfo *li, int dbmode);
int dbmdb_ctx_t_set(void *arg, char *attr_name, config_info *config_array, struct berval *bval, char *err_buf, int phase, int apply_mod, int mod_op);
void dbmdb_ctx_t_get(void *arg, config_info *config, char *buf);
int dbmdb_add_op_attrs(Slapi_PBlock *pb, struct ldbminfo *li, struct backentry *ep, int *status);
int dbmdb_back_ldif2db(Slapi_PBlock *pb);
void dbmdb_set_recovery_required(struct ldbminfo *li);
void *dbmdb_ctx_t_db_logdirectory_get_ext(void *arg);
void dbmdb_set_env_debugging(MDB_env *pEnv, dbmdb_ctx_t *conf);
void dbmdb_back_free_incl_excl(char **include, char **exclude);
int dbmdb_back_ok_to_dump(const char *dn, char **include, char **exclude);
int dbmdb_back_fetch_incl_excl(Slapi_PBlock *pb, char ***include, char ***exclude);

int dbmdb_idl_new_compare_dups(dbmdb_dbi_t * db __attribute__((unused)), const MDB_val *a, const MDB_val *b);

int dbmdb_delete_indices(ldbm_instance *inst);
uint32_t dbmdb_get_optimal_block_size(struct ldbminfo *li);
int dbmdb_copyfile(char *source, char *destination, int overwrite, int mode);
int dbmdb_delete_instance_dir(backend *be);
uint64_t dbmdb_database_size(struct ldbminfo *li);

int dbmdb_set_batch_transactions(void *arg, void *value, char *errorbuf, int phase, int apply);
int dbmdb_set_batch_txn_min_sleep(void *arg, void *value, char *errorbuf, int phase, int apply);
int dbmdb_set_batch_txn_max_sleep(void *arg, void *value, char *errorbuf, int phase, int apply);
void *dbmdb_get_batch_transactions(void *arg);
void *dbmdb_get_batch_txn_min_sleep(void *arg);
void *dbmdb_get_batch_txn_max_sleep(void *arg);
int dbmdb_update_db_ext(ldbm_instance *inst, char *oldext, char *newext);
int dbmdb_restore_file_init(struct ldbminfo *li);
void dbmdb_restore_file_update(struct ldbminfo *li, const char *directory);
int dbmdb_import_file_init(ldbm_instance *inst);
void dbmdb_import_file_update(ldbm_instance *inst);
int dbmdb_import_file_check(ldbm_instance *inst);
int dbmdb_import_subcount_mother_init(import_subcount_stuff *mothers, ID parent_id, size_t count);
int dbmdb_import_subcount_mother_count(import_subcount_stuff *mothers, ID parent_id);
void dbmdb_import_configure_index_buffer_size(size_t size);
size_t dbmdb_import_get_index_buffer_size(void);
int dbmdb_ldbm_back_wire_import(Slapi_PBlock *pb);
void *dbmdb_factory_constructor(void *object, void *parent);
void dbmdb_factory_destructor(void *extension, void *object, void *parent);
int dbmdb_check_db_version(struct ldbminfo *li, int *action);
int dbmdb_check_db_inst_version(ldbm_instance *inst);
int dbmdb_adjust_idl_switch(char *ldbmversion, struct ldbminfo *li);
int dbmdb_ldbm_upgrade(ldbm_instance *inst, int action);
int dbmdb_lookup_dbversion(char *dbversion, int flag);
int dbmdb_dse_conf_backup(struct ldbminfo *li, char *destination_directory);
int dbmdb_dse_conf_verify(struct ldbminfo *li, char *src_dir);
int dbmdb_import_file_check_fn_t(ldbm_instance *inst);
dbi_error_t dbmdb_map_error(const char *funcname, int err);
dbi_dbslist_t *dbmdb_list_dbs(const char *dbhome);
int dbmdb_public_in_import(ldbm_instance *inst);


/* dbimpl helpers */
backend *dbmdb_be(void);
void dbmdb_dbival2dbt(dbi_val_t *dbi, MDB_val *dbt, PRBool isresponse);
int dbmdb_dbt2dbival(MDB_val *dbt, dbi_val_t *dbi, PRBool isresponse, int rc);
int dbmdb_uses_locking(MDB_env *db_env);
int dbmdb_uses_transactions(MDB_env *db_env);
int dbmdb_uses_mpool(MDB_env *db_env);
int dbmdb_uses_logging(MDB_env *db_env);

/* config functions */
int dbmdb_instance_delete_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst);
int dbmdb_instance_post_delete_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst);
int dbmdb_instance_add_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst);
int dbmdb_instance_postadd_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst);
void dbmdb_ctx_t_setup_default(struct ldbminfo *li);

/* monitor functions */
int dbmdb_monitor_instance_search(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int dbmdb_monitor_search(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int dbmdb_dbmonitor_search(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int dbmdb_instance_register_monitor(ldbm_instance *inst);
void dbmdb_instance_unregister_monitor(ldbm_instance *inst);

/*
 * mdb_perfctrs.c
 */
void dbmdb_perfctrs_wait(size_t milliseconds, perfctrs_private *priv, MDB_env *db_env);
void dbmdb_perfctrs_init(struct ldbminfo *li, perfctrs_private **priv);
void dbmdb_perfctrs_terminate(perfctrs_private **priv, MDB_env *db_env);
void dbmdb_perfctrs_as_entry(Slapi_Entry *e, dbmdb_ctx_t *ctx);

/* mdb_import.c */
int dbmdb_import_fifo_validate_capacity_or_expand(ImportJob *job, size_t entrysize);
FifoItem *dbmdb_import_fifo_fetch(ImportJob *job, ID id, int worker);
void dbmdb_import_free_job(ImportJob *job);
int dbmdb_import_entry_belongs_here(Slapi_Entry *e, backend *be);
int dbmdb_import_make_merge_filenames(char *directory, char *indexname, int pass, char **oldname, char **newname);
void dbmdb_import_main(void *arg);

/* mdb_import-merge.c */
int dbmdb_import_mega_merge(ImportJob *job);

/* mdb_import-threads.c */
void dbmdb_import_producer(void *param);
void dbmdb_index_producer(void *param);
void dbmdb_upgradedn_producer(void *param);
void dbmdb_import_foreman(void *param);
void dbmdb_import_worker(void *param);
void dbmdb_import_writer(void *param);
int dbmdb_import_writer_create_dbi(ImportWorkerInfo *info, dbmdb_wctx_id_t wctx_id, const char *filename, PRBool delayed);
int dbmdb_import_sync_write(ImportJob *job, long wqslot, dbmdb_waction_t action, MDB_val *key, MDB_val *data);
int dbmdb_import_write_push(ImportJob *job, long wqslot, dbmdb_waction_t action, MDB_val *key, MDB_val *data);
back_txn *dbmdb_get_wctx(ImportJob *job, ImportWorkerInfo *info, dbmdb_wctx_id_t wctx_id);
void dbmdb_writer_init(ImportJob *job);
void dbmdb_writer_cleanup(ImportJob *job);
void dbmdb_writer_wakeup(ImportJob *job);
double dbmdb_writer_get_progress(ImportJob *job);

/* mdb_misc.c */
int dbmdb_count_config_entries(char *filter, int *nbentries);

/* mdb_instance.c */
int dbmdb_open_dbi_from_filename(dbmdb_dbi_t *dbi, backend *be, const char *filename, struct attrinfo *ai, int flags);
int dbmdb_open_cursor(dbmdb_cursor_t *dbicur, dbmdb_ctx_t *ctx, dbmdb_dbi_t *dbi, int flags);
int dbmdb_close_cursor(dbmdb_cursor_t *dbicur, int rc);
int dbmdb_make_env(dbmdb_ctx_t *ctx, int readOnly, mdb_mode_t mode);
void dbmdb_ctx_close(dbmdb_ctx_t *ctx);
int dbmdb_dbitxn_begin(dbmdb_cursor_t *dbicur, const char *funcname, MDB_txn *parent, int readonly);
int dbmdb_dbitxn_end(dbmdb_cursor_t *dbicur, const char *funcname, int return_code);
void dbmdb_mdbdbi2dbi_db(const dbmdb_dbi_t *dbi, dbi_db_t **ppDB);
dbi_dbslist_t *dbmdb_list_dbs(const char *dbhome);
void dbmdb_envflags2str(int flags, char *str, int maxlen);
int dbmdb_dbi_remove(dbmdb_ctx_t *conf, dbi_db_t **db);
int dbmdb_dbi_rmdir(dbmdb_ctx_t *conf, const char *dirname);
int dbmdb_clear_dirty_flags(dbmdb_ctx_t *conf, const char *dirname);

/* mdb_txn.c */
int dbmdb_start_txn(const char *funcname, dbi_txn_t *parent_txn, int flags, dbi_txn_t **txn);
int dbmdb_end_txn(const char *funcname, int rc, dbi_txn_t **txn);
void init_mdbtxn(dbmdb_ctx_t *ctx);
MDB_txn *dbmdb_txn(dbi_txn_t *txn);
int dbmdb_is_read_only_txn_thread(void);


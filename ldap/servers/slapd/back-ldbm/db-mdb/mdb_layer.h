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

#define MDB_CONFIG(li) ((dbmdb_config *)(li)->li_dblayer_config)

#define DBMDB_DATAVERSION	1
#define DBMDB_LIBVERSION(v1,v2,v3) ((v3)+1000*(v2)+1000000*(v1))

/* mdb config parameters */

#define CONFIG_MDB_MAX_SIZE       "nsslapd-mdb-max-size"
#define CONFIG_MDB_MAX_READERS    "nsslapd-mdb-max-readers"
#define CONFIG_MDB_MAX_DBS        "nsslapd-mdb-max-dbs"

#define DBMDB_DB_MINSIZE			 ( 4LL * MEGABYTE )
#define DBMDB_DISK_RESERVE(disksize) ((disksize)*2ULL/1000ULL)
#define DBMDB_READERS_MARGIN         10
#define DBMDB_READERS_DEFAULT        50
#define DBMDB_DBS_MARGIN             10
#define DBMDB_DBS_DEFAULT            128





typedef struct dbmdb_db_env
{
#ifdef TODO
    MDB_env *dbmdb_MDB_env;
    Slapi_RWLock *dbmdb_env_lock;
    int dbmdb_openflags;
    int dbmdb_priv_flags;
    pthread_mutex_t dbmdb_thread_count_lock; /* lock for thread_count_cv */
    pthread_cond_t dbmdb_thread_count_cv;    /* condition variable for housekeeping thread shutdown */
    PRInt32 dbmdb_thread_count;              /* Tells us how many threads are running,
                                            * used to figure out when they're all stopped */
#endif /* TODO */
} dbmdb_db_env;
/* Mdb config parameters */
typedef struct
{
    char *dbmdb_dbhome_directory;
    int dbmdb_durable_transactions;
	uint64_t dbmdb_max_size;
	int dbmdb_max_readers;
	int dbmdb_max_dbs;
} dbmdb_baseconfig_t;

typedef struct
{
	int key_maxsize;
	int pagesize;
    int min_readers;
    int min_dbs;
    uint64_t min_size;
    uint64_t max_size;
    char *strversion;
    int libversion;
    int dataversion;
    int disk_reserve;
} dbmdb_limits_t;

/* structure which holds our stuff */
typedef struct dbmdb_config
{
    dbmdb_baseconfig_t config;            /* Config parameters in dse.ldif */
    dbmdb_baseconfig_t running;           /* Config parameters at startup */
    dbmdb_limits_t limits;                /* Limits */
    int dbmdb_libversion;
    perfctrs_private *perfctrs_priv;

} dbmdb_config;

extern Slapi_ComponentId *dbmdb_componentid;

int dbmdb_init(struct ldbminfo *li, config_info *config_array);

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
int dbmdb_config_load_dse_info(struct ldbminfo *li);
int dbmdb_config_internal_set(struct ldbminfo *li, char *attrname, char *value);
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

/* instance functions */
int dbmdb_instance_cleanup(struct ldbm_instance *inst);
int dbmdb_instance_config_set(ldbm_instance *inst, char *attrname, int mod_apply, int mod_op, int phase, struct berval *value);
int dbmdb_instance_create(struct ldbm_instance *inst);
int dbmdb_instance_search_callback(Slapi_Entry *e, int *returncode, char *returntext, ldbm_instance *inst);

/* function for autotuning */
int dbmdb_start_autotune(struct ldbminfo *li);

/* helper functions */
int dbmdb_get_aux_id2entry(backend *be, MDB_dbi**ppDB, MDB_env **ppEnv, char **path);
int dbmdb_get_aux_id2entry_ext(backend *be, MDB_dbi**ppDB, MDB_env **ppEnv, char **path, int flags);
int dbmdb_release_aux_id2entry(backend *be, MDB_dbi*pDB, MDB_env *pEnv);
char *dbmdb_get_home_dir(struct ldbminfo *li, int *dbhome);
char *dbmdb_get_db_dir(struct ldbminfo *li);
int dbmdb_copy_directory(struct ldbminfo *li, Slapi_Task *task, char *src_dir, char *dest_dir, int restore, int *cnt, int indexonly, int is_changelog);
int dbmdb_remove_env(struct ldbminfo *li);
int dbmdb_bt_compare(MDB_dbi*db, const MDB_val *dbt1, const MDB_val *dbt2);
int dbmdb_open_huge_file(const char *path, int oflag, int mode);
int dbmdb_check_and_set_import_cache(struct ldbminfo *li);
int dbmdb_close_file(MDB_dbi**db);
int dbmdb_post_close(struct ldbminfo *li, int dbmode);
int dbmdb_config_set(void *arg, char *attr_name, config_info *config_array, struct berval *bval, char *err_buf, int phase, int apply_mod, int mod_op);
void dbmdb_config_get(void *arg, config_info *config, char *buf);
int dbmdb_add_op_attrs(Slapi_PBlock *pb, struct ldbminfo *li, struct backentry *ep, int *status);
int dbmdb_back_ldif2db(Slapi_PBlock *pb);
void dbmdb_set_recovery_required(struct ldbminfo *li);
void *dbmdb_config_db_logdirectory_get_ext(void *arg);
int dbmdb_db_remove(dbmdb_db_env *env, char const path[], char const dbName[]);
void dbmdb_set_env_debugging(MDB_env *pEnv, dbmdb_config *conf);
void dbmdb_back_free_incl_excl(char **include, char **exclude);
int dbmdb_back_ok_to_dump(const char *dn, char **include, char **exclude);
int dbmdb_back_fetch_incl_excl(Slapi_PBlock *pb, char ***include, char ***exclude);
PRUint64 dbmdb_get_id2entry_size(ldbm_instance *inst);

int dbmdb_idl_new_compare_dups(MDB_dbi* db __attribute__((unused)), const MDB_val *a, const MDB_val *b);

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


/* dbimpl helpers */
backend *dbmdb_be(void);
void dbmdb_dbival2dbt(dbi_val_t *dbi, MDB_val *dbt, PRBool isresponse);
void dbmdb_dbt2dbival(MDB_val *dbt, dbi_val_t *dbi, PRBool isresponse);
int dbmdb_uses_locking(MDB_env *db_env);
int dbmdb_uses_transactions(MDB_env *db_env);
int dbmdb_uses_mpool(MDB_env *db_env);
int dbmdb_uses_logging(MDB_env *db_env);

/* mdb version functions */
int dbmdb_version_write(struct ldbminfo *li, const char *directory, const char *dataversion, PRUint32 flags);
int dbmdb_version_read(struct ldbminfo *li, const char *directory, char **ldbmversion, char **dataversion);
int dbmdb_version_exists(struct ldbminfo *li, const char *directory);

/* config functions */
int dbmdb_instance_delete_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst);
int dbmdb_instance_post_delete_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst);
int dbmdb_instance_add_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst);
int dbmdb_instance_postadd_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst);
void dbmdb_config_setup_default(struct ldbminfo *li);

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
void dbmdb_perfctrs_as_entry(Slapi_Entry *e, perfctrs_private *priv, MDB_env *db_env);

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

/* mdb_misc.c */
int dbmdb_count_config_entries(char *filter, int *nbentries);


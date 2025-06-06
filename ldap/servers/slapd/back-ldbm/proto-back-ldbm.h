/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2023 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef _PROTO_BACK_LDBM
#define _PROTO_BACK_LDBM

struct _ImportJob;        /* fully defined in import.h */


/*
 * attr.c
 */
struct attrinfo *attrinfo_new(void);
void attrinfo_delete(struct attrinfo **pp);
void attrinfo_delete_from_tree(backend *be, struct attrinfo *ai);
void ainfo_get(backend *be, char *type, struct attrinfo **at);
void attr_masks(backend *be, char *type, int *indexmask, int *syntaxmask);
void attr_masks_ex(backend *be, char *type, int *indexmask, int *syntaxmask, struct attrinfo **at);
int attr_index_config(backend *be, char *fname, int lineno, Slapi_Entry *e, int init, int none, char *err_buf);
int db2index_add_indexed_attr(backend *be, char *attrString);
int ldbm_compute_init(void);
void attrinfo_deletetree(ldbm_instance *inst);
void attr_create_empty(backend *be, char *type, struct attrinfo **ai);

/*
 * cache.c
 */
void cache_disable(void);
int cache_init(struct cache *cache, struct ldbm_instance *inst, uint64_t maxsize, int64_t maxentries, int type);
void cache_clear(struct cache *cache, int type);
void cache_destroy_please(struct cache *cache, int type);
void cache_set_max_size(struct cache *cache, uint64_t bytes, int type);
void cache_set_max_entries(struct cache *cache, int64_t entries);
uint64_t cache_get_max_size(struct cache *cache);
int64_t cache_get_max_entries(struct cache *cache);
void cache_get_stats(struct cache *cache, struct cache_stats *stats);
void cache_debug_hash(struct cache *cache, char **out);
int cache_remove(struct cache *cache, void *e);
void cache_return(struct cache *cache, void **bep);
void cache_lock(struct cache *cache);
void cache_unlock(struct cache *cache);
struct backentry *cache_find_dn(struct cache *cache, const char *dn, unsigned long ndnlen);
struct backentry *cache_find_id(struct cache *cache, ID id);
struct backentry *cache_find_uuid(struct cache *cache, const char *uuid);
int cache_add(struct cache *cache, void *ptr, void **alt);
int cache_add_tentative(struct cache *cache, struct backentry *e, struct backentry **alt);
int cache_lock_entry(struct cache *cache, struct backentry *e);
void cache_unlock_entry(struct cache *cache, struct backentry *e);
int cache_replace(struct cache *cache, void *oldptr, void *newptr);
int cache_has_otherref(struct cache *cache, void *bep);
int cache_is_in_cache(struct cache *cache, void *ptr);
void revert_cache(ldbm_instance *inst, struct timespec *start_time);
int cache_is_reverted_entry(struct cache *cache, struct backentry *e);

#ifdef CACHE_DEBUG
void check_entry_cache(struct cache *cache, struct backentry *e);
#endif

Hashtable *new_hash(u_long size, u_long offset, HashFn hfn, HashTestFn tfn);
int add_hash(Hashtable *ht, void *key, uint32_t keylen, void *entry, void **alt);
int find_hash(Hashtable *ht, const void *key, uint32_t keylen, void **entry);
int remove_hash(Hashtable *ht, const void *key, uint32_t keylen);

struct backdn *dncache_find_id(struct cache *cache, ID id);

/*
 * dblayer.c
 */
int dblayer_init(struct ldbminfo *li);
int dblayer_setup(struct ldbminfo *li);
int dblayer_start(struct ldbminfo *li, int dbmode);
int dblayer_close(struct ldbminfo *li, int dbmode);
int dblayer_instance_close(backend *be);
int dblayer_get_index_file(backend *be, struct attrinfo *a, dbi_db_t **ppDB, int create);
int dblayer_release_index_file(backend *be, struct attrinfo *a, dbi_db_t *pDB);
int dblayer_erase_index_file(backend *be, struct attrinfo *a, PRBool use_lock, int no_force_chkpt);
int dblayer_get_id2entry(backend *be, dbi_db_t **ppDB);
int dblayer_get_changelog(backend *be, dbi_db_t ** ppDB, int create);
int dblayer_release_id2entry(backend *be, dbi_db_t *pDB);
void dblayer_destroy_txn_stack(void);
int dblayer_txn_init(struct ldbminfo *li, back_txn *txn);
int dblayer_txn_begin(backend *be, back_txnid parent_txn, back_txn *txn);
int dblayer_txn_begin_ext(struct ldbminfo *li, back_txnid parent_txn, back_txn *txn, PRBool use_lock);
int dblayer_txn_commit(backend *be, back_txn *txn);
int dblayer_txn_commit_ext(struct ldbminfo *li, back_txn *txn, PRBool use_lock);
int dblayer_txn_abort(backend *be, back_txn *txn);
int dblayer_txn_abort_ext(struct ldbminfo *li, back_txn *txn, PRBool use_lock);
int dblayer_read_txn_abort(backend *be, back_txn *txn);
int dblayer_read_txn_begin(backend *be, back_txnid parent_txn, back_txn *txn);
int dblayer_read_txn_commit(backend *be, back_txn *txn);
int dblayer_txn_begin_all(struct ldbminfo *li, back_txnid parent_txn, back_txn *txn);
int dblayer_txn_commit_all(struct ldbminfo *li, back_txn *txn);
int dblayer_txn_abort_all(struct ldbminfo *li, back_txn *txn);
void dblayer_unlock_backend(backend *be);
void dblayer_lock_backend(backend *be);
int dblayer_plugin_begin(Slapi_PBlock *pb);
int dblayer_plugin_commit(Slapi_PBlock *pb);
int dblayer_plugin_abort(Slapi_PBlock *pb);
int dblayer_backup(struct ldbminfo *li, char *destination_directory, Slapi_Task *task);
int dblayer_restore(struct ldbminfo *li, char *source_directory, Slapi_Task *task);
int dblayer_delete_database(struct ldbminfo *li);
int dblayer_close_indexes(backend *be);
int dblayer_open_file(backend *be, char *indexname, int create, struct attrinfo *ai, dbi_db_t **ppDB);
void dblayer_remember_disk_filled(struct ldbminfo *li);
int dblayer_instance_start(backend *be, int normal_mode);
int dblayer_make_new_instance_data_dir(backend *be);
int dblayer_get_instance_data_dir(backend *be);
PRInt64 db_atol(char *str, int *err);
PRInt64 db_atoi(char *str, int *err);
uint32_t db_strtoul(const char *str, int *err);
uint64_t db_strtoull(const char *str, int *err);
int dblayer_in_import(ldbm_instance *inst);
int ldbm_back_entry_release(Slapi_PBlock *pb, void *backend_info_ptr);

char *dblayer_get_full_inst_dir(struct ldbminfo *li, ldbm_instance *inst, char *buf, int buflen);

int ldbm_back_get_info(Slapi_Backend *be, int cmd, void **info);
int ldbm_back_set_info(Slapi_Backend *be, int cmd, void *info);
int ldbm_back_ctrl_info(Slapi_Backend *be, int cmd, void *info);

int dblayer_is_restored(void);
void dblayer_set_restored(void);
int dblayer_restore_file_init(struct ldbminfo *li);
void dblayer_restore_file_update(struct ldbminfo *li, char *directory);
int dblayer_import_file_init(ldbm_instance *inst);
void dblayer_import_file_update(ldbm_instance *inst);
int dblayer_import_file_check(ldbm_instance *inst);
const char *dblayer_get_db_suffix(Slapi_Backend *be);

/*
 * dn2entry.c
 */
struct backentry *dn2entry(Slapi_Backend *be, const Slapi_DN *sdn, back_txn *txn, int *err);
struct backentry *dn2entry_ext(Slapi_Backend *be, const Slapi_DN *sdn, back_txn *txn, int flags, int *err);
struct backentry *dn2entry_or_ancestor(Slapi_Backend *be, const Slapi_DN *sdn, Slapi_DN *ancestor, back_txn *txn, int *err);
struct backentry *dn2ancestor(Slapi_Backend *be, const Slapi_DN *sdn, Slapi_DN *ancestordn, back_txn *txn, int *err, int allow_suffix);
int get_copy_of_entry(Slapi_PBlock *pb, const entry_address *addr, back_txn *txn, int plock_parameter, int must_exist);
int get_copy_of_entry_ext(Slapi_PBlock *pb, ID id, const entry_address *addr, back_txn *txn, int plock_parameter, int must_exist);
void done_with_pblock_entry(Slapi_PBlock *pb, int plock_parameter);

/*
 * uniqueid2entry.c
 */
struct backentry *uniqueid2entry(backend *be, const char *uniqueid, back_txn *txn, int *err);

/*
 * filterindex.c
 */
IDList *filter_candidates(Slapi_PBlock *pb, backend *be, const char *base, Slapi_Filter *f, Slapi_Filter *nextf, int range, int *err);
IDList *filter_candidates_ext(Slapi_PBlock *pb, backend *be, const char *base, Slapi_Filter *f, Slapi_Filter *nextf, int range, int *err, int allidslimit);

/*
 * findentry.c
 */
/* Return code */
#define FE_RC_SENT_RESULT 1
struct backentry *find_entry2modify(Slapi_PBlock *pb, Slapi_Backend *be, const entry_address *addr, back_txn *txn, int *rc);
struct backentry *find_entry(Slapi_PBlock *pb, Slapi_Backend *be, const entry_address *addr, back_txn *txn, int *rc);
struct backentry *find_entry2modify_only(Slapi_PBlock *pb, Slapi_Backend *be, const entry_address *addr, back_txn *txn, int *rc);
struct backentry *find_entry2modify_only_ext(Slapi_PBlock *pb, Slapi_Backend *be, const entry_address *addr, int flags, back_txn *txn, int *rc);
struct backentry *find_entry_only(Slapi_PBlock *pb, Slapi_Backend *be, const entry_address *addr, back_txn *txn, int *rc);
int check_entry_for_referral(Slapi_PBlock *pb, Slapi_Entry *entry, char *matched, const char *callingfn);

/*
 * haschildren.c
 */
int has_children(struct ldbminfo *li, struct backentry *p, back_txn *txn, int *err);

/*
 * id2entry.c
 */
int id2entry_add(backend *be, struct backentry *e, back_txn *txn);
int id2entry_add_ext(backend *be, struct backentry *e, back_txn *txn, int encrypt, int *cache_res);
int id2entry_delete(backend *be, struct backentry *e, back_txn *txn);
struct backentry *id2entry(backend *be, ID id, back_txn *txn, int *err);

/*
 * idl.c
 */
IDList *idl_alloc(NIDS nids);
void idl_free(IDList **idl);
NIDS idl_length(IDList *idl);
int idl_is_allids(IDList *idl);
int idl_append(IDList *idl, ID id);
int idl_append_extend(IDList **idl, ID id);
void idl_insert(IDList **idl, ID id);
int idl_sort_cmp(const void *x, const void *y);
/*
 * idl_delete - delete an id from an id list.
 * returns  0   id deleted
 *      1   id deleted, first id in block has changed
 *      2   id deleted, block is empty
 *      3   id not there
 *      4   cannot delete from allids block
 */
int idl_delete(IDList **idl, ID id);
IDList *idl_allids(backend *be);
IDList *idl_fetch(backend *be, dbi_db_t *db, dbi_val_t *key, dbi_txn_t *txn, struct attrinfo *a, int *err);
IDList *idl_fetch_ext(backend *be, dbi_db_t *db, dbi_val_t *key, dbi_txn_t *txn, struct attrinfo *a, int *err, int allidslimit);
int idl_insert_key(backend *be, dbi_db_t *db, dbi_val_t *key, ID id, back_txn *txn, struct attrinfo *a, int *disposition);
int idl_delete_key(backend *be, dbi_db_t *db, dbi_val_t *key, ID id, back_txn *txn, struct attrinfo *a);
IDList *idl_intersection(backend *be, IDList *a, IDList *b);
IDList *idl_union(backend *be, IDList *a, IDList *b);
int idl_notin(backend *be, IDList *a, IDList *b, IDList **new_result);
ID idl_firstid(IDList *idl);
ID idl_nextid(IDList *idl, ID id);
int idl_init_private(backend *be, struct attrinfo *a);
int idl_release_private(struct attrinfo *a);
int idl_id_is_in_idlist(IDList *idl, ID id);

idl_iterator idl_iterator_init(const IDList *idl);
idl_iterator idl_iterator_increment(idl_iterator *i);
idl_iterator idl_iterator_decrement(idl_iterator *i);
ID idl_iterator_dereference(idl_iterator i, const IDList *idl);
ID idl_iterator_dereference_increment(idl_iterator *i, const IDList *idl);
ID idl_iterator_dereference_decrement(idl_iterator *i, const IDList *idl);
size_t idl_sizeof(IDList *idl);
int idl_store_block(backend *be, dbi_db_t *db, dbi_val_t *key, IDList *idl, dbi_txn_t *txn, struct attrinfo *a);
void idl_set_tune(int val);
int idl_get_tune(void);
size_t idl_get_allidslimit(struct attrinfo *a, int allidslimit);
int idl_get_idl_new(void);
IDList *idl_new_range_fetch(backend *be, dbi_db_t *db, dbi_val_t *lowerkey, dbi_val_t *upperkey, dbi_txn_t *txn, struct attrinfo *a, int *flag_err, int allidslimit, int sizelimit, struct timespec *expire_time, int lookthrough_limit, int operator);
IDList *idl_lmdb_range_fetch(backend *be, dbi_db_t *db, dbi_val_t *lowerkey, dbi_val_t *upperkey, dbi_txn_t *txn, struct attrinfo *a, int *flag_err, int allidslimit, int sizelimit, struct timespec *expire_time, int lookthrough_limit, int operator);
char *get_index_name(backend *be, dbi_db_t *db, struct attrinfo *a);

int64_t idl_compare(IDList *a, IDList *b);

/*
 * idl_set.c
 */
IDListSet *idl_set_create(void);
void idl_set_destroy(IDListSet *idl_set);
void idl_set_insert_idl(IDListSet *idl_set, IDList *idl);
void idl_set_insert_complement_idl(IDListSet *idl_set, IDList *idl);
int64_t idl_set_union_shortcut(IDListSet *idl_set);
int64_t idl_set_intersection_shortcut(IDListSet *idl_set);
IDList *idl_set_union(IDListSet *idl_set, backend *be);
IDList *idl_set_intersect(IDListSet *idl_set, backend *be);

/*
 * index.c
 */
int index_addordel_entry(backend *be, struct backentry *e, int flags, back_txn *txn);
int index_add_mods(backend *be, /*const*/ LDAPMod **mods, struct backentry *olde, struct backentry *newe, back_txn *txn);
int index_addordel_string(backend *be, const char *type, const char *s, ID id, int flags, back_txn *txn);
int index_addordel_values_sv(backend *be, const char *type, Slapi_Value **vals, Slapi_Value **evals, ID id, int flags, back_txn *txn);
int index_addordel_values_ext_sv(backend *be, const char *type, Slapi_Value **vals, Slapi_Value **evals, ID id, int flags, back_txn *txn, int *idl_disposition, void *buffer_handle);
int id_array_init(Id_Array *new_guy, int size);

IDList *index_read(backend *be, const char *type, const char *indextype, const struct berval *val, back_txn *txn, int *err);
IDList *index_read_ext(backend *be, char *type, const char *indextype, const struct berval *val, back_txn *txn, int *err, int *unindexed);
IDList *index_read_ext_allids(Slapi_PBlock *pb, backend *be, char *type, const char *indextype, const struct berval *val, back_txn *txn, int *err, int *unindexed, int allidslimit);
IDList *index_range_read(Slapi_PBlock *pb, backend *be, char *type, const char *indextype, int ftype, struct berval *val, struct berval *nextval, int range, back_txn *txn, int *err);
IDList *index_range_read_ext(Slapi_PBlock *pb, backend *be, char *type, const char *indextype, int ftype, struct berval *val, struct berval *nextval, int range, back_txn *txn, int *err, int allidslimit);
const char *encode(const struct berval *data, char buf[BUFSIZ]);
int DBTcmp(dbi_val_t *L, dbi_val_t *R, value_compare_fn_type cmp_fn);

extern const char *indextype_PRESENCE;
extern const char *indextype_EQUALITY;
extern const char *indextype_APPROX;
extern const char *indextype_SUB;

int index_buffer_init(size_t size, int flags, void **h);
int index_buffer_flush(void *h, backend *be, dbi_txn_t *txn, struct attrinfo *a);
int index_buffer_terminate(backend *be, void *h);

int get_suffix_key(Slapi_Backend *be, struct _back_info_index_key *info);
int set_suffix_key(Slapi_Backend *be, struct _back_info_index_key *info);
char *index_index2prefix(const char *indextype);
void index_free_prefix(char *);

/*
 * instance.c
 */
int ldbm_instance_create(backend *be, char *name);
int ldbm_instance_create_default_indexes(backend *be);
int ldbm_instance_start(backend *be);
void ldbm_instance_stop_cache(backend *be);
int ldbm_instance_startall(struct ldbminfo *li);
int ldbm_instance_stopall_caches(struct ldbminfo *li);
ldbm_instance *ldbm_instance_find_by_name(struct ldbminfo *li, char *name);
int ldbm_instance_destroy(ldbm_instance *inst);

/*
 * ldif2ldbm.c
 */
void import_subcount_stuff_init(import_subcount_stuff *stuff);
void import_subcount_stuff_term(import_subcount_stuff *stuff);
int get_parent_rdn(dbi_db_t *db, ID parentid, Slapi_RDN *srdn);


/*
 * modify.c
 */
int32_t entry_get_rdn_mods(Slapi_PBlock *pb, Slapi_Entry *entry, CSN *csn, int repl_op, Slapi_Mods **smods_ret);
int modify_update_all(backend *be, Slapi_PBlock *pb, modify_context *mc, back_txn *txn);
void modify_init(modify_context *mc, struct backentry *old_entry);
int modify_apply_mods(modify_context *mc, Slapi_Mods *smods);
int modify_term(modify_context *mc, backend *be);
int modify_switch_entries(modify_context *mc, backend *be);
int modify_unswitch_entries(modify_context *mc, backend *be);
int modify_apply_mods_ignore_error(modify_context *mc, Slapi_Mods *smods, int error);

/*
 * add.c
 */
void add_update_entry_operational_attributes(struct backentry *ep, ID pid);
void add_update_entrydn_operational_attributes(struct backentry *ep);

/*
 * misc.c
 */
void ldbm_nasty(const char *func, const char *str, int c, int err);
void ldbm_log_access_message(Slapi_PBlock *pblock, char *string);
int return_on_disk_full(struct ldbminfo *li);
int ldbm_attribute_always_indexed(const char *attrtype);
void ldbm_destroy_instance_name(struct ldbminfo *li);
char *compute_entry_tombstone_dn(const char *entrydn, const char *uniqueid);
char *compute_entry_tombstone_rdn(const char *entryrdn, const char *uniqueid);
int instance_set_busy(ldbm_instance *inst);
int instance_set_busy_and_readonly(ldbm_instance *inst);
void instance_set_not_busy(ldbm_instance *inst);
void allinstance_set_busy(struct ldbminfo *li);
void allinstance_set_not_busy(struct ldbminfo *li);
int is_anyinstance_busy(struct ldbminfo *li);
int is_instance_busy(ldbm_instance *inst);
int ldbm_delete_dirs(char *path);
int mkdir_p(char *dir, unsigned int mode);
int is_fullpath(char *path);
char get_sep(char *path);
int ldbm_txn_ruv_modify_context(Slapi_PBlock *pb, modify_context *mc);
int get_value_from_string(const char *string, char *type, char **value);
int get_values_from_string(const char *string, char *type, char ***valuearray);
void normalize_dir(char *dir);
void ldbm_set_error(Slapi_PBlock *pb, int retval, int *ldap_result_code, char **ldap_result_message);

/*
 * nextid.c
 */
ID next_id(backend *be);
void next_id_return(backend *be, ID id);
ID next_id_get(backend *be);
void id_internal_to_stored(ID, char *);
ID id_stored_to_internal(const char *);
void sizeushort_internal_to_stored(size_t i, char *b);
size_t sizeushort_stored_to_internal(const char *b);
void get_ids_from_disk(backend *be);
void get_both_ids(struct ldbminfo *li, ID *nextid, ID *nextid2index);

/*
 * backentry.c
 */
struct backentry *backentry_init(Slapi_Entry *e);
struct backentry *backentry_alloc(void);
void backentry_free(struct backentry **bep);
struct backentry *backentry_dup(struct backentry *);
void backentry_clear_entry(struct backentry *);
char *backentry_get_ndn(const struct backentry *e);
const Slapi_DN *backentry_get_sdn(const struct backentry *e);

struct backdn *backdn_init(Slapi_DN *sdn, ID id, int to_remove_from_hash);
void backdn_free(struct backdn **bdn);
void backentry_init_weight(BackEntryWeightData *starttime);
void backentry_compute_weight(struct backentry *e, const BackEntryWeightData *starttime);

/*
 * parents.c
 */
int parent_update_on_childchange(modify_context *mc, int op, size_t *numofchildren);

/*
 * perfctrs.c
 */
void perfctrs_wait(size_t milliseconds, perfctrs_private *priv, dbi_env_t *db_env);
void perfctrs_init(struct ldbminfo *li, perfctrs_private **priv);
void perfctrs_terminate(perfctrs_private **priv, dbi_env_t *db_env);
void perfctrs_as_entry(struct ldbminfo *li, Slapi_Entry *e, perfctrs_private *priv, dbi_env_t *db_env);

/*
 * rmdb.c
 */
int ldbm_back_rmdb(Slapi_PBlock *pb);

/*
 * sort.c
 */

/*
 * Definitions for sort spec object
 */
struct sort_spec_thing
{
    char *type;                       /* attribute type */
    char *matchrule;                  /* Matching rule string */
    int order;                        /* 0 == ascending, 1 == decending */
    struct sort_spec_thing *next;     /* Link to the next one */
    Slapi_PBlock *mr_pb;              /* For matchrule indexing */
    value_compare_fn_type compare_fn; /* For non-matchrule indexing */
    Slapi_Attr sattr;
};
typedef struct sort_spec_thing sort_spec_thing;
typedef struct sort_spec_thing sort_spec;

void sort_spec_free(sort_spec *s);
int sort_candidates(backend *be, int lookthrough_limit, struct timespec *expire_time, Slapi_PBlock *pb, IDList *candidates, sort_spec_thing *sort_spec, char **sort_error_type);
int make_sort_response_control(Slapi_PBlock *pb, int code, char *error_type);
int parse_sort_spec(struct berval *sort_spec_ber, sort_spec **ps);
struct berval *attr_value_lowest(struct berval **values, value_compare_fn_type compare_fn);
int sort_attr_compare(struct berval **value_a, struct berval **value_b, value_compare_fn_type compare_fn);
const char *sort_log_access(Slapi_PBlock *pb, sort_spec_thing *s, IDList *candidates, PRBool just_copy);

/*
 * dbsize.c
 */
int ldbm_db_size(Slapi_PBlock *pb);

/*
 * external functions
 */
int ldbm_back_bind(Slapi_PBlock *pb);
int ldbm_back_unbind(Slapi_PBlock *pb);
int ldbm_back_search(Slapi_PBlock *pb);
int ldbm_back_compare(Slapi_PBlock *pb);
int ldbm_back_modify(Slapi_PBlock *pb);
int ldbm_back_modrdn(Slapi_PBlock *pb);
int ldbm_back_add(Slapi_PBlock *pb);
int ldbm_back_delete(Slapi_PBlock *pb);
int ldbm_back_abandon(Slapi_PBlock *pb);
int ldbm_back_config(Slapi_PBlock *pb);
int ldbm_back_close(Slapi_PBlock *pb);
int ldbm_back_cleanup(Slapi_PBlock *pb);
void ldbm_back_instance_set_destructor(void **arg);
int ldbm_back_start(Slapi_PBlock *pb);
int ldbm_back_seq(Slapi_PBlock *pb);
int ldbm_back_ldif2ldbm(Slapi_PBlock *pb);
int ldbm_back_ldbm2ldif(Slapi_PBlock *pb);
int ldbm_back_ldbm2ldifalt(Slapi_PBlock *pb);
int ldbm_back_ldbm2index(Slapi_PBlock *pb);
int ldbm_back_upgradednformat(Slapi_PBlock *pb);
int ldbm_back_archive2ldbm(Slapi_PBlock *pb);
int ldbm_back_ldbm2archive(Slapi_PBlock *pb);
int ldbm_back_upgradedb(Slapi_PBlock *pb);
int ldbm_back_dbverify(Slapi_PBlock *pb);
int ldbm_back_next_search_entry(Slapi_PBlock *pb);
void ldbm_back_search_results_release(void **search_results);
int ldbm_back_init(Slapi_PBlock *pb);
void ldbm_back_prev_search_results(Slapi_PBlock *pb);
int ldbm_back_isinitialized(void);
int32_t ldbm_back_compact(Slapi_Backend *be, PRBool just_changelog);
int32_t ldbm_archive_config(char *bakdir, Slapi_Task *task);

/*
 * vlv.c
 */
struct vlv_request
{
    ber_int_t beforeCount;
    ber_int_t afterCount;
    ber_tag_t tag;
    ber_int_t index;
    ber_int_t contentCount;
    struct berval value;
};

struct vlv_response
{
    ber_int_t targetPosition;
    ber_int_t contentCount;
    ber_int_t result;
};

char ** vlv_list_filenames(ldbm_instance *inst);
int does_vlv_need_init(ldbm_instance *inst);
int vlv_init(ldbm_instance *inst);
void vlv_close(ldbm_instance *inst);
int vlv_remove_callbacks(ldbm_instance *inst);
const Slapi_Entry **vlv_get_search_entries(void);
struct vlvIndex *vlv_find_searchname(const char *name, backend *be);
struct vlvIndex *vlv_find_indexname(const char *name, backend *be);
char *vlv_getindexnames(backend *be);
int vlv_search_build_candidate_list(Slapi_PBlock *pb, const Slapi_DN *base, int *rc, const sort_spec *sort_control, const struct vlv_request *vlv_request_control, IDList **candidates, struct vlv_response *vlv_response_control);
int vlv_update_index(struct vlvIndex *p, back_txn *txn, struct ldbminfo *li, Slapi_PBlock *pb, struct backentry *oldEntry, struct backentry *newEntry);
int vlv_update_all_indexes(back_txn *txn, backend *be, Slapi_PBlock *pb, struct backentry *oldEntry, struct backentry *newEntry);
int vlv_filter_candidates(backend *be, Slapi_PBlock *pb, const IDList *candidates, const Slapi_DN *base, int scope, Slapi_Filter *filter, IDList **filteredCandidates, int lookthrough_limit, struct timespec *expire_time);
int vlv_trim_candidates_txn(backend *be, const IDList *candidates, const sort_spec *sort_control, const struct vlv_request *vlv_request_control, IDList **filteredCandidates, struct vlv_response *pResponse, back_txn *txn);
int vlv_trim_candidates(backend *be, const IDList *candidates, const sort_spec *sort_control, const struct vlv_request *vlv_request_control, IDList **filteredCandidates, struct vlv_response *pResponse);
int vlv_parse_request_control(backend *be, struct berval *vlv_spec_ber, struct vlv_request *vlvp);
int vlv_make_response_control(Slapi_PBlock *pb, const struct vlv_response *vlvp);
void vlv_getindices(int32_t (*callback_fn)(caddr_t, caddr_t),  void *param, backend *be);
void vlv_print_access_log(Slapi_PBlock *pb, struct vlv_request *vlvi, struct vlv_response *vlvo, sort_spec_thing *sort_control);
void vlv_grok_new_import_entry(const struct backentry *e, backend *be, int *seen_them_all);
IDList *vlv_find_index_by_filter(struct backend *be, const char *base, Slapi_Filter *f);
IDList *vlv_find_index_by_filter_txn(struct backend *be, const char *base, Slapi_Filter *f, back_txn *txn);
int vlv_delete_search_entry(Slapi_PBlock *pb, Slapi_Entry *e, ldbm_instance *inst);
void vlv_acquire_lock(backend *be);
void vlv_release_lock(backend *be);
int vlv_isvlv(char *filename);
void vlv_rebuild_scope_filter(backend *be);


/*
 * archive.c
 */
int ldbm_temporary_close_all_instances(Slapi_PBlock *pb);
int ldbm_restart_temporary_closed_instances(Slapi_PBlock *pb);

/*
 * archive.c
 */
int ldbm_temporary_close_all_instances(Slapi_PBlock *pb);
int ldbm_restart_temporary_closed_instances(Slapi_PBlock *pb);
/*
 * Indexfile.c
 */
int indexfile_delete_all_keys(backend *be, char *type, back_txn *txn);
int indexfile_primary_modifyall(backend *be, LDAPMod **mods_to_perform, char **indexes_to_update, back_txn *txn);

/*
 * ldbm_search.c
 */
Slapi_Filter *create_onelevel_filter(Slapi_Filter *filter, const struct backentry *e, int managedsait);
Slapi_Filter *create_subtree_filter(Slapi_Filter *filter, int managedsait);
IDList *subtree_candidates(Slapi_PBlock *pb, backend *be, const char *base, const struct backentry *e, Slapi_Filter *filter, int *allids_before_scopingp, int *err);
void search_set_tune(struct ldbminfo *li, int val);
int search_get_tune(struct ldbminfo *li);
int compute_lookthrough_limit(Slapi_PBlock *pb, struct ldbminfo *li);
int compute_allids_limit(Slapi_PBlock *pb, struct ldbminfo *li);


/*
 * matchrule.c
 */
int create_matchrule_indexer(Slapi_PBlock **pb, char *matchrule, char *type);
int destroy_matchrule_indexer(Slapi_PBlock *pb);
int matchrule_values_to_keys(Slapi_PBlock *pb, Slapi_Value **input_values, struct berval ***output_values);
int matchrule_values_to_keys_sv(Slapi_PBlock *pb, Slapi_Value **input_values, Slapi_Value ***output_values);

/*
 * init.c
 */
int ldbm_attribute_always_indexed(const char *attrtype);

/*
 * dbversion.c
 */
int dbversion_read(struct ldbminfo *li, const char *directory, char **ldbmversion, char **dataversion);

/*
 * config_ldbm.c
 */
int ldbm_config_load_dse_info_phase0(struct ldbminfo *li);
int ldbm_config_load_dse_info_phase1(struct ldbminfo *li);
void ldbm_config_setup_default(struct ldbminfo *li);
void ldbm_config_internal_set(struct ldbminfo *li, char *attrname, char *value);
void ldbm_instance_config_setup_default(ldbm_instance *inst);
int ldbm_instance_postadd_instance_entry_callback(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int ldbm_instance_add_instance_entry_callback(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int ldbm_instance_delete_instance_entry_callback(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int ldbm_instance_post_delete_instance_entry_callback(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
/* Index config functions */
int ldbm_index_init_entry_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int ldbm_instance_index_config_add_callback(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *e, int *returncode, char *returntext, void *arg);
int ldbm_instance_index_config_delete_callback(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *e, int *returncode, char *returntext, void *arg);
int ldbm_instance_index_config_modify_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
/* Attribute Encryption config functions */
int ldbm_attrcrypt_init_entry_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int ldbm_instance_attrcrypt_config_add_callback(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *e, int *returncode, char *returntext, void *arg);
int ldbm_instance_attrcrypt_config_delete_callback(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *e, int *returncode, char *returntext, void *arg);
int ldbm_instance_attrcrypt_config_modify_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);

int back_crypt_init(Slapi_Backend *be, const char *dn, const char *encAlgorithm, void **handle);
int back_crypt_destroy(void *handle);
int back_crypt_encrypt_value(void *handle, struct berval *in, struct berval **out);
int
back_crypt_decrypt_value(void *handle, struct berval *in, struct berval **out);

void replace_ldbm_config_value(char *conftype, char *val, struct ldbminfo *li);

/*
 * ancestorid.c
 */
int ldbm_ancestorid_index_entry(backend *be, struct backentry *e, int flags, back_txn *txn);
int ldbm_ancestorid_read(backend *be, back_txn *txn, ID id, IDList **idl);
int ldbm_ancestorid_read_ext(backend *be, back_txn *txn, ID id, IDList **idl, int allidslimit);
int ldbm_ancestorid_move_subtree(
    backend *be,
    const Slapi_DN *olddn,
    const Slapi_DN *newdn,
    ID id,
    IDList *subtree_idl,
    back_txn *txn);

/*
 * import.c
 */
int ldbm_back_wire_import(Slapi_PBlock *pb);
void import_abort_all(struct _ImportJob *job, int wait_for_them);
void *factory_constructor(void *object __attribute__((unused)), void *parent __attribute__((unused)));
void factory_destructor(void *extension, void *object, void *parent __attribute__((unused)));
uint64_t wait_for_ref_count(Slapi_Counter *inst_ref_count);

/*
 * ldbm_attrcrypt.c
 */
int attrcrypt_decrypt_entry(backend *be, struct backentry *e);
int attrcrypt_encrypt_entry_inplace(backend *be, const struct backentry *inout);
int attrcrypt_encrypt_entry(backend *be, const struct backentry *in, struct backentry **out);
int attrcrypt_encrypt_index_key(backend *be, struct attrinfo *ai, const struct berval *in, struct berval **out);
int attrcrypt_decrypt_index_key(backend *be, struct attrinfo *ai, const struct berval *in, struct berval **out);
int attrcrypt_hash_large_index_key(backend *be, char **prefix, struct attrinfo *ai, const struct berval *in, struct berval **out);
int attrcrypt_init(ldbm_instance *li);
int attrcrypt_cleanup_private(ldbm_instance *li);

/*
 * ldbm_usn.c
 */
void ldbm_usn_init(struct ldbminfo *li);
int ldbm_usn_enabled(backend *be);
int ldbm_set_last_usn(Slapi_Backend *be);

/*
 * ldbm_entryrdn.c
 */
int entryrdn_index_entry(backend *be, struct backentry *e, int flags, back_txn *txn);
int entryrdn_index_read(backend *be, const Slapi_DN *sdn, ID *id, back_txn *txn);
int
entryrdn_index_read_ext(backend *be, const Slapi_DN *sdn, ID *id, int flags, back_txn *txn);
int entryrdn_rename_subtree(backend *be, const Slapi_DN *oldsdn, Slapi_RDN *newsrdn, const Slapi_DN *newsupsdn, ID id, back_txn *txn, int flags);
int entryrdn_get_subordinates(backend *be, const Slapi_DN *sdn, ID id, IDList **subordinates, back_txn *txn, int flags);
int entryrdn_lookup_dn(backend *be, const char *rdn, ID id, char **dn, Slapi_RDN **psrdn, back_txn *txn);
int entryrdn_get_parent(backend *be, const char *rdn, ID id, char **prdn, ID *pid, back_txn *txn);
int entryrdn_compare_rdn_elem(const void *elem_a, const void *elem_b);
void *entryrdn_encode_data(backend *be, size_t *rdn_elem_len, ID id, const char *nrdn, const char *rdn);
void entryrdn_decode_data(backend *be, void *rdn_elem, ID *id, int *nrdnlen, char **nrdn, int *rdnlen, char **rdn);


#endif

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

#ifndef _PROTO_BACK_LDBM
#define _PROTO_BACK_LDBM

/*
 * attr.c
 */
struct attrinfo * attrinfo_new();
void attrinfo_delete(struct attrinfo **pp);
void ainfo_get( backend *be, char *type, struct attrinfo **at );
void attr_masks( backend *be, char *type, int *indexmask,
 int *syntaxmask );
void attr_masks_ex( backend *be, char *type, int *indexmask,
 int *syntaxmask, struct attrinfo **at );
void attr_index_config( backend *be, char *fname, int lineno,
 int argc, char **argv, int init );
int ldbm_compute_init();
void attrinfo_deletetree(ldbm_instance *inst);
void attr_create_empty(backend *be,char *type,struct attrinfo **ai);

/*
 * cache.c
 */
int cache_init(struct cache *cache, size_t maxsize, long maxentries);
void cache_clear(struct cache *cache);
void cache_destroy_please(struct cache *cache);
void cache_set_max_size(struct cache *cache, size_t bytes);
void cache_set_max_entries(struct cache *cache, long entries);
size_t cache_get_max_size(struct cache *cache);
long cache_get_max_entries(struct cache *cache);
void cache_get_stats(struct cache *cache, u_long *hits, u_long *tries,
             long *entries,long *maxentries, 
             size_t *size, size_t *maxsize);
void cache_debug_hash(struct cache *cache, char **out);
int cache_remove(struct cache *cache, struct backentry *e);
void cache_return(struct cache *cache, struct backentry **bep);
struct backentry *cache_find_dn(struct cache *cache, const char *dn, unsigned long ndnlen);
struct backentry *cache_find_id(struct cache *cache, ID id);
struct backentry *cache_find_uuid(struct cache *cache, const char *uuid);
int cache_add(struct cache *cache, struct backentry *e,
          struct backentry **alt);
int cache_add_tentative(struct cache *cache, struct backentry *e,
            struct backentry **alt);
int cache_lock_entry(struct cache *cache, struct backentry *e);
void cache_unlock_entry(struct cache *cache, struct backentry *e);
int cache_replace(struct cache *cache, struct backentry *olde,
          struct backentry *newe);

Hashtable *new_hash(u_long size, u_long offset, HashFn hfn,
               HashTestFn tfn);
int add_hash(Hashtable *ht, void *key, size_t keylen, void *entry,
            void **alt);
int find_hash(Hashtable *ht, const void *key, size_t keylen, void **entry);
int remove_hash(Hashtable *ht, const void *key, size_t keylen);

/*
 * dblayer.c
 */
int dblayer_init(struct ldbminfo *li);
int dblayer_terminate(struct ldbminfo *li);
int dblayer_start(struct ldbminfo *li, int dbmode);
int dblayer_flush(struct ldbminfo *li );
int dblayer_close(struct ldbminfo *li, int dbmode );
void dblayer_pre_close(struct ldbminfo *li);
int dblayer_post_close(struct ldbminfo *li, int dbmode );
int dblayer_instance_close(backend *be);
int dblayer_get_index_file(backend *be,struct attrinfo *a, DB** ppDB, int create);
int dblayer_release_index_file(backend *be,struct attrinfo *a, DB* pDB);
int dblayer_erase_index_file(backend *be, struct attrinfo *a, int no_force_chkpt);
int dblayer_erase_index_file_nolock(backend *be, struct attrinfo *a, int no_force_chkpt); 
int dblayer_get_id2entry(backend *be, DB **ppDB);
int dblayer_release_id2entry(backend *be, DB *pDB);
int dblayer_get_aux_id2entry(backend *be, DB **ppDB, DB_ENV **ppEnv);
int dblayer_release_aux_id2entry(backend *be, DB *pDB, DB_ENV *pEnv);
int dblayer_txn_init(struct ldbminfo *li, back_txn *txn);
int dblayer_txn_begin(struct ldbminfo *li,back_txnid parent_txn, back_txn *txn);
int dblayer_txn_commit(struct ldbminfo *li, back_txn *txn);
int dblayer_txn_abort(struct ldbminfo *li, back_txn *txn);
int dblayer_read_txn_abort(struct ldbminfo *li, back_txn *txn);
int dblayer_read_txn_begin(struct ldbminfo *li,back_txnid parent_txn, back_txn *txn);
int dblayer_read_txn_commit(struct ldbminfo *li, back_txn *txn);
size_t dblayer_get_optimal_block_size(struct ldbminfo *li);
void dblayer_unlock_backend(backend *be);
void dblayer_lock_backend(backend *be);
int dblayer_plugin_begin(Slapi_PBlock *pb);
int dblayer_plugin_commit(Slapi_PBlock *pb);
int dblayer_plugin_abort(Slapi_PBlock *pb);
int dblayer_memp_stat(struct ldbminfo *li, DB_MPOOL_STAT **gsp,DB_MPOOL_FSTAT ***fsp);
int dblayer_memp_stat_instance(ldbm_instance *inst, DB_MPOOL_STAT **gsp, DB_MPOOL_FSTAT ***fsp);
int dblayer_backup(struct ldbminfo *li, char *destination_directory,
                   Slapi_Task *task);
int dblayer_restore(struct ldbminfo *li, char* source_directory, Slapi_Task *task, char *bename);
int dblayer_copy_directory(struct ldbminfo *li, Slapi_Task *task,
                           char *instance_dir, char *destination_dir,
                           int restore, int *cnt, int instance_dir_flag,
                           int indexonly, int resetlsns);
int dblayer_copyfile(char* source, char * destination, int overwrite, int mode);
int dblayer_delete_instance_dir(backend *be);
int dblayer_delete_database(struct ldbminfo *li);
int dblayer_database_size(struct ldbminfo *li, unsigned int *size);
int dblayer_terminate(struct ldbminfo *li);
int dblayer_close_indexes(backend *be);
int dblayer_open_file(backend *be, char* indexname, int create, int index_flags, DB **ppDB);
int dblayer_close_file(DB *db);
void dblayer_sys_pages(size_t *pagesize, size_t *pages, size_t *procpages, size_t *availpages);
int dblayer_is_cachesize_sane(size_t *cachesize);
void dblayer_remember_disk_filled(struct ldbminfo *li);
int dblayer_open_huge_file(const char *path, int oflag, int mode);
int dblayer_instance_start(backend *be, int normal_mode);
int dblayer_make_new_instance_data_dir(backend *be);
int dblayer_get_instance_data_dir(backend *be);
char *dblayer_strerror(int error);
PRInt64 db_atol(char *str, int *err);
PRInt64 db_atoi(char *str, int *err);
unsigned long db_strtoul(const char *str, int *err);
int dblayer_set_batch_transactions(void *arg, void *value, char *errorbuf, int phase, int apply); 
void *dblayer_get_batch_transactions(void *arg); 
int dblayer_in_import(ldbm_instance *inst);

int dblayer_update_db_ext(ldbm_instance *inst, char *oldext, char *newext);
void dblayer_set_recovery_required(struct ldbminfo *li);

char *dblayer_get_home_dir(struct ldbminfo *li, int *dbhome);
char *dblayer_get_full_inst_dir(struct ldbminfo *li, ldbm_instance *inst,
                                char *buf, int buflen);
void autosize_import_cache(struct ldbminfo *li);


/*
 * dn2entry.c
 */
struct backentry *dn2entry(Slapi_Backend *be, const Slapi_DN *sdn, back_txn *txn, int    *err);
struct backentry *dn2entry_or_ancestor(Slapi_Backend *be, const Slapi_DN *sdn, Slapi_DN *ancestor, back_txn *txn, int *err);
struct backentry *dn2ancestor(Slapi_Backend *be,const Slapi_DN *sdn,Slapi_DN *ancestordn,back_txn *txn,int *err);
int get_copy_of_entry(Slapi_PBlock *pb, const entry_address *addr, back_txn *txn, int plock_parameter, int must_exist);
void done_with_pblock_entry(Slapi_PBlock *pb, int plock_parameter);

/*
 * uniqueid2entry.c
 */
struct backentry * uniqueid2entry(backend *be, const char *uniqueid, 
                                  back_txn *txn, int *err);

/*
 * filterindex.c
 */
IDList * filter_candidates( Slapi_PBlock *pb, backend *be, const char *base, Slapi_Filter *f, Slapi_Filter *nextf, int range, int *err );

/*
 * findentry.c
 */
struct backentry * find_entry2modify( Slapi_PBlock *pb, Slapi_Backend *be, const entry_address *addr, back_txn *txn );
struct backentry * find_entry( Slapi_PBlock *pb, Slapi_Backend *be, const entry_address *addr, back_txn *txn );
struct backentry * find_entry2modify_only( Slapi_PBlock *pb, Slapi_Backend *be, const entry_address *addr, back_txn *txn);
struct backentry * find_entry_only( Slapi_PBlock *pb, Slapi_Backend *be, const entry_address *addr, back_txn *txn);
int check_entry_for_referral(Slapi_PBlock *pb, Slapi_Entry *entry, char *matched, const char *callingfn);

/*
 * haschildren.c
 */
int has_children( struct ldbminfo *li, struct backentry *p, back_txn *txn, int *err );

/*
 * id2entry.c
 */
int id2entry_add( backend *be, struct backentry *e, back_txn *txn );
int id2entry_add_ext( backend *be, struct backentry *e, back_txn *txn, int encrypt );
int id2entry_delete( backend *be, struct backentry *e, back_txn *txn );
struct backentry * id2entry( backend *be, ID id, back_txn *txn, int *err );

/*
 * idl.c
 */
IDList * idl_alloc( NIDS nids );
void idl_free( IDList *idl );
NIDS idl_length(IDList *idl);
int idl_is_allids(IDList *idl);
int idl_append( IDList *idl, ID id);
void idl_insert(IDList **idl, ID id);
/*
 * idl_delete - delete an id from an id list.
 * returns  0   id deleted
 *      1   id deleted, first id in block has changed
 *      2   id deleted, block is empty
 *      3   id not there
 *      4   cannot delete from allids block
 */
int idl_delete( IDList **idl, ID id );
IDList * idl_allids( backend *be );
IDList * idl_fetch( backend *be, DB* db, DBT *key, DB_TXN *txn, struct attrinfo *a, int *err );
int idl_insert_key( backend *be, DB* db, DBT *key, ID id, DB_TXN *txn, struct attrinfo *a,int *disposition );
int idl_delete_key( backend *be, DB *db, DBT *key, ID id, DB_TXN *txn, struct attrinfo *a );
IDList * idl_intersection( backend *be, IDList *a, IDList *b );
IDList * idl_union( backend *be, IDList *a, IDList *b );
int idl_notin( backend *be, IDList *a, IDList *b , IDList **new_result);
ID idl_firstid( IDList *idl );
ID idl_nextid( IDList *idl, ID id );
int idl_init_private(backend *be, struct attrinfo *a);
int idl_release_private(struct attrinfo *a);

idl_iterator idl_iterator_init(const IDList *idl);
idl_iterator idl_iterator_increment(idl_iterator *i);
idl_iterator idl_iterator_decrement(idl_iterator *i);
ID idl_iterator_dereference(idl_iterator i, const IDList *idl);
ID idl_iterator_dereference_increment(idl_iterator *i, const IDList *idl);
size_t idl_sizeof(IDList *idl);
int idl_store_block(backend *be,DB *db,DBT *key,IDList *idl,DB_TXN *txn,struct attrinfo *a);
void idl_set_tune(int val);
int idl_get_tune();
size_t idl_get_allidslimit(struct attrinfo *a);
int idl_get_idl_new();
int idl_new_compare_dups(
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 3200
    DB *db,
#endif
    const DBT *a, 
    const DBT *b
);

/*
 * index.c
 */
int index_addordel_entry( backend *be, struct backentry *e, int flags, back_txn *txn );
int index_add_mods( backend *be, const LDAPMod**mods, struct backentry *olde, struct backentry *newe, back_txn *txn );
int index_addordel_string(backend *be, const char *type, const char *s, ID id, int flags, back_txn *txn);
int index_addordel_values_sv( backend *be, const char *type, Slapi_Value **vals, Slapi_Value **evals, ID id, int flags, back_txn *txn );
int index_addordel_values_ext_sv( backend *be, const char *type, Slapi_Value **vals, Slapi_Value **evals, ID id, int flags, back_txn *txn,int *idl_disposition, void *buffer_handle );
int id_array_init(Id_Array *new_guy, int size);

IDList* index_read( backend *be, char *type, const char* indextype, const struct berval* val, back_txn *txn, int *err );
IDList* index_read_ext( backend *be, char *type, const char* indextype, const struct berval* val, back_txn *txn, int *err, int *unindexed );
IDList* index_range_read( Slapi_PBlock *pb, backend *be, char *type, const char* indextype, int ftype, struct berval* val, struct berval* nextval, int range, back_txn *txn, int *err );
const char *encode( const struct berval* data, char buf[BUFSIZ] );

extern const char* indextype_PRESENCE;
extern const char* indextype_EQUALITY;
extern const char* indextype_APPROX;
extern const char* indextype_SUB;

int index_buffer_init(size_t size,int flags,void **h);
int index_buffer_flush(void *h,backend *be, DB_TXN *txn,struct attrinfo *a);
int index_buffer_terminate(void *h);

/*
 * instance.c
 */
int ldbm_instance_create(backend *be, char *name);
int ldbm_instance_create_default_indexes(backend *be);
int ldbm_instance_start(backend *be);
int ldbm_instance_stop(backend *be);
int ldbm_instance_startall(struct ldbminfo *li);
int ldbm_instance_stopall(struct ldbminfo *li);
ldbm_instance *ldbm_instance_find_by_name(struct ldbminfo *li, char *name);
int ldbm_instance_destroy(ldbm_instance *inst);

/*
 * ldif2ldbm.c
 */
int import_subcount_mother_init(import_subcount_stuff *mothers,ID parent_id, size_t count);
int import_subcount_mother_count(import_subcount_stuff *mothers,ID parent_id);
void import_subcount_stuff_init(import_subcount_stuff *stuff);
void import_subcount_stuff_term(import_subcount_stuff *stuff);
int update_subordinatecounts(backend *be,import_subcount_stuff *mothers, DB_TXN *txn);
void import_configure_index_buffer_size(size_t size);
size_t import_get_index_buffer_size();
int ldbm_back_fetch_incl_excl(Slapi_PBlock *pb, char ***include,
                  char ***exclude);
void ldbm_back_free_incl_excl(char **include, char **exclude);
int ldbm_back_ok_to_dump(const char *dn, char **include, char **exclude);
int ldbm_back_wire_import(Slapi_PBlock *pb);
void *factory_constructor(void *object, void *parent);
void factory_destructor(void *extension, void *object, void *parent);

/*
 * modify.c
 */
int modify_update_all(backend *be, Slapi_PBlock *pb,modify_context *mc,back_txn *txn);
void modify_init(modify_context *mc,struct backentry *old_entry);
int modify_apply_mods(modify_context *mc, Slapi_Mods *smods);
int modify_term(modify_context *mc,backend *be);
int modify_switch_entries(modify_context *mc,backend *be);

/*
 * add.c
 */
void add_update_entry_operational_attributes(struct backentry *ep, ID pid);
void add_update_entrydn_operational_attributes(struct backentry *ep);

/*
 * misc.c
 */
void ldbm_nasty(const char* str, int c, int err);
void ldbm_log_access_message(Slapi_PBlock *pblock,char *string);
int return_on_disk_full(struct ldbminfo  *li);
int ldbm_attribute_always_indexed(const char *attrtype);
void ldbm_destroy_instance_name(struct ldbminfo *li);
char *compute_entry_tombstone_dn(const char *entrydn, const char *uniqueid);
int instance_set_busy(ldbm_instance *inst);
int instance_set_busy_and_readonly(ldbm_instance *inst);
void instance_set_not_busy(ldbm_instance *inst);
void allinstance_set_busy(struct ldbminfo *li);
void allinstance_set_not_busy(struct ldbminfo *li);
int is_anyinstance_busy(struct ldbminfo *li);
int ldbm_delete_dirs(char *path);
int mkdir_p(char *dir, unsigned int mode);
int is_fullpath(char *path);
char get_sep(char *path);

/*
 * nextid.c
 */
ID next_id( backend *be );
void next_id_return( backend *be, ID id );
ID next_id_get( backend *be );
void id_internal_to_stored(ID,char*);
ID id_stored_to_internal(char*);
#if 0
int write_dbversion( ldbm_instance *inst );
#endif
void get_ids_from_disk(backend *be);
void get_both_ids( struct ldbminfo *li, ID *nextid, ID *nextid2index );

/*
 * backentry.c
 */
struct backentry *backentry_init( Slapi_Entry *e );
struct backentry *backentry_alloc();
void backentry_free( struct backentry **bep );
struct backentry *backentry_dup( struct backentry * );
void backentry_clear_entry( struct backentry * );
char *backentry_get_ndn(const struct backentry *e);
const Slapi_DN *backentry_get_sdn(const struct backentry *e);

/*
 * parents.c
 */
int parent_update_on_childchange(modify_context *mc,int op, size_t *numofchildren);

/*
 * perfctrs.c
 */
void perfctrs_wait(size_t milliseconds,perfctrs_private *priv,DB_ENV *db_env);
void perfctrs_init(struct ldbminfo *li,perfctrs_private **priv);
void perfctrs_terminate(perfctrs_private **priv);
void perfctrs_as_entry( Slapi_Entry *e, perfctrs_private *priv, DB_ENV *db_env );

/*
 * rmdb.c
 */
int ldbm_back_rmdb( Slapi_PBlock *pb );

/*
 * sort.c
 */

/*
 * Definitions for sort spec object
 */
struct sort_spec_thing
{
    char *type;
    char *matchrule; /* Matching rule string */
    int order; /* 0 == ascending, 1 == decending */
    struct sort_spec_thing *next;    /* Link to the next one */
    Slapi_PBlock *mr_pb; /* For matchrule indexing */
    value_compare_fn_type compare_fn; /* For non-matchrule indexing */
};
typedef struct sort_spec_thing sort_spec_thing;
typedef struct sort_spec_thing sort_spec;

void sort_spec_free(sort_spec *s);
int sort_candidates(backend *be, int lookthrough_limit, time_t time_up, Slapi_PBlock *pb, IDList *candidates, sort_spec_thing *sort_spec, char **sort_error_type) ;
int make_sort_response_control ( Slapi_PBlock *pb, int code, char *error_type);
int parse_sort_spec(struct berval *sort_spec_ber, sort_spec **ps);
struct berval* attr_value_lowest(struct berval **values, value_compare_fn_type compare_fn);
int sort_attr_compare(struct berval ** value_a, struct berval ** value_b, value_compare_fn_type compare_fn);
void sort_log_access(Slapi_PBlock *pb,sort_spec_thing *s,IDList *candidates);

/*
 * dbsize.c
 */
int ldbm_db_size( Slapi_PBlock *pb );

/*
 * external functions
 */
int ldbm_back_bind( Slapi_PBlock *pb );
int ldbm_back_unbind( Slapi_PBlock *pb );
int ldbm_back_search( Slapi_PBlock *pb );
int ldbm_back_compare( Slapi_PBlock *pb );
int ldbm_back_modify( Slapi_PBlock *pb );
int ldbm_back_modrdn( Slapi_PBlock *pb );
int ldbm_back_add( Slapi_PBlock *pb );
int ldbm_back_delete( Slapi_PBlock *pb );
int ldbm_back_abandon( Slapi_PBlock *pb );
int ldbm_back_config( Slapi_PBlock *pb );
int ldbm_back_close( Slapi_PBlock *pb );
int ldbm_back_cleanup( Slapi_PBlock *pb );
void ldbm_back_instance_set_destructor(void **arg);
int ldbm_back_flush( Slapi_PBlock *pb );
int ldbm_back_start( Slapi_PBlock *pb );
int ldbm_back_seq( Slapi_PBlock *pb );
int ldbm_back_ldif2ldbm( Slapi_PBlock *pb );
int ldbm_back_ldbm2ldif( Slapi_PBlock *pb );
int ldbm_back_ldbm2ldifalt( Slapi_PBlock *pb );
int ldbm_back_ldbm2index( Slapi_PBlock *pb );
int ldbm_back_archive2ldbm( Slapi_PBlock *pb );
int ldbm_back_ldbm2archive( Slapi_PBlock *pb );
int ldbm_back_upgradedb( Slapi_PBlock *pb );
int ldbm_back_next_search_entry( Slapi_PBlock *pb ); 
int ldbm_back_next_search_entry_ext( Slapi_PBlock *pb, int use_extension );
int ldbm_back_db_test( Slapi_PBlock *pb ); 
int ldbm_back_entry_release( Slapi_PBlock *pb, void *backend_info_ptr );
int ldbm_back_init( Slapi_PBlock *pb ); 

/*
 * monitor.c
 */

int ldbm_back_monitor_search(Slapi_PBlock *pb, Slapi_Entry* e,
    Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
int ldbm_back_monitor_instance_search(Slapi_PBlock *pb, Slapi_Entry *e,
    Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int ldbm_back_dbmonitor_search(Slapi_PBlock *pb, Slapi_Entry *e,
    Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);

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

int vlv_init(ldbm_instance *inst);
int vlv_remove_callbacks(ldbm_instance *inst);
const Slapi_Entry **vlv_get_search_entries();
struct vlvIndex* vlv_find_searchname(const char * name, backend *be);
struct vlvIndex* vlv_find_indexname(const char * name, backend *be);
char *vlv_getindexnames();
int vlv_search_build_candidate_list(Slapi_PBlock *pb, const Slapi_DN *base, int *rc, const sort_spec* sort_control, 
                    const struct vlv_request *vlv_request_control, IDList** candidates, struct vlv_response *vlv_response_control);
int vlv_update_index(struct vlvIndex* p, back_txn *txn, struct ldbminfo *li, Slapi_PBlock *pb, struct backentry* oldEntry, struct backentry* newEntry);
int vlv_update_all_indexes(back_txn *txn, backend *be, Slapi_PBlock *pb, struct backentry* oldEntry, struct backentry* newEntry);
int vlv_filter_candidates(backend *be, Slapi_PBlock *pb, const IDList *candidates, const Slapi_DN *base, int scope, Slapi_Filter *filter, IDList** filteredCandidates,int lookthrough_limit, time_t time_up);
int vlv_trim_candidates(backend *be, const IDList *candidates, const sort_spec* sort_control, const struct vlv_request *vlv_request_control, IDList** filteredCandidates,struct vlv_response *pResponse);
int vlv_parse_request_control(backend *be, struct berval *vlv_spec_ber, struct vlv_request* vlvp);
int vlv_make_response_control(Slapi_PBlock *pb, const struct vlv_response* vlvp);
void vlv_getindices(IFP callback_fn,void *param, backend *be);
void vlv_print_access_log(Slapi_PBlock *pb,struct vlv_request* vlvi, struct vlv_response *vlvo);
void vlv_grok_new_import_entry(const struct backentry *e, backend *be);
IDList *vlv_find_index_by_filter(struct backend *be, const char *base, 
                 Slapi_Filter *f);
int vlv_delete_search_entry(Slapi_PBlock *pb, Slapi_Entry* e, ldbm_instance *inst); 
void vlv_acquire_lock(backend *be);
void vlv_release_lock(backend *be);
int vlv_isvlv(char *filename);

/* 
 * Indexfile.c
 */
int indexfile_delete_all_keys(backend *be,char* type,back_txn *txn);
int indexfile_primary_modifyall(backend *be, LDAPMod **mods_to_perform,char **indexes_to_update,back_txn *txn);

/*
 * bedse.c
 */
#if 0
int bedse_init();
int bedse_search(Slapi_PBlock *pb);
struct dse_callback *bedse_register_callback(int operation, const Slapi_DN *base, int scope, const char *filter, int (*fn)(Slapi_PBlock *,Slapi_Entry *,Slapi_Entry *,int*,char*,void *), void *fn_arg);
void bedse_remove_callback(int operation, const Slapi_DN *base, int scope, const char *filter, int (*fn)(Slapi_PBlock *,Slapi_Entry *,Slapi_Entry *,int*,char*,void *));
int bedse_add_index_entry(int argc, char **argv);
#endif

/*
 * search.c
 */
Slapi_Filter* create_onelevel_filter(Slapi_Filter* filter, const struct backentry *e, int managedsait, Slapi_Filter** fid2kids, Slapi_Filter** focref, Slapi_Filter** fand, Slapi_Filter** forr);
Slapi_Filter* create_subtree_filter(Slapi_Filter* filter, int managedsait, Slapi_Filter** focref, Slapi_Filter** forr);
IDList* subtree_candidates(Slapi_PBlock *pb, backend *be, const char *base, const struct backentry *e, Slapi_Filter *filter, int managedsait, int *allids_before_scopingp, int *err);
void search_set_tune(struct ldbminfo *li,int val);
int search_get_tune(struct ldbminfo *li);

/*
 * matchrule.c
 */
int create_matchrule_indexer(Slapi_PBlock **pb,char* matchrule,char* type);
int destroy_matchrule_indexer(Slapi_PBlock *pb);
int matchrule_values_to_keys(Slapi_PBlock *pb,struct berval **input_values,struct berval ***output_values);
int matchrule_values_to_keys_sv(Slapi_PBlock *pb,Slapi_Value **input_values, Slapi_Value ***output_values);

/*
 * upgrade.c
 */
int check_db_version(struct ldbminfo *li, int *action);
int check_db_inst_version(ldbm_instance *inst);
int adjust_idl_switch(char *ldbmversion, struct ldbminfo *li);
int ldbm_upgrade(ldbm_instance *inst, int action);
int lookup_dbversion(char *dbversion, int flag);


/*
 * init.c
 */
int ldbm_attribute_always_indexed(const char *attrtype);

/*
 * dbversion.c
 */
int dbversion_write(struct ldbminfo *li, const char *dir, const char *dversion);
int dbversion_read(struct ldbminfo *li, const char *directory,
                   char *ldbmversion, char *dataversion);
int dbversion_exists(struct ldbminfo *li, const char *directory);

/* 
 * config_ldbm.c
 */
int ldbm_config_load_dse_info(struct ldbminfo *li);
void ldbm_config_setup_default(struct ldbminfo *li);
void ldbm_config_internal_set(struct ldbminfo *li, char *attrname, char *value);
void ldbm_instance_config_internal_set(ldbm_instance *inst, char *attrname, char *value);
void ldbm_instance_config_setup_default(ldbm_instance *inst);
int ldbm_instance_postadd_instance_entry_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
int ldbm_instance_add_instance_entry_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
int ldbm_instance_delete_instance_entry_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
int ldbm_instance_post_delete_instance_entry_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
/* Index config functions */
int ldbm_index_init_entry_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
int ldbm_instance_index_config_add_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg);
int ldbm_instance_index_config_delete_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg);
int ldbm_instance_index_config_modify_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
/* Attribute Encryption config functions */
int ldbm_attrcrypt_init_entry_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
int ldbm_instance_attrcrypt_config_add_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg);
int ldbm_instance_attrcrypt_config_delete_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg);
int ldbm_instance_attrcrypt_config_modify_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);

void replace_ldbm_config_value(char *conftype, char *val, struct ldbminfo *li);

/*
 * ancestorid.c
 */
int ldbm_ancestorid_create_index(backend *be);
int ldbm_ancestorid_index_entry(backend *be, struct backentry *e, int flags, back_txn *txn);
int ldbm_ancestorid_read(backend *be, back_txn *txn, ID id, IDList **idl);
int ldbm_ancestorid_move_subtree(
    backend        *be,
    const Slapi_DN    *olddn,
    const Slapi_DN    *newdn,
    ID            id,
    IDList        *subtree_idl,
    back_txn        *txn
);

#endif

/*
 * import-threads.c
 */
int dse_conf_backup(struct ldbminfo *li, char *destination_directory);
int dse_conf_verify(struct ldbminfo *li, char *src_dir, char *bename);

/*
 * ldbm_attrcrypt.c
 */
int attrcrypt_decrypt_entry(backend *be, struct backentry *e);
int attrcrypt_encrypt_entry_inplace(backend *be, const struct backentry *inout);
int attrcrypt_encrypt_entry(backend *be, const struct backentry *in, struct backentry **out);
int attrcrypt_encrypt_index_key(backend *be, struct attrinfo	*ai, const struct berval	*in, struct berval **out);
int attrcrypt_init(ldbm_instance *li);

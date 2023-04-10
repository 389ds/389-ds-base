/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * Copyright (C) 2020 William Brown <william@blackhats.net.au>
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#pragma once

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* slapi-private.h - external header file for some special plugins */

#ifndef _SLAPISTATE
#define _SLAPISTATE

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h> /* for time_t */
#include "nspr.h"
#include "portable.h"
#include "slapi-plugin.h"
/*
 * XXXmcs: we can stop including slapi-plugin-compat4.h once we stop using
 * deprecated functions internally.
 */
#include "slapi-plugin-compat4.h"

/* slapi platform abstraction functions. */
#include <slapi_pal.h>

/* Define our internal logging macro */
#define slapi_log_err(level, subsystem, fmt, ...)
#ifdef LDAP_ERROR_LOGGING
#undef slapi_log_err
#define slapi_log_err(level, subsystem, ...) slapi_log_error(level, subsystem, __VA_ARGS__)
#endif

/*
 * server shutdown status
 */
#define SLAPI_SHUTDOWN_SIGNAL   1
#define SLAPI_SHUTDOWN_DISKFULL 2
#define SLAPI_SHUTDOWN_EXIT     3

/* filter */
typedef enum _slapi_filter_flags_t {
    SLAPI_FILTER_LDAPSUBENTRY = 1,
    SLAPI_FILTER_TOMBSTONE = 2,
    SLAPI_FILTER_RUV = 4,
    SLAPI_FILTER_NORMALIZED_TYPE = 8,
    SLAPI_FILTER_NORMALIZED_VALUE = 16,
    SLAPI_FILTER_INVALID_ATTR_UNDEFINE = 32,
    SLAPI_FILTER_INVALID_ATTR_WARN = 64,
} slapi_filter_flags;

/*
    Optimized filter path. For example the following code was lifted from int.c (syntaxes plugin):

    if(ftype == LDAP_FILTER_EQUALITY_FAST) {
        tmp=(char *)slapi_ch_calloc(1,(sizeof(Slapi_Value)+sizeof(struct berval)+len+1));
        tmpval=(Slapi_Value *)tmp;
        tmpbv=(struct berval *)(tmp + sizeof(Slapi_Value));
        tmpbv->bv_val=(char *)tmp + sizeof(Slapi_Value) + (sizeof(struct berval));
        tmpbv->bv_len=len;
        tmpval->bvp=tmpbv;
        b = (unsigned char *)&num;
        memcpy(tmpbv->bv_val,b,len);
        (*ivals)=(Slapi_Value **)tmpval;
    }

    The following diagram helps explain the strategy.

    +---------------------------------------------------------------+
    |             Single contiguous allocated block                 |
    +------------------------+------------------------+-------------+
    | Slapi_Value            | struct berval          | octetstring |
    +----------------+-------+------------------------+-------------+
    | struct berval* |  ...  |  ...    | char *bv_val |  <value>    |
    |       v        |       |         |      v       |             |
    +-------+--------+-------+---------+------+-------+-------------+
            |                 ^               |        ^
            |_________________|               |________|

    The goal is to malloc one large chunk of memory up front and then manipulate the pointers to point
    into this chunk. We then can free the whole block at once by calling a single slapi_ch_free (see filterindex.c).

 */
#define LDAP_FILTER_EQUALITY_FAST 0xaaL
/*
 * Slapi_Mods and Slapi_Mod base structures.
 * Ideally, these would be moved to modutil.c and the structures would be
 * completely opaque to users of the slapi_mods_...() API.  But today some
 * plugins such as replication use these directly for efficiency reasons.
 */
typedef struct slapi_mods
{
    LDAPMod **mods;
    int num_elements;
    int num_mods;
    int iterator;
    int free_mods; /* flag to indicate that the mods were dynamically allocated and needs to be freed */
} slapi_mods;

typedef struct slapi_mod
{
    LDAPMod *mod;
    int num_elements;
    int num_values;
    int iterator;
    int free_mod; /* flag to inidicate that the mod was dynamically allocated and needs to be freed */
} slapi_mod;

void slapi_ch_free_ref(void *ptr);

/*
 * file I/O
 */
PRInt32 slapi_read_buffer(PRFileDesc *fd, void *buf, PRInt32 amount);
PRInt32 slapi_write_buffer(PRFileDesc *fd, void *buf, PRInt32 amount);
/* rename a file, overwriting the destfilename if it exists */
int slapi_destructive_rename(const char *srcfilename,
                             const char *destfilename);
/* make a copy of a file */
int slapi_copy(const char *srcfilename, const char *destfile);

/* CSN */

typedef struct csn CSN;
typedef unsigned char CSNType;
typedef struct csnset_node CSNSet;

#define _CSN_TSTAMP_STRSIZE 8
#define _CSN_SEQNUM_STRSIZE 4
#define _CSN_REPLID_STRSIZE 4
#define _CSN_SUBSEQNUM_STRSIZE 4
#define _CSN_VALIDCSN_STRLEN (_CSN_TSTAMP_STRSIZE + _CSN_SEQNUM_STRSIZE + \
                              _CSN_REPLID_STRSIZE + _CSN_SUBSEQNUM_STRSIZE)
#define CSN_STRSIZE (_CSN_VALIDCSN_STRLEN + 1)

#define CSN_TYPE_UNKNOWN             0x00
#define CSN_TYPE_NONE                0x01
#define CSN_TYPE_ATTRIBUTE_DELETED   0x03
#define CSN_TYPE_VALUE_UPDATED       0x04
#define CSN_TYPE_VALUE_DELETED       0x05
#define CSN_TYPE_VALUE_DISTINGUISHED 0x06

#define VALUE_NOTFOUND 1
#define VALUE_PRESENT  2
#define VALUE_DELETED  3

#define ATTRIBUTE_NOTFOUND 1
#define ATTRIBUTE_PRESENT  2
#define ATTRIBUTE_DELETED  3

/*
 * csn.c
 */
typedef PRUint16 ReplicaId;
/* max 2 byte unsigned int value */
#define MAX_REPLICA_ID 65535
/* we will use this value for the replica ID of read only replicas */
#define READ_ONLY_REPLICA_ID MAX_REPLICA_ID
CSN *csn_new(void);
CSN *csn_new_by_string(const char *s);
void csn_init_by_csn(CSN *csn1, const CSN *csn2);
void csn_init_by_string(CSN *csn, const char *s);
void csn_init(CSN *csn);
CSN *csn_dup(const CSN *csn);
void csn_free(CSN **csn);
void csn_set_replicaid(CSN *csn, ReplicaId rid);
void csn_set_time(CSN *csn, time_t csntime);
void csn_set_seqnum(CSN *csn, PRUint16 seqnum);
ReplicaId csn_get_replicaid(const CSN *csn);
time_t csn_get_time(const CSN *csn);
PRUint16 csn_get_seqnum(const CSN *csn);
PRUint16 csn_get_subseqnum(const CSN *csn);
char *csn_as_string(const CSN *csn, PRBool replicaIdOrder, char *ss); /* WARNING: ss must be CSN_STRSIZE bytes, or NULL. */
int csn_is_equal(const CSN *csn1, const CSN *csn2);
int csn_compare(const CSN *csn1, const CSN *csn2);
int csn_compare_ext(const CSN *csn1, const CSN *csn2, unsigned int flags);
#define CSN_COMPARE_SKIP_SUBSEQ 0x1
time_t csn_time_difference(const CSN *csn1, const CSN *csn2);
size_t csn_string_size(void);
char *csn_as_attr_option_string(CSNType t, const CSN *csn, char *ss);
const CSN *csn_max(const CSN *csn1, const CSN *csn2);
/* this function allows to expand a csn into a set of csns.
   The sequence is derived by adding a sequence number to the base csn
   passed to it. This is useful when a single client operation needs to be
   expanded into multiple operations. For instance, subtree move operation
   is split into a sequence of adds and deletes with each add and delete assigned
   a csn from the set.*/
int csn_increment_subsequence(CSN *csn);

void csnplFreeCSNPL_CTX(void *arg);
/*
 * csnset.c
 */
void csnset_add_csn(CSNSet **csnset, CSNType t, const CSN *csn);
void csnset_insert_csn(CSNSet **csnset, CSNType t, const CSN *csn);
void csnset_update_csn(CSNSet **csnset, CSNType t, const CSN *csn);
void csnset_free(CSNSet **csnset);
const CSN *csnset_get_csn_of_type(const CSNSet *csnset, CSNType t);
void csnset_purge(CSNSet **csnset, const CSN *csnUpTo);
size_t csnset_string_size(CSNSet *csnset);
size_t csnset_size(CSNSet *csnset);
CSNSet *csnset_dup(const CSNSet *csnset);
void csnset_as_string(const CSNSet *csnset, char *s);
void csnset_remove_csn(CSNSet **csnset, CSNType t);
const CSN *csnset_get_last_csn(const CSNSet *csnset);
int csnset_contains(const CSNSet *csnset, const CSN *csn);
const CSN *csnset_get_previous_csn(const CSNSet *csnset, const CSN *csn);
void *csnset_get_first_csn(const CSNSet *csnset, CSN **csn, CSNType *t);
void *csnset_get_next_csn(const CSNSet *csnset, void *cookie, CSN **csn, CSNType *t);

/*
 * csngen.c
 */

/* error codes returned from CSN generation routines */
enum
{
    CSN_SUCCESS = 0,
    CSN_MEMORY_ERROR,      /* memory allocation failed */
    CSN_LIMIT_EXCEEDED,    /* timestamp is way out of sync */
    CSN_INVALID_PARAMETER, /* invalid function argument */
    CSN_INVALID_FORMAT,    /* invalid state format */
    CSN_LDAP_ERROR,        /* LDAP operation failed */
    CSN_NSPR_ERROR,        /* NSPR API failure */
    CSN_TIME_ERROR         /* Error generating new CSN due to clock failure */
};

typedef struct csngen CSNGen;

/* allocates new csn generator */
CSNGen *csngen_new(ReplicaId rid, Slapi_Attr *state);
/* frees csn generator data structure */
void csngen_free(CSNGen **gen);
/* generates new csn. If notify is non-zero, the generator calls
   "generate" functions registered through csngen_register_callbacks call */
int csngen_new_csn(CSNGen *gen, CSN **csn, PRBool notify);
/* this function should be called for csns generated with non-zero notify
   that were unused because the corresponding operation was aborted.
   The function calls "abort" functions registered through
   csngen_register_callbacks call */
void csngen_abort_csn(CSNGen *gen, const CSN *csn);
/* this function should be called when a remote CSN for the same part of
   the dit becomes known to the server (for instance, as part of RUV during
   replication session. In response, the generator would adjust its notion
   of time so that it does not generate smaller csns */
int csngen_adjust_time(CSNGen *gen, const CSN *csn);
/* returns PR_TRUE if the csn was generated by this generator and
   PR_FALSE otherwise. */
void csngen_rewrite_rid(CSNGen *gen, ReplicaId rid);

PRBool csngen_is_local_csn(const CSNGen *gen, const CSN *csn);

/* returns current state of the generator so that it can be saved in the DIT */
int csngen_get_state(const CSNGen *gen, Slapi_Mod *state);

typedef void (*GenCSNFn)(const CSN *newCsn, void *cbData);
typedef void (*AbortCSNFn)(const CSN *delCsn, void *cbData);
/* registers callbacks to be called when csn is created or aborted */
void *csngen_register_callbacks(CSNGen *gen, GenCSNFn genFn, void *genArg, AbortCSNFn abortFn, void *abortArg);
/* unregisters callbacks registered via call to csngenRegisterCallbacks */
void csngen_unregister_callbacks(CSNGen *gen, void *cookie);

/* debugging function */
void csngen_dump_state(const CSNGen *gen, int severity);

/* this function tests csn generator */
void csngen_test(void);

/*
 * State storage management routines
 *
 *
 */

/*
 * attr_value_find_wsi looks for a particular value (rather, the berval
 * part of the slapi_value v) and returns it in "value". The function
 * returns VALUE_PRESENT, VALUE_DELETED, or VALUE_NOTFOUND.
 */
int attr_value_find_wsi(Slapi_Attr *a, const struct berval *bval, Slapi_Value **value);

/*
 * entry_attr_find_wsi takes an entry and a type and looks for the
 * attribute. If the attribute is found on the list of existing attributes,
 * it is returned in "a" and the function returns ATTRIBUTE_PRESENT. If the attribute is
 * found on the deleted list, "a" is set and the function returns ATTRIBUTE_DELETED.
 * If the attribute is not found on either list, the function returns ATTRIBUTE_NOTFOUND.
 */
int entry_attr_find_wsi(Slapi_Entry *e, const char *type, Slapi_Attr **a);

/*
 * entry_add_present_attribute_wsi adds an attribute to the entry.
 */
int entry_add_present_attribute_wsi(Slapi_Entry *e, Slapi_Attr *a);

/*
 * entry_add_deleted_attribute_wsi adds a deleted attribute to the entry.
 */
int entry_add_deleted_attribute_wsi(Slapi_Entry *e, Slapi_Attr *a);

/*
 * entry_apply_mods_wsi is similar to entry_apply_mods. It also
 * handles the state storage information. "csn" is the CSN associated with
 * this modify operation.
 */
int entry_apply_mods_wsi(Slapi_Entry *e, Slapi_Mods *smods, const CSN *csn, int urp);
int entry_first_deleted_attribute(const Slapi_Entry *e, Slapi_Attr **a);
int entry_next_deleted_attribute(const Slapi_Entry *e, Slapi_Attr **a);

/* entry.c */
int entry_apply_mods(Slapi_Entry *e, LDAPMod **mods);
int is_type_protected(const char *type);
int entry_apply_mods_ignore_error(Slapi_Entry *e, LDAPMod **mods, int ignore_error);
int slapi_entries_diff(Slapi_Entry **old_entries, Slapi_Entry **new_entries, int testall, const char *logging_prestr, const int force_update, void *plg_id);
void set_attr_to_protected_list(char *attr, int flag);

/* entrywsi.c */
int32_t entry_assign_operation_csn(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *parententry, CSN **opcsn);
const CSN *entry_get_maxcsn(const Slapi_Entry *entry);
void entry_set_maxcsn(Slapi_Entry *entry, const CSN *csn);
const CSN *entry_get_dncsn(const Slapi_Entry *entry);
const CSNSet *entry_get_dncsnset(const Slapi_Entry *entry);
int entry_add_dncsn(Slapi_Entry *entry, const CSN *csn);
int entry_set_csn(Slapi_Entry *entry, const CSN *csn);
void entry_purge_state_information(Slapi_Entry *entry, const CSN *csnUpto);
void entry_add_rdn_csn(Slapi_Entry *e, const CSN *csn);
/* this adds a csn to the entry's e_dncsnset but makes sure the set is in increasing csn order */
#define ENTRY_DNCSN_INCREASING 0x1 /* for flags below */
int entry_add_dncsn_ext(Slapi_Entry *entry, const CSN *csn, PRUint32 flags);
const CSN *entry_get_deletion_csn(Slapi_Entry *entry);

/* attr.c */
Slapi_Attr *slapi_attr_init_locking_optional(Slapi_Attr *a, const char *type, PRBool use_lock);
Slapi_Attr *slapi_attr_init_nosyntax(Slapi_Attr *a, const char *type);
int slapi_attr_init_syntax(Slapi_Attr *a);
int attr_set_csn(Slapi_Attr *a, const CSN *csn);
int attr_set_deletion_csn(Slapi_Attr *a, const CSN *csn);
const CSN *attr_get_deletion_csn(const Slapi_Attr *a);
int attr_first_deleted_value(Slapi_Attr *a, Slapi_Value **v);
int attr_next_deleted_value(Slapi_Attr *a, int hint, Slapi_Value **v);
void attr_purge_state_information(Slapi_Entry *entry, Slapi_Attr *attr, const CSN *csnUpto);
Slapi_Value **attr_get_present_values(const Slapi_Attr *a);
int attr_add_deleted_value(Slapi_Attr *a, const Slapi_Value *v);

/* value.c */
Slapi_Value *value_new(const struct berval *bval, CSNType t, const CSN *csn);
Slapi_Value *value_init(Slapi_Value *v, const struct berval *bval, CSNType t, const CSN *csn);
void value_done(Slapi_Value *v);
Slapi_Value *value_update_csn(Slapi_Value *value, CSNType t, const CSN *csn);
Slapi_Value *value_add_csn(Slapi_Value *value, CSNType t, const CSN *csn);
const CSN *value_get_csn(const Slapi_Value *value, CSNType t);
const CSNSet *value_get_csnset(const Slapi_Value *value);
Slapi_Value *value_remove_csn(Slapi_Value *value, CSNType t);
int value_contains_csn(const Slapi_Value *value, CSN *csn);
int value_dn_normalize_value(Slapi_Value *value);

/* dn.c */
/* this functions should only be used for dns allocated on the stack */
Slapi_DN *slapi_sdn_init(Slapi_DN *sdn);
Slapi_DN *slapi_sdn_init_dn_byref(Slapi_DN *sdn, const char *dn);
Slapi_DN *slapi_sdn_init_dn_byval(Slapi_DN *sdn, const char *dn);
Slapi_DN *slapi_sdn_init_dn_passin(Slapi_DN *sdn, const char *dn);
Slapi_DN *slapi_sdn_init_ndn_byref(Slapi_DN *sdn, const char *dn);
Slapi_DN *slapi_sdn_init_ndn_byval(Slapi_DN *sdn, const char *dn);
Slapi_DN *slapi_sdn_init_normdn_byref(Slapi_DN *sdn, const char *dn);
Slapi_DN *slapi_sdn_init_normdn_byval(Slapi_DN *sdn, const char *dn);
Slapi_DN *slapi_sdn_init_normdn_ndn_passin(Slapi_DN *sdn, const char *dn);
Slapi_DN *slapi_sdn_init_normdn_passin(Slapi_DN *sdn, const char *dn);
char *slapi_dn_normalize_original(char *dn);
char *slapi_dn_normalize_case_original(char *dn);
int32_t ndn_cache_init(void);
void ndn_cache_destroy(void);
int ndn_cache_started(void);
void ndn_cache_get_stats(uint64_t *hits, uint64_t *tries, uint64_t *size, uint64_t *max_size, uint64_t *thread_size, uint64_t *evicts, uint64_t *slots, uint64_t *count);
void ndn_cache_inc_import_task(void);
void ndn_cache_dec_import_task(void);
#define NDN_DEFAULT_SIZE 20971520 /* 20mb - size of normalized dn cache */

/* filter.c */
int filter_flag_is_set(const Slapi_Filter *f, unsigned char flag);
char *slapi_filter_to_string(const Slapi_Filter *f, char *buffer, size_t bufsize);
char *slapi_filter_to_string_internal(const struct slapi_filter *f, char *buf, size_t *bufsize);
void slapi_filter_optimise(Slapi_Filter *f);

/* operation.c */

#define OP_FLAG_PS 0x000001
#define OP_FLAG_PS_CHANGESONLY 0x000002
#define OP_FLAG_GET_EFFECTIVE_RIGHTS 0x000004
#define OP_FLAG_REPLICATED 0x000008             /* A Replicated Operation */
#define OP_FLAG_REPL_FIXUP 0x000010             /* A Fixup Operation,        \
                                                 * generated as a consequence \
                                                 * of a Replicated Operation. \
                                                 */
#define OP_FLAG_INTERNAL SLAPI_OP_FLAG_INTERNAL /* 0x000020 */
#define OP_FLAG_ACTION_LOG_ACCESS 0x000040
#define OP_FLAG_ACTION_LOG_AUDIT 0x000080
#define OP_FLAG_ACTION_SCHEMA_CHECK 0x000100
#define OP_FLAG_ACTION_LOG_CHANGES 0x000200
#define OP_FLAG_ACTION_INVOKE_FOR_REPLOP 0x000400
#define OP_FLAG_NEVER_CHAIN SLAPI_OP_FLAG_NEVER_CHAIN         /* 0x000800 */
#define OP_FLAG_TOMBSTONE_ENTRY SLAPI_OP_FLAG_TOMBSTONE_ENTRY /* 0x001000 */
#define OP_FLAG_RESURECT_ENTRY 0x002000
#define OP_FLAG_CENOTAPH_ENTRY 0x004000
#define OP_FLAG_ACTION_NOLOG 0x008000            /* Do not log the entry in \
                                                  * audit log or change log  \
                                                  */
#define OP_FLAG_SKIP_MODIFIED_ATTRS 0x010000     /* Do not update the      \
                                                  * modifiersname,          \
                                                  * modifiedtimestamp, etc. \
                                                  * attributes              \
                                                  */
#define OP_FLAG_REPL_RUV 0x020000                /* Flag to tell to the backend \
                                                  * that the entry to be added/  \
                                                  * modified is RUV. This info   \
                                                  * is used to skip VLV op.      \
                                                  * (see #329951)                \
                                                  */
#define OP_FLAG_PAGED_RESULTS 0x040000           /* simple paged results */
#define OP_FLAG_SERVER_SIDE_SORTING 0x080000     /* server side sorting  */
#define OP_FLAG_REVERSE_CANDIDATE_ORDER 0x100000 /* reverse the search candidate list */
#define OP_FLAG_NEVER_CACHE 0x200000             /* never keep the entry in cache */
#define OP_FLAG_TOMBSTONE_FIXUP 0x400000         /* operation is tombstone fixup op */
#define OP_FLAG_BULK_IMPORT 0x800000             /* operation is bulk import */
#define OP_FLAG_NOOP 0x01000000                  /* operation results from urp and
                                                  * should be ignored */
#define OP_FLAG_ACTION_SKIP_PWDPOLICY 0x02000000 /* Skip applying pw policy rules to the password
                                                  * change operation, as it's from an upgrade on
                                                  * bind rather than a normal password change */
#define OP_FLAG_SUBENTRIES_FALSE 0x04000000      /* Normal entries are visible and subentries are not */
#define OP_FLAG_SUBENTRIES_TRUE 0x08000000       /* Subentries are visible and normal entries are not */

/* reverse search states */
#define REV_STARTED 1
#define LAST_REV_ENTRY 2

CSN *operation_get_csn(Slapi_Operation *op);
void operation_set_csn(Slapi_Operation *op, CSN *csn);
void operation_set_flag(Slapi_Operation *op, int flag);
void operation_clear_flag(Slapi_Operation *op, int flag);
int operation_is_flag_set(Slapi_Operation *op, int flag);
unsigned long operation_get_type(Slapi_Operation *op);
LDAPMod **copy_mods(LDAPMod **orig_mods);

/* Structures use to collect statistics per operation */
/* used for LDAP_STAT_READ_INDEX */
struct component_keys_lookup
{
    char *index_type;
    char *attribute_type;
    char *key;
    int id_lookup_cnt;
    struct component_keys_lookup *next;
};
typedef struct op_search_stat
{
    struct component_keys_lookup *keys_lookup;
    struct timespec keys_lookup_start;
    struct timespec keys_lookup_end;
} Op_search_stat;

/* structure store in the operation extension */
typedef struct op_stat
{
    Op_search_stat *search_stat;
} Op_stat;

void op_stat_init(void);
Op_stat *op_stat_get_operation_extension(Slapi_PBlock *pb);
void op_stat_set_operation_extension(Slapi_PBlock *pb, Op_stat *op_stat);

/*
 * From ldap.h
 * #define LDAP_MOD_ADD            0x00
 * #define LDAP_MOD_DELETE         0x01
 * #define LDAP_MOD_REPLACE        0x02
 * #define LDAP_MOD_INCREMENT      0x03 -- Openldap extension
 * #define LDAP_MOD_BVALUES        0x80
 */
#define LDAP_MOD_IGNORE 0x100

/* dl.c */
typedef struct datalist DataList;

typedef int (*CMPFN)(const void *el1, const void *el2);
typedef void (*FREEFN)(void **);
DataList *dl_new(void);
void dl_free(DataList **dl);
void dl_init(DataList *dl, int init_alloc);
void dl_cleanup(DataList *dl, FREEFN freefn);
void dl_add(DataList *dl, void *element);
void dl_add_index(DataList *dl, void *element, int index);
void *dl_replace(const DataList *dl, const void *elementOld, void *elementNew, CMPFN cmpfn, FREEFN freefn);
void *dl_get_first(const DataList *dl, int *cookie);
void *dl_get_next(const DataList *dl, int *cookie);
void *dl_get_prev(const DataList *dl, int *cookie);
void *dl_get(const DataList *dl, const void *element, CMPFN cmpfn);
void *dl_delete(DataList *dl, const void *element, CMPFN cmpfn, FREEFN freefn);
int dl_get_count(const DataList *dl);

struct ava
{
    char *ava_type;
    struct berval ava_value; /* JCM SLAPI_VALUE! */
    void *ava_private;       /* data private to syntax handler */
};

typedef enum {
    FILTER_TYPE_SUBSTRING,
    FILTER_TYPE_AVA,
    FILTER_TYPE_PRES
} filter_type_t;

/*
 * vattr entry routines.
 * vattrcache private (for the moment)
 */
#define SLAPI_ENTRY_VATTR_NOT_RESOLVED    -1
#define SLAPI_ENTRY_VATTR_RESOLVED_ABSENT -2
#define SLAPI_ENTRY_VATTR_RESOLVED_EXISTS  0

int slapi_entry_vattrcache_merge_sv(Slapi_Entry *e, const char *type, Slapi_ValueSet *vals, int buffer_flags);
int slapi_entry_vattrcache_find_values_and_type_ex(const Slapi_Entry *e,
                                                   const char *type,
                                                   Slapi_ValueSet ***results,
                                                   char ***actual_type_name);
SLAPI_DEPRECATED int
slapi_entry_vattrcache_find_values_and_type(const Slapi_Entry *e,
                                            const char *type,
                                            Slapi_ValueSet **results,
                                            char **actual_type_name);
int slapi_entry_vattrcache_findAndTest(const Slapi_Entry *e, const char *type, Slapi_Filter *f, filter_type_t filter_type, int *rc);

int slapi_vattrcache_iscacheable(const char *type);
void slapi_vattrcache_cache_all(void);
void slapi_vattrcache_cache_none(void);

int vattr_test_filter(Slapi_PBlock *pb,
                      /* Entry we're interested in */ Slapi_Entry *e,
                      Slapi_Filter *f,
                      filter_type_t filter_type,
                      char *type);

/* filter routines */

int test_substring_filter(Slapi_PBlock *pb, Slapi_Entry *e, struct slapi_filter *f, int verify_access, int only_check_access, int *access_check_done);
int test_ava_filter(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Attr *a, struct ava *ava, int ftype, int verify_access, int only_check_access, int *access_check_done);
int test_presence_filter(Slapi_PBlock *pb, Slapi_Entry *e, char *type, int verify_access, int only_check_access, int *access_check_done);

/* this structure allows to address entry by dn or uniqueid */
typedef struct entry_address
{
    char *udn; /* unnormalized dn */
    char *uniqueid;
    Slapi_DN *sdn;
} entry_address;

/*
 * LDAP Operation input parameters.
 */
typedef struct slapi_operation_parameters
{
    unsigned long operation_type; /* SLAPI_OPERATION_ADD, SLAPI_OPERATION_MODIFY ... */
    entry_address target_address; /* address of target entry */
    CSN *csn;                     /* The Change Sequence Number assigned to this operation. */

    LDAPControl **request_controls; /* array v3 LDAPMessage controls  */

    union
    {
        struct add_parameters
        {
            struct slapi_entry *target_entry;
            char *parentuniqueid;
        } p_add;

        struct bind_parameters
        {
            ber_tag_t bind_method;
            struct berval *bind_creds;
            char *bind_saslmechanism;          /* v3 sasl mechanism name */
            struct berval *bind_ret_saslcreds; /* v3 serverSaslCreds */
        } p_bind;

        struct compare_parameters
        {
            struct ava compare_ava;
        } p_compare;

        struct modify_parameters
        {
            LDAPMod **modify_mods;
        } p_modify;

        struct modrdn_parameters
        {
            char *modrdn_newrdn;
            int modrdn_deloldrdn;
            entry_address modrdn_newsuperior_address; /* address of the superior entry */
            LDAPMod **modrdn_mods;                    /* modifiers name and timestamp */
        } p_modrdn;

        struct search_parameters
        {
            int search_scope;
            int search_deref;
            int search_sizelimit;
            int search_timelimit;
            struct slapi_filter *search_filter;
            struct slapi_filter *search_filter_intended;
            char *search_strfilter;
            char **search_attrs;
            int search_attrsonly;
            int search_is_and;
            char **search_gerattrs;
        } p_search;

        struct abandon_parameters
        {
            int abandon_targetmsgid;
        } p_abandon;

        struct extended_parameters
        {
            char *exop_oid;
            struct berval *exop_value;
        } p_extended;
    } p;
} slapi_operation_parameters;

struct slapi_operation_parameters *operation_parameters_new(void);
struct slapi_operation_parameters *operation_parameters_dup(struct slapi_operation_parameters *sop);
void operation_parameters_done(struct slapi_operation_parameters *sop);
void operation_parameters_free(struct slapi_operation_parameters **sop);


/*
 * errormap.c
 */
char *slapd_pr_strerror(const PRErrorCode prerrno);
const char *slapd_system_strerror(const int syserrno);
const char *slapd_versatile_strerror(const PRErrorCode prerrno);


/*
 * localhost.c
 */
char *get_localhost_DNS(void);
/* Return the fully-qualified DNS name of this machine.
   The caller should _not_ free this pointer. */
char *get_localhost_DN(void);

/*
 * Reference-counted objects
 */
typedef void (*FNFree)(void **);
typedef struct object Object;
Object *object_new(void *user_data, FNFree destructor);
void object_acquire(Object *o);
void object_release(Object *o);
void *object_get_data(Object *o);

/* Sets of reference-counted objects */
#define OBJSET_SUCCESS 0
#define OBJSET_ALREADY_EXISTS 1
#define OBJSET_NO_SUCH_OBJECT 2
typedef int (*CMPFn)(Object *set, const void *name);
typedef struct objset Objset;
Objset *objset_new(FNFree objset_destructor);
void objset_delete(Objset **set);
int objset_add_obj(Objset *set, Object *object);
Object *objset_find(Objset *set, CMPFn compare_fn, const void *name);
int objset_remove_obj(Objset *set, Object *object);
Object *objset_first_obj(Objset *set);
Object *objset_next_obj(Objset *set, Object *previous);
int objset_is_empty(Objset *set);
int objset_size(Objset *set);

/* backend management */
typedef struct index_config
{
    char *attr_name;  /* attr name: dn, cn, etc. */
    char *index_type; /* space terminated list of indexes;
                         possible types: "eq" "sub" "pres" "approx" */
    int system;       /* marks this index as system */
} IndexConfig;

void be_set_sizelimit(Slapi_Backend *be, int sizelimit);
void be_set_pagedsizelimit(Slapi_Backend *be, int sizelimit);
void be_set_timelimit(Slapi_Backend *be, int timelimit);
int be_isdeleted(const Slapi_Backend *be);


/* used by mapping tree to delay sending of result code when several
 * backend are parsed
 */
void slapi_set_ldap_result(Slapi_PBlock *pb, int err, char *matched, char *text, int nentries, struct berval **urls);
void slapi_send_ldap_result_from_pb(Slapi_PBlock *pb);

/* mapping tree utility functions */
typedef struct mt_node mapping_tree_node;
mapping_tree_node *slapi_get_mapping_tree_node_by_dn(const Slapi_DN *dn);
char *slapi_get_mapping_tree_node_configdn(const Slapi_DN *root);
Slapi_DN *slapi_get_mapping_tree_node_configsdn(const Slapi_DN *root);
const Slapi_DN *slapi_get_mapping_tree_node_root(const mapping_tree_node *node);
const char *slapi_get_mapping_tree_config_root(void);
Slapi_Backend *slapi_mapping_tree_find_backend_for_sdn(Slapi_DN *sdn);
/* possible flags to check for */
#define SLAPI_MTN_LOCAL    0x1
#define SLAPI_MTN_PRIVATE  0x2
#define SLAPI_MTN_READONLY 0x4
PRBool slapi_mapping_tree_node_is_set(const mapping_tree_node *node,
                                      PRUint32 flag);
Slapi_DN *slapi_mtn_get_dn(mapping_tree_node *node);
int slapi_mapping_tree_select_and_check(Slapi_PBlock *pb, char *newdn, Slapi_Backend **be, Slapi_Entry **referral, char *errorbuf, size_t ebuflen);
int slapi_mapping_tree_select_all(Slapi_PBlock *pb, Slapi_Backend **be_list, Slapi_Entry **referral_list, char *errorbuf, size_t ebuflen);
void slapi_mapping_tree_free_all(Slapi_Backend **be_list,
                                 Slapi_Entry **referral_list);

/* Mapping Tree */
int slapi_mapping_tree_select(Slapi_PBlock *pb, Slapi_Backend **be, Slapi_Entry **referral, char *error_string, size_t ebuflen);
char **slapi_mtn_get_referral(const Slapi_DN *sdn);
int slapi_mtn_set_referral(const Slapi_DN *sdn, char **referral);
int slapi_mtn_set_state(const Slapi_DN *sdn, char *state);
char *slapi_mtn_get_state(const Slapi_DN *sdn);
void slapi_mtn_be_set_readonly(Slapi_Backend *be, int readonly);
void slapi_mtn_be_stopping(Slapi_Backend *be);
void slapi_mtn_be_started(Slapi_Backend *be);
void slapi_mtn_be_disable(Slapi_Backend *be);
void slapi_mtn_be_enable(Slapi_Backend *be);
const char *slapi_mtn_get_backend_name(const Slapi_DN *sdn);

void slapi_be_stopping(Slapi_Backend *be);
void slapi_be_free(Slapi_Backend **be);
void slapi_be_Rlock(Slapi_Backend *be);
void slapi_be_Wlock(Slapi_Backend *be);
void slapi_be_Unlock(Slapi_Backend *be);

/* components */
struct slapi_componentid
{
    char *sci_magic;
    const struct slapdplugin *sci_plugin;
    char *sci_component_name;
};

struct slapi_componentid *
generate_componentid(struct slapdplugin *pp, char *name);
void release_componentid(struct slapi_componentid *id);
struct slapi_componentid *plugin_get_default_component_id(void);

/* interface for component mgmt */
/* Well-known components DNs    */
/* Should be documented somehow for the chaining backend */

#define COMPONENT_BASE_DN "cn=components,cn=config"
#define COMPONENT_ROLES "cn=roles," COMPONENT_BASE_DN
#define COMPONENT_RESLIMIT "cn=resource limits," COMPONENT_BASE_DN
#define COMPONENT_PWPOLICY "cn=password policy," COMPONENT_BASE_DN
#define COMPONENT_CERT_AUTH "cn=certificate-based authentication," COMPONENT_BASE_DN
#define COMPONENT_SASL "cn=sasl," COMPONENT_BASE_DN

/* Component names for logging */
#define SLAPI_COMPONENT_NAME_NSPR    "Netscape Portable Runtime"
#define SLAPI_COMPONENT_NAME_LDAPSDK "LDAP sdk"

/* loads the policies related to the replication of the schema */
int slapi_schema_load_repl_policies(void);
void slapi_schema_get_repl_entries(char **repl_schema_top, char **repl_schema_supplier, char **repl_schema_consumer, char **default_supplier_policy, char **default_consumer_policy);
/* return the list of attr defined in the schema matching the attr flags */
char **slapi_schema_list_attribute_names(unsigned long flag);
/* return the list of attributes belonging to the objectclass */
char **slapi_schema_list_objectclass_attributes(const char *ocname_or_oid,
                                                PRUint32 flags);
char *slapi_schema_get_superior_name(const char *ocname_or_oid);

CSN *dup_global_schema_csn(void);

/* misc function for the chaining backend */
#define CHAIN_ROOT_UPDATE_REJECT   0
#define CHAIN_ROOT_UPDATE_LOCAL    1
#define CHAIN_ROOT_UPDATE_REFERRAL 2

char *slapi_get_rootdn(void); /* return the directory manager dn in use */

/* plugin interface to bulk import */
/* This function initiates bulk import. The pblock must contain
   SLAPI_LDIF2DB_GENERATE_UNIQUEID -- currently always set to TIME_BASED
   SLAPI_CONNECTION -- connection over which bulk import is coming
   SLAPI_BACKEND -- the backend being imported
   or
   SLAPI_TARGET_DN that contains root of the imported area.
   The function returns LDAP_SUCCESS or LDAP error code
*/
int slapi_start_bulk_import(Slapi_PBlock *pb);

/* This function adds an entry to the bulk import. The pblock must contain
   SLAPI_CONNECTION -- connection over which bulk import is coming
   SLAPI_BACKEND -- optional backend pointer; if missing computed based on entry dn
   The function returns LDAP_SUCCESS or LDAP error code
*/
int slapi_import_entry(Slapi_PBlock *pb, Slapi_Entry *e);

/* This function stops bulk import. The pblock must contain
   SLAPI_CONNECTION -- connection over which bulk import is coming
   SLAPI_BACKEND -- the backend being imported
   or
   SLAPI_TARGET_DN that contains root of the imported area.
   The function returns LDAP_SUCCESS or LDAP error code
*/
int slapi_stop_bulk_import(Slapi_PBlock *pb);

/* allows plugins to close inbound connection */
void slapi_disconnect_server(Slapi_Connection *conn);

/* functions to look up instance names by suffixes (backend_manager.c) */
int slapi_lookup_instance_name_by_suffixes(char **included,
                                           char **excluded,
                                           char ***instances);
int slapi_lookup_instance_name_by_suffix(char *suffix,
                                         char ***suffixes,
                                         char ***instances,
                                         int isexact);

/* begin and end the task subsystem */
void task_init(void);
void task_cancel_all(void);
void task_shutdown(void);
void task_cleanup(void);

/* for reversible encyrption */
#define SLAPI_MB_CREDENTIALS "nsmultiplexorcredentials"
#define SLAPI_REP_CREDENTIALS "nsds5ReplicaCredentials"
int pw_rever_encode(Slapi_Value **vals, char *attr_name);
int pw_rever_decode(char *cipher, char **plain, const char *attr_name);

int32_t update_pw_encoding(Slapi_PBlock *orig_pb, Slapi_Entry *e, Slapi_DN *sdn, char *cleartextpassword);


/* config routines */

int slapi_config_get_readonly(void);
int slapi_config_get_unhashed_pw_switch(void);

/*
 * charray.c
 */
void charray_add(char ***a, char *s);
void charray_merge(char ***a, char **s, int copy_strs);
void charray_merge_nodup(char ***a, char **s, int copy_strs);
void charray_free(char **array);
int charray_inlist(char **a, char *s);
int charray_utf8_inlist(char **a, char *s);
char **charray_dup(char **a);
int charray_remove(char **a, const char *s, int freeit);
char **cool_charray_dup(char **a);
void cool_charray_free(char **array);
void charray_subtract(char **a, char **b, char ***c);
char **charray_intersection(char **a, char **b);
int charray_get_index(char **array, char *s);
int charray_normdn_add(char ***chararray, char *dn, char *errstr);
void charray_assert_present(char ***a, char *s);


/******************************************************************************
 * value array routines.
 *
 * It is unclear if these should ever be public, but today they are used by
 * some plugins.  They would need to be renamed to have a slapi_ prefix at
 * the very least before we make them public.
 */
void valuearray_add_value(Slapi_Value ***vals, const Slapi_Value *addval);
void valuearray_add_valuearray(Slapi_Value ***vals, Slapi_Value **addvals, PRUint32 flags);
void valuearray_add_valuearray_fast(Slapi_Value ***vals, Slapi_Value **addvals, int nvals, int naddvals, int *maxvals, int exact, int passin);
Slapi_Value *valueset_find_sorted(const Slapi_Attr *a, const Slapi_ValueSet *vs, const Slapi_Value *v, size_t *index);
int valueset_insert_value_to_sorted(const Slapi_Attr *a, Slapi_ValueSet *vs, Slapi_Value *vi, int dupcheck);
void valueset_array_to_sorted(const Slapi_Attr *a, Slapi_ValueSet *vs);
void valueset_big_array_to_sorted(const Slapi_Attr *a, Slapi_ValueSet *vs);
void valueset_array_to_sorted_quick(const Slapi_Attr *a, Slapi_ValueSet *vs, size_t s, size_t e);
void valueset_swap_values(size_t *a, size_t *b);

/* NOTE: if the flags include SLAPI_VALUE_FLAG_PASSIN and SLAPI_VALUE_FLAG_DUPCHECK
 * THE CALLER MUST PROVIDE THE dup_index PARAMETER in order to know where in addval
 * the un-copied values start e.g. to free them for cleanup
 * see valueset_replace_valuearray_ext() for an example
 */
int slapi_valueset_add_attr_valuearray_ext(const Slapi_Attr *a, Slapi_ValueSet *vs, Slapi_Value **addval, int nvals, unsigned long flags, int *dup_index);
int valuearray_find(const Slapi_Attr *a, Slapi_Value **va, const Slapi_Value *v);
int valuearray_dn_normalize_value(Slapi_Value **vals);

/*
 * proxyauth.c
 */
int proxyauth_get_dn(Slapi_PBlock *pb, char **proxydnp, char **errtextp);


/******************************************************************************
 * Database plugin interface.
 *
 * Prior to the 5.0 release, this was a public interface that lived in
 * slapi-plugin.h, so it is still a good idea to avoid making changes to it
 * that are not backwards compatible.
 */

/* plugin type */
#define SLAPI_PLUGIN_DATABASE 1

/* database plugin functions */
#define SLAPI_PLUGIN_DB_BIND_FN                   200
#define SLAPI_PLUGIN_DB_UNBIND_FN                 201
#define SLAPI_PLUGIN_DB_SEARCH_FN                 202
#define SLAPI_PLUGIN_DB_COMPARE_FN                203
#define SLAPI_PLUGIN_DB_MODIFY_FN                 204
#define SLAPI_PLUGIN_DB_MODRDN_FN                 205
#define SLAPI_PLUGIN_DB_ADD_FN                    206
#define SLAPI_PLUGIN_DB_DELETE_FN                 207
#define SLAPI_PLUGIN_DB_ABANDON_FN                208
#define SLAPI_PLUGIN_DB_CONFIG_FN                 209
#define SLAPI_PLUGIN_DB_SEQ_FN                    213
#define SLAPI_PLUGIN_DB_ENTRY_FN                  214
#define SLAPI_PLUGIN_DB_REFERRAL_FN               215
#define SLAPI_PLUGIN_DB_RESULT_FN                 216
#define SLAPI_PLUGIN_DB_LDIF2DB_FN                217
#define SLAPI_PLUGIN_DB_DB2LDIF_FN                218
#define SLAPI_PLUGIN_DB_BEGIN_FN                  219
#define SLAPI_PLUGIN_DB_COMMIT_FN                 220
#define SLAPI_PLUGIN_DB_ABORT_FN                  221
#define SLAPI_PLUGIN_DB_ARCHIVE2DB_FN             222
#define SLAPI_PLUGIN_DB_DB2ARCHIVE_FN             223
#define SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_FN      224
#define SLAPI_PLUGIN_DB_FREE_RESULT_SET_FN        225
#define SLAPI_PLUGIN_DB_TEST_FN                   227
#define SLAPI_PLUGIN_DB_DB2INDEX_FN               228
#define SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_EXT_FN  229
#define SLAPI_PLUGIN_DB_WIRE_IMPORT_FN            234
#define SLAPI_PLUGIN_DB_UPGRADEDB_FN              235
#define SLAPI_PLUGIN_DB_DBVERIFY_FN               236
#define SLAPI_PLUGIN_DB_SEARCH_RESULTS_RELEASE_FN 238
#define SLAPI_PLUGIN_DB_PREV_SEARCH_RESULTS_FN    239
#define SLAPI_PLUGIN_DB_UPGRADEDNFORMAT_FN        240
/* database plugin-specific parameters */
#define SLAPI_PLUGIN_DB_NO_ACL                    250
#define SLAPI_PLUGIN_DB_RMDB_FN                   280
#define SLAPI_PLUGIN_DB_GET_INFO_FN               290
#define SLAPI_PLUGIN_DB_SET_INFO_FN               291
#define SLAPI_PLUGIN_DB_CTRL_INFO_FN              292
#define SLAPI_PLUGIN_DB_COMPACT_FN                294

/**** End of database plugin interface. **************************************/


/******************************************************************************
 * Interface to the UniqueID generator (uniqueid.c)
 *
 * This could be made public someday, although it is a large interface and
 * not all of the elements follow the SLAPI_ naming convention.
 */
/* error codes */
#define UID_UPDATE_SHUTDOWN -1     /* update state information only during server shutdown */
#define UID_UPDATE_INTERVAL 600000 /* 10 minutes */

enum
{
    UID_SUCCESS,         /* operation was successfull              */
    UID_ERROR_BASE = 10, /* start of the error codes               */
    UID_BADDATA,         /* invalid parameter passed to a function */
    UID_MEMORY_ERROR,    /* memory allocation failed               */
    UID_SYSTEM_ERROR,    /* I/O failed (currently, further details
                           can be obtained using PR_GetError      */
    UID_TIME_ERROR,      /* UUID can't be generated because system
                           time has not been update               */
    UID_ERROR_END        /* end of the error codes                 */
};

/* Function:    slapi_uniqueIDNew
   Description: allocates new id
   Parameters:   none
   Return:        pointer to the newly allocated id if successful
                NULL if the system is out of memory
 */
Slapi_UniqueID *slapi_uniqueIDNew(void);

/* Function:    slapi_uniqueIDDestroy
   Description:    destroys UniqueID object and sets its pointer to NULL
   Parameters:    uId - id to destroy
   Return:        none
 */
void slapi_uniqueIDDestroy(Slapi_UniqueID **uId);

/* Function:    slapi_uniqueIDCompare
   Description: this function compares two ids (byte by byte).
   Parameters:  uId1, uId2 - ids to compare
   Return:      -1 if uId1 <  uId2
                0  if uId2 == uId2
                1  if uId2 >  uId2
                UID_BADDATA if invalid pointer passed to the function
*/
int slapi_uniqueIDCompare(const Slapi_UniqueID *uId1, const Slapi_UniqueID *uId2);

int slapi_uniqueIDCompareString(const char *uuid1, const char *uuid2);

/*  Function:    slapi_uniqueIDFormat
    Description: this function converts entryId to its string representation.
                 The id format is HH-HHHHHHHH-HHHHHHHH-HHHHHHHH-HHHHHHHH
                 where H is a hex digit.
    Parameters:  uId  - unique id
                 buff - buffer in which id is returned;
    Return:      UID_SUCCESS - function was successfull
                 UID_BADDATA - invalid parameter passed to the function
*/
int slapi_uniqueIDFormat(const Slapi_UniqueID *uId, char **buff);

/*  Function:    slapi_uniqueIDScan
    Description: this function converts a string buffer into uniqueID.
                 Currently, it only supports
                 HH-HHHHHHHH-HHHHHHHH-HHHHHHHH-HHHHHHHH data format.
    Parameters:  uId  - unique id to be returned
                 buff - buffer with uniqueID.
    Return:      UID_SUCCESS - function was successfull
                 UID_BADDATA - null parameter(s) or bad format
*/
int slapi_uniqueIDScan(Slapi_UniqueID *uId, const char *buff);


/* Function:     slapi_uniqueIDIsUUID
   Description:  tests if given entry id is of UUID type
   Parameters:   uId - unique id to test
   Return        UID_SUCCESS - function was successfull
                 UID_BADDATA - invalid data passed to the function
 */
int slapi_uniqueIDIsUUID(const Slapi_UniqueID *uId);

/* Name:        slapi_uniqueIDSize
   Description:    returns size of the string version of uniqueID in bytes
   Parameters:  none
   Return:        size of the string version of uniqueID in bytes
 */
int slapi_uniqueIDSize(void);

/* Name:        slapi_uniqueIDRdnSize
   Description:    returns size of SLAPI_ATTR_UNIQUEID=slapi_uniqueIDSize()
   Parameters:  none
   Return:        size of the string version of "SLAPI_ATTR_UNIQUEID=uniqueID" in bytes
 */
int slapi_uniqueIDRdnSize(void);

/* Name:        slapi_uniqueIDDup
   Description:    duplicates an UniqueID object
   Parameters:    uId - id to duplicate
   Return:        duplicate of the Id
 */
Slapi_UniqueID *slapi_uniqueIDDup(Slapi_UniqueID *uId);

/*
 * interface to UniqueID generator - uniqueidgen.c
 */

/* Function:    slapi_uniqueIDGenerate
   Description: this function generates uniqueid in a singlethreaded
                environment.
   Parameters:  uId - buffer to receive the ID.
   Return:      UID_SUCCESS if function succeeds;
                UID_BADDATA if invalid pointer passed to the function;
                UID_SYSTEM_ERROR update to persistent storage failed.
*/

int slapi_uniqueIDGenerate(Slapi_UniqueID *uId);

/* Function:    slapi_uniqueIDGenerateString
   Description: this function generates uniqueid an returns it as a string
                in a singlethreaded environment. This function returns the
                data in the format generated by slapi_uniqueIDFormat.
   Parameters:  uId - buffer to receive the ID.    Caller is responsible for
                freeing uId buffer.
   Return:      UID_SUCCESS if function succeeds;
                UID_BADDATA if invalid pointer passed to the function;
                UID_MEMORY_ERROR if malloc fails;
                UID_SYSTEM_ERROR update to persistent storage failed.
*/

int slapi_uniqueIDGenerateString(char **uId);

/* Function:    slapi_uniqueIDGenerateMT
   Description: this function generates entry id in a multithreaded
                environment. Used in conjunction with
                uniqueIDUpdateState function.
   Parameters:  uId - structure in which new id will be returned.
   Return:      UID_SUCCESS if function succeeds;
                UID_BADDATA if invalid pointer passed to the function;
                UID_TIME_ERROR uniqueIDUpdateState must be called
                before the id can be generated.
*/

int slapi_uniqueIDGenerateMT(Slapi_UniqueID *uId);

/* Function:    slapi_uniqueIDGenerateMTString
   Description: this function generates  uniqueid and returns it as a
                string in a multithreaded environment. Used in conjunction
                with uniqueIDUpdateState function.
   Parameters:  uId - buffer in which new id will be returned. Caller is
                responsible for freeing uId buffer.
   Return:      UID_SUCCESS if function succeeds;
                UID_BADDATA if invalid pointer passed to the function;
                UID_MEMORY_ERROR if malloc fails;
                UID_TIME_ERROR uniqueIDUpdateState must be called
                before the id can be generated.
*/

int slapi_uniqueIDGenerateMTString(char **uId);

/* Function:    slapi_uniqueIDGenerateFromName
   Description:    this function generates an id from a name. See uuid
                draft for more details. This function can be used in
                both a singlethreaded and a multithreaded environments.
   Parameters:    uId        - generated id
                uIDBase - uid used for generation to distinguish among
                different name spaces
                name - buffer containing name from which to generate the id
                namelen - length of the name buffer
   Return:        UID_SUCCESS if function succeeds
                UID_BADDATA if invalid argument is passed to the
                function.
*/

int slapi_uniqueIDGenerateFromName(Slapi_UniqueID *uId,
                                   const Slapi_UniqueID *uIdBase,
                                   const void *name,
                                   int namelen);

/* Function:    slapi_uniqueIDGenerateFromName
   Description:    this function generates an id from a name and returns
                it in the string format. See uuid draft for more
                details. This function can be used in both a
                singlethreaded and a multithreaded environments.
   Parameters:    uId        - generated id in string form
                uIDBase - uid used for generation to distinguish among
                different name spaces in string form. NULL means to use
                empty id as the base.
                name - buffer containing name from which to generate the id
                namelen - length of the name buffer
   Return:        UID_SUCCESS if function succeeds
                UID_BADDATA if invalid argument is passed to the
                function.
*/

int slapi_uniqueIDGenerateFromNameString(char **uId,
                                         const char *uIdBase,
                                         const void *name,
                                         int namelen);

/**** End of UniqueID generator interface. ***********************************/


/*****************************************************************************
 * JCMREPL - Added for the replication plugin.
 */

void schema_expand_objectclasses_nolock(Slapi_Entry *e);

#define DSE_SCHEMA_NO_LOAD    0x0001  /* schema won't get loaded */
#define DSE_SCHEMA_NO_CHECK   0x0002  /* schema won't be checked */
#define DSE_SCHEMA_NO_BACKEND 0x0004  /* don't add as backend */

#define DSE_SCHEMA_NO_GLOCK          0x0010  /* don't lock global resources */
#define DSE_SCHEMA_LOCKED            0x0020  /* already locked with reload_schemafile_lock; no further lock needed */
#define DSE_SCHEMA_USER_DEFINED_ONLY 0x0100  /* refresh user defined schema */
#define DSE_SCHEMA_USE_PRIV_SCHEMA   0x0200  /* Use a provided private schema */

/* */
#define OC_CONSUMER "consumer"
#define OC_SUPPLIER "supplier"

#define SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY       0
#define SLAPI_RTN_BIT_FETCH_PARENT_ENTRY            1
#define SLAPI_RTN_BIT_FETCH_NEWPARENT_ENTRY         2
#define SLAPI_RTN_BIT_FETCH_TARGET_ENTRY            3
#define SLAPI_RTN_BIT_FETCH_EXISTING_UNIQUEID_ENTRY 4

/* Attribute use to mark entries that had a replication conflict on the DN */
#define ATTR_NSDS5_REPLCONFLICT "nsds5ReplConflict"

/* Time */
#include <time.h> /* difftime, localtime_r, mktime */
/* Duplicated: time_t read_localTime (struct berval* from); */
time_t time_plus_sec(time_t l, long r);
char *format_localTime(time_t from);
time_t read_localTime(struct berval *from);
time_t parse_localTime(char *from);
void write_localTime(time_t from, struct berval *into);
time_t current_time(void) __attribute__((deprecated));
char *format_genTime(time_t from);
void write_genTime(time_t from, struct berval *into);
time_t read_genTime(struct berval *from);
time_t parse_genTime(char *from);
long parse_duration(char *value) __attribute__((deprecated));
long parse_duration_32bit(char *value);
time_t parse_duration_time_t(char *value);
char *gen_duration(long duration);

/* Client SSL code */
int slapd_security_library_is_initialized(void);
int slapd_nss_is_initialized(void);
char *slapd_get_tmp_dir(void);

/* thread-data.c */
/* defines for internal logging */
typedef enum _slapi_op_nest_state {
    OP_STATE_NOTNESTED = 0,
    OP_STATE_NESTED = 1,
    OP_STATE_PREV_NESTED = 2,
} slapi_log_nest_state;


struct slapi_td_log_op_state_t {
    int32_t op_id;
    int32_t op_int_id;
    int32_t op_nest_count;
    slapi_log_nest_state op_nest_state;
    int64_t conn_id;
};

int slapi_td_init(void);
int slapi_td_set_dn(char *dn);
void slapi_td_get_dn(char **dn);
int slapi_td_plugin_lock_init(void);
int slapi_td_get_plugin_locked(void);
int slapi_td_set_plugin_locked(void);
int slapi_td_set_plugin_unlocked(void);
struct slapi_td_log_op_state_t * slapi_td_get_log_op_state(void);
void slapi_td_internal_op_start(void);
void slapi_td_internal_op_finish(void);
void slapi_td_reset_internal_logging(uint64_t conn_id, int32_t op_id);

/*  Thread Local Storage Index Types - thread_data.c */

/* util.c */
#include <stdio.h> /* GGOODREPL - For BUFSIZ, below, gak */
const char *escape_string(const char *str, char buf[BUFSIZ]);
const char *escape_string_with_punctuation(const char *str, char buf[BUFSIZ]);
const char *escape_string_for_filename(const char *str, char buf[BUFSIZ]);
void strcpy_unescape_value(char *d, const char *s);
void get_internal_conn_op (uint64_t *connid, int32_t *op_id, int32_t *op_internal_id, int32_t *op_nested_count);
char *slapi_berval_get_string_copy(const struct berval *bval);
char get_sep(char *path);
int mkdir_p(char *dir, unsigned int mode);
const char *ldif_getline_ro( const char **next);
void dup_ldif_line(struct berval *copy, const char *line, const char *endline);

/* lenstr stuff */

typedef struct _lenstr
{
    char *ls_buf;
    size_t ls_len;
    size_t ls_maxlen;
} lenstr;
#define LS_INCRSIZE 256

void addlenstr(lenstr *l, const char *str);
void lenstr_free(lenstr **);
lenstr *lenstr_new(void);

/* config DN */
char *get_config_DN(void);

/* Data Version */
const char *get_server_dataversion(void);

/* Configuration Parameters */
int config_get_port(void);
int config_get_secureport(void);

/* Local host information */
char *get_localhost_DN(void);
char *get_localhost_DNS(void);

/* GGOODREPL get_data_source definition should move into repl DLL */
struct berval **get_data_source(Slapi_PBlock *pb, const Slapi_DN *sdn, int orc, void *cf_refs);

#if defined(__GNUC__) && (((__GNUC__ == 4) && (__GNUC_MINOR__ >= 4)) || (__GNUC__ > 4))
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#endif

/* JCMREPL - IFP and CFP should be defined centrally */
#ifndef _IFP
#define _IFP
typedef int (*IFP)(); /* takes undefined arguments */
#endif

#if defined(__GNUC__) && (((__GNUC__ == 4) && (__GNUC_MINOR__ >= 4)) || (__GNUC__ > 4))
#pragma GCC diagnostic pop
#endif

#ifndef _CFP
#define _CFP
typedef char *(*CFP)(void);
#endif

void bervalarray_add_berval_fast(struct berval ***vals, const struct berval *addval, int nvals, int *maxvals);


/* this is the root configuration entry beneath which all plugin
   configuration entries will be found */
#define PLUGIN_BASE_DN "cn=plugins,cn=config"

#define SLAPI_PLUGIN_DEFAULT_CONFIG "cn=plugin default config,cn=config"

/***** End of items added for the replication plugin. ***********************/

/* macro to specify the behavior of upgradedb & upgradednformat */
#define SLAPI_UPGRADEDB_FORCE     0x1  /* reindex all (no check w/ idl switch) */
#define SLAPI_UPGRADEDB_SKIPINIT  0x2  /* call upgradedb as part of other op */
#define SLAPI_UPGRADEDB_DN2RDN    0x4  /* modify id2entry from dn format to rdn;  generate entryrdn index */
#define SLAPI_UPGRADEDNFORMAT     0x8  /* specify this op is upgradednformat */
#define SLAPI_DRYRUN             0x10             /* dryrun mode for upgradednformat */
#define SLAPI_UPGRADEDNFORMAT_V1 0x20 /* taking care multipe spaces */


/*
 * Macro to set port to the 'port' field of a NSPR PRNetAddr union.
 ** INPUTS:
 ** PRNetAddr *myaddr   A network address.
 ** PRUint16   myport   port to set to the 'port' field of 'addr'.
 ** RETURN: none
 *
 * Note: Copy from ldappr-int.h in
 *   ldapcsdk:mozilla/directory/c-sdk/ldap/libraries/libprldap
 * Introduced to avoid calling PR_SetNetAddr w/ PR_IpAddrNull just to set port.
 * Once NSPR starts providing better function/macro to do the same job,
 * this macro should be replaced with it. (newer than NSPR v4.6.2)
 */
#define PRLDAP_SET_PORT(myaddr, myport) \
    ((myaddr)->raw.family == PR_AF_INET6 ? ((myaddr)->ipv6.port = PR_htons(myport)) : ((myaddr)->inet.port = PR_htons(myport)))

/* plugin.c */
int plugin_enabled(const char *plugin_name, void *identity);

/**
 * For "database" plugins that need to call preoperation backend & backend txn plugins.
 * This function should be called right before the operation is performed.
 *
 * \param Slapi_PBLock object
 * \param int operation
 *
 *     Operations:
 *         SLAPI_PLUGIN_ADD_OP
 *         SLAPI_PLUGIN_MOD_OP
 *         SLAPI_PLUGIN_MODRDN_OP
 *         SLAPI_PLUGIN_DEL_OP
 *
 * \return zero on success, non-zero for failure
 */
int slapi_plugin_call_preop_be_plugins(Slapi_PBlock *pb, int operation);

/**
 * For "database" plugins that need to call postoperation backend & backend txn plugins.
 * This function should be called right after the operation is performed.
 *
 * \param Slapi_PBLock object
 * \param int operation
 *
 *     Operations:
 *         SLAPI_PLUGIN_ADD_OP
 *         SLAPI_PLUGIN_MOD_OP
 *         SLAPI_PLUGIN_MODRDN_OP
 *         SLAPI_PLUGIN_DEL_OP
 *
 * \return zero on success, non-zero for failure
 */
int slapi_plugin_call_postop_be_plugins(Slapi_PBlock *pb, int operation);

/* protect_db.c */
/* is_slapd_running()
 * returns 1 if slapd is running, 0 if not, -1 on error
 */
int is_slapd_running(void);

/* schema.c */
void schema_destroy_dse_lock(void);

/* attrsyntax.c */
int slapi_add_internal_attr_syntax(const char *name, const char *oid, const char *syntax, const char *mr_equality, unsigned long extraflags);

/* pw.c */
void pw_exp_init(void);
int pw_copy_entry_ext(Slapi_Entry *src_e, Slapi_Entry *dest_e);
int pw_get_ext_size(Slapi_Entry *e, size_t *size);

/* op_shared.c */
void modify_update_last_modified_attr(Slapi_PBlock *pb, Slapi_Mods *smods);

/* add.c */
void add_internal_modifiersname(Slapi_PBlock *pb, Slapi_Entry *e);

/* dse.c */
void dse_init_backup_lock(void);
void dse_destroy_backup_lock(void);
void dse_backup_lock(void);
void dse_backup_unlock(void);

/* ldaputil.c */
char *ldaputil_get_saslpath(void);
int slapi_client_uses_non_nss(LDAP *ld);
int slapi_client_uses_openssl(LDAP *ld);

/* rewriters.c */
int32_t rewriters_init(void);

/* ssl.c */
/*
 * If non NULL buf and positive bufsize is given,
 * the memory is used to store the version string.
 * Otherwise, the memory for the string is allocated.
 * The latter case, caller is responsible to free it.
 */
/* vnum is supposed to be in one of the following:
 * nss3/sslproto.h
 * #define SSL_LIBRARY_VERSION_2                   0x0002
 * #define SSL_LIBRARY_VERSION_3_0                 0x0300
 * #define SSL_LIBRARY_VERSION_TLS_1_0             0x0301
 * #define SSL_LIBRARY_VERSION_TLS_1_1             0x0302
 * #define SSL_LIBRARY_VERSION_TLS_1_2             0x0303
 * #define SSL_LIBRARY_VERSION_TLS_1_3             0x0304
 * ...
 */
char *slapi_getSSLVersion_str(PRUint16 vnum, char *buf, size_t bufsize);

/*
 * time.c
 *
 * Return Value:
 *    Success: duration in seconds
 *    Failure: -1
 */
time_t slapi_parse_duration(const char *value);
long long slapi_parse_duration_longlong(const char *value) __attribute__((deprecated));
int slapi_is_duration_valid(const char *value);

/**
 * Possible results of a cachesize check
 */
typedef enum _util_cachesize_result {
    /**
     * The requested cachesize was valid and can be used.
     */
    UTIL_CACHESIZE_VALID = 0,
    /**
     * The requested cachesize may cause OOM and was reduced.
     */
    UTIL_CACHESIZE_REDUCED = 1,
    /**
     * An error occured resolving the cache size. You must stop processing.
     */
    UTIL_CACHESIZE_ERROR = 2,
} util_cachesize_result;
/**
 * Determine if the requested cachesize will exceed the system memory limits causing an out of memory condition. You must
 * check the result before proceeding to correctly use the cache.
 *
 * \param mi. The system memory infomation. You should retrieve this with spal_meminfo_get(), and destroy it after use.
 * \param cachesize. The requested allocation. If this value is greater than the memory available, this value will be REDUCED to be valid.
 *
 * \return util_cachesize_result.
 * \sa util_cachesize_result, spal_meminfo_get
 */
util_cachesize_result util_is_cachesize_sane(slapi_pal_meminfo *mi, uint64_t *cachesize);

/**
 * Retrieve the number of threads the server should run with based on this hardware.
 *
 * \return -1 if the hardware detection failed. Any positive value is threads to use.
 */
long util_get_hardware_threads(void);
/**
 * Retrieve the number of threads the server should run with based on this hardware.
 *   positive return value is in the [min.max] range
 *
 * \return -1 if the hardware detection failed. Any positive value is threads to use.
 */
long util_get_capped_hardware_threads(long min, long max);

/**
 * Write an error message to the given error buffer.
 *
 * \param errorbuf. The buffer that the error message is written into.  If NULL, nothing happens.  It could be a static array or allocated memory.  If it is allocated memory, the next param len should be given.
 * \param len. The length of errorbuf.  If 0 is given, sizeof(errorbuf) is used.
 * \param fmt. The format of the error message.
 */
void slapi_create_errormsg(char *errorbuf, size_t len, const char *fmt, ...);

struct slapi_entry *slapi_pblock_get_pw_entry(Slapi_PBlock *pb);
void slapi_pblock_set_pw_entry(Slapi_PBlock *pb, struct slapi_entry *entry);

uint32_t slapi_pblock_get_operation_notes(Slapi_PBlock *pb);
void slapi_pblock_set_operation_notes(Slapi_PBlock *pb, uint32_t opnotes);
void slapi_pblock_set_flag_operation_notes(Slapi_PBlock *pb, uint32_t opflag);
void slapi_pblock_set_result_text_if_empty(Slapi_PBlock *pb, char *text);

int32_t slapi_pblock_get_task_warning(Slapi_PBlock *pb);
void slapi_pblock_set_task_warning(Slapi_PBlock *pb, task_warning warning);

int slapi_exists_or_add_internal(Slapi_DN *dn, const char *filter, const char *entry, const char *modifier_name);

void slapi_log_backtrace(int loglevel);

#ifdef __cplusplus
}
#endif

#endif

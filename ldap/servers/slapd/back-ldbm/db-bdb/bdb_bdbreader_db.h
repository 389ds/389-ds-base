/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2024 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

/*
 * This file provides the part of the Berkeley Datatabase 5.3 API
 * definition that are refered within 389-ds-base project
 */

#ifndef DB_H_
#define DB_H_

/* Needed for configure */
#define DB_VERSION_MAJOR 5
#define DB_VERSION_MINOR 3
#define DB_VERSION_PATCH 280


#define DB_INIT_MPOOL        0x000001
#define DB_INIT_TXN          0x000002
#define DB_INIT_LOG          0x000004
#define DB_INIT_LOCK         0x000008
#define DB_REGION_INIT       0x000010
#define DB_FREE_SPACE        0x000020
#define DB_FREELIST_ONLY     0x000040
#define DB_TXN_NOWAIT        0x000080
#define DB_ARCH_ABS          0x000100
#define DB_ARCH_LOG          0x000200
#define DB_RDONLY            0x000400
#define DB_STAT_CLEAR        0x000800



#define DB_DUP               0x000100
#define DB_DUPSORT           0x000200
#define DB_RECNUM            0x000400
#define DB_FORCE             0x000800
#define DB_SYSTEM_MEM        0x001000
#define DB_THREAD            0x002000
#define DB_CREATE            0x004000
#define DB_PRIVATE           0x008000
#define DB_MULTIPLE_KEY      0x010000
#define DB_MULTIPLE          0x020000
#define DB_RMW               0x040000
#define DB_RECOVER           0x080000
#define DB_TXN_WRITE_NOSYNC  0x100000
#define DB_AUTO_COMMIT       0x200000
#define DB_RECOVER_FATAL     0x400000
#define DB_LOCKDOWN          0x800000
#define DB_TRUNCATE          0x1000000
#define XDB_RECOVER          0x000000

enum {
    DB_FIRST = 1,
    DB_CURRENT,
    DB_GET_BOTH,
    DB_GET_BOTH_RANGE,
    DB_GET_RECNO,
    DB_LAST,
    DB_NEXT,
    DB_NEXT_DUP,
    DB_NEXT_NODUP,
    DB_NODUPDATA,
    DB_PREV,
    DB_SET,
    DB_SET_RANGE,
    DB_SET_RECNO,
    DB_UNKNOWN,

};



#define OPEN_FLAGS_CLOSED 0x58585858
#define OPEN_FLAGS_OPEN   0xdbdbdbdb

#define DB_DBT_USERMEM 1
#define DB_DBT_MALLOC  2
#define DB_DBT_REALLOC 4
#define DB_DBT_PARTIAL 8

#define DB_MULTIPLE_INIT(pointer, dbt)
#define DB_MULTIPLE_NEXT(pointer, dbt, retdata, retdlen)
#define DB_MULTIPLE_KEY_NEXT(pointer, dbt, retkey, retklen, retdata, retdlen)

typedef enum {
    DB_SUCCESS,
    DB_NOTFOUND,    /* Should be 1 because btree_next returns 1 when there are no more records */
    DB_BUFFER_SMALL,
    DB_KEYEXIST,
    DB_RUNRECOVERY,
    DB_NOTSUPPORTED,
    DB_LOCK_DEADLOCK,
    DB_OSERROR,
} db_error_t;

enum {
    DB_LOCK_DEFAULT,
    DB_LOCK_NORUN,
    DB_VERB_DEADLOCK,
    DB_VERB_RECOVERY,
    DB_VERB_WAITSFOR,
};

/* DB_LOCK_YOUNGEST mustt be a define because it is used in STRINGIFYDEFINE */
#define DB_LOCK_YOUNGEST 9

typedef struct db DB;
typedef struct db_env DB_ENV;
typedef struct db_txn DB_TXN;
typedef struct db_cursor DBC;

typedef enum {
    DB_BTREE,
    DB_HASH,
    DB_RECNO,
} DBTYPE;

typedef struct {
    int compact_pages_free;
} DB_COMPACT;

typedef struct {
    int st_nactive;
    int st_ncommits;
    int st_naborts;
    int st_region_wait;
} DB_TXN_STAT;

typedef struct {
    int st_maxnlocks;
    int st_ndeadlocks;
    int st_nlockers;
    int st_region_wait;
    int st_w_bytes;
    int st_wc_bytes;
    int st_wc_mbytes;
    int st_w_mbytes;

} DB_LOG_STAT;

typedef struct {
    int st_bytes;
    int st_cache_hit;
    int st_cache_miss;
    int st_gbytes;
    int st_hash_buckets;
    int st_hash_longest;
    int st_hash_searches;
    int st_page_create;
    int st_page_in;
    int st_page_out;
    int st_ro_evict;
    int st_rw_evict;
    int st_page_dirty;
    int st_pagesize;
    int st_page_clean;
    int st_page_trickle;
    int st_hash_examined;
    int st_region_wait;


} DB_MPOOL_STAT;

typedef struct {
    int bt_ndata;
    u_int32_t bt_pagecnt;
} DB_BTREE_STAT;

typedef struct {
    int st_nlocks;
    int st_maxlocks;
    int st_region_wait;
    int st_ndeadlocks;
    int st_maxnlocks;
    int st_nlockers;
    int st_lock_wait;
    int st_nobjects;
    int st_maxnobjects;
    int st_nrequests;

} DB_LOCK_STAT;

typedef struct {
    char *file_name;
    int st_page_in;
    int st_page_out;
    int st_cache_hit;
    int st_cache_miss;
} DB_MPOOL_FSTAT;

typedef struct dbt {
    u_int32_t flags;
    u_int32_t size;
    u_int32_t ulen;
    u_int32_t dlen;
    u_int32_t doff;
    void *data;
} DBT;


struct db_cursor {
    DB *dbp;
    int (*c_close)(DBC *);
    int (*c_del)(DBC *, u_int32_t);
    int (*c_get)(DBC *, DBT*, DBT*, u_int32_t);
    int (*c_put)(DBC *, DBT*, DBT*, u_int32_t);
    int (*c_count)(DBC *, void *, u_int32_t);
        /* fields only used by the implementation */
    void *impl;
};

struct db_txn {
    int (*id)(DB_TXN*);
    int (*commit)(DB_TXN*, u_int32_t);
    int (*abort)(DB_TXN*);
};

struct db_env {
    int (*close)(DB_ENV*, int);
    int (*lock_detect)(DB_ENV*, int, int, int*);
    int (*open)(DB_ENV*, const char*, int, int);
    int (*remove)(DB_ENV *, const char *, u_int32_t);
    int (*set_flags)(DB_ENV*, u_int32_t, u_int32_t);
    int (*set_verbose)(DB_ENV *, u_int32_t, int);
    int (*txn_checkpoint)(const DB_ENV *, u_int32_t , u_int32_t , u_int32_t);
    int (*txn_begin)(DB_ENV *, DB_TXN *, DB_TXN **, u_int32_t);
    void (*get_open_flags)(DB_ENV*, u_int32_t*);
    void (*mutex_set_tas_spins)(DB_ENV *, u_int32_t);
    void (*set_alloc)(DB_ENV *, void *, void *, void*);
    void (*set_cachesize)(DB_ENV *, u_int32_t , u_int32_t , int);
    void (*set_data_dir)(DB_ENV *, const char *);
    void (*set_errcall)(DB_ENV *, void (*)(const DB_ENV *, const char *, const char *));
    void (*set_errpfx)(DB_ENV *, const char *);
    void (*set_lg_bsize)(DB_ENV *, long);
    void (*set_lg_dir)(DB_ENV *, const char*);
    void (*set_lg_max)(DB_ENV *, u_int32_t);
    void (*set_lg_regionmax)(DB_ENV *, u_int32_t);
    void (*set_lk_max_lockers)(DB_ENV *, u_int32_t);
    void (*set_lk_max_locks)(DB_ENV *, u_int32_t);
    void (*set_lk_max_objects)(DB_ENV *, u_int32_t);
    void (*set_shm_key)(DB_ENV *, long);
    void (*set_tx_max)(DB_ENV *, u_int32_t);
    void (*log_flush)(DB_ENV *, u_int32_t);
    int (*lock_stat)(DB_ENV *, DB_LOCK_STAT**, u_int32_t);
    int (*log_archive)(DB_ENV *, void*, u_int32_t);
    int (*memp_stat)(DB_ENV *, void*, void *, u_int32_t);
    int (*memp_trickle)(DB_ENV *, u_int32_t, int*);
    int (*dbrename)(DB_ENV *, DB_TXN *, const char *, const char *, const char *, u_int32_t);
    int (*stat)(DB_ENV *, DB_TXN *, void *, u_int32_t);
    int (*log_stat)(DB_ENV *, DB_LOG_STAT **, u_int32_t);
    int (*txn_stat)(DB_ENV *, DB_TXN_STAT **, u_int32_t);
        /* fields only used by the implementation */
    char *db_home;
};

struct db {
    int (*close)(DB*, u_int32_t);
    int (*compact)(DB*, DB_TXN*, DBT*, DBT*, void *, u_int32_t, DBT*);
    int (*cursor)(DB*, DB_TXN*, DBC**, u_int32_t);
    int (*del)(DB*, DB_TXN*, DBT*, u_int32_t);
    int (*get)(DB*, DB_TXN*, DBT*, DBT*, u_int32_t);
    int (*get_type)(DB *, DBTYPE *);
    int (*open)(DB*, DB_TXN*, const char*, const char *, DBTYPE, u_int32_t, int);
    int (*put)(DB*, DB_TXN*, DBT*, DBT*, u_int32_t);
    int (*remove)(DB*, const char*, const char*, u_int32_t);
    int (*rename)(DB*, const char*, const char*, const char*, u_int32_t);
    int (*set_bt_compare)(DB*, int (*)(DB *, const DBT *, const DBT *));
    int (*set_dup_compare)(DB*, int (*)(DB *, const DBT *, const DBT *));
    int (*set_flags)(DB*, u_int32_t);
    int (*set_pagesize)(DB *, u_int32_t);
    int (*stat)(DB *, DB_TXN *, void *, u_int32_t);
    int (*verify)(DB *, const char *, const char *, FILE *, u_int32_t);
    void *app_private;
    u_int32_t open_flags;
    char *fname;
    int pgsize;
        /* fields only used by the implementation */
    void *impl;
    DB_ENV *env;
    DBC *cur;
};

char * db_version(int *major, int *minor, int *patch);
int db_env_create(DB_ENV **, u_int32_t);
int db_create(DB **, DB_ENV *, u_int32_t);
char *db_strerror(int);

#endif /* DB_H_ */

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/


void dbmdb_format_dbslist_info(char *info, dbmdb_dbi_t *dbi);

#define DBGMDB_LEVEL_MDBAPI 1
#define DBGMDB_LEVEL_TXN    2
#define DBGMDB_LEVEL_IMPORT 4
#define DBGMDB_LEVEL_BULKOP 8
#define DBGMDB_LEVEL_VLV    0x10
#define DBGMDB_LEVEL_OTHER  0x20
#define DBGMDB_LEVEL_REPL   0x1000
#define DBGMDB_LEVEL_FORCE  0x10000

#define DBGMDB_LEVEL_PRINTABLE 0xfff

#define DBISTRMAXSIZE         80

extern int dbgmdb_level; /* defined in mdb_debug.c */
void dbg_log(const char *file, int lineno, const char *funcname, int loglevel, char *fmt, ...);
void dbgval2str(char *buff, size_t bufsiz, MDB_val *val);
void dbmdb_dbg_set_dbi_slots(dbmdb_dbi_t *slots);
void dbi_str(MDB_cursor *cursor, int dbi, char dbistr[DBISTRMAXSIZE]);

/* #define DBMDB_DEBUG 1 */
#define DBGMDB_LEVEL_DEFAULT DBGMDB_LEVEL_MDBAPI+DBGMDB_LEVEL_TXN+DBGMDB_LEVEL_IMPORT+ \
                             DBGMDB_LEVEL_BULKOP+DBGMDB_LEVEL_OTHER+DBGMDB_LEVEL_VLV

/* Define the wrapper associated with each log level */
#ifdef DBMDB_DEBUG
#define SLAPI_LOG_DBGMDB SLAPI_LOG_INFO
#define MDB_CURSOR_OPEN(txn,dbi,cursor) dbg_mdb_cursor_open(__FILE__,__LINE__,__FUNCTION__,txn,dbi,cursor)
#define MDB_CURSOR_RENEW(txn,cursor) dbg_mdb_cursor_renew(__FILE__,__LINE__,__FUNCTION__,txn,cursor)
#define MDB_CURSOR_CLOSE(cursor) dbg_mdb_cursor_close(__FILE__,__LINE__,__FUNCTION__,cursor)
#define MDB_CURSOR_GET(cursor,key,data,op) dbg_mdb_cursor_get(__FILE__,__LINE__,__FUNCTION__,cursor,key,data,op)
#define MDB_PUT(txn,dbi,key,data,flags) dbg_mdb_put(__FILE__,__LINE__,__FUNCTION__,txn,dbi,key,data,flags)
#define MDB_GET(txn,dbi,key,data) dbg_mdb_get(__FILE__,__LINE__,__FUNCTION__,txn,dbi,key,data)
#define MDB_DEL(txn,dbi,key,data) dbg_mdb_del(__FILE__,__LINE__,__FUNCTION__,txn,dbi,key,data)
#define MDB_CURSOR_PUT(cursor,key,data,flags) dbg_mdb_cursor_put(__FILE__,__LINE__,__FUNCTION__,cursor,key,data,flags)
#define MDB_DBI_OPEN(txn,dbname,flags,dbi) dbg_mdb_dbi_open(__FILE__,__LINE__,__FUNCTION__,txn,dbname,flags,dbi)
#define MDB_DROP(txn, dbi, del) dbg_mdb_drop(__FILE__,__LINE__,__FUNCTION__,txn,dbi,del)
#define MDB_DBG_SET_FN(action, dbname, txn, dbi, fn) dbmdb_log_dbi_set_fn(__FILE__,__LINE__,__FUNCTION__, action, dbname, txn, dbi, fn)
#define MDB_BULKOP_CURSOR_GET(cursor,key,data,op) dbg_mdb_bulkop_cursor_get(__FILE__,__LINE__,__FUNCTION__,cursor,key,data,op)

#define TXN_BEGIN(env, parent_txn, flags, txn) dbg_txn_begin(__FILE__,__LINE__,__FUNCTION__, env, parent_txn, flags, txn)
#define TXN_COMMIT(txn) dbg_txn_end(__FILE__,__LINE__,__FUNCTION__, txn, 1)
#define TXN_ABORT(txn) dbg_txn_end(__FILE__,__LINE__,__FUNCTION__, txn, 0)
#define TXN_RESET(txn) dbg_txn_reset(__FILE__,__LINE__,__FUNCTION__, txn)
#define TXN_RENEW(txn) dbg_txn_renew(__FILE__,__LINE__,__FUNCTION__, txn)
#define TXN_LOG(msg,txn) dbg_log(__FILE__,__LINE__,__FUNCTION__,DBGMDB_LEVEL_TXN, msg, (ulong)(txn))
#define pthread_gettid() syscall(__NR_gettid)

#define DBG_LOG(...) dbg_log(__FILE__,__LINE__,__FUNCTION__, __VA_ARGS__)


int dbg_mdb_cursor_open(const char *file, int lineno, const char *funcname, MDB_txn *txn, MDB_dbi dbi, MDB_cursor **cursor);
int dbg_mdb_cursor_renew(const char *file, int lineno, const char *funcname, MDB_txn *txn, MDB_cursor *cursor);
void dbg_mdb_cursor_close(const char *file, int lineno, const char *funcname, MDB_cursor *cursor);
int dbg_mdb_cursor_get(const char *file, int lineno, const char *funcname, MDB_cursor *cursor, MDB_val *key, MDB_val *data, MDB_cursor_op op);
int dbg_mdb_put(const char *file, int lineno, const char *funcname, MDB_txn *txn, MDB_dbi dbi, MDB_val *key, MDB_val *data, unsigned int flags);
int dbg_mdb_get(const char *file, int lineno, const char *funcname, MDB_txn *txn, MDB_dbi dbi, MDB_val *key, MDB_val *data);
int dbg_mdb_del(const char *file, int lineno, const char *funcname, MDB_txn *txn, MDB_dbi dbi, MDB_val *key, MDB_val *data);
int dbg_mdb_cursor_put(const char *file, int lineno, const char *funcname, MDB_cursor *cursor, MDB_val *key, MDB_val *data, unsigned int flags);
int dbg_mdb_dbi_open(const char *file, int lineno, const char *funcname, MDB_txn *txn, const char *dbname, unsigned int flags, MDB_dbi *dbi);
int dbg_mdb_drop(const char *file, int lineno, const char *funcname, MDB_txn *txn, MDB_dbi dbi, int del);
int dbg_mdb_bulkop_cursor_get(const char *file, int lineno, const char *funcname, MDB_cursor *cursor, MDB_val *key, MDB_val *data, MDB_cursor_op op);

int dbg_txn_begin(const char *file, int lineno, const char *funcname, MDB_env *env, MDB_txn *parent_txn, int flags, MDB_txn **txn);
int dbg_txn_end(const char *file, int lineno, const char *funcname, MDB_txn *txn, int iscommit);
void dbg_txn_reset(const char *file, int lineno, const char *funcname, MDB_txn *txn);
int dbg_txn_renew(const char *file, int lineno, const char *funcname, MDB_txn *txn);
void debug_txn_dbi(MDB_txn *txn, MDB_dbi dbi);

void dbmdb_log_dbi_set_fn(const char *file, int lineno, const char *funcname, const char *action, const char *dbname, MDB_txn *txn, int dbi, MDB_cmp_func *fn);


#else /* DBMDB_DEBUG */

#define SLAPI_LOG_DBGMDB SLAPI_LOG_TRACE
#define MDB_CURSOR_OPEN(txn,dbi,cursor) mdb_cursor_open(txn,dbi,cursor)
#define MDB_CURSOR_RENEW(txn,cursor) mdb_cursor_renew(txn,cursor)
#define MDB_CURSOR_CLOSE(cursor) mdb_cursor_close(cursor)
#define MDB_CURSOR_GET(cursor,key,data,op) mdb_cursor_get(cursor,key,data,op)
#define MDB_PUT(txn,dbi,key,data,flags) mdb_put(txn,dbi,key,data,flags)
#define MDB_GET(txn,dbi,key,data) mdb_get(txn,dbi,key,data)
#define MDB_DEL(txn,dbi,key,data) mdb_del(txn,dbi,key,data)
#define MDB_CURSOR_PUT(cursor,key,data,flags) mdb_cursor_put(cursor,key,data,flags)
#define MDB_DBI_OPEN(txn,dbname,flags,dbi) mdb_dbi_open(txn,dbname,flags,dbi)
#define MDB_DROP(txn, dbi, del) mdb_drop(txn,dbi,del)
#define MDB_DBG_SET_FN(action, dbname, txn, dbi, fn)
#define MDB_BULKOP_CURSOR_GET(cursor,key,data,op)  mdb_cursor_get(cursor,key,data,op)

#define TXN_BEGIN(env, parent_txn, flags, txn) mdb_txn_begin(env, parent_txn, flags, txn)
#define TXN_COMMIT(txn) mdb_txn_commit(txn)
#define TXN_ABORT(txn) mdb_txn_abort(txn)
#define TXN_RESET(txn) mdb_txn_reset(txn)
#define TXN_RENEW(txn) mdb_txn_renew(txn)
#define TXN_LOG(msg,txn)
#define pthread_gettid() 0

#define DBG_LOG(...)

#endif /* DBMDB_DEBUG */

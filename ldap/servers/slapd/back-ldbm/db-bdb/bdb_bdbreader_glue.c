/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2024 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "bdb_layer.h"
#include <robdb.h>

int db_close(DB *, u_int32_t);

/*
 * This file contains the stub that transform usual bdb API (limited to the function 389-ds needs to export a db and a changelog
 * to  bdb_ro callbacks
 */

#undef slapi_log_err

#define LOGK(k)    (((k)&&(k)->data && (k)->size) ? (k)->data : "<NULL>")
#define LOGN(v, names)    (((v)> sizeof names/sizeof names[0]) ? "Unexpected value" : names[v])

int db_cursor(DB *db, DB_TXN *txnid, DBC **cursorp, u_int32_t flags);
int dbc_close(DBC *dbc);

void slapi_log_err(int loglvl, char *module, char *msg, ...) {
#ifdef DEBUG
    static FILE *fd = NULL;
    va_list ap;
    va_start(ap, msg);
    if (fd == NULL) {
        fd = fopen("/tmp/mylog", "w");
    }
    fprintf(fd, "[%s]:%d ", module, loglvl);
    vfprintf(fd, msg, ap);
    fflush(fd);
#endif
}

/*
 * Dump a memory buffer in hexa and ascii in error log
 *
 * addr - The memory buffer address.
 * len - The memory buffer lenght.
 */
void
hexadump(char *msg, const void *addr, size_t offset, size_t len)
{
#ifdef DEBUG
#define  HEXADUMP_TAB 4
/* 4 characters per bytes:  2 hexa digits, 1 space and the ascii  */
#define  HEXADUMP_BUF_SIZE (4*16+HEXADUMP_TAB)
    char hexdigit[] = "0123456789ABCDEF";

    const unsigned char *pt = addr;
    char buff[HEXADUMP_BUF_SIZE+1];
    memset (buff, ' ', HEXADUMP_BUF_SIZE);
    buff[HEXADUMP_BUF_SIZE] = '\0';
    while (len > 0) {
        int dpl;
        for (dpl = 0; dpl < 16 && len>0; dpl++, len--) {
           buff[3*dpl] = hexdigit[((*pt) >> 4) & 0xf];
           buff[3*dpl+1] = hexdigit[(*pt) & 0xf];
           buff[3*16+HEXADUMP_TAB+dpl] = (*pt>=0x20 && *pt<0x7f) ? *pt : '.';
           pt++;
        }
        for (;dpl < 16; dpl++) {
           buff[3*dpl] = ' ';
           buff[3*dpl+1] = ' ';
           buff[3*16+HEXADUMP_TAB+dpl] = ' ';
        }
        slapi_log_err(0, msg, "[0x%08lx]  %s\n", offset, buff);
        offset += 16;
    }
#endif
}

char *db_version(int *major, int *minor, int *patch)
{
    static char version[200];
    sprintf(version, "Read-Only Berkeley Database Stub %d.%d.%d", DB_VERSION_MAJOR, DB_VERSION_MINOR, DB_VERSION_PATCH);
    if (major != NULL) {
        *major = DB_VERSION_MAJOR;
    }
    if (minor != NULL) {
        *minor = DB_VERSION_MINOR;
    }
    if (patch != NULL) {
        *patch = DB_VERSION_PATCH;
    }
    return version;
}

int nothing()
{
    return DB_SUCCESS;
}

/*
 * Define libbdbreader callbacks
 * Note: slapi_ch_calloc, slapi_ch_malloc, slapi_ch_realloc
 * are not directly used because their prototype is
 * slightl
 * slapi_ch_calloc are slithly different
 */
void* bdbreader_calloc(size_t nbelmt, size_t size)
{
    return slapi_ch_calloc(nbelmt, size);
}

/*
 * Redefines all the callback because bdbreader_malloc and
 * slapi_ch_malloc prototypes are slithly different
 */
void* bdbreader_malloc(size_t size)
{
    return slapi_ch_malloc(size);
}

void* bdbreader_realloc(void* pt, size_t size)
{
    return slapi_ch_realloc(pt, size);
}

static void bdbreader_free(void **pt)
{
    slapi_ch_free(pt);
}

static void bdbreader_log(const char *msg, ...)
{
    char buffer[512];
    va_list ap;

    va_start(ap, msg);
    PR_vsnprintf(buffer, (sizeof buffer), msg, ap);
    va_end(ap);
    slapi_log_err(SLAPI_LOG_ERR, "libbdbreader", "%s", buffer);
}

int dbenv_open(DB_ENV *dbenv, const char *db_home, int flags, int mode)
{
    if (dbenv->db_home == NULL) {
        /* If not set by dbenv->set_dir_lg() */
        dbenv->db_home = slapi_ch_strdup(db_home);
    }
    /* Initialize libbdbreader callbacks */
    bdbreader_set_calloc_cb(bdbreader_calloc);
    bdbreader_set_malloc_cb(bdbreader_malloc);
    bdbreader_set_realloc_cb(bdbreader_realloc);
    bdbreader_set_free_cb(bdbreader_free);
    bdbreader_set_log_cb(bdbreader_log);
    return DB_SUCCESS;
}

int dbenv_close(DB_ENV *dbenv, int flags)
{
    slapi_ch_free_string(&dbenv->db_home);
    slapi_ch_free((void**)&dbenv);
    return DB_SUCCESS;
}

char *db_strerror(int err)
{
    switch (err) {
        case DB_SUCCESS:
            return (char*)"DB_SUCCESS";
        case DB_LOCK_DEADLOCK:
            return (char*)"DB_LOCK_DEADLOCK";
        case DB_NOTFOUND:
            return (char*)"DB_NOTFOUND";
        case DB_BUFFER_SMALL:
            return (char*)"DB_BUFFER_SMALL";
        case DB_KEYEXIST:
            return (char*)"DB_KEYEXIST";
        case DB_RUNRECOVERY:
            return (char*)"DB_RUNRECOVERY";
        case DB_NOTSUPPORTED:
            return (char*)"DB_NOTSUPPORTED";
        default:
            return (char*)"Unknonwn error";
    }
}

/* lg_dir is used to store the database directory */
static void
db_set_lg_dir(DB_ENV *penv, const char *lg_dir)
{
    penv->db_home = slapi_ch_strdup(lg_dir);
}

int db_env_create(DB_ENV **penv, u_int32_t flags)
{
    DB_ENV *env = (void*)slapi_ch_calloc(1, sizeof *env);

    env->close = dbenv_close;
    env->lock_detect = (void*)nothing;
    env->open = dbenv_open;
    env->remove = (void*)nothing;
    env->set_flags = (void*)nothing;
    env->set_verbose = (void*)nothing;
    env->txn_checkpoint = (void*)nothing;
    env->txn_begin = (void*)nothing;
    env->get_open_flags = (void*)nothing;
    env->mutex_set_tas_spins = (void*)nothing;
    env->set_alloc = (void*)nothing;
    env->set_cachesize = (void*)nothing;
    env->set_data_dir = (void*)nothing;
    env->set_errcall = (void*)nothing;
    env->set_errpfx = (void*)nothing;
    env->set_lg_bsize = (void*)nothing;
    env->set_lg_dir = db_set_lg_dir;
    env->set_lg_max = (void*)nothing;
    env->set_lg_regionmax = (void*)nothing;
    env->set_lk_max_lockers = (void*)nothing;
    env->set_lk_max_locks = (void*)nothing;
    env->set_lk_max_objects = (void*)nothing;
    env->set_shm_key = (void*)nothing;
    env->set_tx_max = (void*)nothing;
    env->log_flush = (void*)nothing;
    env->lock_stat = (void*)nothing;
    env->log_archive = (void*)nothing;
    env->memp_stat = (void*)nothing;
    env->memp_trickle = (void*)nothing;
    env->dbrename = (void*)nothing;
    env->stat = (void*)nothing;
    env->log_stat = (void*)nothing;
    env->txn_stat = (void*)nothing;
    *penv = env;
    return DB_SUCCESS;
}

int db_open(DB *db, DB_TXN *txnid, const char *file,
            const char *database, DBTYPE type, u_int32_t flags, int mode)
{
    slapi_log_err(SLAPI_LOG_INFO, "bdb_ro", "%s: db=%p txnid=%p file=%s database=%s "
                  "type=0x%x flags=0x%x mode=0x%x\n", __FUNCTION__, db, 
                   txnid, file, database, type, flags, mode);
    if (*file == '/') {
        db->fname = slapi_ch_strdup(file);
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "bdb_ro",
                  "%s: db_home=%s\n", __FUNCTION__, db->env->db_home);
        db->fname = slapi_ch_smprintf("%s/%s", db->env->db_home, file);
    }
    slapi_log_err(SLAPI_LOG_ERR, "bdb_ro", "%s: db->fname=%s\n", __FUNCTION__, db->fname);
    db->impl = bdbreader_bdb_open(db->fname);
    db->open_flags = OPEN_FLAGS_OPEN;
    if (db->impl) {
        if (db_cursor(db, NULL, &db->cur, 0) != DB_SUCCESS) {
            slapi_log_err(SLAPI_LOG_ERR, "bdb_ro",
                          "%s: Failed to create cursor for %s database\n",
                          __FUNCTION__, db->fname);
            db_close(db, flags);
            return DB_OSERROR;
        }
        return DB_SUCCESS;
    }
    slapi_log_err(SLAPI_LOG_ERR, "bdb_ro",
                  "%s: Failed to open %s database\n", __FUNCTION__, db->fname);
    return DB_OSERROR;
}

int db_close(DB *db, u_int32_t flags)
{
    bdbreader_bdb_close((struct bdb_db **)&db->impl);
    db->impl = NULL;
    db->open_flags = OPEN_FLAGS_CLOSED;
    dbc_close(db->cur);
    slapi_ch_free_string(&db->fname);
    slapi_ch_free((void**)&db);
    return DB_SUCCESS;
}

int dbc_close(DBC *dbc)
{
    slapi_log_err(SLAPI_LOG_INFO, "bdb_ro", "%s: dbc=%p dbc->impl=%p\n", __FUNCTION__, dbc, dbc->impl);
    bdbreader_cur_close((struct bdb_cur **)&dbc->impl);
    slapi_ch_free((void **)&dbc);
    return DB_SUCCESS;
}

int dbc_del(DBC *dbc, u_int32_t flags)
{
    return DB_NOTSUPPORTED;
}

int copy_val(const DBT *from, DBT *to)
{
    switch (to->flags) {
        case DB_DBT_MALLOC:
            to->size = from->size;
            if (from->data == NULL) {
                to->data = NULL;
            } else if (from->size > 0) {
                to->data = slapi_ch_malloc(from->size);
                memcpy(to->data, from->data, from->size);
            }
            return DB_SUCCESS;
        case DB_DBT_REALLOC:
            to->size = from->size;
            if (from->data == NULL) {
                to->data = NULL;
            } else if (from->size > 0) {
                to->data = slapi_ch_realloc(to->data, from->size);
                memcpy(to->data, from->data, from->size);
            }
            return DB_SUCCESS;
        case DB_DBT_USERMEM:
            to->size = from->size;
            if (from->size > to->ulen) {
                return DB_BUFFER_SMALL;
            }
            memcpy(to->data, from->data, from->size);
            return DB_SUCCESS;
    }
    return DB_NOTSUPPORTED;
}


int store_val(int rc, DBC *dbc, DBT *key, DBT *data)
{
    DBT k1 = {0};
    DBT d1 = {0};

    if (rc == DB_SUCCESS) {
        rc = bdbreader_cur_getcurval(dbc->impl, &k1.data, &k1.size,  &d1.data, &d1.size);
    }
    if (rc == DB_SUCCESS) {
        rc = copy_val(&k1, key);
    }
    if (rc == DB_SUCCESS) {
        rc = copy_val(&d1, data);
    }
    return rc;
}


int dbc_get(DBC *dbc, DBT *key, DBT *data, u_int32_t flags)
{
    static const char *flagnames[] = {
        "0", "DB_FIRST", "DB_CURRENT", "DB_GET_BOTH", "DB_GET_BOTH_RANGE",
        "DB_GET_RECNO", "DB_LAST", "DB_NEXT", "DB_NEXT_DUP", "DB_NEXT_NODUP",
        "DB_NODUPDATA", "DB_PREV", "DB_SET", "DB_SET_RANGE", "DB_SET_RECNO",
        "DB_UNKNOWN" };
    static const char *errname[] = {
        "DB_SUCCESS", "DB_NOTFOUND", "DB_BUFFER_SMALL",
        "DB_KEYEXIST", "DB_RUNRECOVERY", "DB_NOTSUPPORTED", "DB_LOCK_DEADLOCK", "DB_OSERROR" };

    slapi_log_err(SLAPI_LOG_ERR, "bdb_ro", "==> dbc_get(key=%s flags=%d:%s)\n", LOGK(key), flags, LOGN(flags, flagnames));

    int rc = DB_NOTSUPPORTED;
    DBT k1 = {0};
    DBT d1 = {0};

    switch (flags) {
        case DB_FIRST:
            rc = bdbreader_cur_lookup_ge(dbc->impl, NULL, 0);
            if (rc == DB_NOTFOUND) {
                rc = bdbreader_cur_next(dbc->impl);
            }
            rc = store_val(rc, dbc, key, data);
            break;
        case DB_CURRENT:
            rc = store_val(rc, dbc, key, data);
            break;
        case DB_GET_BOTH:
            break;
        case DB_GET_BOTH_RANGE:
            break;
        case DB_GET_RECNO:
            break;
        case DB_LAST:
            rc = DB_SUCCESS;
            while (rc == DB_SUCCESS) {
                rc = bdbreader_cur_next(dbc->impl);
                if (rc == DB_SUCCESS) {
                    rc = bdbreader_cur_getcurval(dbc->impl, &k1.data, &k1.size, &d1.data, &d1.size);
                }
                if (rc == DB_NOTFOUND) {
                    rc = copy_val(&k1, key) | copy_val(&d1, data);
                    break;
                }
            }
            break;
        case DB_NEXT:
            rc = bdbreader_cur_next(dbc->impl);
            rc = store_val(rc, dbc, key, data);
            break;
        case DB_NEXT_DUP:
            rc = bdbreader_cur_next(dbc->impl);
            if (rc == DB_SUCCESS) {
                rc = bdbreader_cur_getcurval(dbc->impl, &k1.data, &k1.size, NULL, NULL);
                if (rc == DB_SUCCESS) {
                    if (k1.size == key->size && memcmp(k1.data, key->data, k1.size) == 0) {
                        rc = store_val(rc, dbc, key, data);
                    } else {
                        rc = DB_NOTFOUND;
                    }
                }
            }
            break;
        case DB_NEXT_NODUP:
            rc = DB_SUCCESS;
            while (rc == DB_SUCCESS) {
                rc = bdbreader_cur_next(dbc->impl);
                if (rc == DB_SUCCESS) {
                    rc = bdbreader_cur_getcurval(dbc->impl, &k1.data, &k1.size, NULL, NULL);
                }
                if (k1.size != key->size || memcmp(k1.data, key->data, k1.size) != 0) {
                    rc = store_val(rc, dbc, key, data);
                    break;
                }
            }
            break;
        case DB_NODUPDATA:
            break;
        case DB_PREV:
            break;
        case 0:
            /* _get_and_add_parent_rdns set the flags ==> probably a bug
             *   (according BDB C API)
             * But lets assume it is a DB_SET
             */
        case DB_SET:
            rc = bdbreader_cur_lookup(dbc->impl, key->data, key->size);
            rc = store_val(rc, dbc, key, data);
            break;
        case DB_SET_RANGE:
            rc = bdbreader_cur_lookup_ge(dbc->impl, key->data, key->size);
            rc = store_val(rc, dbc, key, data);
            break;
        case DB_SET_RECNO:
            break;
    }

    slapi_log_err(SLAPI_LOG_ERR, "bdb_ro", "==> dbc_get(flags=%d:%s) rc=%d:%s\n",
                  flags, LOGN(flags, flagnames), rc, LOGN(rc, errname));
    if (key && key->data && key->size) {
        hexadump("Key", key->data, 0, key->size);
    }
    if (data && data->data && data->size) {
        hexadump("Data", data->data, 0, data->size);
    }
    return rc;
}

int dbc_put(DBC *dbc, DBT *key, DBT *data, u_int32_t flags)
{
    return DB_NOTSUPPORTED;
}

int dbc_count(DBC *dbc, void *countp, u_int32_t flags)
{
    return DB_NOTSUPPORTED;
}

int db_cursor(DB *db, DB_TXN *txnid, DBC **cursorp, u_int32_t flags)
{
    DBC *dbc = (void*)slapi_ch_calloc(1, sizeof *dbc);

    *cursorp = dbc;
    dbc->dbp = db;

    dbc->c_close = dbc_close;
    dbc->c_del = dbc_del;
    dbc->c_get = dbc_get;
    dbc->c_put = dbc_put;
    dbc->c_count = dbc_count;
    dbc->impl = bdbreader_cur_open(db->impl);

    slapi_log_err(SLAPI_LOG_INFO, "bdb_ro", "%s: dbc=%p dbc->impl=%p\n", __FUNCTION__, dbc, dbc->impl);
    return (db->impl == NULL) ? DB_OSERROR : DB_SUCCESS;
}

int db_del(DB *db, DB_TXN *txnid, DBT *key, u_int32_t flags)
{
    return DB_NOTSUPPORTED;
}

int db_put(DB *db, DB_TXN *txnid, DBT *key, DBT *data, u_int32_t flags)
{
    return DB_NOTSUPPORTED;
}

int db_get(DB *db, DB_TXN *txnid, DBT *key, DBT *data, u_int32_t flags)
{
    int rc = bdbreader_cur_lookup(db->cur->impl, key->data, key->size);
    if (rc == DB_SUCCESS) {
        rc = dbc_get(db->cur, key, data, flags);
    }
    return rc;
}

int db_create(DB **pdb, DB_ENV *env, u_int32_t flags)
{
    DB *db = (void*)slapi_ch_calloc(1, sizeof *db);
    db->close = db_close;
    db->compact = (void*)nothing;
    db->cursor = db_cursor;
    db->del = db_del;
    db->get = db_get;
    db->open = db_open;
    db->put = db_put;
    db->remove = (void*)nothing;
    db->rename = (void*)nothing;
    db->set_bt_compare = (void*)nothing;
    db->set_dup_compare = (void*)nothing;
    db->set_flags = (void*)nothing;
    db->set_pagesize = (void*)nothing;
    db->verify = (void*)nothing;
    db->get_type = (void*)nothing;
    db->stat = (void*)nothing;
    db->env = env;
    db->open_flags = OPEN_FLAGS_CLOSED;
    *pdb = db;
    return DB_SUCCESS;
}

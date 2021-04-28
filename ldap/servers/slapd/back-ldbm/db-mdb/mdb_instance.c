/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "mdb_layer.h"


/* Info file used to check version and get parameters needed to open the db in dbscan case */

#define INFOFILE    "INFO.mdb"
#define DBNAMES     "__DBNAMES"

typedef struct {
    char *name;
    int vtype;
    int voffset;
    int namelen;
} dbmdb_descinfo_t;

/* Following table describes the infofile items */
static dbmdb_descinfo_t dbmdb_descinfo[] = {
    { "LIBVERSION", CONFIG_TYPE_INT, offsetof(dbmdb_ctx_t, info.libversion) },
    { "DATAVERSION", CONFIG_TYPE_INT, offsetof(dbmdb_ctx_t, info.dataversion) },
    { "MAXSIZE", CONFIG_TYPE_UINT64, offsetof(dbmdb_ctx_t, startcfg.max_size) },
    { "MAXREADERS", CONFIG_TYPE_INT, offsetof(dbmdb_ctx_t, startcfg.max_readers) },
    { "MAXDBS", CONFIG_TYPE_INT, offsetof(dbmdb_ctx_t, startcfg.max_dbs) },
    { 0 }
};

/* helper to pretty print flags values */
typedef struct {
    char *name;
    int val;
} flagsdesc_t;

static flagsdesc_t mdb_env_flags_desc[] = {
    { "MDB_FIXEDMAP", MDB_FIXEDMAP},
    { "MDB_NOSUBDIR", MDB_NOSUBDIR},
    { "MDB_NOSYNC", MDB_NOSYNC},
    { "MDB_RDONLY", MDB_RDONLY},
    { "MDB_NOMETASYNC", MDB_NOMETASYNC},
    { "MDB_WRITEMAP", MDB_WRITEMAP},
    { "MDB_MAPASYNC", MDB_MAPASYNC},
    { "MDB_NOTLS", MDB_NOTLS},
    { "MDB_NOLOCK", MDB_NOLOCK},
    { "MDB_NORDAHEAD", MDB_NORDAHEAD},
    { "MDB_NOMEMINIT", MDB_NOMEMINIT},
    { 0 }
};

static flagsdesc_t mdb_op_flags_desc[] = {
    { "MDB_NOOVERWRITE", MDB_NOOVERWRITE},
    { "MDB_NODUPDATA", MDB_NODUPDATA},
    { "MDB_CURRENT", MDB_CURRENT},
    { "MDB_RESERVE", MDB_RESERVE},
    { "MDB_APPEND", MDB_APPEND},
    { "MDB_APPENDDUP", MDB_APPENDDUP},
    { "MDB_MULTIPLE", MDB_MULTIPLE},
    { 0 }
};

static flagsdesc_t mdb_dbi_flags_desc[] = {
    { "MDB_REVERSEKEY", MDB_REVERSEKEY},
    { "MDB_DUPSORT", MDB_DUPSORT},
    { "MDB_INTEGERKEY", MDB_INTEGERKEY},
    { "MDB_DUPFIXED", MDB_DUPFIXED},
    { "MDB_INTEGERDUP", MDB_INTEGERDUP},
    { "MDB_REVERSEDUP", MDB_REVERSEDUP},
    { "MDB_CREATE", MDB_CREATE},
    { 0 }
};

static flagsdesc_t mdb_state_desc[] = {
    { "DBIST_DIRTY", DBIST_DIRTY },
    { 0 }
};

/*
 * Search index for dbname in ctx->dbis.
 * Note: caller must held ctx->dbis_lock 
 * Return code:
 *    >= 0  ctx->dbis[rc] is the entry for dbname
 *    <0 dbname is not in the list and may be inserted in ctx->dbis[-rc-1] 
 */
int dbmdb_db_lookup(dbmdb_ctx_t *ctx, const char *dbname)
{
    int imax = ctx->nbdbis-1;
    int imin = 0;
    int rc = 0;
    int i;

    while (imax >= imin) {
        i = (imin + imax) / 2;
        rc = strcmp(ctx->dbis[i].dbname, dbname);
        if (rc == 0) {
            return i;
        }
        if (rc>0) {
            imax = i-1;
        } else {
            imin = i+1;
        }
    }
    return -1 - imin; 
}

int dbmdb_dbitxn_begin(dbmdb_cursor_t *dbicur, const char *funcname, MDB_txn *parent, int readonly)
{
    int rc = mdb_txn_begin(dbicur->dbi.env, NULL, readonly, &dbicur->txn);
    if (0 != rc) {
        slapi_log_err(SLAPI_LOG_ERR,
                  "dbmdb_get_db", "Failed to open a transaction in function %s. Database instance is: %s. err=%d %s\n",
                  funcname, dbicur->dbi.dbname, rc, mdb_strerror(rc));
    }
    return rc;
}

int dbmdb_dbitxn_end(dbmdb_cursor_t *dbicur, const char *funcname, int return_code)
{
    if (dbicur->txn) {
        if (0 == return_code) {
            return_code = mdb_txn_commit(dbicur->txn);
            dbicur->txn = NULL;
            if (0 != return_code) {
                slapi_log_err(SLAPI_LOG_ERR,
                        "dbmdb_get_db", "Failed to commit a transaction in function %s. Database instance is: %s. err=%d %s\n",
                        funcname, dbicur->dbi.dbname, return_code, mdb_strerror(return_code));
            }
        } else {
            mdb_txn_abort(dbicur->txn);
        }
        dbicur->txn = NULL;
    }
    return return_code;
}


#define MDB_DBIOPEN_MASK (MDB_REVERSEKEY | MDB_DUPSORT | MDB_INTEGERKEY |  \
                MDB_DUPFIXED | MDB_INTEGERDUP | MDB_REVERSEDUP | MDB_CREATE)

/* 
 * Flags:
 *     MDB_CREATE:  create the database if not alreay created
 *     any of MDB_DBIOPEN_MASK 
 */
int dbmdb_open_dbname(dbmdb_dbi_t *dbi, dbmdb_ctx_t *ctx, const char *dbname, int flags)
{
    int rc = 0;
    int n = 0;
    dbistate_t fstate = {0};        /* database State stored in DBNAMES database */
    dbmdb_cursor_t cur = {0};       /* To manage txn */
    MDB_val key = {0};
    MDB_val data = {0};
    int idx;                        /* Position in ctx->dbis table */

    dbi->env = ctx->env;
    dbi->dbname = dbname;
    dbi->dbi = 0;
    cur.dbi = *dbi;
    fstate.flags = flags & MDB_DBIOPEN_MASK;
    key.mv_data = (void*)dbname;
    key.mv_size = strlen(dbname)+1;    /* Includes final \0 */
    data.mv_data = &fstate;
    data.mv_size = sizeof fstate;

    if (ctx->readonly)
        flags |= MDB_RDONLY;

    pthread_mutex_lock(&ctx->dbis_lock);

    /* Lookup for db handles in dbilist table */
    idx = dbmdb_db_lookup(ctx, dbname);
    if (idx>0) {
        dbi->dbi = ctx->dbis[idx].dbi;
        fstate = ctx->dbis[idx].state;
    } else if (flags & MDB_RDONLY) {
        if (ctx->nbdbis > 0) {
            /* Should not try to add new db instances in read mode */
            rc = MDB_NOTFOUND;
            goto openfail;
        }
        /* We are trying to open __DBNAMES db in readonly mode */
        flags &= ~MDB_CREATE;
    } else if (ctx->nbdbis >= ctx->startcfg.max_dbs) {
        rc = MDB_DBS_FULL;
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_open_cursor",
            "Cannot open database %s (database table is full)\n", dbname);
        goto openfail;
    }
    
    if (!dbi->dbi) {
        /* open or create the dbname database */
        rc = dbmdb_dbitxn_begin(&cur, "dbmdb_open_dbname", NULL, flags & MDB_RDONLY);
        if (rc) {
            goto openfail;
        }
        rc = mdb_dbi_open(cur.txn, dbname, (flags & MDB_DBIOPEN_MASK), &dbi->dbi);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_open_cursor",
                "Failed to open %s database err=%d: %s\n", dbname, rc, mdb_strerror(rc));
            goto openfail;
        }
        if (ctx->nbdbis == 0) {
            /* First open database should be DBNAMES  - lets save its handle in global context */
            PR_ASSERT(strcmp(dbname, DBNAMES) == 0);
            ctx->dbinames_dbi = dbi->dbi;
        } else if (idx<0) {
            /* Adding the entry in DBNAMES while holding the txn */
            rc = mdb_put(cur.txn, ctx->dbinames_dbi, &key, &data, 0);
            if (rc) {
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_open_cursor",
                    "Failed to add item in DBNAMES database err=%d: %s while opening database %s\n", rc, mdb_strerror(rc), dbname);
                goto openfail;
            }
        }
        rc = dbmdb_dbitxn_end(&cur, "dbmdb_open_dbname", 0);
        if (rc) {
            goto openfail;
        }
        if (idx>0) {
            ctx->dbis[idx].dbi = dbi->dbi;
        } else {
            /* Insert new database instance in dbilist */
            idx = -idx -1;  /* Get the insert position */
            n = ctx->nbdbis - idx;
            ctx->nbdbis++;
            if (n>0) {
                bcopy(&ctx->dbis[idx], &ctx->dbis[idx+1], n * sizeof (dbmdb_dbi_t));
            }
            ctx->dbis[idx].dbname = slapi_ch_strdup(dbname);
            ctx->dbis[idx].state = fstate;
            ctx->dbis[idx].dbi = dbi->dbi;
        }
    }
    dbi->state = fstate;
openfail:
    rc = dbmdb_dbitxn_end(&cur, "dbmdb_open_dbname", rc);
    pthread_mutex_unlock(&ctx->dbis_lock);
    return rc;
}

int dbmdb_open_cursor(dbmdb_cursor_t *dbicur, dbmdb_ctx_t *ctx, const char *dbname, int flags)
{
    int rc = dbmdb_open_dbname(&dbicur->dbi, ctx, dbname, flags);
    if (rc) {
        return rc;
    }
    if (ctx->readonly)
        flags |= MDB_RDONLY;
    rc = dbmdb_dbitxn_begin(dbicur, "dbmdb_open_cursor", NULL, flags & MDB_RDONLY);
    if (rc) {
        return rc;
    }
    rc = mdb_cursor_open(dbicur->txn, dbicur->dbi.dbi, &dbicur->cur);
    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_open_cursor",
                "Failed to open a txn err=%d: %s\n", rc, mdb_strerror(rc));
        dbmdb_dbitxn_end(dbicur, "dbmdb_open_cursor", 0);
        return rc;
    }
    return rc;
}


static int dbmdb_init_dbilist(dbmdb_ctx_t *ctx)
{
    dbmdb_cursor_t dbicur = {0};
    MDB_val data = {0};
    MDB_val key = {0};
    int rc = 0;


    ctx->dbis = calloc(ctx->startcfg.max_dbs, sizeof (dbmdb_dbi_t));
    /* open or create the DBNAMES database */
    rc = dbmdb_open_cursor(&dbicur, ctx, DBNAMES, MDB_CREATE);
    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_init_dbilist",
                "Failed to open or create DBNAMES database err=%d: %s\n", rc, mdb_strerror(rc));
        return rc;
    }

    rc = mdb_cursor_get(dbicur.cur, &key, &data, MDB_FIRST);
    while (rc == 0) {
        if (ctx->nbdbis >= ctx->startcfg.max_dbs) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_init_dbilist",
                "Too many databases in DBNAMES (%d/%d).\n", ctx->nbdbis, ctx->startcfg.max_dbs);
            mdb_cursor_close(dbicur.cur);
            mdb_txn_abort(dbicur.txn);
        }
        ctx->dbis[ctx->nbdbis].dbname = slapi_ch_strdup(key.mv_data);
        ctx->dbis[ctx->nbdbis].state = *(dbistate_t*)(data.mv_data);
        ctx->dbis[ctx->nbdbis].dbi = 0;
        ctx->dbis[ctx->nbdbis].env = ctx->env;
        ctx->nbdbis++;
        rc = mdb_cursor_get(dbicur.cur, &key, &data, MDB_NEXT);
    }
    mdb_cursor_close(dbicur.cur);
    mdb_txn_abort(dbicur.txn);
    if (rc == MDB_NOTFOUND) {
        rc = 0;
    }
    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_init_dbilist",
                "Failed to read DBNAMES database err=%d: %s\n", rc, mdb_strerror(rc));
    }
    return rc;
}



/* Helper function that find info descriptor which match the line */
static dbmdb_descinfo_t *get_descinfo(const char *line)
{
    dbmdb_descinfo_t *pti;
    for (pti=dbmdb_descinfo; pti->name; pti++) {
        if (!pti->namelen) {
            pti->namelen = strlen(pti->name);
        }
        if (line[pti->namelen] == '=' && strncmp(line, pti->name, pti->namelen) == 0) {
            return pti;
        }
    }
    return NULL;
}

/* Convert string to value according to its type */
static void s2v(int vtype, const char *str, void *value)
{
    switch(vtype) {
        case CONFIG_TYPE_INT:
            sscanf(str, "%d", (int*)value);
            break;
        case CONFIG_TYPE_UINT64:
            sscanf(str, "%llu", (unsigned long long*)value);
            break;
        default:
            PR_ASSERT(0);
            break;
    }
}

/* Convert value to string according to its type */
static void v2s(int vtype, const void *value, char *str)
{
    switch(vtype) {
        case CONFIG_TYPE_INT:
            sprintf(str, "%d", *(int*)value);
            break;
        case CONFIG_TYPE_UINT64:
            sprintf(str, "%llu", *(unsigned long long*)value);
            break;
        default:
            PR_ASSERT(0);
            break;
    }
}

static int dbmdb_read_infofile(dbmdb_ctx_t *ctx, int log_errors)
{
    char filename[MAXPATHLEN];
    char line [40];
    FILE *f;

    PR_snprintf(filename, MAXPATHLEN, "%s/%s", ctx->home, INFOFILE);
    f = fopen(filename, "r");
    if (!f) {
        if (log_errors) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_read_infofile",
                "Failed to open info file %s errno=%d\n", filename, errno);
        }
        return LDAP_UNWILLING_TO_PERFORM;
    }
    while (fgets(line, (sizeof line), f)) {
        dbmdb_descinfo_t *pti = get_descinfo(line);
        if (pti) {
            s2v(pti->vtype, &line[pti->namelen+1], ((char*)ctx)+pti->voffset);
        }
    }
    fclose(f);
    return 0;
}


static int dbmdb_write_infofile(dbmdb_ctx_t *ctx)
{
    char filename[MAXPATHLEN+10];
    char val [32];
    FILE *f;
    dbmdb_descinfo_t *pti;
    
    PR_snprintf(filename, MAXPATHLEN, "%s/%s", ctx->home, INFOFILE);
    f = fopen(filename, "w");
    if (!f) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_write_infofile",
                "Failed to open info file %s errno=%d\n", filename, errno);
        return LDAP_UNWILLING_TO_PERFORM;
    }
    for (pti=dbmdb_descinfo; !ferror(f) && pti->name; pti++) {
        v2s(pti->vtype, ((char*)ctx)+pti->voffset, val);
        fprintf(f, "%s=%s\n", pti->name, val);
    }
    if (ferror(f)) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_write_infofile",
                "Failed to write info file %s errno=%d\n", filename, errno);
        fclose(f);
        return LDAP_UNWILLING_TO_PERFORM;
    }

    fclose(f);
    return 0;
}

/* Compute starting config parameter from dse config */
void dbmdb_init_startcfg(dbmdb_ctx_t *ctx)
{
    ctx->startcfg = ctx->dsecfg;
    if (ctx->startcfg.max_size == 0) {
        if (ctx->limits.max_size > ctx->limits.disk_reserve) {
            ctx->startcfg.max_size = ctx->limits.max_size - ctx->limits.disk_reserve;
        } else {
            ctx->startcfg.max_size = ctx->limits.max_size;
        }
    }

    if (ctx->startcfg.max_readers == 0) {
        ctx->startcfg.max_readers = DBMDB_READERS_DEFAULT;
    }
    if (ctx->startcfg.max_readers < ctx->limits.min_readers) {
        ctx->startcfg.max_readers = ctx->limits.min_readers;
    }

    if (ctx->startcfg.max_dbs == 0) {
        ctx->startcfg.max_dbs = DBMDB_DBS_DEFAULT;
    }
    if (ctx->startcfg.max_dbs < ctx->limits.min_dbs) {
        ctx->startcfg.max_dbs = ctx->limits.min_dbs;
    }
}

/* 
 * Perform the global actions needed when upgrading the database context
 * from olddatavsersion to ctx->info.dataversion
 * Note: actions performed when upgrading a specific backend are done in 
 * dbmdb_instance_start
 */
static int dbmdb_global_upgrade(dbmdb_ctx_t *ctx, int olddatavsersion)
{
    return 0;
}

int dbmdb_make_env(dbmdb_ctx_t *ctx, int readOnly, mdb_mode_t mode)
{
    MDB_env *env = NULL;
    int flags = 0;
    dbmdb_info_t infofileinfo = {0};
    dbmdb_info_t curinfo = ctx->info;
    int rc = 0;

    ctx->readonly = readOnly;
    /* read the info file */
    rc = dbmdb_read_infofile(ctx, !ctx->startcfg.dseloaded);
    infofileinfo = ctx->info;

    if (ctx->dsecfg.dseloaded) {
        /* Config has been provided ==> generate/update the info file */
        ctx->info = curinfo;    /* Lets ignore the read info file for now */
        if (!ctx->startcfg.dseloaded) {
            dbmdb_init_startcfg(ctx);
        }
        rc = dbmdb_write_infofile(ctx);
    } else {
        /* No Config ==> read it from info file */
    }
    if (rc) {
        return rc;
    }
    if (readOnly) {
        flags = MDB_RDONLY;
    }

    rc = mdb_env_create(&env);
    ctx->env = env;
    if (rc == 0) {
        rc =  mdb_env_set_mapsize(env, ctx->startcfg.max_size);
    }
    if (rc == 0) {
        rc =  mdb_env_set_maxdbs(env, ctx->startcfg.max_dbs);
    }
    if (rc == 0) {
        rc =  mdb_env_set_maxreaders(env, ctx->startcfg.max_readers);
    }
    if (rc == 0) {
        rc =  mdb_env_open(env, ctx->home, flags, mode);
    }
    if (rc == 0 && infofileinfo.dataversion && infofileinfo.dataversion != curinfo.dataversion) {
        rc = dbmdb_global_upgrade(ctx, infofileinfo.dataversion);
    }
    if (rc == 0) {
        rc =  dbmdb_init_dbilist(ctx);
    }

    if (rc != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_make_env",
                "Failed to initialize mdb environment err=%d: %s\n", rc, mdb_strerror(rc));
    }
    if (rc != 0 && env) {
        ctx->env = NULL;
        mdb_env_close(env);
    }
    return rc;
}

/* convert database instance from dbmdb plugin instance to dbimpl instance */
void dbmdb_mdbdbi2dbi_db(const dbmdb_dbi_t *dbi, dbi_db_t **ppDB)
{
    dbmdb_dbi_t *db = (dbmdb_dbi_t *) slapi_ch_calloc(1, sizeof (dbmdb_dbi_t));   
    *db = *dbi;
    *ppDB = (dbi_db_t *)db;
}



/* close the database env and release the context resource */
void dbmdb_ctx_close(dbmdb_ctx_t *ctx)
{
    if (ctx->env) {
        mdb_env_close(ctx->env);
        ctx->env = NULL;
    }
    slapi_ch_free((void**)&ctx->dbis);
    pthread_mutex_destroy(&ctx->dbis_lock);
}

int append_str(char *buff, int bufsize, int pos, const char *str1, const char *str2)
{
    int l1 = strlen(str1);
    int l2 = strlen(str2);
    if (pos+l1+l2+1 < bufsize) {
        strcpy(buff+pos, str1);
        strcpy(buff+pos+l1, str2);
        buff[pos+l1+l2] = 0;
        pos += l1+l2;
    }
    return pos;
}

int append_flags(char *buff, int bufsize, int pos, const char *name, int flags, flagsdesc_t *desc)
{
    int remainder = flags;
    char b[12];

    pos = append_str(buff, bufsize, pos, name, ": ");
    for (; desc->name; desc++) {
        if ((flags & desc->val) == desc->val) {
            pos = append_str(buff, bufsize, pos, desc->name, "|");
            remainder &= ~desc->val;
        }
    }
    snprintf(b, (sizeof b), "0x%x", remainder);
    pos = append_str(buff, bufsize, pos, b, " ");
    return pos;
}

dbi_dbslist_t *dbmdb_list_dbs(const char *dbhome)
{
    dbi_dbslist_t *dbs = NULL;
    dbmdb_ctx_t ctx = {0};
    int len = 0;
    int i;

    strncpy(ctx.home, dbhome, MAXPATHLEN);
    if (dbmdb_make_env(&ctx, 1, 0444)) {
        return NULL;
    }
    dbs = (dbi_dbslist_t*)slapi_ch_calloc(ctx.nbdbis+1, sizeof (dbi_dbslist_t));
    for (i=0; i<ctx.nbdbis; i++) {
        PR_snprintf(dbs[i].filename, PATH_MAX, "%s/%s", dbhome, ctx.dbis[i].dbname);
        len = 0;
        len = append_flags(dbs[i].info, PATH_MAX, len, "flags", ctx.dbis[i].state.flags, mdb_dbi_flags_desc); 
        len = append_flags(dbs[i].info, PATH_MAX, len, "state", ctx.dbis[i].state.state, mdb_state_desc); 
        PR_snprintf(dbs[i].info+len, PATH_MAX-len, " dataversion: %d", ctx.dbis[i].state.dataversion);
    }
    dbs[i].filename[0]=0;
    dbmdb_ctx_close(&ctx);
    return dbs;
}

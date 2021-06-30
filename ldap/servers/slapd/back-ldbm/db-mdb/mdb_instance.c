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

/* Flags allowed in mdb_dbi_open */
#define MDB_DBIOPEN_MASK (MDB_REVERSEKEY | MDB_DUPSORT | MDB_INTEGERKEY |  \
                MDB_DUPFIXED | MDB_INTEGERDUP | MDB_REVERSEDUP | MDB_CREATE)

/* Helper to read/write info file */
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

/* Context needed when having to spawn a new thread to open a dbi */
typedef struct {
    dbmdb_dbi_t *dbi;
    dbmdb_ctx_t *ctx;
    const char *dbname;
    int flags;                /* Open flags */
    MDB_val key;
    MDB_val data;
    struct attrinfo *ai;
    MDB_cmp_func *dupsort_fn;  /* dbi dupsort callback */
    dbistate_t fstate;        /* database State stored in DBNAMES database */
    int rc;
} dbi_open_ctx_t;

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

/*
 * Rules:
 * NULL comes before anything else.
 * Otherwise, strcmp(elem_a->rdn_elem_nrdn_rdn - elem_b->rdn_elem_nrdn_rdn) is
 * returned.
 */
int
dbmdb_entryrdn_compare_dups(const MDB_val *a, const MDB_val *b)
{
    if (NULL == a) {
        if (NULL == b) {
            return 0;
        } else {
            return -1;
        }
    } else if (NULL == b) {
        return 1;
    }
    return entryrdn_compare_rdn_elem(a->mv_data, b->mv_data);
}

static PRBool
is_dbfile(const char *dbname, const char *filename)
{
    int len = strlen(filename);
    if (strncmp(dbname, filename, len))
        return PR_FALSE;
    if (dbname[len] && strcmp(&dbname[len], LDBM_FILENAME_SUFFIX))
        return PR_FALSE;
    return PR_TRUE;
}

/* Determine mdb open flags according to dbi type */
static void
dbmdb_get_file_params(dbi_open_ctx_t *octx)
{
    const char *pt = strrchr(octx->dbname, '/');

    if (!pt) {
        pt = octx->dbname;
    } else {
        pt++;
    }
    if (is_dbfile(pt, LDBM_ENTRYRDN_STR)) {
        octx->fstate.flags |= MDB_DUPSORT;
        octx->dupsort_fn = dbmdb_entryrdn_compare_dups;
    } else if (is_dbfile(pt, ID2ENTRY)) {
        octx->fstate.flags |= MDB_INTEGERKEY;
        octx->dupsort_fn = dbmdb_entryrdn_compare_dups;
    } else if (strstr(pt,  DBNAMES)) {
        octx->fstate.flags |= 0;
        octx->dupsort_fn = NULL;
    } else if (strstr(pt,  CHANGELOG_PATTERN)) {
        octx->fstate.flags |= 0;
        octx->dupsort_fn = NULL;
    } else {
        octx->fstate.flags |= MDB_DUPSORT + MDB_INTEGERDUP + MDB_DUPFIXED;
        octx->dupsort_fn = NULL;
    }
}

static int
dbmdb_set_dbi_callbacks(dbi_open_ctx_t *octx)
{
    int rc = 0;
    if (!octx->ai) {
        /* Only dbi needing callbacks are thoses supporting duplicates (in other words: the index ones) */
        return 0;
    }
    if (!idl_get_idl_new()) {
       slapi_log_err(SLAPI_LOG_ERR, "dbmdb_set_dbi_callbacks",
                    "configuration error: should not use old idl scheme with mdb\n");
       return MDB_INVALID;
    }

   /*
    * May need to use mdb_set_dupsort / mdb_set_compare here
    * (ai->ai_indexmask & INDEX_VLV))
    *
    *   if (ai->ai_key_cmp_fn) {} /# set in attr_index_config() #/
    * This is so that we can have ordered keys in the index, so that
    * greater than/less than searches work on indexed attrs.  We had
    * to introduce this when we changed the integer key format from
    * a 32/64 bit value to a normalized string value.  The default
    * mdb key cmp is based on length and lexicographic order, which
    * does not work with integer strings.
    *
    * NOTE: If we ever need to use app_private for something else, we
    * will have to create some sort of data structure with different
    * fields for different uses.  We will also need to have a new()
    * function that creates and allocates that structure, and a
    * destroy() function that destroys the structure, and make sure
    * to call it when the DB* is closed and/or freed.
    */
    return rc;
}

/* Really open the dbi within mdb */
static void *dbmdb_mdb_open_dbname(void *arg)
{
    dbi_open_ctx_t *octx = arg;
    dbi_txn_t *txn = NULL;
    int open_flags;

    dbmdb_get_file_params(octx);
    octx->rc =  dbmdb_set_dbi_callbacks(octx);
    if (octx->rc) {
        return NULL;
    }
    open_flags = (octx->fstate.flags | octx->flags) & MDB_DBIOPEN_MASK;

    /* open or create the dbname database */
    /* First we need a txn */
    octx->rc = START_TXN(&txn, NULL, TXNFL_DBI);
    if (octx->rc) {
        return NULL;
    }
    if (octx->dbi->dbi == 0) {
        octx->rc = MDB_DBI_OPEN(TXN(txn), octx->dbname, open_flags, &octx->dbi->dbi);
    }
    if (octx->rc) {
        if (octx->rc != MDB_NOTFOUND) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_open_dbname",
                "Failed to open %s database err=%d: %s\n", octx->dbname, octx->rc, mdb_strerror(octx->rc));
        }
        goto openfail;
    }

    if (octx->ctx->nbdbis > 0 && (open_flags & MDB_CREATE)) {
        /* Adding the entry in DBNAMES while holding the txn */
        octx->rc = MDB_PUT(TXN(txn), octx->ctx->dbinames_dbi, &octx->key, &octx->data, 0);
        if (octx->rc) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_open_dbname",
                "Failed to add item in DBNAMES database err=%d: %s while opening database %s\n", octx->rc, mdb_strerror(octx->rc), octx->dbname);
            goto openfail;
        }
    }
    if (octx->dupsort_fn) {
        mdb_set_dupsort(TXN(txn), octx->dbi->dbi, octx->dupsort_fn);
    }
    if (octx->fstate.flags & MDB_TRUNCATE_DBI) {
        /* Lets commit the txn to insure that dbi is fully open
         * then restart a new txn to clear the dbi */
        octx->rc = END_TXN(&txn, octx->rc);
        if (!octx->rc) {
            return NULL;
        }
        octx->rc = START_TXN(&txn, NULL, TXNFL_DBI);
        if (octx->rc) {
            return NULL;
        }
        octx->rc = MDB_DROP(txn, octx->dbi->dbi, 0);
        if (octx->rc) {
            slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_get_db", "Failed to truncate the database instance %s. err=%d %s\n",
                       octx->dbname, octx->rc, mdb_strerror(octx->rc));
            goto openfail;
        }
    }

openfail:
    octx->rc = END_TXN(&txn, octx->rc);
    return NULL;
}


/*
 * Flags:
 *     MDB_CREATE:  create the database if not alreay created
 *     any of MDB_DBIOPEN_MASK
 */
static int dbmdb_open_dbname(dbmdb_dbi_t *dbi, dbmdb_ctx_t *ctx, const char *dbname, struct attrinfo *ai, int flags)
{
    const int allowed_flags = MDB_OPEN_DIRTY_DBI | MDB_MARK_DIRTY_DBI | MDB_TRUNCATE_DBI |
        MDB_RDONLY | MDB_CREATE;
    dbi_open_ctx_t octx = {0};
    int rc = 0;
    int n = 0;
    int idx;                        /* Position in ctx->dbis table */

    if (ctx->readonly || (flags&MDB_RDONLY)) {
        flags &= ~MDB_CREATE;
    }
    if (flags & MDB_MARK_DIRTY_DBI) {
        flags |= MDB_OPEN_DIRTY_DBI;
        octx.fstate.state = DBIST_DIRTY;
    }

    if (flags & ~allowed_flags) {
        char badflags[80];
        dbmdb_envflags2str(flags & ~allowed_flags, badflags, sizeof badflags);
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_open_dbname",
            "Unexpected flags %s when trying to open database %s (invalid flags) \n", badflags, dbname);
        return MDB_INVALID;
    }

    dbi->env = ctx->env;
    dbi->dbname = dbname;
    dbi->dbi = 0;
    octx.dbi = dbi;
    octx.ctx = ctx;
    octx.dbname = dbname;
    octx.flags = flags;
    octx.ai = ai;
    octx.key.mv_data = (void*)dbname;
    octx.key.mv_size = strlen(dbname)+1;    /* Includes final \0 */
    octx.data.mv_data = &octx.fstate;
    octx.data.mv_size = sizeof octx.fstate;

    pthread_mutex_lock(&ctx->dbis_lock);

    /* Lookup for db handles in dbilist table */
    idx = dbmdb_db_lookup(ctx, dbname);
    if (idx>=0) {
        *dbi = ctx->dbis[idx];
        octx.fstate = dbi->state;
    } else if (ctx->nbdbis >= ctx->startcfg.max_dbs) {
        rc = MDB_DBS_FULL;
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_open_dbname",
            "Cannot open database %s (database table is full)\n", dbname);
        goto openfail;
    }

    if ((octx.fstate.state & DBIST_DIRTY) && !(flags & MDB_OPEN_DIRTY_DBI)) {
        rc = MDB_BAD_DBI;
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_open_dbname",
            "Cannot open database %s (database is dirty)\n", dbname);
        goto openfail;
    }

    if (!dbi->dbi || (flags & (MDB_TRUNCATE_DBI|MDB_MARK_DIRTY_DBI))) {
        /* If dbi is not existing or need to be truncated, we need a write txn */
        /* Either we can get it directly in our thread or we spawn a new thread */
        if ((dbmdb_is_read_only_txn_thread() || (flags & MDB_RDONLY)) && !ctx->readonly) {
            /* Lets open the dbi in another thread so we can open a write txn */
            pthread_t tid;
            pthread_create(&tid, NULL, dbmdb_mdb_open_dbname, &octx);
            pthread_join(tid, NULL);
        } else {
            /* lets go on in same thread as we can start a sub txn */
            dbmdb_mdb_open_dbname(&octx);
        }
        rc = octx.rc;
        if (rc) {
            goto openfail;
        }
    }

    PR_ASSERT (dbi->dbi);

    if (ctx->nbdbis == 0) {
        /* First open database should be DBNAMES  - lets save its handle in global context */
        PR_ASSERT(strcmp(dbname, DBNAMES) == 0);
        ctx->dbinames_dbi = octx.dbi->dbi;
    }

    if (idx>=0) {
        ctx->dbis[idx].dbi = dbi->dbi;
    } else {
        /* Insert new database instance in dbilist */
        idx = -idx -1;  /* Get the insert position */
        n = ctx->nbdbis - idx;
        ctx->nbdbis++;
        if (n>0) {
            bcopy(&ctx->dbis[idx], &ctx->dbis[idx+1], n * sizeof (dbmdb_dbi_t));
        }
        ctx->dbis[idx].dbname = dbname;
        ctx->dbis[idx].state = octx.fstate;
        ctx->dbis[idx].dbi = octx.dbi->dbi;
    }
    dbi->state = octx.fstate;
openfail:
    pthread_mutex_unlock(&ctx->dbis_lock);
    return rc;
}

char *dbmdb_build_dbname(backend *be, const char *filename)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    int len = strlen(filename) - strlen(LDBM_FILENAME_SUFFIX);
    int has_suffix = (len > 0 && strcmp(filename+len, LDBM_FILENAME_SUFFIX) == 0);
    const char *suffix = has_suffix ? "" : LDBM_FILENAME_SUFFIX;

    PR_ASSERT(filename[0] != '/');
    if (strcmp(filename, DBNAMES) == 0) {
        return slapi_ch_strdup(DBNAMES);
    }
    if (strchr(filename, '/')) {
        return slapi_ch_smprintf("%s%s", filename, suffix);
    } else {
        return slapi_ch_smprintf("%s/%s%s", inst->inst_name, filename, suffix);
    }
}

int dbmdb_open_dbi_from_filename(dbmdb_dbi_t *dbi, backend *be, const char *filename, struct attrinfo *ai, int flags)
{
    struct ldbminfo *li = (struct ldbminfo *)(be->be_database->plg_private);
    char *dbname = dbmdb_build_dbname(be, filename);;
    dbmdb_ctx_t *ctx = MDB_CONFIG(li);
    int rc;

    rc = dbmdb_open_dbname(dbi, ctx, dbname, ai, flags);
    if (dbname != dbi->dbname) {
        slapi_ch_free_string(&dbname);
    }
    return rc;
}

int dbmdb_open_cursor(dbmdb_cursor_t *dbicur, dbmdb_ctx_t *ctx, dbmdb_dbi_t *dbi, int flags)
{
    int rc = 0;

    if (dbi) {
        dbicur->dbi = *dbi;
    } else {
        rc = dbmdb_open_dbname(&dbicur->dbi, ctx, DBNAMES, NULL, flags);
    }
    if (rc) {
        return rc;
    }

    if (ctx->readonly)
        flags |= MDB_RDONLY;
    rc = START_TXN(&dbicur->txn, NULL, 0);
    if (rc) {
        return rc;
    }
    rc = MDB_CURSOR_OPEN(TXN(dbicur->txn), dbicur->dbi.dbi, &dbicur->cur);
    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_open_cursor",
                "Failed to open a cursor err=%d: %s\n", rc, mdb_strerror(rc));
        END_TXN(&dbicur->txn, rc);
    }
    return rc;
}

int dbmdb_close_cursor(dbmdb_cursor_t *dbicur, int rc)
{
    if (dbicur->cur) {
        MDB_CURSOR_CLOSE(dbicur->cur);
    }
    rc = END_TXN(&dbicur->txn, rc);
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
    rc = dbmdb_open_cursor(&dbicur, ctx, NULL, MDB_CREATE);
    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_init_dbilist",
                "Failed to open or create DBNAMES database err=%d: %s\n", rc, mdb_strerror(rc));
        return rc;
    }

    rc = MDB_CURSOR_GET(dbicur.cur, &key, &data, MDB_FIRST);
    while (rc == 0) {
        if (ctx->nbdbis >= ctx->startcfg.max_dbs) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_init_dbilist",
                "Too many databases in DBNAMES (%d/%d).\n", ctx->nbdbis, ctx->startcfg.max_dbs);
            dbmdb_close_cursor(&dbicur, 1);
            return MDB_DBS_FULL;
        }
        ctx->dbis[ctx->nbdbis].dbname = slapi_ch_strdup(key.mv_data);
        ctx->dbis[ctx->nbdbis].state = *(dbistate_t*)(data.mv_data);
        ctx->dbis[ctx->nbdbis].dbi = 0;
        ctx->dbis[ctx->nbdbis].env = ctx->env;
        ctx->nbdbis++;
        rc = MDB_CURSOR_GET(dbicur.cur, &key, &data, MDB_NEXT);
    }
    dbmdb_close_cursor(&dbicur, 1);
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
    MDB_envinfo envinfo = {0};
    int rc = 0;

    init_mdbtxn(ctx);
    ctx->readonly = readOnly;
    /* read the info file - log error if comming from db-stat (because db and info is supposed to exists) */
    rc = dbmdb_read_infofile(ctx, !ctx->dsecfg.dseloaded);
    infofileinfo = ctx->info;       /* Save version info */

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
    if (rc ==0) {
        rc = mdb_env_info(env, &envinfo);
    }
    if (rc ==0) { /* Update the INFO file with the real size provided by the db */
        dbmdb_cfg_t oldcfg = ctx->startcfg;
        ctx->startcfg.max_size = envinfo.me_mapsize;
        ctx->startcfg.max_readers = envinfo.me_maxreaders;
        if (ctx->dsecfg.dseloaded && memcmp(&ctx->startcfg, &oldcfg, sizeof oldcfg)) {
            rc = dbmdb_write_infofile(ctx);
        }
    }

    slapi_log_err(SLAPI_LOG_INFO, "dbmdb_make_env", "MBD environment created with maxsize=%lu.\n", ctx->startcfg.max_size);
    slapi_log_err(SLAPI_LOG_INFO, "dbmdb_make_env", "MBD environment created with max readers=%d.\n", ctx->startcfg.max_readers);
    slapi_log_err(SLAPI_LOG_INFO, "dbmdb_make_env", "MBD environment created with max database instances=%d.\n", ctx->startcfg.max_dbs);

    /* If some upgrade is needed based on libmdb version, then another test must be done here.
     *  and the new test should be based on infofileinfo.libversion
     */
    if (rc == 0 && infofileinfo.dataversion && infofileinfo.dataversion != DBMDB_CURRENT_DATAVERSION) {
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
    db->dbname = slapi_ch_strdup(db->dbname);
    *ppDB = (dbi_db_t *)db;
}



/* close the database env and release the context resource */
void dbmdb_ctx_close(dbmdb_ctx_t *ctx)
{
    if (ctx->env) {
        mdb_env_close(ctx->env);
        ctx->env = NULL;
    }
    ctx->nbdbis = 0;
    slapi_ch_free((void**)&ctx->dbis);
    pthread_mutex_destroy(&ctx->dbis_lock);
    pthread_mutex_destroy(&ctx->rcmutex);
    pthread_rwlock_destroy(&ctx->dbmdb_env_lock);
}

/* API used by dbscan to list the dbs */
dbi_dbslist_t *dbmdb_list_dbs(const char *dbhome)
{
    dbi_dbslist_t *dbs = NULL;
    dbmdb_dbi_t dbi;
    dbmdb_ctx_t ctx = {0};
    int i;

    strncpy(ctx.home, dbhome, MAXPATHLEN);
    if (dbmdb_make_env(&ctx, 1, 0444)) {
        return NULL;
    }
    dbs = (dbi_dbslist_t*)slapi_ch_calloc(ctx.nbdbis+1, sizeof (dbi_dbslist_t));
    for (i=0; i<ctx.nbdbis; i++) {
        if (ctx.dbis[i].dbi == 0) {
            dbmdb_open_dbname(&dbi, &ctx, ctx.dbis[i].dbname, NULL, 0);
        }
        PR_snprintf(dbs[i].filename, PATH_MAX, "%s/%s", dbhome, ctx.dbis[i].dbname);
        dbmdb_format_dbslist_info(dbs[i].info, &ctx.dbis[i]);
    }
    dbs[i].filename[0]=0;
    dbmdb_ctx_close(&ctx);
    return dbs;
}

/* remove db instance by index in conf->dbis - caller must hold conf->dbis_lock */
int dbmdb_dbi_remove_from_idx(dbmdb_ctx_t *conf, dbi_txn_t *txn, int idx)
{
    dbmdb_dbi_t *ldb = &conf->dbis[idx];
    MDB_val key = {0};
    int rc = 0;

    key.mv_data = (char*)(ldb->dbname);
    key.mv_size = strlen(ldb->dbname)+1;
    if (0 == ldb->dbi) {
        /* dbi is not open ==> should open it. */
        rc = MDB_DBI_OPEN(TXN(txn), ldb->dbname, ldb->state.flags & MDB_DBIOPEN_MASK, &ldb->dbi);
        if (rc == MDB_NOTFOUND) {
            rc = 0;
        }
    }

    if (rc == 0 && ldb->dbi>0) {
        /* Remove db instance from database */
        rc = MDB_DROP(TXN(txn), ldb->dbi, 1);
    }
    if (rc == 0) {
        /* Remove db instance from DBNAMES */
        rc = mdb_del(TXN(txn), conf->dbinames_dbi, &key, &key);
        if (rc == MDB_NOTFOUND) {
            rc = 0;
        }
    }
    if (rc == 0 && idx >=0 && idx < conf->nbdbis) {
        /* Remove db instance from conf->dbis */
        int n = conf->nbdbis - idx - 1;
        conf->nbdbis--;
        if (n>0) {
            bcopy(&conf->dbis[idx+1], &conf->dbis[idx], n * sizeof (dbmdb_dbi_t));
        }
    }
    return rc;
}

/* Remove an open database */
int dbmdb_dbi_remove(dbmdb_ctx_t *conf, dbi_db_t **db)
{
    dbmdb_dbi_t *ldb = *db;
    dbi_txn_t *txn = NULL;
    int idx;
    int rc = 0;

    PR_ASSERT(conf);
    PR_ASSERT(ldb);
    pthread_mutex_lock(&conf->dbis_lock);
    rc = START_TXN(&txn, NULL, TXNFL_DBI);
    /* Lookup for db handle slot in dbilist table */
    idx = dbmdb_db_lookup(conf, ldb->dbname);
    if (rc == 0 && idx >=0) {
        PR_ASSERT(conf->dbis[idx].dbi == ldb->dbi);
        PR_ASSERT(strcmp(conf->dbis[idx].dbname, ldb->dbname) == 0);
        rc = dbmdb_dbi_remove_from_idx(conf, txn, idx);
        if (rc == 0) {
            *db = NULL;
        }
    }
    rc = END_TXN(&txn, rc);
    pthread_mutex_unlock(&conf->dbis_lock);
    return dbmdb_map_error(__FUNCTION__, rc);
}

/* Remove all databases belonging to an instance directory */
int dbmdb_dbi_rmdir(dbmdb_ctx_t *conf, const char *dirname)
{
    dbi_txn_t *txn = NULL;
    int idx;
    int rc = 0;
    int len = strlen(dirname);

    pthread_mutex_lock(&conf->dbis_lock);
    rc = START_TXN(&txn, NULL, TXNFL_DBI);
    for (idx=0; rc==0 && idx<conf->nbdbis; idx++) {
        if (strncmp(dirname, conf->dbis[idx].dbname, len) == 0 &&
            conf->dbis[idx].dbname[len] == '/') {
            rc = dbmdb_dbi_remove_from_idx(conf, txn, idx);
            if (rc == 0) {
                /* slot has been removed so next slot to process is still idx */
                idx--;
            }
        }
    }
    rc = END_TXN(&txn, rc);
    pthread_mutex_unlock(&conf->dbis_lock);
    return dbmdb_map_error(__FUNCTION__, rc);
}

int dbmdb_dbi_set_dirty(dbmdb_ctx_t *conf, dbmdb_dbi_t *dbi, int dirty_flags)
{
    dbi_txn_t *txn = NULL;
    MDB_val data = {0};
    MDB_val key = {0};
    dbmdb_dbi_t db;
    int idx;
    int rc = 0;

    key.mv_data = (char*)(dbi->dbname);
    key.mv_size = strlen(dbi->dbname)+1;

    pthread_mutex_lock(&conf->dbis_lock);
    rc = START_TXN(&txn, NULL, TXNFL_DBI);
    idx = dbmdb_db_lookup(conf, dbi->dbname);
    if (rc == 0 && idx >=0) {
        db = conf->dbis[idx];
        db.state.state = dirty_flags;
        PR_ASSERT(db.dbi == dbi->dbi);
        PR_ASSERT(strcmp(db.dbname, dbi->dbname) == 0);
        key.mv_data = (char*)(dbi->dbname);
        key.mv_size = strlen(dbi->dbname)+1;
        data.mv_data = &db.state;
        data.mv_size = sizeof db.state;
        rc = MDB_PUT(TXN(txn), conf->dbinames_dbi, &key, &data, 0);
    }
    rc = END_TXN(&txn, rc);
    if (rc == 0) {
        conf->dbis[idx].state.state = dirty_flags;
    }
    pthread_mutex_unlock(&conf->dbis_lock);
    return dbmdb_map_error(__FUNCTION__, rc);
}

int dbmdb_clear_dirty_flags(dbmdb_ctx_t *conf, const char *dirname)
{
    dbi_txn_t *txn = NULL;
    int idx;
    int rc = 0;
    int len = strlen(dirname);
    dbmdb_dbi_t *dbi;
    MDB_val data = {0};
    MDB_val key = {0};

    pthread_mutex_lock(&conf->dbis_lock);
    rc = START_TXN(&txn, NULL, TXNFL_DBI);
    for (idx=0; rc==0 && idx<conf->nbdbis; idx++) {
        dbi = &conf->dbis[idx];
        if ((dbi->state.state & DBIST_DIRTY) &&
            strncmp(dirname, dbi->dbname, len) == 0 &&
            dbi->dbname[len] == '/') {
            dbi->state.state &= ~DBIST_DIRTY;
            key.mv_data = (char*)(dbi->dbname);
            key.mv_size = strlen(dbi->dbname)+1;
            data.mv_data = &dbi->state;
            data.mv_size = sizeof dbi->state;
            rc |= MDB_PUT(TXN(txn), conf->dbinames_dbi, &key, &data, 0);
        }
    }
    rc = END_TXN(&txn, rc);
    pthread_mutex_unlock(&conf->dbis_lock);
    return dbmdb_map_error(__FUNCTION__, rc);
}

int dbmdb_recno_cache_get_mode(dbmdb_recno_cache_ctx_t *rcctx)
{
    struct ldbminfo *li = (struct ldbminfo *)rcctx->cursor->be->be_database->plg_private;
    int curdbi = mdb_cursor_dbi(rcctx->cursor->cur);
    MDB_txn *txn = mdb_cursor_txn(rcctx->cursor->cur);
    dbmdb_ctx_t *ctx = MDB_CONFIG(li);
    char *rcdbname = NULL;
    dbmdb_dbi_t *dbi;
    int idx = 0;
    int rc = 0;

    rcctx->mode = RCMODE_UNKNOWN;
    rcctx->cursortxn = txn;

    pthread_mutex_lock(&ctx->dbis_lock);
    /* first lets look up in the table */
    for (idx=0; rc==0 && idx<ctx->nbdbis; idx++) {
        dbi = &ctx->dbis[idx];
        if (curdbi == dbi->dbi) {
            rcdbname = slapi_ch_smprintf("~recno-cache/%s", dbi->dbname);
            rcctx->dbi = *dbi;
            break;
        }
    }
    if (!rcdbname) {
        /* Really weird: an open dbi should be in dbis table */
        return MDB_NOTFOUND;
    }
    /* Now lets search for the associated recno cache dbi */
    for (idx=0; rc==0 && idx<ctx->nbdbis; idx++) {
        dbi = &ctx->dbis[idx];
        if (strcmp(rcdbname, dbi->dbname) == 0 && dbi->dbi > 0) {
            rcctx->rcdbi = *dbi;
            rcctx->mode = RCMODE_USE_CURSOR_TXN;
            break;
        }
    }

    pthread_mutex_unlock(&ctx->dbis_lock);
    if (rcctx->mode == RCMODE_USE_CURSOR_TXN) {
        /* DBI cache was found,  Let check that it is usuable */
        slapi_ch_free_string(&rcdbname);
        rcctx->key.mv_data = "OK";
        rcctx->key.mv_size = 2;
        rc = mdb_get(txn, rcctx->rcdbi.dbi, &rcctx->key, &rcctx->data);
        if (rc) {
            rcctx->mode = RCMODE_UNKNOWN;
        }
        if (rc != MDB_NOTFOUND) {
            return rc;
        }
    }
    /* if rcdbname is set then cache is not open
     * if rcdbname is NULL then cache must be rebuild
     * ==> In both case A write TXN is needed
     *  (either as subtxn or in new thread)
     */
    rcctx->rcdbname = rcdbname;
    rcctx->env = ctx->env;
    rc = TXN_BEGIN(ctx->env, rcctx->cursortxn, 0, &txn);
    if (rc == 0) {
        TXN_ABORT(txn);
        txn = NULL;
        rcctx->mode = RCMODE_USE_SUBTXN;
    } else if (rcctx->rc == EINVAL) {
        rcctx->mode = RCMODE_USE_NEW_THREAD;
        rcctx->rc = 0;
    }
    return rc;
}

int dbmdb_open_recno_cache_dbi(dbmdb_recno_cache_ctx_t *rcctx)
{
    int rc = 0;
    if (rcctx->rcdbname) {
        rc = dbmdb_open_dbi_from_filename(&rcctx->rcdbi, rcctx->cursor->be, rcctx->rcdbname, NULL, 0);
        slapi_ch_free_string(&rcctx->rcdbname);
    }
    return rc;
}

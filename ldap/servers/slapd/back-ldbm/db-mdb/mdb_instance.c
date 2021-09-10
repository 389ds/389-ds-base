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
#include <search.h>

/* Info file used to check version and get parameters needed to open the db in dbscan case */

/*
 * Maximum db size by default (final size may be smaller if not enough disk is available
 *  final size may be greater is explicitly configured
 */
#define DEFAULT_DB_SIZE (2L*1024*1024*1024)

#define STOP_AVL_APPLY  (-8)

/* Flags allowed in mdb_dbi_open */
#define MDB_DBIOPEN_MASK (MDB_REVERSEKEY | MDB_DUPSORT | MDB_INTEGERKEY |  \
                MDB_DUPFIXED | MDB_INTEGERDUP | MDB_REVERSEDUP | MDB_CREATE)


#define TST(thecmd) do { rc = (thecmd); if (rc) { errinfo.file = __FILE__; errinfo.line = __LINE__; \
                         errinfo.cmd = #thecmd; goto error; } } while(0)


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

typedef struct {
    struct backend *be;
    dbmdb_ctx_t *ctx;
    dbmdb_dbi_t *dbi;        /* Result dbi (in add case) or selected dbi */
    MDB_txn *txn;
    int rc;                  /* used in avl_apply callbacks */
    const char *func;        /* Calling function name */
        /* For dbi_remove() */
    int deletion_flags;
        /* For dbi_list() */
    dbmdb_dbi_t **dbilist;
    int dbilistidx;
} dbi_open_ctx_t;

/* Error context */
typedef struct {
    char *cmd;
    char *file;
    int line;
} errinfo_t;

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
dbmdb_get_file_params(const char *dbname, int *flags, MDB_cmp_func **dupsort_fn)
{
    const char *fname = strrchr(dbname, '/');
    fname = fname ? fname+1 : dbname;

    if (is_dbfile(fname, LDBM_ENTRYRDN_STR)) {
        *flags |= MDB_DUPSORT;
        *dupsort_fn = dbmdb_entryrdn_compare_dups;
    } else if (is_dbfile(fname, ID2ENTRY)) {
        *flags |= MDB_INTEGERKEY;
        *dupsort_fn = NULL;
    } else if (strstr(fname,  DBNAMES)) {
        *flags |= 0;
        *dupsort_fn = NULL;
    } else if (strstr(fname,  CHANGELOG_PATTERN)) {
        *flags |= 0;
        *dupsort_fn = NULL;
    } else {
        *flags |= MDB_DUPSORT + MDB_INTEGERDUP + MDB_DUPFIXED;
        *dupsort_fn = NULL;
    }
}

char *dbmdb_build_dbname(backend *be, const char *filename)
{
    int len = strlen(filename) - strlen(LDBM_FILENAME_SUFFIX);
    int has_suffix = (len > 0 && strcmp(filename+len, LDBM_FILENAME_SUFFIX) == 0);
    const char *suffix = has_suffix ? "" : LDBM_FILENAME_SUFFIX;

    PR_ASSERT(filename[0] != '/');
    if (strchr(filename, '/')) {
        return slapi_ch_smprintf("%s%s", filename, suffix);
    }
    if (!be) {
        return slapi_ch_strdup(filename);
    } else {
        ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
        return slapi_ch_smprintf("%s/%s%s", inst->inst_name, filename, suffix);
    }
}

int cmp_dbi_names(const void *i1, const void *i2)
{
    const dbmdb_dbi_t *e1 = i1;
    const dbmdb_dbi_t *e2 = i2;
    return strcmp(e1->dbname, e2->dbname);
}

int add_dbi(dbi_open_ctx_t *octx, backend *be, const char *fname, int flags)
{
    MDB_cmp_func *dupsort_fn = NULL;
    dbmdb_ctx_t *ctx = octx->ctx;
    dbmdb_dbi_t treekey = {0};
    dbmdb_dbi_t **node = NULL;
    MDB_val data = {0};
    MDB_val key = {0};
    int flags2 = 0;
    int rc = 0;

    octx->dbi = NULL;
    treekey.dbname = dbmdb_build_dbname(be, fname);
    node = tfind(&treekey, &ctx->dbis_treeroot, cmp_dbi_names);

    if (node) {
        /* Already open - just returns the dbi */
        slapi_ch_free((void**)&treekey.dbname);
        octx->dbi = *node;
        return 0;
    }

    /* let create/open the dbi */
    dbmdb_get_file_params(treekey.dbname, &flags2, &dupsort_fn);
    treekey.env = ctx->env;
    treekey.state.flags = flags | flags2;
    treekey.state.state = DBIST_CLEAN;
    treekey.state.dataversion = DBMDB_CURRENT_DATAVERSION;
    octx->rc = MDB_DBI_OPEN(octx->txn, treekey.dbname, treekey.state.flags, &treekey.dbi);
    if (octx->rc) {
        slapi_log_err(SLAPI_LOG_ERR, "add_dbi", "Failed to open database instance %s. Error is %d: %s.\n",
                      treekey.dbname, rc, mdb_strerror(rc));
        slapi_ch_free((void**)&treekey.dbname);
        return octx->rc;
    }
    if (dupsort_fn) {
        mdb_set_dupsort(octx->txn, treekey.dbi, dupsort_fn);
    }
    /* And register it in DBNAMES */
    key.mv_data = (void*)(treekey.dbname);
    key.mv_size = strlen(treekey.dbname)+1;
    data.mv_data = &treekey.state;
    data.mv_size = sizeof treekey.state;
    if (strcmp(DBNAMES, treekey.dbname) == 0) {
        ctx->dbinames_dbi = treekey.dbi;
    }
    octx->rc = MDB_PUT(octx->txn, ctx->dbinames_dbi, &key, &data, 0);
    if (octx->rc) {
        slapi_log_err(SLAPI_LOG_ERR, "add_dbi", "Failed to insert database instance %s in DBNAMES. Error is %d: %s.\n",
                      treekey.dbname, rc, mdb_strerror(rc));
        slapi_ch_free((void**)&treekey.dbname);
        return octx->rc;
    }
    /* copy the dbi in the right slot */
    octx->ctx->dbi_slots[treekey.dbi] = treekey;
    /* And insert the slot in the name btree */
    node = tsearch(&octx->ctx->dbi_slots[treekey.dbi], &octx->ctx->dbis_treeroot, cmp_dbi_names);
    octx->dbi = &octx->ctx->dbi_slots[treekey.dbi];

    return 0;
}

static int
add_index_dbi(struct attrinfo *ai, dbi_open_ctx_t *octx)
{
    int flags = octx->ctx->readonly ? MDB_RDONLY: MDB_CREATE;
    char *rcdbname = NULL;

    slapi_log_error(SLAPI_LOG_DEBUG, "add_index_dbi", "ai_type = %s ai_indexmask=0x%x.\n", ai->ai_type, ai->ai_indexmask);

    if (ai->ai_indexmask & INDEX_VLV) {
        rcdbname = slapi_ch_smprintf("%s%s", RECNOCACHE_PREFIX, ai->ai_type);
        octx->rc = add_dbi(octx, octx->be, rcdbname, flags);
        slapi_ch_free_string(&rcdbname);
        if (octx->rc) {
            return STOP_AVL_APPLY;
        }
    }
    if (ai->ai_indexmask & INDEX_ANY) {
        octx->rc = add_dbi(octx, octx->be, ai->ai_type, flags);
        return octx->rc ? STOP_AVL_APPLY : 0;
    } else {
        return 0;
    }
}


/* Open/creat all the dbis to avoid opening the db in operation towards this backend
 *  There are nasty issues if file is created but its parent txn get aborted
 * So lets open all dbis right now (to insure dbi are open when starting
 *  an instance or when its configuration change) (should include the changelog too )
 */
int
dbmdb_open_all_files(dbmdb_ctx_t *ctx, backend *be)
{
    int flags = ctx->readonly ? MDB_RDONLY: MDB_CREATE;
    int mask = ctx->readonly ? MDB_RDONLY: 0;
    dbi_open_ctx_t octx = {0};
    MDB_cursor *cur = NULL;
    dbi_txn_t *txn = NULL;
    MDB_val data = {0};
    MDB_val key = {0};
    char *special_names[] = { ID2ENTRY, NULL };
    dbmdb_dbi_t *sn_dbis[(sizeof special_names) / sizeof special_names[0]] = {0};
    int *valid_slots = NULL;
    errinfo_t errinfo = {0};
    int rc = 0;
    int i;

    pthread_mutex_lock(&ctx->dbis_lock);
    TST(START_TXN(&txn, NULL, TXNFL_DBI));

    if (!ctx->dbi_slots) {
        ctx->dbi_slots = (dbmdb_dbi_t*)slapi_ch_calloc(ctx->startcfg.max_dbs, sizeof (dbmdb_dbi_t));
    }
    valid_slots = (int*) slapi_ch_calloc(ctx->startcfg.max_dbs, sizeof (int));
    for (i=0; i < ctx->startcfg.max_dbs; i++) {
        valid_slots[i] = (ctx->dbi_slots[i].dbname != NULL);
    }

    octx.ctx = ctx;
    octx.be = be;
    octx.txn = TXN(txn);

    TST(add_dbi(&octx, NULL, DBNAMES, flags));
    TST(MDB_CURSOR_OPEN(octx.txn, ctx->dbinames_dbi, &cur));
    TST(MDB_CURSOR_GET(cur, &key, &data, MDB_FIRST));
    while (rc == 0) {
        if (((char*)(key.mv_data))[key.mv_size-1]) {
            slapi_log_error(SLAPI_LOG_ERR, "dbmdb_open_all_files", "unexpected non nul terminated key in __DBNAMES database.\n");
        } else {
            dbistate_t *st = data.mv_data;
            TST(add_dbi(&octx, NULL, key.mv_data, ((st->flags|mask)&~flags) ));
        }
        rc = MDB_CURSOR_GET(cur, &key, &data, MDB_NEXT);
    }
    if (rc == MDB_NOTFOUND)
        rc =  0;

    if (be) {
        ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
        for (i=0; special_names[i]; i++) {
            TST(add_dbi(&octx, be, special_names[i], flags));
            sn_dbis[i] = octx.dbi;
        }
        inst->inst_id2entry = sn_dbis[0];
        if (avl_apply(inst->inst_attrs, add_index_dbi, &octx, STOP_AVL_APPLY, AVL_INORDER)) {
            TST(octx.rc);
        }
    }

error:
    if (cur) {
        MDB_CURSOR_CLOSE(cur);
    }

    rc = END_TXN(&txn, rc);
    if (rc && !errinfo.cmd) {
         slapi_log_error(SLAPI_LOG_ERR, "dbmdb_open_all_files", "Failed to commit txn while adding new db instance. Error %d :%s.\n", rc, mdb_strerror(rc));
    }
    if (rc && errinfo.cmd) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_open_all_files", "%s failed at %s[%d] with rc=%d: %s.\n", errinfo.cmd, errinfo.file, errinfo.line, rc, mdb_strerror(rc));
    }
    if (rc) {
        /* Roll back invalid slots and rebuild dbis table */
        for (i=0; i < ctx->startcfg.max_dbs; i++) {
            if (!valid_slots[i]) {
                tdelete(&ctx->dbi_slots[i], ctx->dbis_treeroot, cmp_dbi_names);
                slapi_ch_free((void**)&ctx->dbi_slots[i].dbname);
            }
        }
    }
    slapi_ch_free((void**)&valid_slots);

    pthread_mutex_unlock(&ctx->dbis_lock);
    return dbmdb_map_error(__FUNCTION__, rc);
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
        if (ctx->startcfg.max_size > DEFAULT_DB_SIZE) {
            ctx->startcfg.max_size = DEFAULT_DB_SIZE;
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
        rc =  dbmdb_open_all_files(ctx, NULL);
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

void free_dbi_node(void *node)
{
    dbmdb_dbi_t **dbi = node;
    slapi_ch_free((void**)&(*dbi)->dbname);
}

/* close the database env and release the context resource */
void dbmdb_ctx_close(dbmdb_ctx_t *ctx)
{
    if (ctx->env) {
        mdb_env_close(ctx->env);
        ctx->env = NULL;
    }
    if (ctx->dbi_slots) {
        tdestroy(ctx->dbis_treeroot, free_dbi_node);
        slapi_ch_free((void**)&ctx->dbi_slots);
        pthread_mutex_destroy(&ctx->dbis_lock);
        pthread_mutex_destroy(&ctx->rcmutex);
        pthread_rwlock_destroy(&ctx->dbmdb_env_lock);
    }
}

static void
dbi_list_insert(const void *nodep, VISIT which, void *closure)
{
    dbi_open_ctx_t *octx = closure;
    dbmdb_dbi_t *dbi = *(dbmdb_dbi_t**)nodep;
    struct backend *be = octx->be;

    switch (which) {
        case preorder:
        case endorder:
            break;
        case postorder:
        case leaf:
            if (octx->dbi && octx->dbi->dbi != dbi->dbi)
                return;    /* Not the wanted dbi */
            if (be) {
                int len = strlen(be->be_name);
                if (strncmp(dbi->dbname, be->be_name, len) || dbi->dbname[len] != '/')
                    return;  /* Not an instance of wanted backend */
            }

            octx->dbilist[octx->dbilistidx++] = *(dbmdb_dbi_t**)nodep;
            break;
    }
}

static dbmdb_dbi_t **
dbi_list(dbi_open_ctx_t *octx)
{
    octx->dbilist = (dbmdb_dbi_t **)slapi_ch_calloc(octx->ctx->startcfg.max_dbs+1, sizeof (dbmdb_dbi_t *));
    twalk_r(octx->ctx->dbis_treeroot, dbi_list_insert, octx);
    return octx->dbilist;
}

dbmdb_dbi_t **
dbmdb_list_dbis(dbmdb_ctx_t *ctx, backend *be, char *fname, int islocked, int *size)
{
    dbi_open_ctx_t octx = {0};
    dbmdb_dbi_t treekey = {0};
    dbmdb_dbi_t **node;

    octx.func = __FUNCTION__;
    octx.ctx = ctx;
    octx.be = be;

    if (!islocked)
        pthread_mutex_lock(&ctx->dbis_lock);
    if (fname) {
        treekey.dbname = dbmdb_build_dbname(be, fname);
        node = tfind(&treekey, &ctx->dbis_treeroot, cmp_dbi_names);
        slapi_ch_free((void**)&treekey.dbname);
        octx.dbilist = (dbmdb_dbi_t **)slapi_ch_calloc(2, sizeof (dbmdb_dbi_t *));
        if (node)
            octx.dbilist[octx.dbilistidx++] = *node;
    } else {
        dbi_list(&octx);
    }
    if (!islocked)
        pthread_mutex_unlock(&ctx->dbis_lock);
    *size = octx.dbilistidx;
    return octx.dbilist;
}



/* API used by dbscan to list the dbs */
dbi_dbslist_t *dbmdb_list_dbs(const char *dbhome)
{
    dbi_dbslist_t *dbs = NULL;
    dbmdb_dbi_t **dbilist;
    dbmdb_ctx_t ctx = {0};
    int size=0;
    int i;

    strncpy(ctx.home, dbhome, MAXPATHLEN);
    if (dbmdb_make_env(&ctx, 1, 0444)) {
        return NULL;
    }
    /* list all dbs */
    dbilist = dbmdb_list_dbis(&ctx, NULL, NULL, PR_FALSE, &size);
    dbs = (dbi_dbslist_t*)slapi_ch_calloc(size+1, sizeof (dbi_dbslist_t));
    for (i=0; i<size; i++) {
        PR_snprintf(dbs[i].filename, PATH_MAX, "%s/%s", dbhome, dbilist[i]->dbname);
        dbmdb_format_dbslist_info(dbs[i].info, dbilist[i]);
    }
    dbmdb_ctx_close(&ctx);
    slapi_ch_free((void**)&dbilist);
    return dbs;
}

static int
dbi_remove1(MDB_txn *txn, dbmdb_dbi_t *dbi, int deletion_flags)
{
    int rc = MDB_DROP(txn, dbi->dbi, deletion_flags);
    if (rc == 0 && deletion_flags) {
        MDB_val key = {0};
        key.mv_data = (char*)(dbi->dbname);
        key.mv_size = strlen(key.mv_data)+1;
        rc = MDB_DEL(txn, dbi->dbi, &key, NULL);
    }
    return rc;
}

/* Remove or Reset selected db instances */
static int
dbi_remove(dbi_open_ctx_t *octx)
{
    dbmdb_dbi_t **dbilist = NULL;
    dbmdb_ctx_t *ctx = octx->ctx;
    dbmdb_dbi_t treekey = {0};
    dbi_txn_t *txn = NULL;
    int rc = 0;
    int i;

    pthread_mutex_lock(&ctx->dbis_lock);
    rc = START_TXN(&txn, NULL, TXNFL_DBI);
    if (rc) {
        pthread_mutex_unlock(&ctx->dbis_lock);
        return rc;
    }
    octx->txn = TXN(txn);
    if (octx->dbi) {
        rc = dbi_remove1(octx->txn, octx->dbi, octx->deletion_flags);
    } else {
        dbilist = dbi_list(octx);
        for (i = 0; !rc && dbilist[i]; i++) {
            rc = dbi_remove1(octx->txn, dbilist[i], octx->deletion_flags);
        }
    }
    rc = END_TXN(&txn, rc);
    if (rc == 0) {
        if (octx->dbi) {
            /* Remove name(s) from tree and slot */
            if (octx->deletion_flags) {
                treekey.dbname = octx->dbi->dbname;
                tdelete(&treekey, &ctx->dbis_treeroot, cmp_dbi_names);
                slapi_ch_free((void**)&octx->dbi->dbname);
            } else {
                for (i = 0; dbilist[i]; i++) {
                    /* Remove name from tree and slot */
                    treekey.dbname = dbilist[i]->dbname;
                    tdelete(&treekey, &ctx->dbis_treeroot, cmp_dbi_names);
                    slapi_ch_free((void**)&dbilist[i]->dbname);
                }
            }
        }
    }
    if (rc) {
        if (octx->dbi) {
            slapi_log_err(SLAPI_LOG_ERR, "dbi_remove", "Failed to remove %s dbi. rc=%d: %s.\n",
                      octx->dbi->dbname, rc, mdb_strerror(rc));
        } else {
            slapi_log_err(SLAPI_LOG_ERR, "dbi_remove", "Failed to remove backend %s dbis. rc=%d: %s.\n",
                      octx->be->be_name, rc, mdb_strerror(rc));
        }
    }
    pthread_mutex_unlock(&ctx->dbis_lock);
    slapi_ch_free((void**)&dbilist);
    return rc;
}


/* Remove an open database */
int dbmdb_dbi_remove(dbmdb_ctx_t *ctx, dbi_db_t **dbi)
{
    dbi_open_ctx_t octx = {0};

    octx.func = __FUNCTION__;
    octx.ctx = ctx;
    octx.dbi = *dbi;
    octx.deletion_flags = 1;
    return dbmdb_map_error(__FUNCTION__, dbi_remove(&octx));
}

/* Remove all databases belonging to an instance directory */
int dbmdb_dbi_rmdir(backend *be)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    dbmdb_ctx_t *ctx = (dbmdb_ctx_t *)li->li_dblayer_config;
    dbi_open_ctx_t octx = {0};

    octx.func = __FUNCTION__;
    octx.ctx = ctx;
    octx.be = be;
    octx.deletion_flags = 1;
    return dbmdb_map_error(__FUNCTION__, dbi_remove(&octx));
}


int dbi_set_dirty(dbi_open_ctx_t *octx, int dirty_flags, int dirty_mask, int *old_flags)
{
    MDB_val data = {0};
    MDB_val key = {0};
    dbmdb_ctx_t *ctx = octx->ctx;
    dbmdb_dbi_t *dbi = octx->dbi;

    PR_ASSERT(&ctx->dbi_slots[dbi->dbi] == dbi);
    key.mv_data = (char*)(dbi->dbname);
    key.mv_size = strlen(dbi->dbname)+1;
    data.mv_data = &dbi->state;
    data.mv_size = sizeof dbi->state;
    *old_flags = dbi->state.state;
    dbi->state.state &= ~dirty_mask;
    dbi->state.state |= (dirty_flags & dirty_mask);
    dbi->state.state = dirty_flags & dirty_mask;
    if (*old_flags != dbi->state.state) {
        return MDB_PUT(octx->txn, ctx->dbinames_dbi, &key, &data, 0);
    }
    return 0;
}


/*
 * Set DIRTY condition to a dbi
 */
int dbmdb_dbi_set_dirty(dbmdb_ctx_t *ctx, dbmdb_dbi_t *dbi, int dirty_flags)
{
    dbi_open_ctx_t octx = {0};
    octx.func = __FUNCTION__;
    dbi_txn_t *txn = NULL;
    octx.ctx = ctx;
    int old_flags;
    int rc = 0;

    PR_ASSERT(ctx->dbi_slots[dbi->dbi].dbi == dbi->dbi);
    PR_ASSERT(strcmp(ctx->dbi_slots[dbi->dbi].dbname, dbi->dbname) == 0);
    octx.dbi = &ctx->dbi_slots[dbi->dbi];

    pthread_mutex_lock(&ctx->dbis_lock);
    rc = START_TXN(&txn, NULL, TXNFL_DBI);
    if (rc == 0) {
        old_flags = dbi->state.state;
        octx.txn = TXN(txn);
        rc = dbi_set_dirty(&octx, dirty_flags, -1, &old_flags);
        rc = END_TXN(&txn, rc);
        if (rc) {
            dbi->state.state = old_flags;
        }
    }
    return dbmdb_map_error(__FUNCTION__, rc);
}


/*
 * Remove DIRTY condition from a dbi
 */
int dbmdb_clear_dirty_flags(struct backend *be)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    dbmdb_ctx_t *ctx = (dbmdb_ctx_t *)li->li_dblayer_config;
    dbi_open_ctx_t octx = {0};
    dbmdb_dbi_t **dbilist = NULL;
    int *oldflaglist = NULL;
    dbi_txn_t *txn = NULL;
    int rc;
    int i;

    octx.func = __FUNCTION__;
    octx.ctx = ctx;
    octx.be = be;
    pthread_mutex_lock(&ctx->dbis_lock);
    rc = START_TXN(&txn, NULL, TXNFL_DBI);
    octx.txn = TXN(txn);
    if (rc) {
        return dbmdb_map_error(__FUNCTION__, rc);
    }
    oldflaglist = (int*)slapi_ch_calloc(ctx->startcfg.max_dbs+1, sizeof (int *));
    dbilist = dbi_list(&octx);

    for (i = 0; !rc && dbilist[i]; i++) {
        rc = dbi_set_dirty(&octx, 0, DBIST_DIRTY, &oldflaglist[i]);
    }
    rc = END_TXN(&txn, rc);
    if (rc) {
        while (--i >= 0) {
            dbilist[i]->state.state = oldflaglist[i];
        }
    }
    pthread_mutex_unlock(&ctx->dbis_lock);
    slapi_ch_free((void**)&oldflaglist);
    slapi_ch_free((void**)&dbilist);
    return dbmdb_map_error(__FUNCTION__, rc);
}

dbmdb_dbi_t *
dbi_get_by_name(dbmdb_ctx_t *ctx, struct backend *be, const char *fname)
{
    dbmdb_dbi_t treekey = {0};
    dbmdb_dbi_t **node = NULL;

    treekey.dbname = dbmdb_build_dbname(be, fname);
    pthread_mutex_lock(&ctx->dbis_lock);
    node = tfind(&treekey, &ctx->dbis_treeroot, cmp_dbi_names);
    pthread_mutex_unlock(&ctx->dbis_lock);
    slapi_ch_free((void**)&treekey.dbname);
    return node ? *node : NULL;
}

int dbmdb_update_dbi_state(dbmdb_ctx_t *ctx, dbmdb_dbi_t *dbi, dbistate_t *state, dbi_txn_t *txn, int is_locked)
{
    MDB_val data = {0};
    MDB_val key = {0};
    int rc = 0;
    int has_txn = (txn != NULL);

    if (!is_locked)
        pthread_mutex_lock(&ctx->dbis_lock);  /* Insure that dbi->dbname does not vanish */
    if (!has_txn)
        rc = START_TXN(&txn, NULL, 0);
    if (rc)
        goto error;
    if (!dbi->dbname)
        rc = MDB_NOTFOUND;
    if (rc)
        goto error;
    key.mv_data = (void*)(dbi->dbname);
    key.mv_size = strlen(key.mv_data)+1;
    data.mv_data = state;
    data.mv_size = sizeof *state;
    rc = MDB_PUT(TXN(txn), ctx->dbinames_dbi, &key, &data, 0);
error:
    if (!has_txn)
        rc = END_TXN(txn, rc);
    if (!is_locked)
        pthread_mutex_unlock(&ctx->dbis_lock);
    return rc;
}

int dbmdb_open_dbi_from_filename(dbmdb_dbi_t **dbi, backend *be, const char *filename, struct attrinfo *ai, int flags)
{
    const int custom_flags = MDB_OPEN_DIRTY_DBI | MDB_MARK_DIRTY_DBI | MDB_TRUNCATE_DBI;
    const int allowed_flags = custom_flags | MDB_RDONLY | MDB_CREATE;
    struct ldbminfo *li = (struct ldbminfo *)(be->be_database->plg_private);
    dbmdb_ctx_t *ctx = MDB_CONFIG(li);
    dbi_open_ctx_t octx = {0};
    dbi_txn_t *txn = NULL;
    int rc = 0;

    if (ctx->readonly || (flags&MDB_RDONLY)) {
        flags &= ~MDB_CREATE;
    }
    if (flags & MDB_MARK_DIRTY_DBI) {
        flags |= MDB_OPEN_DIRTY_DBI;
    }
    if (flags & ~allowed_flags) {
        char badflags[80];
        dbmdb_envflags2str(flags & ~allowed_flags, badflags, sizeof badflags);
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_open_dbname",
            "Unexpected flags %s when trying to open database %s (invalid flags) \n", badflags, filename);
        return MDB_INVALID;
    }

    /* Let see if the file is not already open */
    *dbi = dbi_get_by_name(ctx, be, filename);
    if (!*dbi && (flags & MDB_CREATE)) {
        pthread_mutex_lock(&ctx->dbis_lock);
        rc = START_TXN(&txn, NULL, TXNFL_DBI);
        if (!rc) {
            octx.ctx = ctx;
            octx.be = be;
            octx.txn = TXN(txn);
            rc = add_dbi(&octx, be, filename, flags & ~custom_flags);
            rc = END_TXN(txn, rc);
            *dbi = octx.dbi;
        }
        pthread_mutex_unlock(&ctx->dbis_lock);
    }
    if (rc) {
        return rc;
    }
    if (!*dbi) {
        return MDB_NOTFOUND;
    }
    if (((*dbi)->state.state & DBIST_DIRTY) && !(flags & MDB_OPEN_DIRTY_DBI)) {
        return MDB_NOTFOUND;
    }
    if (!((*dbi)->state.state & DBIST_DIRTY) && (flags & MDB_MARK_DIRTY_DBI)) {
           dbistate_t st = (*dbi)->state;
           st.state |= DBIST_DIRTY;
           rc = dbmdb_update_dbi_state(ctx, *dbi, &st, NULL, PR_FALSE);
    }
    if (!rc && (flags & MDB_TRUNCATE_DBI)) {
        octx.ctx = ctx;
        octx.dbi = *dbi;
        octx.deletion_flags = 0;
        rc = dbi_remove(&octx);
    }
    return rc;
}


int dbmdb_open_cursor(dbmdb_cursor_t *dbicur, dbmdb_ctx_t *ctx, dbmdb_dbi_t *dbi, int flags)
{
    int rc = 0;

    dbicur->dbi = dbi;
    if (ctx->readonly)
        flags |= MDB_RDONLY;
    rc = START_TXN(&dbicur->txn, NULL, 0);
    if (rc) {
        return rc;
    }
    rc = MDB_CURSOR_OPEN(TXN(dbicur->txn), dbicur->dbi->dbi, &dbicur->cur);
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


/*
 * Determine how to build the recno cache:
 *  RCMODE_USE_CURSOR_TXN - cache does need to be rebuild and
 *   current txn could be used to read from the cache
 *  RCMODE_USE_SUBTXN - cache must be rebuilt and we should open a sub txn
 *   from current r/w txn to rebuild the cache
 *  RCMODE_USE_NEW_THREAD - cache must be rebuilt - and a new thread
 *   must be spawned to rebuilt the cache
 */
int dbmdb_recno_cache_get_mode(dbmdb_recno_cache_ctx_t *rcctx)
{
    struct ldbminfo *li = (struct ldbminfo *)rcctx->cursor->be->be_database->plg_private;
    int curdbi = mdb_cursor_dbi(rcctx->cursor->cur);
    MDB_txn *txn = mdb_cursor_txn(rcctx->cursor->cur);
    dbmdb_ctx_t *ctx = MDB_CONFIG(li);
    char *rcdbname = NULL;
    int rc = 0;

    rcctx->mode = RCMODE_UNKNOWN;
    rcctx->cursortxn = txn;
    rcctx->dbi = &ctx->dbi_slots[curdbi];
    rcdbname = slapi_ch_smprintf("%s%s", RECNOCACHE_PREFIX, rcctx->dbi->dbname);

    /* Now lets search for the associated recno cache dbi */
    rcctx->rcdbi = dbi_get_by_name(ctx, NULL, rcdbname);
    if (rcctx->rcdbi)
        rcctx->mode = RCMODE_USE_CURSOR_TXN;

    if (rcctx->mode == RCMODE_USE_CURSOR_TXN) {
        /* DBI cache was found,  Let check that it is usuable */
        slapi_ch_free_string(&rcdbname);
        rcctx->key.mv_data = "OK";
        rcctx->key.mv_size = 2;
        rc = MDB_GET(txn, rcctx->rcdbi->dbi, &rcctx->key, &rcctx->data);
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
    } else if (rc == EINVAL) {
        rcctx->mode = RCMODE_USE_NEW_THREAD;
        rc = 0;
    }
    return rc;
}

/* Gather environment and dbi stats */
dbmdb_stats_t *
dbdmd_gather_stats(dbmdb_ctx_t *ctx, backend *be)
{
    int len, idx, rc;
    dbi_txn_t *txn = NULL;
    dbmdb_dbi_t *dbi = NULL;
    dbmdb_stats_t *stats = NULL;
    dbmdb_dbis_stat_t *dbistats = NULL;
    dbmdb_dbi_t **dbilist = NULL;
    dbi_open_ctx_t octx = {0};
    const char *pt;


    octx.func = __FUNCTION__;
    octx.ctx = ctx;
    octx.be = be;
    pthread_mutex_lock(&ctx->dbis_lock);
    rc = START_TXN(&txn, NULL, TXNFL_RDONLY);
    if (rc) {
        pthread_mutex_unlock(&ctx->dbis_lock);
        return NULL;
    }

    dbilist = dbi_list(&octx);
    len = sizeof (dbmdb_stats_t) + octx.dbilistidx * sizeof (dbmdb_dbis_stat_t);
    stats = (dbmdb_stats_t*)slapi_ch_calloc(1, len);
    stats->nbdbis = octx.dbilistidx;
    for (idx=0; rc==0 && idx < octx.dbilistidx; idx++) {
        dbi = dbilist[idx];
        dbistats = &stats->dbis[idx];
        /* Filter if instance_name is provided */
        pt = dbi->dbname;
        /* Skip ~recno-cache/ part */
        if (*pt == '~') {
            pt = strchr(pt, '/');
            pt = pt ? pt+1 : dbi->dbname;
        }
        /* dbname may vanish after freeing the dbis_lock ==> duplicate it */
        dbistats->dbname = slapi_ch_strdup(dbi->dbname);
        if (dbi->state.state & DBIST_DIRTY) {
            dbistats->flags |= DBI_STAT_FLAGS_DIRTY;
        }
        if (dbi->state.flags & MDB_DUPSORT) {
            dbistats->flags |= DBI_STAT_FLAGS_SUPPORTDUP;
        }
        if (dbi->dbi >0) {
            dbistats->flags |= DBI_STAT_FLAGS_OPEN;
            rc = mdb_stat(TXN(txn), dbi->dbi, &dbistats->stat);
        }
    }
    pthread_mutex_unlock(&ctx->dbis_lock);
    slapi_ch_free((void**)&dbilist);
    rc = END_TXN(&txn, rc);
    if (!be) {
        /* Gather env statistics */
        rc = mdb_env_stat(ctx->env, & stats->envstat);
        rc = mdb_env_info(ctx->env, & stats->envinfo);
    }
    return stats;
}

void
dbmdb_free_stats(dbmdb_stats_t **stats)
{
    dbmdb_stats_t *st = *stats;
    int idx;

    if (st) {
        for (idx=0; idx < st->nbdbis; idx++) {
            slapi_ch_free_string(&st->dbis[idx].dbname);
        }
    }
    slapi_ch_free((void**)stats);
}




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
#include <dlfcn.h>
#include "mdb_dbicmp.h"


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

#define RETRO_CHANGELOG_DB    "changelog/changenumber.db"


#define TST(thecmd) do { rc = (thecmd); if (rc) { errinfo.file = __FILE__; errinfo.line = __LINE__; \
                         errinfo.cmd = #thecmd; goto error; } } while(0)

#define PAGE_MASK        ((size_t)(ctx->info.pagesize)-1)
#define PAGE_ALIGN(size)   (((size)+PAGE_MASK)&~PAGE_MASK)


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
    struct attrinfo *ai;
} dbi_open_ctx_t;

/* Error context */
typedef struct {
    char *cmd;
    char *file;
    int line;
} errinfo_t;

/* Helper used to generate smaller key if privdn nrdn is too long. */
typedef struct {
    char header[sizeof (long)];
    unsigned long h;
    unsigned long id;
} privdb_small_key_t;

/*
 * Global reference to dbi slot table (used for debug purpose and for
 * dbmdb_dbicmp function. dbi are not closed (until either
 * backend/index get deleted) or if the whole db environment get closed.
 * In these cases there should not have any operations using the dbi
 * so we can access the slot table without using locks.
 */
static dbmdb_dbi_t *dbi_slots;    /* The alloced slots */
static int dbi_nbslots;           /* Number of available slots in dbi_slots */

/*
 * twalk_r is not available before glibc-2.30 so lets replace it by twalk
 * and a global variable (it is possible because there is a single call
 *   and it is protected by global mutex dbmdb_ctx->dbis_lock )
 */
#if __GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 30)

static struct {
    void *closure;
    void (*cb)(const void *nodep, VISIT which, void *closure);
} twalk_ctx;

static void twalk_cb(const void *nodep, VISIT which, int depth)
{
    twalk_ctx.cb(nodep, which, twalk_ctx.closure);
}

#define twalk_r(_tree, _cb, _closure) { twalk_ctx.cb = (_cb); twalk_ctx.closure = (_closure); twalk(_tree, twalk_cb); }

#endif


/*
 * Rules:
 * NULL comes before anything else.
 * Otherwise, strcasecmp(elem_a->rdn_elem_nrdn_rdn - elem_b->rdn_elem_nrdn_rdn) is
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

unsigned int
dbmdb_val2int(const MDB_val *v)
{
    unsigned int iv = 0;
    int len = v ? v->mv_size : 0;
    const char *str = v ? v->mv_data : NULL;
    while (len-- > 0) {
        iv = 10 * iv + *str++ - '0';
    }
    return iv;
}

static PRBool
is_dbfile(const char *dbname, const char *filename)
{
    int len = strlen(filename);
    if (strncasecmp(dbname, filename, len))
        return PR_FALSE;
    if (dbname[len] && strcasecmp(&dbname[len], LDBM_FILENAME_SUFFIX))
        return PR_FALSE;
    return PR_TRUE;
}

/* Determine mdb open flags according to dbi type */
static void
dbmdb_get_file_params(const char *dbname, int *flags, MDB_cmp_func **dupsort_fn)
{
    const char *fname = strrchr(dbname, '/');
    fname = fname ? fname+1 : dbname;

    *dupsort_fn = NULL;
    if (is_dbfile(fname, LDBM_ENTRYRDN_STR)) {
        *flags |= MDB_DUPSORT;
        *dupsort_fn = dbmdb_entryrdn_compare_dups;
    } else if (is_dbfile(fname, LDBM_LONG_ENTRYRDN_STR)) {
        *flags |= 0;
    } else if (is_dbfile(fname, ID2ENTRY)) {
        *flags |= 0;
    } else if (strstr(fname,  CHANGELOG_PATTERN)) {
        *flags |= 0;
    } else {
        *flags |= MDB_DUPSORT + MDB_INTEGERDUP + MDB_DUPFIXED;
    }
}

char *dbmdb_build_dbname(backend *be, const char *filename)
{
    int len = strlen(filename) - strlen(LDBM_FILENAME_SUFFIX);
    int has_suffix = (len > 0 && strcasecmp(filename+len, LDBM_FILENAME_SUFFIX) == 0);
    const char *suffix = has_suffix ? "" : LDBM_FILENAME_SUFFIX;
    char *res, *pt;

    PR_ASSERT(filename[0] != '/');
    /* Only some special private sub db like vlv cache starts with ~
     * None of the other sub db should contains a / unless
     * it is a fullname with the bename
     * So filename containing a / and nor starting by ~ are full names.
     */
    if (filename[0] != '~' && strchr(filename, '/')) {
        /* be name already in filename */
        res = slapi_ch_smprintf("%s%s", filename, suffix);
    } else if (!be) {
        /* no be name */
        return slapi_ch_strdup(filename);
    } else {
        /* add the be name */
        ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
        res = slapi_ch_smprintf("%s/%s%s", inst->inst_name, filename, suffix);
    }
    pt = (char*)slapi_utf8StrToLower((unsigned char*)res);
    slapi_ch_free_string(&res);
    return pt;
}

int cmp_dbi_names(const void *i1, const void *i2)
{
    const dbmdb_dbi_t *e1 = i1;
    const dbmdb_dbi_t *e2 = i2;
    return strcasecmp(e1->dbname, e2->dbname);
}

int dbmdb_update_dbi_cmp_fn(dbmdb_ctx_t *ctx, dbmdb_dbi_t *dbi, value_compare_fn_type cmp_fn, MDB_txn *txn)
{
    MDB_cmp_func *cmp_fn2 = dbmdb_get_dbicmp(dbi->dbi);
    int has_txn = (txn != NULL);
    dbi_txn_t *txn2 = NULL;
    int rc = 0;

    if (!cmp_fn2) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_update_dbi_cmp_fn", "Failed to get a compare function slot "
                      "while trying to open a database instance "
                      "(Hardcoded limit of %d open dbi has been reached)).\n", MAX_DBIS);
        return MDB_DBS_FULL;
    }

    if (!has_txn) {
        rc = START_TXN(&txn2, NULL, 0);
        if (rc) {
           return rc;
        }
        txn = TXN(txn2);
    }
    mdb_set_compare(txn, dbi->dbi, cmp_fn2);
    MDB_DBG_SET_FN("mdb_set_compare", dbi->dbname, txn, dbi->dbi, cmp_fn2);
    dbi->cmp_fn = cmp_fn;

    if (!has_txn)
        rc = END_TXN(&txn2, rc);
    return rc;
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

    octx->dbi = NULL;
    octx->rc = 0;
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
    treekey.state.flags &= ~MDB_RDONLY;
    treekey.state.state = DBIST_CLEAN;
    treekey.state.dataversion = DBMDB_CURRENT_DATAVERSION;
    octx->rc = MDB_DBI_OPEN(octx->txn, treekey.dbname, treekey.state.flags, &treekey.dbi);
    if (octx->rc) {
        slapi_log_err(SLAPI_LOG_ERR, "add_dbi", "Failed to open database instance %s. Error is %d: %s.\n",
                      treekey.dbname, octx->rc, mdb_strerror(octx->rc));
        slapi_ch_free((void**)&treekey.dbname);
        return octx->rc;
    }
    if (octx->ai && octx->ai->ai_key_cmp_fn) {
		octx->rc = dbmdb_update_dbi_cmp_fn(ctx, &treekey, octx->ai->ai_key_cmp_fn, octx->txn);
        if (octx->rc) {
            return octx->rc;
        }
    }

    if (dupsort_fn) {
        mdb_set_dupsort(octx->txn, treekey.dbi, dupsort_fn);
        MDB_DBG_SET_FN("mdb_set_dupsort", treekey.dbname, octx->txn, treekey.dbi, dupsort_fn);
    }
    /* And register it in DBNAMES */
    key.mv_data = (void*)(treekey.dbname);
    key.mv_size = strlen(treekey.dbname)+1;
    data.mv_data = &treekey.state;
    data.mv_size = sizeof treekey.state;
    if (strcasecmp(DBNAMES, treekey.dbname) == 0) {
        ctx->dbinames_dbi = treekey.dbi;
    }
    if (treekey.state.flags & MDB_CREATE) {
        octx->rc = MDB_PUT(octx->txn, ctx->dbinames_dbi, &key, &data, 0);
    }
    if (octx->rc) {
        slapi_log_err(SLAPI_LOG_ERR, "add_dbi", "Failed to insert database instance %s in DBNAMES. Error is %d: %s.\n",
                      treekey.dbname, octx->rc, mdb_strerror(octx->rc));
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

/* avlapply callback to open/create the dbi needed to handle an index */
static int
add_index_dbi(struct attrinfo *ai, dbi_open_ctx_t *octx)
{
    int flags = octx->ctx->readonly ? MDB_RDONLY: MDB_CREATE;
    char *rcdbname = NULL;

    dbg_log(__FILE__,__LINE__,__FUNCTION__, DBGMDB_LEVEL_OTHER, "ai_type = %s ai_indexmask=0x%x.\n", ai->ai_type, ai->ai_indexmask);
    octx->ai = ai;

    if (ai->ai_indexmask & INDEX_VLV) {
        rcdbname = dbmdb_recno_cache_get_dbname(ai->ai_type);
        octx->rc = add_dbi(octx, octx->be, rcdbname, flags);
        slapi_ch_free_string(&rcdbname);
        if (octx->rc) {
            octx->ai = NULL;
            return STOP_AVL_APPLY;
        }
    }
    if (ai->ai_indexmask & INDEX_ANY) {
        octx->rc = add_dbi(octx, octx->be, ai->ai_type, flags);
        octx->ai = NULL;
        return octx->rc ? STOP_AVL_APPLY : 0;
    } else {
        octx->ai = NULL;
        return 0;
    }
}

/* Destructor for dbi's tree */
void free_dbi_node(void *node)
{
    /* as the tree points on ctx->dbi_slots slots, there is nothing to do here */
}

/* Open/creat all the dbis to avoid opening the db in operation towards this backend
 *  There are nasty issues if file is created but its parent txn get aborted
 * So lets open all dbis right now (to insure dbi are open when starting
 *  an instance or when its configuration change) (should include the changelog too )
 */
int
dbmdb_open_all_files(dbmdb_ctx_t *ctx, backend *be)
{
    dbi_open_ctx_t octx = {0};
    MDB_cursor *cur = NULL;
    dbi_txn_t *txn = NULL;
    MDB_val data = {0};
    MDB_val key = {0};
    char *special_names[] = { ID2ENTRY, LDBM_PARENTID_STR, LDBM_ENTRYRDN_STR, LDBM_ANCESTORID_STR, BE_CHANGELOG_FILE, NULL };
    dbmdb_dbi_t *sn_dbis[(sizeof special_names) / sizeof special_names[0]] = {0};
    ldbm_instance *inst = be ? ((ldbm_instance *)be->be_instance_info) : NULL;
    int *valid_slots = NULL;
    errinfo_t errinfo = {0};
    char **vlv_list = NULL;
    int ctxflags;
    int rc = 0;
    int i;

    if (!ctx) {
        if (!be) {
            /* Testing for "be" to avoid a covscan warning although 
              * dbmdb_open_all_files is never called with both parameters NULL
              */
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_open_all_files",
                          "Unable to open the database environment witout either the database context or a backend.\n");
            return DBI_RC_INVALID;
        } else {
            struct ldbminfo *li = (struct ldbminfo *)(be->be_database->plg_private);
            ctx = MDB_CONFIG(li);
        }
    }
    ctxflags = ctx->readonly ? MDB_RDONLY: MDB_CREATE;
    if (does_vlv_need_init(inst)) {
        /* Vlv initialization is quite tricky as it require that
         *  [1] dbis_lock is not held
         *  [2] inst->inst_id2entry is set
         *  [3] vlv_getindices is not yet called
         *  So for newly discovered backend, it is better to explicitly
         *  build the vlv list before initializing it
         */
        vlv_list = vlv_list_filenames(be->be_instance_info);
    }

    /*
     * Lets start by opening all the existing db files (registered in __DBNAMES)
     */

    /* Note: ctx->dbis_lock mutex must always be hold after getting the txn
     *  because txn is held very early (within backend code) in add operation
     *  and inverting the order may lead to deadlock
     */
    TST(START_TXN(&txn, NULL, TXNFL_DBI));
    pthread_mutex_lock(&ctx->dbis_lock);

    if (!ctx->dbi_slots) {
        ctx->dbi_slots = (dbmdb_dbi_t*)slapi_ch_calloc(ctx->startcfg.max_dbs, sizeof (dbmdb_dbi_t));
        dbi_slots = ctx->dbi_slots;
        dbi_nbslots = ctx->startcfg.max_dbs;
    }
    valid_slots = (int*) slapi_ch_calloc(ctx->startcfg.max_dbs, sizeof (int));
    for (i=0; i < ctx->startcfg.max_dbs; i++) {
        valid_slots[i] = (ctx->dbi_slots[i].dbname != NULL);
    }

    octx.ctx = ctx;
    octx.be = be;
    octx.txn = TXN(txn);

    TST(add_dbi(&octx, NULL, DBNAMES, ctxflags));
    TST(MDB_CURSOR_OPEN(octx.txn, ctx->dbinames_dbi, &cur));
    TST(MDB_CURSOR_GET(cur, &key, &data, MDB_FIRST));
    while (rc == 0) {
        if (((char*)(key.mv_data))[key.mv_size-1]) {
            slapi_log_error(SLAPI_LOG_ERR, "dbmdb_open_all_files", "unexpected non NUL terminated key in __DBNAMES database.\n");
        } else {
            dbistate_t *st = data.mv_data;
            int flags = st->flags;  /* Copy the flags (we need to change them and st is read-only */
            /* Should ignore the context flags stored in the state flags but use the current ones */
            flags &= ~(MDB_RDONLY|MDB_CREATE);
            flags |= ctxflags;
            TST(add_dbi(&octx, NULL, key.mv_data, flags));
        }
        rc = MDB_CURSOR_GET(cur, &key, &data, MDB_NEXT);
    }
    if (rc == MDB_NOTFOUND)
        rc =  0;

    if (be) {
        for (i=0; special_names[i]; i++) {
            TST(add_dbi(&octx, be, special_names[i], ctxflags));
            sn_dbis[i] = octx.dbi;
        }
        inst->inst_id2entry = sn_dbis[0];
        if (avl_apply(inst->inst_attrs, add_index_dbi, &octx, STOP_AVL_APPLY, AVL_INORDER)) {
            TST(octx.rc);
        }
        if (be->vlvSearchList_lock) {
            /* vlv search list is initialized so we can use it */
            vlv_getindices((IFP)add_index_dbi, &octx, be);
        } else if (vlv_list) {
            char *rcdbname = NULL;
            for (size_t i=0; rc == 0 && vlv_list[i]; i++) {
                dbg_log(__FILE__,__LINE__,__FUNCTION__, DBGMDB_LEVEL_OTHER, "Opening vlv index %s.\n", vlv_list[i]);
                rcdbname = dbmdb_recno_cache_get_dbname(vlv_list[i]);
                rc = add_dbi(&octx, be, rcdbname, ctxflags);
                slapi_ch_free_string(&rcdbname);
                if (rc ==0) {
                    rc = add_dbi(&octx, be, vlv_list[i], ctxflags);
                }
            }
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
        /* Roll back invalid slots and rebuild dbis tree */
        tdestroy(ctx->dbis_treeroot, free_dbi_node);
        ctx->dbis_treeroot = NULL;
        for (i=0; i < ctx->startcfg.max_dbs; i++) {
            if (ctx->dbi_slots[i].dbname) {
                if (valid_slots[i]) {
                    /* Insert the dbi in tree */
                    tsearch(&ctx->dbi_slots[i], &ctx->dbis_treeroot, cmp_dbi_names);
                } else {
                    slapi_ch_free((void**)&ctx->dbi_slots[i].dbname);
                }
            }
        }
    }
    slapi_ch_free((void**)&valid_slots);

    pthread_mutex_unlock(&ctx->dbis_lock);
    if (vlv_list) {
        charray_free(vlv_list);
        vlv_list = NULL;
    }
    if (rc == 0 && does_vlv_need_init(inst)) {
        vlv_init(inst);
    }
    if (be) {
        vlv_rebuild_scope_filter(be);
    }
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
        if (line[pti->namelen] == '=' && strncasecmp(line, pti->name, pti->namelen) == 0) {
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

    slapi_log_err(SLAPI_LOG_INFO, "dbmdb_make_env", "MDB environment created with maxsize=%lu.\n", ctx->startcfg.max_size);
    slapi_log_err(SLAPI_LOG_INFO, "dbmdb_make_env", "MDB environment created with max readers=%d.\n", ctx->startcfg.max_readers);
    slapi_log_err(SLAPI_LOG_INFO, "dbmdb_make_env", "MDB environment created with max database instances=%d.\n", ctx->startcfg.max_dbs);

    /* If some upgrade is needed based on libmdb version, then another test must be done here.
     *  and the new test should be based on infofileinfo.libversion
     */
    if (rc == 0 && infofileinfo.dataversion && infofileinfo.dataversion != DBMDB_CURRENT_DATAVERSION) {
        rc = dbmdb_global_upgrade(ctx, infofileinfo.dataversion);
    }
    if (rc == 0) {
        /* coverity[tainted_data] */
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

/* close the database env and release the context resource */
void dbmdb_ctx_close(dbmdb_ctx_t *ctx)
{
    int i;
    if (ctx->dbi_slots) {
        /* ASAN report error if the tdestroy call is moved after mdb_env_close env */
        /* I do not really understand why (because the btree does not refer to mdb resources
         * except MDB_dbi (which is an integer)
         */
    }
    if (ctx->env) {
        mdb_env_close(ctx->env);
        ctx->env = NULL;
    }
    if (ctx->dbi_slots) {
        tdestroy(ctx->dbis_treeroot, free_dbi_node);
        ctx->dbis_treeroot = NULL;
        for (i=0; i<ctx->startcfg.max_dbs; i++)
            slapi_ch_free((void**)&ctx->dbi_slots[i].dbname);
        slapi_ch_free((void**)&ctx->dbi_slots);
        dbi_slots = NULL;
        dbi_nbslots = 0;
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
                if (strncasecmp(dbi->dbname, be->be_name, len) || dbi->dbname[len] != '/')
                    return;  /* Not an instance of wanted backend */
            }

            octx->dbilist[octx->dbilistidx++] = dbi;
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
        octx.dbilist = dbi_list(&octx);
    }
    if (!islocked)
        pthread_mutex_unlock(&ctx->dbis_lock);
    *size = octx.dbilistidx;
    return octx.dbilist;
}

int dbmdb_dump_reader(const char *msg, void *ctx)
{
    fprintf(ctx, "MDB READER: %s\n", msg);
    return 0;
}

static void dbmdb_show_st(FILE *fout, const char *prefix, MDB_stat *st)
{
    fprintf(fout, "%s page size: %u\n", prefix, st->ms_psize);
    fprintf(fout, "%s depth: %u\n", prefix, st->ms_depth);
    fprintf(fout, "%s branch pages: %ld\n", prefix, st->ms_branch_pages);
    fprintf(fout, "%s leaf pages: %ld\n", prefix, st->ms_leaf_pages);
    fprintf(fout, "%s overflow pages: %ld\n", prefix, st->ms_overflow_pages);
    fprintf(fout, "%s entries: %ld\n", prefix, st->ms_entries);
}


/* API used by dbscan to get db and dbi statistics */
int dbmdb_show_stat(const char *dbhome, FILE *fout, FILE *ferr)
{
    dbmdb_dbi_t **dbilist;
    dbmdb_ctx_t ctx = {0};
    int size=0;
    int i;
    MDB_stat st = {0};
    MDB_envinfo info = {0};
    long used_pages = 0;
    long alloced = 0;
    dbi_txn_t *txn = NULL;
    char pathmap[MAXPATHLEN];
    char dbiname[10];
    struct stat fst = {0};

    PR_snprintf(pathmap, MAXPATHLEN, "%s/%s", dbhome, DBMAPFILE);
    (void) stat(pathmap, &fst);

    PL_strncpyz(ctx.home, dbhome, MAXPATHLEN);
    if (dbmdb_make_env(&ctx, 1, 0644)) {
        fprintf(ferr, "ERROR: dbmdb_show_stat failed to open db environment %s\n", pathmap);
        return -1;
    }
    /* list all dbs */
    dbilist = dbmdb_list_dbis(&ctx, NULL, NULL, PR_FALSE, &size);
    START_TXN(&txn, NULL, TXNFL_RDONLY);

    /* Get global data */
    mdb_env_info(ctx.env, &info);
    mdb_env_stat(ctx.env, &st);

    fprintf(fout, "Database path: %s\n", pathmap);
    fprintf(fout, "Database file system size: %ld\n", fst.st_size);
    fprintf(fout, "Database map size: %ld\n", info.me_mapsize);
    fprintf(fout, "Database last page number: %ld\n", info.me_last_pgno);
    fprintf(fout, "Database last txnid: %ld\n", info.me_last_txnid);
    fprintf(fout, "Database max readers: %u\n", info.me_maxreaders);
    fprintf(fout, "Database num readers: %u\n", info.me_numreaders);
    dbmdb_show_st(fout, "Database", &st);
    mdb_reader_list(ctx.env, dbmdb_dump_reader, fout);

    for (i=0; i<size; i++) {
        fprintf(fout, "\ndbi: %d dbname: %s\n", dbilist[i]->dbi, dbilist[i]->dbname);
        bzero(&st, sizeof st);
        mdb_stat(TXN(txn), dbilist[i]->dbi, &st);
        used_pages += st.ms_branch_pages+st.ms_leaf_pages+st.ms_overflow_pages;
        sprintf(dbiname, "dbi: %d", dbilist[i]->dbi);
        dbmdb_show_st(fout, dbiname, &st);
    }
    mdb_stat(TXN(txn), 0, &st);        /* Free pages db */
    used_pages += st.ms_branch_pages+st.ms_leaf_pages+st.ms_overflow_pages;
    mdb_stat(TXN(txn), 1, &st);        /* Main db */
    used_pages += st.ms_branch_pages+st.ms_leaf_pages+st.ms_overflow_pages;
    END_TXN(&txn, 0);
    alloced = fst.st_size / st.ms_psize;
    fprintf(fout, "\nPAGES: max=%ld alloced=%ld used=%ld size=%d\n",
        info.me_mapsize / st.ms_psize, alloced, used_pages, st.ms_psize);
    dbmdb_ctx_close(&ctx);
    slapi_ch_free((void**)&dbilist);
    return 0;
}

/* API used by dbscan to list the dbs */
dbi_dbslist_t *dbmdb_list_dbs(const char *dbhome)
{
    dbi_dbslist_t *dbs = NULL;
    dbmdb_dbi_t **dbilist;
    dbmdb_ctx_t ctx = {0};
    int size=0;
    int i;
    MDB_stat st = {0};
    MDB_envinfo info = {0};
    long used_pages = 0;
    long alloced = 0;
    dbi_txn_t *txn = NULL;
    char pathmap[MAXPATHLEN];
    struct stat fst = {0};

    PR_snprintf(pathmap, MAXPATHLEN, "%s/%s", dbhome, DBMAPFILE);
    (void) stat(pathmap, &fst);

    PL_strncpyz(ctx.home, dbhome, MAXPATHLEN);
    if (dbmdb_make_env(&ctx, 1, 0644)) {
        return NULL;
    }
    /* list all dbs */
    dbilist = dbmdb_list_dbis(&ctx, NULL, NULL, PR_FALSE, &size);
    dbs = (dbi_dbslist_t*)slapi_ch_calloc(size+2, sizeof (dbi_dbslist_t));
    START_TXN(&txn, NULL, TXNFL_RDONLY);
    for (i=0; i<size; i++) {
        PR_snprintf(dbs[i].filename, PATH_MAX, "%s/%s", dbhome, dbilist[i]->dbname);
        dbmdb_format_dbslist_info(dbs[i].info, dbilist[i]);
        mdb_stat(TXN(txn), dbilist[i]->dbi, &st);
        used_pages += st.ms_branch_pages+st.ms_leaf_pages+st.ms_overflow_pages;
    }
    mdb_stat(TXN(txn), 0, &st);        /* Free pages db */
    used_pages += st.ms_branch_pages+st.ms_leaf_pages+st.ms_overflow_pages;
    mdb_stat(TXN(txn), 1, &st);        /* Main db */
    used_pages += st.ms_branch_pages+st.ms_leaf_pages+st.ms_overflow_pages;
    END_TXN(&txn, 0);
    mdb_env_info(ctx.env, &info);
    alloced = fst.st_size / st.ms_psize;
    PR_snprintf(dbs[i].filename, PATH_MAX, "GLOBAL STATS: pages max=%ld alloced=%ld used=%ld size=%d",
        info.me_mapsize / st.ms_psize, alloced, used_pages, st.ms_psize);
    dbmdb_ctx_close(&ctx);
    slapi_ch_free((void**)&dbilist);
    return dbs;
}

static int
dbi_remove1(dbmdb_ctx_t *ctx, MDB_txn *txn, dbmdb_dbi_t *dbi, int deletion_flags)
{
    int rc = MDB_DROP(txn, dbi->dbi, deletion_flags);
    if (rc == 0 && deletion_flags) {
        MDB_val key = {0};
        key.mv_data = (char*)(dbi->dbname);
        key.mv_size = strlen(key.mv_data)+1;
        rc = MDB_DEL(txn, ctx->dbinames_dbi, &key, NULL);
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

    /* reset operation can be done in englobing txn (so flag is 0)
     * but not remove operation (so flag is TXNFL_DBI)
     */
    rc = START_TXN(&txn, NULL, octx->deletion_flags ? TXNFL_DBI : 0);
    if (rc) {
        return rc;
    }
    pthread_mutex_lock(&ctx->dbis_lock);
    octx->txn = TXN(txn);
    if (octx->dbi) {
        rc = dbi_remove1(octx->ctx, octx->txn, octx->dbi, octx->deletion_flags);
    } else {
        dbilist = dbi_list(octx);
        for (i = 0; !rc && dbilist[i]; i++) {
            rc = dbi_remove1(octx->ctx, octx->txn, dbilist[i], octx->deletion_flags);
        }
    }
    rc = END_TXN(&txn, rc);
    if (rc == 0) {
        if (octx->deletion_flags) {
            if (octx->dbi) {
            /* Remove name(s) from tree and slot */
                treekey.dbname = octx->dbi->dbname;
                tdelete(&treekey, &ctx->dbis_treeroot, cmp_dbi_names);
                slapi_ch_free((void**)&octx->dbi->dbname);
            } else if (dbilist) { /* this test is always true but avoid a gcc warning */
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

/* Reset an open database */
int dbmdb_dbi_reset(dbmdb_ctx_t *ctx, dbi_db_t *dbi)
{
    dbi_open_ctx_t octx = {0};

    octx.func = __FUNCTION__;
    octx.ctx = ctx;
    octx.dbi = dbi;
    octx.deletion_flags = 0;
    return dbmdb_map_error(__FUNCTION__, dbi_remove(&octx));
}


/* Remove an open database */
int dbmdb_dbi_remove(dbmdb_ctx_t *ctx, dbi_db_t **dbi)
{
    dbi_open_ctx_t octx = {0};

    octx.func = __FUNCTION__;
    octx.ctx = ctx;
    octx.dbi = *dbi;
    octx.deletion_flags = 1;
    *dbi = NULL;
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
    PR_ASSERT(strcasecmp(ctx->dbi_slots[dbi->dbi].dbname, dbi->dbname) == 0);
    octx.dbi = &ctx->dbi_slots[dbi->dbi];

    rc = START_TXN(&txn, NULL, TXNFL_DBI);
    if (rc == 0) {
        pthread_mutex_lock(&ctx->dbis_lock);
        old_flags = dbi->state.state;
        octx.txn = TXN(txn);
        rc = dbi_set_dirty(&octx, dirty_flags, -1, &old_flags);
        pthread_mutex_unlock(&ctx->dbis_lock);
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
    rc = START_TXN(&txn, NULL, TXNFL_DBI);
    octx.txn = TXN(txn);
    if (rc) {
        return dbmdb_map_error(__FUNCTION__, rc);
    }
    pthread_mutex_lock(&ctx->dbis_lock);
    oldflaglist = (int*)slapi_ch_calloc(ctx->startcfg.max_dbs+1, sizeof (int));
    dbilist = dbi_list(&octx);

    for (i = 0; !rc && dbilist[i]; i++) {
        octx.dbi = dbilist[i];
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

    if (!has_txn)
        rc = START_TXN(&txn, NULL, 0);
    if (!is_locked)
        pthread_mutex_lock(&ctx->dbis_lock);  /* Insure that dbi->dbname does not vanish */
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
        rc = END_TXN(&txn, rc);
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
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_open_dbi_from_filename",
            "Unexpected flags %s when trying to open database %s (invalid flags) \n", badflags, filename);
        return MDB_INVALID;
    }

    /* Let see if the file is not already open */
    *dbi = dbi_get_by_name(ctx, be, filename);
    if (!*dbi && (flags & MDB_CREATE)) {
        if (dbmdb_has_a_txn()) {
            /* In theory all dbi associated with a backend are always kept open.
             * And in configuration change case current thread should not have a pending txn.
             * So far only known case of hitting this condition is that
             * online import/bulk import/reindex failed and suffix database was cleared)
             */
            slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_open_dbi_from_filename",
                          "Attempt to open to open dbi %s/%s while txn is already pending."
                          " Usually that means that the index must be reindex. Root cause is"
                          " likely that last import of reindex failed or that the index was"
                          " created but not yet reindexed).\n", be->be_name, filename);
            slapi_log_backtrace(SLAPI_LOG_WARNING);
            return MDB_NOTFOUND;
        }

        rc = START_TXN(&txn, NULL, TXNFL_DBI);
        if (!rc) {
            octx.ctx = ctx;
            octx.be = be;
            octx.txn = TXN(txn);
            octx.ai = ai;
            pthread_mutex_lock(&ctx->dbis_lock);
            rc = add_dbi(&octx, be, filename, flags & ~custom_flags);
            pthread_mutex_unlock(&ctx->dbis_lock);
            rc = END_TXN(&txn, rc);
            *dbi = octx.dbi;
        }
    }
    if (rc) {
        return rc;
    }
    if (!*dbi) {
        return MDB_NOTFOUND;
    }
    if (ai && ai->ai_key_cmp_fn != (*dbi)->cmp_fn) {
        if (! (*dbi)->cmp_fn) {
            rc = dbmdb_update_dbi_cmp_fn(ctx, *dbi, ai->ai_key_cmp_fn, NULL);
        }
        (*dbi)->cmp_fn = ai->ai_key_cmp_fn;
    }

    if (((*dbi)->state.state & DBIST_DIRTY) && !(flags & MDB_OPEN_DIRTY_DBI)) {
        return MDB_NOTFOUND;
    }
    if (!rc && !((*dbi)->state.state & DBIST_DIRTY) && (flags & MDB_MARK_DIRTY_DBI)) {
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
 * Returns the vlv cache db name (without the backend)
 * Return value must be freed by the caller.
 */
char *
dbmdb_recno_cache_get_dbname(const char *vlvdbiname)
{
    char *rcdbname = NULL;
    const char *vlvidxname = NULL;

    vlvidxname = strrchr(vlvdbiname, '/');
    vlvidxname = vlvidxname ? vlvidxname+1 : vlvdbiname;
    rcdbname = slapi_ch_smprintf("%s%s", RECNOCACHE_PREFIX, vlvidxname);
    return rcdbname;
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
    rcdbname = dbmdb_recno_cache_get_dbname(rcctx->dbi->dbname);
    rcctx->rcdbname = rcdbname;
    rcctx->env = ctx->env;

    /* Now lets search for the associated recno cache dbi */
    rcctx->rcdbi = dbi_get_by_name(ctx, rcctx->cursor->be, rcdbname);
    if (rcctx->rcdbi)
        rcctx->mode = RCMODE_USE_CURSOR_TXN;

    if (rcctx->mode == RCMODE_USE_CURSOR_TXN) {
        /* DBI cache was found,  Let check that it is usuable */
        rcctx->key.mv_data = "OK";
        rcctx->key.mv_size = 2;
        rc = MDB_GET(txn, rcctx->rcdbi->dbi, &rcctx->key, &rcctx->data);
        if (rc) {
            rcctx->mode = RCMODE_UNKNOWN;
        }
        if (rc != MDB_NOTFOUND) {
            /* There was an error or cache is valid.
             * Im both cases there is no need to rebuilt the cache.
             */
            return rc;
        }
    }
    /* Cache must be rebuilt so a write TXN is needed (either as subtxn or in new thread) */
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


    octx.func = __FUNCTION__;
    octx.ctx = ctx;
    octx.be = be;
    rc = START_TXN(&txn, NULL, TXNFL_RDONLY);
    if (rc) {
        return NULL;
    }
    pthread_mutex_lock(&ctx->dbis_lock);
    dbilist = dbi_list(&octx);
    len = sizeof (dbmdb_stats_t) + octx.dbilistidx * sizeof (dbmdb_dbis_stat_t);
    stats = (dbmdb_stats_t*)slapi_ch_calloc(1, len);
    stats->nbdbis = octx.dbilistidx;
    for (idx=0; rc==0 && idx < octx.dbilistidx; idx++) {
        dbi = dbilist[idx];
        dbistats = &stats->dbis[idx];
        /* Filter if instance_name is provided */
        /* Skip ~recno-cache/ part */
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

int
dbmdb_reset_vlv_file(backend *be, const char *filename)
{
    struct ldbminfo *li = (struct ldbminfo *)(be->be_database->plg_private);
    dbmdb_ctx_t *ctx = MDB_CONFIG(li);
    dbmdb_dbi_t *dbi = NULL;
    char *rcdbname = dbmdb_recno_cache_get_dbname(filename);
    int rc = 0;
    dbi = dbi_get_by_name(ctx, be, filename);
    if (dbi)
        rc = dbmdb_dbi_reset(ctx, dbi);
    dbi = dbi_get_by_name(ctx, be, rcdbname);
    if (dbi && !rc)
        rc = dbmdb_dbi_reset(ctx, dbi);
    slapi_ch_free_string(&rcdbname);
    return rc;
}

dbmdb_dbi_t *
dbmdb_get_dbi_from_slot(int dbi)
{
    return (dbi_slots && dbi>=0 && dbi<dbi_nbslots) ? &dbi_slots[dbi] : NULL;
}

/* Compare two index values (similar to bdb_bt_compare) */
int
dbmdb_dbicmp(int dbi, const MDB_val *v1, const MDB_val *v2)
{
    dbmdb_dbi_t *dbi1 = dbmdb_get_dbi_from_slot(dbi);
    value_compare_fn_type cmp_fn = dbi1 ? dbi1->cmp_fn : NULL;
    struct berval bv1, bv2;
    int rc;

    bv1.bv_val = (char *)v1->mv_data;
    bv1.bv_len = (ber_len_t)v1->mv_size;
    bv2.bv_val = (char *)v2->mv_data;
    bv2.bv_len = (ber_len_t)v2->mv_size;

    if (cmp_fn && bv1.bv_len>0 && bv2.bv_len>0 &&
        bv1.bv_val[0] == EQ_PREFIX &&
        bv2.bv_val[0] == EQ_PREFIX) {
        bv1.bv_val++;
        bv1.bv_len--;
        bv2.bv_val++;
        bv2.bv_len--;
        rc = cmp_fn(&bv1, &bv2);
        return rc;
    } else {
        return slapi_berval_cmp(&bv1, &bv2);
    }
}

static void
dbmdb_privdb_discard_cursor(mdb_privdb_t *db)
{
    if (db->cursor) {
        MDB_CURSOR_CLOSE(db->cursor);
    }
    if (db->txn) {
        TXN_ABORT(db->txn);
    }
    db->cursor = NULL;
    db->txn = NULL;
    db->wcount = 0;
}

/* Insure we have a proper write txn and an open cursor */
static int
dbmdb_privdb_handle_cursor(mdb_privdb_t *db, int dbi_index)
{
    int rc;
    if (db->wcount >= 1000) {
        MDB_CURSOR_CLOSE(db->cursor);
        rc = TXN_COMMIT(db->txn);
        db->cursor = NULL;
        db->txn = NULL;
        db->wcount = 0;
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_privdb_handle_cursor",
                          "Failed to commit dndb transaction. Error is %d: %s.", rc, mdb_strerror(rc));
            TXN_ABORT(db->txn);
            return -1;
        }
    }
    if (!db->txn) {
        rc = TXN_BEGIN(db->env, NULL, 0, &db->txn);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_privdb_handle_cursor",
                          "Failed to begin dndb transaction. Error is %d: %s.", rc, mdb_strerror(rc));
            return -1;
        }
        rc = MDB_CURSOR_OPEN(db->txn, db->dbis[dbi_index].dbi, &db->cursor);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_privdb_handle_cursor",
                          "Failed to open dndb cursor. Error is %d: %s.", rc, mdb_strerror(rc));
            dbmdb_privdb_discard_cursor(db);
            return -1;
        }
    }
    return 0;
}


/* Cleanup a private database */
void
dbmdb_privdb_destroy(mdb_privdb_t **db)
{
    char path[MAXPATHLEN];
    if (*db) {
        dbmdb_privdb_discard_cursor(*db);
        if ((*db)->env) {
            mdb_env_close((*db)->env);
        }
        if ((*db)->path[0]) {
            PR_snprintf(path, MAXPATHLEN, "%s/%s", (*db)->path, DBMAPFILE);
            unlink(path);
            PR_snprintf(path, MAXPATHLEN, "%s/lock.mdb", (*db)->path);
            unlink(path);
            rmdir((*db)->path);
        }
        slapi_ch_free((void**)db);
    }
}


/* Compute a smaller key for long rdn */
int
dbmdb_privdb_init_small_key(mdb_privdb_t *db, const MDB_val *longkey, int addnewkey, privdb_small_key_t *smallkey)
{
    MDB_val key = {0};
    MDB_val data = {0};
    unsigned long nextid = 0;
    char *pt = longkey->mv_data;
    int rc = 0;

    memset(smallkey, 0, sizeof *smallkey);
    /* Prepare the samllkey used to store the long key with id=0 */
    strcpy(smallkey->header, "==>");
    for (pt += longkey->mv_size - 1; pt >= (char*)longkey->mv_data; pt--) {
        smallkey->h += ((smallkey->h << 3) | (smallkey->h >> ((8 * sizeof(long)) - 3))) ^ (*pt & 0x1f);
    }
    key.mv_data = smallkey;
    key.mv_size = sizeof (*smallkey);
    rc = MDB_CURSOR_GET(db->cursor, &key, &data, MDB_SET_RANGE);
    /* Let walk all the keys that have the right hash */
    while (rc == 0 && key.mv_size == sizeof(privdb_small_key_t) &&
           memcmp(key.mv_data, smallkey, offsetof(privdb_small_key_t, id)) == 0) {
        if (longkey->mv_size == data.mv_size &&
            memcmp(longkey->mv_data, data.mv_data, data.mv_size) == 0) {
            /* Key to the wanted data exists, lets return the small key used for getting/storing data ! */
            memcpy(smallkey, key.mv_data, key.mv_size);
            smallkey->header[0] = '@';
            return 0;
        }
        /* Wrong key, lets get next one */
        memcpy(&nextid, &((privdb_small_key_t*)data.mv_data)->id, sizeof nextid);
        nextid++;
        rc = MDB_CURSOR_GET(db->cursor, &key, &data, MDB_NEXT);
    }
    if (rc ==0 || rc == MDB_NOTFOUND) {
        /* Key does not exists */
        if (addnewkey) {
            /* Lets add it! */
            smallkey->id = nextid;
            key.mv_data = smallkey;
            key.mv_size = sizeof (*smallkey);
            rc = MDB_CURSOR_PUT(db->cursor, &key, (MDB_val*)longkey, 0);
            smallkey->header[0] = '@';
        } else {
            rc = MDB_NOTFOUND;
        }
    }
    return rc;
}


int
dbmdb_privdb_get(mdb_privdb_t *db, int dbi_idx, MDB_val *key, MDB_val *data)
{
    int rc = dbmdb_privdb_handle_cursor(db, 0);
    data->mv_data = NULL;
    data->mv_size = 0;
    if (!rc) {
        if (key->mv_size > db->maxkeysize) {
            privdb_small_key_t small_key = {0};
            MDB_val key2 = {0};
            key2.mv_data = &small_key;
            key2.mv_size = sizeof small_key;
            rc = dbmdb_privdb_init_small_key(db, key, 0, &small_key);
            if (!rc) {
                rc = MDB_CURSOR_GET(db->cursor, &key2, data, MDB_SET_KEY);
            }
        } else {
            rc = MDB_CURSOR_GET(db->cursor, key, data, MDB_SET_KEY);
        }
        if (rc && rc != MDB_NOTFOUND) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_privdb_handle_cursor",
                          "Failed to get key from dndb cursor Error is %d: %s.", rc, mdb_strerror(rc));
        }
    }
    return rc;
}


int
dbmdb_privdb_put(mdb_privdb_t *db, int dbi_idx, MDB_val *key, MDB_val *data)
{
    int rc = dbmdb_privdb_handle_cursor(db, 0);
    if (!rc) {
        if (key->mv_size > db->maxkeysize) {
            privdb_small_key_t small_key = {0};
            MDB_val key2 = {0};
            key2.mv_data = &small_key;
            key2.mv_size = sizeof small_key;
            rc = dbmdb_privdb_init_small_key(db, key, 1, &small_key);
            if (!rc) {
                rc = MDB_CURSOR_PUT(db->cursor, &key2, data, MDB_NOOVERWRITE);
            }
        } else {
            rc = MDB_CURSOR_PUT(db->cursor, key, data, MDB_NOOVERWRITE);
        }
        if (rc && rc != MDB_KEYEXIST) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_privdb_handle_cursor",
                          "Failed to put data into dndb cursor Error is %d: %s.", rc, mdb_strerror(rc));
        }
    }
    if (!rc) {
        db->wcount++;
    }
    return rc;
}


/* Create a private database environment */
mdb_privdb_t *
dbmdb_privdb_create(dbmdb_ctx_t *ctx, size_t dbsize, ...)
{
    va_list va;
    int nbdbis = 0;
    mdb_privdb_t *db;
    int rc = 0;
    int i;
    MDB_txn *txn = NULL;

    va_start(va, dbsize);
    while (va_arg(va, char*)) {
        nbdbis++;
    }
    va_end(va);

    db = (mdb_privdb_t*)slapi_ch_calloc(1, (sizeof(mdb_privdb_t))+(nbdbis+1)*(sizeof(dbmdb_dbi_t)));
    db->dbis = (dbmdb_dbi_t*) &db[1];
    db->env_flags = MDB_NOMETASYNC | MDB_NOSYNC | MDB_NOTLS | MDB_NOLOCK | MDB_NORDAHEAD | MDB_NOMEMINIT ;
    db->db_size = PAGE_ALIGN(dbsize);

    rc = mdb_env_create(&db->env);
    if (rc) {
        slapi_log_error(SLAPI_LOG_ERR, "dbmdb_privdb_create", "Failed to create lmdb environment. Error %d :%s.\n", rc, mdb_strerror(rc));
        goto bail;
    }

    db->maxkeysize = mdb_env_get_maxkeysize(db->env);
    mdb_env_set_maxdbs(db->env, nbdbis);
    mdb_env_set_mapsize(db->env, db->db_size);

    i=0;
    do {
        PR_snprintf(db->path, (sizeof db->path), "%s/priv@%d", ctx->home, i++);
        errno = 0;
    } while (i>0 && mkdir(db->path, 0700) && errno == EEXIST);

    if (i<0 || errno) {
        slapi_log_error(SLAPI_LOG_ERR, "dbmdb_privdb_create", "Failed to create lmdb environment directory %s. Error %d :%s.\n", db->path, errno, strerror(errno));
        db->path[0] = 0;
    }

    rc = mdb_env_open(db->env, db->path, db->env_flags, 0600);
    if (rc) {
        slapi_log_error(SLAPI_LOG_ERR, "dbmdb_privdb_create", "Failed to open lmdb environment with path %s. Error %d :%s.\n", db->path, rc, mdb_strerror(rc));
        goto bail;
    }

    rc = TXN_BEGIN(db->env, NULL, 0, &txn);
    if (rc) {
        slapi_log_error(SLAPI_LOG_ERR, "dbmdb_privdb_create", "Failed to begin a txn for lmdb environment with path %s. Error %d :%s.\n", db->path, rc, mdb_strerror(rc));
        goto bail;
    }

    va_start(va, dbsize);
    for (i=0; i<nbdbis; i++) {
        db->dbis[i].env = db->env;
        db->dbis[i].state.flags = MDB_CREATE;
        db->dbis[i].dbname = va_arg(va, char*);
        if (rc == 0) {
            rc = MDB_DBI_OPEN(txn, db->dbis[i].dbname, db->dbis[i].state.flags, &db->dbis[i].dbi);
        }
    }
    va_end(va);
    if (rc) {
        TXN_ABORT(txn);
        slapi_log_error(SLAPI_LOG_ERR, "dbmdb_privdb_create", "Failed to open a database instance for lmdb environment with path %s. Error %d :%s.\n", db->path, rc, mdb_strerror(rc));
        goto bail;
    }
    rc = TXN_COMMIT(txn);
    if (rc) {
        TXN_ABORT(txn);
        slapi_log_error(SLAPI_LOG_ERR, "dbmdb_privdb_create", "Failed to commit database instance creation transaction for lmdb environment with path %s. Error %d :%s.\n", db->path, rc, mdb_strerror(rc));
        goto bail;
    }

bail:
    if (rc) {
        dbmdb_privdb_destroy(&db);
    }
    return db;
}

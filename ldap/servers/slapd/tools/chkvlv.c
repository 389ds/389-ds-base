/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2024 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/


/* Build with: gcc -o chkvlv chkvlv.c -llmdb */
/* Usage: chkvlv dbdir */

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <lmdb.h>
#include <stdint.h>


#define T(expr)		rc = expr; if (rc) { printf("%s[%d]: %s returned %s (%d)\n", __FILE__, __LINE__, #expr, mdb_strerror(rc), rc); exit(1); }

#define RECNO_CACHE_PREFIX   "/~recno-cache"
#define VLV_PREFIX   "/vlv#"


typedef struct {
    char *name;
    MDB_dbi dbi;
    int is_vlv;
    int recno_idx;
} dbi_t;

typedef struct {
    MDB_val data;
    MDB_val key;
    int len;
    uint32_t recno;
    /* followed by key value then data value */
} recno_elmt_t;

typedef struct {
    MDB_cursor *cur;
    MDB_txn *txn;
    MDB_val key;
    MDB_val data;
    int count;
} iterator_t;


int nbdbis;
dbi_t *dbis;
MDB_env *env = NULL;
MDB_dbi dbi = 0;


/* Iterate calling 'cb' callback on each database instance records */
int
iterate(MDB_txn *txn, MDB_dbi dbi, int (*cb)(iterator_t *, void*), void *ctx)
{
    int rc = 0;
    iterator_t it = {0};
    it.txn = txn;
    T(mdb_cursor_open(txn, dbi, &it.cur));
    rc = mdb_cursor_get(it.cur, &it.key, &it.data, MDB_FIRST);
    while (rc==0) {
        rc = cb(&it, ctx);
        if (rc == 0) {
            it.count++;
            rc = mdb_cursor_get(it.cur, &it.key, &it.data, MDB_NEXT);
        }
    }
    mdb_cursor_close(it.cur);
    if (rc == MDB_NOTFOUND) {
        rc = 0;
    }
    return rc;
}

void
open_db(const char *dbdir)
{
    int rc = 0;
    char buf[200];
    char buf2[200];

    FILE *fd = NULL;
    size_t maxsize = 0;
    MDB_dbi  maxdbs = 0;
    unsigned int maxreaders = 0;
    char *pt = NULL;

	T(mdb_env_create(&env));
    sprintf(buf,"%s/INFO.mdb",dbdir);
    fd = fopen(buf, "r");
    if (fd==NULL) {
        perror(buf);
        printf("The <dbdir>' parameter is probably invalid.\n");
        exit(1);
    }
    while (pt=fgets(buf2, (sizeof buf2), fd)) {
        sscanf(buf2, "MAXSIZE=%ld", &maxsize);
        sscanf(buf2, "MAXREADERS=%ud", &maxreaders);
        sscanf(buf2, "MAXDBS=%ud", &maxdbs);
    }
    fclose(fd);

    sprintf(buf,"%s/data.mdb",dbdir);
    T(mdb_env_set_maxdbs(env, maxdbs));
    T(mdb_env_set_mapsize(env, maxsize));
    T(mdb_env_set_maxreaders(env, maxreaders));
	T(mdb_env_open(env, dbdir, MDB_RDONLY , 0700));
}

char *
dup_val(const MDB_val *val)
{
    char *str = malloc(val->mv_size+1);
    if (str==NULL) {
        fprintf(stderr, "Cannot alloc %ld bytes.\n", val->mv_size+1);
        exit(1);
    }
    memcpy(str, val->mv_data, val->mv_size);
    str[val->mv_size] = 0;
    return str;
}


int
dup_recno_elmt(const MDB_val *val, recno_elmt_t *elmt)
{
    if (val->mv_size < sizeof *elmt) {
        printf("Unexpected record size %ld (Should be >= %ld)\n",
               val->mv_size, sizeof *elmt);
        return -1;
    }
    memcpy(elmt, val->mv_data, sizeof *elmt);
    size_t expected_size = (sizeof *elmt) + elmt->key.mv_size + elmt->data.mv_size;
    if (val->mv_size != expected_size) {
        printf("Unexpected record size %ld (Should be %ld)\n",
               val->mv_size, expected_size);
        elmt->key.mv_data = elmt->data.mv_data = NULL;
        return -1;
    }
    char *pt = val->mv_data;
    elmt->key.mv_data = pt+sizeof *elmt;
    elmt->data.mv_data = pt+(sizeof *elmt)+elmt->key.mv_size;
    elmt->key.mv_data = dup_val(&elmt->key);
    elmt->data.mv_data = dup_val(&elmt->data);
    return 0;
}

void
free_recno_elmt(recno_elmt_t *elmt)
{
    if (elmt->key.mv_data) {
        free(elmt->key.mv_data);
        elmt->key.mv_data = NULL;
    }
    if (elmt->data.mv_data) {
        free(elmt->data.mv_data);
        elmt->data.mv_data = NULL;
    }
}

int
count_cb(iterator_t *it, void *ctx)
{
    *(int*)ctx = it->count;
    return 0;
}

int
store_dbi(iterator_t *it, void *ctx)
{
    int rc = 0;
    if (it->count > nbdbis) {
        return MDB_NOTFOUND;
    }
    char *name = dup_val(&it->key);
    T(mdb_dbi_open(it->txn, name , 0, &dbis[it->count].dbi));
    dbis[it->count].name = name;
    dbis[it->count].is_vlv = strstr(name, VLV_PREFIX) && !strstr(name, RECNO_CACHE_PREFIX);
    return 0;
}

void open_dbis()
{
    int rc = 0;
    MDB_dbi dbi = 0;
	MDB_txn *txn = 0;

    T(mdb_txn_begin(env, NULL, MDB_RDONLY, &txn));
    T(mdb_dbi_open(txn, "__DBNAMES", 0, &dbi));
    T(mdb_txn_commit(txn));
    T(mdb_txn_begin(env, NULL, MDB_RDONLY, &txn));
    T(iterate(txn, dbi, count_cb, &nbdbis));
    dbis = calloc(nbdbis, sizeof (dbi_t));
    if (!dbis) {
        fprintf(stderr, "Cannot alloc %ld bytes.\n", nbdbis*sizeof (dbi_t));
        exit(1);
    }
    T(iterate(txn, dbi, store_dbi, NULL));
    T(mdb_txn_commit(txn));

    for (size_t count = 0; count < nbdbis; count++) {
        if (dbis[count].is_vlv) {
            char buf2[200];
            char *pt = dbis[count].name;
            char *pt2 = buf2;
            while (*pt!='/') {
                *pt2++ = *pt++;
            }
            strcpy(pt2,RECNO_CACHE_PREFIX);
            pt2 += strlen(pt2);
            strcpy(pt2,pt);

            for (size_t i = 0; i < nbdbis; i++) {
                if (strcmp(dbis[i].name, buf2)==0) {
                    dbis[count].recno_idx = i;
                }
            }
        }
    }
}

void
dump_val(const MDB_val *val)
{
    unsigned char *pt = val->mv_data;
    for (size_t i = val->mv_size; i >0; i--) {
        if ( *pt >= 0x32 && *pt < 0x7f && *pt != '\\') {
            putchar(*pt);
        } else {
            printf("\\%02x", *pt);
        }
        pt++;
    }
}

int
cmp_val(const MDB_val *val1, const MDB_val *val2)
{
    size_t len = val1->mv_size > val2->mv_size ? val2->mv_size : val1->mv_size;
    int rc = memcmp(val1->mv_data, val2->mv_data, len);
    if (rc!=0) return rc;
    return val1->mv_size - val2->mv_size;
}

typedef struct {
    dbi_t *vlvdbi;
    recno_elmt_t *elmt;
    iterator_t *it;
    int found;
} check_recno_ctx_t;

int
check_recno_ctx(iterator_t *it, void *ctx)
{
    check_recno_ctx_t *rctx = ctx;
    if (cmp_val(&it->key, &rctx->elmt->key) == 0) {
        rctx->found = 1;
        if (it->count+1 != rctx->elmt->recno) {
            printf("Problem (invalid recno value) detected in vlv cache record #%d\n", it->count);
            printf("Found %d instead of %d\n", rctx->elmt->recno, it->count+1);
        }
    }
    return 0;
}

void
check_recno_record(iterator_t *it, dbi_t *vlvdbi, recno_elmt_t *elmt)
{
    int rc = 0;
    check_recno_ctx_t ctx = {0};
    ctx.vlvdbi = vlvdbi;
    ctx.elmt = elmt;
    ctx.it = it;
    if (dup_recno_elmt(&it->data, elmt)) {
        printf("Problem (invalid data size) detected in vlv cache record #%d\n", it->count);
        return;
    }
    T(iterate(it->txn, vlvdbi->dbi, check_recno_ctx, &ctx));
}

int
walk_cache(iterator_t *it, void *ctx)
{
    char *pt = it->key.mv_data;
    recno_elmt_t elmt = {0};
    MDB_val vkey;
    MDB_val vdata;
    switch (*pt) {
        case 'O':
            printf("vlv cache is in sync.\n");
            return 0;
        case 'D':
            if (it->key.mv_size < sizeof vkey.mv_size) {
                printf("Problem (invalid key size) detected in vlv cache record #%d\n", it->count);
                return 0;
            }
            memcpy(&vkey.mv_size, pt + it->key.mv_size - sizeof vkey.mv_size, sizeof vkey.mv_size);
            vkey.mv_data = pt+1;
            vdata.mv_data = pt+1+vkey.mv_size;
            vdata.mv_size = it->key.mv_size - 1-vkey.mv_size - sizeof vkey.mv_size;
            printf("vkey: "); dump_val(&vkey); putchar('\n');
            printf("vdata: "); dump_val(&vdata); putchar('\n');
            check_recno_record(it, ctx, &elmt);
            if (cmp_val(&vkey, &elmt.key) != 0) {
                printf("Problem (missmatching key value) detected in vlv cache record #%d\n", it->count);
                return 0;
            }
            if (cmp_val(&vdata, &elmt.data) != 0) {
                printf("Problem (missmatching data value) detected in vlv cache record #%d\n", it->count);
                return 0;
            }
            return 0;
        case 'R':
            if (it->key.mv_size !=11) {
                printf("Problem (invalid key size) detected in vlv cache record #%d\n", it->count);
                return 0;
            }
            check_recno_record(it, ctx, &elmt);
            return 0;
    }

    return 0;
}

void
process_vlv(int idx)
{
    int rc = 0;
	MDB_txn *txn = 0;
    printf("Processing: %s\n", dbis[idx].name);
    T(mdb_txn_begin(env, NULL, MDB_RDONLY, &txn));
    T(iterate(txn, dbis[dbis[idx].recno_idx].dbi, walk_cache, &dbis[idx]))
    T(mdb_txn_commit(txn));
}


int main(int argc, char **argv)
{
    int rc = 0;
    if (argc != 2) {
        printf("Usage: %s <dbdir>\n", argv[1]);
        printf("\tThis tools check the lmdb vlv caches consistency\n")
        exit(1);
    }
    char *dbdir = argv[1];

    open_db(argv[1]);
    open_dbis();
    for (size_t i = 0; i < nbdbis; i++) {
        if (dbis[i].is_vlv) {
            process_vlv(i);
        }
    }
    return 0;
}

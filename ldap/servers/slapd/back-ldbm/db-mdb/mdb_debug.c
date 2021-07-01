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
#include "mdb_import_threads.h"

#ifdef DBMDB_DEBUG
#include <execinfo.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <syscall.h>
#endif



/* This file contains some utility to format some strings
 * and conditionnal code used to debug dbmdb plugin
 */

int dbgmdb_level = DBGMDB_LEVEL_MDBAPI;


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

static flagsdesc_t mdb_dbi_flags_desc[] = {
    { "MDB_REVERSEKEY", MDB_REVERSEKEY},
    { "MDB_DUPSORT", MDB_DUPSORT},
    { "MDB_INTEGERKEY", MDB_INTEGERKEY},
    { "MDB_DUPFIXED", MDB_DUPFIXED},
    { "MDB_INTEGERDUP", MDB_INTEGERDUP},
    { "MDB_REVERSEDUP", MDB_REVERSEDUP},
    { "MDB_CREATE", MDB_CREATE},
    { "MDB_OPEN_DIRTY_DBI", MDB_OPEN_DIRTY_DBI},
    { "MDB_MARK_DIRTY_DBI", MDB_MARK_DIRTY_DBI},
    { "MDB_TRUNCATE_DBI", MDB_TRUNCATE_DBI},
    { 0 }
};

static flagsdesc_t mdb_state_desc[] = {
    { "DBIST_DIRTY", DBIST_DIRTY },
    { 0 }
};


/* concat two strings to a buffer */
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

/* Utility to add flags values to a buffer */
int append_flags(char *buff, int bufsize, int pos, const char *name, int flags, flagsdesc_t *desc)
{
    int remain = flags;
    int pos1;
    char b[12];

    pos1 = pos = append_str(buff, bufsize, pos, name, ": ");
    for (; desc->name; desc++) {
        if ((flags & desc->val) == desc->val) {
            remain &= ~desc->val;
            pos = append_str(buff, bufsize, pos, desc->name,  remain ? "|" : "");
        }
    }
    if (pos == pos1 || remain) {
        snprintf(b, (sizeof b), "0x%x", remain);
        pos = append_str(buff, bufsize, pos, b, " ");
    }
    return pos;
}

/* Utility to add enum values to a buffer */
int append_enum(char *buff, int bufsize, int pos, const char *name, int flags, flagsdesc_t *desc)
{
    char b[12];

    pos = append_str(buff, bufsize, pos, name, ": ");
    for (; desc->name; desc++) {
        if (flags == desc->val) {
            pos = append_str(buff, bufsize, pos, desc->name, "");
            return pos;
        }
    }
    snprintf(b, (sizeof b), "0x%x", flags);
    pos = append_str(buff, bufsize, pos, b, " ");
    return pos;
}

/* convert mdb_env_open flags to string */
void dbmdb_envflags2str(int flags, char *str, int maxlen)
{
    char buf[30];
    PR_snprintf(buf, sizeof buf, "flags=0x%x", flags);
    append_flags(str, maxlen, 0, buf, flags, mdb_env_flags_desc);
}

/* Format the info string returned by dbmdb_list_dbs (i.e for dbscan -L) */
void dbmdb_format_dbslist_info(char *info, dbmdb_dbi_t *dbi)
{
    int nbentries = -1;
    int len = 0;
    dbmdb_get_entries_count(dbi, &nbentries);
    len = append_flags(info, PATH_MAX, len, "flags", dbi->state.flags, mdb_dbi_flags_desc);
    len = append_flags(info, PATH_MAX, len, " state", dbi->state.state, mdb_state_desc);
    PR_snprintf(info+len, PATH_MAX-len, " dataversion: %d nb_entries=%d", dbi->state.dataversion, nbentries);
}


#if DBMDB_DEBUG
/* MDB API Debug dhould be completed I only added the function that I wanted to trace */

#define DBGVAL2STRMAXSIZE     40


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

static flagsdesc_t mdb_loglvl_desc[] = {
    { "MDB API", DBGMDB_LEVEL_MDBAPI },
    { "TXN", DBGMDB_LEVEL_TXN },
    { "IMPORT", DBGMDB_LEVEL_IMPORT },
    { 0 }
};

static flagsdesc_t mdb_cursor_op_desc[] = {
     { "MDB_FIRST", MDB_FIRST },
     { "MDB_FIRST_DUP", MDB_FIRST_DUP },
     { "MDB_GET_BOTH", MDB_GET_BOTH },
     { "MDB_GET_BOTH_RANGE", MDB_GET_BOTH_RANGE },
     { "MDB_GET_CURRENT", MDB_GET_CURRENT },
     { "MDB_GET_MULTIPLE", MDB_GET_MULTIPLE },
     { "MDB_LAST", MDB_LAST },
     { "MDB_LAST_DUP", MDB_LAST_DUP },
     { "MDB_NEXT", MDB_NEXT },
     { "MDB_NEXT_DUP", MDB_NEXT_DUP },
     { "MDB_NEXT_MULTIPLE", MDB_NEXT_MULTIPLE },
     { "MDB_NEXT_NODUP", MDB_NEXT_NODUP },
     { "MDB_PREV", MDB_PREV },
     { "MDB_PREV_DUP", MDB_PREV_DUP },
     { "MDB_PREV_NODUP", MDB_PREV_NODUP },
     { "MDB_SET", MDB_SET },
     { "MDB_SET_KEY", MDB_SET_KEY },
     { "MDB_SET_RANGE", MDB_SET_RANGE },
     { "MDB_PREV_MULTIPLE", MDB_PREV_MULTIPLE },
     { 0 }
};

static void get_stack(char *buff, size_t buflen);

/* Convert raw data in printable string */
static void
dbgval2str(char *buff, size_t bufsiz, MDB_val *val)
{
    char *data = val ? val->mv_data : NULL;
    char *dataend = &data[val ? val->mv_size : 0];
    char *buffend = &buff[bufsiz-4];   /* Reserve space for "...\0" */

    while (data && data < dataend && buff < buffend ) {
        if (*data < 0x20 || *data > 0x7E) {
            sprintf(buff, "\\%02x", (unsigned char)*data);
            buff += 3;
        } else if (*data == '\\') {
            strcpy(buff, "\\");
            buff += 2;
        } else {
            *buff++ = *data;
        }
        data++;
    }
    *buff = 0;
    if (buff >= buffend) {
        strcpy(buffend, "...");
    }
}

static void
dbgcursor2str(char *buff, size_t bufsiz, MDB_cursor *cursor)
{
    snprintf(buff, bufsiz, "%p[%d]", cursor, mdb_cursor_dbi(cursor));
}

void
dbg_log(const char *file, int lineno, const char *funcname, int loglevel, char *fmt, ...)
{
    char flagsstr[DBGVAL2STRMAXSIZE];
    char *p = NULL;
    va_list ap;

    va_start(ap, fmt);
    p = PR_vsmprintf(fmt, ap);
    va_end(ap);

    append_flags(flagsstr, sizeof flagsstr, 0, "LOGLVL", loglevel, mdb_loglvl_desc);
    if (loglevel & dbgmdb_level) {
        slapi_log_err(SLAPI_LOG_INFO, (char*)funcname, "dbmdb: %s[%d]: %s %s\n", file, lineno, flagsstr, p);
    }
    slapi_ch_free_string(&p);
}


int
dbg_mdb_cursor_open(const char *file, int lineno, const char *funcname, MDB_txn *txn, MDB_dbi dbi, MDB_cursor **cursor)
{
    int rc = mdb_cursor_open(txn, dbi, cursor);
    if (dbgmdb_level & DBGMDB_LEVEL_MDBAPI) {
        dbg_log(file, lineno, funcname, DBGMDB_LEVEL_MDBAPI, "mdb_cursor_open(txn: %p, dbi: %d cursor: %p)=%d", txn, dbi, *cursor, rc);
    }
    return rc;
}

void
dbg_mdb_cursor_close(const char *file, int lineno, const char *funcname, MDB_cursor *cursor)
{
    mdb_cursor_close(cursor);
    if (dbgmdb_level & DBGMDB_LEVEL_MDBAPI) {
        dbg_log(file, lineno, funcname, DBGMDB_LEVEL_MDBAPI, "mdb_cursor_close(cursor: %p)", cursor);
    }
}

int
dbg_mdb_cursor_get(const char *file, int lineno, const char *funcname, MDB_cursor *cursor, MDB_val *key, MDB_val *data, MDB_cursor_op op)
{
    char keystr[DBGVAL2STRMAXSIZE];
    char datastr[DBGVAL2STRMAXSIZE];
    char flagsstr[DBGVAL2STRMAXSIZE];
    char cursorstr[DBGVAL2STRMAXSIZE];
    if (dbgmdb_level & DBGMDB_LEVEL_MDBAPI) {
        dbgval2str(keystr, sizeof keystr, key);
        dbgval2str(datastr, sizeof datastr, data);
        dbgcursor2str(cursorstr, sizeof cursorstr, cursor);
        append_enum(flagsstr, sizeof flagsstr, 0, "op", op, mdb_cursor_op_desc);
        dbg_log(file, lineno, funcname, DBGMDB_LEVEL_MDBAPI, "CALLING mdb_cursor_get(cursor: %s,key: %s,data: %s,%s)", cursorstr, keystr, datastr, flagsstr);
    }
    int rc = mdb_cursor_get(cursor, key, data, op);
    if (dbgmdb_level & DBGMDB_LEVEL_MDBAPI) {
        dbgval2str(keystr, sizeof keystr, key);
        dbgval2str(datastr, sizeof datastr, data);
        append_enum(flagsstr, sizeof flagsstr, 0, "op", op, mdb_cursor_op_desc);
        dbg_log(file, lineno, funcname, DBGMDB_LEVEL_MDBAPI, "mdb_cursor_get(cursor: %s,key: %s,data: %s,%s)=%d", cursorstr, keystr, datastr, flagsstr, rc);
    }
    return rc;
}

int
dbg_mdb_put(const char *file, int lineno, const char *funcname, MDB_txn *txn, MDB_dbi dbi, MDB_val *key, MDB_val *data, unsigned int flags)
{
    int rc = mdb_put(txn, dbi, key, data, flags);
    if (dbgmdb_level & DBGMDB_LEVEL_MDBAPI) {
        char keystr[DBGVAL2STRMAXSIZE];
        char datastr[DBGVAL2STRMAXSIZE];
        char flagsstr[DBGVAL2STRMAXSIZE];

        dbgval2str(keystr, sizeof keystr, key);
        dbgval2str(datastr, sizeof datastr, data);
        append_flags(flagsstr, sizeof flagsstr, 0, "flags", flags, mdb_op_flags_desc);
        dbg_log(file, lineno, funcname, DBGMDB_LEVEL_MDBAPI, "mdb_put(txn: %p,dbi: %d ,key: %s,data: %s,%s)=%d", txn, dbi, keystr, datastr, flagsstr, rc);
    }
    return rc;
}

int
dbg_mdb_get(const char *file, int lineno, const char *funcname, MDB_txn *txn, MDB_dbi dbi, MDB_val *key, MDB_val *data)
{
    char keystr[DBGVAL2STRMAXSIZE];
    char datastr[DBGVAL2STRMAXSIZE];
    if (dbgmdb_level & DBGMDB_LEVEL_MDBAPI) {

        dbgval2str(keystr, sizeof keystr, key);
        dbgval2str(datastr, sizeof datastr, data);
        dbg_log(file, lineno, funcname, DBGMDB_LEVEL_MDBAPI, "CALLING mdb_get(txn: %p,dbi: %d ,key: %s,data: %s)", txn, dbi, keystr, datastr);
    }
    int rc = mdb_get(txn, dbi, key, data);
    if (dbgmdb_level & DBGMDB_LEVEL_MDBAPI) {
        dbgval2str(keystr, sizeof keystr, key);
        dbgval2str(datastr, sizeof datastr, data);
        dbg_log(file, lineno, funcname, DBGMDB_LEVEL_MDBAPI, "mdb_get(txn: %p,dbi: %d ,key: %s,data: %s)=%d", txn, dbi, keystr, datastr, rc);
    }
    return rc;
}

int
dbg_mdb_del(const char *file, int lineno, const char *funcname, MDB_txn *txn, MDB_dbi dbi, MDB_val *key, MDB_val *data)
{
    int rc = mdb_del(txn, dbi, key, data);
    if (dbgmdb_level & DBGMDB_LEVEL_MDBAPI) {
        char keystr[DBGVAL2STRMAXSIZE];
        char datastr[DBGVAL2STRMAXSIZE];

        dbgval2str(keystr, sizeof keystr, key);
        dbgval2str(datastr, sizeof datastr, data);
        dbg_log(file, lineno, funcname, DBGMDB_LEVEL_MDBAPI, "mdb_del(txn: %p,dbi: %d ,key: %s,data: %s)=%d", txn, dbi, keystr, datastr, rc);
    }
    return rc;
}

int
dbg_mdb_cursor_put(const char *file, int lineno, const char *funcname, MDB_cursor *cursor, MDB_val *key, MDB_val *data, unsigned int flags)
{
    int rc = mdb_cursor_put(cursor, key, data, flags);
    if (dbgmdb_level & DBGMDB_LEVEL_MDBAPI) {
        char keystr[DBGVAL2STRMAXSIZE];
        char datastr[DBGVAL2STRMAXSIZE];
        char flagsstr[DBGVAL2STRMAXSIZE];
        char cursorstr[DBGVAL2STRMAXSIZE];

        dbgval2str(keystr, sizeof keystr, key);
        dbgval2str(datastr, sizeof datastr, data);
        dbgcursor2str(cursorstr, sizeof cursorstr, cursor);
        append_flags(flagsstr, sizeof flagsstr, 0, "flags", flags, mdb_op_flags_desc);
        dbg_log(file, lineno, funcname, DBGMDB_LEVEL_MDBAPI, "mdb_cursor_put(cursor: %s,key: %s,data: %s,%s)=%d", cursorstr, keystr, datastr, flagsstr, rc);
    }
    return rc;
}

int
dbg_mdb_dbi_open(const char *file, int lineno, const char *funcname, MDB_txn *txn, const char *dbname, unsigned int flags, MDB_dbi *dbi)
{
    int rc = mdb_dbi_open(txn, dbname, flags, dbi);
    if (dbgmdb_level & DBGMDB_LEVEL_MDBAPI) {
        char flagsstr[DBGVAL2STRMAXSIZE];
        append_flags(flagsstr, sizeof flagsstr, 0, "flags", flags, mdb_dbi_flags_desc);
        dbg_log(file, lineno, funcname, DBGMDB_LEVEL_MDBAPI, "mdb_dbi_open(txn: %p, dbname: %s, %s, *dbi: %d)=%d", txn, dbname, flagsstr, *dbi, rc);
    }
    return rc;
}

int
dbg_mdb_drop(const char *file, int lineno, const char *funcname, MDB_txn *txn, MDB_dbi dbi, int del)
{
    int rc = mdb_drop(txn, dbi, del);
    if (dbgmdb_level & DBGMDB_LEVEL_MDBAPI) {
        dbg_log(file, lineno, funcname, DBGMDB_LEVEL_MDBAPI, "mdb_drop(txn: %p, dbi: %d, del: %d)=%d", txn, dbi, del, rc);
    }
    return rc;
}

static void get_stack(char *buff, size_t buflen)
{
    /* faster version but provides less info */
    void *frames[50];
    int nbframes;
    char *pt = buff;
    char *end = &buff[buflen] - 1;  /* reserve space for final \0 */
    char **symbols;
    int len, i;

    nbframes = backtrace(frames, (sizeof frames)/sizeof frames[0]);
    symbols = backtrace_symbols(frames, nbframes);
    if (symbols) {
        for (i=0; i<nbframes; i++) {
            len = strlen(symbols[i]);
            if (pt + len + 2 >= end) {
                break;  /* Buffer too small */
            }
            *pt++ = '\t';
            strcpy(pt, symbols[i]);
            pt += len;
            *pt++ = '\n';
        }
        free(symbols);
    }
    *pt = 0;
}


int dbg_txn_begin(const char *file, int lineno, const char *funcname, MDB_env *env, MDB_txn *parent_txn, int flags, MDB_txn **txn)
{
    if (!(dbgmdb_level & DBGMDB_LEVEL_TXN)) {
        return mdb_txn_begin(env, parent_txn, flags, txn);
    }
    char stack[2048];
    char strflags[100];
    get_stack(stack, sizeof stack);
    dbmdb_envflags2str(flags, strflags, sizeof strflags);
    dbg_log(file, lineno, funcname, DBGMDB_LEVEL_TXN, "TXN_BEGIN[%d]. txn_parent=%p, %s, stack is:\n%s\n Waiting ...", pthread_gettid(), parent_txn, strflags, stack);
    int rc = mdb_txn_begin(env, parent_txn, flags, txn);
    dbg_log(file, lineno, funcname, DBGMDB_LEVEL_TXN, "Done. txn_begin(env=%p, txn_parent=%p, flags=0x%x, txn=%p) returned %d.",
        env, parent_txn, flags, *txn, rc);
    return rc;
}

int dbg_txn_end(const char *file, int lineno, const char *funcname, MDB_txn *txn, int iscommit)
{
    if (!(dbgmdb_level & DBGMDB_LEVEL_TXN)) {
        if (iscommit) {
            return mdb_txn_commit(txn);
        } else {
            mdb_txn_abort(txn);
            return 0;
        }
    }
    int rc = 0;
    char stack[2048];
    get_stack(stack, sizeof stack);
    if (iscommit) {
        rc = mdb_txn_commit(txn);
        dbg_log(file, lineno, funcname, DBGMDB_LEVEL_TXN, "TXN_COMMIT[%d] (txn=%p) returned %d. stack is:\n%s", pthread_gettid(), txn, rc, stack);
    } else {
        mdb_txn_abort(txn);
        dbg_log(file, lineno, funcname, DBGMDB_LEVEL_TXN, "TXN_ABORT[%d] (txn=%p). stack is:\n%s", pthread_gettid(), txn, stack);
    }
    return rc;
}

void
dbg_import_elmt(const char *file, int lineno, const char *funcname, const char *msg, void *elmt)
{
#define S(v) ((v)?(v):"")
    wqelem_t *elmt2 = elmt;
    char keystr[DBGVAL2STRMAXSIZE];
    char datastr[DBGVAL2STRMAXSIZE];
    char *actions[]={
        "NONE",
        "IMPORT_WRITE_ACTION_RMDIR",
        "IMPORT_WRITE_ACTION_OPEN",
        "IMPORT_WRITE_ACTION_ADD_INDEX",
        "IMPORT_WRITE_ACTION_DEL_INDEX",
        "IMPORT_WRITE_ACTION_ADD_VLV",
        "IMPORT_WRITE_ACTION_DEL_VLV",
        "IMPORT_WRITE_ACTION_ADD_ENTRYRDN",
        "IMPORT_WRITE_ACTION_DEL_ENTRYRDN",
        "IMPORT_WRITE_ACTION_ADD",
        "IMPORT_WRITE_ACTION_CLOSE"
    };
    const char *dbi_str = NULL;
    char *key_str = NULL;
    char *data_str = NULL;
    char *iupd_str = NULL;
    index_update_t iupd;
    MDB_val data;
    MDB_val key;
    Slapi_RDN *srdn = NULL;

    if (DBGMDB_LEVEL_IMPORT & dbgmdb_level) {
        return;
    }

    if (elmt2->flags & WQFL_SYNC_OP) {
        key = ((MDB_val*)(elmt2->values))[0];
        data = ((MDB_val*)(elmt2->values))[1];
    } else {
        key.mv_data = elmt2->values;
        key.mv_size = elmt2->keylen;
        data.mv_data = &elmt2->values[key.mv_size];
        data.mv_size = elmt2->data_len;
    }

    dbgval2str(keystr, sizeof keystr, &key);
    dbgval2str(datastr, sizeof datastr, &data);

    dbi_str = elmt2->slot->dbi.dbname;
    if (!dbi_str)
    dbi_str = elmt2->slot->dbipath;
    if (!dbi_str)
    dbi_str = "???";

    switch (elmt2->action) {
        case IMPORT_WRITE_ACTION_ADD_INDEX:
        case IMPORT_WRITE_ACTION_DEL_INDEX:
            memcpy(&iupd, data.mv_data, data.mv_size);
            iupd_str = slapi_ch_smprintf(" ID%dai=%s", iupd.id, iupd.a->ai_type);
            break;
        case IMPORT_WRITE_ACTION_ADD_ENTRYRDN:
        case IMPORT_WRITE_ACTION_DEL_ENTRYRDN:
            memcpy(&iupd.id, data.mv_data, sizeof iupd.id);
            srdn = key.mv_data;
            slapi_ch_free_string(&key_str);
            slapi_ch_free_string(&data_str);
            iupd_str = slapi_ch_smprintf(" ID%drdn=%s", iupd.id, slapi_rdn_get_rdn(srdn));
            break;
        default:
            break;
    }

    if (elmt2->action < IMPORT_WRITE_ACTION_RMDIR || elmt2->action > IMPORT_WRITE_ACTION_CLOSE) {
        slapi_log_err(SLAPI_LOG_INFO, "dbmdb_import_writer", "%p: %s UNKNOWN ACTION %d", elmt2, msg, elmt2->action);
    } else {
        dbg_log(file, lineno, funcname, DBGMDB_LEVEL_IMPORT,  "%p: %s %s dbi = %s%s key(%ld):\n%s\ndata(%ld):\n%s",
                elmt2, msg, actions[elmt2->action], dbi_str, S(iupd_str), key.mv_size, keystr, data.mv_size, datastr);
    }
    slapi_ch_free_string(&iupd_str);
}

#else /* DBMDB_DEBUG */

void
dbg_log(const char *file, int lineno, const char *funcname, int loglevel, char *fmt, ...)
{
}

#endif /* DBMDB_DEBUG */

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * the "new" ("deluxe") backend import code
 *
 * please make sure you use 4-space indentation on this file.
 */

#include "back-ldbm.h"
#include "vlv_srch.h"
#include "dblayer.h"
#include "import.h"

#define ERR_IMPORT_ABORTED -23
#define NEED_DN_NORM -24
#define NEED_DN_NORM_SP -25
#define NEED_DN_NORM_BT -26

/* Protect against import context destruction */
static pthread_mutex_t import_ctx_mutex = PTHREAD_MUTEX_INITIALIZER;


/********** routines to manipulate the entry fifo **********/

/********** logging stuff **********/

#define LOG_BUFFER 512


/* this adds a line to the 'nsTaskLog' value, which is cumulative (anything
 * logged here is added to the end)
 */
void
import_log_notice(ImportJob *job, int log_level, char *subsystem, char *format, ...)
{
    va_list ap;
    char buffer[LOG_BUFFER];

    va_start(ap, format);
    PR_vsnprintf(buffer, LOG_BUFFER, format, ap);
    va_end(ap);

    if (job->task) {
        slapi_task_log_notice(job->task, "%s", buffer);
    }
    /* also save it in the logs for posterity */
    if (job->flags & (FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1)) {
        slapi_log_err(log_level, subsystem, "upgradedn %s: %s\n",
                      job->inst->inst_name, buffer);
    } else if (job->flags & FLAG_REINDEXING) {
        slapi_log_err(log_level, subsystem, "reindex %s: %s\n",
                      job->inst->inst_name, buffer);
    } else {
        slapi_log_err(log_level, subsystem, "import %s: %s\n",
                      job->inst->inst_name, buffer);
    }
}


/********** starting threads and stuff **********/

/* Solaris is weird---we need an LWP per thread but NSPR doesn't give us
 * one unless we make this magic belshe-call */
/* Fixed on Solaris 8; NSPR supports PR_GLOBAL_BOUND_THREAD */
#define CREATE_THREAD PR_CreateThread

/********** monitoring the worker threads **********/



#define IMPORT_CHUNK_TEST_HOLDOFF_TIME (5 * 60) /* Seconds */

/* Got to be lower than this: */
#define IMPORT_CHUNK_TEST_CACHE_HIT_RATIO (0.99)
/* Less than half as fast as we were doing: */
#define IMPORT_CHUNK_TEST_SLOWDOWN_RATIO_A (0.5)
/* A lot less fast than we were doing: */
#define IMPORT_CHUNK_TEST_SLOWDOWN_RATIO_B (0.1)


/* find the rate (ids/time) of work from a worker thread between history
 * marks A and B.
 */
#define HISTORY(N) (job->progress_history[N])
#define TIMES(N) (job->progress_times[N])
#define PROGRESS(A, B) ((HISTORY(B) > HISTORY(A)) ? ((double)(HISTORY(B) - HISTORY(A)) / \
                                                     (double)(TIMES(B) - TIMES(A)))      \
                                                  : (double)0)

/********** running passes **********/


/* Task here is as follows:
 * First, if this is pass #1, check for the presence of a merge
 *     directory. If it is not present, create it.
 * If it is present, delete all the files in it.
 * Then, flush the dblayer and close files.
 * Now create a numbered subdir of the merge directory for this pass.
 * Next, move the index files, except entrydn, parentid and id2entry to
 *     the merge subdirectory. Important to move if we can, because
 *     that can be millions of times faster than a copy.
 * Finally open the dblayer back up because the caller expects
 *     us to not muck with it.
 */

int
import_main_offline(void *arg)
{
    ImportJob *job = (ImportJob *)arg;
    ldbm_instance *inst = job->inst;
    dblayer_private *priv = (dblayer_private *)inst->inst_li->li_dblayer_private;

    return priv->dblayer_import_fn(arg);
}


int
ldbm_back_wire_import(Slapi_PBlock *pb)
{
    backend *be = NULL;
    struct ldbminfo *li;
    dblayer_private *priv = NULL;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (be == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_wire_import",
                      "Backend is not set\n");
        return -1;
    }
    li = (struct ldbminfo *)be->be_database->plg_private;
    priv = (dblayer_private *)li->li_dblayer_private;

    return priv->ldbm_back_wire_import_fn(pb);
}

/* Threads management */

/* Return the mutex that protects against import context destruction */
pthread_mutex_t *
get_import_ctx_mutex()
{
    return &import_ctx_mutex;
}


/* tell all the threads to abort */
void
import_abort_all(ImportJob *job, int wait_for_them)
{
    ImportWorkerInfo *worker;

    /* tell all the worker threads to abort */
    job->flags |= FLAG_ABORT;
    pthread_mutex_lock(&import_ctx_mutex);
    for (worker = job->worker_list; worker; worker = worker->next)
        worker->command = ABORT;

    if (wait_for_them) {
        /* Having done that, wait for them to say that they've stopped */
        for (worker = job->worker_list; worker != NULL;) {
            DS_Sleep(PR_MillisecondsToInterval(100));
            if ((worker->state != FINISHED) && (worker->state != ABORTED) &&
                (worker->state != QUIT)) {
                continue;
            } else {
                worker = worker->next;
            }
        }
    }
    pthread_mutex_unlock(&import_ctx_mutex);
}


void *
factory_constructor(void *object __attribute__((unused)), void *parent __attribute__((unused)))
{
    return NULL;
}

void
factory_destructor(void *extension, void *object, void *parent __attribute__((unused)))
{
    ImportJob *job = (ImportJob *)extension;
    Connection *conn = (Connection *)object;
    PRThread *thread;

    if (extension == NULL)
        return;

    /* connection was destroyed while we were still storing the extension --
     * this is bad news and means we have a bulk import that needs to be
     * aborted!
     */
    thread = job->main_thread;
    slapi_log_err(SLAPI_LOG_ERR, "factory_destructor",
                  "ERROR bulk import abandoned: conn=%ld was closed\n",
                  conn->c_connid);

    import_abort_all(job, 1);
    /* wait for bdb_import_main to finish... */
    PR_JoinThread(thread);
    /* extension object is free'd by bdb_import_main */
    return;
}

/*
 * Wait 10 seconds for a reference counter to get to zero, otherwise return
 * the reference count.
 */
uint64_t
wait_for_ref_count(Slapi_Counter *inst_ref_count)
{
    uint64_t refcnt = 0;
    PRBool logged_msg = PR_FALSE;

    for (size_t i = 0; i < 20; i++) {
        refcnt = slapi_counter_get_value(inst_ref_count);
        if (refcnt == 0) {
            return 0;
        }
        if(!logged_msg) {
            slapi_log_err(SLAPI_LOG_INFO, "db2ldif",
                          "waiting for pending operations to complete ...\n");
            logged_msg = PR_TRUE;
        }

        DS_Sleep(PR_MillisecondsToInterval(500));
    }

    /* Done waiting, return the current ref count */
    return slapi_counter_get_value(inst_ref_count);
}

/********** helper functions for importing **********/

int
import_update_entry_subcount(backend *be, ID parentid, size_t sub_count, size_t t_sub_count, int isencrypted, back_txn *txn)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    int ret = 0;
    modify_context mc = {0};
    char value_buffer[22] = {0}; /* enough digits for 2^64 children */
    char t_value_buffer[22] = {0}; /* enough digits for 2^64 children */
    struct backentry *e = NULL;
    char *numsub_str = numsubordinates;
    Slapi_Mods *smods = NULL;
    static char *sourcefile = "import.c";

    /* Get hold of the parent */
    e = id2entry(be, parentid, txn, &ret);
    if ((NULL == e) || (0 != ret)) {
        slapi_log_err(SLAPI_LOG_ERR, "import_update_entry_subcount", "failed to read entry with ID %d ret=%d\n",
                parentid, ret);
        ldbm_nasty("import_update_entry_subcount", sourcefile, 5, ret);
        return (0 == ret) ? -1 : ret;
    }
    /* Lock it (not really required since we're single-threaded here, but
     * let's do it so we can reuse the modify routines) */
    cache_lock_entry(&inst->inst_cache, e);
    modify_init(&mc, e);
    mc.attr_encrypt = isencrypted;
    sprintf(value_buffer, "%lu", (long unsigned int)sub_count);
    sprintf(t_value_buffer, "%lu", (long unsigned int)t_sub_count);
    smods = slapi_mods_new();
    if (sub_count) {
        slapi_mods_add(smods, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES, numsub_str,
                       strlen(value_buffer), value_buffer);
    } else {
        /* Make sure that the attribute is deleted */
        slapi_mods_add_mod_values(smods, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES, numsub_str, NULL);
    }
    if (t_sub_count) {
        slapi_mods_add(smods, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES, LDBM_TOMBSTONE_NUMSUBORDINATES_STR,
                       strlen(t_value_buffer), t_value_buffer);
    } else {
        /* Make sure that the attribute is deleted */
        slapi_mods_add_mod_values(smods, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES, LDBM_TOMBSTONE_NUMSUBORDINATES_STR, NULL);
    }
    ret = modify_apply_mods(&mc, smods); /* smods passed in */
    if (0 == ret) {
        /* This will correctly index subordinatecount: */
        ret = modify_update_all(be, NULL, &mc, txn);
        if (0 == ret) {
            modify_switch_entries(&mc, be);
        }
    }
    /* entry is unlocked and returned to the cache in modify_term */
    modify_term(&mc, be);
    return ret;
}

struct silctx {
    const char *suffix;
    size_t count;
    bool partial;
    char *linebuff;
    size_t linebuffsize;
    size_t linebuffpos;
};

/* Read ldif line to check if suffix is present */
static bool
db2ldif_read_ldif_line(FILE *fd, struct silctx *ctx)
{
    int c = 0;
    ctx->linebuffpos = 0;
    while ((c=fgetc(fd)) != EOF) {
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            if (ctx->linebuffpos < ctx->linebuffsize) {
                ctx->linebuff[ctx->linebuffpos] = 0;
            }
            /* Check for continued line */
            c = fgetc(fd);
            if (c == EOF) {
                break;
            }
            if (c == ' ') {
                continue;
            }
            ungetc(c, fd);
            break;
        }
        if (ctx->linebuffpos < ctx->linebuffsize) {
            ctx->linebuff[ctx->linebuffpos++] = c;
        }
    }
    return c!= EOF;
}

static bool
db2ldif_is_suffix_in_file(const char *filename, struct silctx *ctx)
{
    FILE *fd = fopen(filename, "r");
    if (fd == NULL) {
        return false;
    }
    bool found = false;
    while (!found && db2ldif_read_ldif_line(fd, ctx)) {
        if (ctx->linebuffpos >= ctx->linebuffsize) {
            /* Oversided lines could not match the suffix */
            continue;
        }
        if (ctx->linebuffpos == 0) {
            ctx->count++;
            if (!ctx->partial && ctx->count > 4) {
                /* Should already have hit the suffix */
                break;
            }
        }
        if (strncmp(ctx->linebuff, "dn:", 3) == 0) {
            char *suff = ctx->linebuff+3;
            char *ndn = NULL;
            size_t ndnlen = 0;
            int rc = 0;
            while (isspace(*suff)) {
                suff++;
            }
            rc = slapi_dn_normalize_case_ext(suff, 0, &ndn, &ndnlen);
            if (rc < 0) {
                /* invalid dn ==> ignore it */
                continue;
            }
            if (strcmp(ctx->suffix, ndn) == 0) {
                found = true;
            }
            if (rc > 0) {
                slapi_ch_free_string(&ndn);
            }
        }
    }
    fclose(fd);
    return found;
}

/* Tells that all entries are skipped because suffix entry is not in ldif */
static void *
db2ldif_skip_all(void *args)
{
    Slapi_Task *task = args;
    Slapi_Backend *be = slapi_task_get_data(task);
    const char *suffix = NULL;
    if (task->task_dn == NULL) {
        task = NULL;  /* Dioscard thge pseudo task */
    }

    slapi_task_wait(task);
    suffix = slapi_sdn_get_dn(be->be_suffix);
    slapi_task_log_notice(task, "Backend instance '%s': "
            "Import complete. Processed 0 entries, all entries were "
            "skipped because ldif file does not contain the suffix entry %s).\n",
             be->be_name, suffix);
    slapi_log_err(SLAPI_LOG_WARNING, "db2ldif_is_suffix_in_ldif", "%s: "
            "Import complete. Processed 0 entries, all entries were "
            "skipped because ldif file does not contain the suffix entry %s).\n",
             be->be_name, suffix);
    slapi_task_finish(task, 0);
    g_decr_active_threadcnt();
    return NULL;
}

bool
db2ldif_is_suffix_in_ldif(Slapi_PBlock *pb, ldbm_instance *inst)
{
    char **name_array = NULL;
    Slapi_Task *task = NULL;
    struct silctx ctx = {0};
    pthread_t tid = 0;
    Slapi_Task pseudo_task = {0};

    slapi_pblock_get(pb, SLAPI_LDIF2DB_INCLUDE, &name_array);
    if (name_array != NULL) {
        ctx.partial = true;
    }
    slapi_pblock_get(pb, SLAPI_LDIF2DB_EXCLUDE, &name_array);
    if (name_array != NULL) {
        ctx.partial = true;
    }
    ctx.suffix = slapi_sdn_get_ndn(inst->inst_be->be_suffix);
    ctx.linebuffsize = 3*strlen(ctx.suffix)+5;
    if (ctx.linebuffsize <  LDIF_LINE_WIDTH+1) {
        ctx.linebuffsize = LDIF_LINE_WIDTH+1;
    }
    ctx.linebuff = slapi_ch_malloc(ctx.linebuffsize);
    slapi_pblock_get(pb, SLAPI_LDIF2DB_FILE, &name_array);
    for (; name_array && *name_array; name_array++) {
        if (db2ldif_is_suffix_in_file(*name_array, &ctx)) {
            slapi_ch_free_string(&ctx.linebuff);
            return true;
        }
    }
    slapi_ch_free_string(&ctx.linebuff);
    slapi_pblock_get(pb, SLAPI_BACKEND_TASK, &task);
    slapi_pblock_set_task_warning(pb, WARN_SKIPPED_IMPORT_ENTRY);
    if (task == NULL) {
        task = &pseudo_task;
    }
    slapi_task_set_data(task, inst->inst_be);
    /*
     * if task->task_dn it is a real task that should run in another
     * thread to let current thread creates the task entry
     * otherwise it is a pseudo task that should run in current thread
     * and if pthreasd_create fails we also run in current thread
     * to ensure the work is done but this will trigger a warning
     * about being unable to modify the task.
     */
    g_incr_active_threadcnt();  /* decreased in db2ldif_skip_all */
    if (task->task_dn == NULL ||
        pthread_create(&tid, NULL, db2ldif_skip_all, task) != 0) {
            db2ldif_skip_all(task);
    } else {
        pthread_detach(tid);
    }
    return false;
}

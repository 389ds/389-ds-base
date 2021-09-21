/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * the threads that make up an import:
 * producer (1)
 * foreman (1)
 * worker (N: 1 for each index)
 * writer (1)
 *
 * a wire import (aka "fast replica" import) won't have a producer thread.
 */

#include <stddef.h>
#include "mdb_layer.h"
#include "../vlv_srch.h"
#include "mdb_import_threads.h"

#define CV_TIMEOUT    10000000  /* 10 milli seconds timeout */

/* Value to determine when to wait before adding item to write queue and
 * when to wait until having enough item in queue to start emptying it
 * Note: the thresholds applies to a single writing queue slot (i.e dbi)
 */
#define MAX_WEIGHT        (256*1024)        /* queue full threshold */
#define MIN_WEIGHT        (MAX_WEIGHT/4)    /* enough data to open a txn threshold */
#define BASE_WEIGHT       256               /* minimum weight of a queue element */

 /* Compute the padding size needed to get aligned on long integer */
#define ALIGN_TO_LONG(pos)            ((-(long)(pos))&((sizeof(long))-1))


static void dbmdb_import_wait_for_space_in_fifo(ImportJob *job, size_t new_esize);
static int dbmdb_import_get_and_add_parent_rdns(ImportWorkerInfo *info, ldbm_instance *inst, dbmdb_dbi_t **db, dbi_txn_t *txn, ID id, ID *total_id, Slapi_RDN *srdn, int *curr_entry);
static int _get_import_entryusn(ImportJob *job, Slapi_Value **usn_value);
static pseudo_back_txn_t **dbmdb_get_ptwctx(ImportJob*job, ImportWorkerInfo *info, dbmdb_wctx_id_t wctx_id);
static pseudo_back_txn_t *dbmdb_new_wctx(ImportJob*job, ImportWorkerInfo *info, dbmdb_wctx_id_t wctx_id);
static long dbmdb_get_wqslot(ImportJob* job, ImportWorkerInfo *info, dbmdb_wctx_id_t wctx_id);
static void dbmdb_free_wctx(ImportJob*job, ImportWorkerInfo*info, dbmdb_wctx_id_t wctx_id);


static struct backentry *
dbmdb_import_make_backentry(Slapi_Entry *e, ID id)
{
    struct backentry *ep = backentry_alloc();

    if (NULL != ep) {
        ep->ep_entry = e;
        ep->ep_id = id;
    }
    return ep;
}

static void
dbmdb_import_decref_entry(struct backentry *ep)
{
    PR_AtomicDecrement(&(ep->ep_refcnt));
    PR_ASSERT(ep->ep_refcnt >= 0);
}

/* generate uniqueid if requested */
static int
dbmdb_import_generate_uniqueid(ImportJob *job, Slapi_Entry *e)
{
    const char *uniqueid = slapi_entry_get_uniqueid(e);
    int rc = UID_SUCCESS;

    if (!uniqueid && (job->uuid_gen_type != SLAPI_UNIQUEID_GENERATE_NONE)) {
        char *newuniqueid;

        /* generate id based on dn */
        if (job->uuid_gen_type == SLAPI_UNIQUEID_GENERATE_NAME_BASED) {
            char *dn = slapi_entry_get_dn(e);

            rc = slapi_uniqueIDGenerateFromNameString(&newuniqueid,
                                                      job->uuid_namespace, dn, strlen(dn));
        } else {
            /* time based */
            rc = slapi_uniqueIDGenerateString(&newuniqueid);
        }

        if (rc == UID_SUCCESS) {
            slapi_entry_set_uniqueid(e, newuniqueid);
        } else {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_generate_uniqueid",
                          "Failed to generate uniqueid for %s; error=%d.\n",
                          slapi_entry_get_dn_const(e), rc);
        }
    }

    return (rc);
}

/*
 * Check if the tombstone csn is missing, if so add it.
 */
static void
dbmdb_import_generate_tombstone_csn(Slapi_Entry *e)
{
    if (e->e_flags & SLAPI_ENTRY_FLAG_TOMBSTONE) {
        if (attrlist_find(e->e_attrs, SLAPI_ATTR_TOMBSTONE_CSN) == NULL) {
            const CSN *tombstone_csn = NULL;
            char tombstone_csnstr[CSN_STRSIZE];

            /* Add the tombstone csn str */
            if ((tombstone_csn = entry_get_deletion_csn(e))) {
                csn_as_string(tombstone_csn, PR_FALSE, tombstone_csnstr);
                slapi_entry_add_string(e, SLAPI_ATTR_TOMBSTONE_CSN, tombstone_csnstr);
            }
        }
    }
}


/**********  BETTER LDIF PARSER  **********/


/* like the function in libldif, except this one doesn't need to use
 * FILE (which breaks on various platforms for >4G files or large numbers
 * of open files)
 */
#define LDIF_BUFFER_SIZE 8192

typedef struct
{
    char *b;       /* buffer */
    size_t size;   /* how full the buffer is */
    size_t offset; /* where the current entry starts */
} ldif_context;

static void
dbmdb_import_init_ldif(ldif_context *c)
{
    c->size = c->offset = 0;
    c->b = NULL;
}

static void
dbmdb_import_free_ldif(ldif_context *c)
{
    if (c->b)
        FREE(c->b);
    dbmdb_import_init_ldif(c);
}

static char *
dbmdb_import_get_entry(ldif_context *c, int fd, int *lineno)
{
    int ret;
    int done = 0, got_lf = 0;
    size_t bufSize = 0, bufOffset = 0, i;
    char *buf = NULL;

    while (!done) {

        /* If there's no data in the buffer, get some */
        if ((c->size == 0) || (c->offset == c->size)) {
            /* Do we even have a buffer ? */
            if (!c->b) {
                c->b = slapi_ch_malloc(LDIF_BUFFER_SIZE);
                if (!c->b)
                    return NULL;
            }
            ret = read(fd, c->b, LDIF_BUFFER_SIZE);
            if (ret < 0) {
                /* Must be error */
                goto error;
            } else if (ret == 0) {
                /* eof */
                if (buf) {
                    /* last entry */
                    buf[bufOffset] = 0;
                    return buf;
                }
                return NULL;
            } else {
                /* read completed OK */
                c->size = ret;
                c->offset = 0;
            }
        }

        /* skip blank lines at start of entry */
        if (bufOffset == 0) {
            size_t n;
            char *p;

            for (n = c->offset, p = c->b + n; n < c->size; n++, p++) {
                if (!(*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t'))
                    break;
            }
            c->offset = n;
            if (c->offset == c->size)
                continue;
        }

        i = c->offset;
        while (!done && (i < c->size)) {
            /* scan forward in the buffer, looking for the end of the entry */
            while ((i < c->size) && (c->b[i] != '\n'))
                i++;

            if ((i < c->size) && (c->b[i] == '\n')) {
                if (got_lf && ((i == 0) || ((i == 1) && (c->b[0] == '\r')))) {
                    /* saw an lf at the end of the last buffer */
                    i++, (*lineno)++;
                    done = 1;
                    got_lf = 0;
                    break;
                }
                got_lf = 0;
                (*lineno)++;
                /* is this the end?  (need another linefeed) */
                if (++i < c->size) {
                    if (c->b[i] == '\n') {
                        /* gotcha! */
                        i++, (*lineno)++;
                        done = 1;
                    } else if (c->b[i] == '\r') {
                        if (++i < c->size) {
                            if (c->b[i] == '\n') {
                                /* gotcha! (nt) */
                                i++, (*lineno)++;
                                done = 1;
                            }
                        } else {
                            got_lf = 1;
                        }
                    }
                } else {
                    /* lf at the very end of the buffer */
                    got_lf = 1;
                }
            }
        }

        /* copy what we did so far into the output buffer */
        /* (first, make sure the output buffer is large enough) */
        if (bufSize - bufOffset < i - c->offset + 1) {
            char *newbuf = NULL;
            size_t newsize = (buf ? bufSize * 2 : LDIF_BUFFER_SIZE);

            newbuf = slapi_ch_malloc(newsize);
            if (!newbuf)
                goto error;
            /* copy over the old data (if there was any) */
            if (buf) {
                memmove(newbuf, buf, bufOffset);
                slapi_ch_free((void **)&buf);
            }
            buf = newbuf;
            bufSize = newsize;
        }
        memmove(buf + bufOffset, c->b + c->offset, i - c->offset);
        bufOffset += (i - c->offset);
        c->offset = i;
    }

    /* add terminating NUL char */
    buf[bufOffset] = 0;
    return buf;

error:
    if (buf)
        slapi_ch_free((void **)&buf);
    return NULL;
}


/**********  THREADS  **********/

/*
 * Description:
 * 1) return the ldif version #
 * 2) replace "version: 1" with "#ersion: 1"
 *    to pretend like a comment for the str2entry
 */
static int
dbmdb_import_get_version(char *str)
{
    char *s;
    char *mystr, *ms;
    int offset;
    int my_version = 0;

    if ((s = strstr(str, "version:")) == NULL)
        return 0;

    offset = s - str;
    mystr = ms = slapi_ch_strdup(str);
    while ((s = ldif_getline(&ms)) != NULL) {
        struct berval type = {0, NULL}, value = {0, NULL};
        int freeval = 0;
        if (slapi_ldif_parse_line(s, &type, &value, &freeval) >= 0) {
            if (!PL_strncasecmp(type.bv_val, "version", type.bv_len)) {
                my_version = atoi(value.bv_val);
                *(str + offset) = '#';
                /* the memory below was not allocated by the slapi_ch_ functions */
                if (freeval)
                    slapi_ch_free_string(&value.bv_val);
                break;
            }
        }
        /* the memory below was not allocated by the slapi_ch_ functions */
        if (freeval)
            slapi_ch_free_string(&value.bv_val);
    }

    slapi_ch_free((void **)&mystr);
    return my_version;
}

/*
 * add CreatorsName, ModifiersName, CreateTimestamp, ModifyTimestamp to entry
 */
static void
dbmdb_import_add_created_attrs(Slapi_Entry *e)
{
    char buf[SLAPI_TIMESTAMP_BUFSIZE];
    struct berval bv;
    struct berval *bvals[2];

    bvals[0] = &bv;
    bvals[1] = NULL;

    bv.bv_val = "";
    bv.bv_len = 0;
    if (!attrlist_find(e->e_attrs, "creatorsname")) {
        slapi_entry_attr_replace(e, "creatorsname", bvals);
    }
    if (!attrlist_find(e->e_attrs, "modifiersname")) {
        slapi_entry_attr_replace(e, "modifiersname", bvals);
    }

    slapi_timestamp_utc_hr(buf, SLAPI_TIMESTAMP_BUFSIZE);

    bv.bv_val = buf;
    bv.bv_len = strlen(bv.bv_val);
    if (!attrlist_find(e->e_attrs, "createtimestamp")) {
        slapi_entry_attr_replace(e, "createtimestamp", bvals);
    }
    if (!attrlist_find(e->e_attrs, "modifytimestamp")) {
        slapi_entry_attr_replace(e, "modifytimestamp", bvals);
    }
}

/* producer thread:
 * read through the given file list, parsing entries (str2entry), assigning
 * them IDs and queueing them on the entry FIFO.  other threads will do
 * the indexing.
 */
void
dbmdb_import_producer(void *param)
{
    ImportWorkerInfo *info = (ImportWorkerInfo *)param;
    ImportJob *job = info->job;
    ID id = job->first_ID, id_filestart = id;
    Slapi_Entry *e = NULL;
    struct backentry *ep = NULL, *old_ep = NULL;
    ldbm_instance *inst = job->inst;
    backend *be = inst->inst_be;
    PRIntervalTime sleeptime;
    char *estr = NULL;
    int str2entry_flags = 0;
    int finished = 0;
    int detected_eof = 0;
    int fd, curr_file, curr_lineno = 0;
    char *curr_filename = NULL;
    int idx;
    ldif_context c;
    int my_version = 0;
    size_t newesize = 0;
    Slapi_Attr *attr = NULL;

    PR_ASSERT(info != NULL);
    PR_ASSERT(inst != NULL);

    if (job->flags & FLAG_ABORT) {
        goto error;
    }

    sleeptime = PR_MillisecondsToInterval(import_sleep_time);

    /* pause until we're told to run */
    while ((info->command == PAUSE) && !(job->flags & FLAG_ABORT)) {
        info->state = WAITING;
        DS_Sleep(sleeptime);
    }
    info->state = RUNNING;
    dbmdb_import_init_ldif(&c);

    /* Get entryusn, if needed. */
    _get_import_entryusn(job, &(job->usn_value));

    /* jumpstart by opening the first file */
    curr_file = 0;
    fd = -1;
    detected_eof = finished = 0;

    /* we loop around reading the input files and processing each entry
     * as we read it.
     */
    while (!finished) {
        int flags = 0;
        int prev_lineno = 0;
        int lines_in_entry = 0;
        int syntax_err = 0;

        if (job->flags & FLAG_ABORT) {
            goto error;
        }

        /* move on to next file? */
        if (detected_eof) {
            /* check if the file can still be read, whine if so... */
            if (read(fd, (void *)&idx, 1) > 0) {
                import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_producer", "Unexpected end of file found "
                                                                             "at line %d of file \"%s\"",
                                  curr_lineno,
                                  curr_filename);
            }

            if (fd == STDIN_FILENO) {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_import_producer", "Finished scanning file stdin (%lu "
                                                                          "entries)",
                                  (u_long)(id - id_filestart));
            } else {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_import_producer", "Finished scanning file \"%s\" (%lu "
                                                                          "entries)",
                                  curr_filename, (u_long)(id - id_filestart));
            }
            close(fd);
            fd = -1;
            detected_eof = 0;
            id_filestart = id;
            curr_file++;
            if (job->task) {
                job->task->task_progress++;
                slapi_task_status_changed(job->task);
            }
            if (job->input_filenames[curr_file] == NULL) {
                /* done! */
                finished = 1;
                break;
            }
        }

        /* separate from above, because this is also triggered when we
         * start (to open the first file)
         */
        if (fd < 0) {
            curr_lineno = 0;
            curr_filename = job->input_filenames[curr_file];
            if (strcmp(curr_filename, "-") == 0) {
                fd = STDIN_FILENO;
            } else {
                int o_flag = O_RDONLY;
                fd = dbmdb_open_huge_file(curr_filename, o_flag, 0);
            }
            if (fd < 0) {
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_producer",
                                  "Could not open LDIF file \"%s\", errno %d (%s)",
                                  curr_filename, errno, slapd_system_strerror(errno));
                goto error;
            }
            if (fd == STDIN_FILENO) {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_import_producer", "Processing file stdin");
            } else {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_import_producer",
                                  "Processing file \"%s\"", curr_filename);
            }
        }
        if (job->flags & FLAG_ABORT) {
            goto error;
        }

        str2entry_flags = SLAPI_STR2ENTRY_TOMBSTONE_CHECK |
                          SLAPI_STR2ENTRY_REMOVEDUPVALS |
                          SLAPI_STR2ENTRY_EXPAND_OBJECTCLASSES |
                          SLAPI_STR2ENTRY_ADDRDNVALS |
                          SLAPI_STR2ENTRY_NOT_WELL_FORMED_LDIF;

        while ((info->command == PAUSE) && !(job->flags & FLAG_ABORT)) {
            info->state = WAITING;
            DS_Sleep(sleeptime);
        }
        info->state = RUNNING;

        prev_lineno = curr_lineno;
        estr = dbmdb_import_get_entry(&c, fd, &curr_lineno);

        lines_in_entry = curr_lineno - prev_lineno;
        if (!estr) {
            /* error reading entry, or end of file */
            detected_eof = 1;
            continue;
        }

        if (0 == my_version && 0 == strncmp(estr, "version:", 8)) {
            my_version = dbmdb_import_get_version(estr);
            str2entry_flags |= SLAPI_STR2ENTRY_INCLUDE_VERSION_STR;
        }

        /* If there are more than so many lines in the entry, we tell
         * str2entry to optimize for a large entry.
         */
        if (lines_in_entry > STR2ENTRY_ATTRIBUTE_PRESENCE_CHECK_THRESHOLD) {
            flags = str2entry_flags | SLAPI_STR2ENTRY_BIGENTRY;
        } else {
            flags = str2entry_flags;
        }
        if (!(str2entry_flags & SLAPI_STR2ENTRY_INCLUDE_VERSION_STR) &&
            entryrdn_get_switch()) { /* subtree-rename: on */
            char *dn = NULL;
            char *normdn = NULL;
            int rc = 0; /* estr should start with "dn: " or "dn:: " */
            if (strncmp(estr, "dn: ", 4) &&
                NULL == strstr(estr, "\ndn: ") && /* in case comments precedes
                                                     the entry */
                strncmp(estr, "dn:: ", 5) &&
                NULL == strstr(estr, "\ndn:: ")) { /* ditto */
                import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_producer",
                                  "Skipping bad LDIF entry (not starting with \"dn: \") ending line %d of file \"%s\"",
                                  curr_lineno, curr_filename);
                FREE(estr);
                continue;
            }
            /* get_value_from_string decodes base64 if it is encoded. */
            rc = get_value_from_string((const char *)estr, "dn", &dn);
            if (rc) {
                import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_producer",
                                  "Skipping bad LDIF entry (dn has no value\n");
                FREE(estr);
                continue;
            }
            normdn = slapi_create_dn_string("%s", dn);
            slapi_ch_free_string(&dn);
            e = slapi_str2entry_ext(normdn, NULL, estr,
                                    flags | SLAPI_STR2ENTRY_NO_ENTRYDN);
            slapi_ch_free_string(&normdn);
        } else {
            e = slapi_str2entry(estr, flags);
        }
        FREE(estr);
        if (!e) {
            if (!(str2entry_flags & SLAPI_STR2ENTRY_INCLUDE_VERSION_STR)) {
                import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_producer",
                                  "Skipping bad LDIF entry ending line %d of file \"%s\"",
                                  curr_lineno, curr_filename);
            }
            continue;
        }
        /* From here, e != NULL */
        if (0 == my_version) {
            /* after the first entry version string won't be given */
            my_version = -1;
        }

        if (!dbmdb_import_entry_belongs_here(e, inst->inst_be)) {
            /* silently skip */
            slapi_entry_free(e);
            continue;
        }

        if (slapi_entry_schema_check(NULL, e) != 0) {
            import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_producer",
                              "Skipping entry \"%s\" which violates schema, ending line %d of file \"%s\"",
                              slapi_entry_get_dn(e), curr_lineno, curr_filename);
            slapi_entry_free(e);

            job->skipped++;
            continue;
        }

        /* If we are importing pre-encrypted attributes, we need
         * to skip syntax checks for the encrypted values. */
        if (!(job->encrypt) && inst->attrcrypt_configured) {
            Slapi_Entry *e_copy = NULL;

            /* Scan through the entry to see if any present
             * attributes are configured for encryption. */
            slapi_entry_first_attr(e, &attr);
            while (attr) {
                char *type = NULL;
                struct attrinfo *ai = NULL;

                slapi_attr_get_type(attr, &type);

                /* Check if this type is configured for encryption. */
                ainfo_get(be, type, &ai);
                if (ai->ai_attrcrypt != NULL) {
                    /* Make a copy of the entry to use for syntax
                     * checking if a copy has not been made yet. */
                    if (e_copy == NULL) {
                        e_copy = slapi_entry_dup(e);
                    }

                    /* Delete the enrypted attribute from the copy. */
                    slapi_entry_attr_delete(e_copy, type);
                }

                slapi_entry_next_attr(e, attr, &attr);
            }

            if (e_copy) {
                syntax_err = slapi_entry_syntax_check(NULL, e_copy, 0);
                slapi_entry_free(e_copy);
            } else {
                syntax_err = slapi_entry_syntax_check(NULL, e, 0);
            }
        } else {
            syntax_err = slapi_entry_syntax_check(NULL, e, 0);
        }

        /* Check attribute syntax */
        if (syntax_err != 0) {
            import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_producer",
                              "Skipping entry \"%s\" which violates attribute syntax, ending line %d of "
                              "file \"%s\"",
                              slapi_entry_get_dn(e), curr_lineno, curr_filename);
            slapi_entry_free(e);

            job->skipped++;
            continue;
        }

        /* generate uniqueid if necessary */
        if (dbmdb_import_generate_uniqueid(job, e) != UID_SUCCESS) {
            goto error;
        }

        if (g_get_global_lastmod()) {
            dbmdb_import_add_created_attrs(e);
        }
        /* Add nsTombstoneCSN to tombstone entries unless it's already present */
        dbmdb_import_generate_tombstone_csn(e);

        ep = dbmdb_import_make_backentry(e, id);
        if ((ep == NULL) || (ep->ep_entry == NULL)) {
            slapi_entry_free(e);
            backentry_free(&ep);
            goto error;
        }

        /* check for include/exclude subtree lists */
        if (!dbmdb_back_ok_to_dump(backentry_get_ndn(ep),
                                  job->include_subtrees,
                                  job->exclude_subtrees)) {
            backentry_free(&ep);
            continue;
        }

        /* not sure what this does, but it looked like it could be
         * simplified.  if it's broken, it's my fault.  -robey
         */
        if (slapi_entry_attr_find(ep->ep_entry, "userpassword", &attr) == 0) {
            Slapi_Value **va = attr_get_present_values(attr);

            pw_encodevals((Slapi_Value **)va); /* jcm - cast away const */
        }

        if (job->flags & FLAG_ABORT) {
            backentry_free(&ep);
            goto error;
        }

        /* if usn_value is available AND the entry does not have it, */
        if (job->usn_value && slapi_entry_attr_find(ep->ep_entry,
                                                    SLAPI_ATTR_ENTRYUSN, &attr)) {
            slapi_entry_add_value(ep->ep_entry, SLAPI_ATTR_ENTRYUSN,
                                  job->usn_value);
        }

        /* Now we have this new entry, all decoded
         * Next thing we need to do is:
         * (1) see if the appropriate fifo location contains an
         *     entry which had been processed by the indexers.
         *     If so, proceed.
         *     If not, spin waiting for it to become free.
         * (2) free the old entry and store the new one there.
         * (3) Update the job progress indicators so the indexers
         *     can use the new entry.
         */
        idx = id % job->fifo.size;
        old_ep = job->fifo.item[idx].entry;
        if (old_ep) {
            /* for the slot to be recycled, it needs to be already absorbed
             * by the foreman (id >= ready_EID), and all the workers need to
             * be finished with it (refcount = 0).
             */
            while (((old_ep->ep_refcnt > 0) ||
                    (old_ep->ep_id >= job->ready_EID)) &&
                   (info->command != ABORT) && !(job->flags & FLAG_ABORT)) {
                info->state = WAITING;
                DS_Sleep(sleeptime);
            }
            if (job->flags & FLAG_ABORT) {
                backentry_free(&ep);
                goto error;
            }
            info->state = RUNNING;
            PR_ASSERT(old_ep == job->fifo.item[idx].entry);
            job->fifo.item[idx].entry = NULL;
            if (job->fifo.c_bsize > job->fifo.item[idx].esize)
                job->fifo.c_bsize -= job->fifo.item[idx].esize;
            else
                job->fifo.c_bsize = 0;
            backentry_free(&old_ep);
        }

        newesize = (slapi_entry_size(ep->ep_entry) + sizeof(struct backentry));
        /* Check to see if we have the space in the fifo */
        /* If not, make it bigger if possible */
        if (dbmdb_import_fifo_validate_capacity_or_expand(job, newesize) == 1) {
            import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_producer", "Skipping entry \"%s\" "
                                                                         "ending line %d of file \"%s\"",
                              slapi_entry_get_dn(e), curr_lineno, curr_filename);
            import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_producer",
                              "REASON: entry too large (%lu bytes) for the buffer size (%lu bytes), "
                              "and we were UNABLE to expand buffer.",
                              (long unsigned int)newesize, (long unsigned int)job->fifo.bsize);
            backentry_free(&ep);
            job->skipped++;
            continue;
        }
        /* Now check if fifo has enough space for the new entry */
        if ((job->fifo.c_bsize + newesize) > job->fifo.bsize) {
            dbmdb_import_wait_for_space_in_fifo(job, newesize);
        }

        /* We have enough space */
        job->fifo.item[idx].filename = curr_filename;
        job->fifo.item[idx].line = curr_lineno;
        job->fifo.item[idx].entry = ep;
        job->fifo.item[idx].bad = 0;
        job->fifo.item[idx].esize = newesize;

        /* Add the entry size to total fifo size */
        job->fifo.c_bsize += ep->ep_entry ? job->fifo.item[idx].esize : 0;

        /* Update the job to show our progress */
        job->lead_ID = id;
        if ((id - info->first_ID) <= job->fifo.size) {
            job->trailing_ID = info->first_ID;
        } else {
            job->trailing_ID = id - job->fifo.size;
        }

        /* Update our progress meter too */
        info->last_ID_processed = id;
        id++;
        if (job->flags & FLAG_ABORT) {
            goto error;
        }
        if (info->command == STOP) {
            if (fd >= 0)
                close(fd);
            finished = 1;
        }
    }

    /* capture skipped entry warnings for this task */
    if((job) && (job->skipped)) {
        slapi_task_set_warning(job->task, WARN_SKIPPED_IMPORT_ENTRY);
    }

    slapi_value_free(&(job->usn_value));
    dbmdb_import_free_ldif(&c);
    info->state = FINISHED;
    return;

error:
    slapi_value_free(&(job->usn_value));
    info->state = ABORTED;
}

static int
dbmdb_index_set_entry_to_fifo(ImportWorkerInfo *info, Slapi_Entry *e, ID id, ID *total_id, int curr_entry)
{
    int rc = -1;
    ImportJob *job = info->job;
    int idx;
    struct backentry *ep = NULL, *old_ep = NULL;
    size_t newesize = 0;
    Slapi_Attr *attr = NULL;
    PRIntervalTime sleeptime = PR_MillisecondsToInterval(import_sleep_time);

    /* generate uniqueid if necessary */
    if (dbmdb_import_generate_uniqueid(job, e) != UID_SUCCESS) {
        goto bail;
    }

    ep = dbmdb_import_make_backentry(e, id);
    if (NULL == ep) {
        goto bail;
    }

    /* not sure what this does, but it looked like it could be
     * simplified.  if it's broken, it's my fault.  -robey
     */
    if (slapi_entry_attr_find(ep->ep_entry, "userpassword", &attr) == 0) {
        Slapi_Value **va = attr_get_present_values(attr);

        pw_encodevals((Slapi_Value **)va); /* jcm - cast away const */
    }

    if (job->flags & FLAG_ABORT) {
        backentry_free(&ep);
        goto bail;
    }

    /* Now we have this new entry, all decoded
     * Next thing we need to do is:
     * (1) see if the appropriate fifo location contains an
     *     entry which had been processed by the indexers.
     *     If so, proceed.
     *     If not, spin waiting for it to become free.
     * (2) free the old entry and store the new one there.
     * (3) Update the job progress indicators so the indexers
     *     can use the new entry.
     */
    idx = (*total_id) % job->fifo.size;
    old_ep = job->fifo.item[idx].entry;
    if (old_ep) {
        /* for the slot to be recycled, it needs to be already absorbed
         * by the foreman ((*total_id) >= ready_EID), and all the workers
         * need to be finished with it (refcount = 0).
         */
        while (((old_ep->ep_refcnt > 0) || (old_ep->ep_id >= job->ready_EID)) && (info->command != ABORT) && !(job->flags & FLAG_ABORT)) {
            info->state = WAITING;
            DS_Sleep(sleeptime);
        }
        if (job->flags & FLAG_ABORT) {
            backentry_free(&ep);
            goto bail;
        }

        info->state = RUNNING;
        PR_ASSERT(old_ep == job->fifo.item[idx].entry);
        job->fifo.item[idx].entry = NULL;
        if (job->fifo.c_bsize > job->fifo.item[idx].esize) {
            job->fifo.c_bsize -= job->fifo.item[idx].esize;
        } else {
            job->fifo.c_bsize = 0;
        }
        backentry_free(&old_ep);
    }

    newesize = (slapi_entry_size(ep->ep_entry) + sizeof(struct backentry));
    if (dbmdb_import_fifo_validate_capacity_or_expand(job, newesize) == 1) {
        import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_index_set_entry_to_fifo", "Skipping entry \"%s\"",
                          slapi_entry_get_dn(e));
        import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_index_set_entry_to_fifo", "REASON: entry too large (%lu bytes) for "
                                                                             "the buffer size (%lu bytes), and we were UNABLE to expand buffer.",
                          (long unsigned int)newesize, (long unsigned int)job->fifo.bsize);
        backentry_free(&ep);
        job->skipped++;
        rc = 0; /* go to the next loop */
    }
    /* Now check if fifo has enough space for the new entry */
    if ((job->fifo.c_bsize + newesize) > job->fifo.bsize) {
        dbmdb_import_wait_for_space_in_fifo(job, newesize);
    }

    /* We have enough space */
    job->fifo.item[idx].filename = ID2ENTRY LDBM_FILENAME_SUFFIX;
    job->fifo.item[idx].line = curr_entry;
    job->fifo.item[idx].entry = ep;
    job->fifo.item[idx].bad = 0;
    job->fifo.item[idx].esize = newesize;

    /* Add the entry size to total fifo size */
    job->fifo.c_bsize += ep->ep_entry ? job->fifo.item[idx].esize : 0;

    /* Update the job to show our progress */
    job->lead_ID = *total_id;
    if ((*total_id - info->first_ID) <= job->fifo.size) {
        job->trailing_ID = info->first_ID;
    } else {
        job->trailing_ID = *total_id - job->fifo.size;
    }

    /* Update our progress meter too */
    info->last_ID_processed = *total_id;
    (*total_id)++;
    rc = 0; /* done */
bail:
    return rc;
 }

static int dbmdb_get_aux_id2entry(backend*be, dbmdb_dbi_t **dbi, char **path)
{
     return dbmdb_open_dbi_from_filename(dbi, be, ID2ENTRY, NULL, 0);
}

/* producer thread for re-indexing:
 * read id2entry, parsing entries (str2entry) (needed???), assigning
 * them IDs (again, needed???) and queueing them on the entry FIFO.
 * other threads will do the indexing -- same as in import.
 */
void
dbmdb_index_producer(void *param)
{
    ImportWorkerInfo *info = (ImportWorkerInfo *)param;
    ImportJob *job = info->job;
    ID id = job->first_ID;
    Slapi_Entry *e = NULL;
    ldbm_instance *inst = job->inst;
    struct ldbminfo *li = (struct ldbminfo*)inst->inst_be->be_database->plg_private;
    dbmdb_ctx_t *ctx = MDB_CONFIG(li);
    long entrydn_wqslot = -1;
    PRIntervalTime sleeptime;
    int finished = 0;
    int rc = 0;


    char *id2entry = NULL;
    dbmdb_dbi_t *db = NULL;
    dbmdb_cursor_t dbc = {0};
    MDB_val key = {0};
    MDB_val data = {0};
    char *entry_str = NULL;
    uint entry_len = 0;
    int db_rval = -1;
    backend *be = inst->inst_be;
    int isfirst = 1;
    int curr_entry = 0;

    PR_ASSERT(info != NULL);
    PR_ASSERT(inst != NULL);
    PR_ASSERT(be != NULL);

    if (job->flags & FLAG_ABORT)
        goto bail;

    sleeptime = PR_MillisecondsToInterval(import_sleep_time);

    /* pause until we're told to run */
    while ((info->command == PAUSE) && !(job->flags & FLAG_ABORT)) {
        info->state = WAITING;
        DS_Sleep(sleeptime);
    }
    info->state = RUNNING;

    /* open id2entry with dedicated db env and db handler */
    if (dbmdb_get_aux_id2entry(be, &db, &id2entry) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_index_producer", "Could not open id2entry\n");
        goto bail;
    }
    if (job->flags & FLAG_DN2RDN) {
        /* open new id2entry for the rdn format entries */
        /* We also need to write in id2entry so reserve a slot in writer thread
         * andf lags delayed writes  oid2entrywillnotgetoverwrittenwhilewearereadingit
         */
        if (dbmdb_import_writer_create_dbi(info, WCTX_ENTRYDN,ID2ENTRY LDBM_SUFFIX, PR_TRUE)) {
             slapi_log_err(SLAPI_LOG_ERR, "dbmdb_index_producer", "Could not register writer thread slot for new id2entry\n");
            goto bail;
        }
         entrydn_wqslot = dbmdb_get_wqslot(job, NULL, WCTX_ENTRYDN);
    }

    /* get a cursor to we can walk over the table */
     db_rval = dbmdb_open_cursor(&dbc, ctx, db, db->state.flags|MDB_RDONLY);
     if( db_rval){
        slapi_log_err(SLAPI_LOG_ERR,
                      "dbmdb_index_producer", "Failed to get cursor for reindexing\n");
        dblayer_release_id2entry(be, &db);
        goto bail;
    }

    /* we loop around reading the input files and processing each entry
     * as we read it.
     */
    finished = 0;
    while (!finished) {
        ID temp_id;

        if (job->flags & FLAG_ABORT) {
            goto bail;
        }
        while ((info->command == PAUSE) && !(job->flags & FLAG_ABORT)) {
            info->state = WAITING;
            DS_Sleep(sleeptime);
        }
        info->state = RUNNING;

        if (isfirst) {
             db_rval = MDB_CURSOR_GET(dbc.cur, &key, &data, MDB_FIRST);
            isfirst = 0;
        } else {
             db_rval = MDB_CURSOR_GET(dbc.cur, &key, &data, MDB_NEXT);
        }

        if (0 != db_rval) {
            if (MDB_NOTFOUND != db_rval) {
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_index_producer", "%s: Failed to read database, "
                                                               "errno=%d (%s)\n",
                              inst->inst_name, db_rval,
                              dblayer_strerror(db_rval));
                if (job->task) {
                    slapi_task_log_notice(job->task,
                                          "%s: Failed to read database, err %d (%s)",
                                          inst->inst_name, db_rval,
                                          dblayer_strerror(db_rval));
                }
            }
            break;
        }
        curr_entry++;
        temp_id = id_stored_to_internal((char *)key.mv_data);

        /* call post-entry plugin */
         entry_str = data.mv_data;
         entry_len = data.mv_size;
         plugin_call_entryfetch_plugins(&entry_str, &entry_len);
        if (entryrdn_get_switch()) {
            char *rdn = NULL;

            /* rdn is allocated in get_value_from_string */
            rc = get_value_from_string(entry_str, "rdn", &rdn);
            if (rc) {
                /* data.dptr may not include rdn: ..., try "dn: ..." */
                e = slapi_str2entry(entry_str, SLAPI_STR2ENTRY_NO_ENTRYDN);
                if (job->flags & FLAG_DN2RDN) {
                    int len = 0;
                    int options = SLAPI_DUMP_STATEINFO | SLAPI_DUMP_UNIQUEID |
                                  SLAPI_DUMP_RDN_ENTRY;
                     if (data.mv_data != entry_str) {
                         slapi_ch_free_string(&entry_str);
                     }
                     entry_str = slapi_entry2str_with_options(e, &len, options);
                     entry_len = len+1;

                    /* store it in the new id2entry db file */
                    rc = dbmdb_import_write_push(job, entrydn_wqslot, IMPORT_WRITE_ACTION_ADD, &key, &data);
                    if (rc) {
                        slapi_log_err(SLAPI_LOG_TRACE,
                                      "dbmdb_index_producer", "Converting an entry "
                                                        "from dn format to rdn format failed "
                                                        "(dn: %s, ID: %d)\n",
                                      slapi_entry_get_dn_const(e), temp_id);
                        goto bail;
                    }
                }
            } else {
                char *normdn = NULL;
                struct backdn *bdn = dncache_find_id(&inst->inst_dncache, temp_id);
                if (bdn) {
                    /* don't free dn */
                    normdn = (char *)slapi_sdn_get_dn(bdn->dn_sdn);
                    CACHE_RETURN(&inst->inst_dncache, &bdn);
                } else {
                    Slapi_DN *sdn = NULL;
                    rc = entryrdn_lookup_dn(be, rdn, temp_id, &normdn, NULL, NULL);
                    if (rc) {
                        /* We cannot use the entryrdn index;
                         * Compose dn from the entries in id2entry */
                        Slapi_RDN psrdn = {0};
                        char *pid_str = NULL;
                        char *pdn = NULL;

                        slapi_log_err(SLAPI_LOG_TRACE,
                                      "dbmdb_index_producer", "entryrdn is not available; "
                                                        "composing dn (rdn: %s, ID: %d)\n",
                                      rdn, temp_id);
                        rc = get_value_from_string(entry_str, LDBM_PARENTID_STR, &pid_str);
                        if (rc) {
                            rc = 0; /* assume this is a suffix */
                        } else {
                            ID pid = (ID)strtol(pid_str, (char **)NULL, 10);
                            slapi_ch_free_string(&pid_str);
                            /* if pid is larger than the current pid temp_id,
                             * the parent entry hasn't */
                            rc = dbmdb_import_get_and_add_parent_rdns(info, inst, &db, dbc.txn,
                                                                pid, &id, &psrdn, &curr_entry);
                            if (rc) {
                                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_index_producer",
                                              "Failed to compose dn for (rdn: %s, ID: %d)\n",
                                              rdn, temp_id);
                                slapi_ch_free_string(&rdn);
                                slapi_rdn_done(&psrdn);
                                continue;
                            }
                            /* Generate DN string from Slapi_RDN */
                            rc = slapi_rdn_get_dn(&psrdn, &pdn);
                            slapi_rdn_done(&psrdn);
                            if (rc) {
                                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_index_producer",
                                              "Failed to compose dn for (rdn: %s, ID: %d) from Slapi_RDN\n",
                                              rdn, temp_id);
                                slapi_ch_free_string(&rdn);
                                continue;
                            }
                        }
                        normdn = slapi_ch_smprintf("%s%s%s",
                                                   rdn, pdn ? "," : "", pdn ? pdn : "");
                        slapi_ch_free_string(&pdn);
                    }
                    /* dn is not dup'ed in slapi_sdn_new_dn_byref.
                     * It's set to bdn and put in the dn cache. */
                    sdn = slapi_sdn_new_normdn_byval((const char *)normdn);
                    bdn = backdn_init(sdn, temp_id, 0);
                    CACHE_ADD(&inst->inst_dncache, bdn, NULL);
                    CACHE_RETURN(&inst->inst_dncache, &bdn);
                    slapi_log_err(SLAPI_LOG_CACHE, "dbmdb_index_producer - ",
                                  "entryrdn_lookup_dn returned: %s, "
                                  "and set to dn cache\n",
                                  normdn);
                }
                e = slapi_str2entry_ext(normdn, NULL, entry_str, SLAPI_STR2ENTRY_NO_ENTRYDN);
                slapi_ch_free_string(&rdn);
                slapi_ch_free_string(&normdn);
            }
        } else {
            e = slapi_str2entry(entry_str, 0);
            if (NULL == e) {
                if (job->task) {
                    slapi_task_log_notice(job->task,
                                          "%s: WARNING: skipping badly formatted entry (id %lu)",
                                          inst->inst_name, (u_long)temp_id);
                }
                slapi_log_err(SLAPI_LOG_WARNING,
                              "dbmdb_index_producer", "%s: Skipping badly formatted entry (id %lu)\n",
                              inst->inst_name, (u_long)temp_id);
                if (data.mv_data!=entry_str) {
                    slapi_ch_free_string(&entry_str);
                }
                continue;
            }
        }
         if (data.mv_data!=entry_str) {
             slapi_ch_free_string(&entry_str);
         }

        rc = dbmdb_index_set_entry_to_fifo(info, e, temp_id, &id, curr_entry);
        if (rc) {
            goto bail;
        }
        if (job->flags & FLAG_ABORT) {
            goto bail;
        }
        if (info->command == STOP) {
            finished = 1;
        }
    }

    if (job->flags & FLAG_DN2RDN) {
        /* remove id2entry, then rename tmp to id2entry */
         if(dbmdb_import_write_push(job,entrydn_wqslot,IMPORT_WRITE_ACTION_CLOSE,NULL,NULL)){
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_index_producer",
                          "Failed to close %s slot in writer thread.\n", id2entry);
            goto bail;
        }
    }

    dbmdb_close_cursor(&dbc, 1 /* abortthetxn */);
    slapi_ch_free_string(&id2entry);
    info->state = FINISHED;
    return;

bail:
    dbmdb_close_cursor(&dbc, 1 /* Abort txn */);
    slapi_ch_free_string(&id2entry);
    info->state = ABORTED;
    if (job->flags & FLAG_DN2RDN) {
        dbmdb_import_write_push(job, entrydn_wqslot, IMPORT_WRITE_ACTION_CLOSE, NULL, NULL);
    }
}

struct upgradedn_attr
{
    char *ud_type;
    char *ud_value;
    struct upgradedn_attr *ud_next;
    int ud_flags;
#define OLD_DN_NORMALIZE 0x1
};

static void
dbmdb_upgradedn_free_list(struct upgradedn_attr **ud_list)
{
    struct upgradedn_attr *ptr = *ud_list;

    while (ptr) {
        struct upgradedn_attr *next = ptr->ud_next;
        slapi_ch_free_string(&ptr->ud_type);
        slapi_ch_free_string(&ptr->ud_value);
        slapi_ch_free((void **)&ptr);
        ptr = next;
    }
    *ud_list = NULL;
    return;
}

static void
dbmdb_upgradedn_add_to_list(struct upgradedn_attr **ud_list,
                      char *type,
                      char *value,
                      int flag)
{
    struct upgradedn_attr *elem =
        (struct upgradedn_attr *)slapi_ch_malloc(sizeof(struct upgradedn_attr));
    elem->ud_type = type;
    elem->ud_value = value;
    elem->ud_flags = flag;
    elem->ud_next = *ud_list;
    *ud_list = elem;
    return;
}

/*
 * Return value: count of max consecutive spaces
 */
static int
dbmdb_has_spaces(const char *str)
{
    char *p = (char *)str;
    char *np;
    char *endp = p + strlen(str);
    int wcnt;
    int maxwcnt = 0;
next:
    if ((np = strchr(p, ' ')) || (np = strchr(p, '\t'))) {
        wcnt = 0;
        while ((np < endp) && isspace(*np)) {
            wcnt++;
            np++;
        }
        if (maxwcnt < wcnt) {
            maxwcnt = wcnt;
        }
        p = np;
        goto next;
    } else {
        goto bail;
    }
bail:
    return maxwcnt;
}

static int
dbmdb_add_IDs_to_IDarray(ID ***dn_norm_sp_conflict, int *max, int i, char *strids)
{
    char *p, *next, *start;
    ID my_id;
    ID **conflict;
    ID *idp;
    ID *endp;
    size_t len;

    if ((NULL == dn_norm_sp_conflict) || (NULL == max) || (0 == *max)) {
        return 1;
    }
    p = PL_strchr(strids, ':');
    if (NULL == p) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_add_IDs_to_IDarray",
                      "Format error: no ':' in %s\n", strids);
        return 1;
    }
    *p = '\0';
    my_id = (ID)strtol(strids, (char **)NULL, 10);
    if (!my_id) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_add_IDs_to_IDarray",
                      "Invalid ID in %s\n", strids);
        return 1;
    }

    if (NULL == *dn_norm_sp_conflict) {
        *dn_norm_sp_conflict = (ID **)slapi_ch_malloc(sizeof(ID *) * *max);
    } else if (*max == i + 1) {
        *max *= 2;
        *dn_norm_sp_conflict = (ID **)slapi_ch_realloc((char *)*dn_norm_sp_conflict,
                                                       sizeof(ID *) * *max);
    }
    conflict = *dn_norm_sp_conflict;

    len = strlen(strids);
    while (isspace(*(++p)))
        ;
    start = p;
    /* since the format is "ID ID ID ...", max count is len/2 + 1 and +1 for my_id */
    conflict[i] = (ID *)slapi_ch_calloc((len / 2 + 2), sizeof(ID));
    idp = conflict[i];
    endp = idp + len / 2 + 2;
    *idp++ = my_id;
    for (p = strtok_r(start, " \n", &next); p && (idp < endp);
         p = strtok_r(NULL, " \n", &next), idp++) {
        *idp = (ID)strtol(p, (char **)NULL, 10);
    }
    if (idp < endp) {
        *idp = 0;
    } else if (idp == endp) {
        conflict[i] = (ID *)slapi_ch_realloc((char *)conflict[i],
                                             (len / 2 + 3) * sizeof(ID));
        idp = conflict[i];
        *(idp + len / 2 + 2) = 0;
    }
    conflict[i + 1] = NULL;
    return 0;
}

static void
dbmdb_free_IDarray(ID ***dn_norm_sp_conflict)
{
    int i;
    if ((NULL == dn_norm_sp_conflict) || (NULL == *dn_norm_sp_conflict)) {
        return;
    }
    for (i = 0; (*dn_norm_sp_conflict)[i]; i++) {
        slapi_ch_free((void **)&(*dn_norm_sp_conflict)[i]);
    }
    slapi_ch_free((void **)dn_norm_sp_conflict);
}

/*
 * dn_norm_sp_conflicts is a double array of IDs.
 * Format:
 * primary_ID0: conflict_ID ...
 * primary_ID1: conflict_ID ...
 * ...
 *
 * dbmdb_is_conflict_ID looks for ghe given id in hte conflict_ID lists
 * If found, its primary_ID is returned.
 * Otherwise, 0 is returned.
 */
static ID
dbmdb_is_conflict_ID(ID **dn_norm_sp_conflicts, int max, ID id)
{
    int i;
    ID *idp;
    for (i = 0; i < max; i++) {
        for (idp = dn_norm_sp_conflicts[i]; idp && *idp; idp++) {
            if (*idp == id) {
                return *dn_norm_sp_conflicts[i];
            }
        }
    }
    return 0;
}

/*
 * Producer thread for upgrading dn format
 * FLAG_UPGRADEDNFORMAT | FLAG_DRYRUN -- check the necessity of dn upgrade
 * FLAG_UPGRADEDNFORMAT -- execute dn upgrade
 *
 * Read id2entry,
 * Check the DN syntax attributes if it contains '\' or not AND
 * Check the RDNs of the attributes if the value is surrounded by
 * double-quotes or not.
 * If both are false, skip the entry and go to next
 * If either is true, create an entry which contains a correctly normalized
 *     DN attribute values in e_attr list and the original entrydn in the
 *     deleted attribute list e_aux_attrs.
 *
 * If FLAG_UPGRADEDNFORMAT is set, worker_threads for indexing DN syntax
 * attributes (+ cn & ou) are brought up.  Foreman thread updates entrydn
 * or entryrd index as well as the entry itself in the id2entry.db#.
 *
 * Note: QUIT state for info->state is introduced for DRYRUN mode to
 *       distinguish the intentional QUIT (found the dn upgrade candidate)
 *       from ABORTED (aborted or error) and FINISHED (scan all the entries
 *       and found no candidate to upgrade)
 */
/*
 * FLAG_UPGRADEDNFORMAT: need to take care space reduction
 * dn: cn=TEST   USER0 --> dn: cn=TEST USER0
 * If multiple DNs are found having different count of spaces,
 * remove the second and later with exporting them into ldif file.
 * In the dry run, it retrieves all the entries having spaces in dn and
 * store the DN and OID in a file.  Mark, conflicts if any.
 * In the real run, store only no conflict entries and first conflicted one.
 * The rest are stored in an ldif file.
 *
 * Cases:
 *   FLAG_DRYRUN | FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1
 *     ==> 1 & 2-1
 *   FLAG_DRYRUN | FLAG_UPGRADEDNFORMAT_V1
 *     ==> 2-1
 *   FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1
 *     ==> 1 & 2-2,3
 *   FLAG_UPGRADEDNFORMAT_V1
 *     ==> 2-2,3
 *
 *       1) dn normalization
 *       2) space handling
 *       2-1) upgradedn dry-run: output into a file (in the instance db dir?)
 *            format:
 *            <original dn>:OID
 *       2-2) upgrade script sorts the file; retrieve duplicated DNs
 *            <dn>:OID0
 *            <dupped dn>:OID1
 *            <dupped dn>:OID2
 *
 *            format:
 *            OID0:OID1 OID2 ...
 *       2-3) upgradedn: compare the OID with the OID1,OID2,
 *            if match, rename the rdn value to "value <entryid>".
 */
void
dbmdb_upgradedn_producer(void *param)
{
    ImportWorkerInfo *info = (ImportWorkerInfo *)param;
    ImportJob *job = info->job;
    ID id = job->first_ID;
    Slapi_Entry *e = NULL;
    struct backentry *ep = NULL, *old_ep = NULL;
    ldbm_instance *inst = job->inst;
    struct ldbminfo* li = (struct ldbminfo*)inst->inst_be->be_database->plg_private;
    dbmdb_ctx_t *ctx = MDB_CONFIG(li);
    dbmdb_cursor_t dbc={0};
    PRIntervalTime sleeptime;
    int finished = 0;
    int idx;
    int rc = 0;
    Slapi_Attr *a = NULL;
    Slapi_DN *sdn = NULL;
    char *workdn = NULL;
    struct upgradedn_attr *ud_list = NULL;
    char **ud_vals = NULL;
    char **ud_valp = NULL;
    struct upgradedn_attr *ud_ptr = NULL;
    Slapi_Attr *ud_attr = NULL;
    char *ecopy = NULL;
    char *normdn = NULL;
    char *rdn = NULL;       /* original rdn */
    int is_dryrun = 0;      /* FLAG_DRYRUN */
    int chk_dn_norm = 0;    /* FLAG_UPGRADEDNFORMAT */
    int chk_dn_norm_sp = 0; /* FLAG_UPGRADEDNFORMAT_V1 */
    ID **dn_norm_sp_conflicts = NULL;
    int do_dn_norm = 0;    /* do dn_norm */
    int do_dn_norm_sp = 0; /* do dn_norm_sp */
    int rdn_dbmdb_has_spaces = 0;
    int info_state = 0; /* state to add to info->state (for dryrun only) */
    int skipit = 0;
    ID pid;
    struct backdn *bdn = NULL;

    /* vars for Berkeley MDB_dbi*/
    dbmdb_dbi_t *db = NULL;
    MDB_val key = {0};
    MDB_val data = {0};
    int db_rval = -1;
    backend *be = inst->inst_be;
    int isfirst = 1;
    int curr_entry = 0;
    size_t newesize = 0;
     char *entry_str = NULL;
     uint entry_len = 0;

    PR_ASSERT(info != NULL);
    PR_ASSERT(inst != NULL);
    PR_ASSERT(be != NULL);

    if (job->flags & FLAG_ABORT) {
        goto error;
    }

    is_dryrun = job->flags & FLAG_DRYRUN;
    chk_dn_norm = job->flags & FLAG_UPGRADEDNFORMAT;
    chk_dn_norm_sp = job->flags & FLAG_UPGRADEDNFORMAT_V1;

    if (!chk_dn_norm && !chk_dn_norm_sp) {
        /* Nothing to do... */
        slapi_log_err(SLAPI_LOG_INFO, "dbmdb_upgradedn_producer",
                      "UpgradeDnFormat is not required.\n");
        info->state = FINISHED;
        goto done;
    }

    sleeptime = PR_MillisecondsToInterval(import_sleep_time);

    /* pause until we're told to run */
    while ((info->command == PAUSE) && !(job->flags & FLAG_ABORT)) {
        info->state = WAITING;
        DS_Sleep(sleeptime);
    }
    info->state = RUNNING;

    /* open id2entry with dedicated db env and db handler */
     if(dbmdb_get_aux_id2entry(be, &db, NULL) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer",
                      "Could not open id2entry\n");
        goto error;
    }

    /* get a cursor to we can walk over the table */
     db_rval = dbmdb_open_cursor(&dbc, ctx, db, db->state.flags|MDB_RDONLY);
     if (db_rval) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer",
                 "Failed to get %s cursor for reindexing\n", db->dbname);
         dblayer_release_id2entry(be,&db);
        goto error;
    }

    /* we loop around reading the input files and processing each entry
     * as we read it.
     */
    finished = 0;
    while (!finished) {
        ID temp_id;
        int dn_in_cache;

        if (job->flags & FLAG_ABORT) {
            goto error;
        }
        while ((info->command == PAUSE) && !(job->flags & FLAG_ABORT)) {
            info->state = WAITING;
            DS_Sleep(sleeptime);
        }
        info->state = RUNNING;

        if (isfirst) {
            db_rval = MDB_CURSOR_GET(dbc.cur, &key, &data, MDB_FIRST);
            isfirst = 0;
        } else {
             db_rval = MDB_CURSOR_GET(dbc.cur, &key, &data, MDB_NEXT);
        }

        if (0 != db_rval) {
             if( MDB_NOTFOUND == db_rval) {
                slapi_log_err(SLAPI_LOG_INFO, "dbmdb_upgradedn_producer",
                              "%s: Finished reading database\n", inst->inst_name);
                if (job->task) {
                    slapi_task_log_notice(job->task,
                                          "%s: Finished reading database", inst->inst_name);
                }
            } else {
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer", "%s: Failed to read database, "
                                                                   "errno=%d (%s)\n",
                              inst->inst_name, db_rval,
                              dblayer_strerror(db_rval));
                if (job->task) {
                    slapi_task_log_notice(job->task,
                                          "%s: Failed to read database, err %d (%s)",
                                          inst->inst_name, db_rval,
                                          dblayer_strerror(db_rval));
                }
            }
            finished = 1;
            break; /* error or done */
        }
        curr_entry++;
        temp_id = id_stored_to_internal(key.mv_data);

        /* call post-entry plugin */
         entry_str = data.mv_data;
         entry_len = data.mv_size;
         plugin_call_entryfetch_plugins(&entry_str, &entry_len);

        slapi_ch_free_string(&ecopy);
        ecopy = (char *)slapi_ch_malloc(entry_len + 1);
        memcpy(ecopy, entry_str, entry_len);
        ecopy[entry_len] = '\0';
        normdn = NULL;
        do_dn_norm = 0;
        do_dn_norm_sp = 0;
        rdn_dbmdb_has_spaces = 0;
        dn_in_cache = 0;
        if (entryrdn_get_switch()) {

            /* original rdn is allocated in get_value_from_string */
            rc = get_value_from_string(entry_str, "rdn", &rdn);
            if (rc) {
                /* data.dptr may not include rdn: ..., try "dn: ..." */
                e = slapi_str2entry(entry_str, SLAPI_STR2ENTRY_USE_OBSOLETE_DNFORMAT);
            } else {
                bdn = dncache_find_id(&inst->inst_dncache, temp_id);
                if (bdn) {
                    /* don't free normdn */
                    normdn = (char *)slapi_sdn_get_dn(bdn->dn_sdn);
                    CACHE_RETURN(&inst->inst_dncache, &bdn);
                    dn_in_cache = 1;
                } else {
                    /* free normdn */
                    rc = entryrdn_lookup_dn(be, rdn, temp_id,
                                            (char **)&normdn, NULL, NULL);
                    if (rc) {
                        /* We cannot use the entryrdn index;
                         * Compose dn from the entries in id2entry */
                        Slapi_RDN psrdn = {0};
                        char *pid_str = NULL;
                        char *pdn = NULL;

                        slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_upgradedn_producer",
                                      "entryrdn is not available; composing dn (rdn: %s, ID: %d)\n",
                                      rdn, temp_id);
                        rc = get_value_from_string(entry_str, LDBM_PARENTID_STR, &pid_str);
                        if (rc) {
                            rc = 0; /* assume this is a suffix */
                        } else {
                            pid = (ID)strtol(pid_str, (char **)NULL, 10);
                            slapi_ch_free_string(&pid_str);
                            /* if pid is larger than the current pid temp_id,
                             * the parent entry hasn't */
                            rc = dbmdb_import_get_and_add_parent_rdns(info, inst, &db,dbc.txn,
                                                                pid, &id, &psrdn, &curr_entry);
                            if (rc) {
                                slapi_log_err(SLAPI_LOG_ERR,
                                              "upgradedn: Failed to compose dn for "
                                              "(rdn: %s, ID: %d)\n",
                                              rdn, temp_id);
                                slapi_ch_free_string(&rdn);
                                slapi_rdn_done(&psrdn);
                                continue;
                            }
                            /* Generate DN string from Slapi_RDN */
                            rc = slapi_rdn_get_dn(&psrdn, &pdn);
                            slapi_rdn_done(&psrdn);
                            if (rc) {
                                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer",
                                              "Failed to compose dn for (rdn: %s, ID: %d) from Slapi_RDN\n",
                                              rdn, temp_id);
                                slapi_ch_free_string(&rdn);
                                continue;
                            }
                        }
                        /* free normdn */
                        normdn = slapi_ch_smprintf("%s%s%s",
                                                   rdn, pdn ? "," : "", pdn ? pdn : "");
                        slapi_ch_free_string(&pdn);
                    }
                    if (is_dryrun) {
                        /* if not dryrun, we may change the DN, In such case ,
                         * we need to put the new value to cache.*/
                        /* dn is dup'ed in slapi_sdn_new_dn_byval.
                         * It's set to bdn and put in the dn cache. */
                        /* normdn is allocated in this scope.
                         * Thus, we can just passin. */
                        sdn = slapi_sdn_new_normdn_passin(normdn);
                        bdn = backdn_init(sdn, temp_id, 0);
                        CACHE_ADD(&inst->inst_dncache, bdn, NULL);
                        CACHE_RETURN(&inst->inst_dncache, &bdn);
                        /* don't free this normdn  */
                        normdn = (char *)slapi_sdn_get_dn(sdn);
                        slapi_log_err(SLAPI_LOG_CACHE, "dbmdb_upgradedn_producer",
                                      "entryrdn_lookup_dn returned: %s, "
                                      "and set to dn cache\n",
                                      normdn);
                        dn_in_cache = 1;
                    }
                }
                e = slapi_str2entry_ext(normdn, NULL, entry_str,
                                        SLAPI_STR2ENTRY_USE_OBSOLETE_DNFORMAT);
                slapi_ch_free_string(&rdn);
            }
        } else {
            e = slapi_str2entry(entry_str, SLAPI_STR2ENTRY_USE_OBSOLETE_DNFORMAT);
            rdn = slapi_ch_strdup(slapi_entry_get_rdn_const(e));
            if (NULL == rdn) {
                Slapi_RDN srdn;
                slapi_rdn_init_dn(&srdn, slapi_entry_get_dn_const(e));
                rdn = (char *)slapi_rdn_get_rdn(&srdn); /* rdn is allocated in
                                                         * slapi_rdn_init_dn */
            }
        }
        if (NULL == e) {
            if (job->task) {
                slapi_task_log_notice(job->task,
                                      "%s: WARNING: skipping badly formatted entry (id %lu)",
                                      inst->inst_name, (u_long)temp_id);
            }
            slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_upgradedn_producer",
                          "%s: Skipping badly formatted entry (id %lu)\n",
                          inst->inst_name, (u_long)temp_id);
            slapi_ch_free_string(&rdn);
            continue;
        }

        /* From here, e != NULL */
        /*
         * treat dn specially since the entry was generated with the flag
         * SLAPI_STR2ENTRY_USE_OBSOLETE_DNFORMAT
         * -- normalize it with the new format
         */
        if (!normdn) {
            /* No rdn in id2entry or entrydn */
            normdn = (char *)slapi_sdn_get_dn(&(e->e_sdn));
        }

        /*
         * If dryrun && check_dn_norm_sp,
         * check if rdn's containing multi spaces exist or not.
         * If any, output the DN:ID into a file INST_dn_norm_sp.txt in the
         * ldifdir. We have to continue checking all the entries.
         */
        if (chk_dn_norm_sp) {
            char *dn_id;
            char *path; /* <ldifdir>/<inst>_dn_norm_sp.txt is used for
                           the temp work file */
            /* open "path" once, and set FILE* to upgradefd */
            if (NULL == job->upgradefd) {
                char *ldifdir = config_get_ldifdir();
                if (ldifdir) {
                    path = slapi_ch_smprintf("%s/%s_dn_norm_sp.txt",
                                             ldifdir, inst->inst_name);
                    slapi_ch_free_string(&ldifdir);
                } else {
                    path = slapi_ch_smprintf("/var/tmp/%s_dn_norm_sp.txt",
                                             inst->inst_name);
                }
                if (is_dryrun) {
                    job->upgradefd = fopen(path, "w");
                    if (NULL == job->upgradefd) {
                        if (job->task) {
                            slapi_task_log_notice(job->task,
                                                  "%s: No DNs to fix.\n", inst->inst_name);
                        }
                        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer",
                                      "%s: No DNs to fix.\n", inst->inst_name);
                        slapi_ch_free_string(&path);
                        goto bail;
                    }
                } else {
                    job->upgradefd = fopen(path, "r");
                    if (NULL == job->upgradefd) {
                        if (job->task) {
                            slapi_task_log_notice(job->task,
                                                  "%s: Error: failed to open a file \"%s\"",
                                                  inst->inst_name, path);
                        }
                        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer",
                                      "%s: Error: failed to open a file \"%s\"\n",
                                      inst->inst_name, path);
                        slapi_ch_free_string(&path);
                        goto error;
                    }
                }
            }
            slapi_ch_free_string(&path);
            if (is_dryrun) {
                rdn_dbmdb_has_spaces = dbmdb_has_spaces(rdn);
                if (rdn_dbmdb_has_spaces > 0) {
                    dn_id = slapi_ch_smprintf("%s:%u\n",
                                              slapi_entry_get_dn_const(e), temp_id);
                    if (EOF == fputs(dn_id, job->upgradefd)) {
                        if (job->task) {
                            slapi_task_log_notice(job->task,
                                                  "%s: Error: failed to write a line \"%s\"",
                                                  inst->inst_name, dn_id);
                        }
                        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer",
                                      "%s: Error: failed to write a line \"%s\"",
                                      inst->inst_name, dn_id);
                        slapi_ch_free_string(&dn_id);
                        goto error;
                    }
                    slapi_ch_free_string(&dn_id);
                    if (rdn_dbmdb_has_spaces > 1) {
                        /* If an rdn containing multi spaces exists,
                         * let's check the conflict. */
                        do_dn_norm_sp = 1;
                    }
                }
            } else { /* !is_dryrun */
                /* check the oid and parentid. */
                /* Set conflict list once, and refer it laster. */
                static int my_idx = 0;
                ID alt_id;
                if (NULL == dn_norm_sp_conflicts) {
                    char buf[BUFSIZ];
                    int my_max = 8;
                    while (fgets(buf, sizeof(buf) - 1, job->upgradefd)) {
                        /* search "OID0: OID1 OID2 ... */
                        if (!isdigit(*buf) || (NULL == PL_strchr(buf, ':'))) {
                            continue;
                        }
                        if (dbmdb_add_IDs_to_IDarray(&dn_norm_sp_conflicts, &my_max,
                                               my_idx, buf)) {
                            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer",
                                          "Failed to set IDs %s to conflict list\n", buf);
                            goto error;
                        }
                        my_idx++;
                    }
                }
                alt_id = dbmdb_is_conflict_ID(dn_norm_sp_conflicts, my_idx, temp_id);
                if (alt_id) {
                    if (alt_id != temp_id) {
                        char *newrdn = slapi_create_dn_string("%s %u", rdn, temp_id);
                        char *parentdn = slapi_dn_parent(normdn);
                        /* This entry is a conflict of alt_id */
                        slapi_log_err(SLAPI_LOG_NOTICE, "dbmdb_upgradedn_producer",
                                      "Entry %s (%u) is a conflict of (%u)\n",
                                      normdn, temp_id, alt_id);
                        slapi_log_err(SLAPI_LOG_NOTICE, "dbmdb_upgradedn_producer",
                                      "Renaming \"%s\" to \"%s\"\n", rdn, newrdn);
                        if (!dn_in_cache) {
                            /* If not in dn cache, normdn needs to be freed. */
                            slapi_ch_free_string(&normdn);
                        }
                        normdn = slapi_ch_smprintf("%s,%s", newrdn, parentdn);
                        slapi_ch_free_string(&newrdn);
                        slapi_ch_free_string(&parentdn);
                        /* Reset DN and RDN in the entry */
                        slapi_sdn_done(&(e->e_sdn));
                        slapi_sdn_init_normdn_byval(&(e->e_sdn), normdn);
                    }
                    info_state |= DN_NORM_SP;
                    dbmdb_upgradedn_add_to_list(&ud_list,
                                          slapi_ch_strdup(LDBM_ENTRYRDN_STR),
                                          slapi_ch_strdup(rdn), 0);
                    rc = slapi_entry_add_rdn_values(e);
                    if (rc) {
                        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer",
                                      "%s: Failed to add rdn values to an entry: %s (id %lu)\n",
                                      inst->inst_name, normdn, (u_long)temp_id);
                        goto error;
                    }
                } /* !alt_id */
            }     /* !is_dryrun */
        }         /* if (chk_dn_norm_sp) */

        /* dn is dup'ed in slapi_sdn_new_dn_byval.
         * It's set to bdn and put in the dn cache. */
        /* Waited to put normdn into dncache until it could be modified in
         * chk_dn_norm_sp. */
        if (!dn_in_cache) {
            sdn = slapi_sdn_new_normdn_passin(normdn);
            bdn = backdn_init(sdn, temp_id, 0);
            CACHE_ADD(&inst->inst_dncache, bdn, NULL);
            CACHE_RETURN(&inst->inst_dncache, &bdn);
            slapi_log_err(SLAPI_LOG_CACHE, "dbmdb_upgradedn_producer",
                          "set dn %s to dn cache\n", normdn);
        }
        /* Check DN syntax attr values if it contains '\\' or not */
        /* Start from the rdn */
        if (chk_dn_norm) {
            char *endrdn = NULL;
            char *rdnp = NULL;
            endrdn = rdn + strlen(rdn) - 1;

            rdnp = PL_strchr(rdn, '=');
            if (NULL == rdnp) {
                slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_upgradedn_producer",
                              "%s: Skipping an entry with corrupted RDN \"%s\" (id %lu)\n",
                              inst->inst_name, rdn, (u_long)temp_id);
                slapi_entry_free(e);
                e = NULL;
                continue;
            }
            /* rdn contains '\\'.  We have to update the value */
            if (PL_strchr(rdnp, '\\')) {
                do_dn_norm = 1;
            } else {
                while ((++rdnp <= endrdn) && (*rdnp == ' ' || *rdnp == '\t'))
                    ;
                /* DN contains an RDN <type>="<value>" ? */
                if ((rdnp != endrdn) && ('"' == *rdnp) && ('"' == *endrdn)) {
                    do_dn_norm = 1;
                }
            }
            if (do_dn_norm) {
                dbmdb_upgradedn_add_to_list(&ud_list,
                                      slapi_ch_strdup(LDBM_ENTRYRDN_STR),
                                      slapi_ch_strdup(rdn), 0);
                slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_upgradedn_producer",
                              "%s: Found upgradedn candidate: (id %lu)\n",
                              inst->inst_name, (u_long)temp_id);
                /*
                 * In case rdn is type="<RDN>" or type=<\D\N>,
                 * add the rdn value if it's not there.
                 */
                rc = slapi_entry_add_rdn_values(e);
                if (rc) {
                    slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer",
                                  "%s: Failed to add rdn values to an entry: %s (id %lu)\n",
                                  inst->inst_name, normdn, (u_long)temp_id);
                    slapi_entry_free(e);
                    e = NULL;
                    continue;
                }
            }
            /* checking the DN sintax values in the attribute list */
            for (a = e->e_attrs; a; a = a->a_next) {
                if (!slapi_attr_is_dn_syntax_attr(a)) {
                    continue; /* not a syntax dn attr */
                }

                /* dn syntax attr */
                rc = get_values_from_string((const char *)ecopy,
                                            a->a_type, &ud_vals);
                if (rc || (NULL == ud_vals)) {
                    continue; /* empty; ignore it */
                }

                for (ud_valp = ud_vals; ud_valp && *ud_valp; ud_valp++) {
                    char **rdns = NULL;
                    char **rdnsp = NULL;
                    char *valueptr = NULL;
                    char *endvalue = NULL;
                    int isentrydn = 0;

                    /* Also check RDN contains double quoted values */
                    if (strcasecmp(a->a_type, "entrydn")) {
                        /* except entrydn */
                        workdn = slapi_ch_strdup(*ud_valp);
                        isentrydn = 0;
                    } else {
                        /* entrydn: Get Slapi DN */
                        sdn = slapi_entry_get_sdn(e);
                        workdn = slapi_ch_strdup(slapi_sdn_get_dn(sdn));
                        isentrydn = 1;
                    }
                    rdns = slapi_ldap_explode_dn(workdn, 0);
                    skipit = 0;
                    for (rdnsp = rdns; rdnsp && *rdnsp; rdnsp++) {
                        valueptr = PL_strchr(*rdnsp, '=');
                        if (NULL == valueptr) {
                            skipit = 1;
                            break;
                        }
                        endvalue = *rdnsp + strlen(*rdnsp) - 1;
                        while ((++valueptr <= endvalue) &&
                               ((' ' == *valueptr) || ('\t' == *valueptr)))
                            ;
                        if (0 == strlen(valueptr)) {
                            skipit = 1;
                            break;
                        }
                        /* DN syntax value contains an RDN <type>="<value>" or
                         * '\\' in the value ?
                         * If yes, let's upgrade the dn format. */
                        if ((('"' == *valueptr) && ('"' == *endvalue)) ||
                            PL_strchr(valueptr, '\\')) {
                            do_dn_norm = 1;
                            dbmdb_upgradedn_add_to_list(&ud_list,
                                                  slapi_ch_strdup(a->a_type),
                                                  slapi_ch_strdup(*ud_valp),
                                                  isentrydn ? 0 : OLD_DN_NORMALIZE);
                            slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_upgradedn_producer",
                                          "%s: Found upgradedn candidate: %s (id %lu)\n",
                                          inst->inst_name, valueptr, (u_long)temp_id);
                            if (!entryrdn_get_switch() && isentrydn) {
                                /* entrydn format */
                                /*
                                 * In case entrydn is type="<DN>",<REST> or
                                 *                    type=<\D\N>,<REST>,
                                 * add the rdn value if it's not there.
                                 */
                                rc = slapi_entry_add_rdn_values(e);
                                if (rc) {
                                    slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer",
                                                  "%s: Failed to add rdn values to an entry: %s (id %lu)\n",
                                                  inst->inst_name, normdn, (u_long)temp_id);
                                    slapi_entry_free(e);
                                    e = NULL;
                                    continue;
                                }
                            }
                            break;
                        }
                        /*
                         * else if (the rdn contains multiple spaces)?
                         * if yes, they are reduced to one.
                         * SET HAVE_MULTI_SPACES???
                         */
                    } /* for (rdnsp = rdns; rdnsp && *rdnsp; rdnsp++) */
                    if (rdns) {
                        slapi_ldap_value_free(rdns);
                    } else {
                        skipit = 1;
                    }
                    if (skipit) {
                        break;
                    }
                    slapi_ch_free_string(&workdn);
                } /* for (ud_valp = ud_vals; ud_valp && *ud_valp; ud_valp++) */
                charray_free(ud_vals);
                ud_vals = NULL;
                if (skipit) {
                    slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_upgradedn_producer",
                                  "%s: Skipping an entry with a corrupted dn (syntax value): %s (id %lu)\n",
                                  inst->inst_name, workdn ? workdn : "unknown", (u_long)temp_id);
                    slapi_ch_free_string(&workdn);
                    dbmdb_upgradedn_free_list(&ud_list);
                    break;
                }
            } /* for (a = e->e_attrs; a; a = a->a_next)  */
            if (skipit) {
                dbmdb_upgradedn_free_list(&ud_list);
                slapi_entry_free(e);
                e = NULL;
                continue;
            }
        } /* end of if (chk_do_norm) */
        slapi_ch_free_string(&rdn);

        if (is_dryrun) {
            if (do_dn_norm) {
                info_state |= DN_NORM;
                /*
                 * If dryrun AND (found we need to do dn norm) AND
                 * (no need to check spaces),
                 * then we can quit here to return.
                 */
                if (!chk_dn_norm_sp) {
                    finished = 0; /* make it sure ... */
                    dbmdb_upgradedn_free_list(&ud_list);
                    slapi_entry_free(e);
                    e = NULL;
                    /* found upgrade dn candidates */
                    goto bail;
                }
            }
            if (do_dn_norm_sp) {
                info_state |= DN_NORM_SP;
            }
            /* We don't have to update dn syntax values. */
            dbmdb_upgradedn_free_list(&ud_list);
            slapi_entry_free(e);
            e = NULL;
            continue;
        }

        skipit = 0;
        for (ud_ptr = ud_list; ud_ptr; ud_ptr = ud_ptr->ud_next) {
            Slapi_Value *value = NULL;
            /* Move the current value to e_aux_attrs. */
            /* entryrdn is special since it does not have an attribute in db */
            if (0 == strcmp(ud_ptr->ud_type, LDBM_ENTRYRDN_STR)) {
                /* entrydn contains half normalized value in id2entry,
                   thus we have to replace it in id2entry.
                   The other DN syntax attribute values store
                   the originals.  They are taken care by the normalizer.
                 */
                a = slapi_attr_new();
                slapi_attr_init(a, ud_ptr->ud_type);
                value = slapi_value_new_string(ud_ptr->ud_value);
                slapi_attr_add_value(a, value);
                slapi_value_free(&value);
                attrlist_add(&e->e_aux_attrs, a);
            } else { /* except "entryrdn" */
                ud_attr = attrlist_find(e->e_attrs, ud_ptr->ud_type);
                if (ud_attr) {
                    /* We have to normalize the orignal string to generate
                       the key in the index.
                     */
                    a = attrlist_find(e->e_aux_attrs, ud_ptr->ud_type);
                    if (!a) {
                        a = slapi_attr_new();
                        slapi_attr_init(a, ud_ptr->ud_type);
                    } else {
                        a = attrlist_remove(&e->e_aux_attrs,
                                            ud_ptr->ud_type);
                    }
                    slapi_dn_normalize_case_original(ud_ptr->ud_value);
                    value = slapi_value_new_string(ud_ptr->ud_value);
                    slapi_attr_add_value(a, value);
                    slapi_value_free(&value);
                    attrlist_add(&e->e_aux_attrs, a);
                }
            }
        }
        dbmdb_upgradedn_free_list(&ud_list);

        ep = dbmdb_import_make_backentry(e, temp_id);
        if (!ep) {
            slapi_entry_free(e);
            e = NULL;
            goto error;
        }

        /* Add the newly case -normalized dn to entrydn in the e_attrs list. */
        add_update_entrydn_operational_attributes(ep);

        if (job->flags & FLAG_ABORT)
            goto error;

        /* Now we have this new entry, all decoded
         * Next thing we need to do is:
         * (1) see if the appropriate fifo location contains an
         *     entry which had been processed by the indexers.
         *     If so, proceed.
         *     If not, spin waiting for it to become free.
         * (2) free the old entry and store the new one there.
         * (3) Update the job progress indicators so the indexers
         *     can use the new entry.
         */
        idx = id % job->fifo.size;
        old_ep = job->fifo.item[idx].entry;
        if (old_ep) {
            /* for the slot to be recycled, it needs to be already absorbed
             * by the foreman (id >= ready_EID), and all the workers need to
             * be finished with it (refcount = 0).
             */
            while (((old_ep->ep_refcnt > 0) ||
                    (old_ep->ep_id >= job->ready_EID)) &&
                   (info->command != ABORT) && !(job->flags & FLAG_ABORT)) {
                info->state = WAITING;
                DS_Sleep(sleeptime);
            }
            if (job->flags & FLAG_ABORT)
                goto error;

            info->state = RUNNING;
            PR_ASSERT(old_ep == job->fifo.item[idx].entry);
            job->fifo.item[idx].entry = NULL;
            if (job->fifo.c_bsize > job->fifo.item[idx].esize)
                job->fifo.c_bsize -= job->fifo.item[idx].esize;
            else
                job->fifo.c_bsize = 0;
            backentry_free(&old_ep);
        }

        newesize = (slapi_entry_size(ep->ep_entry) + sizeof(struct backentry));
        if (dbmdb_import_fifo_validate_capacity_or_expand(job, newesize) == 1) {
            import_log_notice(job, SLAPI_LOG_NOTICE, "dbmdb_upgradedn_producer", "Skipping entry \"%s\"",
                              slapi_entry_get_dn(e));
            import_log_notice(job, SLAPI_LOG_NOTICE, "dbmdb_upgradedn_producer",
                              "REASON: entry too large (%lu bytes) for "
                              "the buffer size (%lu bytes), and we were UNABLE to expand buffer.",
                              (long unsigned int)newesize, (long unsigned int)job->fifo.bsize);
            backentry_free(&ep);
            job->skipped++;
            continue;
        }
        /* Now check if fifo has enough space for the new entry */
        if ((job->fifo.c_bsize + newesize) > job->fifo.bsize) {
            dbmdb_import_wait_for_space_in_fifo(job, newesize);
        }

        /* We have enough space */
        job->fifo.item[idx].filename = ID2ENTRY LDBM_FILENAME_SUFFIX;
        job->fifo.item[idx].line = curr_entry;
        job->fifo.item[idx].entry = ep;
        job->fifo.item[idx].bad = 0;
        job->fifo.item[idx].esize = newesize;

        /* Add the entry size to total fifo size */
        job->fifo.c_bsize += ep->ep_entry ? job->fifo.item[idx].esize : 0;

        /* Update the job to show our progress */
        job->lead_ID = id;
        if ((id - info->first_ID) <= job->fifo.size) {
            job->trailing_ID = info->first_ID;
        } else {
            job->trailing_ID = id - job->fifo.size;
        }

        /* Update our progress meter too */
        info->last_ID_processed = id;
        id++;
        if (job->flags & FLAG_ABORT)
            goto error;
        if (info->command == STOP) {
            finished = 1;
        }
    }
bail:
    info->state = FINISHED | info_state;
    goto done;

error:
    info->state = ABORTED;

done:
    dbmdb_close_cursor(&dbc, 1 /* Abort txn */);
    dbmdb_free_IDarray(&dn_norm_sp_conflicts);
    slapi_ch_free_string(&ecopy);
 if(data.mv_data!=entry_str){
     slapi_ch_free_string(&entry_str);
 }
    slapi_ch_free_string(&rdn);
    if (job->upgradefd) {
        fclose(job->upgradefd);
    }
}

static void
dbmdb_import_wait_for_space_in_fifo(ImportJob *job, size_t new_esize)
{
    struct backentry *temp_ep = NULL;
    size_t i;
    int slot_found;
    PRIntervalTime sleeptime;

    sleeptime = PR_MillisecondsToInterval(import_sleep_time);

    /* Now check if fifo has enough space for the new entry */
    while ((job->fifo.c_bsize + new_esize) > job->fifo.bsize && !(job->flags & FLAG_ABORT)) {
        for (i = 0, slot_found = 0; i < job->fifo.size; i++) {
            temp_ep = job->fifo.item[i].entry;
            if (temp_ep) {
                if (temp_ep->ep_refcnt == 0 && temp_ep->ep_id <= job->ready_EID) {
                    job->fifo.item[i].entry = NULL;
                    if (job->fifo.c_bsize > job->fifo.item[i].esize) {
                        job->fifo.c_bsize -= job->fifo.item[i].esize;
                    } else {
                        job->fifo.c_bsize = 0;
                    }
                    backentry_free(&temp_ep);
                    slot_found = 1;
                }
            }
        }
        if (slot_found == 0)
            DS_Sleep(sleeptime);
    }
}

/* helper function for the foreman: */
static int
dbmdb_foreman_do_parentid(ImportJob *job, FifoItem *fi, struct attrinfo *parentid_ai)
{
    backend *be = job->inst->inst_be;
    Slapi_Value **svals = NULL;
    Slapi_Attr *attr = NULL;
    int idl_disposition = 0;
    int ret = 0;
    struct backentry *entry = fi->entry;

    if (job->flags & (FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1)) {
        /* Get the parentid attribute value from deleted attr list */
        Slapi_Value *value = NULL;
        Slapi_Attr *pid_to_del =
            attrlist_remove(&entry->ep_entry->e_aux_attrs, "parentid");
        if (pid_to_del) {
            /* Delete it. */
            ret = slapi_attr_first_value(pid_to_del, &value);
            if (ret < 0) {
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_foreman_do_parentid",
                                  "Error: retrieving parentid value (error %d)",
                                  ret);
            } else {
                const struct berval *bval =
                    slapi_value_get_berval((const Slapi_Value *)value);
                ret = index_addordel_string(be, LDBM_PARENTID_STR,
                                            bval->bv_val, entry->ep_id,
                                            BE_INDEX_DEL | BE_INDEX_EQUALITY | BE_INDEX_NORMALIZED,
                                            dbmdb_get_wctx(job, NULL, WCTX_PARENTID));
                if (ret) {
                    import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_foreman_do_parentid",
                                      "Error: deleting %s from  parentid index "
                                      "(error %d: %s)",
                                      bval->bv_val, ret, dblayer_strerror(ret));
                    return ret;
                }
            }
            slapi_attr_free(&pid_to_del);
        }
    }

    if (slapi_entry_attr_find(entry->ep_entry, LDBM_PARENTID_STR, &attr) == 0) {
        svals = attr_get_present_values(attr);
        ret = index_addordel_values_ext_sv(be, LDBM_PARENTID_STR, svals, NULL,
                                           entry->ep_id, BE_INDEX_ADD,
                                           dbmdb_get_wctx(job, NULL, WCTX_PARENTID),
                                           &idl_disposition, NULL);
        if (idl_disposition != IDL_INSERT_NORMAL) {
            char *attr_value = slapi_value_get_berval(svals[0])->bv_val;
            ID parent_id = atol(attr_value);

            if (idl_disposition == IDL_INSERT_NOW_ALLIDS) {
                dbmdb_import_subcount_mother_init(job->mothers, parent_id,
                                            idl_get_allidslimit(parentid_ai, 0) + 1);
            } else if (idl_disposition == IDL_INSERT_ALLIDS) {
                dbmdb_import_subcount_mother_count(job->mothers, parent_id);
            }
        }
        if (ret != 0) {
            import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_foreman_do_parentid",
                              "Can't update parentid index (error %d)", ret);
            return ret;
        }
    }

    return 0;
}

/* helper function for the foreman: */
static int
dbmdb_foreman_do_entrydn(ImportJob *job, FifoItem *fi)
{
    backend *be = job->inst->inst_be;
    struct berval bv;
    int err = 0, ret = 0;
    IDList *IDL;
    struct backentry *entry = fi->entry;

    if (job->flags & (FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1)) {
        /* Get the entrydn attribute value from deleted attr list */
        Slapi_Value *value = NULL;
        Slapi_Attr *entrydn_to_del =
            attrlist_remove(&entry->ep_entry->e_aux_attrs, "entrydn");

        if (entrydn_to_del) {
            /* Delete it. */
            ret = slapi_attr_first_value(entrydn_to_del, &value);
            if (ret < 0) {
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_foreman_do_entrydn",
                                  "Error: retrieving entrydn value (error %d)",
                                  ret);
            } else {
                const struct berval *bval =
                    slapi_value_get_berval((const Slapi_Value *)value);
                ret = index_addordel_string(be, LDBM_ENTRYDN_STR,
                                            bval->bv_val, entry->ep_id,
                                            BE_INDEX_DEL | BE_INDEX_EQUALITY | BE_INDEX_NORMALIZED,
                                            dbmdb_get_wctx(job, NULL, WCTX_ENTRYDN));
                if (ret) {
                    import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_foreman_do_entrydn",
                                      "Error: deleting %s from  entrydn index "
                                      "(error %d: %s)",
                                      bval->bv_val, ret, dblayer_strerror(ret));
                    return ret;
                }
            }
            slapi_attr_free(&entrydn_to_del);
        }
    }

    /* insert into the entrydn index */
    bv.bv_val = (void *)backentry_get_ndn(entry); /* jcm - Had to cast away const */
    bv.bv_len = strlen(bv.bv_val);

    /* We need to check here whether the DN is already present in
     * the entrydn index. If it is then the input ldif
     * contained a duplicate entry, which it isn't allowed to */
    /* Due to popular demand, we only warn on this, given the
     * tendency for customers to want to import dirty data */
    /* So, we do an index read first */
    err = 0;
    IDL = index_read(be, LDBM_ENTRYDN_STR, indextype_EQUALITY, &bv, NULL, &err);
    if (job->flags & (FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1)) {
        /*
         * In the UPGRADEDNFORMAT case , if entrydn value exists,
         * that means either 1) entrydn is not upgraded (ID == entry->ep_id)
         * or 2) a duplicated entry is found (ID != entry->ep_id).
         * (1) is normal. For (2), need to return a specific error
         * LDBM_ERROR_FOUND_DUPDN.
         * Otherwise, add entrydn to the entrydn index file.
         */
        if (IDL) {
            ID id = idl_firstid(IDL); /* entrydn is a single attr */
            idl_free(&IDL);
            if (id != entry->ep_id) { /* case (2) */
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_foreman_do_entrydn",
                                  "Duplicated entrydn detected: \"%s\": Entry ID: (%d, %d)",
                                  bv.bv_val, id, entry->ep_id);
                return LDBM_ERROR_FOUND_DUPDN;
            }
        } else {
            ret = index_addordel_string(be, LDBM_ENTRYDN_STR,
                                        bv.bv_val, entry->ep_id,
                                        BE_INDEX_ADD | BE_INDEX_NORMALIZED,
                                        dbmdb_get_wctx(job, NULL, WCTX_ENTRYDN));
            if (ret) {
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_foreman_do_entrydn",
                                  "Error writing entrydn index (error %d: %s)",
                                  ret, dblayer_strerror(ret));
                return ret;
            }
        }
    } else {
        /* Did this work ? */
        if (IDL) {
            /* IMPOSTER ! Get thee hence... */
            import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_foreman_do_entrydn",
                              "Skipping duplicate entry \"%s\" found at line %d of file \"%s\"",
                              slapi_entry_get_dn(entry->ep_entry),
                              fi->line, fi->filename);
            idl_free(&IDL);
            /* skip this one */
            fi->bad = FIFOITEM_BAD;
            job->skipped++;
            return -1; /* skip to next entry */
        }
        ret = index_addordel_string(be, LDBM_ENTRYDN_STR, bv.bv_val, entry->ep_id,
                                    BE_INDEX_ADD | BE_INDEX_NORMALIZED,
                                    dbmdb_get_wctx(job, NULL, WCTX_ENTRYDN));

        if (ret) {
            import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_foreman_do_entrydn",
                              "Error writing entrydn index (error %d: %s)",
                              ret, dblayer_strerror(ret));
            return ret;
        }
    }

    return 0;
}

/* helper function for the foreman: */
static int
dbmdb_foreman_do_entryrdn(ImportJob *job, FifoItem *fi)
{
    backend *be = job->inst->inst_be;
    int ret = 0;
    struct backentry *entry = fi->entry;

    if (job->flags & (FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1)) {
        /* Get the entrydn attribute value from deleted attr list */
        Slapi_Value *value = NULL;
        Slapi_Attr *entryrdn_to_del = NULL;
        entryrdn_to_del = attrlist_remove(&entry->ep_entry->e_aux_attrs,
                                          LDBM_ENTRYRDN_STR);
        if (entryrdn_to_del) {
            /* Delete it. */
            ret = slapi_attr_first_value(entryrdn_to_del, &value);
            if (ret < 0) {
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_foreman_do_entryrdn",
                                  "Error: retrieving entryrdn value (error %d)",
                                  ret);
            } else {
                const struct berval *bval =
                    slapi_value_get_berval((const Slapi_Value *)value);
                ret = entryrdn_index_entry(be, entry, BE_INDEX_DEL, NULL);
                if (ret) {
                    import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_foreman_do_entryrdn",
                                      "Error: deleting %s from  entrydn index "
                                      "(error %d: %s)",
                                      bval->bv_val, ret, dblayer_strerror(ret));
                    return ret;
                }
            }
            slapi_attr_free(&entryrdn_to_del);
            /* Waited to update e_srdn to delete the old entryrdn.
             * Now updated it to adjust to the new s_dn. */
            slapi_rdn_set_all_dn(&(entry->ep_entry->e_srdn),
                                 slapi_entry_get_dn_const(entry->ep_entry));
        }
    }
    ret = entryrdn_index_entry(be, entry, BE_INDEX_ADD, dbmdb_get_wctx(job, NULL, WCTX_ENTRYRDN));
    if (LDBM_ERROR_FOUND_DUPDN == ret) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_foreman_do_entryrdn",
                          "Duplicated DN detected: \"%s\": Entry ID: (%d)",
                          slapi_entry_get_dn(entry->ep_entry), entry->ep_id);
        return ret;
    } else if (0 != ret) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_foreman_do_entryrdn",
                          "Error writing entryrdn index (error %d: %s)",
                          ret, dblayer_strerror(ret));
        return ret;
    }
    return 0;
}
 static void
close_foreman_wqslots(ImportJob *job)
 {
     long wqslot = dbmdb_get_wqslot(job, NULL, WCTX_PARENTID);
     dbmdb_import_write_push(job, wqslot, IMPORT_WRITE_ACTION_CLOSE, NULL, NULL);
     dbmdb_free_wctx(job, NULL, WCTX_PARENTID);

     if(entryrdn_get_switch()){
         wqslot = dbmdb_get_wqslot(job, NULL, WCTX_ENTRYRDN);
         dbmdb_import_write_push(job, wqslot, IMPORT_WRITE_ACTION_CLOSE, NULL, NULL);
         dbmdb_free_wctx(job, NULL, WCTX_ENTRYRDN);
     }else{
         wqslot = dbmdb_get_wqslot(job, NULL, WCTX_ENTRYDN);
         dbmdb_import_write_push(job, wqslot, IMPORT_WRITE_ACTION_CLOSE, NULL, NULL);
         dbmdb_free_wctx(job, NULL, WCTX_ENTRYDN);
     }
     wqslot = dbmdb_get_wqslot(job, NULL, WCTX_ENTRYID);
     dbmdb_import_write_push(job, wqslot, IMPORT_WRITE_ACTION_CLOSE, NULL, NULL);
     dbmdb_free_wctx(job, NULL, WCTX_ENTRYID);
 }

/* foreman thread:
 * i go through the FIFO just like the other worker threads, but i'm
 * responsible for the interrelated indexes: entrydn, entryrdn, id2entry,
 * and the operational attributes (plus the parentid index).
 */
void
dbmdb_import_foreman(void *param)
{
    ImportWorkerInfo *info = (ImportWorkerInfo *)param;
    ImportJob *job = info->job;
    ldbm_instance *inst = job->inst;
    backend *be = inst->inst_be;
    PRIntervalTime sleeptime;
    int finished = 0;
    ID id = info->first_ID;
    int ret = 0;
    struct attrinfo *parentid_ai;
    Slapi_PBlock *pb = slapi_pblock_new();

    PR_ASSERT(info != NULL);
    PR_ASSERT(inst != NULL);

    if (job->flags & FLAG_ABORT) {
        goto error;
    }

    /* the pblock is used only by dbmdb_add_op_attrs */
    slapi_pblock_set(pb, SLAPI_BACKEND, be);
    sleeptime = PR_MillisecondsToInterval(import_sleep_time);
    info->state = RUNNING;

    ainfo_get(be, LDBM_PARENTID_STR, &parentid_ai);
    if (dbmdb_import_writer_create_dbi(info, WCTX_PARENTID, LDBM_PARENTID_STR LDBM_SUFFIX, PR_FALSE)) {
        goto error;
    }
    if (entryrdn_get_switch()){
        if (dbmdb_import_writer_create_dbi(info, WCTX_ENTRYRDN, LDBM_ENTRYRDN_STR LDBM_SUFFIX, PR_FALSE)) {
            goto error;
        }
    } else {
        if(dbmdb_import_writer_create_dbi(info, WCTX_ENTRYDN, LDBM_ENTRYDN_STR LDBM_SUFFIX, PR_FALSE)) {
            goto error;
        }
    }
    if (dbmdb_import_writer_create_dbi(info, WCTX_ENTRYID, ID2ENTRY LDBM_SUFFIX, PR_FALSE)) {
        goto error;
    }
    if (dbmdb_import_writer_create_dbi(info, WCTX_UNIQUEID, SLAPI_ATTR_UNIQUEID LDBM_SUFFIX, PR_FALSE)) {
        goto error;
    }

    while (!finished) {
        FifoItem *fi = NULL;
        int parent_status = 0;

        if (job->flags & FLAG_ABORT) {
            goto error;
        }

        while (((info->command == PAUSE) || (id > job->lead_ID)) &&
               (info->command != STOP) && (info->command != ABORT) &&
               !(job->flags & FLAG_ABORT)) {
            /* Check to see if we've been told to stop */
            info->state = WAITING;
            DS_Sleep(sleeptime);
        }
        if (info->command == STOP) {
            finished = 1;
            continue;
        }
        if (job->flags & FLAG_ABORT) {
            goto error;
        }

        info->state = RUNNING;

        /* Read that entry from the cache */
        fi = dbmdb_import_fifo_fetch(job, id, 0);
        if (NULL == fi) {
            import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_foreman",
                              "Entry id %d is missing", id);
            continue;
        }
        if (NULL == fi->entry) {
            import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_foreman",
                              "Entry for id %d is missing", id);
            continue;
        }
        if (job->flags & FLAG_UPGRADEDNFORMAT_V1) {
            if (entryrdn_get_switch()) { /* subtree-rename: on */
                /* insert into the entryrdn index */
                ret = dbmdb_foreman_do_entryrdn(job, fi);
            } else {
                /* insert into the entrydn index */
                ret = dbmdb_foreman_do_entrydn(job, fi);
                if (ret == -1) {
                    goto cont; /* skip entry */
                }
            }
            goto next;
        }
        /* first, fill in any operational attributes */
        /* dbmdb_add_op_attrs wants a pblock for some reason. */
        if (job->flags & FLAG_UPGRADEDNFORMAT) {
            /* Upgrade dn format may alter the DIT structure. */
            /* It requires a special treatment for that. */
            parent_status = IMPORT_ADD_OP_ATTRS_SAVE_OLD_PID;
        }
        if (dbmdb_add_op_attrs(pb, inst->inst_li, fi->entry, &parent_status) != 0) {
            import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_foreman",
                              "Could not add op attrs to entry ending at line %d of file \"%s\"",
                              fi->line, fi->filename);
            goto error;
        }

        if (entryrdn_get_switch() ||
            !slapi_entry_flag_is_set(fi->entry->ep_entry,
                                     SLAPI_ENTRY_FLAG_TOMBSTONE)) {
            /*
             * Only check for a parent and add to the entry2dn index
             */
            if (job->flags & FLAG_ABORT) {
                goto error;
            }

            if (parent_status == IMPORT_ADD_OP_ATTRS_NO_PARENT) {
/* If this entry is a suffix entry, this is not a problem */
/* However, if it is not, this is an error---it means that
                 * someone tried to import an entry before importing its parent
                 * we reject the entry but carry on since we've not stored
                 * anything related to this entry.
                 */
#define RUVRDN SLAPI_ATTR_UNIQUEID "=" RUV_STORAGE_ENTRY_UNIQUEID
                if (!slapi_be_issuffix(inst->inst_be, backentry_get_sdn(fi->entry)) &&
                    strcasecmp(backentry_get_ndn(fi->entry), RUVRDN) /* NOT nsuniqueid=ffffffff-... */) {
                    import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_foreman",
                                      "Skipping entry \"%s\" which has no parent, ending at line %d "
                                      "of file \"%s\"",
                                      slapi_entry_get_dn(fi->entry->ep_entry), fi->line, fi->filename);
                    /* skip this one */
                    fi->bad = FIFOITEM_BAD;
                    job->skipped++;
                    goto cont; /* below */
                }
            }
            if (job->flags & FLAG_ABORT) {
                goto error;
            }

            if (entryrdn_get_switch()) { /* subtree-rename: on */
                /* insert into the entryrdn index */
                ret = dbmdb_foreman_do_entryrdn(job, fi);
            } else {
                /* insert into the entrydn index */
                ret = dbmdb_foreman_do_entrydn(job, fi);
                if (ret == -1) {
                    goto cont; /* skip entry */
                }
            }
            if ((job->flags & FLAG_UPGRADEDNFORMAT) && (LDBM_ERROR_FOUND_DUPDN == ret)) {
                /*
                 * Duplicated DN is detected.
                 *
                 * Rename <DN> to nsuniqueid=<uuid>+<DN>
                 * E.g., uid=tuser,dc=example,dc=com ==>
                 * nsuniqueid=<uuid>+uid=tuser,dc=example,dc=com
                 *
                 * Note: FLAG_UPGRADEDNFORMAT only.
                 */
                Slapi_Attr *orig_entrydn = NULL;
                Slapi_Attr *new_entrydn = NULL;
                Slapi_Attr *nsuniqueid = NULL;
                const char *uuidstr = NULL;
                char *new_dn = NULL;
                char *orig_dn =
                    slapi_ch_strdup(slapi_entry_get_dn(fi->entry->ep_entry));
                nsuniqueid = attrlist_find(fi->entry->ep_entry->e_attrs,
                                           "nsuniqueid");
                if (nsuniqueid) {
                    Slapi_Value *uival = NULL;
                    slapi_attr_first_value(nsuniqueid, &uival);
                    uuidstr = slapi_value_get_string(uival);
                } else {
                    import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_foreman",
                                      "Failed to get nsUniqueId of the duplicated entry %s; "
                                      "Entry ID: %d",
                                      orig_dn, fi->entry->ep_id);
                    slapi_ch_free_string(&orig_dn);
                    goto cont;
                }
                new_entrydn = slapi_attr_new();
                new_dn = slapi_create_dn_string("nsuniqueid=%s+%s",
                                                uuidstr, orig_dn);
                /* releasing original dn */
                slapi_sdn_done(&fi->entry->ep_entry->e_sdn);
                /* setting new dn; pass in */
                slapi_sdn_init_dn_passin(&fi->entry->ep_entry->e_sdn, new_dn);

                /* Replacing entrydn attribute value */
                orig_entrydn = attrlist_remove(&fi->entry->ep_entry->e_attrs,
                                               "entrydn");
                /* released in forman_do_entrydn */
                attrlist_add(&fi->entry->ep_entry->e_aux_attrs, orig_entrydn);

                /* Setting new entrydn attribute value */
                slapi_attr_init(new_entrydn, "entrydn");
                valueset_add_string(new_entrydn, &new_entrydn->a_present_values,
                                    /* new_dn: duped in valueset_add_string */
                                    (const char *)new_dn,
                                    CSN_TYPE_UNKNOWN, NULL);
                attrlist_add(&fi->entry->ep_entry->e_attrs, new_entrydn);

                /* Try foreman_do_entry(r)dn, again. */
                if (entryrdn_get_switch()) { /* subtree-rename: on */
                    /* insert into the entryrdn index */
                    ret = dbmdb_foreman_do_entryrdn(job, fi);
                } else {
                    /* insert into the entrydn index */
                    ret = dbmdb_foreman_do_entrydn(job, fi);
                }
                if (ret) {
                    import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_foreman",
                                      "Failed to rename duplicated DN %s to %s; Entry ID: %d",
                                      orig_dn, new_dn, fi->entry->ep_id);
                    slapi_ch_free_string(&orig_dn);
                    if (-1 == ret) {
                        goto cont; /* skip entry */
                    } else {
                        goto error;
                    }
                } else {
                    import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_foreman",
                                      "Duplicated entry %s is renamed to %s; Entry ID: %d",
                                      orig_dn, new_dn, fi->entry->ep_id);
                    slapi_ch_free_string(&orig_dn);
                }
            } else if (0 != ret) {
                goto error;
            }
        }

        if (job->flags & FLAG_ABORT) {
            goto error;
        }
    next:
        if (!(job->flags & FLAG_REINDEXING)) /* reindex reads data from id2entry */
        {
            /* insert into the id2entry index
             * (that isn't really an index -- it's the storehouse of the entries
             * themselves.)
             */
            /* id2entry_add_ext replaces an entry if it already exists.
             * therefore, the Entry ID stays the same.
             */
            ret = id2entry_add_ext(be, fi->entry, dbmdb_get_wctx(job, NULL, WCTX_ENTRYID), job->encrypt, NULL);
            if (ret) {
                /* DB_RUNRECOVERY usually occurs if disk fills */
                if (LDBM_OS_ERR_IS_DISKFULL(ret)) {
                    import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_foreman",
                                      "OUT OF SPACE ON DISK or FILE TOO LARGE -- "
                                      "Could not store the entry ending at line %d of file \"%s\"",
                                      fi->line, fi->filename);
                } else if (ret == MDB_PANIC) {
                    import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_foreman",
                                      "(LARGEFILE SUPPORT NOT ENABLED? OUT OF SPACE ON DISK?) -- "
                                      "Could not store the entry ending at line %d of file \"%s\"",
                                      fi->line, fi->filename);
                } else {
                    import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_foreman",
                                      "Could not store the entry ending at line %d of file \"%s\" -- error %d",
                                      fi->line, fi->filename, ret);
                }
                goto error;
            }
        }

        if (job->flags & FLAG_ABORT) {
            goto error;
        }

        if ((entryrdn_get_switch() /* subtree-rename: on */ &&
             !slapi_entry_flag_is_set(fi->entry->ep_entry,
                                      SLAPI_ENTRY_FLAG_TOMBSTONE)) ||
            !entryrdn_get_switch()) {
            /* parentid index
             * (we have to do this here, because the parentID is dependent on
             * looking up by entrydn/entryrdn.)
             * Only add to the parent index if the entry is not a tombstone &&
             * subtree-rename is on.
             */
            ret = dbmdb_foreman_do_parentid(job, fi, parentid_ai);
            if (ret != 0)
                goto error;
        }

        if (!slapi_entry_flag_is_set(fi->entry->ep_entry,
                                     SLAPI_ENTRY_FLAG_TOMBSTONE)) {
            /* Lastly, before we're finished with the entry, pass it to the
               vlv code to see whether it's within the scope a VLV index. */
            vlv_grok_new_import_entry(fi->entry, be);
        }
        if (job->flags & FLAG_ABORT) {
            goto error;
        }


        /* Remove the entry from the cache (Put in the cache in id2entry_add) */
        if (!(job->flags & FLAG_REINDEXING)) {
            /* reindex reads data from id2entry */
            CACHE_REMOVE(&inst->inst_cache, fi->entry);
        }
        fi->entry->ep_refcnt = job->number_indexers;

    cont:
        if (job->flags & FLAG_ABORT) {
            goto error;
        }

        job->ready_ID = id;
        job->ready_EID = fi->entry->ep_id;
        info->last_ID_processed = id;
        id++;

        if (job->flags & FLAG_ABORT) {
            goto error;
        }
    }

    slapi_pblock_destroy(pb);
    info->state = FINISHED;
    close_foreman_wqslots(job);
    return;

error:
    slapi_pblock_destroy(pb);
    info->state = ABORTED;
    close_foreman_wqslots(job);
}


/* worker thread:
 * given an attribute, this worker plows through the entry FIFO, building
 * up the attribute index.
 */
void
dbmdb_import_worker(void *param)
{
    ImportWorkerInfo *info = (ImportWorkerInfo *)param;
    ImportJob *job = info->job;
    ldbm_instance *inst = job->inst;
    backend *be = inst->inst_be;
    PRIntervalTime sleeptime;
    int finished = 0;
    ID id = info->first_ID;
    int ret = 0;
    int idl_disposition = 0;
    struct vlvIndex *vlv_index = NULL;
    void *substring_key_buffer = NULL;
    FifoItem *fi = NULL;
    int is_objectclass_attribute;
    int is_nsuniqueid_attribute;
    int is_nscpentrydn_attribute;
    int is_nstombstonecsn_attribute;
    void *attrlist_cursor;
    long wqslot=-1;

    PR_ASSERT(NULL != info);
    PR_ASSERT(NULL != inst);

    if (job->flags & FLAG_ABORT) {
        goto error;
    }

    if (dbmdb_start_txn(__FUNCTION__, NULL, TXNFL_RDONLY, &info->txn)) {
        goto error;
    }

    if (dbmdb_import_writer_create_dbi(info, WCTX_GENERIC, info->index_info->name, PR_FALSE)) {
       goto error;
    }

    if (INDEX_VLV == info->index_info->ai->ai_indexmask) {
        vlv_index = vlv_find_indexname(info->index_info->name, be);
        if (NULL == vlv_index) {
            goto error;
        }
    }

    /*
     * If the entry is a Tombstone, then we only add it to the nsuniqeid index,
     * the nscpEntryDN index, and the idlist for (objectclass=tombstone). These
     * flags are just handy for working out what to do in this case .
     */
    is_objectclass_attribute =
        (strcasecmp(info->index_info->name, "objectclass") == 0);
    is_nsuniqueid_attribute =
        (strcasecmp(info->index_info->name, SLAPI_ATTR_UNIQUEID) == 0);
    is_nscpentrydn_attribute =
        (strcasecmp(info->index_info->name, SLAPI_ATTR_NSCP_ENTRYDN) == 0);
    is_nstombstonecsn_attribute =
        (strcasecmp(info->index_info->name, SLAPI_ATTR_TOMBSTONE_CSN) == 0);

    if (1 != idl_get_idl_new()) {
        /* Is there substring indexing going on here ? */
        if ((INDEX_SUB & info->index_info->ai->ai_indexmask) &&
            (info->index_buffer_size > 0)) {
            /* Then make a key buffer thing */
            ret = index_buffer_init(info->index_buffer_size, 0,
                                    &substring_key_buffer);
            if (0 != ret) {
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_worker",
                                  "IMPORT FAIL 1 (error %d)", ret);
            }
        }
    }

    sleeptime = PR_MillisecondsToInterval(import_sleep_time);
    info->state = RUNNING;
    info->last_ID_processed = id - 1;

    while (!finished) {
        struct backentry *ep = NULL;
        Slapi_Value **svals = NULL;
        Slapi_Attr *attr = NULL;

        if (job->flags & FLAG_ABORT) {
            goto error;
        }

        /* entry can be NULL if it turned out to be bogus */
        while (!finished && !ep) {
            /* This worker thread must wait if the command flag is "PAUSE" or
                 * the entry corresponds to the current entry treated by the foreman
                 * thread, and the state is neither STOP nor ABORT
                 */
            while (((info->command == PAUSE) || (id > job->ready_ID)) &&
                   (info->command != STOP) && (info->command != ABORT) &&
                   !(job->flags & FLAG_ABORT)) {
                /* Check to see if we've been told to stop */
                info->state = WAITING;
                DS_Sleep(sleeptime);
            }

            if (info->command == STOP) {
                finished = 1;
                continue;
            }
            if (job->flags & FLAG_ABORT) {
                goto error;
            }

            info->state = RUNNING;

            /* Read that entry from the cache */
            fi = dbmdb_import_fifo_fetch(job, id, 1);
            ep = fi ? fi->entry : NULL;
            if (!ep) {
                /* skipping an entry that turned out to be bad */
                info->last_ID_processed = id;
                id++;
            }
        }
        if (finished)
            continue;

        if (!slapi_entry_flag_is_set(fi->entry->ep_entry,
                                     SLAPI_ENTRY_FLAG_TOMBSTONE)) {
            /* This is not a tombstone entry. */
            /* Is this a VLV index ? */

            if (job->flags & FLAG_ABORT) {
                goto error;
            }

            if (INDEX_VLV == info->index_info->ai->ai_indexmask) {
                /* Yes, call VLV code -- needs pblock to find backend */
                Slapi_PBlock *pb = slapi_pblock_new();

                PR_ASSERT(NULL != vlv_index);
                slapi_pblock_set(pb, SLAPI_BACKEND, be);
                vlv_update_index(vlv_index, dbmdb_get_wctx(job, info, WCTX_GENERIC), inst->inst_li, pb, NULL, ep);
                slapi_pblock_destroy(pb);
            } else {
                /* No, process regular index */
                if (job->flags & (FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1)) {
                    /* Get the attribute value from deleted attr list */
                    Slapi_Value *value = NULL;
                    const struct berval *bval = NULL;
                    Slapi_Attr *key_to_del =
                        attrlist_remove(&fi->entry->ep_entry->e_aux_attrs,
                                        info->index_info->name);

                    if (key_to_del) {
                        int idx = 0;
                        /* Delete it. */
                        for (idx = slapi_attr_first_value(key_to_del, &value);
                             idx >= 0;
                             idx = slapi_attr_next_value(key_to_del, idx,
                                                         &value)) {
                            bval =
                                slapi_value_get_berval((const Slapi_Value *)value);
                            ret = index_addordel_string(be,
                                                        info->index_info->name,
                                                        bval->bv_val,
                                                        fi->entry->ep_id,
                                                        BE_INDEX_DEL | BE_INDEX_EQUALITY |
                                                            BE_INDEX_NORMALIZED,
                                                        dbmdb_get_wctx(job, info, WCTX_GENERIC));
                            if (ret) {
                                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_worker",
                                                  "Error deleting %s from %s index "
                                                  "(error %d: %s)",
                                                  bval->bv_val, info->index_info->name,
                                                  ret, dblayer_strerror(ret));
                                goto error;
                            }
                        }
                        slapi_attr_free(&key_to_del);
                    }
                }

                /* Look for the attribute we're indexing and its subtypes */
                /* For each attr write to the index */
                attrlist_cursor = NULL;
                while ((attr = attrlist_find_ex(ep->ep_entry->e_attrs,
                                                info->index_info->name,
                                                NULL,
                                                NULL,
                                                &attrlist_cursor)) != NULL) {

                    if (job->flags & FLAG_ABORT) {
                        goto error;
                    }
                    if (valueset_isempty(&(attr->a_present_values)))
                        continue;
                    svals = attr_get_present_values(attr);
                    ret = index_addordel_values_ext_sv(be, info->index_info->name,
                                                       svals, NULL, ep->ep_id,
                                                       BE_INDEX_ADD | (job->encrypt ? 0 : BE_INDEX_DONT_ENCRYPT),
                                                       dbmdb_get_wctx(job, info, WCTX_GENERIC),
                                                       &idl_disposition, substring_key_buffer);

                    if (0 != ret) {
                        /* Something went wrong, eg disk filled up */
                        goto error;
                    }
                }
            }
        } else {
            /* This is a Tombstone entry... we only add it to the nsuniqueid
             * index, the nscpEntryDN index,  and the idlist for (objectclass=nstombstone).
             */
            if (job->flags & FLAG_ABORT) {
                goto error;
            }
            if (is_nsuniqueid_attribute) {
                ret = index_addordel_string(be, SLAPI_ATTR_UNIQUEID,
                                            slapi_entry_get_uniqueid(ep->ep_entry), ep->ep_id,
                                            BE_INDEX_ADD,
                                            dbmdb_get_wctx(job, info, WCTX_GENERIC));
                if (0 != ret) {
                    /* Something went wrong, eg disk filled up */
                    goto error;
                }
            }
            if (is_objectclass_attribute) {
                ret = index_addordel_string(be, SLAPI_ATTR_OBJECTCLASS,
                                            SLAPI_ATTR_VALUE_TOMBSTONE, ep->ep_id, BE_INDEX_ADD, dbmdb_get_wctx(job, info, WCTX_GENERIC));
                if (0 != ret) {
                    /* Something went wrong, eg disk filled up */
                    goto error;
                }
            }
            if (is_nscpentrydn_attribute) {
                attrlist_cursor = NULL;
                while ((attr = attrlist_find_ex(ep->ep_entry->e_attrs,
                                                SLAPI_ATTR_NSCP_ENTRYDN,
                                                NULL,
                                                NULL,
                                                &attrlist_cursor)) != NULL) {

                    if (job->flags & FLAG_ABORT) {
                        goto error;
                    }
                    if (valueset_isempty(&(attr->a_present_values)))
                        continue;
                    svals = attr_get_present_values(attr);
                    ret=index_addordel_values_ext_sv(be, info->index_info->name, svals, NULL, ep->ep_id,
                                                     BE_INDEX_ADD|(job->encrypt ? 0 : BE_INDEX_DONT_ENCRYPT),
                             dbmdb_get_wctx(job,info,WCTX_GENERIC),
                             &idl_disposition,substring_key_buffer);

                    if (0 != ret) {
                        /* Something went wrong, eg disk filled up */
                        goto error;
                    }
                }
            }
            if (is_nstombstonecsn_attribute) {
                const CSN *tomb_csn = entry_get_deletion_csn(ep->ep_entry);
                char tomb_csnstr[CSN_STRSIZE];

                if (tomb_csn) {
                    csn_as_string(tomb_csn, PR_FALSE, tomb_csnstr);
                    ret = index_addordel_string(be, SLAPI_ATTR_TOMBSTONE_CSN,
                                                tomb_csnstr, ep->ep_id, BE_INDEX_ADD, dbmdb_get_wctx(job, info, WCTX_GENERIC));
                    if (0 != ret) {
                        /* Something went wrong, eg disk filled up */
                        goto error;
                    }
                }
            }
        }
        dbmdb_import_decref_entry(ep);
        info->last_ID_processed = id;
        id++;

        if (job->flags & FLAG_ABORT) {
            goto error;
        }
    }

    if (job->flags & FLAG_ABORT) {
        goto error;
    }


    /* If we were buffering index keys, now flush them */
    if (substring_key_buffer) {
        ret = index_buffer_flush(substring_key_buffer,
                                 inst->inst_be, NULL,
                                 info->index_info->ai);
        if (0 != ret) {
            goto error;
        }
    }
    info->state = FINISHED;
    goto done;

error:
     if (ret == MDB_PANIC) {
        slapi_log_err(SLAPI_LOG_CRIT, "dbmdb_import_worker",
                      "Cannot import; database recovery needed\n");
    }

    info->state = ABORTED;

done:
    if (substring_key_buffer) {
        index_buffer_terminate(be, substring_key_buffer);
    }
    wqslot = dbmdb_get_wqslot(job, info, WCTX_GENERIC);
    dbmdb_end_txn(__FUNCTION__, 1, &info->txn);
    dbmdb_import_write_push(job, wqslot, IMPORT_WRITE_ACTION_CLOSE, NULL, NULL);
    dbmdb_free_wctx(job, info, WCTX_GENERIC);
}


/*
 * import entries to a backend, over the wire -- entries will arrive
 * asynchronously, so this method has no "producer" thread.  instead, the
 * front-end drops new entries in as they arrive.
 *
 * this is sometimes called "fast replica initialization".
 *
 * some of this code is duplicated from ldif2ldbm, but i don't think we
 * can avoid it.
 */
static int
dbmdb_bulk_import_start(Slapi_PBlock *pb)
{
    struct ldbminfo *li = NULL;
    ImportJob *job = NULL;
    backend *be = NULL;
    PRThread *thread = NULL;
    int ret = 0;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (be == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_bulk_import_start", "Backend is not set\n");
        return -1;
    }
    job = CALLOC(ImportJob);
    slapi_pblock_get(pb, SLAPI_LDIF2DB_ENCRYPT, &job->encrypt);
    li = (struct ldbminfo *)(be->be_database->plg_private);
    job->inst = (ldbm_instance *)be->be_instance_info;

    /* check if an import/restore is already ongoing... */
    PR_Lock(job->inst->inst_config_mutex);
    if (job->inst->inst_flags & INST_FLAG_BUSY) {
        PR_Unlock(job->inst->inst_config_mutex);
        slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_bulk_import_start",
                      "'%s' is already in the middle of another task and cannot be disturbed.\n",
                      job->inst->inst_name);
        FREE(job);
        return SLAPI_BI_ERR_BUSY;
    }
    job->inst->inst_flags |= INST_FLAG_BUSY;
    PR_Unlock(job->inst->inst_config_mutex);

    /* take backend offline */
    slapi_mtn_be_disable(be);

    /* get uniqueid info */
    slapi_pblock_get(pb, SLAPI_LDIF2DB_GENERATE_UNIQUEID, &job->uuid_gen_type);
    if (job->uuid_gen_type == SLAPI_UNIQUEID_GENERATE_NAME_BASED) {
        char *namespaceid;

        slapi_pblock_get(pb, SLAPI_LDIF2DB_NAMESPACEID, &namespaceid);
        job->uuid_namespace = slapi_ch_strdup(namespaceid);
    }

    job->flags = 0; /* don't use files */
    job->flags |= FLAG_INDEX_ATTRS;
    job->flags |= FLAG_ONLINE;
    job->starting_ID = 1;
    job->first_ID = 1;

    job->mothers = CALLOC(import_subcount_stuff);
    /* how much space should we allocate to index buffering? */
    job->job_index_buffer_size = dbmdb_import_get_index_buffer_size();
    if (job->job_index_buffer_size == 0) {
        /* 10% of the allocated cache size + one meg */
        job->job_index_buffer_size = (job->inst->inst_li->li_dbcachesize / 10) +
                                     (1024 * 1024);
    }
    import_subcount_stuff_init(job->mothers);

    pthread_mutex_init(&job->wire_lock, NULL);
    pthread_cond_init(&job->wire_cv, NULL);

    /* COPIED from ldif2ldbm.c : */

    /* shutdown this instance of the db */
    cache_clear(&job->inst->inst_cache, CACHE_TYPE_ENTRY);
    if (entryrdn_get_switch()) {
        cache_clear(&job->inst->inst_dncache, CACHE_TYPE_DN);
    }
    dblayer_instance_close(be);

    /* Delete old database files */
    dbmdb_delete_instance_dir(be);
    /* it's okay to fail -- it might already be gone */

    /* vlv_init should be called before dbmdb_instance_start
     * so the vlv dbi get created
     */
    vlv_init(job->inst);
    /* dbmdb_instance_start will init the id2entry index. */
    /* it also (finally) fills in inst_dir_name */
    ret = dbmdb_instance_start(be, DBLAYER_IMPORT_MODE);
    if (ret != 0)
        goto fail;

    /* END OF COPIED SECTION */

    pthread_mutex_lock(&job->wire_lock);

    /* create thread for dbmdb_import_main, so we can return */
    thread = PR_CreateThread(PR_USER_THREAD, dbmdb_import_main, (void *)job,
                             PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                             PR_JOINABLE_THREAD,
                             SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        PRErrorCode prerr = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_bulk_import_start",
                      "Unable to spawn import thread, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                      prerr, slapd_pr_strerror(prerr));
        pthread_mutex_unlock(&job->wire_lock);
        ret = -2;
        goto fail;
    }

    job->main_thread = thread;

    Connection *pb_conn;
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);

    slapi_set_object_extension(li->li_bulk_import_object, pb_conn, li->li_bulk_import_handle, job);

    /* wait for the dbmdb_import_main to signal that it's ready for entries */
    /* (don't want to send the success code back to the LDAP client until
     * we're ready for the adds to start rolling in)
     */
    pthread_cond_wait(&job->wire_cv, &job->wire_lock);
    pthread_mutex_unlock(&job->wire_lock);

    return 0;

fail:
    PR_Lock(job->inst->inst_config_mutex);
    job->inst->inst_flags &= ~INST_FLAG_BUSY;
    PR_Unlock(job->inst->inst_config_mutex);
    dbmdb_import_free_job(job);
    FREE(job);
    return ret;
}

/* returns 0 on success, or < 0 on error
 *
 * on error, the import process is aborted -- so if this returns an error,
 * don't try to queue any more entries or you'll be sorry.  The caller
 * is also responsible for free'ing the passed in entry on error.  The
 * entry will be consumed on success.
 */
static int
dbmdb_bulk_import_queue(ImportJob *job, Slapi_Entry *entry)
{
    struct backentry *ep = NULL, *old_ep = NULL;
    int idx;
    ID id = 0;
    Slapi_Attr *attr = NULL;
    size_t newesize = 0;

    if (!entry) {
        return -1;
    }

    /* The import is aborted, just ignore that entry */
    if (job->flags & FLAG_ABORT) {
        return -1;
    }

    pthread_mutex_lock(&job->wire_lock);
    /* Let's do this inside the lock !*/
    id = job->lead_ID + 1;
    /* generate uniqueid if necessary */
    if (dbmdb_import_generate_uniqueid(job, entry) != UID_SUCCESS) {
        import_abort_all(job, 1);
        pthread_mutex_unlock(&job->wire_lock);
        return -1;
    }

    /* make into backentry */
    ep = dbmdb_import_make_backentry(entry, id);
    if ((ep == NULL) || (ep->ep_entry == NULL)) {
        import_abort_all(job, 1);
        backentry_free(&ep); /* release the backend wrapper, here */
        pthread_mutex_unlock(&job->wire_lock);
        return -1;
    }

    /* encode the password */
    if (slapi_entry_attr_find(ep->ep_entry, "userpassword", &attr) == 0) {
        Slapi_Value **va = attr_get_present_values(attr);

        pw_encodevals((Slapi_Value **)va); /* jcm - had to cast away const */
    }

    /* if usn_value is available AND the entry does not have it, */
    if (job->usn_value && slapi_entry_attr_find(ep->ep_entry,
                                                SLAPI_ATTR_ENTRYUSN, &attr)) {
        slapi_entry_add_value(ep->ep_entry, SLAPI_ATTR_ENTRYUSN,
                              job->usn_value);
    }

    /* Now we have this new entry, all decoded
     * Next thing we need to do is:
     * (1) see if the appropriate fifo location contains an
     *     entry which had been processed by the indexers.
     *     If so, proceed.
     *     If not, spin waiting for it to become free.
     * (2) free the old entry and store the new one there.
     * (3) Update the job progress indicators so the indexers
     *     can use the new entry.
     */
    idx = id % job->fifo.size;
    old_ep = job->fifo.item[idx].entry;
    if (old_ep) {
        while ((old_ep->ep_refcnt > 0) && !(job->flags & FLAG_ABORT)) {
            DS_Sleep(PR_MillisecondsToInterval(import_sleep_time));
        }

        /* the producer could be startcfg thru the fifo while
         * everyone else is cycling to a new pass...
         * double-check that this entry is < ready_EID
         */
        while ((old_ep->ep_id >= job->ready_EID) && !(job->flags & FLAG_ABORT)) {
            DS_Sleep(PR_MillisecondsToInterval(import_sleep_time));
        }

        if (job->flags & FLAG_ABORT) {
            backentry_clear_entry(ep); /* entry is released in the frontend on failure*/
            backentry_free(&ep);       /* release the backend wrapper, here */
            pthread_mutex_unlock(&job->wire_lock);
            return -2;
        }

        PR_ASSERT(old_ep == job->fifo.item[idx].entry);
        job->fifo.item[idx].entry = NULL;
        if (job->fifo.c_bsize > job->fifo.item[idx].esize)
            job->fifo.c_bsize -= job->fifo.item[idx].esize;
        else
            job->fifo.c_bsize = 0;
        backentry_free(&old_ep);
    }
    /* Is subtree-rename on? And is this a tombstone?
     * If so, need a special treatment */
    if (entryrdn_get_switch() &&
        (ep->ep_entry->e_flags & SLAPI_ENTRY_FLAG_TOMBSTONE)) {
        char *tombstone_rdn =
            slapi_ch_strdup(slapi_entry_get_dn_const(ep->ep_entry));
        if ((0 == PL_strncasecmp(tombstone_rdn, SLAPI_ATTR_UNIQUEID,
                                 sizeof(SLAPI_ATTR_UNIQUEID) - 1)) &&
            /* dn starts with "nsuniqueid=" */
            (NULL == PL_strstr(tombstone_rdn, RUV_STORAGE_ENTRY_UNIQUEID))) {
            /* and this is not an RUV */
            char *sepp = PL_strchr(tombstone_rdn, ',');
            /* dn looks like this:
             * nsuniqueid=042d8081-...-ca8fe9f7,uid=tuser,o=abc.com
             * create a new srdn for the original dn
             * uid=tuser,o=abc.com
             */
            if (sepp) {
                Slapi_RDN mysrdn = {0};
                if (slapi_rdn_init_all_dn(&mysrdn, sepp + 1)) {
                    slapi_log_err(SLAPI_LOG_ERR, "dbmdb_bulk_import_queue",
                                  "Failed to convert DN %s to RDN\n", sepp + 1);
                    slapi_ch_free_string(&tombstone_rdn);
                    /* entry is released in the frontend on failure*/
                    backentry_clear_entry(ep);
                    backentry_free(&ep); /* release the backend wrapper */
                    pthread_mutex_unlock(&job->wire_lock);
                    return -1;
                }
                sepp = PL_strchr(sepp + 1, ',');
                if (sepp) {
                    Slapi_RDN *srdn = slapi_entry_get_srdn(ep->ep_entry);
                    /* nsuniqueid=042d8081-...-ca8fe9f7,uid=tuser, */
                    /*                                           ^ */
                    *sepp = '\0';
                    slapi_rdn_replace_rdn(&mysrdn, tombstone_rdn);
                    slapi_rdn_done(srdn);
                    slapi_entry_set_srdn(ep->ep_entry, &mysrdn);
                    slapi_rdn_done(&mysrdn);
                }
            }
        }
        slapi_ch_free_string(&tombstone_rdn);
    }

    newesize = (slapi_entry_size(ep->ep_entry) + sizeof(struct backentry));
    if (dbmdb_import_fifo_validate_capacity_or_expand(job, newesize) == 1) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_bulk_import_queue", "Entry too large (%lu bytes) for "
                                                                   "the effective import buffer size (%lu bytes), and we were UNABLE to expand buffer. ",
                          (long unsigned int)newesize, (long unsigned int)job->fifo.bsize);
        backentry_clear_entry(ep); /* entry is released in the frontend on failure*/
        backentry_free(&ep);       /* release the backend wrapper, here */
        pthread_mutex_unlock(&job->wire_lock);
        return -1;
    }
    /* Now check if fifo has enough space for the new entry */
    if ((job->fifo.c_bsize + newesize) > job->fifo.bsize) {
        dbmdb_import_wait_for_space_in_fifo(job, newesize);
    }

    /* We have enough space */
    job->fifo.item[idx].filename = "(bulk import)";
    job->fifo.item[idx].line = 0;
    job->fifo.item[idx].entry = ep;
    job->fifo.item[idx].bad = 0;
    job->fifo.item[idx].esize = newesize;

    /* Add the entry size to total fifo size */
    job->fifo.c_bsize += ep->ep_entry ? job->fifo.item[idx].esize : 0;

    /* Update the job to show our progress */
    job->lead_ID = id;
    if ((id - job->starting_ID) <= job->fifo.size) {
        job->trailing_ID = job->starting_ID;
    } else {
        job->trailing_ID = id - job->fifo.size;
    }

    pthread_mutex_unlock(&job->wire_lock);
    return 0;
}

/* plugin entry function for replica init
 *
 * For the SLAPI_BI_STATE_ADD state:
 * On success (rc=0), the entry in pb->pb_import_entry will be
 * consumed.  For any other return value, the caller is
 * responsible for freeing the entry in the pb.
 */
int
dbmdb_ldbm_back_wire_import(Slapi_PBlock *pb)
{
    struct ldbminfo *li;
    backend *be = NULL;
    ImportJob *job = NULL;
    PRThread *thread;
    int state;
    Connection *pb_conn;

    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (be == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ldbm_back_wire_import",
                      "Backend is not set\n");
        return -1;
    }
    li = (struct ldbminfo *)(be->be_database->plg_private);
    slapi_pblock_get(pb, SLAPI_BULK_IMPORT_STATE, &state);
    slapi_pblock_set(pb, SLAPI_LDIF2DB_ENCRYPT, &li->li_online_import_encrypt);
    if (state == SLAPI_BI_STATE_START) {
        /* starting a new import */
        int rc = dbmdb_bulk_import_start(pb);
        if (!rc) {
            /* job must be available since dbmdb_bulk_import_start was successful */
            job = (ImportJob *)slapi_get_object_extension(li->li_bulk_import_object, pb_conn, li->li_bulk_import_handle);
            /* Get entryusn, if needed. */
            _get_import_entryusn(job, &(job->usn_value));
        }
        return rc;
    }

    PR_ASSERT(pb_conn != NULL);
    if (pb_conn != NULL) {
        job = (ImportJob *)slapi_get_object_extension(li->li_bulk_import_object, pb_conn, li->li_bulk_import_handle);
    }

    if ((job == NULL) || (pb_conn == NULL)) {
        /* import might be aborting */
        return -1;
    }

    if (state == SLAPI_BI_STATE_ADD) {
        Slapi_Entry *pb_import_entry = NULL;
        slapi_pblock_get(pb, SLAPI_BULK_IMPORT_ENTRY, &pb_import_entry);
        /* continuing previous import */
        if (!dbmdb_import_entry_belongs_here(pb_import_entry, job->inst->inst_be)) {
            /* silently skip */
            /* We need to consume pb->pb_import_entry on success, so we free it here. */
            slapi_entry_free(pb_import_entry);
            return 0;
        }

        return dbmdb_bulk_import_queue(job, pb_import_entry);
    }

    thread = job->main_thread;

    if (state == SLAPI_BI_STATE_DONE) {
        slapi_value_free(&(job->usn_value));
        /* finished with an import */
        job->flags |= FLAG_PRODUCER_DONE;
        /* "job" struct may vanish at any moment after we set the DONE
         * flag, so keep a copy of the thread id in 'thread' for safekeeping.
         */
        /* wait for dbmdb_import_main to finish... */
        PR_JoinThread(thread);
        slapi_set_object_extension(li->li_bulk_import_object, pb_conn, li->li_bulk_import_handle, NULL);
        return 0;
    }

    /* ??? unknown state */
    slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ldbm_back_wire_import",
                  "ERROR: unknown state %d\n", state);
    return -1;
}

/*
 * backup index configuration
 * this function is called from dblayer_backup (ldbm2archive)
 * [547427] index config must not change between backup and restore
 */
#define DSE_INDEX_FILTER "(objectclass=nsIndex)"
#define DSE_INSTANCE_FILTER "(objectclass=nsBackendInstance)"
static int
dbmdb_dse_conf_backup_core(struct ldbminfo *li, char *dest_dir, char *file_name, char *filter)
{
    Slapi_PBlock *srch_pb = NULL;
    Slapi_Entry **entries = NULL;
    Slapi_Entry **ep = NULL;
    Slapi_Attr *attr = NULL;
    char *attr_name;
    char *filename = NULL;
    PRFileDesc *prfd = NULL;
    int rval = 0;
    int dlen = 0;
    PRInt32 prrval;
    char tmpbuf[BUFSIZ];
    char *tp = NULL;

    dlen = strlen(dest_dir);
    if (0 == dlen) {
        filename = file_name;
    } else {
        filename = slapi_ch_smprintf("%s/%s", dest_dir, file_name);
    }
    slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_dse_conf_backup_core",
                  "(%s): backup file %s\n", filter, filename);

    /* Open the file to write */
    if ((prfd = PR_Open(filename, PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE,
                        SLAPD_DEFAULT_FILE_MODE)) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_dse_conf_backup_core",
                      "(%s): open %s failed: (%s)\n",
                      filter, filename, slapd_pr_strerror(PR_GetError()));
        rval = -1;
        goto out;
    }

    srch_pb = slapi_pblock_new();
    if (!srch_pb) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_dse_conf_backup_core",
                      "(%s): out of memory\n", filter);
        rval = -1;
        goto out;
    }

    slapi_search_internal_set_pb(srch_pb, li->li_plugin->plg_dn,
                                 LDAP_SCOPE_SUBTREE, filter, NULL, 0, NULL, NULL, li->li_identity, 0);
    slapi_search_internal_pb(srch_pb);
    slapi_pblock_get(srch_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    for (ep = entries; ep != NULL && *ep != NULL; ep++) {
        int32_t l = strlen(slapi_entry_get_dn_const(*ep)) + 5 /* "dn: \n" */;
        slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_dse_conf_backup_core",
                      "dn: %s\n", slapi_entry_get_dn_const(*ep));

        if (l <= sizeof(tmpbuf))
            tp = tmpbuf;
        else
            tp = (char *)slapi_ch_malloc(l); /* should be very rare ... */
        sprintf(tp, "dn: %s\n", slapi_entry_get_dn_const(*ep));
        prrval = PR_Write(prfd, tp, l);
        if (prrval != l) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_dse_conf_backup_core",
                          "(%s): write %" PRId32 " failed: %d (%s)\n",
                          filter, l, PR_GetError(), slapd_pr_strerror(PR_GetError()));
            rval = -1;
            if (l > sizeof(tmpbuf))
                slapi_ch_free_string(&tp);
            goto out;
        }
        if (l > sizeof(tmpbuf))
            slapi_ch_free_string(&tp);

        for (slapi_entry_first_attr(*ep, &attr); attr;
             slapi_entry_next_attr(*ep, attr, &attr)) {
            int i;
            Slapi_Value *sval = NULL;
            const struct berval *attr_val;
            int attr_name_len;

            slapi_attr_get_type(attr, &attr_name);
            /* numsubordinates should not be backed up */
            if (!strcasecmp(LDBM_NUMSUBORDINATES_STR, attr_name))
                continue;
            attr_name_len = strlen(attr_name);
            for (i = slapi_attr_first_value(attr, &sval); i != -1;
                 i = slapi_attr_next_value(attr, i, &sval)) {
                attr_val = slapi_value_get_berval(sval);
                l = strlen(attr_val->bv_val) + attr_name_len + 3; /* : \n" */
                slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_dse_conf_backup_core",
                              "%s: %s\n", attr_name, attr_val->bv_val);
                if (l <= sizeof(tmpbuf))
                    tp = tmpbuf;
                else
                    tp = (char *)slapi_ch_malloc(l);
                sprintf(tp, "%s: %s\n", attr_name, attr_val->bv_val);
                prrval = PR_Write(prfd, tp, l);
                if (prrval != l) {
                    slapi_log_err(SLAPI_LOG_ERR, "dbmdb_dse_conf_backup_core",
                                  "(%s): write %" PRId32 " failed: %d (%s)\n",
                                  filter, l, PR_GetError(), slapd_pr_strerror(PR_GetError()));
                    rval = -1;
                    if (l > sizeof(tmpbuf))
                        slapi_ch_free_string(&tp);
                    goto out;
                }
                if (l > sizeof(tmpbuf))
                    slapi_ch_free_string(&tp);
            }
        }
        if (ep + 1 != NULL && *(ep + 1) != NULL) {
            prrval = PR_Write(prfd, "\n", 1);
            if (prrval != 1) {
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_dse_conf_backup_core",
                              "(%s): write %" PRId32 " failed: %d (%s)\n",
                              filter, l, PR_GetError(), slapd_pr_strerror(PR_GetError()));
                rval = -1;
                goto out;
            }
        }
    }

out:
    if (srch_pb) {
        slapi_free_search_results_internal(srch_pb);
        slapi_pblock_destroy(srch_pb);
    }

    if (0 != dlen) {
        slapi_ch_free_string(&filename);
    }

    if (prfd) {
        prrval = PR_Close(prfd);
        if (PR_SUCCESS != prrval) {
            slapi_log_err(SLAPI_LOG_CRIT, "dbmdb_dse_conf_backup_core",
                          "Failed to back up dse indexes %d (%s)\n",
                          PR_GetError(), slapd_pr_strerror(PR_GetError()));
            rval = -1;
        }
    }

    return rval;
}

int
dbmdb_dse_conf_backup(struct ldbminfo *li, char *dest_dir)
{
    int rval = 0;
    rval = dbmdb_dse_conf_backup_core(li, dest_dir, DSE_INSTANCE, DSE_INSTANCE_FILTER);
    rval += dbmdb_dse_conf_backup_core(li, dest_dir, DSE_INDEX, DSE_INDEX_FILTER);
    return rval;
}

/*
 * read the backed up index configuration
 * this function is called from dblayer_restore (archive2ldbm)
 * these functions are placed here to borrow import_get_entry
 * [547427] index config must not change between backup and restore
 */
Slapi_Entry **
dbmdb_read_ldif_entries(struct ldbminfo *li, char *src_dir, char *file_name)
{
    int fd = -1;
    int curr_lineno = 0;
    Slapi_Entry **backup_entries = NULL;
    char *filename = NULL;
    char *estr = NULL;
    int max_entries = 0;
    int nb_entries = 0;
    ldif_context c;

    dbmdb_import_init_ldif(&c);
    filename = slapi_ch_smprintf("%s/%s", src_dir, file_name);
    if (PR_SUCCESS != PR_Access(filename, PR_ACCESS_READ_OK)) {
        slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_read_ldif_entries",
                      "Config backup file %s not found in backup\n",
                      file_name);
        goto out;
    }
    fd = dbmdb_open_huge_file(filename, O_RDONLY, 0);
    if (fd < 0) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_read_ldif_entries",
                      "Can't open config backup file: %s\n", filename);
        goto out;
    }

    while ((estr = dbmdb_import_get_entry(&c, fd, &curr_lineno))) {
        Slapi_Entry *e = slapi_str2entry(estr, 0);
        slapi_ch_free_string(&estr);
        if (!e) {
            slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_read_ldif_entries",
                          "Skipping bad LDIF entry ending line %d of file \"%s\"",
                          curr_lineno, filename);
            continue;
        }
        if (nb_entries+1 >= max_entries) { /* Reserve enough space to add the final NULL element */
            max_entries = max_entries ? 2 * max_entries : 256;
            backup_entries = (Slapi_Entry **)slapi_ch_realloc((char *)backup_entries, max_entries * sizeof(Slapi_Entry *));
        }
        backup_entries[nb_entries++] = e;
    }
    if (!backup_entries) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_read_ldif_entries",
                      "No entry found in backup config file \"%s\"",
                      filename);
        goto out;
    }
    backup_entries[nb_entries] = NULL;
out:
    slapi_ch_free_string(&filename);
    if (fd >= 0) {
        close(fd);
    }
    dbmdb_import_free_ldif(&c);

    return backup_entries;
}

/*
 * read the backed up index configuration
 * adjust them if the current configuration is different from it.
 * this function is called from dblayer_restore (archive2ldbm)
 * these functions are placed here to borrow import_get_entry
 * [547427] index config must not change between backup and restore
 */
int
dbmdb_dse_conf_verify_core(struct ldbminfo *li, char *src_dir, char *file_name, char *filter, int force_update, char *log_str)
{
    Slapi_Entry **backup_entries = dbmdb_read_ldif_entries(li, src_dir, file_name);
    Slapi_Entry **curr_entries = NULL;
    Slapi_Entry **bep = NULL;
    int rval = 0;

    if (!backup_entries) {
        /* Error is already logged */
        return -1;
    }

    char * search_scope = slapi_ch_strdup(li->li_plugin->plg_dn);
    Slapi_PBlock *srch_pb = slapi_pblock_new();

    slapi_search_internal_set_pb(srch_pb, search_scope,
                                 LDAP_SCOPE_SUBTREE, filter, NULL, 0, NULL, NULL, li->li_identity, 0);
    slapi_search_internal_pb(srch_pb);
    slapi_pblock_get(srch_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &curr_entries);
    if (!curr_entries) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_dse_conf_verify_core",
                      "Failed to get current configuration.\n");
        rval = -1;
        goto out;
    }

    if (0 != slapi_entries_diff(backup_entries, curr_entries, 1 /* test_all */,
                                log_str, force_update, li->li_identity)) {
        if (force_update) {
            slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_dse_conf_verify_core",
                          "Current %s is different from backed up configuration; "
                          "The backup is restored.\n",
                          log_str);
        } else {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_dse_conf_verify_core",
                          "Current %s is different from backed up configuration; "
                          "The backup is not restored.\n",
                          log_str);
            rval = -1;
        }
    }

    slapi_free_search_results_internal(srch_pb);
    slapi_pblock_destroy(srch_pb);
out:
    for (bep = backup_entries; bep && *bep; bep++) {
        slapi_entry_free(*bep);
    }
    slapi_ch_free((void **)&backup_entries);

    slapi_ch_free_string(&search_scope);

    return rval;
}

int
dbmdb_dse_conf_verify(struct ldbminfo *li, char *src_dir)
{
    int rval;
    char *instance_entry_filter = NULL;

    instance_entry_filter = slapi_ch_strdup(DSE_INSTANCE_FILTER);

    /* instance should not be changed between bakup and restore */
    rval = dbmdb_dse_conf_verify_core(li, src_dir, DSE_INSTANCE, instance_entry_filter,
                                      0 /* force update */, "Instance Config");
    rval += dbmdb_dse_conf_verify_core(li, src_dir, DSE_INDEX, DSE_INDEX_FILTER,
                                       1 /* force update */, "Index Config");

    slapi_ch_free_string(&instance_entry_filter);

    return rval;
}

static int
dbmdb_import_get_and_add_parent_rdns(ImportWorkerInfo *info,
                               ldbm_instance *inst,
                               dbmdb_dbi_t **db,
                               dbi_txn_t *txn,
                               ID id,
                               ID *total_id,
                               Slapi_RDN *srdn,
                               int *curr_entry)
{
    int rc = -1;
    struct backdn *bdn = NULL;
    Slapi_Entry *e = NULL;
    char *normdn = NULL;

    if (!entryrdn_get_switch()) { /* entryrdn specific function */
        return rc;
    }
    if (NULL == inst || NULL == srdn) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_get_and_add_parent_rdns",
                      "Empty %s\n", NULL == inst ? "inst" : "srdn");
        return rc;
    }

    /* first, try the dn cache */
    bdn = dncache_find_id(&inst->inst_dncache, id);
    if (bdn) {
        Slapi_RDN mysrdn = {0};
        /* Luckily, found the parent in the dn cache! */
        if (slapi_rdn_get_rdn(srdn)) { /* srdn is already in use */
            rc = slapi_rdn_init_all_dn(&mysrdn, slapi_sdn_get_dn(bdn->dn_sdn));
            if (rc) {
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_get_and_add_parent_rdns",
                              "Failed to convert DN %s to RDN\n",
                              slapi_sdn_get_dn(bdn->dn_sdn));
                slapi_rdn_done(&mysrdn);
                CACHE_RETURN(&inst->inst_dncache, &bdn);
                return rc;
            }
            rc = slapi_rdn_add_srdn_to_all_rdns(srdn, &mysrdn);
            if (rc) {
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_get_and_add_parent_rdns",
                              "Failed to merge Slapi_RDN %s to RDN\n",
                              slapi_sdn_get_dn(bdn->dn_sdn));
            }
            slapi_rdn_done(&mysrdn);
        } else { /* srdn is empty */
            rc = slapi_rdn_init_all_dn(srdn, slapi_sdn_get_dn(bdn->dn_sdn));
            if (rc) {
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_get_and_add_parent_rdns",
                              "Failed to convert DN %s to RDN\n",
                              slapi_sdn_get_dn(bdn->dn_sdn));
                CACHE_RETURN(&inst->inst_dncache, &bdn);
                return rc;
            }
        }
        CACHE_RETURN(&inst->inst_dncache, &bdn);
        return rc;
    } else {
        MDB_val key, data;
        char *rdn = NULL;
        char *pid_str = NULL;
        ID storedid;
        Slapi_RDN mysrdn = {0};

        /* not in the dn cache; read id2entry */
        if (NULL == db) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_get_and_add_parent_rdns",
                          "Empty db\n");
            return rc;
        }
        id_internal_to_stored(id, (char *)&storedid);
        key.mv_size = sizeof(ID);
        key.mv_data = &storedid;

        data.mv_data = NULL;
        rc = mdb_get(TXN(txn), DB(db), &key, &data);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_get_and_add_parent_rdns",
                          "Failed to position at ID " ID_FMT "\n", id);
            return rc;
        }
        /* rdn is allocated in get_value_from_string */
        rc = get_value_from_string(data.mv_data, "rdn", &rdn);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_get_and_add_parent_rdns",
                          "Failed to get rdn of entry " ID_FMT "\n", id);
            goto bail;
        }
        /* rdn is set to srdn */
        rc = slapi_rdn_init_all_dn(&mysrdn, rdn);
        if (rc < 0) { /* expect rc == 1 since we are setting "rdn" not "dn" */
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_get_and_add_parent_rdns",
                          "Failed to add rdn %s of entry " ID_FMT "\n", rdn, id);
            goto bail;
        }
        rc = get_value_from_string(data.mv_data, LDBM_PARENTID_STR, &pid_str);
        if (rc) {
            rc = 0; /* assume this is a suffix */
        } else {
            ID pid = (ID)strtol(pid_str, (char **)NULL, 10);
            slapi_ch_free_string(&pid_str);
            rc = dbmdb_import_get_and_add_parent_rdns(info, inst, db,txn,  pid, total_id,
                                                &mysrdn, curr_entry);
            if (rc) {
                slapi_ch_free_string(&rdn);
                goto bail;
            }
        }

        normdn = NULL;
        rc = slapi_rdn_get_dn(&mysrdn, &normdn);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_get_and_add_parent_rdns",
                          "Failed to compose dn for (rdn: %s, ID: %d) "
                          "from Slapi_RDN\n",
                          rdn, id);
            goto bail;
        }
        e = slapi_str2entry_ext(normdn, NULL, data.mv_data, SLAPI_STR2ENTRY_NO_ENTRYDN);
        (*curr_entry)++;
        rc = dbmdb_index_set_entry_to_fifo(info, e, id, total_id, *curr_entry);
        if (rc) {
            goto bail;
        }
        rc = slapi_rdn_add_srdn_to_all_rdns(srdn, &mysrdn);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_get_and_add_parent_rdns",
                          "Failed to merge Slapi_RDN to RDN\n");
        }
    bail:
        slapi_ch_free_string(&rdn);
        return rc;
    }
}

static int
_get_import_entryusn(ImportJob *job, Slapi_Value **usn_value)
{
#define USN_COUNTER_BUF_LEN 64 /* enough size for 64 bit integers */
    static char counter_buf[USN_COUNTER_BUF_LEN] = {0};
    char *usn_init_str = NULL;
    long long usn_init;
    char *endptr = NULL;
    struct berval usn_berval = {0};

    if (NULL == usn_value) {
        return 1;
    }
    *usn_value = NULL;
    /*
     * Check if entryusn plugin is enabled.
     * If yes, get entryusn to set depending upon nsslapd-entryusn-import-init
     */
    if (!plugin_enabled("USN", (void *)plugin_get_default_component_id())) {
        return 1;
    }
    /* get the import_init config param */
    usn_init_str = config_get_entryusn_import_init();
    if (usn_init_str) {
        /* nsslapd-entryusn-import-init has a value */
        usn_init = strtoll(usn_init_str, &endptr, 10);
        if (errno || (0 == usn_init && endptr == usn_init_str)) {
            ldbm_instance *inst = job->inst;
            backend *be = inst->inst_be;
            /* import_init value is not digit.
             * Use the counter which stores the old DB's
             * next entryusn. */
            PR_snprintf(counter_buf, sizeof(counter_buf),
                        "%" PRIu64, slapi_counter_get_value(be->be_usn_counter));
        } else {
            /* import_init value is digit.
             * Initialize the entryusn values with the digit */
            PR_snprintf(counter_buf, sizeof(counter_buf), "%s", usn_init_str);
        }
        slapi_ch_free_string(&usn_init_str);
    } else {
        /* nsslapd-entryusn-import-init is not defined */
        /* Initialize to 0 by default */
        PR_snprintf(counter_buf, sizeof(counter_buf), "0");
    }
    usn_berval.bv_val = counter_buf;
    usn_berval.bv_len = strlen(usn_berval.bv_val);
    *usn_value = slapi_value_new_berval(&usn_berval);
    return 0;
 }

 /* Worker thread utilities */

/* Update statistics and determine if thread processing is finished */
static int
update_writer_thread_stats(ImportWorkerInfo*info)
{
    ImportJob *job = info->job;
    global_writer_ctx_t *gwctx = job->writer_ctx;
    int finished = (gwctx->last_wqslot >= job->number_indexers);
    int slotidx;

     /*
      * Writer thread loop is finished if most slots have been open and
      * all not delayed slots are closed with empty queue
      */
    if (gwctx->first) {
        finished = 0;
    }
    for (slotidx=0; finished && slotidx < gwctx->last_wqslot; slotidx++) {
        wqslot_t *slot = &gwctx->wqslots[slotidx];
        if (!slot->tmpfile && !slot->closed) {
            finished = 0;
        }
    }
    if (gwctx->weight_in  > 0) {
       gwctx->writer_progress = (gwctx->weight_out * 1.0 / gwctx->weight_in);
    } else {
       gwctx->writer_progress = 0.0;
    }
    dbg_log(__FILE__,__LINE__,__FUNCTION__, DBGMDB_LEVEL_IMPORT, "exiting update_writer_thread_stats. Progress=%f finished=%d", gwctx->writer_progress, finished);
    return finished;
 }

static int
handle_entryrdn_key(backend *be, wqslot_t *slot,
         int(*action)(backend*, dbi_cursor_t*, Slapi_RDN*, ID, back_txn*),
         void* vsrdn, void*vid, back_txn *txn)
 {
    dbi_txn_t *db_txn = (txn != NULL) ? txn->back_txn_txn : NULL;
    Slapi_RDN *srdn = vsrdn;
    ID *id = vid;
    int rc = 0;

    if (!slot->cursor.cur) {
        memset(&slot->cursor, 0, sizeof slot->cursor);
        rc = dblayer_new_cursor(be, slot->dbi, db_txn, &slot->cursor);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "entryrdn_index_entry",
                    "Failed to make a cursor:%s(%d)\n", dblayer_strerror(rc), rc);
        }
    }
    if (!rc) {
        rc=action(be, &slot->cursor, srdn, *id, txn);
    }
    return rc;
}

static inline int __attribute__((always_inline))
is_sync_op(wqelem_t *item) {
    return (item && (item->flags & WQFL_SYNC_OP));
}

static inline int __attribute__((always_inline))
has_queue_sync_op(global_writer_ctx_t *wctx) {
    return (is_sync_op(wctx->first));
}

static inline int __attribute__((always_inline))
is_queue_full(global_writer_ctx_t *wctx) {
    /* Queue is full if first queued operation is a synchronos one */
    return (wctx->weight_in - wctx->weight_out >= wctx->max_weight);
}

static inline int __attribute__((always_inline))
has_item_in_queue(global_writer_ctx_t *wctx) {
    return (wctx->weight_in - wctx->weight_out >= wctx->min_weight);
}

static inline void __attribute__((always_inline))
push_item_in_queue(global_writer_ctx_t *wctx, wqelem_t *item)
{
    pthread_mutex_lock(&wctx->mutex);
    PR_ASSERT(wctx->weight_in >= wctx->weight_out);
    if (is_sync_op(item)) {
        while (!wctx->aborted && has_queue_sync_op(wctx)) {
            LOG_ELMT_INFO("Waiting until there is space in queue (synchronous operation)", item);
            pthread_cond_wait(&wctx->queue_full_cv, &wctx->mutex);
            LOG_ELMT_INFO("Found space in queue", item);
        }
        wctx->flush_queue = 1;
    } else {
        while (!wctx->aborted && is_queue_full(wctx)) {
            LOG_ELMT_INFO("Waiting until there is space in queue (non synchronous operation)", item);
            pthread_cond_wait(&wctx->queue_full_cv, &wctx->mutex);
            LOG_ELMT_INFO("Found space in queue", item);
        }
    }

    if (wctx->aborted) {
        slapi_ch_free((void**)&item);
    } else {
        if (wctx->first==NULL) {  /* Queue first item */
            LOG_ELMT_INFO("Added as first item", item);
            item->next = NULL;
            wctx->first = wctx->last = item;
        } else if (is_sync_op(item)) {  /* Add item in queue head */
            LOG_ELMT_INFO("Added at head", item);
            item->next = wctx->first;
            wctx->first = item;
        } else {  /* Add item in queue tail */
            LOG_ELMT_INFO("Added at tail", item);
            item->next = NULL;
            wctx->last->next = item;
            wctx->last = item;
        }
        wctx->weight_in += BASE_WEIGHT + item->len;
        if (has_item_in_queue(wctx) || wctx->flush_queue) {
            LOG_ELMT_INFO("Signaling import writer thread that new data may be available", wctx->first);
            pthread_cond_signal(&wctx->data_available_cv);
        }
    }
    pthread_mutex_unlock(&wctx->mutex);
}

static inline wqelem_t * __attribute__((always_inline))
get_items_from_queue(global_writer_ctx_t *wctx)
{
    wqelem_t *item_list = NULL;

    pthread_mutex_lock(&wctx->mutex);
    while (!wctx->aborted && !has_item_in_queue(wctx) && !wctx->flush_queue) {
        dbg_log(__FILE__, __LINE__, __FUNCTION__, DBGMDB_LEVEL_IMPORT, "Queue size is %ld. \n", wctx->weight_in - wctx->weight_out);
        LOG_ELMT_INFO("Waiting until enough data get queued", item_list);
        pthread_cond_wait(&wctx->data_available_cv, &wctx->mutex);
        LOG_ELMT_INFO("Finished waiting until enough data get queued", item_list);
    }
    item_list = wctx->first;
    wctx->first = wctx->last = NULL;
    wctx->weight_out = wctx->weight_in;
    wctx->flush_queue = 0;
    /* Wakes up threads bloqued because queue was full */
    LOG_ELMT_INFO("Broadcasting worker threads that queue may have some space available", item_list);
    pthread_cond_broadcast(&wctx->queue_full_cv);
    pthread_mutex_unlock(&wctx->mutex);
    return item_list;
}


/* replay an item */
static int
wqueue_process_item(ImportWorkerInfo *info, wqelem_t *elmt, dbi_txn_t *txn)
{
    global_writer_ctx_t *gwctx = info->job->writer_ctx;
    ImportJob *job = info->job;
    ldbm_instance *inst = job->inst;
    back_txn btxn = {0};
    wqslot_t *slot = elmt->slot;
    index_update_t iupd;
    MDB_val data;
    MDB_val key;
    dbi_val_t dkey = {0};
    int rc = 0;

    LOG_ELMT("wqueue_process_item", elmt, PR_TRUE);

    if (gwctx->aborted) {
        return -1;
    }
    if (is_sync_op(elmt)) {
        /* Get key and data pointer (anyway ther lifespawn cover the whole synchronous operation) */
        key = ((MDB_val*)(elmt->values))[0];
        data = ((MDB_val*)(elmt->values))[1];
    } else {
        /* Get key and data duplicates (as original value may be gone while doing the operation */
        key.mv_data = elmt->values;
        key.mv_size = elmt->keylen;
        data.mv_data = &elmt->values[key.mv_size];
        data.mv_size = elmt->data_len;
    }
    btxn.back_txn_txn = txn;
    switch (elmt->action) {
        case IMPORT_WRITE_ACTION_CLOSE:
            /* close should not have been writen in the queue */
        default:
            PR_ASSERT(0);
            abort();
        case IMPORT_WRITE_ACTION_ADD_INDEX:
            PR_ASSERT(data.mv_size == sizeof iupd);
            memcpy(&iupd, data.mv_data, data.mv_size);
            if (iupd.disposition && is_sync_op(elmt) == 0) {
                /* iupd.disposition may not be valid (freed in calling thread)
                 * so lets use storage in writing thread slot instead
                 */
                iupd.disposition = &elmt->slot->idl_disposition;
            }
            dblayer_value_set_buffer(inst->inst_be, &dkey, key.mv_data, key.mv_size);
            rc = idl_insert_key(inst->inst_be, slot->dbi, &dkey, iupd.id, &btxn, iupd.a, iupd.disposition);
            break;
        case IMPORT_WRITE_ACTION_DEL_INDEX:
            PR_ASSERT(data.mv_size == sizeof iupd);
            memcpy(&iupd, data.mv_data, data.mv_size);
            dblayer_value_set_buffer(inst->inst_be, &dkey, key.mv_data, key.mv_size);
            rc = idl_delete_key(inst->inst_be, slot->dbi, &dkey, iupd.id, &btxn, iupd.a);
            break;
        case IMPORT_WRITE_ACTION_ADD_VLV:
            rc = MDB_PUT(TXN(txn), slot->dbi->dbi, &key, &data, 0);
            if (rc) {
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_writer",
                    "Failed to add item in %s mdb database. error %d(%s).\n",
                    elmt->slot->dbi->dbname, rc, mdb_strerror(rc));
            }
            break;
        case IMPORT_WRITE_ACTION_DEL_VLV:
            rc = MDB_DEL(TXN(txn), slot->dbi->dbi, &key, &data);
            if (rc) {
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_writer",
                    "Failed to add item in %s mdb database. error %d(%s).\n",
                    elmt->slot->dbi->dbname, rc, mdb_strerror(rc));
            }
            break;
        case IMPORT_WRITE_ACTION_ADD_ENTRYRDN:
            rc = handle_entryrdn_key(inst->inst_be, slot, entryrdn_insert_key, key.mv_data, data.mv_data, &btxn);
            break;
        case IMPORT_WRITE_ACTION_DEL_ENTRYRDN:
            rc = handle_entryrdn_key(inst->inst_be, slot, entryrdn_delete_key, key.mv_data, data.mv_data, &btxn);
            break;
        case IMPORT_WRITE_ACTION_ADD:
            rc = MDB_PUT(TXN(info->txn), elmt->slot->dbi->dbi, &key, &data, 0);
            if (rc) {
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_writer",
                    "Failed to add item in %s mdb database. error %d(%s).\n",
                    elmt->slot->dbi->dbname, rc, mdb_strerror(rc));
            }
            break;
    }

    return rc;
}

/* Open a write txn and write all items from the list */
static int
wqueue_process_queue(ImportWorkerInfo *info, wqelem_t *list, wq_reader_state_t *state)
{
    int rc = 0;
    global_writer_ctx_t *gwctx = info->job->writer_ctx;
    int idslot_entryrdn = dbmdb_get_wqslot(info->job, NULL, WCTX_ENTRYRDN);
    wqslot_t *wqslot_entryrdn = idslot_entryrdn>=0?&gwctx->wqslots[idslot_entryrdn]:NULL;
    wqelem_t *sync_op = NULL;

    rc = dbmdb_start_txn(__FUNCTION__, NULL, 0, &info->txn);
    while (!rc && list) {
        wqelem_t *next = list->next;
        rc = wqueue_process_item(info, list, info->txn);
        if (is_sync_op(list)) {
            sync_op  = list;
        } else {
            /* Free the list element if it is not part of a static buffer */
            if ((!state || (char*)list < state->buf || (char*)list >= state->bufend)) {
                slapi_ch_free((void**)&list);
            }
        }
        list = next;
    }

    if (wqslot_entryrdn && wqslot_entryrdn->cursor.cur) {
        dblayer_cursor_op(&wqslot_entryrdn->cursor, DBI_OP_CLOSE, NULL, NULL);
    }
    rc = dbmdb_end_txn(__FUNCTION__, rc, &info->txn);
    if (sync_op) {
        /* Synchronous operation ==> should store result and wake up the thread waiting for response */
        LOG_ELMT_INFO("Signaling worker thread waiting on synchronous operation response", sync_op);
        pthread_mutex_lock(sync_op->response_mutex);
        sync_op->rc = rc;
        sync_op->flags |= WQFL_SYNC_DONE;
        pthread_cond_signal(sync_op->response_cv);
        pthread_mutex_unlock(sync_op->response_mutex);
    }
    return rc;
}

/* resize the state buffer */
static void
realloc_state(wq_reader_state_t *state, size_t len)
{
    size_t pos = state->cur-state->buf;
    state->buf = slapi_ch_realloc(state->buf, len);
    state->bufend = state->buf+len;
    state->buflimit = state->buf+MAX_WEIGHT;
    state->cur = state->buf+pos;
}

/* read a single item from tmp file and queue it*/
static int
read_elmt_from_tmpfile(ImportJob *job, wqslot_t *slot, wq_reader_state_t *state, wqelem_t **list)
{
    global_writer_ctx_t *wctx = job->writer_ctx;
    wqelem_t *ptelm;
    char *ptkey;
    char *ptnext;
    size_t bytesread;
    size_t len;

    if (state->buf == NULL) {
        realloc_state(state, 2*MAX_WEIGHT);
    }
    ptelm = (wqelem_t*)(state->cur);
    ptkey = (char*)&ptelm[1];
    if (ptkey>= state->buflimit) {
        /* Not eenough space to store the header within the limit ==> tells to flush the queue */
        state->cur = state->buflimit;
        return 0;
    }
    if (fread(ptelm, (sizeof(wqelem_t)), 1, slot->tmpfile)<1) {
        if (ferror(slot->tmpfile)) {
            import_log_notice(job, SLAPI_LOG_ERR, "read_elmt_from_tmpfile",
                    "mdb writer thread failed to read data from tmpfile %s. Error = %d(%s).\n",
                    slot->tmpfilepath, errno, slapd_system_strerror(errno));
            return LDAP_UNWILLING_TO_PERFORM;
        }
        return 0;
    }
    len = ptelm->keylen+ptelm->data_len;
    len += ALIGN_TO_LONG(len);
    ptnext = &ptkey[len];
    if (ptnext>state->bufend) {
        realloc_state(state, ptnext-state->buf);
        ptelm = (wqelem_t*)(state->cur);
        ptkey = (char*)&ptelm[1];
        ptnext = &ptkey[len];
    }
    while (ptkey<ptnext) {
        bytesread = fread(ptelm, 1, len, slot->tmpfile);
        if (bytesread<1) {
            import_log_notice(job, SLAPI_LOG_ERR, "read_elmt_from_tmpfile",
                    "mdb writer thread failed to read %d bytes from tmp file %s. Error = %d(%s).\n",
                    len, slot->tmpfilepath, errno, slapd_system_strerror(errno));
            return LDAP_UNWILLING_TO_PERFORM;
        }
        ptkey += bytesread;
        len -= bytesread;
    }
    push_item_in_queue(wctx, ptelm);
    state->cur = ptnext;
    return 0;
}


/* Read the elements pushed in tmpfiles and process them */
static int
handle_delayed_slots(ImportWorkerInfo*info)
{
    ImportJob*job = info->job;
    global_writer_ctx_t *gwctx = job->writer_ctx;
    int slotidx;
    wq_reader_state_t state = {0};
    wqelem_t *list = NULL;
    int rc = 0;

    /*
     * Thread is finished if most slots have been open and
     * all not delayed slots are closed
     */
    for(slotidx = 0; !rc && slotidx<gwctx->last_wqslot; slotidx++) {
        wqslot_t*slot = &gwctx->wqslots[slotidx];
        if (slot->tmpfile) {
            rewind(slot->tmpfile);
            while (!rc && !feof(slot->tmpfile)) {
                if (ferror(slot->tmpfile)) {
                    rc = 1;
                    break;
                }
                rc = read_elmt_from_tmpfile(job, slot, &state, &list);
                if (rc) {
                    break;
                }
                if (list&&state.cur>= state.buflimit) {
                    rc = wqueue_process_queue(info, list, &state);
                    state.cur = state.buf;
                }
            }
            fclose(slot->tmpfile);
            if (list&&state.cur>= state.buflimit) {
                rc = wqueue_process_queue(info, list, &state);
            }
        }
    }
    return rc;
}

/* writer thread */

/* writer.thread:
 * i go through the writer queue (unlike the other worker threads),
 * i'm responsible to write data in mdb database as I am the only
 * import thread allowed to start a read-write transaction.
 * (The other threads open read-only txn)
 */
void
dbmdb_import_writer(void*param)
{
    ImportWorkerInfo*info = (ImportWorkerInfo*)param;
    ImportJob*job = info->job;
    global_writer_ctx_t *gwctx = job->writer_ctx;
    wqelem_t *item_list = NULL;
    PRIntervalTime sleeptime;
    int finished = 0;

    sleeptime = PR_MillisecondsToInterval(import_sleep_time);
    if (job->flags&FLAG_ABORT) {
        goto error;
    }

    info->state = RUNNING;

    while (!finished) {
        if (job->flags&FLAG_ABORT) {
            goto error;
        }
        while((info->command == PAUSE) &&
                (info->command != STOP) && (info->command != ABORT) &&
                !(job->flags&FLAG_ABORT)) {
            /* Check to see if we've been told to stop */
            info->state = WAITING;
            DS_Sleep(sleeptime);
        }
        if (info->command == STOP) {
            finished = 1;
            continue;
        }
        if (job->flags & FLAG_ABORT) {
            goto error;
        }

        info->state = RUNNING;

        /* Handles synchronous operations first */
        item_list = get_items_from_queue(gwctx);
        wqueue_process_queue(info, item_list, NULL);
        finished = update_writer_thread_stats(info);
    }
    if (handle_delayed_slots(info)) {
        goto error;
    }

    info->state = FINISHED;
    return;

error:
    info->state = ABORTED;
    gwctx->aborted = ABORTED;
    /* Let flush the operations queue */
    item_list = get_items_from_queue(gwctx);
    wqueue_process_queue(info, item_list, NULL);
}

/* Does not create the dbi (that is done earlier when calling dbmdb_open_all_files())
 * But it creates the associated queue slot in which operations get pushed to the writer thread
 */
int
dbmdb_import_writer_create_dbi(ImportWorkerInfo *info, dbmdb_wctx_id_t wctx_id, const char *filename, PRBool delayed)
{
    pseudo_back_txn_t **ptxn = dbmdb_get_ptwctx(info->job, info, wctx_id);
    global_writer_ctx_t *gwctx = info->job->writer_ctx;
    long slot = PR_ATOMIC_INCREMENT(&gwctx->last_wqslot);
    ldbm_instance *inst = info->job->inst;
    dbmdb_ctx_t *ctx = MDB_CONFIG(inst->inst_li);
    wqslot_t *wqslot;
    int rc;

    if (slot >= gwctx->max_wqslots) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_writer_create_dbi",
                "Failedtocreateanimportwriterdbinstanceslot, (Toomanyopenslots)\n");
        PR_ATOMIC_DECREMENT(&gwctx->last_wqslot);
        return -1;
    }
    *ptxn = dbmdb_new_wctx(info->job, info, wctx_id);
    slot--;
    (*ptxn)->wqslot = slot;
    wqslot = &gwctx->wqslots[slot];
    if (delayed) {
        wqslot->tmpfilepath = slapi_ch_smprintf("%s/%s/%s.mdbimport", ctx->home, inst->inst_name, filename);
        wqslot->tmpfile = fopen(wqslot->tmpfilepath, "w+");
        if (!wqslot->tmpfile) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_writer_create_dbi",
                    "Failedtocreatetemporaryimportfile%s.Erroris%d(%s)\n", wqslot->tmpfilepath, errno, slapd_system_strerror(errno));
            return -1;
        }
    }
    /* Lets associate the slot and the dbi */
    rc = dbmdb_open_dbi_from_filename(&wqslot->dbi, info->job->inst->inst_be, filename, NULL, MDB_OPEN_DIRTY_DBI);
    return rc;
}

/* Perform a synchronous write operation */
int
dbmdb_import_sync_write(ImportJob*job, long wqslot, dbmdb_waction_t action, MDB_val *key, MDB_val *data)
{
    pthread_mutex_t response_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t response_cv = PTHREAD_COND_INITIALIZER;
    global_writer_ctx_t* gwctx = job->writer_ctx;
    wqelem_t *elmt = NULL;
    size_t elmt_len = 0;
    int rc = LDAP_SUCCESS;

    if (wqslot<0 || wqslot >= gwctx->last_wqslot) {
        return LDAP_UNWILLING_TO_PERFORM;   /* Slot is not open */
    }
    elmt_len = offsetof(wqelem_t, values)+2*sizeof(MDB_val);
    elmt = (wqelem_t*)slapi_ch_malloc(elmt_len);
    elmt->action = action;
    elmt->keylen = key->mv_size;
    elmt->data_len = data->mv_size;
    elmt->flags = WQFL_SYNC_OP;
    elmt->slot = &gwctx->wqslots[wqslot];
    elmt->next = NULL;
    elmt->len = elmt_len;
    elmt->response_mutex = &response_mutex;
    elmt->response_cv = &response_cv;
    /* Do not bother to copy the key data and value data in synchronous mode */
    ((MDB_val*)(&elmt->values[0]))[0] = *key;
    ((MDB_val*)(&elmt->values[0]))[1] = *data;
    elmt->rc = -1;

    /* queue the element at head of list */
    LOG_ELMT("dbmdb_import_sync_write", elmt, PR_TRUE);
    push_item_in_queue(gwctx, elmt);

    /* Wait for response */
    LOG_ELMT_INFO("Waiting for synchronous operation response", elmt);
    pthread_mutex_lock(&response_mutex);
    while (!gwctx->aborted && !(job->flags&FLAG_ABORT) && !(elmt->flags & WQFL_SYNC_DONE)) {
        pthread_cond_wait(&response_cv, &response_mutex);
    }
    LOG_ELMT_INFO("Got response from synchronous operation response", elmt);
    pthread_mutex_unlock(&response_mutex);
    if ((job->flags & FLAG_ABORT) || gwctx->aborted) {
        elmt->rc = -1;
        goto err;
    }
err:
    rc = elmt->rc;
    slapi_ch_free((void**)&elmt);
    return rc;
}

/* Queue an asynchronous write operation for the writer thread */
int
dbmdb_import_write_push(ImportJob*job, long wqslot, dbmdb_waction_t action, MDB_val*key, MDB_val*data)
{
    global_writer_ctx_t *gwctx = job->writer_ctx;
    wqslot_t *slot = &gwctx->wqslots[wqslot];
    wqelem_t *elmt = NULL;
    size_t elmt_len = 0;
    int rc = LDAP_SUCCESS;

    if (wqslot<0||wqslot>= gwctx->last_wqslot) {
        return LDAP_UNWILLING_TO_PERFORM; /* Slot is not open */
    }
    if (action == IMPORT_WRITE_ACTION_CLOSE) {
        slot->closed = 1;
        if (slot->tmpfile) {
            rc = fflush(slot->tmpfile);
        }
        pthread_mutex_lock(&gwctx->mutex);
        gwctx->flush_queue = 1;
        pthread_cond_signal(&gwctx->data_available_cv);
        pthread_mutex_unlock(&gwctx->mutex);
        return rc;
    }
    elmt_len = offsetof(wqelem_t, values)+key->mv_size+data->mv_size;
    elmt_len += ALIGN_TO_LONG(elmt_len);
    elmt = (wqelem_t*)slapi_ch_malloc(elmt_len);
    elmt->action = action;
    elmt->keylen = key->mv_size;
    elmt->data_len = data->mv_size;
    elmt->flags = 0;
    elmt->slot = slot;
    elmt->len = elmt_len;
    if (key->mv_size>0) {
        memcpy(&elmt->values[0], key->mv_data, key->mv_size);
    }
    if (data->mv_size>0) {
        memcpy(&elmt->values[key->mv_size], data->mv_data, data->mv_size);
    }

    LOG_ELMT("dbmdb_import_write_push", elmt, PR_TRUE);
    if (slot->tmpfile) {
        /* delayed mode: Need to serialize to write data in temporary file */
        pthread_mutex_lock(&gwctx->mutex);
        if (fwrite(elmt, elmt_len, 1, slot->tmpfile) != 1) {
            import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_write_push",
                    "Failed to write %ld bytes in import temporary file %s. Errror = %d(%s).\n",
                    elmt_len, slot->tmpfilepath, errno, slapd_system_strerror(errno));
            rc = -1;
        }
        pthread_mutex_unlock(&gwctx->mutex);
        slapi_ch_free((void**)&elmt);
        return rc;
    }

    /* Normal mode: use in memory queue */
    /* flowcontrol: Wait until queue get small enough */
    push_item_in_queue(gwctx, elmt);
    return rc;
}

/* back_txn special action callback */
int
dbmdb_back_special_handling(backend *be, back_txn_action action, dbi_db_t *db, dbi_val_t *key, dbi_val_t *data, back_txn *txn)
{
    const pseudo_back_txn_t* btxn = (const pseudo_back_txn_t*)txn;
    MDB_val mkey;
    MDB_val mdata;

    mkey.mv_data = key->data;
    mkey.mv_size = key->size;
    mdata.mv_data = data->data;
    mdata.mv_size = data->size;
    switch (action) {
        case BTXNACT_INDEX_ADD:
            return dbmdb_import_write_push(btxn->job, btxn->wqslot, IMPORT_WRITE_ACTION_ADD_INDEX, &mkey, &mdata);
        case BTXNACT_INDEX_DEL:
            return dbmdb_import_write_push(btxn->job, btxn->wqslot, IMPORT_WRITE_ACTION_DEL_INDEX, &mkey, &mdata);
        case BTXNACT_VLV_ADD:
            return dbmdb_import_write_push(btxn->job, btxn->wqslot, IMPORT_WRITE_ACTION_ADD_VLV, &mkey, &mdata);
        case BTXNACT_VLV_DEL:
            return dbmdb_import_write_push(btxn->job, btxn->wqslot, IMPORT_WRITE_ACTION_DEL_VLV, &mkey, &mdata);
        case BTXNACT_ENTRYRDN_ADD:
            return dbmdb_import_sync_write(btxn->job, btxn->wqslot, IMPORT_WRITE_ACTION_ADD_ENTRYRDN, &mkey, &mdata);
        case BTXNACT_ENTRYRDN_DEL:
            return dbmdb_import_sync_write(btxn->job, btxn->wqslot, IMPORT_WRITE_ACTION_DEL_ENTRYRDN, &mkey, &mdata);
        case BTXNACT_ID2ENTRY_ADD:
            return dbmdb_import_sync_write(btxn->job, btxn->wqslot, IMPORT_WRITE_ACTION_ADD, &mkey, &mdata);
        default:
            PR_ASSERT(0);
            abort();
    }
}

/* Get pseudo txn from job/workinfo context */
static pseudo_back_txn_t *
dbmdb_new_wctx(ImportJob *job, ImportWorkerInfo *info, dbmdb_wctx_id_t wctx_id)
{
    pseudo_back_txn_t *btxn = (pseudo_back_txn_t*)slapi_ch_calloc(sizeof(pseudo_back_txn_t), 1);

    btxn->txn.back_special_handling_fn = dbmdb_back_special_handling;
    btxn->txn.back_txn_txn = info->txn;
    btxn->job = job;
    btxn->info = info;
    btxn->wctx_id = wctx_id;
    btxn->wqslot = -1;
    return btxn;
}

/* Get pseudo txn anchor in job/workinfo context */
static pseudo_back_txn_t **
dbmdb_get_ptwctx(ImportJob *job, ImportWorkerInfo *info, dbmdb_wctx_id_t wctx_id)
{
    if (wctx_id == WCTX_GENERIC) {
        return (pseudo_back_txn_t**)&info->writer_ctx;
    } else {
        return (pseudo_back_txn_t**)&((global_writer_ctx_t*)job->writer_ctx)->predefined_wctx[wctx_id];
    }
}

back_txn*
dbmdb_get_wctx(ImportJob*job, ImportWorkerInfo*info, dbmdb_wctx_id_t wctx_id)
{
    return(back_txn*)*dbmdb_get_ptwctx(job, info, wctx_id);
}

static void
dbmdb_free_wctx(ImportJob*job, ImportWorkerInfo*info, dbmdb_wctx_id_t wctx_id)
{
    pseudo_back_txn_t**txn = dbmdb_get_ptwctx(job, info, wctx_id);
    slapi_ch_free((void*)txn);
}

static long
dbmdb_get_wqslot(ImportJob*job, ImportWorkerInfo*info, dbmdb_wctx_id_t wctx_id)
{
    pseudo_back_txn_t**txn = dbmdb_get_ptwctx(job, info, wctx_id);
    return *txn ? (*txn)->wqslot : -1;
}

void
dbmdb_writer_init(ImportJob*job)
{
    global_writer_ctx_t *gwctx = (global_writer_ctx_t*)slapi_ch_calloc(sizeof*gwctx, 1);
    ldbm_instance *inst = job->inst;
    struct ldbminfo*li = inst->inst_be->be_database->plg_private;
    dbmdb_ctx_t *ctx = MDB_CONFIG(li);
    dbmdb_dbi_t *dummydbi = NULL;

    job->writer_ctx = gwctx;

    gwctx->max_wqslots = ctx->startcfg.max_dbs;
    gwctx->wqslots = (wqslot_t*)slapi_ch_calloc(sizeof gwctx->wqslots[0], gwctx->max_wqslots);
    gwctx->writer_progress = 0.0;
    gwctx->min_weight = MIN_WEIGHT;
    gwctx->max_weight = MAX_WEIGHT;
    gwctx->flush_queue = 0;
    pthread_mutex_init(&gwctx->mutex, NULL);
    pthread_cond_init(&gwctx->data_available_cv, NULL);
    pthread_cond_init(&gwctx->queue_full_cv, NULL);


    /* Here, it is still possible to open write txn
    *  so it seems a good place to open all dbis on which we plan to do
    * some read access from worker threads or from foreman thread.
    * (The dbi must be open before these threads get created and
    * starts a read txn (otherwise the dbi is seen as missing
    * within the txn)
    */
    dbmdb_open_dbi_from_filename(&dummydbi, inst->inst_be, LDBM_PARENTID_STR, NULL, MDB_CREATE|MDB_MARK_DIRTY_DBI);
    if (entryrdn_get_switch()) {
        dbmdb_open_dbi_from_filename(&dummydbi, inst->inst_be, LDBM_ENTRYRDN_STR, NULL, MDB_CREATE|MDB_MARK_DIRTY_DBI);
    } else {
        dbmdb_open_dbi_from_filename(&dummydbi, inst->inst_be, LDBM_ENTRYDN_STR, NULL, MDB_CREATE|MDB_MARK_DIRTY_DBI);
    }
    dbmdb_open_dbi_from_filename(&dummydbi, inst->inst_be, ID2ENTRY, NULL, MDB_CREATE|MDB_MARK_DIRTY_DBI);
}

void
dbmdb_writer_cleanup(ImportJob *job)
{
    global_writer_ctx_t *gwctx = job->writer_ctx;
    wqslot_t *slot;
    int i;

    for(i = 0; i<gwctx->last_wqslot; i++) {
        slot = &gwctx->wqslots[i];
        if (slot->tmpfile) {
            fclose(slot->tmpfile);
            unlink(slot->tmpfilepath);
        }
        slapi_ch_free_string(&slot->tmpfilepath);
    }
    slapi_ch_free((void**)&gwctx->wqslots);
    pthread_mutex_destroy(&gwctx->mutex);
    pthread_cond_destroy(&gwctx->data_available_cv);
    pthread_cond_destroy(&gwctx->queue_full_cv);
    slapi_ch_free(&job->writer_ctx);
}

/* Get writer thread progress statistic */
double
dbmdb_writer_get_progress(ImportJob *job)
{
    global_writer_ctx_t*gwctx = job->writer_ctx;
    return gwctx ? gwctx->writer_progress : 0.0;
}

void dbmdb_writer_wakeup(ImportJob *job)
{
    global_writer_ctx_t *gwctx = job->writer_ctx;
    LOG_ELMT_INFO("Signaling import writer thread that new data may be available", NULL);
    pthread_cond_signal(&gwctx->data_available_cv);
}


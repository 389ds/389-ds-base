/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * the threads that make up an import:
 * producer (1)
 * foreman (1)
 * worker (N: 1 for each index)
 *
 * a wire import (aka "fast replica" import) won't have a producer thread.
 */

#include "back-ldbm.h"
#include "vlv_srch.h"
#include "import.h"
#ifdef XP_WIN32
#define STDIN_FILENO 0
#endif

static void import_wait_for_space_in_fifo(ImportJob *job, size_t new_esize);

static struct backentry *import_make_backentry(Slapi_Entry *e, ID id)
{
    struct backentry *ep = backentry_alloc();

    if (NULL != ep) {
	ep->ep_entry = e;
	ep->ep_id = id;
    }
    return ep;
}

static void import_decref_entry(struct backentry *ep)
{
    PR_AtomicDecrement(&(ep->ep_refcnt));
    PR_ASSERT(ep->ep_refcnt >= 0);
}

/* generate uniqueid if requested */
static void import_generate_uniqueid(ImportJob *job, Slapi_Entry *e)
{
    const char *uniqueid = slapi_entry_get_uniqueid(e);
    int rc;

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
            slapi_entry_set_uniqueid (e, newuniqueid);
        } else {
            char ebuf[BUFSIZ];
            LDAPDebug( LDAP_DEBUG_ANY,
                       "import_generate_uniqueid: failed to generate "
                       "uniqueid for %s; error=%d.\n", 
                       escape_string(slapi_entry_get_dn_const(e), ebuf), rc, 0 );
        }                
    }
}


/**********  BETTER LDIF PARSER  **********/


/* like the function in libldif, except this one doesn't need to use
 * FILE (which breaks on various platforms for >4G files or large numbers
 * of open files)
 */
#define LDIF_BUFFER_SIZE 8192

typedef struct {
    char *b;		/* buffer */
    size_t size;	/* how full the buffer is */
    size_t offset;	/* where the current entry starts */
} ldif_context;

static void import_init_ldif(ldif_context *c)
{
    c->size = c->offset = 0;
    c->b = NULL;
}

static void import_free_ldif(ldif_context *c)
{
    if (c->b)
	FREE(c->b);
    import_init_ldif(c);
}

static char *import_get_entry(ldif_context *c, int fd, int *lineno)
{
    int ret;
    int done = 0, got_lf = 0;
    size_t bufSize = 0, bufOffset = 0, i;
    char *buf = NULL;

    while (!done) {

	/* If there's no data in the buffer, get some */
	if ((c->size == 0) || (c->offset == c->size)) {
	    /* Do we even have a buffer ? */
	    if (! c->b) {
		c->b = slapi_ch_malloc(LDIF_BUFFER_SIZE);
		if (! c->b)
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
                if (!(*p == '\r' || *p == '\n' || *p == ' '|| *p == '\t')) 
                    break;
            }
            c->offset = n;
            if (c->offset == c->size) continue;
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
	    size_t newsize = (buf ? bufSize*2 : LDIF_BUFFER_SIZE);

	    newbuf = slapi_ch_malloc(newsize);
	    if (! newbuf)
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
import_get_version(char *str)
{
    char *s;
    char *type;
    char *valuecharptr;
    char *mystr, *ms;
    int offset;
#if defined(USE_OPENLDAP)
    ber_len_t valuelen;
#else
    int	valuelen;
#endif
    int my_version = 0;
    int retmalloc = 0;

    if ((s = strstr(str, "version:")) == NULL)
	return 0;

    offset = s - str;
    mystr = ms = slapi_ch_strdup(str);
    while ( (s = ldif_getline( &ms )) != NULL ) {
	if ( (retmalloc = ldif_parse_line( s, &type, &valuecharptr, &valuelen )) >= 0 ) {
	    if (!strcasecmp(type, "version")) {
		my_version = atoi(valuecharptr);
		*(str + offset) = '#';
		/* the memory below was not allocated by the slapi_ch_ functions */
		if (retmalloc) slapi_ch_free((void **) &valuecharptr);
		break;
	    } 
	}
	/* the memory below was not allocated by the slapi_ch_ functions */
	if (retmalloc) slapi_ch_free((void **) &valuecharptr);
    }

    slapi_ch_free((void **)&mystr);
    return my_version;
}

/*
 * add CreatorsName, ModifiersName, CreateTimestamp, ModifyTimestamp to entry
 */
static void
import_add_created_attrs(Slapi_Entry *e)
{
    char          buf[20];
    struct berval bv;
    struct berval *bvals[2];
    time_t        curtime;
    struct tm     ltm;

    bvals[0] = &bv;
    bvals[1] = NULL;
    
    bv.bv_val = "";
    bv.bv_len = 0;
    if ( !attrlist_find(e->e_attrs,"creatorsname") ) {
        slapi_entry_attr_replace(e, "creatorsname", bvals);
    }
    if ( !attrlist_find(e->e_attrs,"modifiersname") ) {
        slapi_entry_attr_replace(e, "modifiersname", bvals);
    }

    curtime = current_time();
#ifdef _WIN32
{
    struct tm *pt;
    pt = gmtime(&curtime);
    memcpy(&ltm, pt, sizeof(struct tm));
}
#else
    gmtime_r(&curtime, &ltm);
#endif
    strftime(buf, sizeof(buf), "%Y%m%d%H%M%SZ", &ltm);

    bv.bv_val = buf;
    bv.bv_len = strlen(bv.bv_val);
    if ( !attrlist_find(e->e_attrs,"createtimestamp") ) {
        slapi_entry_attr_replace(e, "createtimestamp", bvals);
    }
    if ( !attrlist_find(e->e_attrs,"modifytimestamp") ) {
        slapi_entry_attr_replace(e, "modifytimestamp", bvals);
    }
}

/* producer thread:
 * read through the given file list, parsing entries (str2entry), assigning
 * them IDs and queueing them on the entry FIFO.  other threads will do
 * the indexing.
 */
void import_producer(void *param)
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
    int str2entry_flags =
    SLAPI_STR2ENTRY_TOMBSTONE_CHECK |
    SLAPI_STR2ENTRY_REMOVEDUPVALS |
    SLAPI_STR2ENTRY_EXPAND_OBJECTCLASSES |
    SLAPI_STR2ENTRY_ADDRDNVALS |
    SLAPI_STR2ENTRY_NOT_WELL_FORMED_LDIF;
    int finished = 0;
    int detected_eof = 0;
    int fd, curr_file, curr_lineno;
    char *curr_filename = NULL;
    int idx;
    ldif_context c;
    int my_version = 0;
    size_t newesize = 0;
    Slapi_Attr *attr = NULL;

    PR_ASSERT(info != NULL);
    PR_ASSERT(inst != NULL);
    
    if ( job->flags & FLAG_ABORT ) {
        goto error;
    }

    sleeptime = PR_MillisecondsToInterval(import_sleep_time);

    /* pause until we're told to run */
    while ((info->command == PAUSE) && !(job->flags & FLAG_ABORT)) {
        info->state = WAITING;
        DS_Sleep(sleeptime);
    }
    info->state = RUNNING;
    import_init_ldif(&c);

    /* jumpstart by opening the first file */
    curr_file = 0;
    fd = -1;
    detected_eof = finished = 0;

    /* we loop around reading the input files and processing each entry
     * as we read it.
     */
    while (! finished) {
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
                import_log_notice(job, "WARNING: Unexpected end of file found "
                                  "at line %d of file \"%s\"", curr_lineno,
                                  curr_filename);
            }

            if (fd == STDIN_FILENO) {
                import_log_notice(job, "Finished scanning file stdin (%lu "
                                  "entries)", (u_long)(id-id_filestart));
            } else {
                import_log_notice(job, "Finished scanning file \"%s\" (%lu "
                                  "entries)", curr_filename, (u_long)(id-id_filestart));
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
#ifdef XP_WIN32
                /* 613041 Somehow the windows low level io lose "\n"
                   at a very particular situation using O_TEXT mode read.
                   I think it is a windows bug for O_TEXT mode read.
                   Use O_BINARY instead, which honestly returns chars
                   without any translation.
                */
                o_flag |= O_BINARY;
#endif
                fd = dblayer_open_huge_file(curr_filename, o_flag, 0);
            }
            if (fd < 0) {
                import_log_notice(job, "Could not open LDIF file \"%s\", errno %d (%s)",
                                  curr_filename, errno, slapd_system_strerror(errno));
                goto error;
            }
            if (fd == STDIN_FILENO) {
                import_log_notice(job, "Processing file stdin");
            } else {
                import_log_notice(job, "Processing file \"%s\"", curr_filename);
            }
        }
         if (job->flags & FLAG_ABORT) {   
             goto error;
         }


        while ((info->command == PAUSE)  && !(job->flags & FLAG_ABORT)){
            info->state = WAITING;
            DS_Sleep(sleeptime);
        }
        info->state = RUNNING;

        prev_lineno = curr_lineno;
        estr = import_get_entry(&c, fd, &curr_lineno);

        lines_in_entry = curr_lineno - prev_lineno;
        if (!estr) {
            /* error reading entry, or end of file */
            detected_eof = 1;
            continue;
        }

        if (0 == my_version && strstr(estr, "version:")) {
            my_version = import_get_version(estr);
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
        e = slapi_str2entry(estr, flags);
        FREE(estr);
        if (! e) {
                if (!(str2entry_flags & SLAPI_STR2ENTRY_INCLUDE_VERSION_STR))
                        import_log_notice(job, "WARNING: skipping bad LDIF entry "
                              "ending line %d of file \"%s\"", curr_lineno,
                              curr_filename);
            continue;
        }
        if (0 == my_version) {
            /* after the first entry version string won't be given */
            my_version = -1;
        }

        if (! import_entry_belongs_here(e, inst->inst_be)) {
            /* silently skip */
            if (e) {
                job->not_here_skipped++;
                slapi_entry_free(e);
            }
            continue;
        }

        if (slapi_entry_schema_check(NULL, e) != 0) {
            char ebuf[BUFSIZ];
            import_log_notice(job, "WARNING: skipping entry \"%s\" which "
                              "violates schema, ending line %d of file "
                              "\"%s\"", escape_string(slapi_entry_get_dn(e), ebuf),
                              curr_lineno, curr_filename);
            if (e) {
                slapi_entry_free(e);
            }

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
        if (syntax_err != 0)
        {
            char ebuf[BUFSIZ];
            import_log_notice(job, "WARNING: skipping entry \"%s\" which "
                              "violates attribute syntax, ending line %d of "
                              "file \"%s\"", escape_string(slapi_entry_get_dn(e), ebuf),
                              curr_lineno, curr_filename);
            if (e) {
                slapi_entry_free(e);
            }

            job->skipped++;
            continue;
        }

        /* generate uniqueid if necessary */
        import_generate_uniqueid(job, e);
        if (g_get_global_lastmod()) {
            import_add_created_attrs(e);
        }

        ep = import_make_backentry(e, id);
        if (!ep)
            goto error;

        /* check for include/exclude subtree lists */
        if (! ldbm_back_ok_to_dump(backentry_get_ndn(ep),
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

            pw_encodevals( (Slapi_Value **)va ); /* jcm - cast away const */
        }

        if (job->flags & FLAG_ABORT) { 
            backentry_free(&ep);
            goto error;
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
                    (old_ep->ep_id >= job->ready_EID))
                   && (info->command != ABORT) && !(job->flags & FLAG_ABORT)) {
                info->state = WAITING;
                DS_Sleep(sleeptime);
            }
            if (job->flags & FLAG_ABORT){
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
        if (newesize > job->fifo.bsize) {    /* entry too big */
            char ebuf[BUFSIZ];
            import_log_notice(job, "WARNING: skipping entry \"%s\" "
                    "ending line %d of file \"%s\"",
                    escape_string(slapi_entry_get_dn(e), ebuf),
                    curr_lineno, curr_filename);
            import_log_notice(job, "REASON: entry too large (%ld bytes) for "
                    "the buffer size (%lu bytes)", newesize, job->fifo.bsize);
            backentry_free(&ep);
            job->skipped++;
            continue;
        }
        /* Now check if fifo has enough space for the new entry */
        if ((job->fifo.c_bsize + newesize) > job->fifo.bsize) {
            import_wait_for_space_in_fifo( job, newesize );
        }

        /* We have enough space */
        job->fifo.item[idx].filename = curr_filename;
        job->fifo.item[idx].line = curr_lineno;
        job->fifo.item[idx].entry = ep;
        job->fifo.item[idx].bad = 0;
        job->fifo.item[idx].esize = newesize;

        /* Add the entry size to total fifo size */
        job->fifo.c_bsize += ep->ep_entry? job->fifo.item[idx].esize : 0;

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
        if (job->flags & FLAG_ABORT){
            goto error;
        }
        if (info->command == STOP) {
            if (fd >= 0)
                close(fd);
            finished = 1;
        }
    }

    import_free_ldif(&c);
    info->state = FINISHED;
    return;

error:
    info->state = ABORTED;
}

/* producer thread for re-indexing:
 * read id2entry, parsing entries (str2entry) (needed???), assigning
 * them IDs (again, needed???) and queueing them on the entry FIFO. 
 * other threads will do the indexing -- same as in import.
 */
void index_producer(void *param)
{
    ImportWorkerInfo *info = (ImportWorkerInfo *)param;
    ImportJob *job = info->job;
    ID id = job->first_ID;
    Slapi_Entry *e = NULL;
    struct backentry *ep = NULL, *old_ep = NULL;
    ldbm_instance *inst = job->inst;
    PRIntervalTime sleeptime;
    int finished = 0;
    int idx;

    /* vars for Berkeley DB */
    DB_ENV *env = NULL;
    DB *db = NULL;
    DBC *dbc = NULL;
    DBT key = {0};
    DBT data = {0};
    int db_rval = -1;
    backend *be = inst->inst_be;
    int isfirst = 1;
    int curr_entry = 0;
    size_t newesize = 0;

    PR_ASSERT(info != NULL);
    PR_ASSERT(inst != NULL);
    PR_ASSERT(be != NULL);
    
    if ( job->flags & FLAG_ABORT )
        goto error;

    sleeptime = PR_MillisecondsToInterval(import_sleep_time);

    /* pause until we're told to run */
    while ((info->command == PAUSE) && !(job->flags & FLAG_ABORT)) {
        info->state = WAITING;
        DS_Sleep(sleeptime);
    }
    info->state = RUNNING;

    /* open id2entry with dedicated db env and db handler */
    if ( dblayer_get_aux_id2entry( be, &db, &env ) != 0  || db == NULL ||
         env == NULL) {
        LDAPDebug( LDAP_DEBUG_ANY, "Could not open id2entry\n", 0, 0, 0 );
        goto error;
    }

    /* get a cursor to we can walk over the table */
    db_rval = db->cursor(db, NULL, &dbc, 0);
    if ( 0 != db_rval ) {
        LDAPDebug( LDAP_DEBUG_ANY,
                   "Failed to get cursor for reindexing\n", 0, 0, 0 );
        dblayer_release_id2entry(be, db);
        goto error;
    }

    /* we loop around reading the input files and processing each entry
     * as we read it.
     */
    finished = 0;
    while (!finished) {
        ID temp_id;

        if (job->flags & FLAG_ABORT) {   
            goto error;
        }
        while ((info->command == PAUSE)  && !(job->flags & FLAG_ABORT)){
            info->state = WAITING;
            DS_Sleep(sleeptime);
        }
        info->state = RUNNING;

        key.flags = DB_DBT_MALLOC;
        data.flags = DB_DBT_MALLOC;
        if (isfirst)
        {
            db_rval = dbc->c_get(dbc, &key, &data, DB_FIRST);
            isfirst = 0;
        }
        else
        {
            db_rval = dbc->c_get(dbc, &key, &data, DB_NEXT);
        }
        
        if (0 != db_rval) {
            if (DB_NOTFOUND != db_rval) {
                LDAPDebug(LDAP_DEBUG_ANY, "%s: Failed to read database, "
                    "errno=%d (%s)\n", inst->inst_name, db_rval,
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
        temp_id = id_stored_to_internal((char *)key.data);
        slapi_ch_free(&(key.data));

        /* call post-entry plugin */
        plugin_call_entryfetch_plugins((char **) &data.dptr, &data.dsize);
        e = slapi_str2entry(data.data, 0);
        if ( NULL == e ) {
            if (job->task) {
                slapi_task_log_notice(job->task,
                    "%s: WARNING: skipping badly formatted entry (id %lu)",
                    inst->inst_name, (u_long)temp_id);
            }
            LDAPDebug(LDAP_DEBUG_ANY,
                "%s: WARNING: skipping badly formatted entry (id %lu)\n",
                inst->inst_name, (u_long)temp_id, 0);
            continue;
        } 
        slapi_ch_free(&(data.data));

        ep = import_make_backentry(e, temp_id);
        if (!ep)
            goto error;

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
                    (old_ep->ep_id >= job->ready_EID))
                   && (info->command != ABORT) && !(job->flags & FLAG_ABORT)) {
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
        if (newesize > job->fifo.bsize) {    /* entry too big */
            char ebuf[BUFSIZ];
            import_log_notice(job, "WARNING: skipping entry \"%s\"",
                    escape_string(slapi_entry_get_dn(e), ebuf));
            import_log_notice(job, "REASON: entry too large (%lu bytes) for "
                    "the buffer size (%lu bytes)", newesize, job->fifo.bsize);
            backentry_free(&ep);
            job->skipped++;
            continue;
        }
        /* Now check if fifo has enough space for the new entry */
        if ((job->fifo.c_bsize + newesize) > job->fifo.bsize) {
            import_wait_for_space_in_fifo( job, newesize );
        }

        /* We have enough space */
        job->fifo.item[idx].filename = ID2ENTRY LDBM_FILENAME_SUFFIX;
        job->fifo.item[idx].line = curr_entry;
        job->fifo.item[idx].entry = ep;
        job->fifo.item[idx].bad = 0;
        job->fifo.item[idx].esize = newesize;

        /* Add the entry size to total fifo size */
        job->fifo.c_bsize += ep->ep_entry? job->fifo.item[idx].esize : 0;

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
        if (info->command == STOP)
        {
            finished = 1;
        }
    }

    dbc->c_close(dbc);
    dblayer_release_aux_id2entry( be, db, env );
    info->state = FINISHED;
    return;

error:
    dbc->c_close(dbc);
    dblayer_release_aux_id2entry( be, db, env );
    info->state = ABORTED;
}

struct upgradedn_attr {
    char *ud_type;
    char *ud_value;
    struct upgradedn_attr *ud_next;
    int ud_flags;
#define OLD_DN_NORMALIZE 0x1
};

static void
upgradedn_free_list(struct upgradedn_attr **ud_list)
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
upgradedn_add_to_list(struct upgradedn_attr **ud_list, 
                      char *type, char *value, int flag)
{
    struct upgradedn_attr *elem =
       (struct upgradedn_attr *) slapi_ch_malloc(sizeof(struct upgradedn_attr));
    elem->ud_type = type;
    elem->ud_value = value;
    elem->ud_flags = flag;
    elem->ud_next = *ud_list;
    *ud_list = elem;
    return;
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
 *     deleted attribute list e_deleted_attrs.
 *
 * If FLAG_UPGRADEDNFORMAT is set, worker_threads for indexing DN syntax
 * attributes (+ cn & ou) are brought up.  Foreman thread updates entrydn
 * index as well as the entry itself in the id2entry.db#.
 *
 * Note: QUIT state for info->state is introduced for DRYRUN mode to
 *       distinguish the intentional QUIT (found the dn upgrade candidate)
 *       from ABORTED (aborted or error) and FINISHED (scan all the entries
 *       and found no candidate to upgrade)
 */
void 
upgradedn_producer(void *param)
{
    ImportWorkerInfo *info = (ImportWorkerInfo *)param;
    ImportJob *job = info->job;
    ID id = job->first_ID;
    Slapi_Entry *e = NULL;
    struct backentry *ep = NULL, *old_ep = NULL;
    ldbm_instance *inst = job->inst;
    PRIntervalTime sleeptime;
    int finished = 0;
    int idx;
    int rc = 0;
    Slapi_Attr *a = NULL;
    Slapi_DN *sdn = NULL;
    char *workdn = NULL;
    int doit = 0;
    int skipit = 0;
    int isentrydn = 0;
    Slapi_Value *value = NULL;
    struct upgradedn_attr *ud_list = NULL;
    char **ud_vals = NULL;
    char **ud_valp = NULL;
    struct upgradedn_attr *ud_ptr = NULL;
    Slapi_Attr *ud_attr = NULL;
    char *ecopy = NULL;

    /* vars for Berkeley DB */
    DB_ENV *env = NULL;
    DB *db = NULL;
    DBC *dbc = NULL;
    DBT key = {0};
    DBT data = {0};
    int db_rval = -1;
    backend *be = inst->inst_be;
    int isfirst = 1;
    int curr_entry = 0;
    size_t newesize = 0;

    PR_ASSERT(info != NULL);
    PR_ASSERT(inst != NULL);
    PR_ASSERT(be != NULL);
    
    if ( job->flags & FLAG_ABORT )
        goto error;

    sleeptime = PR_MillisecondsToInterval(import_sleep_time);

    /* pause until we're told to run */
    while ((info->command == PAUSE) && !(job->flags & FLAG_ABORT)) {
        info->state = WAITING;
        DS_Sleep(sleeptime);
    }
    info->state = RUNNING;

    /* open id2entry with dedicated db env and db handler */
    if ( dblayer_get_aux_id2entry( be, &db, &env ) != 0  || db == NULL ||
         env == NULL) {
        LDAPDebug( LDAP_DEBUG_ANY, "Could not open id2entry\n", 0, 0, 0 );
        goto error;
    }

    /* get a cursor to we can walk over the table */
    db_rval = db->cursor(db, NULL, &dbc, 0);
    if ( 0 != db_rval ) {
        LDAPDebug( LDAP_DEBUG_ANY,
                   "Failed to get cursor for reindexing\n", 0, 0, 0 );
        dblayer_release_id2entry(be, db);
        goto error;
    }

    /* we loop around reading the input files and processing each entry
     * as we read it.
     */
    finished = 0;
    while (!finished) {
        ID temp_id;

        if (job->flags & FLAG_ABORT) {   
            goto error;
        }
        while ((info->command == PAUSE)  && !(job->flags & FLAG_ABORT)){
            info->state = WAITING;
            DS_Sleep(sleeptime);
        }
        info->state = RUNNING;

        key.flags = DB_DBT_MALLOC;
        data.flags = DB_DBT_MALLOC;
        if (isfirst)
        {
            db_rval = dbc->c_get(dbc, &key, &data, DB_FIRST);
            isfirst = 0;
        }
        else
        {
            db_rval = dbc->c_get(dbc, &key, &data, DB_NEXT);
        }
        
        if (0 != db_rval) {
            if (DB_NOTFOUND != db_rval) {
                LDAPDebug(LDAP_DEBUG_ANY, "%s: Failed to read database, "
                    "errno=%d (%s)\n", inst->inst_name, db_rval,
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
        temp_id = id_stored_to_internal((char *)key.data);
        slapi_ch_free(&(key.data));

        /* call post-entry plugin */
        plugin_call_entryfetch_plugins((char **)&data.dptr, &data.dsize);
        ecopy = (char *)slapi_ch_malloc(data.dsize + 1);
        memcpy(ecopy, data.dptr, data.dsize);
        *(ecopy + data.dsize) = '\0';
        e = slapi_str2entry(data.data, SLAPI_STR2ENTRY_USE_OBSOLETE_DNFORMAT);
        if ( NULL == e ) {
            if (job->task) {
                slapi_task_log_notice(job->task,
                    "%s: WARNING: skipping badly formatted entry (id %lu)",
                    inst->inst_name, (u_long)temp_id);
            }
            LDAPDebug(LDAP_DEBUG_ANY,
                "%s: WARNING: skipping badly formatted entry (id %lu)\n",
                inst->inst_name, (u_long)temp_id, 0);
            continue;
        } 
        slapi_ch_free(&(data.data));

        /* Check DN syntax attr values if it contains '\\' or not */
        for (a = e->e_attrs; a; a = a->a_next) {
            if (slapi_attr_is_dn_syntax_attr(a)) { /* is dn syntax attr? */
                rc = get_values_from_string((const char *)ecopy,
                                             a->a_type, &ud_vals);
                if (rc || (NULL == ud_vals)) {
                    continue; /* empty; ignore it */
                }

                for (ud_valp = ud_vals; ud_valp && *ud_valp; ud_valp++) {
                    char **rdns = NULL;
                    char **rdnsp = NULL;
                    char *valueptr = NULL;
                    int valuelen;

                    /* ud_valp contains '\\'.  We have to update the value */
                    if (PL_strchr(*ud_valp, '\\')) {
                        upgradedn_add_to_list(&ud_list, 
                                              slapi_ch_strdup(a->a_type),
                                              slapi_ch_strdup(*ud_valp),
                                              0);
                        LDAPDebug(LDAP_DEBUG_TRACE,
                              "%s: Found upgradedn candidate: %s (id %lu)\n", 
                              inst->inst_name, *ud_valp, (u_long)temp_id);
                        doit = 1;
                        continue;
                    }
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
                    rdns = ldap_explode_dn(workdn, 0);
                    skipit = 0;
                    for (rdnsp = rdns; rdnsp && *rdnsp; rdnsp++) {
                        valueptr = PL_strchr(*rdnsp, '=');
                        if (NULL == valueptr) {
                            skipit = 1;
                            break;
                        }
                        valueptr++;
                        while ((' ' == *valueptr) || ('\t' == *valueptr)) {
                            valueptr++;
                        }
                        valuelen = strlen(valueptr);
                        if (0 == valuelen) {
                            skipit = 1;
                            break;
                        }
                        /* DN contains an RDN <type>="<value>" ? */
                        if (('"' == *valueptr) && 
                            ('"' == *(valueptr + valuelen - 1))) {
                            upgradedn_add_to_list(&ud_list, 
                                                  slapi_ch_strdup(a->a_type),
                                                  slapi_ch_strdup(*ud_valp),
                                                  isentrydn?0:OLD_DN_NORMALIZE);
                            LDAPDebug(LDAP_DEBUG_TRACE,
                                "%s: Found upgradedn candidate: %s (id %lu)\n", 
                                inst->inst_name, valueptr, (u_long)temp_id);
                            doit = 1;
                            break;
                        }
                    }
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
                    LDAPDebug(LDAP_DEBUG_ANY, "%s: WARNING: skipping an entry "
                              "with a corrupted dn (syntax value): %s "
                              "(id %lu)\n",
                              inst->inst_name, 
                              workdn?workdn:"unknown", (u_long)temp_id);
                    slapi_ch_free_string(&workdn);
                    upgradedn_free_list(&ud_list);
                    break;
                }
            } /* if (slapi_attr_is_dn_syntax_attr(a)) */
        } /* for (a = e->e_attrs; a; a = a->a_next)  */
        slapi_ch_free_string(&ecopy);
        if (skipit) {
            upgradedn_free_list(&ud_list);
            slapi_entry_free(e); e = NULL;
            continue;
        }

        if (!doit) {
            /* We don't have to update dn syntax values. */
            upgradedn_free_list(&ud_list);
            slapi_entry_free(e); e = NULL;
            continue;
        }
        
        /* doit */
        if (job->flags & FLAG_DRYRUN) {
            /* We can return SUCCESS (== found upgrade dn candidates) */
            finished = 0; /* make it sure ... */
            upgradedn_free_list(&ud_list);
            slapi_entry_free(e); e = NULL;
            goto bail;
        }

        skipit = 0;
        for (ud_ptr = ud_list; ud_ptr; ud_ptr = ud_ptr->ud_next) {
            /* Move the current value to e_aux_attrs. */
            ud_attr = attrlist_find(e->e_attrs, ud_ptr->ud_type);
            if (ud_attr) {
                if (0 == strcmp(ud_ptr->ud_type, "entrydn")) {
                    /* entrydn contains half normalized value in id2entry,
                       thus we have to replace it in id2entry.  
                       The other DN syntax attribute values store 
                       the originals.  They are taken care by the normalizer.
                     */
                    attrlist_remove(&e->e_attrs, ud_ptr->ud_type);
                    rc = slapi_attr_first_value(ud_attr, &value);
                    if (rc < 0) {
                        LDAPDebug(LDAP_DEBUG_ANY,
                            "%s: WARNING: skipping an entry with no entrydn: "
                            "%s (id %lu)\n",
                            inst->inst_name, ud_ptr->ud_value, (u_long)temp_id);
                        skipit = 1;
                        break;
                    } else {
                        rc = slapi_value_set_string(value, ud_ptr->ud_value);
                        if (rc) {
                            LDAPDebug(LDAP_DEBUG_ANY,
                                  "%s: WARNING: skipping an entry; "
                                  "failed to replace entrydn: %s "
                                  "(id %lu)\n", inst->inst_name, 
                                ud_ptr->ud_value, (u_long)temp_id);
                            skipit = 1;
                            break;
                        }
                    }
                    attrlist_add(&e->e_aux_attrs, ud_attr);
                } else {
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
        upgradedn_free_list(&ud_list);
        if (skipit) {
            slapi_entry_free(e); e = NULL;
            continue;
        }

        ep = import_make_backentry(e, temp_id);
        if (!ep) {
            slapi_entry_free(e); e = NULL;
            goto error;
        }

        /* Add the newly case-normalized dn to entrydn in the e_attrs list. */
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
                    (old_ep->ep_id >= job->ready_EID))
                   && (info->command != ABORT) && !(job->flags & FLAG_ABORT)) {
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
        if (newesize > job->fifo.bsize) {    /* entry too big */
            char ebuf[BUFSIZ];
            import_log_notice(job, "WARNING: skipping entry \"%s\"",
                    escape_string(slapi_entry_get_dn(e), ebuf));
            import_log_notice(job, "REASON: entry too large (%lu bytes) for "
                    "the buffer size (%lu bytes)", newesize, job->fifo.bsize);
            backentry_free(&ep);
            job->skipped++;
            continue;
        }
        /* Now check if fifo has enough space for the new entry */
        if ((job->fifo.c_bsize + newesize) > job->fifo.bsize) {
            import_wait_for_space_in_fifo( job, newesize );
        }

        /* We have enough space */
        job->fifo.item[idx].filename = ID2ENTRY LDBM_FILENAME_SUFFIX;
        job->fifo.item[idx].line = curr_entry;
        job->fifo.item[idx].entry = ep;
        job->fifo.item[idx].bad = 0;
        job->fifo.item[idx].esize = newesize;

        /* Add the entry size to total fifo size */
        job->fifo.c_bsize += ep->ep_entry? job->fifo.item[idx].esize : 0;

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
        if (info->command == STOP)
        {
            finished = 1;
        }
    }
bail:
    dbc->c_close(dbc);
    dblayer_release_aux_id2entry( be, db, env );
    if (job->flags & FLAG_DRYRUN) {
        if (finished) { /* Set if dn upgrade candidates are not found */
            info->state = FINISHED;
        } else { /* At least one dn upgrade candidate is found */
            info->state = QUIT;
        }
    } else {
        info->state = FINISHED;
    }
    return;

error:
    dbc->c_close(dbc);
    dblayer_release_aux_id2entry( be, db, env );
    info->state = ABORTED;
}

static void
import_wait_for_space_in_fifo(ImportJob *job, size_t new_esize)
{
    struct backentry *temp_ep = NULL;
    size_t i;
    int slot_found;
    PRIntervalTime sleeptime;

    sleeptime = PR_MillisecondsToInterval(import_sleep_time);

    /* Now check if fifo has enough space for the new entry */
    while ((job->fifo.c_bsize + new_esize) > job->fifo.bsize) {
        for ( i = 0, slot_found = 0 ; i < job->fifo.size ; i++ ) {
            temp_ep = job->fifo.item[i].entry;
            if (temp_ep) {
                if (temp_ep->ep_refcnt == 0 && temp_ep->ep_id <= job->ready_EID) {
                    job->fifo.item[i].entry = NULL;
                    if (job->fifo.c_bsize > job->fifo.item[i].esize)
                        job->fifo.c_bsize -= job->fifo.item[i].esize;
                    else
                        job->fifo.c_bsize = 0;
                    backentry_free(&temp_ep);
                    slot_found = 1;
                }
            }
        }
        if ( slot_found == 0 )
            DS_Sleep(sleeptime);
    }
}

/* helper function for the foreman: */
static int foreman_do_parentid(ImportJob *job, FifoItem *fi,
                               struct attrinfo *parentid_ai)
{
    backend *be = job->inst->inst_be;
    Slapi_Value **svals = NULL;
    Slapi_Attr *attr = NULL;
    int idl_disposition = 0;
    int ret = 0;
    struct backentry *entry = fi->entry;

    if (job->flags & FLAG_UPGRADEDNFORMAT) {
        /* Get the parentid attribute value from deleted attr list */
        Slapi_Value *value = NULL;
        Slapi_Attr *pid_to_del =
                 attrlist_remove(&entry->ep_entry->e_aux_attrs, "parentid");
        if (pid_to_del) {
            /* Delete it. */
            ret = slapi_attr_first_value(pid_to_del, &value);
            if (ret < 0) {
                import_log_notice(job, 
                                  "Error: retrieving parentid value (error %d)",
                                  ret);
            } else {
                const struct berval *bval = 
                             slapi_value_get_berval((const Slapi_Value *)value);
                ret = index_addordel_string(be, "parentid", 
                             bval->bv_val, entry->ep_id,
                             BE_INDEX_DEL|BE_INDEX_EQUALITY|BE_INDEX_NORMALIZED,
                             NULL);
                if (ret) {
                    import_log_notice(job, 
                                      "Error: deleting %s from  parentid index "
                                      "(error %d: %s)",
                                      bval->bv_val, ret, dblayer_strerror(ret));
                    return ret;
                }
            }
            slapi_attr_free(&pid_to_del);
        }
    }

    if (slapi_entry_attr_find(entry->ep_entry, "parentid", &attr) == 0) {
        svals = attr_get_present_values(attr);
        ret = index_addordel_values_ext_sv(be, "parentid", svals, NULL, 
                                           entry->ep_id, BE_INDEX_ADD, 
                                           NULL, &idl_disposition, NULL);
        if (idl_disposition != IDL_INSERT_NORMAL) {
            char *attr_value = slapi_value_get_berval(svals[0])->bv_val;
            ID parent_id = atol(attr_value);

            if (idl_disposition == IDL_INSERT_NOW_ALLIDS) {
                import_subcount_mother_init(job->mothers, parent_id,
                    idl_get_allidslimit(parentid_ai)+1);
            } else if (idl_disposition == IDL_INSERT_ALLIDS) {
                import_subcount_mother_count(job->mothers, parent_id);
            }
        }
        if (ret != 0) {
            import_log_notice(job, "ERROR: Can't update parentid index "
                              "(error %d)", ret);
            return ret;
        }
    }

    return 0;
}

/* helper function for the foreman: */
static int 
foreman_do_entrydn(ImportJob *job, FifoItem *fi)
{
    backend *be = job->inst->inst_be;
    struct berval bv;
    int err = 0, ret = 0;
    IDList *IDL;
    struct backentry *entry = fi->entry;

    if (job->flags & FLAG_UPGRADEDNFORMAT) {
        /* Get the entrydn attribute value from deleted attr list */
        Slapi_Value *value = NULL;
        Slapi_Attr *entrydn_to_del =
              attrlist_remove(&entry->ep_entry->e_aux_attrs, "entrydn");
        if (entrydn_to_del) {
            /* Delete it. */
            ret = slapi_attr_first_value(entrydn_to_del, &value);
            if (ret < 0) {
                import_log_notice(job, 
                                  "Error: retrieving entrydn value (error %d)",
                                  ret);
            } else {
                const struct berval *bval = 
                             slapi_value_get_berval((const Slapi_Value *)value);
                ret = index_addordel_string(be, "entrydn", 
                             bval->bv_val, entry->ep_id,
                             BE_INDEX_DEL|BE_INDEX_EQUALITY|BE_INDEX_NORMALIZED,
                             NULL);
                if (ret) {
                    import_log_notice(job, 
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
    bv.bv_val = (void*)backentry_get_ndn(entry);   /* jcm - Had to cast away const */
    bv.bv_len = strlen(bv.bv_val);

    /* We need to check here whether the DN is already present in
     * the entrydn index. If it is then the input ldif 
     * contained a duplicate entry, which it isn't allowed to */
    /* Due to popular demand, we only warn on this, given the 
     * tendency for customers to want to import dirty data */
    /* So, we do an index read first */
    err = 0;
    IDL = index_read(be, "entrydn", indextype_EQUALITY, &bv, NULL, &err);
    if (job->flags & FLAG_UPGRADEDNFORMAT) {
        /*
         * In the UPGRADEDNFORMAT case, if entrydn value exists, 
         * that means either 1) entrydn is not upgraded (ID == entry->ep_id)
         * or 2) a duplicated entry is found (ID != entry->ep_id).
         * (1) is normal. For (2), need to return a specific error 
         * LDBM_ERROR_FOUND_DUPDN.
         * Otherwise, add entrydn to the entrydn index file.
         */
        if (IDL) {
            ID id = idl_firstid(IDL); /* entrydn is a single attr */
            idl_free(IDL);
            if (id != entry->ep_id) { /* case (2) */
                import_log_notice(job, "Duplicated entrydn detected: \"%s\": "
                                       "Entry ID: (%d, %d)",
                                       bv.bv_val, id, entry->ep_id);
                return LDBM_ERROR_FOUND_DUPDN;
            }
        } else {
            ret = index_addordel_string(be, "entrydn", 
                                        bv.bv_val, entry->ep_id,
                                        BE_INDEX_ADD|BE_INDEX_NORMALIZED, NULL);
            if (ret) {
                import_log_notice(job, "Error writing entrydn index "
                                       "(error %d: %s)",
                                       ret, dblayer_strerror(ret));
                return ret;
            }
        }
    } else {
        /* Did this work ? */
        if (IDL) {
            /* IMPOSTER ! Get thee hence... */
            import_log_notice(job, "WARNING: Skipping duplicate entry "
                              "\"%s\" found at line %d of file \"%s\"",
                              slapi_entry_get_dn(entry->ep_entry),
                              fi->line, fi->filename);
            idl_free(IDL);
            /* skip this one */
            fi->bad = 1;
            job->skipped++;
            return -1;      /* skip to next entry */
        }
        ret = index_addordel_string(be, "entrydn", bv.bv_val, entry->ep_id,
                                    BE_INDEX_ADD|BE_INDEX_NORMALIZED, NULL);
        if (ret) {
            import_log_notice(job, "Error writing entrydn index "
                                   "(error %d: %s)",
                                   ret, dblayer_strerror(ret));
            return ret;
        }
    }

    return 0;
}

/* foreman thread:
 * i go through the FIFO just like the other worker threads, but i'm 
 * responsible for the interrelated indexes: entrydn, id2entry, and the
 * operational attributes (plus the parentid index).
 */
void import_foreman(void *param)
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

    /* the pblock is used only by add_op_attrs */
    slapi_pblock_set(pb, SLAPI_BACKEND, be);
    sleeptime = PR_MillisecondsToInterval(import_sleep_time);
    info->state = RUNNING;

    ainfo_get(be, "parentid", &parentid_ai);

    while (! finished) {
        FifoItem *fi = NULL;
        int parent_status = 0;
         
        if (job->flags & FLAG_ABORT) { 
            goto error;
        }

        while ( ((info->command == PAUSE) || (id > job->lead_ID)) &&
                (info->command != STOP) && (info->command != ABORT) &&
                !(job->flags & FLAG_ABORT) ) {
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
        fi = import_fifo_fetch(job, id, 0);
        if (NULL == fi) {
            import_log_notice(job, "WARNING: entry id %d is missing", id);
            continue;
        }
        if (NULL == fi->entry) {
            import_log_notice(job, "WARNING: entry for id %d is missing", id);
            continue;
        }

        /* first, fill in any operational attributes */
        /* add_op_attrs wants a pblock for some reason. */
        if (job->flags & FLAG_UPGRADEDNFORMAT) {
            /* Upgrade dn format may alter the DIT structure. */
            /* It requires a special treatment for that. */
            parent_status = IMPORT_ADD_OP_ATTRS_SAVE_OLD_PID;
        }
        if (add_op_attrs(pb, inst->inst_li, fi->entry, &parent_status) != 0) {
            import_log_notice(job, "ERROR: Could not add op attrs to "
                              "entry ending at line %d of file \"%s\"",
                              fi->line, fi->filename);
            goto error;
        }

        if (! slapi_entry_flag_is_set(fi->entry->ep_entry,
                                      SLAPI_ENTRY_FLAG_TOMBSTONE)) {
            /*
             * Only check for a parent and add to the entry2dn index if
             * the entry is not a tombstone.
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
                if (! slapi_be_issuffix(inst->inst_be, backentry_get_sdn(fi->entry))) {
                    import_log_notice(job, "WARNING: Skipping entry \"%s\" "
                                      "which has no parent, ending at line %d "
                                      "of file \"%s\"",
                                      slapi_entry_get_dn(fi->entry->ep_entry),
                                      fi->line, fi->filename);
                    /* skip this one */
                    fi->bad = 1;
                    job->skipped++;
                    goto cont;      /* below */
                }
            }
            if (job->flags & FLAG_ABORT) {
                goto error;
            }

            /* insert into the entrydn index */
            ret = foreman_do_entrydn(job, fi);
            if (ret == -1) {
                goto cont;      /* skip entry */
            } else if ((job->flags & FLAG_UPGRADEDNFORMAT) &&
                       (LDBM_ERROR_FOUND_DUPDN == ret)) {
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
                Slapi_Attr *new_entrydn = slapi_attr_new();
                Slapi_Attr *nsuniqueid = NULL;
                const char *uuidstr = NULL;
                char *new_dn = NULL;
                char *orig_dn = 
                      slapi_ch_strdup(slapi_entry_get_dn(fi->entry->ep_entry));
                struct berval *vals[2];
                struct berval val;
                int rc = 0;
                nsuniqueid = attrlist_find(fi->entry->ep_entry->e_attrs,
                                           "nsuniqueid");
                if (nsuniqueid) {
                    Slapi_Value *uival = NULL;
                    rc = slapi_attr_first_value(nsuniqueid, &uival);
                    uuidstr = slapi_value_get_string(uival);
                } else {
                    import_log_notice(job, "ERROR: Failed to get nsUniqueId "
                                            "of the duplicated entry %s; "
                                            "Entry ID: %d", 
                                            orig_dn, fi->entry->ep_id);
                    goto cont;
                }
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
                valueset_add_string(&new_entrydn->a_present_values,
                                    /* new_dn: duped in valueset_add_string */
                                    (const char *)new_dn,
                                    CSN_TYPE_UNKNOWN, NULL);
                attrlist_add(&fi->entry->ep_entry->e_attrs, new_entrydn);

                /* Try foreman_do_entrydn, again. */
                ret = foreman_do_entrydn(job, fi);
                if (ret) {
                    import_log_notice(job, "ERROR: Failed to rename duplicated "
                                            "DN %s to %s; Entry ID: %d", 
                                            orig_dn, new_dn, fi->entry->ep_id);
                    slapi_ch_free_string(&orig_dn);
                    if (-1 == ret) {
                        goto cont;      /* skip entry */
                    } else {
                        goto error; 
                    }
                } else {
                    import_log_notice(job, "WARNING: Duplicated entry %s is "
                                            "renamed to %s; Entry ID: %d", 
                                            orig_dn, new_dn, fi->entry->ep_id);
                    slapi_ch_free_string(&orig_dn);
                }

            } else if (ret != 0) {
                goto error; 
            }
        }

        if (job->flags & FLAG_ABORT) {
            goto error;
        }

        if (!(job->flags & FLAG_REINDEXING))/* reindex reads data from id2entry */
        {
            /* insert into the id2entry index
             * (that isn't really an index -- it's the storehouse of the entries
             * themselves.)
             */
            /* id2entry_add_ext replaces an entry if it already exists. 
             * therefore, the Entry ID stays the same.
             */
            if ((ret = id2entry_add_ext(be, fi->entry, NULL, job->encrypt)) != 0) {
                /* DB_RUNRECOVERY usually occurs if disk fills */
                if (LDBM_OS_ERR_IS_DISKFULL(ret)) {
                import_log_notice(job, "ERROR: OUT OF SPACE ON DISK or FILE TOO LARGE -- "
                                  "Could not store the entry ending at line "
                                  "%d of file \"%s\"",
                                  fi->line, fi->filename);
                } else if (ret == DB_RUNRECOVERY) {
                    import_log_notice(job, "FATAL ERROR: (LARGEFILE SUPPORT NOT ENABLED? OUT OF SPACE ON DISK?) -- "
                                  "Could not store the entry ending at line "
                                  "%d of file \"%s\"",
                                  fi->line, fi->filename);
                } else {
                    import_log_notice(job, "ERROR: Could not store the entry "
                                  "ending at line %d of file \"%s\" -- "
                                  "error %d", fi->line, fi->filename, ret);
                }
                goto error;
            }
        }

        if (job->flags & FLAG_ABORT) { 
            goto error;
        }

        if (!slapi_entry_flag_is_set(fi->entry->ep_entry,
                                     SLAPI_ENTRY_FLAG_TOMBSTONE)) {
            /* parentid index
             * (we have to do this here, because the parentID is dependent on
             * looking up by entrydn.)
             * Only add to the parent index if the entry is not a tombstone.
             */
            ret = foreman_do_parentid(job, fi, parentid_ai);
            if (ret != 0)
                goto error;
            
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
            cache_remove(&inst->inst_cache, fi->entry);
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

        if (job->flags & FLAG_ABORT){
            goto error;
        }
    }

    slapi_pblock_destroy(pb);
    info->state = FINISHED;
    return;

error:
    slapi_pblock_destroy(pb);
    info->state = ABORTED;
}


/* worker thread:
 * given an attribute, this worker plows through the entry FIFO, building
 * up the attribute index.
 */
void import_worker(void *param)
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
    struct vlvIndex* vlv_index = NULL;
    void *substring_key_buffer = NULL;
    FifoItem *fi = NULL;
    int is_objectclass_attribute;
    int is_nsuniqueid_attribute;
    int is_nscpentrydn_attribute;
    void *attrlist_cursor;
    
    PR_ASSERT(NULL != info);
    PR_ASSERT(NULL != inst);

    if (job->flags & FLAG_ABORT) {
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
     * flags are just handy for working out what to do in this case.
     */
    is_objectclass_attribute =
        (strcasecmp(info->index_info->name, "objectclass") == 0);
    is_nsuniqueid_attribute = 
        (strcasecmp(info->index_info->name, SLAPI_ATTR_UNIQUEID) == 0);
    is_nscpentrydn_attribute =
        (strcasecmp(info->index_info->name, SLAPI_ATTR_NSCP_ENTRYDN) == 0);

    if (1 != idl_get_idl_new()) {
        /* Is there substring indexing going on here ? */
        if ( (INDEX_SUB & info->index_info->ai->ai_indexmask) &&
             (info->index_buffer_size > 0) ) {
            /* Then make a key buffer thing */
            ret = index_buffer_init(info->index_buffer_size, 0, 
                                    &substring_key_buffer);
            if (0 != ret) {
                import_log_notice(job, "IMPORT FAIL 1 (error %d)", ret);
            }
        }
    }

    sleeptime = PR_MillisecondsToInterval(import_sleep_time);
    info->state = RUNNING;
    info->last_ID_processed = id-1;

    while (! finished) {
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
            fi = import_fifo_fetch(job, id, 1);
            ep = fi ? fi->entry : NULL;
            if (!ep) {
                /* skipping an entry that turned out to be bad */
                info->last_ID_processed = id;
                id++;
            }
        }
        if (finished)
            continue;

        if (! slapi_entry_flag_is_set(fi->entry->ep_entry,
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
                vlv_update_index(vlv_index, NULL, inst->inst_li, pb, NULL, ep);
                slapi_pblock_destroy(pb);
            } else {
                /* No, process regular index */
                if (job->flags & FLAG_UPGRADEDNFORMAT) {
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
                                         BE_INDEX_DEL|BE_INDEX_EQUALITY|
                                         BE_INDEX_NORMALIZED,
                                         NULL);
                            if (ret) {
                                import_log_notice(job, 
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
                    if(valueset_isempty(&(attr->a_present_values))) continue;
                    svals = attr_get_present_values(attr);
                    ret = index_addordel_values_ext_sv(be, info->index_info->name,
                        svals, NULL, ep->ep_id, BE_INDEX_ADD | (job->encrypt ? 0 : BE_INDEX_DONT_ENCRYPT), NULL, &idl_disposition,
                        substring_key_buffer);

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
                    BE_INDEX_ADD, NULL);
                if (0 != ret) {
                    /* Something went wrong, eg disk filled up */
                    goto error;
                }
            }
            if (is_objectclass_attribute) {
                ret = index_addordel_string(be, SLAPI_ATTR_OBJECTCLASS,
                    SLAPI_ATTR_VALUE_TOMBSTONE, ep->ep_id, BE_INDEX_ADD, NULL);
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
                    if(valueset_isempty(&(attr->a_present_values))) continue;
                    svals = attr_get_present_values(attr);
                    ret = index_addordel_values_ext_sv(be, info->index_info->name,
                        svals, NULL, ep->ep_id, BE_INDEX_ADD | (job->encrypt ? 0 : BE_INDEX_DONT_ENCRYPT), NULL, &idl_disposition,
                        substring_key_buffer);

                    if (0 != ret) {
                        /* Something went wrong, eg disk filled up */
                        goto error;
                    }
                }
            }
        }
        import_decref_entry(ep);
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
        index_buffer_terminate(substring_key_buffer);
    }
    info->state = FINISHED;
    return;

error:
    if (ret == DB_RUNRECOVERY) {
        LDAPDebug(LDAP_DEBUG_ANY,"cannot import; database recovery needed\n",
                  0,0,0);
    } else if (ret == DB_LOCK_DEADLOCK) {
        /* can this occur? */
    }

    info->state = ABORTED;
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
static int bulk_import_start(Slapi_PBlock *pb)
{
    struct ldbminfo *li = NULL;
    ImportJob *job = NULL;
    backend *be = NULL;
    PRThread *thread = NULL;
    int ret = 0;

    job = CALLOC(ImportJob);
    if (job == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "not enough memory to do import job\n",
                  0, 0, 0);
        return -1;
    }

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    slapi_pblock_get(pb, SLAPI_LDIF2DB_ENCRYPT, &job->encrypt);
    PR_ASSERT(be != NULL);
    li = (struct ldbminfo *)(be->be_database->plg_private);
    job->inst = (ldbm_instance *)be->be_instance_info;

    /* check if an import/restore is already ongoing... */
    PR_Lock(job->inst->inst_config_mutex);
    if (job->inst->inst_flags & INST_FLAG_BUSY) {
        PR_Unlock(job->inst->inst_config_mutex);
        LDAPDebug(LDAP_DEBUG_ANY, "ldbm: '%s' is already in the middle of "
                  "another task and cannot be disturbed.\n",
                  job->inst->inst_name, 0, 0);
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

    job->flags = 0;     /* don't use files */
    job->flags |= FLAG_INDEX_ATTRS;
    job->flags |= FLAG_ONLINE;
    job->starting_ID = 1;
    job->first_ID = 1;

    job->mothers = CALLOC(import_subcount_stuff);
    /* how much space should we allocate to index buffering? */
    job->job_index_buffer_size = import_get_index_buffer_size();
    if (job->job_index_buffer_size == 0) {
	/* 10% of the allocated cache size + one meg */
	job->job_index_buffer_size = (job->inst->inst_li->li_dbcachesize/10) +
            (1024*1024);
    }
    import_subcount_stuff_init(job->mothers);
    job->wire_lock = PR_NewLock();
    job->wire_cv = PR_NewCondVar(job->wire_lock);

    /* COPIED from ldif2ldbm.c : */

    /* shutdown this instance of the db */
    cache_clear(&job->inst->inst_cache);
    dblayer_instance_close(be);

    /* Delete old database files */
    dblayer_delete_instance_dir(be);
    /* it's okay to fail -- it might already be gone */

    /* dblayer_instance_start will init the id2entry index. */
    /* it also (finally) fills in inst_dir_name */
    ret = dblayer_instance_start(be, DBLAYER_IMPORT_MODE);
    if (ret != 0)
        goto fail;

    /* END OF COPIED SECTION */

    PR_Lock(job->wire_lock);
    vlv_init(job->inst);

    /* create thread for import_main, so we can return */
    thread = PR_CreateThread(PR_USER_THREAD, import_main, (void *)job,
                             PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                             PR_JOINABLE_THREAD,
                             SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
	PRErrorCode prerr = PR_GetError();
        LDAPDebug(LDAP_DEBUG_ANY, "unable to spawn import thread, "
				SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
				prerr, slapd_pr_strerror(prerr), 0);
        PR_Unlock(job->wire_lock);
        ret = -2;
        goto fail;
    }

    job->main_thread = thread;
    slapi_set_object_extension(li->li_bulk_import_object, pb->pb_conn,
        li->li_bulk_import_handle, job);

    /* wait for the import_main to signal that it's ready for entries */
    /* (don't want to send the success code back to the LDAP client until
     * we're ready for the adds to start rolling in)
     */
    PR_WaitCondVar(job->wire_cv, PR_INTERVAL_NO_TIMEOUT);
    PR_Unlock(job->wire_lock);

    return 0;

fail:
    PR_Lock(job->inst->inst_config_mutex);
    job->inst->inst_flags &= ~INST_FLAG_BUSY;
    PR_Unlock(job->inst->inst_config_mutex);
    import_free_job(job);
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
static int bulk_import_queue(ImportJob *job, Slapi_Entry *entry)
{
    struct backentry *ep = NULL, *old_ep = NULL;
    int idx;
    ID id = 0;
    Slapi_Attr *attr = NULL;
    size_t newesize = 0;

    PR_Lock(job->wire_lock);
	/* Let's do this inside the lock !*/
    id = job->lead_ID + 1;
    /* generate uniqueid if necessary */
    import_generate_uniqueid(job, entry);

    /* make into backentry */
    ep = import_make_backentry(entry, id);
    if (!ep) {
        import_abort_all(job, 1);
        PR_Unlock(job->wire_lock);
        return -1;
    }

    /* encode the password */
    if (slapi_entry_attr_find(ep->ep_entry, "userpassword", &attr) == 0) {
        Slapi_Value **va = attr_get_present_values(attr);

        pw_encodevals( (Slapi_Value **)va ); /* jcm - had to cast away const */
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
        while ((old_ep->ep_refcnt > 0) && !(job->flags & FLAG_ABORT))
        {
            DS_Sleep(PR_MillisecondsToInterval(import_sleep_time));
        }

        /* the producer could be running thru the fifo while
         * everyone else is cycling to a new pass...
         * double-check that this entry is < ready_EID
         */
        while ((old_ep->ep_id >= job->ready_EID) && !(job->flags & FLAG_ABORT))
        {
            DS_Sleep(PR_MillisecondsToInterval(import_sleep_time));
        }

        if (job->flags & FLAG_ABORT) {
            backentry_clear_entry(ep);      /* entry is released in the frontend on failure*/
            backentry_free( &ep );          /* release the backend wrapper, here */
            PR_Unlock(job->wire_lock);
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

    newesize = (slapi_entry_size(ep->ep_entry) + sizeof(struct backentry));
    if (newesize > job->fifo.bsize) {    /* entry too big */
        char ebuf[BUFSIZ];
        import_log_notice(job, "WARNING: skipping entry \"%s\"",
                    escape_string(slapi_entry_get_dn(ep->ep_entry), ebuf));
        import_log_notice(job, "REASON: entry too large (%lu bytes) for "
                    "the import buffer size (%lu bytes).   Try increasing nsslapd-cachememsize.", newesize, job->fifo.bsize);
        backentry_clear_entry(ep);      /* entry is released in the frontend on failure*/
        backentry_free( &ep );          /* release the backend wrapper, here */
        PR_Unlock(job->wire_lock);
        return -1;
    }
    /* Now check if fifo has enough space for the new entry */
    if ((job->fifo.c_bsize + newesize) > job->fifo.bsize) {
        import_wait_for_space_in_fifo( job, newesize );
    }

    /* We have enough space */
    job->fifo.item[idx].filename = "(bulk import)";
    job->fifo.item[idx].line = 0;
    job->fifo.item[idx].entry = ep;
    job->fifo.item[idx].bad = 0;
    job->fifo.item[idx].esize = newesize;

    /* Add the entry size to total fifo size */
    job->fifo.c_bsize += ep->ep_entry? job->fifo.item[idx].esize : 0;

    /* Update the job to show our progress */
    job->lead_ID = id;
    if ((id - job->starting_ID) <= job->fifo.size) {
        job->trailing_ID = job->starting_ID;
    } else {
        job->trailing_ID = id - job->fifo.size;
    }

    PR_Unlock(job->wire_lock);
    return 0;
}

void *factory_constructor(void *object, void *parent)
{
    return NULL;
}

void factory_destructor(void *extension, void *object, void *parent)
{
    ImportJob *job = (ImportJob *)extension;
    PRThread *thread;

    if (extension == NULL)
        return;

    /* connection was destroyed while we were still storing the extension --
     * this is bad news and means we have a bulk import that needs to be
     * aborted!
     */
    thread = job->main_thread;
    LDAPDebug(LDAP_DEBUG_ANY, "ERROR bulk import abandoned\n",
              0, 0, 0);
    import_abort_all(job, 1);
    /* wait for import_main to finish... */
    PR_JoinThread(thread);
    /* extension object is free'd by import_main */
    return;
}

/* plugin entry function for replica init
 *
 * For the SLAPI_BI_STATE_ADD state:
 * On success (rc=0), the entry in pb->pb_import_entry will be
 * consumed.  For any other return value, the caller is
 * responsible for freeing the entry in the pb.
 */
int ldbm_back_wire_import(Slapi_PBlock *pb)
{
    struct ldbminfo *li;
    backend *be = NULL;
    ImportJob *job = NULL;
    PRThread *thread;
    int state;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    PR_ASSERT(be != NULL);
    li = (struct ldbminfo *)(be->be_database->plg_private);
    slapi_pblock_get(pb, SLAPI_BULK_IMPORT_STATE, &state);
    slapi_pblock_set(pb, SLAPI_LDIF2DB_ENCRYPT, &li->li_online_import_encrypt);
    if (state == SLAPI_BI_STATE_START) {
        /* starting a new import */
        return bulk_import_start(pb);
    }

    PR_ASSERT(pb->pb_conn != NULL);
    if (pb->pb_conn != NULL) {
        job = (ImportJob *)slapi_get_object_extension(li->li_bulk_import_object, pb->pb_conn, li->li_bulk_import_handle);
    }

    if ((job == NULL) || (pb->pb_conn == NULL)) {
        /* import might be aborting */
        return -1;
    }

    if (state == SLAPI_BI_STATE_ADD) {
        /* continuing previous import */
        if (! import_entry_belongs_here(pb->pb_import_entry,
                                        job->inst->inst_be)) {
            /* silently skip */
            /* We need to consume pb->pb_import_entry on success, so we free it here. */
            slapi_entry_free(pb->pb_import_entry);
            return 0;
        }

        return bulk_import_queue(job, pb->pb_import_entry);
    }

    thread = job->main_thread;

    if (state == SLAPI_BI_STATE_DONE) {
        /* finished with an import */
        job->flags |= FLAG_PRODUCER_DONE;
        /* "job" struct may vanish at any moment after we set the DONE
         * flag, so keep a copy of the thread id in 'thread' for safekeeping.
         */
        /* wait for import_main to finish... */
        PR_JoinThread(thread);
        slapi_set_object_extension(li->li_bulk_import_object, pb->pb_conn,
            li->li_bulk_import_handle, NULL);
        return 0;
    }

    /* ??? unknown state */
    LDAPDebug(LDAP_DEBUG_ANY,
              "ERROR: ldbm_back_wire_import: unknown state %d\n",
              state, 0, 0);
    return -1;
}

/*
 * backup index configuration
 * this function is called from dblayer_backup (ldbm2archive)
 * [547427] index config must not change between backup and restore
 */
#define DSE_INDEX "dse_index.ldif"
#define DSE_INSTANCE "dse_instance.ldif"
#define DSE_INDEX_FILTER "(objectclass=nsIndex)"
#define DSE_INSTANCE_FILTER "(objectclass=nsBackendInstance)"
static int
dse_conf_backup_core(struct ldbminfo *li, char *dest_dir, char *file_name, char *filter)
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
    if (0 == dlen)
    {
        filename = file_name;
    }
    else
    {
        filename = slapi_ch_smprintf("%s/%s", dest_dir, file_name);
    }
    LDAPDebug(LDAP_DEBUG_TRACE, "dse_conf_backup(%s): backup file %s\n",
              filter, filename, 0);

    /* Open the file to write */
    if ((prfd = PR_Open(filename, PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE,
                        SLAPD_DEFAULT_FILE_MODE)) == NULL)
    {
        LDAPDebug(LDAP_DEBUG_ANY,
                "dse_conf_backup(%s): open %s failed: (%s)\n",
                filter, filename, slapd_pr_strerror(PR_GetError()));
        rval = -1;
        goto out;
    }

    srch_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(srch_pb, li->li_plugin->plg_dn,
        LDAP_SCOPE_SUBTREE, filter, NULL, 0, NULL, NULL, li->li_identity, 0);
    slapi_search_internal_pb(srch_pb);
    slapi_pblock_get(srch_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    for (ep = entries; ep != NULL && *ep != NULL; ep++)
    {
        size_t l = strlen(slapi_entry_get_dn_const(*ep)) + 5 /* "dn: \n" */;
        LDAPDebug(LDAP_DEBUG_TRACE, "\ndn: %s\n", 
                 slapi_entry_get_dn_const(*ep), 0, 0);

        if (l <= BUFSIZ)
            tp = tmpbuf;
        else
            tp = (char *)slapi_ch_malloc(l);    /* should be very rare ... */
        sprintf(tp, "dn: %s\n", slapi_entry_get_dn_const(*ep));
        prrval = PR_Write(prfd, tp, l);
        if ((size_t)prrval != l)
        {
            LDAPDebug(LDAP_DEBUG_ANY,
                "dse_conf_backup(%s): write %s failed: %d (%s)\n",
                filter, PR_GetError(), slapd_pr_strerror(PR_GetError()));
            rval = -1;
            if (l > BUFSIZ)
                slapi_ch_free_string(&tp);
            goto out;
        }
        if (l > BUFSIZ)
            slapi_ch_free_string(&tp);

        for (slapi_entry_first_attr(*ep, &attr); attr;
             slapi_entry_next_attr(*ep, attr, &attr))
        {
            int i;
            Slapi_Value *sval = NULL;
            const struct berval *attr_val;
            int attr_name_len;

            slapi_attr_get_type(attr, &attr_name);
            /* numsubordinates should not be backed up */
            if (!strcasecmp("numsubordinates", attr_name))
                continue;
            attr_name_len = strlen(attr_name);
            for (i = slapi_attr_first_value(attr, &sval); i != -1;
                 i = slapi_attr_next_value(attr, i, &sval))
            {
                attr_val = slapi_value_get_berval(sval);
                l = strlen(attr_val->bv_val) + attr_name_len + 3; /* : \n" */
                LDAPDebug(LDAP_DEBUG_TRACE, "%s: %s\n", attr_name,
                            attr_val->bv_val, 0);
                if (l <= BUFSIZ)
                    tp = tmpbuf;
                else
                    tp = (char *)slapi_ch_malloc(l);
                sprintf(tp, "%s: %s\n", attr_name, attr_val->bv_val);
                prrval = PR_Write(prfd, tp, l);
                if ((size_t)prrval != l)
                {
                    LDAPDebug(LDAP_DEBUG_ANY,
                        "dse_conf_backup(%s): write %s failed: %d (%s)\n",
                        filter, PR_GetError(), slapd_pr_strerror(PR_GetError()));
                    rval = -1;
                    if (l > BUFSIZ)
                        slapi_ch_free_string(&tp);
                    goto out;
                }
                if (l > BUFSIZ)
                    slapi_ch_free_string(&tp);
            }
        }
        if (ep+1 != NULL && *(ep+1) != NULL)
        {
            prrval = PR_Write(prfd, "\n", 1);
            if ((int)prrval != 1)
            {
                LDAPDebug(LDAP_DEBUG_ANY,
                    "dse_conf_backup(%s): write %s failed: %d (%s)\n",
                    filter, PR_GetError(), slapd_pr_strerror(PR_GetError()));
                rval = -1;
                goto out;
            }
        }
    }

out:
    slapi_free_search_results_internal(srch_pb);
    if (srch_pb)
    {
        slapi_pblock_destroy(srch_pb);
    }

    if (0 != dlen)
    {
        slapi_ch_free_string(&filename);
    }

    if (prfd)
    {
        prrval = PR_Close(prfd);
        if (PR_SUCCESS != prrval)
        {
            LDAPDebug( LDAP_DEBUG_ANY,
                "Fatal Error---Failed to back up dse indexes %d (%s)\n",
                PR_GetError(), slapd_pr_strerror(PR_GetError()), 0);
            rval = -1;
        }
    }

    return rval;
}

int
dse_conf_backup(struct ldbminfo *li, char *dest_dir)
{
    int rval = 0;
    rval  = dse_conf_backup_core(li, dest_dir, DSE_INSTANCE, DSE_INSTANCE_FILTER);
    rval += dse_conf_backup_core(li, dest_dir, DSE_INDEX, DSE_INDEX_FILTER);
    return rval;
}

/*
 * read the backed up index configuration
 * adjust them if the current configuration is different from it.
 * this function is called from dblayer_restore (archive2ldbm)
 * these functions are placed here to borrow import_get_entry
 * [547427] index config must not change between backup and restore
 */
int
dse_conf_verify_core(struct ldbminfo *li, char *src_dir, char *file_name, char *filter, char *log_str, char *entry_filter)
{
    char *filename = NULL;
    int rval = 0;
    ldif_context c;
    int fd = -1;
    int curr_lineno = 0;
    int finished = 0;
    int backup_entry_len = 256;
	char *search_scope = NULL;
    Slapi_Entry **backup_entries = NULL;
    Slapi_Entry **bep = NULL;
    Slapi_Entry **curr_entries = NULL;
    Slapi_PBlock srch_pb;
    
    filename = slapi_ch_smprintf("%s/%s", src_dir, file_name);

    if (PR_SUCCESS != PR_Access(filename, PR_ACCESS_READ_OK))
    {
        LDAPDebug(LDAP_DEBUG_ANY,
            "Warning: config backup file %s not found in backup\n",
            file_name, 0, 0);
        rval = 0;
        goto out;
    }

    fd = dblayer_open_huge_file(filename, O_RDONLY, 0);
    if (fd < 0)
    {
        LDAPDebug(LDAP_DEBUG_ANY,
            "Warning: can't open config backup file: %s\n", filename, 0, 0);
        rval = -1;
        goto out;
    }

    import_init_ldif(&c);
    bep = backup_entries = (Slapi_Entry **)slapi_ch_calloc(1,
                            backup_entry_len * sizeof(Slapi_Entry *));

    while (!finished)
    {
        char *estr = NULL;
        Slapi_Entry *e = NULL;
        estr = import_get_entry(&c, fd, &curr_lineno);

        if (!estr)
            break;
		
		if (entry_filter != NULL) /* Single instance restoration */
		{
			if (NULL == strstr(estr, entry_filter))
				continue;
		}

        e = slapi_str2entry(estr, 0);
        slapi_ch_free_string(&estr);
        if (!e) {
            LDAPDebug(LDAP_DEBUG_ANY, "WARNING: skipping bad LDIF entry "
                "ending line %d of file \"%s\"", curr_lineno, filename, 0);
            continue;
        }
        if (bep - backup_entries >= backup_entry_len)
        {
            backup_entries = (Slapi_Entry **)slapi_ch_realloc((char *)backup_entries, 
                            2 * backup_entry_len * sizeof(Slapi_Entry *));
            bep = backup_entries + backup_entry_len;
            backup_entry_len *= 2;
        }
        *bep = e;
        bep++;
    }
    /* 623986: terminate the list if we reallocated backup_entries */
    if (backup_entry_len > 256)
       *bep = NULL;

    pblock_init(&srch_pb);

	if (entry_filter != NULL)
	{ /* Single instance restoration */
        search_scope = slapi_ch_smprintf("%s,%s", entry_filter, li->li_plugin->plg_dn);
	} else { /* Normal restoration */
        search_scope = slapi_ch_strdup(li->li_plugin->plg_dn);
	}

    slapi_search_internal_set_pb(&srch_pb, search_scope,
        LDAP_SCOPE_SUBTREE, filter, NULL, 0, NULL, NULL, li->li_identity, 0);
    slapi_search_internal_pb(&srch_pb);
    slapi_pblock_get(&srch_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &curr_entries);


    if (0 != slapi_entries_diff(backup_entries, curr_entries, 1 /* test_all */,
                                log_str, 1 /* force_update */, li->li_identity))
    {
        LDAPDebug(LDAP_DEBUG_ANY, "WARNING!!: current %s is "
                  "different from backed up configuration; "
                  "The backup is restored.\n", log_str, 0, 0);
    }

    slapi_free_search_results_internal(&srch_pb);
    pblock_done(&srch_pb);
    import_free_ldif(&c);
out:
    for (bep = backup_entries; bep && *bep; bep++)
        slapi_entry_free(*bep);
    slapi_ch_free((void **)&backup_entries);

    slapi_ch_free_string(&filename);

	slapi_ch_free_string(&search_scope);


    if (fd > 0)
        close(fd);

    return rval;
}

int
dse_conf_verify(struct ldbminfo *li, char *src_dir, char *bename)
{
    int rval;
	char *entry_filter = NULL;
	char *instance_entry_filter = NULL;
	
	if (bename != NULL) /* This was a restore of a single backend */
	{
		/* Entry filter string */
        entry_filter = slapi_ch_smprintf("cn=%s", bename);

		/* Instance search filter */
        instance_entry_filter = slapi_ch_smprintf("(&%s(cn=%s))", DSE_INSTANCE_FILTER, bename);
	} else {
	    instance_entry_filter = slapi_ch_strdup(DSE_INSTANCE_FILTER);
	}

	rval  = dse_conf_verify_core(li, src_dir, DSE_INSTANCE, instance_entry_filter,
                "Instance Config", entry_filter);
    rval += dse_conf_verify_core(li, src_dir, DSE_INDEX, DSE_INDEX_FILTER,
                "Index Config", entry_filter);

	slapi_ch_free_string(&entry_filter);
	slapi_ch_free_string(&instance_entry_filter);

    return rval;
}


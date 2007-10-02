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
 * this is a bunch of routines for merging groups of db files together --
 * currently it's only used for imports (when we import into several small
 * db sets for speed, then merge them).
 */

#include "back-ldbm.h"
#include "import.h"

struct _import_merge_thang
{
	int type;
#define IMPORT_MERGE_THANG_IDL 1 /* Values for type */
#define IMPORT_MERGE_THANG_VLV 2
	union {
		IDList *idl; /* if type == IMPORT_MERGE_THANG_IDL */
		DBT vlv_data; /* if type == IMPORT_MERGE_THANG_VLV */
	} payload;
};
typedef struct _import_merge_thang import_merge_thang;

struct _import_merge_queue_entry 
{
	int *file_referenced_list;
	import_merge_thang thang;
	DBT key;
	struct _import_merge_queue_entry *next;
};
typedef struct _import_merge_queue_entry import_merge_queue_entry;

static int import_merge_get_next_thang(backend *be, DBC *cursor, DB *db, import_merge_thang *thang, DBT *key, int type)
{
    int ret = 0;
    DBT value = {0};

    value.flags = DB_DBT_MALLOC;
    key->flags = DB_DBT_MALLOC;

    thang->type = type;
    if (IMPORT_MERGE_THANG_IDL == type) {
        /* IDL case */
    around:
        ret = cursor->c_get(cursor, key, &value, DB_NEXT_NODUP);
        if (0 == ret) {
            /* Check that we've not reached the beginning of continuation
             * blocks */
            if (CONT_PREFIX != ((char*)key->data)[0]) {
                /* If not, read the IDL using idl_fetch() */
                key->flags = DB_DBT_REALLOC;
                ret = NEW_IDL_NO_ALLID;
                thang->payload.idl = idl_fetch(be, db, key, NULL, NULL, &ret);
                PR_ASSERT(NULL != thang->payload.idl);
            } else {
                free(value.data);
                free(key->data);
                key->flags = DB_DBT_MALLOC;
                goto around; /* Just skip these */
            }
            free(value.data);
        } else {
            if (DB_NOTFOUND == ret) {
               /* This means that we're at the end of the file */
                ret = EOF;
            }
        }
    } else {
        /* VLV case */
        ret = cursor->c_get(cursor,key,&value,DB_NEXT);
        if (0 == ret) {
            thang->payload.vlv_data = value;
            thang->payload.vlv_data.flags = 0;
            key->flags = 0;
        } else {
            if (DB_NOTFOUND == ret) {
		/* This means that we're at the end of the file */
                ret = EOF;
            }
        }
    }

    return ret;
}

static import_merge_queue_entry *import_merge_make_new_queue_entry(import_merge_thang *thang, DBT *key, int fileno, int passes)
{
    /* Make a new entry */
    import_merge_queue_entry *new_entry = (import_merge_queue_entry *)slapi_ch_calloc(1, sizeof(import_merge_queue_entry));

    if (NULL == new_entry) {
        return NULL;
    }
    new_entry->key = *key;
    new_entry->thang = *thang;
    new_entry->file_referenced_list =
        (int *)slapi_ch_calloc(passes, sizeof(fileno));

    if (NULL == new_entry->file_referenced_list) {
        return NULL;
    }
    (new_entry->file_referenced_list)[fileno] = 1;
    return new_entry;
}

/* Put an IDL onto the priority queue */
static int import_merge_insert_input_queue(backend *be, import_merge_queue_entry **queue,int fileno, DBT *key, import_merge_thang *thang,int passes)
{
    /* Walk the list, looking for a key value which is greater than or equal
     * to the presented key */
    /* If an equal key is found, compute the union of the IDLs and store that
     * back in the queue entry */
    /* If a key greater than is found, or no key greater than is found, insert
     * a new queue entry */
    import_merge_queue_entry *current_entry = NULL;
    import_merge_queue_entry *previous_entry = NULL;

    PR_ASSERT(NULL != thang);
    if (NULL == *queue) {
        /* Queue was empty--- put ourselves at the head */
        *queue = import_merge_make_new_queue_entry(thang,key,fileno,passes);
        if (NULL == *queue) {
            return -1;
        }
    } else {
        for (current_entry = *queue; current_entry != NULL;
             current_entry = current_entry->next) {
            int cmp = strcmp(key->data,current_entry->key.data);

            if (0 == cmp) {
                if (IMPORT_MERGE_THANG_IDL == thang->type) { /* IDL case */
                    IDList *idl = thang->payload.idl;
                    /* Equal --- merge into the stored IDL, add file ID 
                     * to the list */
                    IDList *new_idl =
                        idl_union(be, current_entry->thang.payload.idl, idl);

                    idl_free(current_entry->thang.payload.idl);
                    idl_free(idl);
                    current_entry->thang.payload.idl = new_idl;
                    /* Add this file id into the entry's referenced list */
                    (current_entry->file_referenced_list)[fileno] = 1;
                    /* Because we merged the entries, we no longer need the
                     * key, so free it */
                    free(key->data);
                    goto done;
                } else {
                    /* VLV case, we can see exact keys, this is not a bug ! */
                    /* We want to ensure that they key read most recently is
                     * put later in the queue than any others though */
                }
            } else {
                if (cmp < 0) {
                    /* We compare smaller than the stored key, so we should
                     * insert ourselves before this entry */
                    break;
                } else {
                    /* We compare greater than this entry, so we should keep
                     * going */ ;
                }
            }
            previous_entry = current_entry;
        }

        /* Now insert */
        {	
            import_merge_queue_entry *new_entry =
                import_merge_make_new_queue_entry(thang, key, fileno, passes);

            if (NULL == new_entry) {
                return -1;
            }

            /* If not, then we must need to insert ourselves after the last 
             * entry */
            new_entry->next = current_entry;
            if (NULL == previous_entry) {
                *queue = new_entry;
            } else {
                previous_entry->next = new_entry;
            }
        }
    }

done:
    return 0;
}

static int import_merge_remove_input_queue(backend *be, import_merge_queue_entry **queue, import_merge_thang *thang,DBT *key,DBC **input_cursors, DB **input_files,int passes)
{
    import_merge_queue_entry *head = NULL;
    int file_referenced = 0;
    int i = 0;
    int ret = 0;

    PR_ASSERT(NULL != queue);
    head = *queue;
    if (head == NULL) {
        /* Means we've exhausted the queue---we're done */
        return EOF;
    }
    /* Remove the head of the queue */
    *queue = head->next;
    /* Get the IDL */
    *thang = head->thang;
    *key = head->key;
    PR_ASSERT(NULL != thang);
    /* Walk the list of referenced files, reading in the next IDL from each
     * one to the queue */
    for (i = 0 ; i < passes; i++) {
        import_merge_thang new_thang = {0};
        DBT new_key = {0};

        file_referenced = (head->file_referenced_list)[i];
        if (file_referenced) {
            ret = import_merge_get_next_thang(be, input_cursors[i],
                input_files[i], &new_thang, &new_key, thang->type); 
            if (0 != ret) {
                if (EOF == ret) {
                    /* Means that we walked off the end of the list,
                     * do nothing */ 
                    ret = 0;
                } else {
                    /* Some other error */
                    break;
                }
            } else {
		/* This function is responsible for any freeing needed */
                import_merge_insert_input_queue(be, queue, i, &new_key,
                    &new_thang, passes);
            }
        }
    }
    slapi_ch_free( (void**)&(head->file_referenced_list));
    slapi_ch_free( (void**)&head);

    return ret;
}

static int import_merge_open_input_cursors(DB**files, int passes, DBC ***cursors)
{
	int i = 0;
	int ret = 0;
	*cursors = (DBC**)slapi_ch_calloc(passes,sizeof(DBC*));
	if (NULL == *cursors) {
		return -1;
	}

	for (i = 0; i < passes; i++) {
		DB *pDB = files[i];
		DBC *pDBC = NULL;
		if (NULL != pDB) {
			/* Try to open a cursor onto the file */
			ret = pDB->cursor(pDB,NULL,&pDBC,0);
			if (0 != ret)  {
				break;
			} else {
				(*cursors)[i] = pDBC;
			}
		}
	}

	return ret;
}

static int import_count_merge_input_files(ldbm_instance *inst,
	char *indexname, int passes, int *number_found, int *pass_number)
{
    int i = 0;
    int found_one = 0;

    *number_found = 0;
    *pass_number = 0;

    for (i = 0; i < passes; i++) {
	int fd;
	char *filename = slapi_ch_smprintf("%s/%s.%d%s", inst->inst_dir_name, indexname, i+1,
		LDBM_FILENAME_SUFFIX);

	if (NULL == filename) {
	    return -1;
	}

	fd = dblayer_open_huge_file(filename, O_RDONLY, 0);
	slapi_ch_free( (void**)&filename);
	if (fd >= 0) {
	    close(fd);
	    if (found_one == 0) {
		*pass_number = i+1;
	    } 
	    found_one = 1;
	    (*number_found)++;
	} else {
	    ; /* Not finding a file is OK */
	}
    }

    return 0;
}

static int import_open_merge_input_files(backend *be, IndexInfo *index_info,
	int passes, DB ***input_files, int *number_found, int *pass_number)
{
    int i = 0;
    int ret = 0;
    int found_one = 0;

    *number_found = 0;
    *pass_number = 0;
    *input_files = (DB**)slapi_ch_calloc(passes,sizeof(DB*));
    if (NULL == *input_files) {
	/* Memory allocation error */
	return -1;
    }
    for (i = 0; i < passes; i++) {
	DB *pDB = NULL;
	char *filename = slapi_ch_smprintf("%s.%d", index_info->name, i+1);

	if (NULL == filename) {
	    return -1;
	}

	if (vlv_isvlv(filename)) {
		/* not sure why the file would be marked as a vlv index but
		   not the index configuration . . . but better make sure
		   the new code works with the old semantics */
		int saved_mask = index_info->ai->ai_indexmask;
		index_info->ai->ai_indexmask |= INDEX_VLV;
		ret = dblayer_open_file(be, filename, 0, index_info->ai, &pDB);
		index_info->ai->ai_indexmask = saved_mask;
	} else {
		ret = dblayer_open_file(be, filename, 0, index_info->ai, &pDB);
	}

	slapi_ch_free( (void**)&filename);
	if (0 == ret) {
	    if (found_one == 0) {
		*pass_number = i+1;
	    } 
	    found_one = 1;
	    (*number_found)++;
	    (*input_files)[i] = pDB;
	} else {
	    if (ENOENT == ret) {
		ret = 0; /* Not finding a file is OK */
	    } else {
		break;
	    }
	}
    }

    return ret;
}

/* Performs the n-way merge on one file */
static int import_merge_one_file(ImportWorkerInfo *worker, int passes,
				 int *key_count)
{
    ldbm_instance *inst = worker->job->inst;
    backend *be = inst->inst_be;
    DB *output_file = NULL;
    int ret = 0;
    int preclose_ret = 0;
    int number_found = 0;
    int pass_number = 0;

    PR_ASSERT(NULL != inst);
    
    /* Try to open all the input files.
       If we can't open file a file, we assume that is 
       because there was no data in it. */
    ret = import_count_merge_input_files(inst, worker->index_info->name,
					 passes, &number_found, &pass_number);
    if (0 != ret) {
	goto error;
    }
    /* If there were no input files, then we're finished ! */
    if (0 == number_found) {
	ret = 0;
	goto error;
    }
    /* Special-case where there's only one input file---just rename it */
    if (1 == number_found) {
	char *newname = NULL;
	char *oldname = NULL;

	ret = import_make_merge_filenames(inst->inst_dir_name,
		worker->index_info->name, pass_number, &oldname, &newname);
	if (0 != ret) {
	    import_log_notice(worker->job, "Failed making filename in merge");
	    goto error;
	}
	ret = PR_Rename(newname,oldname);
	if (0 != ret) {
		PRErrorCode prerr = PR_GetError();
	    import_log_notice(worker->job, "Failed to rename file \"%s\" to \"%s\" "
					"in merge, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)",
					oldname, newname, prerr, slapd_pr_strerror(prerr));
		slapi_ch_free( (void**)&newname);
		slapi_ch_free( (void**)&oldname);
	    goto error;
	}
	slapi_ch_free( (void**)&newname);
	slapi_ch_free( (void**)&oldname);
	*key_count = -1;
    } else {
	/* We really need to merge */
	import_merge_queue_entry *merge_queue = NULL;
	DB **input_files = NULL;
	DBC **input_cursors = NULL;
	DBT key = {0};
	import_merge_thang thang = {0};
	int i = 0;
	int not_finished = 1;
	int vlv_index = (INDEX_VLV == worker->index_info->ai->ai_indexmask);

#if 0
	/* Close and re-open regions, bugs otherwise */
	ret = dblayer_close(inst->inst_li, DBLAYER_IMPORT_MODE);
	if (0 != ret) {
	    if (ENOSPC == ret) {
		import_log_notice(worker->job, "FAILED: NO DISK SPACE LEFT");
	    } else {
		import_log_notice(worker->job, "MERGE FAIL 8 %d", ret);
	    }
	    return ret;
	}
	ret = dblayer_start(inst->inst_li, DBLAYER_IMPORT_MODE);
	if (0 != ret) {
	    import_log_notice(worker->job, "MERGE FAIL 9");
	    return ret;
	}
	ret = dblayer_instance_start(be, DBLAYER_IMPORT_MODE);
	if (0 != ret) {
	    import_log_notice(worker->job, "MERGE FAIL 9A");
	    return ret;
	}
#else
        /* we have reason to believe that it's okay to leave the region files
         * open in db3.x, since they track which files are opened and closed.
         * if we had to close the region files, we'd have to take down the
         * whole backend and defeat the purpose of an online import ---
         * baaad medicine.
         */
        ret = dblayer_instance_close(be);
        if (0 != ret) {
            import_log_notice(worker->job, "MERGE FAIL 8i %d\n", ret);
            return ret;
        }
        ret = dblayer_instance_start(be, DBLAYER_IMPORT_MODE);
        if (0 != ret) {
            import_log_notice(worker->job, "MERGE FAIL 8j %d\n", ret);
            return ret;
        }
#endif

	ret = import_open_merge_input_files(be, worker->index_info,
		passes, &input_files, &number_found, &pass_number);
	if (0 != ret) {
	    import_log_notice(worker->job, "MERGE FAIL 10");
	    return ret;
	}

	ret = dblayer_open_file(be, worker->index_info->name, 1,
				worker->index_info->ai, &output_file);
	if (0 != ret) {
	    import_log_notice(worker->job, "Failed to open output file for "
			      "index %s in merge", worker->index_info->name);
	    goto error;
	}

	/* OK, so we now have input and output files open and can proceed to 
	 * merge */
	/* We want to pre-fill the input IDL queue */
	/* Open cursors onto the input files */
	ret = import_merge_open_input_cursors(input_files, passes,
					      &input_cursors);
	if (0 != ret) {
	    import_log_notice(worker->job, "MERGE FAIL 2 %s %d",
			      worker->index_info->name, ret);
	    goto error;
	}

	/* Now read from the first location in each file and insert into the 
	 * queue */
	for (i = 0; i < passes; i++) if (input_files[i]) {
	    import_merge_thang prime_thang = {0};

	    /* Read an IDL from the file */
	    ret = import_merge_get_next_thang(be, input_cursors[i],
		input_files[i], &prime_thang, &key,
		vlv_index ? IMPORT_MERGE_THANG_VLV : IMPORT_MERGE_THANG_IDL);
	    if (0 != ret) {
		import_log_notice(worker->job, "MERGE FAIL 1 %s %d",
				  worker->index_info->name, ret);
		goto error;
	    }
	    /* Put it on the queue */
	    ret = import_merge_insert_input_queue(be, &merge_queue, i,& key,
						  &prime_thang, passes);
	    if (0 != ret) {
		import_log_notice(worker->job, "MERGE FAIL 0 %s",
				  worker->index_info->name);
		goto error;
	    }
	}

	/* We now have a pre-filled queue, so we may now proceed to remove the
	   head entry and write it to the output file, and repeat this process
	   until we've finished reading all the input data */
	while (not_finished && (0 == ret) ) {
	    ret = import_merge_remove_input_queue(be, &merge_queue, &thang,
		&key, input_cursors, input_files, passes);
	    if (0 != ret) {
		/* Have we finished cleanly ? */
		if (EOF == ret) {
		    not_finished = 0;
		} else {
		    import_log_notice(worker->job, "MERGE FAIL 3 %s, %d",
				      worker->index_info->name, ret);
		}
	    } else {
		/* Write it out */
		(*key_count)++;
		if (vlv_index) {
		    /* Write the vlv index */
		    ret = output_file->put(output_file, NULL, &key,
			&(thang.payload.vlv_data),0);
		    free(thang.payload.vlv_data.data);
		    thang.payload.vlv_data.data = NULL;
		} else {
		    /* Write the IDL index */
		    ret = idl_store_block(be, output_file, &key,
			thang.payload.idl, NULL, worker->index_info->ai);
		    /* Free the key we got back from the queue */
		    idl_free(thang.payload.idl);
		    thang.payload.idl = NULL;
		}
		free(key.data);
		key.data = NULL;
		if (0 != ret) {
		    /* Failed to write--- most obvious cause being out of 
		       disk space, let's make sure that we at least print a
		       sensible error message right here. The caller should
		       really handle this properly, but we're always bad at
		       this. */
		    if (ret == DB_RUNRECOVERY || ret == ENOSPC) {
			import_log_notice(worker->job, "OUT OF SPACE ON DISK, "
					  "failed writing index file %s",
					  worker->index_info->name);
		    } else {
			import_log_notice(worker->job, "Failed to write "
					  "index file %s, errno=%d (%s)\n",
					  worker->index_info->name, errno,
					  dblayer_strerror(errno));
		    }
		}
	    }
	}
	preclose_ret = ret;
	/* Now close the files */
	dblayer_close_file(output_file);
	/* Close the cursors */
	/* Close and delete the files */
	for (i = 0; i < passes; i++) {
	    DBC *cursor = input_cursors[i];
	    DB *db = input_files[i];
	    if (NULL != db) {
		PR_ASSERT(NULL != cursor);
		ret = cursor->c_close(cursor);
		if (0 != ret) {
		    import_log_notice(worker->job, "MERGE FAIL 4");
		} 
		ret = dblayer_close_file(db);
		if (0 != ret) {
		    import_log_notice(worker->job, "MERGE FAIL 5");
		}
		/* Now make the filename and delete the file */
		{
		    char *newname = NULL;
		    char *oldname = NULL;
		    ret = import_make_merge_filenames(inst->inst_dir_name,
			worker->index_info->name, i+1, &oldname, &newname);
		    if (0 != ret) {
			import_log_notice(worker->job, "MERGE FAIL 6");
		    } else {
			ret = PR_Delete(newname);
			if (0 != ret) {
			    import_log_notice(worker->job, "MERGE FAIL 7");
			}
			slapi_ch_free( (void**)&newname);
			slapi_ch_free( (void**)&oldname);
		    }
		}
	    }			
	}
	if (preclose_ret != 0) ret = preclose_ret;
	slapi_ch_free( (void**)&input_files);
	slapi_ch_free( (void**)&input_cursors);
    }
    if (EOF == ret) {
	ret = 0;
    }

error:
    return ret;
}

/********** the real deal here: **********/

/* Our mission here is as follows: 
 * for each index job except entrydn and id2entry:
 *     open all the pass files
 *     open a new output file
 *     iterate cursors over all of the input files picking each distinct
 *         key and combining the input IDLs into a merged IDL. Put that
 *         IDL to the output file.
 */
int import_mega_merge(ImportJob *job)
{
    ImportWorkerInfo *current_worker = NULL;
    int ret = 0;
    time_t beginning = 0;
    time_t end = 0;
    int passes = job->current_pass;

    if (1 == job->number_indexers) {
	import_log_notice(job, "Beginning %d-way merge of one file...", passes);
    } else {
	import_log_notice(job, "Beginning %d-way merge of up to %lu files...",
			  passes, job->number_indexers);
    }

    time(&beginning);
    /* Iterate over the files */
    for (current_worker = job->worker_list; 
	 (ret == 0) && (current_worker != NULL);
	 current_worker = current_worker->next) {
	/* We need to ignore the primary index */
	if ((current_worker->work_type != FOREMAN) && 
	    (current_worker->work_type != PRODUCER)) {
	    time_t file_beginning = 0;
	    time_t file_end = 0;
	    int key_count = 0;

	    time(&file_beginning);
	    ret = import_merge_one_file(current_worker,passes,&key_count);
	    time(&file_end);
	    if (key_count == 0) {
		import_log_notice(job, "No files to merge for \"%s\".",
				  current_worker->index_info->name);
	    } else {
		if (-1 == key_count) {
		    import_log_notice(job, "Merged \"%s\": Simple merge - "
				      "file renamed.", 
				      current_worker->index_info->name);
		} else {
		    import_log_notice(job, "Merged \"%s\": %d keys merged "
				      "in %ld seconds.",
				      current_worker->index_info->name,
				      key_count, file_end-file_beginning);
		}
	    }
	}
    }

    time(&end);
    if (0 == ret) {
	int seconds_to_merge = end - beginning;
	
	import_log_notice(job, "Merging completed in %d seconds.",
			  seconds_to_merge);
    }

    return ret;
}

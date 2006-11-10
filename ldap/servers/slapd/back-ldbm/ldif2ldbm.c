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

/* ldif2ldbm.c
 *
 * common functions for import (old and new) and export
 * the export code (db2ldif)
 * code for db2index (is this still in use?)
 */

#include "back-ldbm.h"
#include "vlv_srch.h"
#include "dblayer.h"
#include "import.h"

static char *sourcefile = "ldif2ldbm.c";


static int db2index_add_indexed_attr(backend *be, char *attrString);

static int ldbm_exclude_attr_from_export( struct ldbminfo *li,
		const char *attr, int dump_uniqueid );


/**********  common routines for classic/deluxe import code **********/

static size_t import_config_index_buffer_size = DEFAULT_IMPORT_INDEX_BUFFER_SIZE;

void import_configure_index_buffer_size(size_t size)
{
    import_config_index_buffer_size = size;
}

size_t import_get_index_buffer_size() {
    return import_config_index_buffer_size;
}

static PRIntn import_subcount_hash_compare_keys(const void *v1, const void *v2)
{
    return( ((ID)v1 == (ID)v2 ) ? 1 : 0);
}

static PRIntn import_subcount_hash_compare_values(const void *v1, const void *v2)
{
    return( ((size_t)v1 == (size_t)v2 ) ? 1 : 0);
}

static PLHashNumber import_subcount_hash_fn(const void *id)
{
    return (PLHashNumber) id;
}

void import_subcount_stuff_init(import_subcount_stuff *stuff)
{
    stuff->hashtable = PL_NewHashTable(IMPORT_SUBCOUNT_HASHTABLE_SIZE,
	import_subcount_hash_fn, import_subcount_hash_compare_keys,
	import_subcount_hash_compare_values, NULL, NULL);
}

void import_subcount_stuff_term(import_subcount_stuff *stuff)
{
    if ( stuff != NULL && stuff->hashtable != NULL ) {
	PL_HashTableDestroy(stuff->hashtable);
    }
}

/* fetch include/exclude DNs from the pblock and normalize them --
 * returns true if there are any include/exclude DNs
 * [used by both ldif2db and db2ldif]
 */
int ldbm_back_fetch_incl_excl(Slapi_PBlock *pb, char ***include,
			      char ***exclude)
{
    char **pb_incl, **pb_excl;
    char subtreeDn[BUFSIZ];
    char *normSubtreeDn;
    int i;

    slapi_pblock_get(pb, SLAPI_LDIF2DB_INCLUDE, &pb_incl);
    slapi_pblock_get(pb, SLAPI_LDIF2DB_EXCLUDE, &pb_excl);
    *include = *exclude = NULL;

    /* normalize */
    if (pb_excl) {
	for (i = 0; pb_excl[i]; i++) {
	    PL_strncpyz(subtreeDn, pb_excl[i], sizeof(subtreeDn));
	    normSubtreeDn = slapi_dn_normalize_case(subtreeDn);
	    charray_add(exclude, slapi_ch_strdup(normSubtreeDn));
	}
    }
    if (pb_incl) {
	for (i = 0; pb_incl[i]; i++) {
	    PL_strncpyz(subtreeDn, pb_incl[i], sizeof(subtreeDn));
	    normSubtreeDn = slapi_dn_normalize_case(subtreeDn);
	    charray_add(include, slapi_ch_strdup(normSubtreeDn));
	}
    }
    return (pb_incl || pb_excl);
}

void ldbm_back_free_incl_excl(char **include, char **exclude)
{
    if (include) {
	charray_free(include);
    }
    if (exclude) {
	charray_free(exclude);
    }
}

/* check if a DN is in the include list but NOT the exclude list
 * [used by both ldif2db and db2ldif]
 */
int ldbm_back_ok_to_dump(const char *dn, char **include, char **exclude)
{
    int i = 0;
    
    if (!(include || exclude))
	return(1);

    if (exclude) {
	i = 0;
	while (exclude[i]) {
	    if (slapi_dn_issuffix(dn,exclude[i]))
		return(0);
	    i++;
	}
    }

    if (include) {
	i = 0;
	while (include[i]) {
	    if (slapi_dn_issuffix(dn,include[i]))
		return(1);
	    i++;
	}
	/* not in include... bye. */
	return(0);
    }

    return(1);
}


/*
 * add_op_attrs - add the parentid, entryid, dncomp,
 * and entrydn operational attributes to an entry. 
 * Also---new improved washes whiter than white version
 * now removes any bogus operational attributes you're not
 * allowed to specify yourself on entries.
 * Currenty the list of these is: numSubordinates, hasSubordinates
 */
int add_op_attrs(Slapi_PBlock *pb, struct ldbminfo *li, struct backentry *ep,
		 int *status)
{
    backend *be;
    const char *pdn;
    ID pid = 0;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);

    /*
     * add the parentid and entryid operational attributes
     */

    if (NULL != status) {
	*status = IMPORT_ADD_OP_ATTRS_OK;
    }

    /* parentid */
    if ( (pdn = slapi_dn_parent( backentry_get_ndn(ep))) != NULL ) {
	struct berval bv;
	IDList *idl;
	int err = 0;

	/*
	 * read the entrydn index to get the id of the parent
	 * If this entry's parent is not present in the index, 
	 * we'll get a DB_NOTFOUND error here.
	 * In olden times, we just ignored this, but now...
	 * we see this as meaning that the entry is either a
	 * suffix entry, or its erroneous. So, we signal this to the
	 * caller via the status parameter.
	 */
        bv.bv_val = (char *)pdn;
	bv.bv_len = strlen(pdn);
	if ( (idl = index_read( be, "entrydn", indextype_EQUALITY, &bv, NULL,
				&err )) != NULL ) {
	    pid = idl_firstid( idl );
	    idl_free( idl );
	} else {
	    /* empty idl */
	    if ( 0 != err && DB_NOTFOUND != err ) {
		LDAPDebug( LDAP_DEBUG_ANY, "database error %d\n", err, 0, 0 );
		slapi_ch_free( (void**)&pdn );
		return( -1 );
	    }
	    if (NULL != status) {
	        *status = IMPORT_ADD_OP_ATTRS_NO_PARENT;
	    }
	}
	slapi_ch_free( (void**)&pdn );
    } else {
	if (NULL != status) {
	    *status = IMPORT_ADD_OP_ATTRS_NO_PARENT;
	}
    }

    /* Get rid of attributes you're not allowed to specify yourself */
    slapi_entry_delete_values( ep->ep_entry, hassubordinates, NULL );
    slapi_entry_delete_values( ep->ep_entry, numsubordinates, NULL );
    
    /* Add the entryid, parentid and entrydn operational attributes */
    /* Note: This function is provided by the Add code */
    add_update_entry_operational_attributes(ep, pid);
    
    return( 0 );
}

/**********  functions for maintaining the subordinate count **********/

/* Update subordinate count in a hint list, given the parent's ID */
int import_subcount_mother_init(import_subcount_stuff *mothers, ID parent_id,
				size_t count)
{
    PR_ASSERT(NULL == PL_HashTableLookup(mothers->hashtable,(void*)parent_id));
    PL_HashTableAdd(mothers->hashtable,(void*)parent_id,(void*)count);
    return 0;
}

/* Look for a subordinate count in a hint list, given the parent's ID */
static int import_subcount_mothers_lookup(import_subcount_stuff *mothers,
	ID parent_id, size_t *count)
{
    size_t stored_count = 0;

    *count = 0;
    /* Lookup hash table for ID */
    stored_count = (size_t)PL_HashTableLookup(mothers->hashtable,
					      (void*)parent_id);
    /* If present, return the count found */
    if (0 != stored_count) {
	*count = stored_count;
	return 0;
    }
    return -1;
}

/* Update subordinate count in a hint list, given the parent's ID */
int import_subcount_mother_count(import_subcount_stuff *mothers, ID parent_id)
{
    size_t stored_count = 0;

    /* Lookup the hash table for the target ID */
    stored_count = (size_t)PL_HashTableLookup(mothers->hashtable,
					      (void*)parent_id);
    PR_ASSERT(0 != stored_count);
    /* Increment the count */
    stored_count++;
    PL_HashTableAdd(mothers->hashtable, (void*)parent_id, (void*)stored_count);
    return 0;
}

static int import_update_entry_subcount(backend *be, ID parentid,
					size_t sub_count)
{
    ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
    int ret = 0;
    modify_context mc = {0};
    char value_buffer[20]; /* enough digits for 2^64 children */
    struct backentry *e = NULL;
    int isreplace = 0;

    /* Get hold of the parent */
    e = id2entry(be,parentid,NULL,&ret);
    if ( (NULL == e) || (0 != ret)) {
	ldbm_nasty(sourcefile,5,ret);
	return (0 == ret) ? -1 : ret;
    }
    /* Lock it (not really required since we're single-threaded here, but 
     * let's do it so we can reuse the modify routines) */
    cache_lock_entry( &inst->inst_cache, e );
    modify_init(&mc,e);
    sprintf(value_buffer,"%lu",sub_count);
    /* attr numsubordinates could already exist in the entry,
       let's check whether it's already there or not */
    isreplace = (attrlist_find(e->ep_entry->e_attrs, numsubordinates) != NULL);
    {
	int op = isreplace ? LDAP_MOD_REPLACE : LDAP_MOD_ADD;
	Slapi_Mods *smods= slapi_mods_new();

        slapi_mods_add(smods, op | LDAP_MOD_BVALUES, numsubordinates,
			strlen(value_buffer), value_buffer);
    	ret = modify_apply_mods(&mc,smods); /* smods passed in */
    }
    if (0 == ret || LDAP_TYPE_OR_VALUE_EXISTS == ret) {
	/* This will correctly index subordinatecount: */
	ret = modify_update_all(be,NULL,&mc,NULL);
	if (0 == ret) {
	    modify_switch_entries( &mc,be);
	}
    }
    modify_term(&mc,be);
    return ret;
}

struct _import_subcount_trawl_info {
    struct _import_subcount_trawl_info *next;
    ID id;
    size_t sub_count;
};
typedef struct _import_subcount_trawl_info import_subcount_trawl_info;

static void import_subcount_trawl_add(import_subcount_trawl_info **list, ID id)
{
    import_subcount_trawl_info *new_info = CALLOC(import_subcount_trawl_info);

    new_info->next = *list;
    new_info->id = id;
    *list = new_info;
}

static int import_subcount_trawl(backend *be, import_subcount_trawl_info *trawl_list)
{
    ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
    ID id = 1;
    int ret = 0;
    import_subcount_trawl_info *current = NULL;
    char value_buffer[20]; /* enough digits for 2^64 children */

    /* OK, we do */
    /* We open id2entry and iterate through it */
    /* Foreach entry, we check to see if its parentID matches any of the 
     * values in the trawl list . If so, we bump the sub count for that
     * parent in the list.
     */
    while (1) {
        struct backentry *e = NULL;

        /* Get the next entry */
        e = id2entry(be,id,NULL,&ret);
        if ( (NULL == e) || (0 != ret)) {
            if (DB_NOTFOUND == ret) {
                break;
            } else {
                ldbm_nasty(sourcefile,8,ret);
                return ret;
            }
        }
        for (current = trawl_list; current != NULL; current = current->next) {
            sprintf(value_buffer,"%lu",(u_long)current->id);
            if (slapi_entry_attr_hasvalue(e->ep_entry,"parentid",value_buffer)) {
                /* If this entry's parent ID matches one we're trawling for, 
                 * bump its count */
                current->sub_count++;
            }
        }
        /* Free the entry */
        cache_remove(&inst->inst_cache, e);
        cache_return(&inst->inst_cache, &e);
        id++;
    }
    /* Now update the parent entries from the list */
    for (current = trawl_list; current != NULL; current = current->next) {
        /* Update the parent entry with the correctly counted subcount */
        ret = import_update_entry_subcount(be,current->id,current->sub_count);
        if (0 != ret) {
            ldbm_nasty(sourcefile,10,ret);
            break;
        }
    }
    return ret;
}

/*
 * Function: update_subordinatecounts
 *
 * Returns: Nothing
 * 
 */
int update_subordinatecounts(backend *be, import_subcount_stuff *mothers,
			     DB_TXN *txn)
{
	int ret = 0;
	DB *db    = NULL;
	DBC *dbc  = NULL; 
	struct attrinfo  *ai  = NULL;
	DBT key  = {0};
	DBT data = {0};
	import_subcount_trawl_info *trawl_list = NULL;

	/* Open the parentid index */
	ainfo_get( be, "parentid", &ai );

	/* Open the parentid index file */
	if ( (ret = dblayer_get_index_file( be, ai, &db, DBOPEN_CREATE )) != 0 ) {
		ldbm_nasty(sourcefile,67,ret);
		return(ret);
	}

	/* Get a cursor so we can walk through the parentid */
	ret = db->cursor(db,txn,&dbc,0);
	if (ret != 0 ) {
		ldbm_nasty(sourcefile,68,ret);
                dblayer_release_index_file( be, ai, db );
		return ret;
	}

	/* Walk along the index */
	while (1) {
		size_t sub_count = 0;
		int found_count = 1;
		ID parentid = 0;
	
		/* Foreach key which is an equality key : */
		data.flags = DB_DBT_MALLOC;
		key.flags = DB_DBT_MALLOC;
		ret = dbc->c_get(dbc,&key,&data,DB_NEXT_NODUP);
		if (NULL != data.data) {
			free(data.data);
			data.data = NULL;
		}
		if (0 != ret) {
			if (ret != DB_NOTFOUND) {
				ldbm_nasty(sourcefile,62,ret);
			}
			if (NULL != key.data) {
				free(key.data);
				key.data = NULL;
			}
			break;
		}
		if (*(char*)key.data == EQ_PREFIX) {
			char *idptr = NULL;
	
			/* construct the parent's ID from the key */
			/* Look for the ID in the hint list supplied by the caller */
			/* If its there, we know the answer already */
			idptr = (((char *) key.data) + 1);
			parentid = (ID) atol(idptr);
			PR_ASSERT(0 != parentid);
			ret = import_subcount_mothers_lookup(mothers,parentid,&sub_count);
			if (0 != ret) {
				IDList *idl = NULL;
		
				/* If it's not, we need to compute it ourselves: */
				/* Load the IDL matching the key */
				key.flags = DB_DBT_REALLOC;
				ret = NEW_IDL_NO_ALLID;
				idl = idl_fetch(be,db,&key,NULL,NULL,&ret);
				if ( (NULL == idl) || (0 != ret)) {
					ldbm_nasty(sourcefile,4,ret);
                                        dblayer_release_index_file( be, ai, db );
					return (0 == ret) ? -1 : ret;
				}
				/* The number of IDs in the IDL tells us the number of
				 * subordinates for the entry */
				/* Except, the number might be above the allidsthreshold,
				 * in which case */
				if (ALLIDS(idl)) {
					/* We add this ID to the list for which to trawl */
					import_subcount_trawl_add(&trawl_list,parentid);
					found_count = 0;
				} else {
					/* We get the count from the IDL */
					sub_count = idl->b_nids;
				}
				idl_free(idl);
			} 
			/* Did we get the count ? */
			if (found_count) {
				PR_ASSERT(0 != sub_count);
				/* If so, update the parent now */
				import_update_entry_subcount(be,parentid,sub_count);
			}
		}
		if (NULL != key.data) {
			free(key.data);
			key.data = NULL;
		}
	}

	ret = dbc->c_close(dbc);
	if (0 != ret) {
		ldbm_nasty(sourcefile,6,ret);
	}
	dblayer_release_index_file( be, ai, db );

	/* Now see if we need to go trawling through id2entry for the info
	 * we need */
	if (NULL != trawl_list) {
		ret = import_subcount_trawl(be,trawl_list);
		if (0 != ret) {
			ldbm_nasty(sourcefile,7,ret);
		}
	}
	return(ret);
}


/**********  ldif2db entry point  **********/

/*
	Some notes about this stuff: 

	The front-end does call our init routine before calling us here.
	So, we get the regular chance to parse the config file etc.
	However, it does _NOT_ call our start routine, so we need to
	do whatever work that did and which we need for this work , here.
	Furthermore, the front-end simply exits after calling us, so we need
	to do any cleanup work here also.
 */

/*
 * ldbm_back_ldif2ldbm - backend routine to convert an ldif file to
 * a database.
 */
int ldbm_back_ldif2ldbm( Slapi_PBlock *pb )
{
    struct ldbminfo *li;
    ldbm_instance *inst = NULL;
    char *instance_name;
    int ret, task_flags;
    
    slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
    slapi_pblock_get( pb, SLAPI_BACKEND_INSTANCE_NAME, &instance_name );

    /* BEGIN complex dependencies of various initializations. */
    /* hopefully this will go away once import is not run standalone... */

    slapi_pblock_get(pb, SLAPI_TASK_FLAGS, &task_flags);
    if (task_flags & TASK_RUNNING_FROM_COMMANDLINE) {
        li->li_flags |= TASK_RUNNING_FROM_COMMANDLINE;
        ldbm_config_load_dse_info(li);
        autosize_import_cache(li);
    }

    /* Find the instance that the ldif2db will be done on. */
    inst = ldbm_instance_find_by_name(li, instance_name);
    if (NULL == inst) {
        LDAPDebug(LDAP_DEBUG_ANY, "Unknown ldbm instance %s\n", instance_name,
                  0, 0);
        return -1;
    }

    /* check if an import/restore is already ongoing... */
    if (instance_set_busy(inst) != 0) {
        LDAPDebug(LDAP_DEBUG_ANY, "ldbm: '%s' is already in the middle of "
                  "another task and cannot be disturbed.\n",
                  inst->inst_name, 0, 0);
        return -1;
    }

    /***** prepare & init libdb and dblayer *****/

    if (! (task_flags & TASK_RUNNING_FROM_COMMANDLINE)) {
        /* shutdown this instance of the db */
        LDAPDebug(LDAP_DEBUG_ANY, "Bringing %s offline...\n", 
                  instance_name, 0, 0);
        slapi_mtn_be_disable(inst->inst_be);

        cache_clear(&inst->inst_cache);
        dblayer_instance_close(inst->inst_be);
    	dblayer_delete_indices(inst);
    } else {
        /* from the command line, libdb needs to be started up */
        ldbm_config_internal_set(li, CONFIG_DB_TRANSACTION_LOGGING, "off");

        if (0 != (ret = dblayer_start(li, DBLAYER_IMPORT_MODE)) ) {
            if (LDBM_OS_ERR_IS_DISKFULL(ret)) {
                LDAPDebug(LDAP_DEBUG_ANY, "ERROR: Failed to init database.  "
                          "There is either insufficient disk space or "
                          "insufficient memory available to initialize the "
                          "database.\n", 0, 0, 0);
                LDAPDebug(LDAP_DEBUG_ANY,"Please check that\n"
                          "1) disks are not full,\n"
                          "2) no file exceeds the file size limit,\n"
                          "3) the configured dbcachesize is not too large for the available memory on this machine.\n",
                                                  0, 0, 0);
            } else {
                LDAPDebug(LDAP_DEBUG_ANY, "ERROR: Failed to init database "
                          "(error %d: %s)\n", ret, dblayer_strerror(ret), 0);
            }
            goto fail;
        }
    }

    /* Delete old database files */
    dblayer_delete_instance_dir(inst->inst_be);
    /* it's okay to fail -- the directory might have already been deleted */

    /* dblayer_instance_start will init the id2entry index. */
    /* it also (finally) fills in inst_dir_name */
    ret = dblayer_instance_start(inst->inst_be, DBLAYER_IMPORT_MODE);
    if (ret != 0) {
        goto fail;
    }

    vlv_init(inst);

    /***** done init libdb and dblayer *****/

    /* always use "new" import code now */
    slapi_pblock_set(pb, SLAPI_BACKEND, inst->inst_be);
    return ldbm_back_ldif2ldbm_deluxe(pb);

fail:
    /* DON'T enable the backend -- leave it offline */
    instance_set_not_busy(inst);
    return ret;
}


/**********  db2ldif, db2index  **********/


/* fetch an IDL for the series of subtree specs */
/* (used for db2ldif) */
static IDList *ldbm_fetch_subtrees(backend *be, char **include, int *err)
{
    int i;
    ID id;
    IDList *idltotal = NULL, *idltmp;
    back_txn *txn = NULL;
    struct berval bv;

    /* for each subtree spec... */
    for (i = 0; include[i]; i++) {
	IDList *idl = NULL;

        /* 
         * First map the suffix to its entry ID.
         * Note that the suffix is already normalized.
         */
	bv.bv_val = include[i];
	bv.bv_len = strlen(include[i]);
        idl = index_read(be, "entrydn", indextype_EQUALITY, &bv, txn, err);
        if (idl == NULL) {
            LDAPDebug(LDAP_DEBUG_ANY, "warning: entrydn not indexed on '%s'\n",
                      include[i], 0, 0);
            continue;
        }
        id = idl_firstid(idl);
        idl_free(idl);
        idl = NULL;

        /*
         * Now get all the descendants of that suffix.
         */
        *err = ldbm_ancestorid_read(be, txn, id, &idl);
        if (idl == NULL) {
            LDAPDebug(LDAP_DEBUG_ANY, "warning: ancestorid not indexed on %lu\n",
                      id, 0, 0);
            continue;
        }

        /* Insert the suffix itself */
        idl_insert(&idl, id);

        /* Merge the idlists */
	if (! idltotal) {
	    idltotal = idl;
	} else if (idl) {
	    idltmp = idl_union(be, idltotal, idl);
	    idl_free(idltotal);
	    idl_free(idl);
	    idltotal = idltmp;
	}
    }
    
    return idltotal;
}

#define FD_STDOUT 1


/*
 * ldbm_back_ldbm2ldif - backend routine to convert database to an
 * ldif file.
 * (reunified at last)
 */
int
ldbm_back_ldbm2ldif( Slapi_PBlock *pb )
{
    backend          *be;
    struct ldbminfo  *li = NULL;
    DB               *db = NULL;
    DBC              *dbc = NULL;
    struct backentry *ep;
    DBT              key = {0};
    DBT              data = {0};
    char             *type, *fname = NULL;
    int              len, printkey, rc, ok_index;
    int              return_value = 0;
    int              nowrap = 0;
    int              nobase64 = 0;
    NIDS             idindex = 0;
    ID               temp_id;
    char             **exclude_suffix = NULL;
    char             **include_suffix = NULL;
    int              decrypt = 0;
    int              dump_replica = 0;
    int              dump_uniqueid = 1;
    int              fd;
    IDList           *idl = NULL;    /* optimization for -s include lists */
    int              cnt = 0, lastcnt = 0;
    int              options = 0;
    int              keepgoing = 1;
    int              isfirst = 1;
    int              appendmode = 0;
    int              appendmode_1 = 0;
    int              noversion = 0;
    ID               lastid;
    int              task_flags;
    Slapi_Task       *task;
    int              run_from_cmdline = 0;
    char             *instance_name;
    ldbm_instance    *inst;
    int              str2entry_options= 0;
    int              retry;
    int              we_start_the_backends = 0;
    int              server_running;

    LDAPDebug( LDAP_DEBUG_TRACE, "=> ldbm_back_ldbm2ldif\n", 0, 0, 0 );

    slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
    slapi_pblock_get( pb, SLAPI_TASK_FLAGS, &task_flags );
    slapi_pblock_get( pb, SLAPI_DB2LDIF_DECRYPT, &decrypt );
    slapi_pblock_get( pb, SLAPI_DB2LDIF_SERVER_RUNNING, &server_running );
    run_from_cmdline = (task_flags & TASK_RUNNING_FROM_COMMANDLINE);

    dump_replica = pb->pb_ldif_dump_replica;
    if (run_from_cmdline) {
        li->li_flags |= TASK_RUNNING_FROM_COMMANDLINE;
        if (!dump_replica) {
            we_start_the_backends = 1;
        }
    }

    if (we_start_the_backends) {
        /* No ldbm be's exist until we process the config information. */

        /*
         * Note that we should only call this once. If we're
         * dumping several backends then it gets called multiple
         * times and we get warnings in the error log like this:
         *   WARNING: ldbm instance NetscapeRoot already exists
         */
        ldbm_config_load_dse_info(li);
    }

    if (run_from_cmdline && li->li_dblayer_private->dblayer_private_mem
        && server_running)
    {
        LDAPDebug(LDAP_DEBUG_ANY,
             "Cannot export the database while the server is running and "
             "nsslapd-db-private-mem option is used, "
             "please use ldif2db.pl\n", 0, 0, 0);
             return_value = -1;
        goto bye;
    }

    if (run_from_cmdline) {

        /* Now that we have processed the config information, we look for
         * the be that should do the db2ldif. */
        slapi_pblock_get(pb, SLAPI_BACKEND_INSTANCE_NAME, &instance_name);
        inst = ldbm_instance_find_by_name(li, instance_name);
        if (NULL == inst) {
            LDAPDebug(LDAP_DEBUG_ANY, "Unknown ldbm instance %s\n",
                          instance_name, 0, 0);
            return_value = -1;
            goto bye;
        }
        /* [605974] command db2ldif should not be able to run when on-line 
         * import is running */
        if (dblayer_in_import(inst)) {
            LDAPDebug(LDAP_DEBUG_ANY, "instance %s is busy\n",
                          instance_name, 0, 0);
            return_value = -1;
            goto bye;
        }

        /* store the be in the pb */
        be = inst->inst_be;
        slapi_pblock_set(pb, SLAPI_BACKEND, be);
    } else {
        slapi_pblock_get(pb, SLAPI_BACKEND, &be);
        inst = (ldbm_instance *)be->be_instance_info;

        if (NULL == inst) {
            LDAPDebug(LDAP_DEBUG_ANY, "Unknown ldbm instance\n", 0, 0, 0);
            return_value = -1;
            goto bye;
        }

        /* check if an import/restore is already ongoing... */
        if (instance_set_busy(inst) != 0) {
            LDAPDebug(LDAP_DEBUG_ANY, "ldbm: '%s' is already in the middle"
                    " of another task and cannot be disturbed.\n",
                    inst->inst_name, 0, 0);
            return_value = -1;
            goto bye;
        }
    }

    slapi_pblock_get( pb, SLAPI_BACKEND_TASK, &task );
    
    ldbm_back_fetch_incl_excl(pb, &include_suffix, &exclude_suffix);

    str2entry_options= (dump_replica?0:SLAPI_STR2ENTRY_TOMBSTONE_CHECK);

    slapi_pblock_get( pb, SLAPI_DB2LDIF_FILE, &fname );
    slapi_pblock_get( pb, SLAPI_DB2LDIF_PRINTKEY, &printkey );
    slapi_pblock_get( pb, SLAPI_DB2LDIF_DUMP_UNIQUEID, &dump_uniqueid );

    /* tsk, overloading printkey.  shame on me. */
    ok_index = !(printkey & EXPORT_ID2ENTRY_ONLY);
    printkey &= ~EXPORT_ID2ENTRY_ONLY;

    nobase64 = (printkey & EXPORT_MINIMAL_ENCODING);
    printkey &= ~EXPORT_MINIMAL_ENCODING;
    nowrap = (printkey & EXPORT_NOWRAP);
    printkey &= ~EXPORT_NOWRAP;
    appendmode = (printkey & EXPORT_APPENDMODE);
    printkey &= ~EXPORT_APPENDMODE;
    appendmode_1 = (printkey & EXPORT_APPENDMODE_1);
    printkey &= ~EXPORT_APPENDMODE_1;
    noversion = (printkey & EXPORT_NOVERSION);
    printkey &= ~EXPORT_NOVERSION;

    /* decide whether to dump uniqueid */
    if (dump_uniqueid)
        options |= SLAPI_DUMP_UNIQUEID;
    if (nowrap)
        options |= SLAPI_DUMP_NOWRAP;
    if (nobase64)
        options |= SLAPI_DUMP_MINIMAL_ENCODING;
    if (dump_replica)
        options |= SLAPI_DUMP_STATEINFO;

    if (fname == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "db2ldif: no LDIF filename supplied\n",
                      0, 0, 0);
        return_value = -1;
        goto bye;
    }

    if (strcmp(fname, "-")) {    /* not '-' */
        if (appendmode) {
            if (appendmode_1) {
                fd = dblayer_open_huge_file(fname, O_WRONLY|O_CREAT|O_TRUNC,
                                        SLAPD_DEFAULT_FILE_MODE);
            } else {
                fd = dblayer_open_huge_file(fname, O_WRONLY|O_CREAT|O_APPEND,
                                        SLAPD_DEFAULT_FILE_MODE);
            }
        } else {
            /* open it */
            fd = dblayer_open_huge_file(fname, O_WRONLY|O_CREAT|O_TRUNC,
                    SLAPD_DEFAULT_FILE_MODE);
        }
        if (fd < 0) {
            LDAPDebug(LDAP_DEBUG_ANY, "db2ldif: can't open %s: %d (%s)\n",
                  fname, errno, dblayer_strerror(errno));
            return_value = -1;
            goto bye;
        }
    } else {                    /* '-' */
        fd = FD_STDOUT;
    }

    if ( we_start_the_backends )  {
        if (0 != dblayer_start(li,DBLAYER_EXPORT_MODE)) {
            LDAPDebug( LDAP_DEBUG_ANY, "db2ldif: Failed to init database\n",
                0, 0, 0 );
            return_value = -1;
            goto bye;
        }
        /* dblayer_instance_start will init the id2entry index. */
        if (0 != dblayer_instance_start(be, DBLAYER_EXPORT_MODE)) {
            LDAPDebug(LDAP_DEBUG_ANY, "db2ldif: Failed to init instance\n",
                0, 0, 0);
            return_value = -1;
            goto bye;
        }
    }

    /* idl manipulation requires nextid to be init'd now */
    if (include_suffix && ok_index)
        get_ids_from_disk(be);

    if ((( dblayer_get_id2entry( be, &db )) != 0) || (db == NULL)) {
        LDAPDebug( LDAP_DEBUG_ANY, "Could not open/create id2entry\n",
            0, 0, 0 );
        ldbm_back_free_incl_excl(include_suffix, exclude_suffix);
        return_value = -1;
        goto bye;
    }

    /* if an include_suffix was given (and we're pretty sure the
     * entrydn and ancestorid indexes are valid), we try to
     * assemble an id-list of candidates instead of plowing thru
     * the whole database.  this is a big performance improvement
     * when exporting config info (which is usually on the order
     * of 100 entries) from a database that may be on the order of
     * GIGS in size.
     */
    {
        /* Here, we assume that the table is ordered in EID-order, 
         * which it is !
         */
        /* get a cursor to we can walk over the table */
        return_value = db->cursor(db,NULL,&dbc,0);
        if (0 != return_value ) {
            LDAPDebug( LDAP_DEBUG_ANY,
               "Failed to get cursor for db2ldif\n",
               0, 0, 0 );
            ldbm_back_free_incl_excl(include_suffix, exclude_suffix);
            return_value = -1;
            goto bye;
        }
        key.flags = DB_DBT_MALLOC;
        data.flags = DB_DBT_MALLOC;
        return_value = dbc->c_get(dbc,&key,&data,DB_LAST);
        if (0 != return_value) {
            keepgoing = 0;
        } else {
            lastid = id_stored_to_internal((char *)key.data);
            free( key.data );
            free( data.data );
            isfirst = 1;
        }
    }
    if (include_suffix && ok_index && !dump_replica) {
        int err;

        idl = ldbm_fetch_subtrees(be, include_suffix, &err);
        if (! idl) {
            /* most likely, indexes are bad. */
            LDAPDebug(LDAP_DEBUG_ANY,
                  "Failed to fetch subtree lists (error %d) %s\n",
                  err, dblayer_strerror(err), 0);
            LDAPDebug(LDAP_DEBUG_ANY,
                  "Possibly the entrydn or ancestorid index is corrupted or "
                  "does not exist.\n", 0, 0, 0);
            LDAPDebug(LDAP_DEBUG_ANY,
                  "Attempting direct unindexed export instead.\n",
                  0, 0, 0);
            ok_index = 0;
            idl = NULL;
        } else if (ALLIDS(idl)) {
            /* allids list is no help at all -- revert to trawling
             * the whole list. */
            ok_index = 0;
            idl_free(idl);
            idl = NULL;
        }
        idindex = 0;
    }

    /* When user has specifically asked not to print the version
     * or when this is not the first backend that is append into
     * this file : don't print the version
     */
    if ((!noversion) && ((!appendmode) || (appendmode_1))) {
        char vstr[64];
        int myversion = 1;    /* XXX: ldif version;
                 * needs to be modified when version
                 * control begins.
                 */

        sprintf(vstr, "version: %d\n\n", myversion);
        write(fd, vstr, strlen(vstr));
    }

    while ( keepgoing ) {
        Slapi_Attr *this_attr, *next_attr;

            /*
             * All database operations in a transactional environment,
             * including non-transactional reads can receive a return of
             * DB_LOCK_DEADLOCK. Which operation gets aborted depends
             * on the deadlock detection policy, but can include
             * non-transactional reads (in which case the single
             * operation should just be retried).
             */

        if (idl) {
            /* exporting from an ID list */
            if (idindex >= idl->b_nids)
                break;
            id_internal_to_stored(idl->b_ids[idindex], (char *)&temp_id);
            key.data = (char *)&temp_id;
            key.size = sizeof(temp_id);
            data.flags = DB_DBT_MALLOC;

            for (retry = 0; retry < RETRY_TIMES; retry++) {
                return_value = db->get(db, NULL, &key, &data, 0);
                if (return_value != DB_LOCK_DEADLOCK) break;
            }
            if (return_value) {
                LDAPDebug(LDAP_DEBUG_ANY, "db2ldif: failed to read "
                      "entry %lu, err %d\n", (u_long)idl->b_ids[idindex],
                      return_value, 0);
                    return_value = -1;
                break;
            }
            /* back to internal format: */
            temp_id = idl->b_ids[idindex];
            idindex++;
        } else {
            /* follow the cursor */
            key.flags = DB_DBT_MALLOC;
            data.flags = DB_DBT_MALLOC;
            if (isfirst) {
                for (retry = 0; retry < RETRY_TIMES; retry++) {
                    return_value = dbc->c_get(dbc,&key,&data,DB_FIRST);
                    if (return_value != DB_LOCK_DEADLOCK) break;
                }
                isfirst = 0;
            } else {
                for (retry = 0; retry < RETRY_TIMES; retry++) {
                    return_value = dbc->c_get(dbc,&key,&data,DB_NEXT);
                    if (return_value != DB_LOCK_DEADLOCK) break;
                }
            }

            if (0 != return_value)
                break;

            /* back to internal format */
            temp_id = id_stored_to_internal((char *)key.data);
            free(key.data);
        }

        /* call post-entry plugin */
        plugin_call_entryfetch_plugins( (char **) &data.dptr, &data.dsize );

        ep = backentry_alloc();
        ep->ep_entry = slapi_str2entry( data.data, str2entry_options );
        free(data.data);

        if ( (ep->ep_entry) != NULL ) {
            ep->ep_id = temp_id;
            cnt++;
        } else {
            LDAPDebug( LDAP_DEBUG_ANY,
               "skipping badly formatted entry with id %lu\n",
               (u_long)temp_id, 0, 0 );
            backentry_free( &ep );
            continue;
        }
        if (!ldbm_back_ok_to_dump(backentry_get_ndn(ep), include_suffix,
                      exclude_suffix)) {
            backentry_free( &ep );
            continue;
        }
        if(!dump_replica && slapi_entry_flag_is_set(ep->ep_entry, SLAPI_ENTRY_FLAG_TOMBSTONE))
        {
            /* We only dump the tombstones if the user needs to create a replica from the ldif */
            backentry_free( &ep );
            continue;
        }


        /* do not output attributes that are in the "exclude" list */
		/* Also, decrypt any encrypted attributes, if we're asked to */
        rc = slapi_entry_first_attr( ep->ep_entry, &this_attr );
        while (0 == rc) {
            rc = slapi_entry_next_attr( ep->ep_entry,
                        this_attr, &next_attr );
            slapi_attr_get_type( this_attr, &type );
            if ( ldbm_exclude_attr_from_export( li, type, dump_uniqueid )) {
                slapi_entry_delete_values( ep->ep_entry, type, NULL );
            } 
            this_attr = next_attr;
        }
		if (decrypt) {
			/* Decrypt in place */
			rc = attrcrypt_decrypt_entry(be, ep);
			if (rc) {
				LDAPDebug(LDAP_DEBUG_ANY,"Failed to decrypt entry%s\n", ep->ep_entry->e_sdn , 0, 0);
			}
		}

        data.data = slapi_entry2str_with_options( ep->ep_entry, &len, options );
        data.size = len + 1;

        if ( printkey & EXPORT_PRINTKEY ) {
            char idstr[32];
        
            sprintf(idstr, "# entry-id: %lu\n", (u_long)ep->ep_id);
            write(fd, idstr, strlen(idstr));
        }
        write(fd, data.data, len);
        write(fd, "\n", 1);
        if (cnt % 1000 == 0) {
            int percent;

            if (idl) {
                percent = (idindex*100 / idl->b_nids);
            } else {
                percent = (ep->ep_id*100 / lastid);
            }
            if (task != NULL) {
                slapi_task_log_status(task,
                            "%s: Processed %d entries (%d%%).",
                            inst->inst_name, cnt, percent);
                slapi_task_log_notice(task,
                            "%s: Processed %d entries (%d%%).",
                            inst->inst_name, cnt, percent);
            }
            LDAPDebug(LDAP_DEBUG_ANY,
                             "export %s: Processed %d entries (%d%%).\n",
                             inst->inst_name, cnt, percent);
            lastcnt = cnt;
        }

        backentry_free( &ep );
        free( data.data );
    }
    /* DB_NOTFOUND -> successful end */
    if (return_value == DB_NOTFOUND)
        return_value = 0;

    /* done cycling thru entries to write */
    if (lastcnt != cnt) {
        if (task) {
            slapi_task_log_status(task,
                    "%s: Processed %d entries (100%%).",
                    inst->inst_name, cnt);
            slapi_task_log_notice(task,
                    "%s: Processed %d entries (100%%).",
                    inst->inst_name, cnt);
        }
        LDAPDebug(LDAP_DEBUG_ANY,
                      "export %s: Processed %d entries (100%%).\n",
                      inst->inst_name, cnt, 0);
    }

    if (idl) {
        idl_free(idl);
    }
	if (dbc) {
        dbc->c_close(dbc);
    }

    dblayer_release_id2entry( be, db );
    ldbm_back_free_incl_excl(include_suffix, exclude_suffix);

    if (fd != FD_STDOUT) {
        close(fd);
    }

    LDAPDebug( LDAP_DEBUG_TRACE, "<= ldbm_back_ldbm2ldif\n", 0, 0, 0 );

    if (we_start_the_backends && 0 != dblayer_flush(li)) {
        LDAPDebug( LDAP_DEBUG_ANY, "db2ldif: Failed to flush database\n",
               0, 0, 0 );
    }

    if (we_start_the_backends) {
        if (0 != dblayer_close(li,DBLAYER_EXPORT_MODE)) {
        LDAPDebug( LDAP_DEBUG_ANY,
               "db2ldif: Failed to close database\n",
               0, 0, 0 );
        }
    } else if (run_from_cmdline && dump_replica) {
        /*
         * It should not be necessary to close the dblayer here.
         * However it masks complex thread timing issues that
         * prevent a correct shutdown of the plugins.  Closing the
         * dblayer here means we cannot dump multiple replicas
         * using -r, but the server doesn't allow that either.
         */

        /*
         * Use DBLAYER_NORMAL_MODE to match the value that was provided
         * to dblayer_start() and ensure creation of the guardian file.
         */
        if (0 != dblayer_close(li,DBLAYER_NORMAL_MODE)) {
        LDAPDebug( LDAP_DEBUG_ANY,
               "db2ldif: Failed to close database\n",
               0, 0, 0 );
        }
    }

    if (!run_from_cmdline) {
        instance_set_not_busy(inst);
    }

bye:
    if (inst != NULL) {
        PR_Lock(inst->inst_config_mutex);
        inst->inst_flags &= ~INST_FLAG_BUSY;
        PR_Unlock(inst->inst_config_mutex);
    }
    
    return( return_value );
}


static void ldbm2index_bad_vlv(Slapi_Task *task, ldbm_instance *inst,
                               char *index)
{
    char *text = vlv_getindexnames(inst->inst_be);

    if (task) {
        slapi_task_log_status(task, "%s: Unknown VLV index '%s'", 
                              inst->inst_name, index);
        slapi_task_log_notice(task, "%s: Unknown VLV index '%s'",
                              inst->inst_name, index);
        slapi_task_log_notice(task, "%s: Known VLV indexes are: %s",
                              inst->inst_name, text);
    }
    LDAPDebug(LDAP_DEBUG_ANY,
              "ldbm2index: Unknown VLV Index named '%s'\n", index, 0, 0);
    LDAPDebug(LDAP_DEBUG_ANY,
              "ldbm2index: Known VLV Indexes are: %s\n", text, 0, 0);
    slapi_ch_free((void**)&text);
}

/*
 * ldbm_back_ldbm2index - backend routine to create a new index from an
 * existing database
 */
int
ldbm_back_ldbm2index(Slapi_PBlock *pb)
{
    char             *instance_name;
    struct ldbminfo  *li;
    int              task_flags, run_from_cmdline;
    ldbm_instance    *inst;
    backend          *be;
    DB               *db = NULL;
    DBC              *dbc = NULL;
    char             **indexAttrs = NULL;
    struct vlvIndex  **pvlv= NULL;
    DBT              key = {0};
    DBT              data = {0};
    IDList           *idl = NULL; /* optimization for vlv index creation */
    int              numvlv = 0;
    int              return_value = -1;
    ID               temp_id;
    int              i, j;
    ID               lastid;
    struct backentry *ep;
    char             *type;
    NIDS             idindex = 0;
    int              count = 0;
    Slapi_Attr       *attr;
    Slapi_Task       *task;
    int              ret = 0;
    int              isfirst = 1;
    int              index_aid = 0;          /* index ancestorid */

    LDAPDebug( LDAP_DEBUG_TRACE, "=> ldbm_back_ldbm2index\n", 0, 0, 0 );
        
    slapi_pblock_get(pb, SLAPI_BACKEND_INSTANCE_NAME, &instance_name);
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_TASK_FLAGS, &task_flags);
    run_from_cmdline = (task_flags & TASK_RUNNING_FROM_COMMANDLINE);
    slapi_pblock_get(pb, SLAPI_BACKEND_TASK, &task);

    if (run_from_cmdline) {
        /* No ldbm backend exists until we process the config info. */
        li->li_flags |= TASK_RUNNING_FROM_COMMANDLINE;
        ldbm_config_load_dse_info(li);
    }

    inst = ldbm_instance_find_by_name(li, instance_name);
    if (NULL == inst) {
        if (task) {
            slapi_task_log_notice(task, "Unknown ldbm instance %s",
                                  instance_name);
        }
        LDAPDebug(LDAP_DEBUG_ANY, "Unknown ldbm instance %s\n",
                  instance_name, 0, 0);
        return -1;
    }
    be = inst->inst_be;
    slapi_pblock_set(pb, SLAPI_BACKEND, be);

    /* would love to be able to turn off transactions here, but i don't
     * think it's in the cards...
     */
    if (run_from_cmdline) {
        /* Turn off transactions */
        ldbm_config_internal_set(li, CONFIG_DB_TRANSACTION_LOGGING, "off");

        if (0 != dblayer_start(li,DBLAYER_INDEX_MODE)) {
            LDAPDebug( LDAP_DEBUG_ANY,
                       "ldbm2index: Failed to init database\n", 0, 0, 0 );
            return( -1 );
        }

        /* dblayer_instance_start will init the id2entry index. */
        if (0 != dblayer_instance_start(be, DBLAYER_INDEX_MODE)) {
            LDAPDebug(LDAP_DEBUG_ANY, "db2ldif: Failed to init instance\n",
                      0, 0, 0);
            return -1;
        }

        /* Initialise the Virtual List View code */
        vlv_init(inst);
    }

    /* make sure no other tasks are going, and set the backend readonly */
    if (instance_set_busy_and_readonly(inst) != 0) {
        LDAPDebug(LDAP_DEBUG_ANY, "ldbm: '%s' is already in the middle of "
                  "another task and cannot be disturbed.\n",
                  inst->inst_name, 0, 0);
        return -1;
    }

    if ((( dblayer_get_id2entry( be, &db )) != 0 ) || (db == NULL)) {
        LDAPDebug( LDAP_DEBUG_ANY, "Could not open/create id2entry\n",
                   0, 0, 0 );
        instance_set_not_busy(inst);
        return( -1 );
    }

    /* get a cursor to we can walk over the table */
    return_value = db->cursor(db, NULL, &dbc, 0);
    if (0 != return_value ) {
        LDAPDebug( LDAP_DEBUG_ANY,
                   "Failed to get cursor for ldbm2index\n", 0, 0, 0 );
        dblayer_release_id2entry(be, db);
        instance_set_not_busy(inst);
        return( -1 );
    }

    /* ask for the last id so we can give cute percentages */
    key.flags = DB_DBT_MALLOC;
    data.flags = DB_DBT_MALLOC;
    return_value = dbc->c_get(dbc, &key, &data, DB_LAST);
    if (return_value == DB_NOTFOUND) {
        lastid = 0;
        isfirst = 0;  /* neither a first nor a last */
    } else if (return_value == 0) {
        lastid = id_stored_to_internal((char *)key.data);
        free(key.data);
        free(data.data);
        isfirst = 1;
    } else {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "Failed to seek within id2entry (BAD %d)\n", 
                  return_value, 0 ,0);
        dbc->c_close(dbc);
        dblayer_release_id2entry(be, db);
        instance_set_not_busy(inst);
        return( -1 );
    }

    /* Work out which indexes we should build */
    /* explanation: for archaic reasons, the list of indexes is passed to
     * ldif2index as a string list, where each string either starts with a
     * 't' (normal index) or a 'T' (vlv index).
     * example: "tcn" (normal index cn)
     */
    {
        char **attrs = NULL;
        struct vlvIndex *p = NULL;
        struct attrinfo *ai = NULL;

        slapi_pblock_get(pb, SLAPI_DB2INDEX_ATTRS, &attrs);
        for (i = 0; attrs[i] != NULL; i++) {
            switch(attrs[i][0]) {
            case 't':        /* attribute type to index */
                db2index_add_indexed_attr(be, attrs[i]);
                ainfo_get(be, attrs[i]+1, &ai);
                /* the ai was added above, if it didn't already exist */
                PR_ASSERT(ai != NULL);
                if (strcasecmp(attrs[i]+1, "ancestorid") == 0) {
                    if (task) {
                        slapi_task_log_notice(task, "%s: Indexing ancestorid",
                                              inst->inst_name);
                    }
                    LDAPDebug(LDAP_DEBUG_ANY, "%s: Indexing ancestorid\n",
                              inst->inst_name, 0, 0);
                    index_aid = 1;
                } else {
                    charray_add(&indexAttrs, attrs[i]+1);
                    ai->ai_indexmask |= INDEX_OFFLINE;
                    if (task) {
                      slapi_task_log_notice(task, "%s: Indexing attribute: %s",
                                            inst->inst_name, attrs[i]+1);
                    }
                    LDAPDebug(LDAP_DEBUG_ANY, "%s: Indexing attribute: %s\n",
                              inst->inst_name, attrs[i]+1, 0);
                }
                dblayer_erase_index_file(be, ai, i/* chkpt; 1st time only */);
                break;
            case 'T':        /* VLV Search to index */
                p = vlv_find_searchname((attrs[i])+1, be);
                if (p == NULL) {
                    ldbm2index_bad_vlv(task, inst, attrs[i]+1);
                    ret = -1;
                    goto out;
                } else {
                    vlvIndex_go_offline(p, be);
                    if (pvlv == NULL) {
                        pvlv = (struct vlvIndex **)slapi_ch_calloc(1,
                                                     sizeof(struct vlvIndex *));
                    } else {
                        pvlv = (struct vlvIndex **)slapi_ch_realloc((char*)pvlv,
                                          (numvlv+1)*sizeof(struct vlvIndex *));
                    }
                    pvlv[numvlv] = p;
                    numvlv++;
                    /* Get rid of the index if it already exists */
                    PR_Delete(vlvIndex_filename(p));
                    if (task) {
                        slapi_task_log_notice(task, "%s: Indexing VLV: %s",
                                              inst->inst_name, attrs[i]+1);
                    }
                    LDAPDebug(LDAP_DEBUG_ANY, "%s: Indexing VLV: %s\n",
                              inst->inst_name, attrs[i]+1, 0);
                }
                break;
            }
        }
    }

    /* if we're only doing vlv indexes, we can accomplish this with an
     * idl composed from the ancestorid list, instead of traversing the
     * entire database.
     */
    if (!indexAttrs && !index_aid && pvlv) {
        int i, err;
        char **suffix_list = NULL;

        /* create suffix list */
        for (i = 0; i < numvlv; i++) {
            char *s = slapi_ch_strdup(slapi_sdn_get_dn(vlvIndex_getBase(pvlv[i])));

            s = slapi_dn_normalize_case(s);
            charray_add(&suffix_list, s);
        }
        idl = ldbm_fetch_subtrees(be, suffix_list, &err);
        charray_free(suffix_list);
        if (! idl) {
            LDAPDebug(LDAP_DEBUG_ANY,
                      "%s: WARNING: Failed to fetch subtree lists: (%d) %s\n",
                      inst->inst_name, err, dblayer_strerror(err));
            LDAPDebug(LDAP_DEBUG_ANY,
                      "%s: Possibly the entrydn or ancestorid index is "
                      "corrupted or does not exist.\n", inst->inst_name, 0, 0);
            LDAPDebug(LDAP_DEBUG_ANY,
                      "%s: Attempting brute-force method instead.\n",
                      inst->inst_name, 0, 0);
            if (task) {
                slapi_task_log_notice(task,
                    "%s: WARNING: Failed to fetch subtree lists (err %d) -- "
                    "attempting brute-force method instead.", 
                    inst->inst_name, err);
            }
        } else if (ALLIDS(idl)) {
            /* that's no help. */
            idl_free(idl);
            idl = NULL;
        }
    }

    if (idl) {
        /* don't need that cursor, we have a shopping list. */
        dbc->c_close(dbc);
        idindex = 0;
    }

    /* Bug 603120: slapd dumps core while indexing and deleting the db at the
     * same time. Now added the lock for the indexing code too.
     */
    vlv_acquire_lock(be);
    while (1) {
        if (idl) {
            if (idindex >= idl->b_nids)
                break;
            id_internal_to_stored(idl->b_ids[idindex], (char *)&temp_id);
            key.data = (char *)&temp_id;
            key.size = sizeof(temp_id);
            data.flags = DB_DBT_MALLOC;

            return_value = db->get(db, NULL, &key, &data, 0);
            if (return_value) {
                LDAPDebug(LDAP_DEBUG_ANY, "%s: Failed "
                          "to read database, errno=%d (%s)\n",
                          inst->inst_name, return_value,
                          dblayer_strerror(return_value));
                if (task) {
                    slapi_task_log_notice(task,
                        "%s: Failed to read database, err %d (%s)",
                        inst->inst_name, return_value,
                        dblayer_strerror(return_value));
                }
                break;
            }
            /* back to internal format: */
            temp_id = idl->b_ids[idindex];
            idindex++;
        } else {
            key.flags = DB_DBT_MALLOC;
            data.flags = DB_DBT_MALLOC;
            if (isfirst) {
                return_value = dbc->c_get(dbc, &key, &data, DB_FIRST);
                isfirst = 0;
            } else{
                return_value = dbc->c_get(dbc, &key, &data, DB_NEXT);
            }

            if (0 != return_value) {
                if (DB_NOTFOUND == return_value) {
                    break;
                } else {
                    LDAPDebug(LDAP_DEBUG_ANY, "%s: Failed to read database, "
                              "errno=%d (%s)\n", inst->inst_name, return_value,
                              dblayer_strerror(return_value));
                    if (task) {
                        slapi_task_log_notice(task,
                            "%s: Failed to read database, err %d (%s)",
                            inst->inst_name, return_value,
                            dblayer_strerror(return_value));
                    }
                    break;
                }
            }
            temp_id = id_stored_to_internal((char *)key.data);
            free(key.data);
        }

        /* call post-entry plugin */
        plugin_call_entryfetch_plugins( (char **) &data.dptr, &data.dsize );

        ep = backentry_alloc();
        ep->ep_entry = slapi_str2entry( data.data, 0 );
        free(data.data);

        if ( ep->ep_entry != NULL ) {
            ep->ep_id = temp_id;
        } else {
            if (task) {
                slapi_task_log_notice(task,
                    "%s: WARNING: skipping badly formatted entry (id %lu)",
                    inst->inst_name, (u_long)temp_id);
            }
            LDAPDebug(LDAP_DEBUG_ANY,
                      "%s: WARNING: skipping badly formatted entry (id %lu)\n",
                      inst->inst_name, (u_long)temp_id, 0);
            backentry_free( &ep );
            continue;
        }

        if ( add_op_attrs( pb, li, ep, NULL ) != 0 ) {
            if (task) {
                slapi_task_log_notice(task,
                    "%s: ERROR: Could not add op attrs to entry (id %lu)",
                    inst->inst_name, (u_long)ep->ep_id);
            }
            LDAPDebug(LDAP_DEBUG_ANY,
                "%s: ERROR: Could not add op attrs to entry (id %lu)\n",
                inst->inst_name, (u_long)ep->ep_id, 0);
            backentry_free( &ep );
            ret = -1;
            goto out;
        }

        /*
         * Update the attribute indexes
         */
        if (indexAttrs != NULL) {
            for (i = slapi_entry_first_attr(ep->ep_entry, &attr); i == 0;
                 i = slapi_entry_next_attr(ep->ep_entry, attr, &attr)) {
                Slapi_Value **svals;
                int rc = 0;

                slapi_attr_get_type( attr, &type );
                for ( j = 0; indexAttrs[j] != NULL; j++ ) {
                    if (slapi_attr_type_cmp(indexAttrs[j], type,
                                            SLAPI_TYPE_CMP_SUBTYPE) == 0 ) {
                        back_txn txn;
                        svals = attr_get_present_values(attr);

                        if (run_from_cmdline)
                        {
                            txn.back_txn_txn = NULL;
                        }
                        else
                        {
                            rc = dblayer_txn_begin(li, NULL, &txn);
                            if (0 != rc) {
                                LDAPDebug(LDAP_DEBUG_ANY,
                                    "%s: ERROR: failed to begin txn for update "
                                    "index '%s'\n",
                                    inst->inst_name, indexAttrs[j], 0);
                                LDAPDebug(LDAP_DEBUG_ANY,
                                    "%s: Error %d: %s\n", inst->inst_name, rc,
                                    dblayer_strerror(rc));
                                if (task) {
                                    slapi_task_log_notice(task,
                                        "%s: ERROR: failed to begin txn for "
                                        "update index '%s' (err %d: %s)",
                                        inst->inst_name, indexAttrs[j], rc,
                                        dblayer_strerror(rc));
                                }
                                ret = -2;
                                goto out;
                            }
                        }
                        rc = index_addordel_values_sv(
                            be, indexAttrs[j], svals,
                            NULL, ep->ep_id, BE_INDEX_ADD, &txn);
                        if (rc != 0) {
                            LDAPDebug(LDAP_DEBUG_ANY,
                                "%s: ERROR: failed to update index '%s'\n",
                                inst->inst_name, indexAttrs[j], 0);
                            LDAPDebug(LDAP_DEBUG_ANY,
                                "%s: Error %d: %s\n", inst->inst_name, rc,
                                dblayer_strerror(rc));
                            if (task) {
                                slapi_task_log_notice(task,
                                    "%s: ERROR: failed to update index '%s' "
                                    "(err %d: %s)", inst->inst_name,
                                    indexAttrs[j], rc, dblayer_strerror(rc));
                            }
                            if (!run_from_cmdline)
                                   dblayer_txn_abort(li, &txn);
                            ret = -2;
                            goto out;
                        }
                        if (!run_from_cmdline)
                        {
                            rc = dblayer_txn_commit(li, &txn);
                            if (0 != rc) {
                                LDAPDebug(LDAP_DEBUG_ANY,
                                    "%s: ERROR: failed to commit txn for "
                                    "update index '%s'\n",
                                    inst->inst_name, indexAttrs[j], 0);
                                LDAPDebug(LDAP_DEBUG_ANY,
                                    "%s: Error %d: %s\n", inst->inst_name, rc,
                                    dblayer_strerror(rc));
                                if (task) {
                                    slapi_task_log_notice(task,
                                        "%s: ERROR: failed to commit txn for "
                                        "update index '%s' "
                                        "(err %d: %s)", inst->inst_name,
                                        indexAttrs[j], rc, dblayer_strerror(rc));
                                }
                                ret = -2;
                                goto out;
                            }
                        }
                    }
                }
            }
        }

        /*
         * Update the Virtual List View indexes
         */
        for ( j = 0; j<numvlv; j++ ) {
            back_txn txn;
            int rc = 0;
            if (run_from_cmdline)
            {
                txn.back_txn_txn = NULL;
            }
            else
            if (!run_from_cmdline)
            {
                rc = dblayer_txn_begin(li, NULL, &txn);
                if (0 != rc) {
                    LDAPDebug(LDAP_DEBUG_ANY,
                      "%s: ERROR: failed to begin txn for update index '%s'\n",
                      inst->inst_name, indexAttrs[j], 0);
                    LDAPDebug(LDAP_DEBUG_ANY,
                        "%s: Error %d: %s\n", inst->inst_name, rc,
                        dblayer_strerror(rc));
                    if (task) {
                        slapi_task_log_notice(task,
                         "%s: ERROR: failed to begin txn for update index '%s' "
                         "(err %d: %s)", inst->inst_name,
                         indexAttrs[j], rc, dblayer_strerror(rc));
                    }
                    ret = -2;
                    goto out;
                }
            }
            vlv_update_index(pvlv[j], &txn, li, pb, NULL, ep);
            if (!run_from_cmdline)
            {
                rc = dblayer_txn_commit(li, &txn);
                if (0 != rc) {
                    LDAPDebug(LDAP_DEBUG_ANY,
                      "%s: ERROR: failed to commit txn for update index '%s'\n",
                      inst->inst_name, indexAttrs[j], 0);
                    LDAPDebug(LDAP_DEBUG_ANY,
                        "%s: Error %d: %s\n", inst->inst_name, rc,
                        dblayer_strerror(rc));
                    if (task) {
                        slapi_task_log_notice(task,
                        "%s: ERROR: failed to commit txn for update index '%s' "
                        "(err %d: %s)", inst->inst_name,
                        indexAttrs[j], rc, dblayer_strerror(rc));
                    }
                    ret = -2;
                    goto out;
                }
            }
        }

        /*
         * Update the ancestorid index
         */
        if (index_aid) {
            int rc;

            rc = ldbm_ancestorid_index_entry(be, ep, BE_INDEX_ADD, NULL);
            if (rc != 0) {
                LDAPDebug(LDAP_DEBUG_ANY,
                          "%s: ERROR: failed to update index 'ancestorid'\n",
                          inst->inst_name, 0, 0);
                LDAPDebug(LDAP_DEBUG_ANY,
                          "%s: Error %d: %s\n", inst->inst_name, rc,
                          dblayer_strerror(rc));
                if (task) {
                    slapi_task_log_notice(task,
                        "%s: ERROR: failed to update index 'ancestorid' "
                        "(err %d: %s)", inst->inst_name,
                        rc, dblayer_strerror(rc));
                }
                ret = -2;
                goto out;
            }
        }

        count++;
        if ((count % 1000) == 0) {
            int percent;

            if (idl) {
                percent = (idindex*100 / (idl->b_nids ? idl->b_nids : 1));
            } else {
                percent = (ep->ep_id*100 / (lastid ? lastid : 1));
            }
            if (task) {
                task->task_progress = (idl ? idindex : ep->ep_id);
                task->task_work = (idl ? idl->b_nids : lastid);
                slapi_task_status_changed(task);
                slapi_task_log_status(task, "%s: Indexed %d entries (%d%%).",
                                      inst->inst_name, count, percent);
                slapi_task_log_notice(task, "%s: Indexed %d entries (%d%%).",
                                      inst->inst_name, count, percent);
            }
            LDAPDebug(LDAP_DEBUG_ANY, "%s: Indexed %d entries (%d%%).\n",
                      inst->inst_name, count, percent);
        }

        backentry_free( &ep );
    }
    vlv_release_lock(be);

    /* if we got here, we finished successfully */

    /* activate all the indexes we added */
    for (i = 0; indexAttrs && indexAttrs[i]; i++) {
        struct attrinfo *ai = NULL;

        ainfo_get(be, indexAttrs[i], &ai);
        PR_ASSERT(ai != NULL);
        ai->ai_indexmask &= ~INDEX_OFFLINE;
    }
    for (i = 0; i < numvlv; i++) {
        vlvIndex_go_online(pvlv[i], be);
    }

    if (task) {
        slapi_task_log_status(task, "%s: Finished indexing.",
                              inst->inst_name);
        slapi_task_log_notice(task, "%s: Finished indexing.",
                              inst->inst_name);
    }
    LDAPDebug(LDAP_DEBUG_ANY, "%s: Finished indexing.\n",
              inst->inst_name, 0, 0);

out:
    if (idl) {
        idl_free(idl);
    } else {
        dbc->c_close(dbc);
    }
    dblayer_release_id2entry( be, db );

    instance_set_not_busy(inst);

    LDAPDebug( LDAP_DEBUG_TRACE, "<= ldbm_back_ldbm2index\n", 0, 0, 0 );

    if (run_from_cmdline) {
        if (0 != dblayer_flush(li)) {
            LDAPDebug(LDAP_DEBUG_ANY,
                      "%s: Failed to flush database\n", inst->inst_name, 0, 0);
        }
        dblayer_instance_close(be);
        if (0 != dblayer_close(li,DBLAYER_INDEX_MODE)) {
            LDAPDebug(LDAP_DEBUG_ANY,
                      "%s: Failed to close database\n", inst->inst_name, 0, 0);
        }
    }

    if (indexAttrs) {
        slapi_ch_free((void **)&indexAttrs);
    }

    return (ret);
}

/*
 * The db2index mode of slapd accepts commandline specification of
 * an attribute to be indexed and the types of indexes to be created.
 * The format is:
 * (ns-)slapd db2index -tattributeName[:indextypes[:matchingrules]]
 * where indextypes and matchingrules(OIDs) are comma separated lists
 * e.g.,
 * -tuid:eq,pres
 * -tuid:sub:2.1.15.17.blah
 */
static int
db2index_add_indexed_attr(backend *be, char *attrString)
{
    char *iptr = NULL;
    char *mptr = NULL;
    char *nsslapd_index_value[4];
    int argc = 0;
    int i;

    if (NULL == (iptr = strchr(attrString, ':'))) {
        return(0);
    }
    iptr[0] = '\0';
    iptr++;
    
    nsslapd_index_value[argc++] = slapi_ch_strdup(attrString+1);
    
    if (NULL != (mptr = strchr(iptr, ':'))) {
        mptr[0] = '\0';
        mptr++;
    }
    nsslapd_index_value[argc++] = slapi_ch_strdup(iptr);
    if (NULL != mptr) {
        nsslapd_index_value[argc++] = slapi_ch_strdup(mptr);
    }
    nsslapd_index_value[argc] = NULL;
    attr_index_config(be, "from db2index()", 0, argc, nsslapd_index_value, 0);

    for ( i=0; i<argc; i++ ) {
        slapi_ch_free((void **)&nsslapd_index_value[i]);
    }
    return(0);
}


/*
 * Determine if the given normalized 'attr' is to be excluded from LDIF
 * exports.
 *
 * Returns a non-zero value if:
 *    1) The 'attr' is in the configured list of attribute types that
 *			are to be excluded.
 * OR 2) dump_uniqueid is non-zero and 'attr' is the unique ID attribute.
 *
 * Return 0 if the attribute is not to be excluded.
 */
static int
ldbm_exclude_attr_from_export( struct ldbminfo *li , const char *attr,
		int dump_uniqueid )

{
	int		i, rc = 0;

	if ( !dump_uniqueid && 0 == strcasecmp( SLAPI_ATTR_UNIQUEID, attr )) {
		rc = 1;				/* exclude */

	} else if ( NULL != li && NULL != li->li_attrs_to_exclude_from_export ) {
		for ( i = 0; li->li_attrs_to_exclude_from_export[i] != NULL; ++i ) {
			if ( 0 == strcasecmp( li->li_attrs_to_exclude_from_export[i],
						attr )) {
				rc = 1;		/* exclude */
				break;
			}
		}
	}

	return( rc );
}

/*
 * ldbm_back_upgradedb - 
 *
 * functions to convert idl from the old format to the new one
 * (604921) Support a database uprev process any time post-install
 */

void upgradedb_core(Slapi_PBlock *pb, ldbm_instance *inst);
int upgradedb_copy_logfiles(struct ldbminfo *li, char *destination_dir, int restore, int *cnt);
int upgradedb_delete_indices_4cmd(ldbm_instance *inst);
void normalize_dir(char *dir);

/*
 * ldbm_back_upgradedb - 
 *    check the DB version and if it's old idl'ed index,
 *    then reindex using new idl.
 *
 * standalone only -- not allowed to run while DS is up.
 */
int ldbm_back_upgradedb(Slapi_PBlock *pb)
{
    struct ldbminfo *li;
    Object *inst_obj = NULL;
    ldbm_instance *inst = NULL;
    int run_from_cmdline = 0;
    int task_flags = 0;
    int server_running = 0;
    int rval = 0;
    int backup_rval = 0;
    char *dest_dir = NULL;
    char *orig_dest_dir = NULL;
    char *home_dir = NULL;
    int up_flags;
    int i;
    Slapi_Task *task;
    char inst_dir[MAXPATHLEN];
    char *inst_dirp = NULL;
                                                                         
    slapi_pblock_get(pb, SLAPI_SEQ_TYPE, &up_flags);
    slapi_log_error(SLAPI_LOG_TRACE, "upgrade DB", "Reindexing all...\n");
    slapi_pblock_get(pb, SLAPI_TASK_FLAGS, &task_flags);
    slapi_pblock_get(pb, SLAPI_BACKEND_TASK, &task);
    slapi_pblock_get(pb, SLAPI_DB2LDIF_SERVER_RUNNING, &server_running);

    run_from_cmdline = (task_flags & TASK_RUNNING_FROM_COMMANDLINE);
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    if (run_from_cmdline)
    {
        if (!(up_flags & SLAPI_UPGRADEDB_SKIPINIT))
        {
            ldbm_config_load_dse_info(li);
        }
        autosize_import_cache(li);
    }
    else
    {
        Object *inst_obj, *inst_obj2;
        ldbm_instance *inst = NULL;

        /* server is up -- mark all backends busy */
        slapi_log_error(SLAPI_LOG_TRACE, "upgrade DB",
                        "server is up -- marking all LDBM backends busy\n");
        for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
             inst_obj = objset_next_obj(li->li_instance_set, inst_obj))
        {
            inst = (ldbm_instance *)object_get_data(inst_obj);
            /* check if an import/restore is already ongoing... */
            /* BUSY flag is cleared at the end of import_main (join thread);
               it should not cleared in this thread [610347] */
            if (instance_set_busy(inst) != 0)
            {
                slapi_log_error(SLAPI_LOG_FATAL, "upgrade DB",
                            "ldbm: '%s' is already in the middle of "
                            "another task and cannot be disturbed.\n",
                            inst->inst_name);
                if (task)
                {
                    slapi_task_log_notice(task,
                        "Backend '%s' is already in the middle of "
                        "another task and cannot be disturbed.\n",
                        inst->inst_name);
                }

                /* painfully, we have to clear the BUSY flags on the
                 * backends we'd already marked...
                 */
                for (inst_obj2 = objset_first_obj(li->li_instance_set);
                     inst_obj2 && (inst_obj2 != inst_obj);
                     inst_obj2 = objset_next_obj(li->li_instance_set, inst_obj2))
                {
                    inst = (ldbm_instance *)object_get_data(inst_obj2);
                    instance_set_not_busy(inst);
                }
                object_release(inst_obj2);
                object_release(inst_obj);
                return -1;
            }
        }
    }

    inst_obj = objset_first_obj(li->li_instance_set);
    if (inst_obj)
    {
        inst = (ldbm_instance *)object_get_data(inst_obj);
        if (!(up_flags & SLAPI_UPGRADEDB_FORCE))
        { /* upgrade idl to new */
            li->li_flags |= LI_FORCE_MOD_CONFIG;
            /* set new idl */
            ldbm_config_internal_set(li, CONFIG_IDL_SWITCH, "new");
            /* First check the dbversion */
            rval = check_db_inst_version(inst);
            if (!(DBVERSION_NEED_IDL_OLD2NEW & rval))
            {
                slapi_log_error(SLAPI_LOG_FATAL, "upgrade DB",
                                "Index version is up-to-date\n");
                return 0;
            }
        }
    }
    else
    {
        slapi_log_error(SLAPI_LOG_FATAL,
                        "upgrade DB", "No instance to be upgraded\n");
        return -1;
    }

    /* we are going to go forward */
    /* 
     * First, backup index files and checkpoint log files
     * since the server is not up and running, we can just copy them.
     */
    slapi_pblock_get( pb, SLAPI_SEQ_VAL, &dest_dir );
    if (NULL == dest_dir)
    {
        slapi_log_error(SLAPI_LOG_FATAL, "upgrade DB",
                        "Backup directory is not specified.\n");
        return -1;
    }

    {
        int cnt = 0;
        PRFileInfo info;

        orig_dest_dir = dest_dir;
        normalize_dir(dest_dir);
        /* clean up the backup dir first, then create it */
        rval = PR_GetFileInfo(dest_dir, &info);
        if (PR_SUCCESS == rval)
        {
            if (PR_FILE_DIRECTORY == info.type)    /* directory exists */
            {
                time_t tm = time(0);    /* long */

                char *tmpname = slapi_ch_smprintf("%s/%ld", dest_dir, tm);
                dest_dir = tmpname;
            }
            else    /* not a directory */
                PR_Delete(dest_dir);
        }

        if (mkdir_p(dest_dir, 0700) < 0)
            goto fail0;

        while (1)
        {
            inst_dirp = dblayer_get_full_inst_dir(inst->inst_li, inst,
                                                  inst_dir, MAXPATHLEN);
            backup_rval = dblayer_copy_directory(li, NULL /* task */,
                                              inst_dirp, dest_dir, 0/*backup*/,
                                              &cnt, 0, 1, 0);
            if (inst_dirp != inst_dir)
                slapi_ch_free_string(&inst_dirp);
            if (backup_rval < 0)
            {
                slapi_log_error(SLAPI_LOG_FATAL, "upgrade DB",
                  "Warning: Failed to backup index files (instance %s).\n",
                  inst_dirp);
                goto fail1;
            }

            /* delete index files to be reindexed */
            if (run_from_cmdline)
            {
                if (0 != upgradedb_delete_indices_4cmd(inst))
                {
                    slapi_log_error(SLAPI_LOG_FATAL, "upgrade DB",
                        "Can't clean up indices in %s\n", inst->inst_dir_name);
                    goto fail1;
                }
            }
            else
            {
                if (0 != dblayer_delete_indices(inst))
                {
                    slapi_log_error(SLAPI_LOG_FATAL, "upgrade DB",
                        "Can't clean up indices in %s\n", inst->inst_dir_name);
                    goto fail1;
                }
            }

            inst_obj = objset_next_obj(li->li_instance_set, inst_obj);
            if (NULL == inst_obj)
                break;
            inst = (ldbm_instance *)object_get_data(inst_obj);
        }

        /* copy checkpoint logs */
        backup_rval += upgradedb_copy_logfiles(li, dest_dir, 0, &cnt);
    }

    if (run_from_cmdline)
        ldbm_config_internal_set(li, CONFIG_DB_TRANSACTION_LOGGING, "off");

    inst_obj = objset_first_obj(li->li_instance_set);
    for (i = 0; NULL != inst_obj; i++)
    {
        if (run_from_cmdline)
        {
            /* need to call dblayer_start for each instance,
               since dblayer_close is called in upgradedb_core =>
               ldbm_back_ldif2ldbm_deluxe */
            if (0 != dblayer_start(li, DBLAYER_IMPORT_MODE))
            {
                slapi_log_error(SLAPI_LOG_FATAL, "upgrade DB",
                                  "upgradedb: Failed to init database\n");
                goto fail1;
            }
        }

        inst = (ldbm_instance *)object_get_data(inst_obj);
        slapi_pblock_set(pb, SLAPI_BACKEND, inst->inst_be);
        slapi_pblock_set(pb, SLAPI_BACKEND_INSTANCE_NAME, inst->inst_name);
        upgradedb_core(pb, inst);
        inst_obj = objset_next_obj(li->li_instance_set, inst_obj);
    }

    /* upgrade idl to new; otherwise no need to modify idl-switch */
    if (!(up_flags & SLAPI_UPGRADEDB_FORCE))
    {
        replace_ldbm_config_value(CONFIG_IDL_SWITCH, "new", li);
    }

    home_dir = dblayer_get_home_dir(li, NULL);

    /* write db version files */
    dbversion_write(li, home_dir, NULL);

    inst_obj = objset_first_obj(li->li_instance_set);
    while (NULL != inst_obj)
    {
        char *inst_dirp = NULL;
        inst_dirp = dblayer_get_full_inst_dir(li, inst, inst_dir, MAXPATHLEN);
        inst = (ldbm_instance *)object_get_data(inst_obj);
        dbversion_write(li, inst_dirp, NULL);
        inst_obj = objset_next_obj(li->li_instance_set, inst_obj);
        if (inst_dirp != inst_dir)
            slapi_ch_free_string(&inst_dirp);
    }

    /* close the database down again */
    if (run_from_cmdline)
    {
        if (0 != dblayer_flush(li))
        {
            slapi_log_error(SLAPI_LOG_FATAL, "upgrade DB",
                            "Failed to flush database\n");
        }
        if (0 != dblayer_close(li,DBLAYER_IMPORT_MODE))
        {
            slapi_log_error(SLAPI_LOG_FATAL, "upgrade DB",
                            "Failed to close database\n");
            goto fail1;
        }
    }

    /* delete backup */
    if (NULL != dest_dir)
        ldbm_delete_dirs(dest_dir);

    if (dest_dir != orig_dest_dir)
        slapi_ch_free_string(&dest_dir);

    return 0;

fail1:
    if (0 != dblayer_flush(li))
        slapi_log_error(SLAPI_LOG_FATAL, "upgrade DB",
                        "Failed to flush database\n");

    /* Ugly! (we started dblayer with DBLAYER_IMPORT_MODE)
     * We just want not to generate a guardian file...
     */
    if (0 != dblayer_close(li,DBLAYER_ARCHIVE_MODE))
        slapi_log_error(SLAPI_LOG_FATAL, "upgrade DB",
                        "Failed to close database\n");

    /* restore from the backup, if possible */
    if (NULL != dest_dir)
    {
        if (0 == backup_rval)    /* only when the backup succeeded... */
        {
            int cnt = 0;

            inst_obj = objset_first_obj(li->li_instance_set);
            while (NULL != inst_obj)
            {
                inst = (ldbm_instance *)object_get_data(inst_obj);

                inst_dirp = dblayer_get_full_inst_dir(inst->inst_li, inst,
                                                      inst_dir, MAXPATHLEN);
                backup_rval = dblayer_copy_directory(li, NULL /* task */,
                                                     inst->inst_dir_name,
                                                     dest_dir, 1/*restore*/,
                                                     &cnt, 0, 1, 0);
                if (inst_dirp != inst_dir)
                    slapi_ch_free_string(&inst_dirp);
                if (backup_rval < 0)
                    slapi_log_error(SLAPI_LOG_FATAL, "upgrade DB",
                        "Failed to restore index files (instance %s).\n",
                        inst->inst_name);

                inst_obj = objset_next_obj(li->li_instance_set, inst_obj);
            }
    
            backup_rval = upgradedb_copy_logfiles(li, dest_dir, 1, &cnt);
            if (backup_rval < 0)
                slapi_log_error(SLAPI_LOG_FATAL, "upgrade DB",
                        "Failed to restore log files.\n");
        }

        /* anyway clean up the backup dir */
        ldbm_delete_dirs(dest_dir);
    }

fail0:
    if (dest_dir != orig_dest_dir)
        slapi_ch_free_string(&dest_dir);

    return rval;
}

void normalize_dir(char *dir)
{
    int l = strlen(dir);
    if ('/' == dir[l-1] || '\\' == dir[l-1])
    {
        dir[l-1] = '\0';
    }
}

#define LOG    "log."
#define LOGLEN    4
int upgradedb_copy_logfiles(struct ldbminfo *li, char *destination_dir,
                                 int restore, int *cnt)
{
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    char *src;
    char *dest;
    int srclen;
    int destlen;
    int rval = 0;
    int len0 = 0;
    int len1 = 0;
    char *from = NULL;
    char *to = NULL;

    *cnt = 0;
    if (restore)
    {
        src = destination_dir;
        dest = li->li_directory;
    }
    else
    {
        src = li->li_directory;
        dest = destination_dir;
    }
    srclen = strlen(src);
    destlen = strlen(dest);

    /* Open the instance dir so we can look what's in it. */
    dirhandle = PR_OpenDir(src);
    if (NULL == dirhandle)
        return -1;

    while (NULL != (direntry =
                    PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT)))
    {
        if (NULL == direntry->name)
            break;

        if (0 == strncmp(direntry->name, LOG, 4))
        {
            int filelen = strlen(direntry->name);
            char *p, *endp;
            int fromlen, tolen;
            int notalog = 0;

            endp = (char *)direntry->name + filelen;
            for (p = (char *)direntry->name + LOGLEN; p < endp; p++)
            {
                if (!isdigit(*p))
                {
                    notalog = 1;
                    break;
                }
            }
            if (notalog)
                continue;    /* go to next file */

            fromlen = srclen + filelen + 2;
            if (len0 < fromlen)
            {
                slapi_ch_free_string(&from);
                from = slapi_ch_calloc(1, fromlen);
                len0 = fromlen;
            }
            sprintf(from, "%s/%s", src, direntry->name);
            tolen = destlen + filelen + 2;
            if (len1 < tolen)
            {
                slapi_ch_free_string(&to);
                to = slapi_ch_calloc(1, tolen);
                len1 = tolen;
            }
            sprintf(to, "%s/%s", dest, direntry->name);
            if (NULL == from || NULL == to)
                break;
            rval = dblayer_copyfile(from, to, 1, DEFAULT_MODE);
            if (rval < 0)
                break;
            cnt++;
        }
    }
    slapi_ch_free_string(&from);
    slapi_ch_free_string(&to);
    PR_CloseDir(dirhandle);

    return rval;
}

int upgradedb_delete_indices_4cmd(ldbm_instance *inst)
{
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    int rval = 0;
    char fullpath[MAXPATHLEN];
    char *fullpathp = fullpath;
    char inst_dir[MAXPATHLEN];
    char *inst_dirp = dblayer_get_full_inst_dir(inst->inst_li, inst,
                                                inst_dir, MAXPATHLEN);

    slapi_log_error(SLAPI_LOG_TRACE, "upgrade DB",
                    "upgradedb_delete_indices_4cmd: %s\n");
    dirhandle = PR_OpenDir(inst_dirp);
    if (!dirhandle)
    {
        slapi_log_error(SLAPI_LOG_FATAL, "upgrade DB",
                        "upgradedb_delete_indices_4cmd: PR_OpenDir failed\n");
        if (inst_dirp != inst_dir)
            slapi_ch_free_string(&inst_dirp);
        return -1;
    }

    while (NULL != (direntry =
                    PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT)))
    {
        PRFileInfo info;
        int len;

        if (! direntry->name)
            break;

        if (0 == strcmp(direntry->name, ID2ENTRY LDBM_FILENAME_SUFFIX))
            continue;

        len = strlen(inst_dirp) + strlen(direntry->name) + 2;
        if (len > MAXPATHLEN)
        {
            fullpathp = (char *)slapi_ch_malloc(len);
        }
        sprintf(fullpathp, "%s/%s", inst_dirp, direntry->name);
        rval = PR_GetFileInfo(fullpathp, &info);
        if (PR_SUCCESS == rval && PR_FILE_DIRECTORY != info.type)
        {
            PR_Delete(fullpathp);
            slapi_log_error(SLAPI_LOG_TRACE, "upgrade DB",
                            "upgradedb_delete_indices_4cmd: %s deleted\n", fullpath);
        }
        if (fullpathp != fullpath)
            slapi_ch_free_string(&fullpathp);
    }
    PR_CloseDir(dirhandle);
    if (inst_dirp != inst_dir)
        slapi_ch_free_string(&inst_dirp);
    return rval;
}

/*
 * upgradedb_core
 */
void upgradedb_core(Slapi_PBlock *pb, ldbm_instance *inst)
{
    backend *be = NULL;
    int task_flags = 0;
    int run_from_cmdline = 0;

    slapi_pblock_get(pb, SLAPI_TASK_FLAGS, &task_flags);
    run_from_cmdline = (task_flags & TASK_RUNNING_FROM_COMMANDLINE);

    be = inst->inst_be;
    slapi_log_error(SLAPI_LOG_FATAL, "upgrade DB",
                    "%s: Start upgradedb.\n", inst->inst_name);

    if (!run_from_cmdline)
    {
        /* shutdown this instance of the db */
        slapi_log_error(SLAPI_LOG_TRACE, "upgrade DB",
                    "Bringing %s offline...\n", inst->inst_name);
        slapi_mtn_be_disable(inst->inst_be);

        cache_clear(&inst->inst_cache);
        dblayer_instance_close(be);
    }

    /* dblayer_instance_start will init the id2entry index. */
    if (0 != dblayer_instance_start(be, DBLAYER_IMPORT_MODE))
    {
        slapi_log_error(SLAPI_LOG_FATAL, "upgrade DB",
                    "upgradedb: Failed to init instance %s\n", inst->inst_name);
        return;
    }

    if (run_from_cmdline)
        vlv_init(inst);    /* Initialise the Virtual List View code */

    ldbm_back_ldif2ldbm_deluxe(pb);
}


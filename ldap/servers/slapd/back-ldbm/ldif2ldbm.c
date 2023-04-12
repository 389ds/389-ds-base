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

#define DB2INDEX_ANCESTORID 0x1   /* index ancestorid */
#define DB2INDEX_ENTRYRDN 0x2     /* index entryrdn */
#define DB2LDIF_ENTRYRDN 0x4      /* export entryrdn */
#define DB2INDEX_OBJECTCLASS 0x10 /* for reindexing "objectclass: nstombstone" */

#define LDIF2LDBM_EXTBITS(x) ((x)&0xf)

typedef struct _export_args
{
    struct backentry *ep;
    int decrypt;
    int options;
    int printkey;
    IDList *idl;
    NIDS idindex;
    ID lastid;
    int fd;
    Slapi_Task *task;
    char **include_suffix;
    char **exclude_suffix;
    int *cnt;
    int *lastcnt;
    IDList *pre_exported_idl; /* exported IDList, which ID is larger than
                                 its children's ID.  It happens when an entry
                                 is added and existing entries are moved under
                                 the newly added entry. */
} export_args;

/* static functions */


/**********  common routines for classic/deluxe import code **********/

static PRIntn
import_subcount_hash_compare_keys(const void *v1, const void *v2)
{
    return (((ID)((uintptr_t)v1) == (ID)((uintptr_t)v2)) ? 1 : 0);
}

static PRIntn
import_subcount_hash_compare_values(const void *v1, const void *v2)
{
    return (((size_t)v1 == (size_t)v2) ? 1 : 0);
}

static PLHashNumber
import_subcount_hash_fn(const void *id)
{
    return (PLHashNumber)((uintptr_t)id);
}

void
import_subcount_stuff_init(import_subcount_stuff *stuff)
{
    stuff->hashtable = PL_NewHashTable(IMPORT_SUBCOUNT_HASHTABLE_SIZE,
                                       import_subcount_hash_fn, import_subcount_hash_compare_keys,
                                       import_subcount_hash_compare_values, NULL, NULL);
}

void
import_subcount_stuff_term(import_subcount_stuff *stuff)
{
    if (stuff != NULL && stuff->hashtable != NULL) {
        PL_HashTableDestroy(stuff->hashtable);
    }
}



/**********  functions for maintaining the subordinate count **********/




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
int
ldbm_back_ldif2ldbm(Slapi_PBlock *pb)
{
    struct ldbminfo *li;
    int rc, task_flags;

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_TASK_FLAGS, &task_flags);
    if (task_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE) {
        /* initialize UniqueID generator - must be done once backends are started
           and event queue is initialized but before plugins are started */
        /* This dn is normalize. */
        Slapi_DN *sdn =
            slapi_sdn_new_ndn_byref("cn=uniqueid generator,cn=config");
            rc = uniqueIDGenInit(NULL, sdn /*const*/,
                                 0 /* use single thread mode */);
        slapi_sdn_free(&sdn);
        if (rc != UID_SUCCESS) {
            slapi_log_err(SLAPI_LOG_EMERG,
                          "ldbm_back_ldif2ldbm", "Failed to initialize uniqueid generator; error = %d. "
                                                 "Exiting now.\n",
                          rc);
            return -1;
        }

        dblayer_setup(li);
        li->li_flags |= SLAPI_TASK_RUNNING_FROM_COMMANDLINE;
    }
    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;

    return priv->dblayer_ldif2db_fn(pb);;
}


/**********  db2ldif, db2index  **********/

/*
 * ldbm_back_ldbm2ldif - backend routine to convert database to an
 * ldif file.
 * (reunified at last)
 */
#define LDBM2LDIF_BUSY (-2)
int
ldbm_back_ldbm2ldif(Slapi_PBlock *pb)
{
    struct ldbminfo *li;
    int32_t dump_replica = 0;
    int32_t run_from_cmdline = 0;
    int32_t we_start_the_backends = 0;
    int task_flags;

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_TASK_FLAGS, &task_flags);
    run_from_cmdline = (task_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE);

    dump_replica = slapi_pblock_get_ldif_dump_replica(pb);
    if (run_from_cmdline) {
        li->li_flags |= SLAPI_TASK_RUNNING_FROM_COMMANDLINE;
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
         *   WARNING: ldbm instance userRoot already exists
         */

        if (dblayer_setup(li)) {
            slapi_log_err(SLAPI_LOG_CRIT, "ldbm_back_ldbm2ldif", "dblayer_setup failed\n");
            return -1;
        }
    }

    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;

    return priv->dblayer_db2ldif_fn(pb);;
}


/*
 * ldbm_back_ldbm2index - backend routine to create a new index from an
 * existing database
 */
int
ldbm_back_ldbm2index(Slapi_PBlock *pb)
{
    struct ldbminfo *li;
    int32_t run_from_cmdline = 0;
    int task_flags;

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_TASK_FLAGS, &task_flags);
    run_from_cmdline = (task_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE);

    if (run_from_cmdline) {
        li->li_flags |= SLAPI_TASK_RUNNING_FROM_COMMANDLINE;

        if (dblayer_setup(li)) {
            slapi_log_err(SLAPI_LOG_CRIT, "ldbm_back_ldbm2index", "dblayer_setup failed\n");
            return -1;
        }
    }

    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;

    return priv->dblayer_db2index_fn(pb);;
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
int
db2index_add_indexed_attr(backend *be, char *attrString)
{
    char *iptr = NULL;
    char *mptr = NULL;
    Slapi_Entry *e;
    struct berval *vals[2];
    struct berval val;
    char *ptr;
    char *next;

    vals[0] = &val;
    vals[1] = NULL;

    if (NULL == (iptr = strchr(attrString, ':'))) {
        return (0);
    }
    e = slapi_entry_alloc();
    iptr[0] = '\0';
    iptr++;

    /* set the index name */
    val.bv_val = attrString + 1;
    val.bv_len = strlen(attrString);
    /* bv_val is copied into the entry. */
    slapi_entry_add_values(e, "cn", vals);

    if (NULL != (mptr = strchr(iptr, ':'))) {
        mptr[0] = '\0';
        mptr++;
    }

    /* set the index type */
    for (ptr = strtok_r(iptr, ", ", &next);
         ptr;
         ptr = strtok_r(NULL, ", ", &next)) {
        val.bv_val = ptr;
        val.bv_len = strlen(ptr);
        /* bv_val is copied into the entry. */
        slapi_entry_add_values(e, "nsIndexType", vals);
    }

    if (NULL != mptr) {
        for (ptr = strtok_r(mptr, ", ", &next);
             ptr;
             ptr = strtok_r(NULL, ", ", &next)) {
            val.bv_val = ptr;
            val.bv_len = strlen(ptr);
            /* bv_val is copied into the entry. */
            slapi_entry_add_values(e, "nsMatchingRule", vals);
        }
    }

    attr_index_config(be, "from db2index()", 0, e, 0, 0, NULL);
    slapi_entry_free(e);

    return (0);
}

/*
 * ldbm_back_upgradedb -
 *
 * functions to convert idl from the old format to the new one
 * (604921) Support a database uprev process any time post-install
 */


/*
 * ldbm_back_upgradedb -
 *    check the DB version and if it's old idl'ed index,
 *    then reindex using new idl.
 *
 * standalone only -- not allowed to run while DS is up.
 */
int
ldbm_back_upgradedb(Slapi_PBlock *pb)
{
    /* upgradedb should be deprecated */
    struct ldbminfo *li = NULL;
    int task_flags;

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_TASK_FLAGS, &task_flags);

    if (task_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE) {
        dblayer_setup(li);
        li->li_flags |= SLAPI_TASK_RUNNING_FROM_COMMANDLINE;
    }
    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;

    return priv->dblayer_upgradedb_fn(pb);
}

#define LOG "log."


/*
 * ldbm_back_upgradednformat
 *
 * Update old DN format in entrydn and the leaf attr value to the new one
 *
 * The implementation would be similar to the upgradedb for new idl.
 * Scan each entry, checking the entrydn value with the normalized dn.
 * If they don't match,
 *   replace the old entrydn value with the new one in the entry
 *   in id2entry.db4.
 *   also get the leaf RDN attribute value, unescape it, and check
 *   if it is in the entry.  If not, add it.
 * Then, update the key in the entrydn index and the leaf RDN attribute
 * (if need it).
 *
 * Return value:  0: success (the backend instance includes update
 *                   candidates for DRYRUN mode)
 *                1: the backend instance is up-to-date (DRYRUN mode only)
 *               -1: error
 *
 * standalone only -- not allowed to run while DS is up.
 */
int
ldbm_back_upgradednformat(Slapi_PBlock *pb)
{
    struct ldbminfo *li = NULL;
    int task_flags;
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_TASK_FLAGS, &task_flags);

    if (task_flags & SLAPI_TASK_RUNNING_FROM_COMMANDLINE) {
        if (dblayer_setup(li)) {
            slapi_log_err(SLAPI_LOG_CRIT, "ldbm_back_upgradednformat", "dblayer_setup failed\n");
            return -1;
        }
        li->li_flags |= SLAPI_TASK_RUNNING_FROM_COMMANDLINE;
    }
    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;

    return priv->dblayer_upgradedn_fn(pb);
}

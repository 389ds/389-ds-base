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
 * dse.c - DSE (DSA-Specific Entry) persistent storage.
 *
 * The DSE store is an LDIF file contained in the file dse.ldif.
 * The file is located in the directory specified with '-D'
 * when staring the server.
 *
 * In core, the DSEs are stored in an AVL tree, keyed on
 * DN.  Whenever a modification is made to a DSE, the
 * in-core entry is updated, then dse_write_file() is
 * called to commit the changes to disk.
 *
 * This is designed for a small number of DSEs, say
 * a maximum of 10 or 20.  If large numbers of DSEs
 * need to be stored, this approach of writing out
 * the entire contents on every modification will
 * be insufficient.
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <prio.h>
#include <prcountr.h>
#include "slap.h"
#include <pwd.h>
/* Needed to access read_config_dse */
#include "proto-slap.h"
#include <stdbool.h>

#include <unistd.h> /* provides fsync/close */

/* #define SLAPI_DSE_DEBUG */ /* define this to force trace log    */
                              /* messages to always be logged        */

#ifdef SLAPI_DSE_DEBUG
#define SLAPI_DSE_TRACELEVEL SLAPI_LOG_INFO
#else /* SLAPI_DSE_DEBUG */
#define SLAPI_DSE_TRACELEVEL SLAPI_LOG_TRACE
#endif /* SLAPI_DSE_DEBUG */

#define SCHEMA_VIOLATION -2
#define STOP_TRAVERSAL -2

/* This is returned by dupentry_replace if the duplicate entry was found and
   replaced.  This is returned up through the avl_insert() in
   dse_replace_entry().  Otherwise, if avl_insert() returns 0, the
   entry was added i.e. a duplicate was not found.
*/
#define DSE_ENTRY_WAS_REPLACED -3
/* This is returned by dupentry_merge if the duplicate entry was found and
   merged.  This is returned up through the avl_insert() in dse_add_entry_pb().
   Otherwise, if avl_insert() returns 0, the
   entry was added i.e. a duplicate was not found.
*/
#define DSE_ENTRY_WAS_MERGED -4

/* some functions can be used either from within a lock or "standalone" */
#define DSE_USE_LOCK 1
#define DSE_NO_LOCK 0

struct dse_callback
{
    int operation;
    int flags;
    Slapi_DN *base;
    int scope;
    char *filter;              /* NULL means match all entries */
    Slapi_Filter *slapifilter; /* NULL means match all entries */
    int (*fn)(Slapi_PBlock *, Slapi_Entry *, Slapi_Entry *, int *, char *, void *);
    void *fn_arg;
    struct slapdplugin *plugin;
    struct dse_callback *next;
};

struct dse
{
    char *dse_filename;    /* these are the primary files which get read from */
    char *dse_tmpfile;     /* and written to when changes are made via LDAP */
    char *dse_fileback;    /* contain the latest info, just before a new change */
    char *dse_filestartOK; /* contain the latest info with which the server has successfully started */
    char *dse_configdir;   /* The location of config files - allows us to fsync the dir post rename */
    Avlnode *dse_tree;
    struct dse_callback *dse_callback;
    Slapi_RWLock *dse_rwlock;        /* a read-write lock to protect the whole dse backend */
    char **dse_filelist;             /* these are additional read only files used to */
                                     /* initialize the dse */
    int dse_is_updateable;           /* if non-zero, this DSE can be written to */
    int dse_readonly_error_reported; /* used to ensure that read-only errors are logged only once */
    pthread_mutex_t dse_backup_lock; /* used to block write when online backup is in progress */
    bool dse_backup_in_progress;     /* tell that online backup is in progress (protected by dse_rwlock) */
};

struct dse_node
{
    Slapi_Entry *entry;
};

/* search set stuff - used to pass search results to the frontend */
typedef struct dse_search_set
{
    DataList dl;
    int current_entry;
} dse_search_set;

static int dse_permission_to_write(struct dse *pdse, int loglevel);
static int dse_write_file_nolock(struct dse *pdse);
static int dse_apply_nolock(struct dse *pdse, int32_t (*fp)(caddr_t, caddr_t), caddr_t arg);
static int dse_replace_entry(struct dse *pdse, Slapi_Entry *e, int write_file, int use_lock);
static dse_search_set *dse_search_set_new(void);
static void dse_search_set_delete(dse_search_set *ss);
static void dse_search_set_clean(dse_search_set *ss);
static void dse_free_entry(void **data);
static void dse_search_set_add_entry(dse_search_set *ss, Slapi_Entry *e);
static Slapi_Entry *dse_search_set_get_next_entry(dse_search_set *ss);
static int dse_add_entry_pb(struct dse *pdse, Slapi_Entry *e, Slapi_PBlock *pb);
static struct dse_node *dse_find_node(struct dse *pdse, const Slapi_DN *dn);
static int dse_modify_plugin(Slapi_Entry *pre_entry, Slapi_Entry *post_entry, char *returntext);
static int dse_add_plugin(Slapi_Entry *entry, char *returntext);
static int dse_delete_plugin(Slapi_Entry *entry, char *returntext);
static int dse_pre_modify_plugin(Slapi_Entry *entryBefore, Slapi_Entry *entryAfter, LDAPMod **mods);


/*
  richm: In almost all modes e.g. db2ldif, ldif2db, etc. we do not need/want
  to write out the dse.ldif and ldbm.ldif files.  The only mode which really
  needs to write out the file is the regular server mode.  The variable
  dont_ever_write_dse_files tells dse_write_file_nolock whether or not to write
  the .ldif file for the entry.  The default is 1, which means never write the
  file.  The server, when it starts up in regular mode, must call
  dse_unset_dont_ever_write_dse_files() to enable this file to be written
*/
static int dont_ever_write_dse_files = 1;

/* Forward declarations */
static int entry_dn_cmp(caddr_t d1, caddr_t d2);
static int dupentry_disallow(caddr_t d1, caddr_t d2);
static int dupentry_merge(caddr_t d1, caddr_t d2);
static int dse_write_entry(caddr_t data, caddr_t arg);
static int ldif_record_end(char *p);
static int dse_call_callback(struct dse *pdse, Slapi_PBlock *pb, int operation, int flags, Slapi_Entry *entryBefore, Slapi_Entry *entryAfter, int *returncode, char *returntext);

/* Lock the dse in read mode */
INLINE_DIRECTIVE static void
dse_lock_read(struct dse *pdse, int use_lock)
{
    if (use_lock == DSE_USE_LOCK && pdse->dse_rwlock) {
        slapi_rwlock_rdlock(pdse->dse_rwlock);
    }
}

/* Lock the dse in write mode and wait until the */
INLINE_DIRECTIVE static void
dse_lock_write(struct dse *pdse, int use_lock)
{
    if (use_lock != DSE_USE_LOCK || !pdse->dse_rwlock) {
        return;
    }
    slapi_rwlock_wrlock(pdse->dse_rwlock);
    while (pdse->dse_backup_in_progress) {
        slapi_rwlock_unlock(pdse->dse_rwlock);
        /* Wait util dse_backup_lock is unlocked */
        pthread_mutex_lock(&pdse->dse_backup_lock);
        pthread_mutex_unlock(&pdse->dse_backup_lock);
        slapi_rwlock_wrlock(pdse->dse_rwlock);
    }
}

/* release the dse lock */
INLINE_DIRECTIVE static void
dse_lock_unlock(struct dse *pdse, int use_lock)
{
    if (use_lock == DSE_USE_LOCK && pdse->dse_rwlock) {
        slapi_rwlock_unlock(pdse->dse_rwlock);
    }
}

/* Call cb(pdse) */
INLINE_DIRECTIVE static void
dse_call_cb(void (*cb)(struct dse*))
{
    Slapi_Backend *be = slapi_be_select_by_instance_name("DSE");
    if (be) {
        struct dse *pdse = NULL;
        slapi_be_Rlock(be);
        pdse = be->be_database->plg_private;
        if (pdse) {
            cb(pdse);
        }
        slapi_be_Unlock(be);
    }
}

/* Helper for dse_backup_lock() */
static void
dse_backup_lock_cb(struct dse *pdse)
{
    pthread_mutex_lock(&pdse->dse_backup_lock);
    slapi_rwlock_wrlock(pdse->dse_rwlock);
    pdse->dse_backup_in_progress = true;
    slapi_rwlock_unlock(pdse->dse_rwlock);
}

/* Helper for dse_backup_unlock() */
static void
dse_backup_unlock_cb(struct dse *pdse)
{
    slapi_rwlock_wrlock(pdse->dse_rwlock);
    pdse->dse_backup_in_progress = false;
    slapi_rwlock_unlock(pdse->dse_rwlock);
    pthread_mutex_unlock(&pdse->dse_backup_lock);
}

/* Tells that a backup thread is starting */
void
dse_backup_lock()
{
    dse_call_cb(dse_backup_lock_cb);
}

/* Tells that a backup thread is ending */
void
dse_backup_unlock()
{
    dse_call_cb(dse_backup_unlock_cb);
}

/*
 * Map a DN onto a dse_node.
 * Returns NULL if not found.
 * You must have a read or write lock on the dse_rwlock while
 * using the returned node.
 */
static struct dse_node *
dse_find_node(struct dse *pdse, const Slapi_DN *dn)
{
    struct dse_node *n = NULL;
    if (NULL != dn) {
        struct dse_node searchNode;
        Slapi_Entry *fe = slapi_entry_alloc();
        slapi_entry_init(fe, NULL, NULL);
        slapi_entry_set_sdn(fe, dn);
        searchNode.entry = fe;

        n = (struct dse_node *)avl_find(pdse->dse_tree, (caddr_t)&searchNode, entry_dn_cmp);

        slapi_entry_free(fe);
    }
    return n;
}

static int counters_created = 0;
PR_DEFINE_COUNTER(dse_entries_exist);

/*
 * Map a DN onto a real Entry.
 * Returns NULL if not found.
 */
static Slapi_Entry *
dse_get_entry_copy(struct dse *pdse, const Slapi_DN *dn, int use_lock)
{
    Slapi_Entry *e = NULL;
    struct dse_node *n;

    dse_lock_read(pdse, use_lock);
    n = dse_find_node(pdse, dn);
    if (n != NULL) {
        e = slapi_entry_dup(n->entry);
    }
    dse_lock_unlock(pdse, use_lock);

    return e;
}

static struct dse_callback *
dse_callback_new(int operation,
                 int flags,
                 const Slapi_DN *base,
                 int scope,
                 const char *filter,
                 dseCallbackFn fn,
                 void *fn_arg,
                 struct slapdplugin *plugin)
{
    struct dse_callback *p = NULL;
    p = (struct dse_callback *)slapi_ch_calloc(1, sizeof(struct dse_callback));
    if (p != NULL) {
        p->operation = operation;
        p->flags = flags;
        p->base = slapi_sdn_dup(base);
        p->scope = scope;
        if (NULL == filter) {
            p->filter = NULL;
            p->slapifilter = NULL;
        } else {
            p->filter = slapi_ch_strdup(filter);
            p->slapifilter = slapi_str2filter(p->filter);
            filter_normalize(p->slapifilter);
        }
        p->fn = fn;
        p->fn_arg = fn_arg;
        p->plugin = plugin;
        p->next = NULL;
    }
    return p;
}

static void
dse_callback_delete(struct dse_callback **pp)
{
    if (pp != NULL) {
        slapi_sdn_free(&((*pp)->base));
        slapi_ch_free((void **)&((*pp)->filter));
        slapi_filter_free((*pp)->slapifilter, 1);
        slapi_ch_free((void **)pp);
    }
}

static void
dse_callback_deletelist(struct dse_callback **pp)
{
    if (pp != NULL) {
        struct dse_callback *p, *n;
        for (p = *pp; p != NULL;) {
            n = p->next;
            dse_callback_delete(&p);
            p = n;
        }
    }
}

/*
  Makes a copy of the entry passed in, so it's const
*/
static struct dse_node *
dse_node_new(const Slapi_Entry *entry)
{
    struct dse_node *p = NULL;
    p = (struct dse_node *)slapi_ch_malloc(sizeof(struct dse_node));
    if (p != NULL) {
        p->entry = slapi_entry_dup(entry);
    }
    if (!counters_created) {
        PR_CREATE_COUNTER(dse_entries_exist, "DSE", "entries", "");
        counters_created = 1;
    }
    PR_INCREMENT_COUNTER(dse_entries_exist);
    return p;
}

static void
dse_node_delete(struct dse_node **pp)
{
    slapi_entry_free((*pp)->entry);
    slapi_ch_free((void **)&(*pp));
    PR_DECREMENT_COUNTER(dse_entries_exist);
}

static void
dse_callback_addtolist(struct dse_callback **pplist, struct dse_callback *p)
{
    if (pplist != NULL) {
        p->next = NULL;
        if (*pplist == NULL) {
            *pplist = p;
        } else {
            struct dse_callback *t = *pplist;
            for (; t->next != NULL; t = t->next)
                ;
            t->next = p;
        }
    }
}

static void
dse_callback_removefromlist(struct dse_callback **pplist, int operation, int flags, const Slapi_DN *base, int scope, const char *filter, dseCallbackFn fn)
{
    if (pplist != NULL) {
        struct dse_callback *t = *pplist;
        struct dse_callback *prev = NULL;
        for (; t != NULL;) {
            if ((t->operation & operation) && (t->flags & flags) &&
                (t->fn == fn) && (scope == t->scope) &&
                (slapi_sdn_compare(base, t->base) == 0) &&
                ((NULL == filter && NULL == t->filter) || /* both are NULL OR */
                 ((filter && t->filter) &&                /* both are not NULL AND match. */
                  (strcasecmp(filter, t->filter) == 0)))) {
                if (prev == NULL) {
                    *pplist = t->next;
                } else {
                    prev->next = t->next;
                }
                dse_callback_delete(&t);
                t = NULL;
            } else {
                prev = t;
                t = t->next;
            }
        }
    }
}

/*
 * Create a new dse structure.
 */
struct dse *
dse_new(char *filename, char *tmpfilename, char *backfilename, char *startokfilename, const char *configdir)
{
    struct dse *pdse = NULL;
    char *realconfigdir = NULL;

    if (configdir != NULL) {
        realconfigdir = slapi_ch_strdup(configdir);
    } else {
        realconfigdir = config_get_configdir();
    }
    if (realconfigdir != NULL) {
        pdse = (struct dse *)slapi_ch_calloc(1, sizeof(struct dse));
        if (pdse != NULL) {
            pdse->dse_rwlock = slapi_new_rwlock();
            /* Set the full path name for the config DSE entry */
            if (!strstr(filename, realconfigdir)) {
                pdse->dse_filename = slapi_ch_smprintf("%s/%s", realconfigdir, filename);
            } else {
                pdse->dse_filename = slapi_ch_strdup(filename);
            }

            if (!strstr(tmpfilename, realconfigdir)) {
                pdse->dse_tmpfile = slapi_ch_smprintf("%s/%s", realconfigdir, tmpfilename);
            } else {
                pdse->dse_tmpfile = slapi_ch_strdup(tmpfilename);
            }

            pdse->dse_configdir = slapi_ch_strdup(realconfigdir);

            if (backfilename != NULL) {
                if (!strstr(backfilename, realconfigdir)) {
                    pdse->dse_fileback = slapi_ch_smprintf("%s/%s", realconfigdir, backfilename);
                } else {
                    pdse->dse_fileback = slapi_ch_strdup(backfilename);
                }
            } else {
                pdse->dse_fileback = NULL;
            }

            if (startokfilename != NULL) {
                if (!strstr(startokfilename, realconfigdir)) {
                    pdse->dse_filestartOK = slapi_ch_smprintf("%s/%s", realconfigdir, startokfilename);
                } else {
                    pdse->dse_filestartOK = slapi_ch_strdup(startokfilename);
                }
            } else {
                pdse->dse_filestartOK = NULL;
            }

            pdse->dse_tree = NULL;
            pdse->dse_callback = NULL;
            pdse->dse_is_updateable = dse_permission_to_write(pdse,
                                                              SLAPI_LOG_TRACE);
            pthread_mutex_init(&pdse->dse_backup_lock, NULL);
        }
        slapi_ch_free((void **)&realconfigdir);
    }
    return pdse;
}

/*
 * Create a new dse structure with a file list
 */
struct dse *
dse_new_with_filelist(char *filename, char *tmpfilename, char *backfilename, char *startokfilename, const char *configdir, char **filelist)
{
    struct dse *newdse = dse_new(filename, tmpfilename, backfilename, startokfilename, configdir);
    newdse->dse_filelist = filelist;
    return newdse;
}

static int
dse_internal_delete_entry(caddr_t data)
{
    struct dse_node *n = (struct dse_node *)data;
    dse_node_delete(&n);
    return 0;
}

/*
 * Get rid of a dse structure.
 */
int
dse_destroy(struct dse *pdse)
{
    int nentries = 0;

    if (NULL == pdse) {
        return 0; /* no one checks this return value */
    }
    dse_lock_write(pdse, DSE_USE_LOCK);
    slapi_ch_free((void **)&(pdse->dse_filename));
    slapi_ch_free((void **)&(pdse->dse_tmpfile));
    slapi_ch_free((void **)&(pdse->dse_fileback));
    slapi_ch_free((void **)&(pdse->dse_filestartOK));
    slapi_ch_free((void **)&(pdse->dse_configdir));
    dse_callback_deletelist(&pdse->dse_callback);
    charray_free(pdse->dse_filelist);
    nentries = avl_free(pdse->dse_tree, dse_internal_delete_entry);
    dse_lock_unlock(pdse, DSE_USE_LOCK);
    if (pdse->dse_rwlock) {
        slapi_destroy_rwlock(pdse->dse_rwlock);
    }
    slapi_ch_free((void **)&pdse);
    slapi_log_err(SLAPI_DSE_TRACELEVEL, "dse_destroy", "Removed [%d] entries from the dse tree.\n",
                  nentries);

    return 0; /* no one checks this return value */
}

/*
 * Get rid of a dse structure.
 */
int
dse_deletedse(Slapi_PBlock *pb)
{
    struct dse *pdse = NULL;

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &pdse);

    if (pdse) {
        dse_destroy(pdse);
    }

    /* data is freed, so make sure no one tries to use it */
    slapi_pblock_set(pb, SLAPI_PLUGIN_PRIVATE, NULL);

    return 0;
}

static char *subordinatecount = "numsubordinates";

/*
 * Get the number of subordinates for this entry.
 */
static size_t
dse_numsubordinates(Slapi_Entry *entry)
{
    int ret = 0;
    Slapi_Attr *read_attr = NULL;
    size_t current_sub_count = 0;

    /* Get the present value of the subcount attr, or 0 if not present */
    ret = slapi_entry_attr_find(entry, subordinatecount, &read_attr);
    if (0 == ret) {
        /* decode the value */
        Slapi_Value *sval;
        slapi_attr_first_value(read_attr, &sval);
        if (sval != NULL) {
            const struct berval *bval = slapi_value_get_berval(sval);
            if (NULL != bval)
                current_sub_count = atol(bval->bv_val);
        }
    }
    return current_sub_count;
}

/*
 * Update the numsubordinates count.
 * mod_op is either an Add or Delete.
 */
static void
dse_updateNumSubordinates(Slapi_Entry *entry, int op)
{
    int ret = 0;
    int mod_op = 0;
    Slapi_Attr *read_attr = NULL;
    size_t current_sub_count = 0;
    int already_present = 0;

    /* For now, we're only interested in subordinatecount.
       We first examine the present value for the attribute.
       If it isn't present and we're adding, we assign value 1 to the attribute and add it.
       If it is present, we increment or decrement depending upon whether we're adding or deleting.
       If the value after decrementing is zero, we remove it.
    */

    /* Get the present value of the subcount attr, or 0 if not present */
    ret = slapi_entry_attr_find(entry, subordinatecount, &read_attr);
    if (0 == ret) {
        /* decode the value */
        Slapi_Value *sval;
        slapi_attr_first_value(read_attr, &sval);
        if (sval != NULL) {
            const struct berval *bval = slapi_value_get_berval(sval);
            if (bval != NULL) {
                already_present = 1;
                current_sub_count = atol(bval->bv_val);
            }
        }
    }

    /* are we adding ? */
    if ((SLAPI_OPERATION_ADD == op) && !already_present) {
        /* If so, and the parent entry does not already have a subcount attribute, we need to add it */
        mod_op = LDAP_MOD_ADD;
    } else {
        if (SLAPI_OPERATION_DELETE == op) {
            if (!already_present) {
                /* This means that something is wrong---deleting a child but no subcount present on parent */
                slapi_log_err(SLAPI_LOG_ERR, "dse_updateNumSubordinates",
                              "numsubordinates assertion failure\n");
                return;
            } else {
                if (current_sub_count == 1) {
                    mod_op = LDAP_MOD_DELETE;
                } else {
                    mod_op = LDAP_MOD_REPLACE;
                }
            }
        } else {
            mod_op = LDAP_MOD_REPLACE;
        }
    }

    /* Now compute the new value */
    if (SLAPI_OPERATION_ADD == op) {
        current_sub_count++;
    } else {
        current_sub_count--;
    }
    {
        char value_buffer[22] = {0}; /* enough digits for 2^64 children */
        struct berval *vals[2];
        struct berval val;
        vals[0] = &val;
        vals[1] = NULL;
        sprintf(value_buffer, "%lu", (long unsigned int)current_sub_count);
        val.bv_val = value_buffer;
        val.bv_len = strlen(val.bv_val);
        switch (mod_op) {
        case LDAP_MOD_ADD:
            attrlist_merge(&entry->e_attrs, subordinatecount, vals);
            break;
        case LDAP_MOD_REPLACE:
            attrlist_replace(&entry->e_attrs, subordinatecount, vals);
            break;
        case LDAP_MOD_DELETE:
            attrlist_delete(&entry->e_attrs, subordinatecount);
            break;
        }
    }
}

/* the write lock should always be acquired before calling this function */
static void
dse_updateNumSubOfParent(struct dse *pdse, const Slapi_DN *child, int op)
{
    Slapi_DN parent;
    slapi_sdn_init(&parent);
    slapi_sdn_get_parent(child, &parent);
    if (!slapi_sdn_isempty(&parent)) {
        /* no lock because caller should already have the write lock */
        Slapi_Entry *parententry = dse_get_entry_copy(pdse, &parent, DSE_NO_LOCK);
        if (parententry != NULL) {
            /* Decrement the numsubordinate count of the parent entry */
            dse_updateNumSubordinates(parententry, op);
            /* no lock because caller should always have the write lock */
            dse_replace_entry(pdse, parententry, 0, DSE_NO_LOCK);
            slapi_entry_free(parententry);
        }
    }
    slapi_sdn_done(&parent);
}

/* check if a file is valid, or if a provided backup file can be used.
 * there is no way to determine if the file contents is usable, the only
 * checks that can be done is that the file exists and that it is not size 0
 */
int
dse_check_file(char *filename, char *backupname)
{
    int rc = 0; /* Fail */
    PRFileInfo64 prfinfo;

    if (PR_GetFileInfo64(filename, &prfinfo) == PR_SUCCESS) {
        if (prfinfo.size > 0) {
            /* File exists and has content. */
            return 1;
        } else {
            slapi_log_err(SLAPI_LOG_INFO, "dse_check_file",
                          "The config %s has zero length. Attempting restore ... \n", filename);
            rc = PR_Delete(filename);
        }
    } else {
        slapi_log_err(SLAPI_LOG_INFO, "dse_check_file",
                      "The config %s can not be accessed. Attempting restore ... (reason: %d)\n", filename, rc);
    }

    if (backupname) {

        if (PR_GetFileInfo64(backupname, &prfinfo) != PR_SUCCESS) {
            slapi_log_err(SLAPI_LOG_INFO, "dse_check_file",
                          "The backup %s can not be accessed. Check it exists and permissions.\n", backupname);
            return 0;
        }

        if (prfinfo.size <= 0) {
            slapi_log_err(SLAPI_LOG_ERR, "dse_check_file",
                      "The backup file %s has zero length, refusing to restore it.\n", backupname);
            return 0;
        }

        rc = PR_Rename(backupname, filename);
        if (rc != PR_SUCCESS) {
            slapi_log_err(SLAPI_LOG_INFO, "dse_check_file",
                      "The configuration file %s was NOT able to be restored from %s, error %d\n", filename, backupname, rc);
            return 0;
        }

        slapi_log_err(SLAPI_LOG_INFO, "dse_check_file",
                  "The configuration file %s was restored from backup %s\n", filename, backupname);
        return 1;

    } else {
        slapi_log_err(SLAPI_LOG_INFO, "dse_check_file", "No backup filename provided.\n");
        return 0;
    }
}

static int
dse_read_one_file(struct dse *pdse, const char *filename, Slapi_PBlock *pb, int primary_file)
{
    Slapi_Entry *e = NULL;
    char *entrystr = NULL;
    char *buf = NULL;
    char *lastp = NULL;
    int rc = 0; /* Fail */
    PRInt32 nr = 0;
    PRFileInfo64 prfinfo;
    PRFileDesc *prfd = 0;
    int schema_flags = 0;

    slapi_pblock_get(pb, SLAPI_SCHEMA_FLAGS, &schema_flags);

    if ((NULL != pdse) && (NULL != filename)) {
        /* check if the "real" file exists and cam be used, if not try tmp as backup */
        rc = dse_check_file((char *)filename, pdse->dse_tmpfile);
        if (!rc) {
            rc = dse_check_file((char *)filename, pdse->dse_fileback);
        }

        if ((rc = PR_GetFileInfo64(filename, &prfinfo)) != PR_SUCCESS) {
            slapi_log_err(SLAPI_LOG_ERR, "dse_read_one_file",
                          "The configuration file %s could not be accessed, error %d\n",
                          filename, rc);
            rc = 0; /* Fail */
        } else if ((prfd = PR_Open(filename, PR_RDONLY, SLAPD_DEFAULT_DSE_FILE_MODE)) == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "dse_read_one_file",
                          "The configuration file %s could not be read. " SLAPI_COMPONENT_NAME_NSPR " %d (%s)\n",
                          filename,
                          PR_GetError(), slapd_pr_strerror(PR_GetError()));
            rc = 0; /* Fail */
        } else {
            int done = 0;
            /* read the entire file into core */
            buf = slapi_ch_malloc(prfinfo.size + 1);
            if ((nr = slapi_read_buffer(prfd, buf, prfinfo.size)) < 0) {
                slapi_log_err(SLAPI_LOG_ERR, "dse_read_one_file",
                              "Could only read %d of %ld bytes from config file %s\n",
                              nr, (long int)prfinfo.size, filename);
                rc = 0; /* Fail */
                done = 1;
            }

            (void)PR_Close(prfd);
            buf[nr] = '\0';

            if (!done) {
                int lineno = 1;
                int lines = 0;
                int dont_check_dups = 0;
                int str2entry_flags = SLAPI_STR2ENTRY_EXPAND_OBJECTCLASSES |
                                      SLAPI_STR2ENTRY_NOT_WELL_FORMED_LDIF;
                if (schema_flags & DSE_SCHEMA_LOCKED)
                    str2entry_flags |= SLAPI_STR2ENTRY_NO_SCHEMA_LOCK;

                PR_ASSERT(pb);
                slapi_pblock_get(pb, SLAPI_DSE_DONT_CHECK_DUPS, &dont_check_dups);
                if (!dont_check_dups) {
                    str2entry_flags |= SLAPI_STR2ENTRY_REMOVEDUPVALS;
                }

                /* Convert LDIF to entry structures */
                rc = 1; /* assume we will succeed */
                while ((entrystr = dse_read_next_entry(buf, &lastp)) != NULL) {
                    char *p, *q;
                    char errbuf[1024];
                    size_t estrlen = strlen(entrystr);
                    size_t cpylen =
                        (estrlen < sizeof(errbuf)) ? estrlen : sizeof(errbuf) - 1;

                    memcpy(errbuf, entrystr, cpylen);
                    errbuf[cpylen] = '\0';

                    lines = 1;
                    p = entrystr;
                    while ((q = strchr(p, '\n'))) {
                        p = q + 1;
                        lines++;
                    }

                    e = slapi_str2entry(entrystr, str2entry_flags);
                    if (e != NULL) {
                        int returncode = 0;
                        char returntext[SLAPI_DSE_RETURNTEXT_SIZE] = {0};

                        slapi_log_err(SLAPI_LOG_TRACE, "dse_read_one_file",
                                      " processing entry \"%s\" in file %s%s "
                                      "(lineno: %d)\n",
                                      slapi_entry_get_dn_const(e), filename,
                                      primary_file ? " (primary file)" : "",
                                      lineno);

                        /* remove the numsubordinates attr, which may be bogus */
                        slapi_entry_attr_delete(e, subordinatecount);

                        /* set the "primary file" flag if appropriate */
                        slapi_pblock_set(pb, SLAPI_DSE_IS_PRIMARY_FILE, &primary_file);
                        if (dse_call_callback(pdse, pb, DSE_OPERATION_READ,
                                              DSE_FLAG_PREOP, e, NULL, &returncode,
                                              returntext) == SLAPI_DSE_CALLBACK_OK) {
                            /*
                             * This will free the entry if not added, so it is
                             * definitely consumed by this call
                             */
                            if (dse_add_entry_pb(pdse, e, pb) == SCHEMA_VIOLATION) {
                                /* schema violation, return failure */
                                rc = 0;
                            }
                        } else /* free entry if not used */
                        {
                            slapi_log_err(SLAPI_LOG_FATAL,
                                          "dse_read_one_file",
                                          "The entry %s in file %s "
                                          "(lineno: %d) is invalid, "
                                          "error code %d (%s) - %s\n",
                                          slapi_entry_get_dn_const(e),
                                          filename, lineno, returncode,
                                          ldap_err2string(returncode),
                                          returntext);
                            slapi_entry_free(e);
                            rc = 0; /* failure */
                        }
                    } else {
                        slapi_log_err(SLAPI_LOG_ERR, "dse_read_one_file",
                                      "Parsing entry (lineno: %d) "
                                      "in file %s failed.\n",
                                      lineno, filename);
                        slapi_log_err(SLAPI_LOG_ERR, "dse_read_one_file",
                                      "Invalid section [%s%s]\n",
                                      errbuf, cpylen == estrlen ? "" : " ...");
                        rc = 0; /* failure */
                    }
                    lineno += lines + 1 /* 1 is for a blank line. */;
                }
            }
            slapi_ch_free((void **)&buf);
        }
    }

    return rc;
}

/*
 * Read the file we were initialised with into memory.
 * If not NULL call entry_filter_fn on each entry as it's read.
 * The function is free to modify the entry before it's places
 * into the AVL tree. True means add the entry. False means don't.
 *
 * Return 1 for OK, 0 for Fail.
 */
int
dse_read_file(struct dse *pdse, Slapi_PBlock *pb)
{
    int rc = 1; /* Good */
    int ii;
    char **filelist = 0;
    char *filename = 0;

    filelist = charray_dup(pdse->dse_filelist);
    filename = slapi_ch_strdup(pdse->dse_filename);

    for (ii = 0; rc && filelist && filelist[ii]; ++ii) {
        if (strcasecmp(filename, filelist[ii]) != 0) {
            rc = dse_read_one_file(pdse, filelist[ii], pb, 0 /* not primary */);
        }
    }

    if (rc) {
        rc = dse_read_one_file(pdse, filename, pb, 1 /* primary file */);
    }

    charray_free(filelist);
    slapi_ch_free((void **)&filename);

    return rc;
}

/*
 * Structure to carry context information whilst
 * traversing the tree writing the entries to disk.
 */
typedef struct _fpw
{
    PRFileDesc *fpw_prfd;
    int fpw_rc;
    struct dse *fpw_pdse;
} FPWrapper;


static int
dse_rw_permission_to_one_file(const char *name, int loglevel)
{
    PRErrorCode prerr = 0;
    const char *accesstype = "";

    if (NULL == name) {
        return 1; /* file won't be used -- return "sufficient permission" */
    }

    if (PR_Access(name, PR_ACCESS_EXISTS) == PR_SUCCESS) {
        /* file exists: check for read and write permission */
        if (PR_Access(name, PR_ACCESS_WRITE_OK) != PR_SUCCESS) {
            prerr = PR_GetError();
            accesstype = "write";
        } else if (PR_Access(name, PR_ACCESS_READ_OK) != PR_SUCCESS) {
            prerr = PR_GetError();
            accesstype = "read";
        }
    } else {
        /* file does not exist: make sure we can create it */
        PRFileDesc *prfd;

        prfd = PR_Open(name, PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE,
                       SLAPD_DEFAULT_DSE_FILE_MODE);
        if (NULL == prfd) {
            prerr = PR_GetError();
            accesstype = "create";
        } else {
            PR_Close(prfd);
            PR_Delete(name);
        }
    }

    if (prerr != 0) {
        slapi_log_err(loglevel,
                      "dse_rw_permission_to_one_file", "Unable to %s \"%s\": " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                      accesstype, name, prerr, slapd_pr_strerror(prerr));
        return 0; /* insufficient permission */
    } else {
        return 1; /* sufficient permission */
    }
}


/*
 * Check that we have permission to write to all the files that
 * dse_write_file_nolock() uses.
 * Returns a non-zero value if sufficient permission and 0 if not.
 */
static int
dse_permission_to_write(struct dse *pdse, int loglevel)
{
    int rc = 1; /* sufficient permission */

    if (NULL != pdse->dse_filename) {
        if (!dse_rw_permission_to_one_file(pdse->dse_filename, loglevel) ||
            !dse_rw_permission_to_one_file(pdse->dse_fileback, loglevel) ||
            !dse_rw_permission_to_one_file(pdse->dse_tmpfile, loglevel)) {
            rc = 0; /* insufficient permission */
        }
    }

    return rc;
}


/*
 * Check for read-only status and return an appropriate error to the
 * LDAP client.
 * Returns 0 if no error was returned and non-zero if one was.
 */
static int
dse_check_for_readonly_error(Slapi_PBlock *pb, struct dse *pdse)
{
    int rc = 0; /* default: no error */

    dse_lock_read(pdse, DSE_USE_LOCK);
    if (!pdse->dse_is_updateable) {
        if (!pdse->dse_readonly_error_reported) {
            if (NULL != pdse->dse_filename) {
                slapi_log_err(SLAPI_LOG_ERR, "dse_check_for_readonly_error",
                              "The DSE database stored in \"%s\" is not writeable\n",
                              pdse->dse_filename);
                /* log the details too */
                (void)dse_permission_to_write(pdse, SLAPI_LOG_ERR);
            }
            pdse->dse_readonly_error_reported = 1;
        }
        rc = 1; /* return an error to the client */
    }
    dse_lock_unlock(pdse, DSE_USE_LOCK);

    if (rc != 0) {
        slapi_send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                               "DSE database is read-only", 0, NULL);
    }

    return rc; /* no error */
}

/* Trivial wrapper around slapi_re_comp to handle errors */
static Slapi_Regex *
recomp(const char *regexp)
{
    char *error = "";
    Slapi_Regex *re = slapi_re_comp(regexp, &error);
    if (re == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "is_readonly_set_in_dse",
                      "Failed to compile '%s' regular expression. Error is %s\n",
                      regexp, error);
    }
    slapi_ch_free_string(&error);
    return re;
}

/*
 * Check if "nsslapd-readonly: on" is in cn-config in dse.ldif file
 * ( If the flag is set in memory but on in the file, the file should
 *   be written (to let dsconf able to modify the nsslapd-readonly flag)
 */
static bool
is_readonly_set_in_dse(const char *dsename)
{
    Slapi_Regex *re_config = recomp("^dn:\\s+cn=config\\s*$");
    Slapi_Regex *re_isro = recomp("^" CONFIG_READONLY_ATTRIBUTE ":\\s+on\\s*$");
    Slapi_Regex *re_eoe = recomp("^$");
    bool isconfigentry = false;
    bool isro = false;
    FILE *fdse = NULL;
    char line[128];

    if (!dsename) {
        goto done;
    }
    if (re_config == NULL || re_isro == NULL || re_eoe == NULL) {
        goto done;
    }
    fdse = fopen(dsename, "r");
    if (fdse == NULL) {
        /* No dse file, we need to write it */
        goto done;
    }
    while (fgets(line, (sizeof line), fdse)) {
        /* Convert the read line to lowercase */
        for (char *pt=line; *pt; pt++) {
            if (isalpha(*pt)) {
                *pt = tolower(*pt);
            }
        }
        if (slapi_re_exec_nt(re_config, line)) {
            isconfigentry = true;
        }
        if (slapi_re_exec_nt(re_eoe, line)) {
            if (isconfigentry) {
                /* End of config entry ==> readonly flag is not set */
                break;
            }
        }
        if (isconfigentry && slapi_re_exec_nt(re_isro, line)) {
            /* Found readonly flag */
            isro = true;
            break;
        }
    }
done:
    if (fdse) {
        (void) fclose(fdse);
    }
    slapi_re_free(re_config);
    slapi_re_free(re_isro);
    slapi_re_free(re_eoe);
    return isro;
}

/*
 * Check if dse.ldif can be written
 * Beware that even in read-only mode dse.ldif file
 * should still be written to change the nsslapd-readonly value
 */
static bool
check_if_readonly(struct dse *pdse)
{
    static bool ro = false;

    if (pdse->dse_filename == NULL) {
        return false;
    }
    if (!slapi_config_get_readonly()) {
        ro = false;
        return ro;
    }
    if (ro) {
        /* read-only mode and dse is up to date ==> Do not modify it. */
        return ro;
    }
    /* First attempt to write the dse.ldif since readonly mode is enabled.
     * Lets check if "nsslapd-readonly: on" is in cn=config entry
     *  and allow to write the dse.ldif if it is the case
     */
    if (is_readonly_set_in_dse(pdse->dse_filename)) {
        /* read-only mode and dse is up to date ==> Do not modify it. */
        ro = true;
    }
    /* Read only mode but nsslapd-readonly value is not up to date. */
    return ro;
}

/*
 * Write the AVL tree of entries back to the LDIF file.
 */
static int
dse_write_file_nolock(struct dse *pdse)
{
    FPWrapper fpw;
    int rc = 0;

    if (dont_ever_write_dse_files || check_if_readonly(pdse)) {
        return rc;
    }

    fpw.fpw_rc = 0;
    fpw.fpw_prfd = NULL;

    if (NULL != pdse->dse_filename) {
        if ((fpw.fpw_prfd = PR_Open(pdse->dse_tmpfile, PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE, SLAPD_DEFAULT_DSE_FILE_MODE)) == NULL) {
            rc = PR_GetOSError();
            slapi_log_err(SLAPI_LOG_ERR, "dse_write_file_nolock", "Cannot open "
                                                                  "temporary DSE file \"%s\" for update: OS error %d (%s)\n",
                          pdse->dse_tmpfile, rc, slapd_system_strerror(rc));
        } else {
            fpw.fpw_pdse = pdse;
            if (avl_apply(pdse->dse_tree, dse_write_entry, &fpw, STOP_TRAVERSAL, AVL_INORDER) == STOP_TRAVERSAL) {
                rc = fpw.fpw_rc;
                slapi_log_err(SLAPI_LOG_ERR, "dse_write_file_nolock", "Cannot write "
                                                                      " temporary DSE file \"%s\": OS error %d (%s)\n",
                              pdse->dse_tmpfile, rc, slapd_system_strerror(rc));
                (void)PR_Close(fpw.fpw_prfd);
                fpw.fpw_prfd = NULL;
            } else {
                (void)PR_Close(fpw.fpw_prfd);
                fpw.fpw_prfd = NULL;
                if (pdse->dse_fileback != NULL) {
                    rc = slapi_destructive_rename(pdse->dse_filename, pdse->dse_fileback);
                    if (rc != 0) {
                        slapi_log_err(SLAPI_LOG_ERR, "dse_write_file_nolock", "Cannot backup"
                                                                              " DSE file \"%s\" to \"%s\": OS error %d (%s)\n",
                                      pdse->dse_filename, pdse->dse_fileback,
                                      rc, slapd_system_strerror(rc));
                    }
                }
                rc = slapi_destructive_rename(pdse->dse_tmpfile, pdse->dse_filename);
                if (rc != 0) {
                    slapi_log_err(SLAPI_LOG_ERR, "dse_write_file_nolock", "Cannot rename"
                                                                          " temporary DSE file \"%s\" to \"%s\":"
                                                                          " OS error %d (%s)\n",
                                  pdse->dse_tmpfile, pdse->dse_filename,
                                  rc, slapd_system_strerror(rc));
                }
                /*
                 * We have now written to the tmp location, and renamed it
                 * we need to open and fsync the dir to make the rename stick.
                 */
                int fp_configdir =
#ifdef O_PATH
                    open(pdse->dse_configdir, O_PATH | O_DIRECTORY)
#else
                    open(pdse->dse_configdir, O_RDONLY | O_DIRECTORY)
#endif
                    ;
                if (fp_configdir != -1) {
                    fsync(fp_configdir);
                    close(fp_configdir);
                }
            }
        }
        if (fpw.fpw_prfd)
            (void)PR_Close(fpw.fpw_prfd);
    }

    dse_backup_unlock();

    return rc;
}

/*
 * Local function for writing an entry to a file.
 * Called by the AVL code during traversal.
 */
static int
dse_write_entry(caddr_t data, caddr_t arg)
{
    struct dse_node *n = (struct dse_node *)data;
    FPWrapper *fpw = (FPWrapper *)arg;
    char *s;
    PRInt32 len;

    if (NULL != n && NULL != n->entry) {
        int returncode;
        char returntext[SLAPI_DSE_RETURNTEXT_SIZE] = "";
        /* need to make a duplicate here for two reasons:
           1) we don't want to hold on to the raw data in the node for any longer
           than we have to; we will usually be inside the dse write lock, but . . .
           2) the write callback may modify the entry, so we want to pass it a
           writeable copy rather than the raw avl tree data pointer
        */
        Slapi_Entry *ec = slapi_entry_dup(n->entry);
        if (dse_call_callback(fpw->fpw_pdse, NULL, DSE_OPERATION_WRITE,
                              DSE_FLAG_PREOP, ec, NULL, &returncode, returntext) == SLAPI_DSE_CALLBACK_OK) {
            /*
             * 3-August-2000 mcs: We used to pass the SLAPI_DUMP_NOOPATTRS
             * option to slapi_entry2str_with_options() so that operational
             * attributes were NOT stored in the DSE LDIF files.  But now
             * we store all attribute types.
             */
            if ((s = slapi_entry2str_with_options(ec, &len, 0)) != NULL) {
                if (slapi_write_buffer(fpw->fpw_prfd, s, len) != len) {
                    fpw->fpw_rc = PR_GetOSError();
                    ;
                    slapi_ch_free((void **)&s);
                    return STOP_TRAVERSAL;
                }
                if (slapi_write_buffer(fpw->fpw_prfd, "\n", 1) != 1) {
                    fpw->fpw_rc = PR_GetOSError();
                    ;
                    slapi_ch_free((void **)&s);
                    return STOP_TRAVERSAL;
                }
                slapi_ch_free((void **)&s);
            }
        }
        slapi_entry_free(ec);
    }
    return 0;
}

/*
 * Adds an entry to the dse backend.  The passed in entry will be
 * free'd always.
 *
 * return -1 for duplicate entry
 * return -2 for schema violation (SCHEMA_VIOLATION)
 */
static int
dse_add_entry_pb(struct dse *pdse, Slapi_Entry *e, Slapi_PBlock *pb)
{
    int dont_write_file = 0, merge = 0; /* defaults */
    int rc = 0;
    struct dse_node *n = dse_node_new(e); /* copies e */
    Slapi_Entry *schemacheckentry = NULL; /* to use for schema checking */

    PR_ASSERT(pb);
    slapi_pblock_get(pb, SLAPI_DSE_DONT_WRITE_WHEN_ADDING, &dont_write_file);
    slapi_pblock_get(pb, SLAPI_DSE_MERGE_WHEN_ADDING, &merge);

    /* keep write lock during both tree update and file write operations */
    dse_lock_write(pdse, DSE_USE_LOCK);
    if (merge) {
        rc = avl_insert(&(pdse->dse_tree), (caddr_t)n, entry_dn_cmp, dupentry_merge);
    } else {
        rc = avl_insert(&(pdse->dse_tree), (caddr_t)n, entry_dn_cmp, dupentry_disallow);
    }
    if (-1 != rc) {
        /* update num sub of parent with no lock; we already hold the write lock */
        if (0 == rc) { /* entry was added, not merged; update numsub */
            /* easter egg entry - don't update num sub */
            if (strcmp(slapi_entry_get_ndn(e), "ou=red hat directory server team,cn=monitor") != 0) {
                dse_updateNumSubOfParent(pdse, slapi_entry_get_sdn_const(e),
                                         SLAPI_OPERATION_ADD);
            }
        } else { /* entry was merged, free temp unused data */
            dse_node_delete(&n);
        }
        if (!dont_write_file) {
            dse_write_file_nolock(pdse);
        }
    } else {                 /* duplicate entry ignored */
        dse_node_delete(&n); /* This also deletes the contained entry */
    }
    dse_lock_unlock(pdse, DSE_USE_LOCK);

    if (rc == -1) {
        /* duplicate entry ignored */
        schemacheckentry = dse_get_entry_copy(pdse,
                                              slapi_entry_get_sdn_const(e),
                                              DSE_USE_LOCK);
    } else /* entry added or merged */
    {
        /* entry was added or merged */
        if (0 == rc) /* 0 return means entry was added, not merged */
        {
            /* save a search of the tree, since we added the entry, the
               contents of e should be the same as what is in the tree */
            schemacheckentry = slapi_entry_dup(e);
        } else /* merged */
        {
            /* schema check the new merged entry, so get it from the tree */
            schemacheckentry = dse_get_entry_copy(pdse,
                                                  slapi_entry_get_sdn_const(e),
                                                  DSE_USE_LOCK);
        }
    }
    if (NULL != schemacheckentry) {
        /*
         * Verify that the new or merged entry conforms to the schema.
         *        Errors are logged by slapi_entry_schema_check().
         */
        if (slapi_entry_schema_check(pb, schemacheckentry)) {
            rc = SCHEMA_VIOLATION;
        }
        slapi_entry_free(schemacheckentry);
    }

    /* Callers expect e (SLAPI_ADD_ENTRY) to be freed */
    /* This function duplicates 'e' for dse_node 'n' and schemacheckentry.
     * 'e' should not have been consumed */
    slapi_entry_free(e);

    return rc;
}

/*
 * Local function for comparing two entries by DN.  Store the entries
 * so that when they are printed out, the child entries are below their
 * ancestor entries
 */
static int
entry_dn_cmp(caddr_t d1, caddr_t d2)
{
    struct dse_node *n1 = (struct dse_node *)d1;
    struct dse_node *n2 = (struct dse_node *)d2;
    const Slapi_DN *dn1 = slapi_entry_get_sdn_const(n1->entry);
    const Slapi_DN *dn2 = slapi_entry_get_sdn_const(n2->entry);
    int retval = slapi_sdn_compare(dn1, dn2);

    if (retval != 0) {
        if (slapi_sdn_issuffix(dn1, dn2)) {
            retval = 1;
        } else if (slapi_sdn_issuffix(dn2, dn1)) {
            retval = -1;
        } else {
            /* put fewer rdns before more rdns */
            int rc = 0;
            char **dnlist1 = slapi_ldap_explode_dn(slapi_sdn_get_ndn(dn1), 0);
            char **dnlist2 = slapi_ldap_explode_dn(slapi_sdn_get_ndn(dn2), 0);
            int len1 = 0;
            int len2 = 0;
            if (dnlist1)
                for (len1 = 0; dnlist1[len1]; ++len1)
                    ;
            if (dnlist2)
                for (len2 = 0; dnlist2[len2]; ++len2)
                    ;

            if (len1 == len2) {
                len1--;
                for (; (rc == 0) && (len1 >= 0); --len1) {
                    rc = strcmp(dnlist1[len1], dnlist2[len1]);
                }
                if (rc)
                    retval = rc;
            } else
                retval = len1 - len2;

            if (dnlist1)
                slapi_ldap_value_free(dnlist1);
            if (dnlist2)
                slapi_ldap_value_free(dnlist2);
        }
    }
    /* else entries are equal if dns are equal */

    return retval;
}


static int
dupentry_disallow(caddr_t d1 __attribute__((unused)), caddr_t d2 __attribute__((unused)))
{
    return -1;
}


static int
dupentry_replace(caddr_t d1, caddr_t d2)
{
    /*
     * Hack attack: since we don't have the address of the pointer
     * in the avl node, we have to replace the e_dn and e_attrs
     * members of the entry which is in the AVL tree with our
     * new entry DN and attrs.  We then point the "new" entry's
     * e_dn and e_attrs pointers to point to the values we just
     * replaced, on the assumption that the caller will be freeing
     * these.
     */
    struct dse_node *n1 = (struct dse_node *)d1; /* OLD */
    struct dse_node *n2 = (struct dse_node *)d2; /* NEW */
    Slapi_Entry *e = n1->entry;
    n1->entry = n2->entry;
    n2->entry = e;
    return DSE_ENTRY_WAS_REPLACED;
}

static int
dupentry_merge(caddr_t d1, caddr_t d2)
{
    struct dse_node *n1 = (struct dse_node *)d1; /* OLD */
    struct dse_node *n2 = (struct dse_node *)d2; /* NEW */
    Slapi_Entry *e1 = n1->entry;
    Slapi_Entry *e2 = n2->entry;
    int rc = 0;
    Slapi_Attr *newattr = 0;

    for (rc = slapi_entry_first_attr(e2, &newattr);
         !rc && newattr;
         rc = slapi_entry_next_attr(e2, newattr, &newattr)) {
        char *type = 0;
        slapi_attr_get_type(newattr, &type);
        if (type) {
            /* insure there are no duplicate values in e1 */
            rc = slapi_entry_merge_values_sv(e1, type,
                                             attr_get_present_values(newattr));
        }
    }

    return DSE_ENTRY_WAS_MERGED;
}

/*
 * Add an entry to the DSE without locking the DSE avl tree.
 * Replaces the entry if it already exists.
 *
 * The given entry e is never consumed.  It is the responsibility of the
 * caller to free it when it is no longer needed.
 *
 * The write_file flag is used if we want to update the entry in memory
 * but we do not want to write out the file.  For example, if we update
 * the numsubordinates in the entry, this is an operational attribute that
 * we do not want saved to disk.
 */
static int
dse_replace_entry(struct dse *pdse, Slapi_Entry *e, int write_file, int use_lock)
{
    int rc = -1;
    if (NULL != e) {
        struct dse_node *n = dse_node_new(e);
        dse_lock_write(pdse, use_lock);
        rc = avl_insert(&(pdse->dse_tree), (caddr_t)n, entry_dn_cmp, dupentry_replace);
        if (write_file)
            dse_write_file_nolock(pdse);
        /* If the entry was replaced i.e. not added as a new entry, we need to
           free the old data, which is set in dupentry_replace */
        if (DSE_ENTRY_WAS_REPLACED == rc) {
            dse_node_delete(&n);
            rc = 0; /* for return to caller */
        }
        dse_lock_unlock(pdse, use_lock);
    }
    return rc;
}


/*
 * Return -1 if p does not point to a valid LDIF
 * end-of-record delimiter (a NULL, two newlines, or two
 * pairs of CRLF).  Otherwise, return the length of
 * the delimiter found.
 */
static int
ldif_record_end(char *p)
{
    if (NULL != p) {
        if ('\0' == *p) {
            return 0;
        } else if ('\n' == *p && '\n' == *(p + 1)) {
            return 2;
        } else if ('\r' == *p && '\n' == *(p + 1) && '\r' == *(p + 2) && '\n' == *(p + 3)) {
            return 4;
        }
    }
    return -1;
}

char *
dse_read_next_entry(char *buf, char **lastp)
{
    char *p, *start;

    if (NULL == buf) {
        *lastp = NULL;
        return NULL;
    }
    p = start = (NULL == *lastp) ? buf : *lastp;
    /* Skip over any leading record delimiters */
    while ('\n' == *p || '\r' == *p) {
        p++;
    }
    if ('\0' == *p) {
        *lastp = NULL;
        return NULL;
    }
    while ('\0' != *p) {
        int rc;
        if ((rc = ldif_record_end(p)) >= 0) {
            /* Found end of LDIF record */
            *p = '\0';
            p += rc;
            break;
        } else {
            p++;
        }
    }
    *lastp = p;
    return start;
}

/*
 * Apply the function to each entry.  The caller is responsible for locking
 * the rwlock in the dse for the appropriate type of operation e.g. for
 * searching, a read lock, for modifying in place, a write lock
 */
static int
dse_apply_nolock(struct dse *pdse, int32_t (*fp)(caddr_t, caddr_t), caddr_t arg)
{
    avl_apply(pdse->dse_tree, fp, arg, STOP_TRAVERSAL, AVL_INORDER);
    return 1;
}


/*
 * Remove the entry from the tree.
 * Returns 1 if entry is removed and 0 if not.
 */
static int
dse_delete_entry(struct dse *pdse, Slapi_PBlock *pb, const Slapi_Entry *e)
{
    int dont_write_file = 0;
    struct dse_node *n = dse_node_new(e);
    struct dse_node *deleted_node = NULL;

    slapi_pblock_get(pb, SLAPI_DSE_DONT_WRITE_WHEN_ADDING, &dont_write_file);

    /* keep write lock for both tree deleting and file writing */
    dse_lock_write(pdse, DSE_USE_LOCK);
    if ((deleted_node = (struct dse_node *)avl_delete(&pdse->dse_tree, (caddr_t)n, entry_dn_cmp))) {
        dse_node_delete(&deleted_node);
    }
    dse_node_delete(&n);

    if (!dont_write_file) {
        /* Decrement the numsubordinate count of the parent entry */
        dse_updateNumSubOfParent(pdse, slapi_entry_get_sdn_const(e),
                                 SLAPI_OPERATION_DELETE);
        dse_write_file_nolock(pdse);
    }
    dse_lock_unlock(pdse, DSE_USE_LOCK);

    return 1;
}


/*
 * Returns a SLAPI_BIND_xxx retun code.
 */
int
dse_bind(Slapi_PBlock *pb) /* JCM There should only be one exit point from this function! */
{
    ber_tag_t method;    /* The bind method */
    struct berval *cred; /* The bind credentials */
    Slapi_Value **bvals;
    struct dse *pdse;
    Slapi_Attr *attr;
    Slapi_DN *sdn = NULL;
    Slapi_Entry *ec = NULL;

    /*Get the parameters*/
    if (slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &pdse) < 0 ||
        slapi_pblock_get(pb, SLAPI_BIND_TARGET_SDN, &sdn) < 0 ||
        slapi_pblock_get(pb, SLAPI_BIND_METHOD, &method) < 0 ||
        slapi_pblock_get(pb, SLAPI_BIND_CREDENTIALS, &cred) < 0) {
        slapi_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL);
        return SLAPI_BIND_FAIL;
    }

    /* always allow noauth simple binds */
    if (method == LDAP_AUTH_SIMPLE && cred->bv_len == 0) {
        /*
         * report success to client, but return
         * SLAPI_BIND_FAIL so we don't
         * authorize based on noauth credentials
         */
        slapi_send_ldap_result(pb, LDAP_SUCCESS, NULL, NULL, 0, NULL);
        return (SLAPI_BIND_FAIL);
    }

    ec = dse_get_entry_copy(pdse, sdn, DSE_USE_LOCK);
    if (ec == NULL) {
        slapi_pblock_set(pb, SLAPI_PB_RESULT_TEXT, "Entry does not exist");
        slapi_send_ldap_result(pb, LDAP_INVALID_CREDENTIALS, NULL, NULL, 0, NULL);
        return (SLAPI_BIND_FAIL);
    }

    switch (method) {
    case LDAP_AUTH_SIMPLE: {
        Slapi_Value cv;
        if (slapi_entry_attr_find(ec, "userpassword", &attr) != 0) {
            slapi_pblock_set(pb, SLAPI_PB_RESULT_TEXT, "Entry does not have userpassword set");
            slapi_send_ldap_result(pb, LDAP_INVALID_CREDENTIALS, NULL, NULL, 0, NULL);
            slapi_entry_free(ec);
            return SLAPI_BIND_FAIL;
        }
        bvals = attr_get_present_values(attr);

        slapi_value_init_berval(&cv, cred);
        if (slapi_pw_find_sv(bvals, &cv) != 0) {
            slapi_pblock_set(pb, SLAPI_PB_RESULT_TEXT, "Invalid credentials");
            slapi_send_ldap_result(pb, LDAP_INVALID_CREDENTIALS, NULL, NULL, 0, NULL);
            slapi_entry_free(ec);
            value_done(&cv);
            return SLAPI_BIND_FAIL;
        }
        value_done(&cv);
    } break;

    default:
        slapi_send_ldap_result(pb, LDAP_STRONG_AUTH_NOT_SUPPORTED, NULL, "auth method not supported", 0, NULL);
        slapi_entry_free(ec);
        return SLAPI_BIND_FAIL;
    }
    slapi_entry_free(ec);
    /* success:  front end will send result */
    return SLAPI_BIND_SUCCESS;
}

int
dse_unbind(Slapi_PBlock *pb __attribute__((unused)))
{
    return 0;
}

/*
 * This structure is simply to pass parameters to dse_search_filter_entry.
 */
struct magicSearchStuff
{
    Slapi_PBlock *pb;
    struct dse *pdse;
    int scope;
    const Slapi_DN *basedn;
    Slapi_Filter *filter;
    int nentries;
    char **attrs;       /*Attributes*/
    int attrsonly;      /*Should we just return the attributes found?*/
    dse_search_set *ss; /* for the temporary results - to pass to the dse search callbacks */
};

/*
 * The function which is called on each node of the AVL tree.
 */
static int
dse_search_filter_entry(caddr_t data, caddr_t arg)
{
    struct dse_node *n = (struct dse_node *)data;
    struct magicSearchStuff *p = (struct magicSearchStuff *)arg;
    if (slapi_sdn_scope_test(slapi_entry_get_sdn_const(n->entry), p->basedn, p->scope)) {
        if (slapi_vattr_filter_test(p->pb, n->entry, p->filter, 1 /* verify access */) == 0) {
            Slapi_Entry *ec = slapi_entry_dup(n->entry);
            p->nentries++;
            if (!p->ss) {
                p->ss = dse_search_set_new();
            }
            dse_search_set_add_entry(p->ss, ec); /* consumes the entry */
        } else {
            /*
            slapd_log_error_proc("dse_search_filter_entry",
                                 "filter test failed: dn %s did not match filter %d\n",
                                 slapi_entry_get_dn_const(n->entry), p->filter->f_choice);
*/
        }
    } else {
        /*
        slapd_log_error_proc("dse_search_filter_entry",
                             "scope test failed: dn %s is not in scope %d of dn [%s]\n",
                             slapi_entry_get_dn_const(n->entry), p->scope,
                             slapi_sdn_get_dn(p->basedn));
*/
    }
    return 0;
}

/*
 * The function which kicks off the traversal of the AVL tree.
 * Returns the number of entries returned.
 */
/* jcm: Not very efficient if there are many DSE entries. */
/* jcm: It applies the filter to every node in the tree regardless */
static int
do_dse_search(struct dse *pdse, Slapi_PBlock *pb, int scope, const Slapi_DN *basedn, Slapi_Filter *filter, char **attrs, int attrsonly)
{
    struct magicSearchStuff stuff;
    stuff.pb = pb;
    stuff.pdse = pdse;
    stuff.scope = scope;
    stuff.basedn = basedn;
    stuff.filter = filter;
    stuff.nentries = 0;
    stuff.attrs = attrs;
    stuff.attrsonly = attrsonly;
    stuff.ss = NULL;
    Operation *pb_op = NULL;
    slapi_pblock_get(pb, SLAPI_OPERATION, &pb_op);

    /*
     * If this is a persistent search and the client is only interested in
     * entries that change, we skip looking through the DSE entries.
     */
    if (pb_op == NULL || !operation_is_flag_set(pb_op, OP_FLAG_PS_CHANGESONLY)) {
        dse_lock_read(pdse, DSE_USE_LOCK);
        dse_apply_nolock(pdse, dse_search_filter_entry, (caddr_t)&stuff);
        dse_lock_unlock(pdse, DSE_USE_LOCK);
    }

    if (stuff.ss) /* something was found which matched our criteria */
    {
        Slapi_Entry *e = NULL;
        for (e = dse_search_set_get_next_entry(stuff.ss);
             e;
             e = dse_search_set_get_next_entry(stuff.ss)) {
            int returncode = 0;
            char returntext[SLAPI_DSE_RETURNTEXT_SIZE] = "";

            if (dse_call_callback(pdse, pb, SLAPI_OPERATION_SEARCH,
                                  DSE_FLAG_PREOP, e, NULL, &returncode, returntext) == SLAPI_DSE_CALLBACK_OK) {
                dse_search_set *ss = NULL;
                slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_SET, &ss);
                /* if this is the first entry - allocate dse_search_set structure */
                if (ss == NULL) {
                    ss = dse_search_set_new();
                    slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, ss);
                }
                /* make another reference to e (stuff.ss references it too)
                   the stuff.ss reference is removed by dse_search_set_clean()
                   below, leaving ss as the sole owner of the memory */
                dse_search_set_add_entry(ss, e);
            } else {
                stuff.nentries--; /* rejected entry */
                /* this leaves a freed pointer in stuff.ss, but that's ok because
                   it should never be referenced, and the reference is removed by
                   the call to dse_search_set_clean() below */
                slapi_entry_free(e);
            }
        }
        dse_search_set_clean(stuff.ss);
    }

    /* the pblock ss now contains the "real" search result set and the copies of
       the entries allocated in dse_search_filter_entry; any entries rejected by
       the search callback were freed above by the call to slapi_entry_free() */
    return stuff.nentries;
}

/*
 * -1 means something went wrong.
 * 0 means everything went ok.
 */
int
dse_search(Slapi_PBlock *pb) /* JCM There should only be one exit point from this function! */
{
    Slapi_Filter *filter;
    Slapi_DN *basesdn = NULL;
    struct dse *pdse;
    char **attrs;
    const char *ndn = NULL;
    char returntext[SLAPI_DSE_RETURNTEXT_SIZE] = "";
    int attrsonly;
    int estimate = 0; /* estimated search result set size */
    int isrootdse = 0;
    int returncode = LDAP_SUCCESS;
    int scope;

    /*
     * Get private information created in the init routine.
     * Also get the parameters of the search operation. These come
     * more or less directly from the client.
     */
    if (slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &pdse) < 0 ||
        slapi_pblock_get(pb, SLAPI_SEARCH_TARGET_SDN, &basesdn) < 0 ||
        slapi_pblock_get(pb, SLAPI_SEARCH_SCOPE, &scope) < 0 ||
        slapi_pblock_get(pb, SLAPI_SEARCH_FILTER, &filter) < 0 ||
        slapi_pblock_get(pb, SLAPI_SEARCH_ATTRS, &attrs) < 0 ||
        slapi_pblock_get(pb, SLAPI_SEARCH_ATTRSONLY, &attrsonly) < 0) {
        slapi_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL);
        return (-1);
    }
    /*
     * Sadly the root dse is still a special case.  We must not allow
     * acl checks on it, or allow onelevel or subtree searches on it.
     */
    isrootdse = slapi_sdn_isempty(basesdn);

    /* Hopefully this plugin DN mapping can be removed in 3.x */
    ndn = slapi_sdn_get_ndn(basesdn);
    if (strstr(ndn, "aster replication plugin,cn=plugins,cn=config")) {
        /* Map the old "problematic" name to the new one */
        slapi_sdn_free(&basesdn);
        basesdn = slapi_sdn_new_dn_byval("cn=Multisupplier Replication Plugin,cn=plugins,cn=config");
        slapi_pblock_set(pb, SLAPI_SEARCH_TARGET_SDN, basesdn);
    }

    /*
     * Now optimise the filter for use: note that unlike ldbm_search,
     * because we don't change the outer filter container, we don't need
     * to set back into pb.
     */
    slapi_filter_optimise(filter);

    switch (scope) {
    case LDAP_SCOPE_BASE: {
        Slapi_Entry *baseentry = NULL;
        baseentry = dse_get_entry_copy(pdse, basesdn, DSE_USE_LOCK);
        if (baseentry == NULL) {
            slapi_send_ldap_result(pb, LDAP_NO_SUCH_OBJECT, NULL, NULL, 0, NULL);
            slapi_log_err(SLAPI_LOG_PLUGIN, "dse_search", "node %s was not found\n",
                          slapi_sdn_get_dn(basesdn));
            return -1;
        }
        /*
         * We don't want to do an acl check for the root dse... because the acl
         * code thinks it's a suffix of every target... so every acl applies to
         * the root dse... which is wrong.
         */
        if (slapi_vattr_filter_test(pb, baseentry, filter, !isrootdse /* verify access */) == 0) {
            /* Callbacks modify a copy of the entry */
            if (dse_call_callback(pdse, pb, SLAPI_OPERATION_SEARCH,
                                  DSE_FLAG_PREOP, baseentry, NULL, &returncode, returntext) == SLAPI_DSE_CALLBACK_OK) {
                dse_search_set *ss;
                ss = dse_search_set_new();
                slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, ss);
                dse_search_set_add_entry(ss, baseentry); /* consumes the entry */
                baseentry = NULL;
                estimate = 1; /* scope base */
            }
        }
        slapi_entry_free(baseentry);
    } break;
    case LDAP_SCOPE_ONELEVEL:
    /* FALL THROUGH */
    case LDAP_SCOPE_SUBTREE:
        if (!isrootdse) {
            estimate = do_dse_search(pdse, pb, scope, basesdn, filter, attrs, attrsonly);
        }
        break;
    }
    slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET_SIZE_ESTIMATE, &estimate);

    /* Search is done, send LDAP_SUCCESS */
    return 0;
}

int32_t
dse_compare(Slapi_PBlock *pb)
{
    /*
     * Inspired largely by ldbm_compare.c. Allow schema aware comparison
     * of entries in the DSE, including cn=config.
     */
    backend *be = NULL;
    char *type = NULL;
    struct berval *bval = NULL;
    Slapi_DN *sdn = NULL;
    struct dse *pdse = NULL;
    Slapi_Entry *ec = NULL;
    Slapi_Value compare_value = {0};

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &pdse);
    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
    slapi_pblock_get(pb, SLAPI_COMPARE_TYPE, &type);
    slapi_pblock_get(pb, SLAPI_COMPARE_VALUE, &bval);

    /* get the entry */
    ec = dse_get_entry_copy(pdse, sdn, DSE_USE_LOCK);
    if (ec == NULL) {
        slapi_send_ldap_result(pb, LDAP_NO_SUCH_OBJECT, NULL, NULL, 0, NULL);
        return -1;
    }

    /* Access control check */
    int32_t err = slapi_access_allowed(pb, ec, type, bval, SLAPI_ACL_COMPARE);
    if (err != LDAP_SUCCESS) {
        slapi_entry_free(ec);
        slapi_send_ldap_result(pb, err, NULL, NULL, 0, NULL);
        return -1;
    }

    /* If cn=config, setup the entry with ALL values we could check from defaults */
    Slapi_DN config_dn;
    slapi_sdn_init_ndn_byref(&config_dn, "cn=config");
    if (slapi_sdn_compare(&config_dn, sdn) == 0) {
        read_config_dse(pb, ec, NULL, &err, NULL, NULL);
        /*
         * read_config_dse, and in turn, config_set_entry always returns
         * a 1 here, which is probably dse_callback related.
         */
        if (err != 1) {
            slapi_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL);
            return -1;
        }
        /*
         * cn=config is now populated
         */
    }
    slapi_sdn_done(&config_dn);

    /* Do the schema aware check. */
    slapi_value_init_berval(&compare_value, bval);

    int32_t result = 0;
    err = slapi_vattr_value_compare(ec, type, &compare_value, &result, 0);

    /* We have the results, now free and then send. */
    slapi_entry_free(ec);
    value_done(&compare_value);

    /* Format the result as expected. */
    if (err != LDAP_SUCCESS) {
        if (SLAPI_VIRTUALATTRS_NOT_FOUND == err) {
            slapi_send_ldap_result(pb, LDAP_NO_SUCH_ATTRIBUTE, NULL, NULL, 0, NULL);
        } else {
            /* Some other problem, call it an operations error */
            slapi_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL);
        }
        return -1;
    } else {
        if (result != 0) {
            slapi_send_ldap_result(pb, LDAP_COMPARE_TRUE, NULL, NULL, 0, NULL);
        } else {
            slapi_send_ldap_result(pb, LDAP_COMPARE_FALSE, NULL, NULL, 0, NULL);
        }
    }
    return 0;
}

/*
 * -1 means something went wrong.
 * 0 means everything went ok.
 */

static int
dse_modify_return(int rv, Slapi_Entry *ec, Slapi_Entry *ecc)
{
    slapi_entry_free(ec);
    slapi_entry_free(ecc);
    return rv;
}

int
dse_modify(Slapi_PBlock *pb) /* JCM There should only be one exit point from this function! */
{
    int err;             /*House keeping stuff*/
    LDAPMod **mods;      /*Used to apply the modifications*/
    LDAPMod **original_mods = NULL; /* some mods can be removed by callback, save them for later logging */
    char *errbuf = NULL; /* To get error back */
    struct dse *pdse;
    Slapi_Entry *ec = NULL;
    Slapi_Entry *ecc = NULL;
    int returncode = LDAP_SUCCESS;
    char returntext[SLAPI_DSE_RETURNTEXT_SIZE] = "";
    Slapi_DN *sdn = NULL;
    Slapi_DN *old_repl_sdn = NULL;
    int dont_write_file = 0; /* default */
    int rc = SLAPI_DSE_CALLBACK_DO_NOT_APPLY;
    int retval = -1;
    int need_be_postop = 0;
    int plugin_started = 0;
    int internal_op = 0;
    int fixup_op = 0;
    PRBool global_lock_owned = PR_FALSE;
    Operation *pb_op = NULL;

    PR_ASSERT(pb);
    if (slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &pdse) < 0 ||
        /* slapi_pblock_get( pb, SLAPI_MODIFY_TARGET, &dn ) < 0 || */
        slapi_pblock_get(pb, SLAPI_MODIFY_TARGET_SDN, &sdn) < 0 ||
        slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods) < 0 || (NULL == pdse)) {
        returncode = LDAP_OPERATIONS_ERROR;
        goto done;
    }

    slapi_pblock_get(pb, SLAPI_DSE_DONT_WRITE_WHEN_ADDING, &dont_write_file);
    if (!dont_write_file && dse_check_for_readonly_error(pb, pdse)) {
        /* already returned result */
        return retval;
    }

    /* Hopefully this plugin DN mapping can be removed in 3.x */
    old_repl_sdn = slapi_sdn_new_dn_byval("cn=Multimaster Replication Plugin,cn=plugins,cn=config");
    if(slapi_sdn_compare(sdn, old_repl_sdn) == 0) {
        /* Map the old name to the new one */
        slapi_sdn_free(&sdn);
        sdn = slapi_sdn_new_dn_byval("cn=Multisupplier Replication Plugin,cn=plugins,cn=config");
        slapi_pblock_set(pb, SLAPI_MODIFY_TARGET_SDN, sdn);
    }
    slapi_sdn_free(&old_repl_sdn);

    slapi_pblock_get(pb, SLAPI_OPERATION, &pb_op);
    if (pb_op){
        internal_op = operation_is_flag_set(pb_op, OP_FLAG_INTERNAL);
        fixup_op = operation_is_flag_set(pb_op, SLAPI_OP_FLAG_FIXUP);
    }
    /* Find the entry we are about to modify. */
    ec = dse_get_entry_copy(pdse, sdn, DSE_USE_LOCK);
    if (ec == NULL) {
        returncode = LDAP_NO_SUCH_OBJECT;
        goto done;
    }

    /* Check acl */
    err = plugin_call_acl_mods_access(pb, ec, mods, &errbuf);
    if (err != LDAP_SUCCESS) {
        returncode = err;
        if (errbuf) {
            PL_strncpyz(returntext, errbuf, sizeof(returntext));
            slapi_ch_free_string(&errbuf);
        }
        goto done;
    }

    /* Save away a copy of the entry, before modifications */
    slapi_pblock_set(pb, SLAPI_ENTRY_PRE_OP, slapi_entry_dup(ec)); /* JCM - When does this get free'd? */
    /* richm - it is freed in modify.c */

    /* Modify a copy of the entry*/
    ecc = slapi_entry_dup(ec);
    err = entry_apply_mods(ecc, mods);

    /* Possibly acquire the global backend lock */
    if (global_backend_lock_requested()) {
        global_backend_lock_lock();
        global_lock_owned = PR_TRUE;
    }
    original_mods = copy_mods(mods);

    /* XXXmcs: should we expand objectclass values here?? */
    /* give the dse callbacks the first crack at the modify */
    rc = dse_call_callback(pdse, pb, SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, ec, ecc, &returncode, returntext);
    if (SLAPI_DSE_CALLBACK_OK == rc) {
        int plugin_rc;

        /* next, give the be plugins a crack at it */
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &returncode);
        slapi_pblock_set(pb, SLAPI_MODIFY_EXISTING_ENTRY, ecc);
        plugin_rc = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_PRE_MODIFY_FN);
        need_be_postop = 1; /* if the be preops were called, have to call the be postops too */
        if (!returncode) {
            slapi_pblock_get(pb, SLAPI_RESULT_CODE, &returncode);
        }
        if (!plugin_rc && !returncode) {
            /* finally, give the betxn plugins a crack at it */
            plugin_rc = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_PRE_MODIFY_FN);
            if (!returncode) {
                slapi_pblock_get(pb, SLAPI_RESULT_CODE, &returncode);
            }
            if (plugin_rc || returncode) {
                slapi_log_err(SLAPI_DSE_TRACELEVEL,
                              "dse_modify", "SLAPI_PLUGIN_BE_TXN_PRE_MODIFY_FN failed - rc %d LDAP error %d:%s\n",
                              plugin_rc, returncode, ldap_err2string(returncode));
            }
        } else {
            slapi_log_err(SLAPI_DSE_TRACELEVEL,
                          "dse_modify", "SLAPI_PLUGIN_BE_PRE_MODIFY_FN failed - rc %d LDAP error %d:%s\n",
                          rc, returncode, ldap_err2string(returncode));
        }
        if (plugin_rc || returncode) {
            char *ldap_result_message = NULL;
            rc = SLAPI_DSE_CALLBACK_ERROR;
            if (!returncode) {
                slapi_log_err(SLAPI_DSE_TRACELEVEL,
                              "dse_modify", "PRE_MODIFY plugin returned non-zero but did not set an LDAP error\n");
                returncode = LDAP_OPERATIONS_ERROR;
            }
            if (!returntext[0]) {
                slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
                if (ldap_result_message && ldap_result_message[0]) {
                    PL_strncpyz(returntext, ldap_result_message, sizeof(returntext));
                }
            }
        } else {
            /*
             * If we are using dynamic plugins, and we are modifying a plugin
             * we need to do some additional checks.  First, check if we are
             * enabling/disabling a plugin.  Then make sure the plugin still
             * starts after applying the plugin changes.
             */
            rc = SLAPI_DSE_CALLBACK_OK;
            if (slapi_entry_attr_hasvalue(ec, SLAPI_ATTR_OBJECTCLASS, "nsSlapdPlugin")) {
                if (config_get_dynamic_plugins() || fixup_op) {
                    if ((plugin_started = dse_modify_plugin(ec, ecc, returntext)) == -1) {
                        returncode = LDAP_UNWILLING_TO_PERFORM;
                        rc = SLAPI_DSE_CALLBACK_ERROR;
                        retval = -1;
                        goto done;
                    }
                    /*
                     * If this is not a internal operation, make sure the plugin
                     * can be restarted.
                     */
                    if (!internal_op) {
                        if (dse_pre_modify_plugin(ec, ecc, mods)) {
                            char *errtext;
                            slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &errtext);
                            if (errtext) {
                                PL_strncpyz(returntext,
                                            "Failed to apply plugin config change, "
                                            "check the errors log for more info.",
                                            sizeof(returntext));
                            }
                            returncode = LDAP_UNWILLING_TO_PERFORM;
                            rc = SLAPI_DSE_CALLBACK_ERROR;
                            retval = -1;
                            goto done;
                        }
                    }
                } else {
                    slapi_log_err(SLAPI_LOG_NOTICE, "dse_modify",
                            "A plugin has been enabled or disabled, "
                            "but nsslapd-dynamic-plugins is off. A server restart is required to change this plugin state.\n");
                } /* end config_get_dynamic_plugins */
            } /* end has nsSlapdPlugin */
        }
    }

    switch (rc) {
    case SLAPI_DSE_CALLBACK_ERROR:
        /* Error occured in the callback -- return error code from callback */
        goto done;
        break;
    case SLAPI_DSE_CALLBACK_DO_NOT_APPLY:
        /* Callback says don't apply the changes -- return Success */
        returncode = LDAP_SUCCESS;
        returntext[0] = '\0';
        retval = 0;
        goto done;
        break;
    case SLAPI_DSE_CALLBACK_OK: {
        /* The callback may alter the mods in the pblock.  This happens
           for example in the schema code.  Since the schema attributes
           are managed exclusively by the schema code, we should not
           apply those mods.  However, for reasons unknown to me, we
           must in the general case call entry_apply_mods before calling
           the modify callback above.  In the case of schema, the schema
           code will remove the schema attributes from the mods.  So, we
           reapply the mods to the entry for the attributes we manage in
           the dse code (e.g. aci)
        */
        int reapply_mods = 0; /* default is to not reapply entry_apply_mods */
        slapi_pblock_get(pb, SLAPI_DSE_REAPPLY_MODS, &reapply_mods);
        /* Callback says apply the changes */
        if (reapply_mods) {
            LDAPMod **modsagain = NULL; /*Used to apply the modifications*/
            slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &modsagain);
            if (NULL != modsagain) {
                /* the dse modify callback must have modified ecc back to it's
                   original state, before the earlier apply_mods, but without the
                   attributes it did not want us to apply mods to */
                err = entry_apply_mods(ecc, modsagain);
            }
        }

        if (err != 0) {
            returncode = err;
            returntext[0] = '\0';
            retval = -1;
            goto done;
        }
        break;
    }
    }

    /* We're applying the mods... check that the entry still obeys the schema */
    if (slapi_entry_schema_check(pb, ecc) != 0) {
        char *errtext;

        slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &errtext);
        if (errtext) {
            PL_strncpyz(returntext, errtext, sizeof(returntext));
        }
        returncode = LDAP_OBJECT_CLASS_VIOLATION;
        retval = -1;
        goto done;
    }

    /* Check if the attribute values in the mods obey the syntaxes */
    if (slapi_mods_syntax_check(pb, mods, 0) != 0) {
        char *errtext;

        slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &errtext);
        if (errtext) {
            PL_strncpyz(returntext, errtext, sizeof(returntext));
        }
        returncode = LDAP_INVALID_SYNTAX;
        retval = -1;
        goto done;
    }

    /* Change the entry itself both on disk and in the AVL tree */
    /* dse_replace_entry free's the existing entry. */
    if (dse_replace_entry(pdse, ecc, !dont_write_file, DSE_USE_LOCK) != 0) {
        returncode = LDAP_OPERATIONS_ERROR;
        retval = -1;
        goto done;
    }
    retval = 0;                                                      /* so far, so good */
    slapi_pblock_set(pb, SLAPI_ENTRY_POST_OP, slapi_entry_dup(ecc)); /* JCM - When does this get free'd? */
    /* richm - it is freed in modify.c */
    /* give the dse callbacks the first crack at the modify */
    rc = dse_call_callback(pdse, pb, SLAPI_OPERATION_MODIFY, DSE_FLAG_POSTOP, ec, ecc, &returncode, returntext);
done:
    if (rc != SLAPI_DSE_CALLBACK_DO_NOT_APPLY) {
        /* make sure OPRETURN is set */
        slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &err);
        if ((retval || returncode) && !err) {
            slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, retval ? &retval : &returncode);
        }
        /* next, give the betxn plugins a crack at it */
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &returncode);
        slapi_pblock_set(pb, SLAPI_MODIFY_EXISTING_ENTRY, ecc);
        if (need_be_postop) {
            plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN);
            if (!returncode) {
                slapi_pblock_get(pb, SLAPI_RESULT_CODE, &returncode);
            }
            if (returncode && !returntext[0]) {
                char *ldap_result_message = NULL;
                slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
                if (ldap_result_message && ldap_result_message[0]) {
                    PL_strncpyz(returntext, ldap_result_message, sizeof(returntext));
                }
            }

            plugin_call_plugins(pb, SLAPI_PLUGIN_BE_POST_MODIFY_FN);
            if (!returncode) {
                slapi_pblock_get(pb, SLAPI_RESULT_CODE, &returncode);
            }
        }
    } else {
        /* It should not happen but just be paranoiac, do not
         * forget to call the postop if needed
         */
        if (need_be_postop) {
            plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN);
            if (!returncode) {
                slapi_pblock_get(pb, SLAPI_RESULT_CODE, &returncode);
            }

            plugin_call_plugins(pb, SLAPI_PLUGIN_BE_POST_MODIFY_FN);
            if (!returncode) {
                slapi_pblock_get(pb, SLAPI_RESULT_CODE, &returncode);
            }
        }
    }

    /* time to restore original mods */
    if (original_mods) {
        LDAPMod **mods_from_callback;
        slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods_from_callback);
        ldap_mods_free(mods_from_callback, 1 /* Free the Array and the Elements */);
        slapi_pblock_set(pb, SLAPI_MODIFY_MODS, original_mods);
    }
    if (global_lock_owned) {
        global_backend_lock_unlock();
    }
    slapi_send_ldap_result(pb, returncode, NULL, returntext[0] ? returntext : NULL, 0, NULL);

    return dse_modify_return(retval, ec, ecc);
}

static int
dse_pre_modify_plugin(Slapi_Entry *entryBefore, Slapi_Entry *entryAfter, LDAPMod **mods)
{
    const char *enabled = NULL;
    int restart_plugin = 1;
    int rc = 0;
    int i;

    /*
     * Only check the mods if the plugin is enabled - no need to restart a plugin if it's not running.
     */
    if ((enabled = slapi_entry_attr_get_ref(entryBefore, ATTR_PLUGIN_ENABLED)) &&
        !strcasecmp(enabled, "on")) {
        for (i = 0; mods && mods[i]; i++) {
            if (strcasecmp(mods[i]->mod_type, ATTR_PLUGIN_ENABLED) == 0) {
                /* we already stop/started the pugin - don't do it again */
                restart_plugin = 0;
                break;
            }
        }
        if (restart_plugin) { /* for all other plugin config changes, restart the plugin */
            if (plugin_restart(entryBefore, entryAfter) != LDAP_SUCCESS) {
                slapi_log_err(SLAPI_LOG_ERR, "dse_pre_modify_plugin",
                              "The configuration change for plugin (%s) could not be applied.\n",
                              slapi_entry_get_dn(entryBefore));
                rc = -1;
            }
        }
    }
    return rc;
}

/*
 * If this is modifying a plugin, check if we are disabling/enabling it - update the
 * global plugins as needed.
 *
 * Return 1 if the plugin was successfully started
 * Return 2 if the plugin was successfully stopped
 * Return -1 on error
 * Return 0 if nothing was done
 */
static int
dse_modify_plugin(Slapi_Entry *pre_entry, Slapi_Entry *post_entry, char *returntext)
{
    int rc = LDAP_SUCCESS;

    if (slapi_entry_attr_hasvalue(pre_entry, "nsslapd-pluginEnabled", "on") &&
        slapi_entry_attr_hasvalue(post_entry, "nsslapd-pluginEnabled", "off")) {
        /*
         * Plugin has been disabled
         */
        if (plugin_delete(post_entry, returntext, 0 /* not locked */)) {
            rc = -1;
        } else {
            rc = 2; /* plugin disabled */
            slapi_log_err(SLAPI_LOG_PLUGIN, "dse_modify_plugin", "Disabled plugin (%s)\n",
                          slapi_entry_get_dn(post_entry));
        }
    } else if (slapi_entry_attr_hasvalue(pre_entry, "nsslapd-pluginEnabled", "off") &&
               slapi_entry_attr_hasvalue(post_entry, "nsslapd-pluginEnabled", "on")) {
        /*
         * Plugin has been enabled
         */
        if (plugin_add(post_entry, returntext, 0 /* not locked */)) {
            rc = -1;
        } else {
            rc = 1; /* plugin started */
            slapi_log_err(SLAPI_LOG_PLUGIN, "dse_modify_plugin", "Enabled plugin (%s)\n",
                          slapi_entry_get_dn(post_entry));
        }
    }

    return rc;
}

/*
 * Add the plugin to the global plugin list
 */
static int
dse_add_plugin(Slapi_Entry *entry, char *returntext)
{
    int rc = LDAP_SUCCESS;

    if (!slapi_entry_attr_hasvalue(entry, SLAPI_ATTR_OBJECTCLASS, "nsSlapdPlugin") ||
        !config_get_dynamic_plugins()) {
        /*
         * This is not a plugin, or we are not allowing dynamic updates.
         */
        return rc;
    }
    rc = plugin_add(entry, returntext, 0 /* not locked */);

    return rc;
}

/*
 * Delete the plugin from the global plugin list
 */
static int
dse_delete_plugin(Slapi_Entry *entry, char *returntext)
{
    int rc = LDAP_SUCCESS;

    if (!slapi_entry_attr_hasvalue(entry, SLAPI_ATTR_OBJECTCLASS, "nsSlapdPlugin") ||
        slapi_entry_attr_hasvalue(entry, "nsslapd-PluginEnabled", "off") ||
        !config_get_dynamic_plugins()) {
        /*
         * This is not a plugin, this plugin was not enabled to begin with, or we
         * are not allowing dynamic updates .
         */
        return rc;
    }
    rc = plugin_delete(entry, returntext, 0 /* not locked */);

    return rc;
}

static int
dse_add_return(int rv, Slapi_Entry *e)
{
    slapi_entry_free(e);
    return rv;
}

/*
 * -1 means something went wrong.
 * 0 means everything went ok.
 */
int
dse_add(Slapi_PBlock *pb) /* JCM There should only be one exit point from this function! */
{
    Slapi_Entry *e = NULL;      /*The new entry to add*/
    Slapi_Entry *e_copy = NULL; /* copy of added entry */
    char *errbuf = NULL;
    int rc = LDAP_SUCCESS;
    int error = -1;
    int dont_write_file = 0; /* default */
    struct dse *pdse;
    int returncode = LDAP_SUCCESS;
    char returntext[SLAPI_DSE_RETURNTEXT_SIZE] = "";
    Slapi_DN *sdn = NULL;
    Slapi_DN parent;
    int need_be_postop = 0;
    PRBool global_lock_owned = PR_FALSE;

    /*
     * Get the database, the dn and the entry to add
     */
    if (slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &pdse) < 0 ||
        slapi_pblock_get(pb, SLAPI_ADD_TARGET_SDN, &sdn) < 0 ||
        slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e) < 0 || (NULL == pdse)) {
        rc = LDAP_OPERATIONS_ERROR;
        goto done;
    }

    slapi_pblock_get(pb, SLAPI_DSE_DONT_WRITE_WHEN_ADDING, &dont_write_file);
    if (!dont_write_file && dse_check_for_readonly_error(pb, pdse)) {
        return (error); /* result already sent */
    }

    /*
     * Check to make sure the entry passes the schema check
     */
    if (slapi_entry_schema_check(pb, e) != 0) {
        char *errtext;
        slapi_log_err(SLAPI_DSE_TRACELEVEL,
                      "dse_add", "entry failed schema check\n");
        slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &errtext);
        if (errtext && errtext[0]) {
            PL_strncpyz(returntext, errtext, sizeof(returntext));
        }
        rc = LDAP_OBJECT_CLASS_VIOLATION;
        e = NULL; /* caller will free upon error */
        goto done;
    }

    /* Check if the attribute values in the entry obey the syntaxes */
    if (slapi_entry_syntax_check(pb, e, 0) != 0) {
        char *errtext;
        slapi_log_err(SLAPI_DSE_TRACELEVEL,
                      "dse_add", "entry failed syntax check\n");
        slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &errtext);
        if (errtext && errtext[0]) {
            PL_strncpyz(returntext, errtext, sizeof(returntext));
        }
        rc = LDAP_INVALID_SYNTAX;
        e = NULL; /* caller will free upon error */
        goto done;
    }

    /*
     * Attempt to find this dn.
     */
    {
        Slapi_Entry *existingentry = dse_get_entry_copy(pdse, sdn, DSE_USE_LOCK);
        if (existingentry != NULL) {
            /*
             * If we've reached this code, there is an entry
             * whose dn matches dn, so tell the user and return
             */
            slapi_entry_free(existingentry);
            rc = LDAP_ALREADY_EXISTS;
            e = NULL; /* caller will free upon error */
            goto done;
        }
    }

    /*
     * Get the parent dn and see if the corresponding entry exists.
     * If the parent does not exist, only allow the "root" user to
     * add the entry.
     */
    slapi_sdn_init(&parent);
    slapi_sdn_get_parent(sdn, &parent);
    if (!slapi_sdn_isempty(&parent)) {
        Slapi_Entry *parententry = NULL;
        parententry = dse_get_entry_copy(pdse, &parent, DSE_USE_LOCK);
        if (parententry == NULL) {
            rc = LDAP_NO_SUCH_OBJECT;
            slapi_log_err(SLAPI_DSE_TRACELEVEL, " dse_add", "Narent does not exist\n");
            slapi_sdn_done(&parent);
            e = NULL; /* caller will free upon error */
            goto done;
        }
        rc = plugin_call_acl_plugin(pb, parententry, NULL, NULL, SLAPI_ACL_ADD, ACLPLUGIN_ACCESS_DEFAULT, &errbuf);
        slapi_entry_free(parententry);
        if (rc != LDAP_SUCCESS) {
            slapi_log_err(SLAPI_DSE_TRACELEVEL, "dse_add", "No access to parent\n");
            if (errbuf && errbuf[0]) {
                PL_strncpyz(returntext, errbuf, sizeof(returntext));
            }
            slapi_ch_free_string(&errbuf);
            slapi_sdn_done(&parent);
            e = NULL; /* caller will free upon error */
            goto done;
        }
    } else {
        /* no parent */
        int isroot;
        slapi_pblock_get(pb, SLAPI_REQUESTOR_ISROOT, &isroot);
        if (!isroot) {
            slapi_log_err(SLAPI_DSE_TRACELEVEL, "dse_add", "No parent and not root\n");
            rc = LDAP_INSUFFICIENT_ACCESS;
            slapi_sdn_done(&parent);
            e = NULL; /* caller will free upon error */
            goto done;
        }
    }
    slapi_sdn_done(&parent);

    /*
     * Before we add the entry, find out if the syntax of the aci
     * aci attribute values are correct or not. We don't want to add
     * the entry if the syntax is incorrect.
     */
    if (plugin_call_acl_verify_syntax(pb, e, &errbuf) != 0) {
        if (errbuf && errbuf[0]) {
            PL_strncpyz(returntext, errbuf, sizeof(returntext));
            slapi_ch_free_string(&errbuf);
        }
        rc = LDAP_INVALID_SYNTAX;
        e = NULL; /* caller will free upon error */
        goto done;
    }

    /* Possibly acquire the global backend lock */
    if (global_backend_lock_requested()) {
        global_backend_lock_lock();
        global_lock_owned = PR_TRUE;
    }

    if (dse_call_callback(pdse, pb, SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, e,
                          NULL, &returncode, returntext) != SLAPI_DSE_CALLBACK_OK) {
        if (!returncode) {
            slapi_log_err(SLAPI_LOG_ERR, "dse_add",
                          "DSE PREOP callback returned error but did not set returncode\n");
            returncode = LDAP_OPERATIONS_ERROR;
        }
        rc = returncode;
        e = NULL; /* caller will free upon error */
        goto done;
    }
    /* next, give the be plugins a crack at it */
    slapi_pblock_set(pb, SLAPI_RESULT_CODE, &returncode);
    plugin_call_plugins(pb, SLAPI_PLUGIN_BE_PRE_ADD_FN);
    need_be_postop = 1; /* have to call be postops now */
    if (!returncode) {
        slapi_pblock_get(pb, SLAPI_RESULT_CODE, &returncode);
    }
    if (!returncode) {
        /* finally, give the betxn plugins a crack at it */
        plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN);
        if (!returncode) {
            slapi_pblock_get(pb, SLAPI_RESULT_CODE, &returncode);
        }
    }
    if (returncode) {
        if (!returntext[0]) {
            char *ldap_result_message = NULL;
            slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
            if (ldap_result_message && ldap_result_message[0]) {
                PL_strncpyz(returntext, ldap_result_message, sizeof(returntext));
            }
        }
        rc = returncode;
        e = NULL; /* caller will free upon error */
        goto done;
    }
    /*
     * Check if we are adding a plugin
     */
    if (dse_add_plugin(e, returntext)) {
        returncode = LDAP_UNWILLING_TO_PERFORM;
        goto done;
    }

    /* make copy for postop fns because add_entry_pb consumes the given entry */
    e_copy = slapi_entry_dup(e);
    if (dse_add_entry_pb(pdse, e_copy, pb) != 0) {
        rc = LDAP_OPERATIONS_ERROR;
        e = NULL; /* caller will free upon error */
        goto done;
    }
    /* The postop must be called after the write lock is released. */
    dse_call_callback(pdse, pb, SLAPI_OPERATION_ADD, DSE_FLAG_POSTOP, e, NULL, &returncode, returntext);
done:
    if (e) {
        slapi_pblock_set(pb, SLAPI_ENTRY_POST_OP, slapi_entry_dup(e));
    }

    /* make sure OPRETURN and RESULT_CODE are set */
    slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &error);
    if (rc || returncode) {
        if (!error) {
            slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, rc ? &rc : &returncode);
        }
        if (!returncode) {
            returncode = rc;
        }
    }
    if (need_be_postop) {
        /* next, give the be txn plugins a crack at it */
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &returncode);
        plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_POST_ADD_FN);

        /* finally, give the be plugins a crack at it */
        plugin_call_plugins(pb, SLAPI_PLUGIN_BE_POST_ADD_FN);
        if (!returncode) {
            slapi_pblock_get(pb, SLAPI_RESULT_CODE, &returncode);
        }
    }
    if (global_lock_owned) {
        global_backend_lock_unlock();
    }
    slapi_send_ldap_result(pb, returncode, NULL, returntext[0] ? returntext : NULL, 0, NULL);
    return dse_add_return(rc, e);
}

/*
 * -1 means something went wrong.
 * 0 means everything went ok.
 */

static int
dse_delete_return(int rv, Slapi_Entry *ec)
{
    slapi_entry_free(ec);
    return rv;
}

int
dse_delete(Slapi_PBlock *pb) /* JCM There should only be one exit point from this function! */
{
    int rc = -1;
    int dont_write_file = 0; /* default */
    struct dse *pdse = NULL;
    int returncode = LDAP_SUCCESS;
    char returntext[SLAPI_DSE_RETURNTEXT_SIZE] = "";
    char *entry_str = "entry";
    char *errbuf = NULL;
    char *attrs[2] = {NULL, NULL};
    Slapi_DN *sdn = NULL;
    Slapi_Entry *ec = NULL; /* copy of entry to delete */
    Slapi_Entry *orig_entry = NULL;
    int need_be_postop = 0;
    PRBool global_lock_owned = PR_FALSE;

    /*
     * Get the database and the dn
     */
    if (slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &pdse) < 0 ||
        slapi_pblock_get(pb, SLAPI_DELETE_TARGET_SDN, &sdn) < 0 ||
        (pdse == NULL)) {
        returncode = LDAP_OPERATIONS_ERROR;
        goto done;
    }

    slapi_pblock_get(pb, SLAPI_DSE_DONT_WRITE_WHEN_ADDING, &dont_write_file);
    if (!dont_write_file && dse_check_for_readonly_error(pb, pdse)) {
        return (rc); /* result already sent */
    }

    ec = dse_get_entry_copy(pdse, sdn, DSE_USE_LOCK);
    if (ec == NULL) {
        returncode = LDAP_NO_SUCH_OBJECT;
        goto done;
    }

    /*
     * Check if this node has any children.
     */
    if (dse_numsubordinates(ec) > 0) {
        returncode = LDAP_NOT_ALLOWED_ON_NONLEAF;
        goto done;
    }

    /*
     * Check the access
     */
    attrs[0] = entry_str;
    attrs[1] = NULL;
    returncode = plugin_call_acl_plugin(pb, ec, attrs, NULL, SLAPI_ACL_DELETE, ACLPLUGIN_ACCESS_DEFAULT, &errbuf);
    if (returncode != LDAP_SUCCESS) {
        if (errbuf && errbuf[0]) {
            PL_strncpyz(returntext, errbuf, sizeof(returntext));
        }
        slapi_ch_free_string(&errbuf);
        goto done;
    }

    /* Possibly acquire the global backend lock */
    if (global_backend_lock_requested()) {
        global_backend_lock_lock();
        global_lock_owned = PR_TRUE;
    }

    if (dse_call_callback(pdse, pb, SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, ec, NULL, &returncode, returntext) == SLAPI_DSE_CALLBACK_OK) {
        slapi_pblock_set(pb, SLAPI_DELETE_BEPREOP_ENTRY, ec);
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &returncode);
        plugin_call_plugins(pb, SLAPI_PLUGIN_BE_PRE_DELETE_FN);
        need_be_postop = 1;
        if (!returncode) {
            slapi_pblock_get(pb, SLAPI_RESULT_CODE, &returncode);
        }
        if (!returncode) {
            plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN);
            if (!returncode) {
                slapi_pblock_get(pb, SLAPI_RESULT_CODE, &returncode);
            }
            if (!returncode) {
                if (dse_delete_entry(pdse, pb, ec) == 0) {
                    returncode = LDAP_OPERATIONS_ERROR;
                }
            }
        }
        /* Setting SLAPI_ENTRY_PRE_OP here,
         * since some betxn postop may need the pre op entry. */
        slapi_pblock_set(pb, SLAPI_ENTRY_PRE_OP, slapi_entry_dup(ec));
    } else {
        goto done;
    }

    dse_call_callback(pdse, pb, SLAPI_OPERATION_DELETE, DSE_FLAG_POSTOP, ec, NULL, &returncode, returntext);
done:
    slapi_pblock_get(pb, SLAPI_DELETE_BEPOSTOP_ENTRY, &orig_entry);
    /* coverity false positive:
     *  Cvar_deref_model: Passing null pointer "ec" to "slapi_pblock_set", which dereferences it.
     * but ec is not dereferenced in SLAPI_DELETE_BEPOSTOP_ENTRY case so lets ignore this one.
     */
    /* coverity[var_deref_model] */
    slapi_pblock_set(pb, SLAPI_DELETE_BEPOSTOP_ENTRY, ec);
    /* make sure OPRETURN and RESULT_CODE are set */
    slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &rc);
    if (returncode || rc) {
        if (!rc) {
            slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &returncode);
        }
        if (!returncode) {
            returncode = rc;
        }
    }
    if (need_be_postop) {
        plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN);
        if (!returncode) {
            slapi_pblock_get(pb, SLAPI_RESULT_CODE, &returncode);
        }

        /* finally, give the be plugins a crack at it */
        plugin_call_plugins(pb, SLAPI_PLUGIN_BE_POST_DELETE_FN);
        if (!returncode) {
            slapi_pblock_get(pb, SLAPI_RESULT_CODE, &returncode);
        }
    }
    if (global_lock_owned) {
        global_backend_lock_unlock();
    }
    if (returncode && !returntext[0]) {
        char *ldap_result_message = NULL;
        slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
        if (ldap_result_message && ldap_result_message[0]) {
            PL_strncpyz(returntext, ldap_result_message, sizeof(returntext));
        }
    }
    /*
     * Check if we are deleting a plugin
     */
    if (returncode == LDAP_SUCCESS) {
        if (dse_delete_plugin(ec, returntext)) {
            rc = LDAP_UNWILLING_TO_PERFORM;
        }
    }
    /* coverity false positive:
     *  var_deref_model: Passing null pointer "orig_entry" to "slapi_pblock_set", which dereferences it.
     * but orig_entry is not dereferenced in SLAPI_DELETE_BEPOSTOP_ENTRY case so lets ignore this one.
     */
    /* coverity[var_deref_model] */
    slapi_pblock_set(pb, SLAPI_DELETE_BEPOSTOP_ENTRY, orig_entry);
    slapi_send_ldap_result(pb, returncode, NULL, returntext, 0, NULL);
    return dse_delete_return(returncode, ec);
}

struct dse_callback *
dse_register_callback(struct dse *pdse,
                      int operation,
                      int flags,
                      const Slapi_DN *base,
                      int scope,
                      const char *filter,
                      dseCallbackFn fn,
                      void *fn_arg,
                      struct slapdplugin *plugin)
{
    struct dse_callback *callback = dse_callback_new(operation, flags, base, scope, filter, fn, fn_arg, plugin);
    dse_callback_addtolist(&pdse->dse_callback, callback);
    return callback;
}

void
dse_remove_callback(struct dse *pdse, int operation, int flags, const Slapi_DN *base, int scope, const char *filter, dseCallbackFn fn)
{
    dse_callback_removefromlist(&pdse->dse_callback, operation, flags, base, scope, filter, fn);
}

/*
 * Return values:
 *    SLAPI_DSE_CALLBACK_ERROR        -- Callback failed.
 *  SLAPI_DSE_CALLBACK_OK           -- OK, do it.
 *  SLAPI_DSE_CALLBACK_DO_NOT_APPLY -- No error, but don't apply changes.
 */
static int
dse_call_callback(struct dse *pdse, Slapi_PBlock *pb, int operation, int flags, Slapi_Entry *entryBefore, Slapi_Entry *entryAfter, int *returncode, char *returntext)
{
    /* ONREPL callbacks can potentially modify pblock parameters like backend
     * which would cause problems during request processing. We need to save
     * "important" fields before calls and restoring them afterwards */
    int rc = SLAPI_DSE_CALLBACK_OK;

    if (pdse->dse_callback != NULL) {
        struct dse_callback *p = pdse->dse_callback;
        struct dse_callback *next = NULL;
        int result = SLAPI_DSE_CALLBACK_OK;

        while (p != NULL) {
            next = p->next;
            if ((p->operation & operation) && (p->flags & flags)) {
                if (slapi_sdn_scope_test(slapi_entry_get_sdn_const(entryBefore), p->base, p->scope)) {
                    if (NULL == p->slapifilter || slapi_vattr_filter_test(pb, entryBefore, p->slapifilter, 0) == 0) {
                        struct slapdplugin *plugin = p->plugin;
                        int plugin_started = 1;

                        if (plugin) {
                            /* this is a plugin callback, update the operation counter */
                            slapi_plugin_op_started(plugin);
                            if (!plugin->plg_started) {
                                /* must be a task function being called */
                                result = SLAPI_DSE_CALLBACK_ERROR;
                                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                            "Task entry (%s) could not added because the (%s) plugin is disabled.",
                                            slapi_entry_get_dn(entryBefore), p->plugin->plg_dn);
                                plugin_started = 0;
                            }
                        }
                        if (plugin_started) {
                            result = (*p->fn)(pb, entryBefore, entryAfter, returncode, returntext, p->fn_arg);
                        }
                        if (result < rc) {
                            rc = result;
                        }
                        slapi_plugin_op_finished(plugin);
                    }
                }
            }
            p = next;
        }
    }
    return rc;
}

int
slapi_config_register_callback(int operation,
                               int flags,
                               const char *base,
                               int scope,
                               const char *filter,
                               dseCallbackFn fn,
                               void *fn_arg)
{
    return slapi_config_register_callback_plugin(operation, flags, base, scope, filter, fn, fn_arg, NULL);
}

/*
 *  We pass in the pblock so we can update the operation counter for "dynamic plugins".
 */
int
slapi_config_register_callback_plugin(int operation,
                                      int flags,
                                      const char *base,
                                      int scope,
                                      const char *filter,
                                      dseCallbackFn fn,
                                      void *fn_arg,
                                      Slapi_PBlock *pb)
{
    int rc = 0;
    Slapi_Backend *be = slapi_be_select_by_instance_name(DSE_BACKEND);
    if (be != NULL) {
        struct dse *pdse = (struct dse *)be->be_database->plg_private;
        if (pdse != NULL) {
            Slapi_DN dn;
            slapi_sdn_init_dn_byref(&dn, base);
            if (pb != NULL) {
                /* if a pblock was passed, this is a plugin, so set the f_arg as the plugin */
                struct slapdplugin *pb_plugin = NULL;
                slapi_pblock_get(pb, SLAPI_PLUGIN, &pb_plugin);
                rc = (NULL != dse_register_callback(pdse, operation, flags, &dn, scope, filter, fn,
                                                    (void *)pb_plugin, pb_plugin));
            } else {
                rc = (NULL != dse_register_callback(pdse, operation, flags, &dn, scope, filter, fn,
                                                    fn_arg, NULL));
            }

            slapi_sdn_done(&dn);
        }
    }
    return rc;
}


int
slapi_config_remove_callback(int operation, int flags, const char *base, int scope, const char *filter, dseCallbackFn fn)
{
    int rc = 0;
    Slapi_Backend *be = slapi_be_select_by_instance_name(DSE_BACKEND);
    if (be != NULL) {
        struct dse *pdse = (struct dse *)be->be_database->plg_private;
        if (pdse != NULL) {
            Slapi_DN dn;
            slapi_sdn_init_dn_byref(&dn, base);
            dse_remove_callback(pdse, operation, flags, &dn, scope, filter, fn);
            slapi_sdn_done(&dn);
            rc = 1;
        }
    }
    return rc;
}

void
dse_set_dont_ever_write_dse_files()
{
    dont_ever_write_dse_files = 1;
}

void
dse_unset_dont_ever_write_dse_files()
{
    dont_ever_write_dse_files = 0;
}

static dse_search_set *
dse_search_set_new(void)
{
    dse_search_set *ss;

    ss = (dse_search_set *)slapi_ch_malloc(sizeof(*ss));

    if (ss) {
        dl_init(&ss->dl, 0);
        ss->current_entry = -1;
    }

    return ss;
}

/* This is similar to delete, but it does not free the entries contained in the
   search set.  This is useful in do_dse_search when we copy the entries from
   1 search set to the other. */
static void
dse_search_set_clean(dse_search_set *ss)
{
    if (ss) {
        dl_cleanup(&ss->dl, NULL);
        slapi_ch_free((void **)&ss);
    }
}

void
dse_search_set_release(void **ss)
{
    dse_search_set_delete(*(dse_search_set **)ss);
}

void
dse_prev_search_results(void *vp)
{
    Slapi_PBlock *pb = (Slapi_PBlock *)vp;
    dse_search_set *ss;
    slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_SET, &ss);
    if (ss) {
        dl_get_prev(&ss->dl, &ss->current_entry);
    }
}

static void
dse_search_set_delete(dse_search_set *ss)
{
    if (ss) {
        dl_cleanup(&ss->dl, dse_free_entry);
        slapi_ch_free((void **)&ss);
    }
}

static void
dse_free_entry(void **data)
{
    Slapi_Entry **e;

    if (data) {
        e = (Slapi_Entry **)data;
        if (*e)
            slapi_entry_free(*e);
    }
}

static void
dse_search_set_add_entry(dse_search_set *ss, Slapi_Entry *e)
{
    PR_ASSERT(ss && e);

    dl_add(&ss->dl, e);
}

static Slapi_Entry *
dse_search_set_get_next_entry(dse_search_set *ss)
{
    PR_ASSERT(ss);

    if (ss->current_entry == -1)
        return (dl_get_first(&ss->dl, &ss->current_entry));
    else
        return (dl_get_next(&ss->dl, &ss->current_entry));
}

int
dse_next_search_entry(Slapi_PBlock *pb)
{
    dse_search_set *ss;
    Slapi_Entry *e;

    slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_SET, &ss);

    /* no entries to return */
    if (ss == NULL) {
        slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, NULL);
        return 0;
    }

    e = dse_search_set_get_next_entry(ss);
    slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, e);

    /* we reached the end of the list */
    if (e == NULL) {
        pagedresults_set_search_result_pb(pb, NULL, 0);
        dse_search_set_delete(ss);
        slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, NULL);
    }

    return 0;
}


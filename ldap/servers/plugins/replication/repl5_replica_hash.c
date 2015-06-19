/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


/* repl5_replica_hash.c */

#include "repl5.h"
#include "plhash.h" 

/* global data */
static PLHashTable *s_hash;
static Slapi_RWLock *s_lock;

struct repl_enum_data
{
    FNEnumReplica fn;
    void *arg;
};

/* Forward declarations */
static PRIntn replica_destroy_hash_entry (PLHashEntry *he, PRIntn index, void *arg); 
static PRIntn replica_enumerate (PLHashEntry *he, PRIntn index, void *hash_data); 


int replica_init_name_hash ()
{
    /* allocate table */
    s_hash = PL_NewHashTable(0, PL_HashString, PL_CompareStrings,
                             PL_CompareValues, NULL, NULL);
    if (s_hash == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_init_name_hash: "
                        "failed to allocate hash table; NSPR error - %d\n",
                        PR_GetError ());	
        return -1;
    }

    /* create lock */
    s_lock = slapi_new_rwlock();
    if (s_lock == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_init_name_hash: "
                        "failed to create lock; NSPR error - %d\n",
                        PR_GetError ());
        replica_destroy_name_hash ();
        return -1;
    }

    return 0;
}

void replica_destroy_name_hash ()
{
    /* destroy the content */
    PL_HashTableEnumerateEntries(s_hash, replica_destroy_hash_entry, NULL);
    
    if (s_hash)
        PL_HashTableDestroy(s_hash); 

    if (s_lock)
        slapi_destroy_rwlock (s_lock);
}

int replica_add_by_name (const char *name, Object *replica)
{
    if (name == NULL || replica == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_add_by_name: NULL argument\n");
        return -1;
    }

    if (s_hash == NULL || s_lock == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_add_by_name: "
                        "replica hash is not initialized\n");
        return -1;
    }

    slapi_rwlock_wrlock (s_lock);
   
    /* make sure that the name is unique */
    if (PL_HashTableLookup(s_hash, name) != NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_add_by_name: "
                        "replica with name (%s) already in the hash\n", name);
        slapi_rwlock_unlock (s_lock);
        return -1 ;    
    }

    /* acquire replica object */
    object_acquire (replica);

    /* add replica */
    if (PL_HashTableAdd(s_hash, name, replica) == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_add_by_name: "
                        "failed to add replica with name (%s); NSPR error - %d\n",
                        name, PR_GetError ());
        object_release (replica);
        slapi_rwlock_unlock (s_lock);
        return -1;
    }

    slapi_rwlock_unlock (s_lock);
    return 0;
}

int replica_delete_by_name (const char *name)
{
    Object *replica;

    if (name == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_delete_by_name: "
                        "NULL argument\n");
        return -1;
    }

    if (s_hash == NULL || s_lock == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_delete_by_name: "
                        "replica hash is not initialized\n");
        return -1;
    }

    slapi_rwlock_wrlock (s_lock);

    /* locate object */
    replica = (Object*)PL_HashTableLookup(s_hash, name);     
    if (replica == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_delete_by_name: "
                        "replica with name (%s) is not in the hash.\n", name);
        slapi_rwlock_unlock (s_lock);
        return -1;
    }

    /* remove from hash */
    PL_HashTableRemove(s_hash, name);
    
    /* release replica */
    object_release (replica);

    slapi_rwlock_unlock (s_lock);

    return 0;
}

Object* replica_get_by_name (const char *name)
{
    Object *replica;

    if (name == NULL)
    {
         slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_get_by_name: "
                         "NULL argument\n");
        return NULL;
    }

    if (s_hash == NULL || s_lock == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_get_by_name: "
                        "replica hash is not initialized\n");
        return NULL;
    }

    slapi_rwlock_rdlock (s_lock);

    /* locate object */
    replica = (Object*)PL_HashTableLookup(s_hash, name);     
    if (replica == NULL)
    {
        slapi_rwlock_unlock (s_lock);
        return NULL;
    }

    object_acquire (replica);

    slapi_rwlock_unlock (s_lock);

    return replica;   
}

void replica_enumerate_replicas (FNEnumReplica fn, void *arg)
{
    struct repl_enum_data data;

    PR_ASSERT (fn);

    data.fn = fn;
    data.arg = arg;

    slapi_rwlock_wrlock (s_lock);
    PL_HashTableEnumerateEntries(s_hash, replica_enumerate, &data);
    slapi_rwlock_unlock (s_lock);
}

/* Helper functions */

/* this function called for each hash node during hash destruction */
static PRIntn replica_destroy_hash_entry (PLHashEntry *he, PRIntn index, void *arg)
{
    Object *r_obj;
    Replica *r;
    
    if (he == NULL)
        return HT_ENUMERATE_NEXT;

    r_obj = (Object*)he->value;
    r = (Replica*)object_get_data (r_obj);
    PR_ASSERT (r);

    /* flash replica state to the disk */
    replica_flush (r);

    /* release replica object */
    object_release (r_obj);

    return HT_ENUMERATE_REMOVE;
}

static PRIntn replica_enumerate (PLHashEntry *he, PRIntn index, void *hash_data)
{
    Object *r_obj;
    Replica *r;
	struct repl_enum_data *data = hash_data;

    r_obj = (Object*)he->value;
    PR_ASSERT (r_obj);

    object_acquire (r_obj);
    r = (Replica*)object_get_data (r_obj);
    PR_ASSERT (r);

    data->fn (r, data->arg);

    object_release (r_obj);

    return HT_ENUMERATE_NEXT;
} 


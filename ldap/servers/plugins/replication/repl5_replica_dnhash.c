/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* repl5_replica_dnhash.c */

#include "repl5.h"
#include "plhash.h" 

/* global data */
static PLHashTable *s_hash;
static PRRWLock *s_lock;

/* Forward declarations */
static PRIntn replica_destroy_hash_entry (PLHashEntry *he, PRIntn index, void *arg); 

int replica_init_dn_hash ()
{
    /* allocate table */
    s_hash = PL_NewHashTable(0, PL_HashString, PL_CompareStrings,
                             PL_CompareValues, NULL, NULL);
    if (s_hash == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_init_dn_hash: "
                        "failed to allocate hash table; NSPR error - %d\n",
                        PR_GetError ());	
        return -1;
    }

    /* create lock */
    s_lock = PR_NewRWLock(PR_RWLOCK_RANK_NONE, "replica_dnhash_lock");
    if (s_lock == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_init_dn_hash: "
                        "failed to create lock; NSPR error - %d\n",
                        PR_GetError ());
        replica_destroy_dn_hash ();
        return -1;
    }

    return 0;
}

void replica_destroy_dn_hash ()
{
    /* destroy the content */
    PL_HashTableEnumerateEntries(s_hash, replica_destroy_hash_entry, NULL);
    
    if (s_hash)
        PL_HashTableDestroy(s_hash); 

    if (s_lock)
        PR_DestroyRWLock (s_lock);
}

int replica_add_by_dn (const char *dn)
{
	char *dn_copy = NULL;

    if (dn == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_add_by_dn: NULL argument\n");
        return -1;
    }

    if (s_hash == NULL || s_lock == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_add_by_dn: "
                        "replica hash is not initialized\n");
        return -1;
    }

    PR_RWLock_Wlock (s_lock);
   
    /* make sure that the dn is unique */
    if (PL_HashTableLookup(s_hash, dn) != NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_add_by_dn: "
                        "replica with dn (%s) already in the hash\n", dn);
        PR_RWLock_Unlock (s_lock);
        return -1 ;    
    }

    /* add dn */
	dn_copy = slapi_ch_strdup(dn);
    if (PL_HashTableAdd(s_hash, dn_copy, dn_copy) == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_add_by_dn: "
                        "failed to add dn (%s); NSPR error - %d\n",
                        dn_copy, PR_GetError ());
		slapi_ch_free((void **)&dn_copy);
        PR_RWLock_Unlock (s_lock);
        return -1;
    }

	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "replica_add_by_dn: "
					"added dn (%s)\n",
					dn_copy);
    PR_RWLock_Unlock (s_lock);
    return 0;
}

int replica_delete_by_dn (const char *dn)
{
	char *dn_copy = NULL;

    if (dn == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_delete_by_dn: "
                        "NULL argument\n");
        return -1;
    }

    if (s_hash == NULL || s_lock == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_delete_by_dn: "
                        "replica hash is not initialized\n");
        return -1;
    }

    PR_RWLock_Wlock (s_lock);

    /* locate object */
    if (NULL == (dn_copy = (char *)PL_HashTableLookup(s_hash, dn)))
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_delete_by_dn: "
                        "dn (%s) is not in the hash.\n", dn);
        PR_RWLock_Unlock (s_lock);
        return -1;
    }

    /* remove from hash */
    PL_HashTableRemove(s_hash, dn);
	slapi_ch_free((void **)&dn_copy);

	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "replica_delete_by_dn: "
					"removed dn (%s)\n",
					dn);
    PR_RWLock_Unlock (s_lock);

    return 0;
}

int replica_is_being_configured (const char *dn)
{
    if (dn == NULL)
    {
         slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_is_dn_in_hash: "
                         "NULL argument\n");
        return 0;
    }

    if (s_hash == NULL || s_lock == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_is_dn_in_hash: "
                        "dn hash is not initialized\n");
        return 0;
    }

    PR_RWLock_Wlock (s_lock);

    /* locate object */
    if (NULL == PL_HashTableLookup(s_hash, dn))
    {
        PR_RWLock_Unlock (s_lock);
        return 0;
    }

    PR_RWLock_Unlock (s_lock);

    return 1;
}

/* Helper functions */

/* this function called for each hash node during hash destruction */
static PRIntn replica_destroy_hash_entry (PLHashEntry *he, PRIntn index, void *arg)
{
	char *dn_copy;
    
    if (he == NULL)
        return HT_ENUMERATE_NEXT;

    dn_copy = (char*)he->value;
	slapi_ch_free((void **)&dn_copy);

    return HT_ENUMERATE_REMOVE;
}

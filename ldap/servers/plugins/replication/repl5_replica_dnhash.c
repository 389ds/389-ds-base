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

/* repl5_replica_dnhash.c */

#include "repl5.h"
#include "plhash.h"

/* global data */
static PLHashTable *s_hash;
static Slapi_RWLock *s_lock;

/* Forward declarations */
static PRIntn replica_destroy_hash_entry(PLHashEntry *he, PRIntn index, void *arg);

int
replica_init_dn_hash()
{
    /* allocate table */
    s_hash = PL_NewHashTable(0, PL_HashString, PL_CompareStrings,
                             PL_CompareValues, NULL, NULL);
    if (s_hash == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_init_dn_hash: "
                                                       "failed to allocate hash table; NSPR error - %d\n",
                      PR_GetError());
        return -1;
    }

    /* create lock */
    s_lock = slapi_new_rwlock();
    if (s_lock == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_init_dn_hash: "
                                                       "failed to create lock; NSPR error - %d\n",
                      PR_GetError());
        replica_destroy_dn_hash();
        return -1;
    }

    return 0;
}

void
replica_destroy_dn_hash()
{
    /* destroy the content */
    PL_HashTableEnumerateEntries(s_hash, replica_destroy_hash_entry, NULL);

    if (s_hash)
        PL_HashTableDestroy(s_hash);

    if (s_lock)
        slapi_destroy_rwlock(s_lock);
}

int
replica_add_by_dn(const char *dn)
{
    char *dn_copy = NULL;

    if (dn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_add_by_dn: NULL argument\n");
        return -1;
    }

    if (s_hash == NULL || s_lock == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_add_by_dn: "
                                                       "replica hash is not initialized\n");
        return -1;
    }

    slapi_rwlock_wrlock(s_lock);

    /* make sure that the dn is unique */
    if (PL_HashTableLookup(s_hash, dn) != NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_add_by_dn: "
                                                       "replica with dn (%s) already in the hash\n",
                      dn);
        slapi_rwlock_unlock(s_lock);
        return -1;
    }

    /* add dn */
    dn_copy = slapi_ch_strdup(dn);
    if (PL_HashTableAdd(s_hash, dn_copy, dn_copy) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_add_by_dn: "
                                                       "failed to add dn (%s); NSPR error - %d\n",
                      dn_copy, PR_GetError());
        slapi_ch_free((void **)&dn_copy);
        slapi_rwlock_unlock(s_lock);
        return -1;
    }

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "replica_add_by_dn: "
                                                    "added dn (%s)\n",
                  dn_copy);
    slapi_rwlock_unlock(s_lock);
    return 0;
}

int
replica_delete_by_dn(const char *dn)
{
    char *dn_copy = NULL;

    if (dn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_delete_by_dn: "
                                                       "NULL argument\n");
        return -1;
    }

    if (s_hash == NULL || s_lock == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_delete_by_dn: "
                                                       "replica hash is not initialized\n");
        return -1;
    }

    slapi_rwlock_wrlock(s_lock);

    /* locate object */
    if (NULL == (dn_copy = (char *)PL_HashTableLookup(s_hash, dn))) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_delete_by_dn: "
                                                       "dn (%s) is not in the hash.\n",
                      dn);
        slapi_rwlock_unlock(s_lock);
        return -1;
    }

    /* remove from hash */
    PL_HashTableRemove(s_hash, dn);
    slapi_ch_free((void **)&dn_copy);

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "replica_delete_by_dn: "
                                                    "removed dn (%s)\n",
                  dn);
    slapi_rwlock_unlock(s_lock);

    return 0;
}

int
replica_is_being_configured(const char *dn)
{
    if (dn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_is_being_configured: "
                                                       "NULL argument\n");
        return 0;
    }

    if (s_hash == NULL || s_lock == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_is_being_configured: "
                                                       "dn hash is not initialized\n");
        return 0;
    }

    slapi_rwlock_wrlock(s_lock);

    /* locate object */
    if (NULL == PL_HashTableLookup(s_hash, dn)) {
        slapi_rwlock_unlock(s_lock);
        return 0;
    }

    slapi_rwlock_unlock(s_lock);

    return 1;
}

/* Helper functions */

/* this function called for each hash node during hash destruction */
static PRIntn
replica_destroy_hash_entry(PLHashEntry *he, PRIntn index __attribute__((unused)), void *arg __attribute__((unused)))
{
    char *dn_copy;

    if (he == NULL) {
        return HT_ENUMERATE_NEXT;
    }

    dn_copy = (char *)he->value;
    slapi_ch_free((void **)&dn_copy);

    return HT_ENUMERATE_REMOVE;
}

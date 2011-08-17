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

/* repl5_replica_dnhash.c */

#include "repl5.h"
#include "plhash.h" 

/* global data */
static PLHashTable *s_hash;
static Slapi_RWLock *s_lock;

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
    s_lock = slapi_new_rwlock();
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
        slapi_destroy_rwlock (s_lock);
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

    slapi_rwlock_wrlock (s_lock);
   
    /* make sure that the dn is unique */
    if (PL_HashTableLookup(s_hash, dn) != NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_add_by_dn: "
                        "replica with dn (%s) already in the hash\n", dn);
        slapi_rwlock_unlock (s_lock);
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
        slapi_rwlock_unlock (s_lock);
        return -1;
    }

	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "replica_add_by_dn: "
					"added dn (%s)\n",
					dn_copy);
    slapi_rwlock_unlock (s_lock);
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

    slapi_rwlock_wrlock (s_lock);

    /* locate object */
    if (NULL == (dn_copy = (char *)PL_HashTableLookup(s_hash, dn)))
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_delete_by_dn: "
                        "dn (%s) is not in the hash.\n", dn);
        slapi_rwlock_unlock (s_lock);
        return -1;
    }

    /* remove from hash */
    PL_HashTableRemove(s_hash, dn);
	slapi_ch_free((void **)&dn_copy);

	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "replica_delete_by_dn: "
					"removed dn (%s)\n",
					dn);
    slapi_rwlock_unlock (s_lock);

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

    slapi_rwlock_wrlock (s_lock);

    /* locate object */
    if (NULL == PL_HashTableLookup(s_hash, dn))
    {
        slapi_rwlock_unlock (s_lock);
        return 0;
    }

    slapi_rwlock_unlock (s_lock);

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

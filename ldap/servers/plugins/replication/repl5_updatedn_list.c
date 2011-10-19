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


/* repl5_updatedn_list.c */

/*
  This is the internal representation for the list of update DNs in the replica.
  The list is implemented as a hash table - the key is the normalized DN, and the
  value is the Slapi_DN representation of the DN
*/

#include "repl5.h"
#include "plhash.h" 

/* global data */

/* typedef ReplicaUpdateDNList PLHashTable; */

struct repl_enum_data
{
    FNEnumDN fn;
    void *arg;
};

/* Forward declarations */
static PRIntn replica_destroy_hash_entry (PLHashEntry *he, PRIntn index, void *arg); 
static PRIntn updatedn_list_enumerate (PLHashEntry *he, PRIntn index, void *hash_data);

static int
updatedn_compare_dns(const void *d1, const void *d2)
{
	return (0 == slapi_sdn_compare((const Slapi_DN *)d1, (const Slapi_DN *)d2));
}

/* create a new updatedn list - if the entry is given, initialize the list from
   the replicabinddn values given in the entry */
ReplicaUpdateDNList
replica_updatedn_list_new(const Slapi_Entry *entry)
{
    /* allocate table */
    PLHashTable *hash = PL_NewHashTable(4, PL_HashString, PL_CompareStrings,
										updatedn_compare_dns, NULL, NULL);
    if (hash == NULL) {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_new_updatedn_list: "
                        "failed to allocate hash table; NSPR error - %d\n",
                        PR_GetError ());	
        return NULL;
    }

	if (entry) {
		Slapi_Attr *attr = NULL;
		if (!slapi_entry_attr_find(entry, attr_replicaBindDn, &attr)) {
			Slapi_ValueSet *vs = NULL;
			slapi_attr_get_valueset(attr, &vs);
			replica_updatedn_list_replace(hash, vs);
			slapi_valueset_free(vs);
		}
	}	

    return (ReplicaUpdateDNList)hash;
}

void
replica_updatedn_list_free(ReplicaUpdateDNList list)
{
    /* destroy the content */
	PLHashTable *hash = list;
    PL_HashTableEnumerateEntries(hash, replica_destroy_hash_entry, NULL);
    
    if (hash)
        PL_HashTableDestroy(hash);
}

void
replica_updatedn_list_replace(ReplicaUpdateDNList list, const Slapi_ValueSet *vs)
{
	replica_updatedn_list_delete(list, NULL); /* delete all values */
	replica_updatedn_list_add(list, vs);
}

/* if vs is given, delete only those values - otherwise, delete all values */
void
replica_updatedn_list_delete(ReplicaUpdateDNList list, const Slapi_ValueSet *vs)
{
	PLHashTable *hash = list;
	if (!vs || slapi_valueset_count(vs) == 0) { /* just delete everything */
		PL_HashTableEnumerateEntries(hash, replica_destroy_hash_entry, NULL);
	} else {
		Slapi_ValueSet *vs_nc = (Slapi_ValueSet *)vs; /* cast away const */
		Slapi_Value *val = NULL;
		int index = 0;
		for (index = slapi_valueset_first_value(vs_nc, &val); val;
			 index = slapi_valueset_next_value(vs_nc, index, &val)) {
			Slapi_DN *dn = slapi_sdn_new_dn_byval(slapi_value_get_string(val));
			/* locate object */
			Slapi_DN *deldn = (Slapi_DN *)PL_HashTableLookup(hash, slapi_sdn_get_ndn(dn));     
			if (deldn == NULL)
			{
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "replica_updatedn_list_delete: "
								"update DN with value (%s) is not in the update DN list.\n",
								slapi_sdn_get_ndn(dn));
			} else {
				/* remove from hash */
				PL_HashTableRemove(hash, slapi_sdn_get_ndn(dn));
				/* free the pointer */
				slapi_sdn_free(&deldn);
			}
			/* free the temp dn */
			slapi_sdn_free(&dn);
		}
	}

	return;
}

void
replica_updatedn_list_add(ReplicaUpdateDNList list, const Slapi_ValueSet *vs)
{
	PLHashTable *hash = list;
	Slapi_ValueSet *vs_nc = (Slapi_ValueSet *)vs; /* cast away const */
	Slapi_Value *val = NULL;
	int index = 0;

	PR_ASSERT(list && vs);

	for (index = slapi_valueset_first_value(vs_nc, &val); val;
		 index = slapi_valueset_next_value(vs_nc, index, &val)) {
		Slapi_DN *dn = slapi_sdn_new_dn_byval(slapi_value_get_string(val));
		const char *ndn = slapi_sdn_get_ndn(dn);

		/* make sure that the name is unique */
		if (PL_HashTableLookup(hash, ndn) != NULL)
		{
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "replica_updatedn_list_add: "
							"update DN with value (%s) already in the update DN list\n",
							ndn);
			slapi_sdn_free(&dn);
		} else {
			PL_HashTableAdd(hash, ndn, dn);
		}
	}

	return;
}

PRBool
replica_updatedn_list_ismember(ReplicaUpdateDNList list, const Slapi_DN *dn)
{
	PLHashTable *hash = list;
	PRBool ret = PR_FALSE;

	const char *ndn = slapi_sdn_get_ndn(dn);

	/* Bug 605169 - null ndn would cause core dump */
	if ( ndn ) {
		ret = (PRBool)((uintptr_t)PL_HashTableLookupConst(hash, ndn));
	}

	return ret;
}

struct list_to_string_data {
	char *string;
	const char *delimiter;
};

static int
convert_to_string(Slapi_DN *dn, void *arg)
{
	struct list_to_string_data *data = (struct list_to_string_data *)arg;
	int newlen = slapi_sdn_get_ndn_len(dn) + strlen(data->delimiter) + 1;
	if (data->string) {
		newlen += strlen(data->string);
		data->string = slapi_ch_realloc(data->string, newlen);
	} else {
		data->string = slapi_ch_calloc(1, newlen);
	}
	strcat(data->string, slapi_sdn_get_dn(dn));
	strcat(data->string, data->delimiter);

	return 1;
}

/* caller must slapi_ch_free_string the returned string */
char *
replica_updatedn_list_to_string(ReplicaUpdateDNList list, const char *delimiter)
{
	struct list_to_string_data data;
	data.string = NULL;
	data.delimiter = delimiter;
	replica_updatedn_list_enumerate(list, convert_to_string, (void *)&data);
	return data.string;
}	

void
replica_updatedn_list_enumerate(ReplicaUpdateDNList list, FNEnumDN fn, void *arg)
{
	PLHashTable *hash = list;
    struct repl_enum_data data;

    PR_ASSERT (fn);

    data.fn = fn;
    data.arg = arg;

    PL_HashTableEnumerateEntries(hash, updatedn_list_enumerate, &data);
}

/* Helper functions */

/* this function called for each hash node during hash destruction */
static PRIntn
replica_destroy_hash_entry(PLHashEntry *he, PRIntn index, void *arg)
{
    Slapi_DN *dn = NULL;
    
    if (he == NULL)
        return HT_ENUMERATE_NEXT;

    dn = (Slapi_DN *)he->value;
    PR_ASSERT (dn);

    slapi_sdn_free(&dn);

    return HT_ENUMERATE_REMOVE;
}

static PRIntn
updatedn_list_enumerate(PLHashEntry *he, PRIntn index, void *hash_data)
{
	Slapi_DN *dn = NULL;
	struct repl_enum_data *data = hash_data;

    dn = (Slapi_DN*)he->value;
    PR_ASSERT (dn);

    data->fn(dn, data->arg);

    return HT_ENUMERATE_NEXT;
} 

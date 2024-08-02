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
static PRIntn replica_destroy_hash_entry(PLHashEntry *he, PRIntn index, void *arg);
static PRIntn updatedn_list_enumerate(PLHashEntry *he, PRIntn index, void *hash_data);

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
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_new_updatedn_list - "
                                                       "Failed to allocate hash table; NSPR error - %d\n",
                      PR_GetError());
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

Slapi_ValueSet *
replica_updatedn_group_new(const Slapi_Entry *entry)
{
    Slapi_ValueSet *vs = NULL;
    if (entry) {
        Slapi_Attr *attr = NULL;
        if (!slapi_entry_attr_find(entry, attr_replicaBindDnGroup, &attr)) {
            slapi_attr_get_valueset(attr, &vs);
        }
    }
    return (vs);
}

ReplicaUpdateDNList
replica_groupdn_list_new(const Slapi_ValueSet *vs)
{
    PLHashTable *hash;

    if (vs == NULL) {
        return NULL;
    }
    /* allocate table */
    hash = PL_NewHashTable(4, PL_HashString, PL_CompareStrings,
                           updatedn_compare_dns, NULL, NULL);
    if (hash == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_new_updatedn_list - "
                                                       "Failed to allocate hash table; NSPR error - %d\n",
                      PR_GetError());
        return NULL;
    }

    replica_updatedn_list_delete(hash, NULL); /* delete all values */
    replica_updatedn_list_add_ext(hash, vs, 1);

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
    replica_updatedn_list_add_ext(list, vs, 0);
}

void
replica_updatedn_list_group_replace(ReplicaUpdateDNList list, const Slapi_ValueSet *vs)
{
    replica_updatedn_list_delete(list, NULL); /* delete all values */
    replica_updatedn_list_add_ext(list, vs, 1);
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
            if (deldn == NULL) {
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "replica_updatedn_list_delete -"
                                                                "Update DN with value (%s) is not in the update DN list.\n",
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

Slapi_ValueSet *
replica_updatedn_list_get_members(Slapi_DN *dn)
{
    static char *const filter_groups = "(|(objectclass=groupOfNames)(objectclass=groupOfUniqueNames)(objectclass=groupOfURLs))";
    static char *const type_member = "member";
    static char *const type_uniquemember = "uniquemember";
    static char *const type_memberURL = "memberURL";

    int rval;
    char *attrs[4];
    Slapi_PBlock *mpb = slapi_pblock_new();
    Slapi_ValueSet *members = slapi_valueset_new();

    attrs[0] = type_member;
    attrs[1] = type_uniquemember;
    attrs[2] = type_memberURL;
    attrs[3] = NULL;
    slapi_search_internal_set_pb(mpb, slapi_sdn_get_ndn(dn), LDAP_SCOPE_BASE, filter_groups,
                                 &attrs[0], 0, NULL /* controls */, NULL /* uniqueid */,
                                 repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION), 0);
    slapi_search_internal_pb(mpb);
    slapi_pblock_get(mpb, SLAPI_PLUGIN_INTOP_RESULT, &rval);
    if (rval == LDAP_SUCCESS) {
        Slapi_Entry **ep;
        slapi_pblock_get(mpb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &ep);
        if ((ep != NULL) && (ep[0] != NULL)) {
            Slapi_Attr *attr = NULL;
            Slapi_Attr *nextAttr = NULL;
            Slapi_ValueSet *vs = NULL;
            char *attrType;
            slapi_entry_first_attr(ep[0], &attr);
            while (attr) {
                slapi_attr_get_type(attr, &attrType);

                if ((strcasecmp(attrType, type_member) == 0) ||
                    (strcasecmp(attrType, type_uniquemember) == 0)) {
                    slapi_attr_get_valueset(attr, &vs);
                    slapi_valueset_join_attr_valueset(attr, members, vs);
                    slapi_valueset_free(vs);
                } else if (strcasecmp(attrType, type_memberURL) == 0) {
                    /* not yet supported */
                }
                slapi_entry_next_attr(ep[0], attr, &nextAttr);
                attr = nextAttr;
            }
        }
    }
    slapi_free_search_results_internal(mpb);
    slapi_pblock_destroy(mpb);
    return (members);
}
/*
 * add  a list of dns to the ReplicaUpdateDNList.
 * The dn could be the dn of a group, so get the entry
 * and check the objectclass. If it is a static or dynamic group
 * generate the list of member dns and recursively call
 * replica_updatedn_list_add().
 * The dn of the group is added to the list, so it will detect
 * potential circular group definitions
 */
void
replica_updatedn_list_add_ext(ReplicaUpdateDNList list, const Slapi_ValueSet *vs, int group_update)
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
        if (PL_HashTableLookup(hash, ndn) != NULL) {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "replica_updatedn_list_add - "
                                                            "Update DN with value (%s) already in the update DN list\n",
                          ndn);
            slapi_sdn_free(&dn);
        } else {
            Slapi_ValueSet *members = NULL;
            PL_HashTableAdd(hash, ndn, dn);
            /* add it, even if it is a group dn, this will
             * prevent problems with circular group definitions
             * then check if it has mor members to add */
            if (group_update) {
                members = replica_updatedn_list_get_members(dn);
                if (members) {
                    replica_updatedn_list_add_ext(list, members, 1);
                    /* free members */
                    slapi_valueset_free(members);
                }
            }
        }
    }

    return;
}
void
replica_updatedn_list_add(ReplicaUpdateDNList list, const Slapi_ValueSet *vs)
{
    replica_updatedn_list_add_ext(list, vs, 0);
}

PRBool
replica_updatedn_list_ismember(ReplicaUpdateDNList list, const Slapi_DN *dn)
{
    PLHashTable *hash = list;
    PRBool ret = PR_FALSE;

    const char *ndn = slapi_sdn_get_ndn(dn);

    /* Bug 605169 - null ndn would cause core dump */
    if (ndn) {
        if ((uintptr_t)PL_HashTableLookupConst(hash, ndn))
            ret = PR_TRUE;
        else
            ret = PR_FALSE;
    }

    return ret;
}

struct list_to_string_data
{
    char *string;
    const char *delimiter;
};

static int
convert_to_string(Slapi_DN *dn, void *arg)
{
    struct list_to_string_data *data = (struct list_to_string_data *)arg;
    char *str = slapi_ch_smprintf("%s%s%s", (data->string ? data->string : ""),
                                  slapi_sdn_get_dn(dn), data->delimiter);
    slapi_ch_free_string(&data->string);
    data->string = str;
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

    PR_ASSERT(fn);

    data.fn = fn;
    data.arg = arg;

    PL_HashTableEnumerateEntries(hash, updatedn_list_enumerate, &data);
}

/* Helper functions */

/* this function called for each hash node during hash destruction */
static PRIntn
replica_destroy_hash_entry(PLHashEntry *he, PRIntn index __attribute__((unused)), void *arg __attribute__((unused)))
{
    Slapi_DN *dn = NULL;

    if (he == NULL) {
        return HT_ENUMERATE_NEXT;
    }

    dn = (Slapi_DN *)he->value;
    PR_ASSERT(dn);

    slapi_sdn_free(&dn);

    return HT_ENUMERATE_REMOVE;
}

static PRIntn
updatedn_list_enumerate(PLHashEntry *he, PRIntn index __attribute__((unused)), void *hash_data)
{
    Slapi_DN *dn = NULL;
    struct repl_enum_data *data = hash_data;

    dn = (Slapi_DN *)he->value;
    PR_ASSERT(dn);

    data->fn(dn, data->arg);

    return HT_ENUMERATE_NEXT;
}

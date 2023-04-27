/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2023 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* util.c   -- utility functions -- functions available form libslapd */
#include <sys/param.h>
#include <unistd.h>
#include <pwd.h>
#include <stdint.h>
#include <fcntl.h>
#include "slap.h"
#include <sys/resource.h>
#include <errno.h>

#define MEMBEROF_CACHE_DEBUG 0
#define MEMBEROF_CONFIG_DN "cn=MemberOf Plugin,cn=plugins,cn=config"
#define MEMBEROF_GOUP_ATTR "memberofgroupattr"
#define MEMBEROF_ATTR "memberofattr"
#define MEMBEROF_INCLUDE_SCOPE "memberOfEntryScope"
#define MEMBEROF_EXLCUDE_SCOPE "memberOfEntryScopeExcludeSubtree"
#define MEMBEROF_ALL_BACKENDS "memberOfAllBackends"
#define MEMBEROF_SKIP_NESTED "memberOfSkipNested"
#define MEMBEROF_ENABLED "nsslapd-pluginEnabled"

struct memberof_plugin_config {
    char **groupattrs;
    char *memberof_attr;
    PRBool all_backends;
    PRBool skip_nested;
    PRBool enabled;
    char **include_scope;
    char **exclude_scope;
};
static struct memberof_plugin_config *memberof_config = NULL;

typedef struct _sm_memberof_get_groups_data {
    Slapi_MemberOfConfig *config;
    Slapi_Value *memberdn_val;
    Slapi_ValueSet **groupvals; /* list of group DN which memberdn_val is member of */
    Slapi_ValueSet **group_norm_vals;
    Slapi_ValueSet **nsuniqueidvals; /* equivalent to groupvals but with nsuniqueid */
    Slapi_ValueSet **already_seen_ndn_vals;
    PRBool use_cache;
} sm_memberof_get_groups_data;


/* The key to access the hash table is the normalized DN
 * The normalized DN is stored in the value because:
 *  - It is used in slapi_valueset_find
 *  - It is used to fill the memberof_get_groups_data.group_norm_vals
 */
typedef struct _sm_memberof_cached_value {
    char *key;
    char *group_dn_val;
    char *group_ndn_val;
    char *nsuniqueid_val;
    int valid;
} sm_memberof_cached_value;

struct sm_cache_stat {
    int total_lookup;
    int successfull_lookup;
    int total_add;
    int total_remove;
    int total_enumerate;
    int cumul_duration_lookup;
    int cumul_duration_add;
    int cumul_duration_remove;
    int cumul_duration_enumerate;
};
static struct sm_cache_stat sm_cache_stat;



static sm_memberof_cached_value *sm_ancestors_cache_lookup(Slapi_MemberOfConfig *config, const char *ndn);
static PLHashEntry *sm_ancestors_cache_add(Slapi_MemberOfConfig *config, const void *key, void *value);
static void sm_ancestor_hashtable_entry_free(sm_memberof_cached_value *entry);
static void sm_cache_ancestors(Slapi_MemberOfConfig *config, Slapi_Value **member_ndn_val, sm_memberof_get_groups_data *groups);
static int  sm_memberof_compare(Slapi_MemberOfConfig *config, const void *a, const void *b);
static void sm_merge_ancestors(Slapi_Value **member_ndn_val, sm_memberof_get_groups_data *v1, sm_memberof_get_groups_data *v2);
static int  sm_memberof_entry_in_scope(Slapi_MemberOfConfig *config, Slapi_DN *sdn);
static int  sm_memberof_get_groups_callback(Slapi_Entry *e, void *callback_data);
static void sm_report_error_msg(Slapi_MemberOfConfig *config, char* msg);
static int  sm_entry_get_groups(Slapi_MemberOfConfig *config, Slapi_DN *member_sdn, char *memberof_attr,
                                Slapi_ValueSet *groupvals, Slapi_ValueSet *nsuniqueidvals);
static PRBool sm_compare_memberof_config(const char *memberof_attr, char **groupattrs, PRBool all_backends, 
                                         PRBool skip_nested, Slapi_DN **include_scope, Slapi_DN **exclude_scope, PRBool enabled_only);
static void sm_add_ancestors_cbdata(sm_memberof_cached_value *ancestors, void *callback_data);
static int  sm_memberof_call_foreach_dn(Slapi_PBlock *pb __attribute__((unused)), Slapi_DN *sdn, Slapi_MemberOfConfig *config, char **types,
                                        plugin_search_entry_callback callback, void *callback_data, int *cached, PRBool use_grp_cache);
static int  sm_memberof_get_groups_r(Slapi_MemberOfConfig *config, Slapi_DN *member_sdn, sm_memberof_get_groups_data *data);
static PRIntn sm_memberof_hash_compare_keys(const void *v1, const void *v2);
static PRIntn sm_memberof_hash_compare_values(const void *v1, const void *v2);
static PLHashNumber sm_memberof_hash_fn(const void *key);
static PLHashTable *sm_hashtable_new(int usetxn);
static PRIntn sm_ancestor_hashtable_remove(PLHashEntry *he, PRIntn index __attribute__((unused)), void *arg __attribute__((unused)));
static void sm_ancestor_hashtable_empty(Slapi_MemberOfConfig *config, char *msg);


#if MEMBEROF_CACHE_DEBUG
static void
sm_dump_cache_entry(sm_memberof_cached_value *double_check, const char *msg)
{
    for (size_t i = 0; double_check[i].valid; i++) {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_dump_cache_entry: %s -> %s/%s\n",
                      msg ? msg : "<no key>",
                      double_check[i].group_dn_val ? double_check[i].group_dn_val : "NULL",
                      double_check[i].nsuniqueid_val ? double_check[i].nsuniqueid_val : "NULL");
    }
}
#endif

static sm_memberof_cached_value *
sm_ancestors_cache_lookup(Slapi_MemberOfConfig *config, const char *ndn)
{
    sm_memberof_cached_value *e;
#if defined(DEBUG)
    long int start;
    struct timespec tsnow;
#endif

    sm_cache_stat.total_lookup++;

#if defined(DEBUG)
    if (clock_gettime(CLOCK_REALTIME, &tsnow) != 0) {
        start = 0;
    } else {
        start = tsnow.tv_nsec;
    }
#endif

    e = (sm_memberof_cached_value *) PL_HashTableLookupConst(config->ancestors_cache, (const void *) ndn);

#if defined(DEBUG)
    if (start) {
        if (clock_gettime(CLOCK_REALTIME, &tsnow) == 0) {
            sm_cache_stat.cumul_duration_lookup += (tsnow.tv_nsec - start);
        }
    }
#endif

    if (e)
        sm_cache_stat.successfull_lookup++;
    return e;
}

/* allocates the plugin hashtable
 * This hash table is used by operation and is protected from
 * concurrent operations with the memberof_lock (if not usetxn, memberof_lock
 * is not implemented and the hash table will be not used.
 *
 * The hash table contains all the DN of the entries for which the memberof
 * attribute has been computed/updated during the current operation
 *
 * hash table should be empty at the beginning and end of the plugin callback
 */

static void
sm_ancestor_hashtable_entry_free(sm_memberof_cached_value *entry)
{
    size_t i;
    if (entry == NULL) {
        return;
    }

    /* entry[] is an array. The last element of the array contains 'valid'=0 */
    for (i = 0; entry[i].valid; i++) {
        slapi_ch_free_string(&entry[i].group_dn_val);
        slapi_ch_free_string(&entry[i].group_ndn_val);
        slapi_ch_free_string(&entry[i].nsuniqueid_val);
    }
    /* Here we are at the ending element containing the key */
    slapi_ch_free_string(&entry[i].key);
}

static PLHashEntry *
sm_ancestors_cache_add(Slapi_MemberOfConfig *config, const void *key, void *value)
{
    PLHashEntry *e;
#if defined(DEBUG)
    long int start;
    struct timespec tsnow;
#endif
    sm_cache_stat.total_add++;

#if defined(DEBUG)
    if (clock_gettime(CLOCK_REALTIME, &tsnow) != 0) {
        start = 0;
    } else {
        start = tsnow.tv_nsec;
    }
#endif

    e = PL_HashTableAdd(config->ancestors_cache, key, value);

#if defined(DEBUG)
    if (start) {
        if (clock_gettime(CLOCK_REALTIME, &tsnow) == 0) {
            sm_cache_stat.cumul_duration_add += (tsnow.tv_nsec - start);
        }
    }
#endif
    return e;
}

/*
 * A cache value consist of an array of all dn/ndn of the groups member_ndn_val belongs to
 * The last element of the array has 'valid=0'
 * the firsts elements of the array has 'valid=1' and the dn/ndn of group it belong to
 */
static void
sm_cache_ancestors(Slapi_MemberOfConfig *config, Slapi_Value **member_ndn_val, sm_memberof_get_groups_data *groups)
{
    Slapi_ValueSet *groupvals = *((sm_memberof_get_groups_data *) groups)->groupvals;
    Slapi_ValueSet *nsuniqueidvals = *((sm_memberof_get_groups_data *) groups)->nsuniqueidvals;
    Slapi_Value *sval_dn = NULL;
    Slapi_Value *sval_nsuniqueid = NULL;
    Slapi_DN *sdn = NULL;
    const char *dn = NULL;
    const char *nsuniqueid = NULL;
    const char *ndn = NULL;
    const char *key = NULL;
    char *key_copy = NULL;
    int hint_dn = 0;
    int hint_nsuniqueid = 0;
    int count = 0;
    size_t index;
    sm_memberof_cached_value *cache_entry;
#if MEMBEROF_CACHE_DEBUG
    sm_memberof_cached_value *double_check;
#endif

    if ((member_ndn_val == NULL) || (*member_ndn_val == NULL)) {
        slapi_log_err(SLAPI_LOG_FATAL, "slapi_memberof", "sm_cache_ancestors: Fail to cache groups ancestor of unknown member\n");
        return;
    }

    /* Allocate the cache entry and fill it */
    count = slapi_valueset_count(groupvals);
    if (count == 0) {
        /* There is no group containing member_ndn_val
         * so cache the NULL value
         */
        cache_entry = (sm_memberof_cached_value *) slapi_ch_calloc(2, sizeof (sm_memberof_cached_value));
        if (!cache_entry) {
            slapi_log_err(SLAPI_LOG_FATAL, "slapi_memberof", "sm_cache_ancestors: Fail to cache no group are ancestor of %s\n",
                          slapi_value_get_string(*member_ndn_val));
            return;
        }
        index = 0;
        cache_entry[index].key = NULL; /* only the last element (valid=0) contains the key */
        cache_entry[index].group_dn_val = NULL;
        cache_entry[index].group_ndn_val = NULL;
        cache_entry[index].nsuniqueid_val = NULL;
        cache_entry[index].valid = 1; /* this entry is valid and indicate no group contain member_ndn_val */
#if MEMBEROF_CACHE_DEBUG
        slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_cache_ancestors: For %s cache %s/%s\n",
                      slapi_value_get_string(*member_ndn_val),
                      cache_entry[index].group_dn_val ? cache_entry[index].group_dn_val : "<empty>",
                      cache_entry[index].nsuniqueid_val ? cache_entry[index].nsuniqueid_val : "<empty>");
#endif
        index++;

    } else {
        cache_entry = (sm_memberof_cached_value *) slapi_ch_calloc(count + 1, sizeof (sm_memberof_cached_value));
        if (!cache_entry) {
            slapi_log_err(SLAPI_LOG_FATAL, "slapi_memberof", "sm_cache_ancestors: Fail to cache groups ancestor of %s\n",
                          slapi_value_get_string(*member_ndn_val));
            return;
        }

        /* Store the dn/ndn into the cache_entry */
        index = 0;
        hint_dn = slapi_valueset_first_value(groupvals, &sval_dn);
        hint_nsuniqueid = slapi_valueset_first_value(nsuniqueidvals, &sval_nsuniqueid);
        while (sval_dn) {
            /* In case of recursion the member_ndn can be present
             * in its own ancestors. Skip it
             */
            if (sm_memberof_compare(groups->config, member_ndn_val, &sval_dn)) {
                /* add this dn/ndn even if it is NULL
                 * in fact a node belonging to no group needs to be cached
                 */
                dn = slapi_value_get_string(sval_dn);
                nsuniqueid = slapi_value_get_string(sval_nsuniqueid);
                sdn = slapi_sdn_new_dn_byval(dn);
                ndn = slapi_sdn_get_ndn(sdn);

                cache_entry[index].key = NULL; /* only the last element (valid=0) contains the key */
                cache_entry[index].group_dn_val = slapi_ch_strdup(dn);
                cache_entry[index].group_ndn_val = slapi_ch_strdup(ndn);
                cache_entry[index].nsuniqueid_val = slapi_ch_strdup(nsuniqueid);
                cache_entry[index].valid = 1;
#if MEMBEROF_CACHE_DEBUG
                slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_cache_ancestors:    %s/%s\n",
                              cache_entry[index].group_dn_val ? cache_entry[index].group_dn_val : "<empty>",
                              cache_entry[index].nsuniqueid_val ? cache_entry[index].nsuniqueid_val : "<empty>");
#endif
                index++;
                slapi_sdn_free(&sdn);
            }

            hint_dn = slapi_valueset_next_value(groupvals, hint_dn, &sval_dn);
            hint_nsuniqueid = slapi_valueset_next_value(nsuniqueidvals, hint_nsuniqueid, &sval_nsuniqueid);
        }
    }
    /* This is marking the end of the cache_entry */
    key = slapi_value_get_string(*member_ndn_val);
    key_copy = slapi_ch_strdup(key);
    cache_entry[index].key = key_copy;
    cache_entry[index].group_dn_val = NULL;
    cache_entry[index].group_ndn_val = NULL;
    cache_entry[index].nsuniqueid_val = NULL;
    cache_entry[index].valid = 0;

    /* Cache the ancestor of member_ndn_val using the
     * normalized DN key
     */
#if MEMBEROF_CACHE_DEBUG
    sm_dump_cache_entry(cache_entry, key);
#endif
    if (sm_ancestors_cache_add(config, (const void*) key_copy, (void *) cache_entry) == NULL) {
        slapi_log_err(SLAPI_LOG_FATAL, "slapi_memberof", "sm_cache_ancestors: Failed to cache ancestor of %s\n", key);
        sm_ancestor_hashtable_entry_free(cache_entry);
        slapi_ch_free((void**) &cache_entry);
        return;
    }
#if MEMBEROF_CACHE_DEBUG
    if ((double_check = sm_ancestors_cache_lookup(config, (const void*) key)) != NULL) {
        sm_dump_cache_entry(double_check, "read back");
    }
#endif
}

/* memberof_compare()
 *
 * compare two attr values
 */
static int
sm_memberof_compare(Slapi_MemberOfConfig *config, const void *a, const void *b)
{
    Slapi_Value *val1;
    Slapi_Value *val2;

    if (a == NULL && b != NULL) {
        return 1;
    } else if (a != NULL && b == NULL) {
        return -1;
    } else if (a == NULL && b == NULL) {
        return 0;
    }
    val1 = *((Slapi_Value **) a);
    val2 = *((Slapi_Value **) b);

    /* We only need to provide a Slapi_Attr here for it's syntax.  We
     * already validated all grouping attributes to use the Distinguished
     * Name syntax, so we can safely just use the first attr.
     */
    return slapi_attr_value_cmp_ext(config->dn_syntax_attr, val1, val2);
}

/*
 * Add in v2 the values that are in v1
 * If the values are already present in v2, they are skipped
 * It does not consum the values in v1
 */
static void
sm_merge_ancestors(Slapi_Value **member_ndn_val, sm_memberof_get_groups_data *v1, sm_memberof_get_groups_data *v2)
{
    Slapi_Value *sval_dn = 0;
    Slapi_Value *sval_ndn = 0;
    Slapi_Value *sval_nsuniqueid = 0;
    Slapi_Value *sval, *sval_2;
    Slapi_DN *val_sdn = 0;
    int hint = 0;
    int hint_nsuniqueid = 0;
    Slapi_MemberOfConfig *config = ((sm_memberof_get_groups_data *) v2)->config;
    Slapi_ValueSet *v1_groupvals = *((sm_memberof_get_groups_data *) v1)->groupvals;
    Slapi_ValueSet *v2_groupvals = *((sm_memberof_get_groups_data *) v2)->groupvals;
    Slapi_ValueSet *v2_group_norm_vals = *((sm_memberof_get_groups_data *) v2)->group_norm_vals;
    Slapi_ValueSet *v1_nsuniqueidvals = *((sm_memberof_get_groups_data *) v1)->nsuniqueidvals;
    Slapi_ValueSet *v2_nsuniqueidvals = *((sm_memberof_get_groups_data *) v2)->nsuniqueidvals;
    int merged_cnt = 0;

    hint = slapi_valueset_first_value(v1_groupvals, &sval);
    hint_nsuniqueid = slapi_valueset_first_value(v1_nsuniqueidvals, &sval_2);
    while (sval) {
        if (sm_memberof_compare(config, member_ndn_val, &sval)) {
            sval_dn = slapi_value_new_string(slapi_value_get_string(sval));
            sval_nsuniqueid = slapi_value_new_string(slapi_value_get_string(sval_2));
            if (sval_dn) {
                /* Use the normalized dn from v1 to search it in v2 */
                val_sdn = slapi_sdn_new_dn_byval(slapi_value_get_string(sval_dn));
                sval_ndn = slapi_value_new_string(slapi_sdn_get_ndn(val_sdn));
                if (!slapi_valueset_find(((sm_memberof_get_groups_data *) v2)->config->dn_syntax_attr,
                        v2_group_norm_vals, sval_ndn)) {
                    /* This ancestor was not already present in v2 => Add it
                     * Using slapi_valueset_add_value it consumes val
                     * so do not free sval
                     */
#if MEMBEROF_CACHE_DEBUG
                    slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_merge_ancestors: add %s\n", slapi_value_get_string(sval_ndn));
#endif
                    slapi_valueset_add_value_ext(v2_groupvals, sval_dn, SLAPI_VALUE_FLAG_PASSIN);
                    slapi_valueset_add_value_ext(v2_group_norm_vals, sval_ndn, SLAPI_VALUE_FLAG_PASSIN);
                    slapi_valueset_add_value_ext(v2_nsuniqueidvals, sval_nsuniqueid, SLAPI_VALUE_FLAG_PASSIN);
                    merged_cnt++;
                } else {
                    /* This ancestor was already present, free sval_ndn/sval_dn that will not be consumed */
#if MEMBEROF_CACHE_DEBUG
                    slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_merge_ancestors: skip (already present) %s\n", slapi_value_get_string(sval_ndn));
#endif
                    slapi_value_free(&sval_dn);
                    slapi_value_free(&sval_ndn);
                    slapi_value_free(&sval_nsuniqueid);
                }
                slapi_sdn_free(&val_sdn);
            }
        }
        hint = slapi_valueset_next_value(v1_groupvals, hint, &sval);
        hint_nsuniqueid = slapi_valueset_next_value(v1_nsuniqueidvals, hint_nsuniqueid, &sval_2);
    }
}

/*
 * Return 1 if the entry is in the scope.
 * For MODRDN the caller should check both the preop
 * and postop entries.  If we are moving out of, or
 * into scope, we should process it.
 */
static int
sm_memberof_entry_in_scope(Slapi_MemberOfConfig *config, Slapi_DN *sdn)
{
    if (config->entryScopeExcludeSubtrees) {
        /* check the excludes */
        size_t i = 0;
        while (config->entryScopeExcludeSubtrees[i]) {
            if (slapi_sdn_issuffix(sdn, config->entryScopeExcludeSubtrees[i])) {
                return 0;
            }
            i++;
        }
    }
    if (config->entryScopes) {
        /* check the excludes */
        size_t i = 0;
        while (config->entryScopes[i]) {
            if (slapi_sdn_issuffix(sdn, config->entryScopes[i])) {
                return 1;
            }
            i++;
        }
        return 0;
    }
    return 1;
}

/* memberof_get_groups_callback()
 *
 * Callback to perform work of memberof_get_groups()
 */
static int
sm_memberof_get_groups_callback(Slapi_Entry *e, void *callback_data)
{
    Slapi_DN *group_sdn = slapi_entry_get_sdn(e);
    char *group_ndn = slapi_entry_get_ndn(e);
    char *group_dn = slapi_entry_get_dn(e);
    const char *group_nsuniqueid = slapi_entry_get_uniqueid(e);
    Slapi_Value *group_ndn_val = 0;
    Slapi_Value *group_dn_val = 0;
    Slapi_Value *group_nsuniqueid_val = 0;
    Slapi_Value *already_seen_ndn_val = 0;
    Slapi_ValueSet *groupvals = *((sm_memberof_get_groups_data *) callback_data)->groupvals;
    Slapi_ValueSet *group_norm_vals = *((sm_memberof_get_groups_data *) callback_data)->group_norm_vals;
    Slapi_ValueSet *group_nsuniqueid_vals = *((sm_memberof_get_groups_data *) callback_data)->nsuniqueidvals;
    Slapi_ValueSet *already_seen_ndn_vals = *((sm_memberof_get_groups_data *) callback_data)->already_seen_ndn_vals;
    Slapi_MemberOfConfig *config = ((sm_memberof_get_groups_data *) callback_data)->config;
    int rc = 0;

    if (slapi_is_shutting_down()) {
        rc = -1;
        goto bail;
    }

    if (config->maxgroups_reached) {
        rc = -1;
        goto bail;
    }

    if (!groupvals || !group_norm_vals) {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof",
                      "sm_memberof_get_groups_callback - NULL groupvals or group_norm_vals\n");
        rc = -1;
        goto bail;
    }
    /* get the DN of the group */
    group_ndn_val = slapi_value_new_string(group_ndn);
    /* group_dn is case-normalized */
    slapi_value_set_flags(group_ndn_val, SLAPI_ATTR_FLAG_NORMALIZED_CIS);

    /* check if e is the same as our original member entry */
    if (0 == sm_memberof_compare(((sm_memberof_get_groups_data *) callback_data)->config,
            &((sm_memberof_get_groups_data *) callback_data)->memberdn_val, &group_ndn_val)) {
        /* A recursive group caused us to find our original
         * entry we passed to memberof_get_groups().  We just
         * skip processing this entry. */
        slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof",
                      "sm_memberof_get_groups_callback - Group recursion"
                      " detected in %s\n",
                      group_ndn);
        slapi_value_free(&group_ndn_val);
        ((sm_memberof_get_groups_data *) callback_data)->use_cache = PR_FALSE;
        goto bail;
    }

    /* Have we been here before?  Note that we don't loop through all of the membership_slapiattrs
     * in config.  We only need this attribute for it's syntax so the comparison can be
     * performed.  Since all of the grouping attributes are validated to use the Dinstinguished
     * Name syntax, we can safely just use the first group_slapiattr. */
    if (slapi_valueset_find(
            ((sm_memberof_get_groups_data *) callback_data)->config->dn_syntax_attr, already_seen_ndn_vals, group_ndn_val)) {
        /* we either hit a recursive grouping, or an entry is
         * a member of a group through multiple paths.  Either
         * way, we can just skip processing this entry since we've
         * already gone through this part of the grouping hierarchy. */
        slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof",
                      "sm_memberof_get_groups_callback - Possible group recursion"
                      " detected in %s\n",
                      group_ndn);
        slapi_value_free(&group_ndn_val);
        ((sm_memberof_get_groups_data *) callback_data)->use_cache = PR_FALSE;
        goto bail;
    }

    /* if the group does not belong to an excluded subtree, adds it to the valueset */
    if (sm_memberof_entry_in_scope(config, group_sdn)) {
        /* Push group_dn_val into the valueset.  This memory is now owned
         * by the valueset. */
        slapi_valueset_add_value_ext(group_norm_vals, group_ndn_val, SLAPI_VALUE_FLAG_PASSIN);

        group_dn_val = slapi_value_new_string(group_dn);
        slapi_valueset_add_value_ext(groupvals, group_dn_val, SLAPI_VALUE_FLAG_PASSIN);

        group_nsuniqueid_val = slapi_value_new_string(group_nsuniqueid);
        slapi_valueset_add_value_ext(group_nsuniqueid_vals, group_nsuniqueid_val, SLAPI_VALUE_FLAG_PASSIN);

        /* push this ndn to detect group recursion */
        already_seen_ndn_val = slapi_value_new_string(group_ndn);
        slapi_valueset_add_value_ext(already_seen_ndn_vals, already_seen_ndn_val, SLAPI_VALUE_FLAG_PASSIN);

        config->current_maxgroup++;

        /* Check we did not hit the maxgroup */
        if ((config->maxgroups) &&
                (config->current_maxgroup >= config->maxgroups)) {
            char *msg = "slapi_memberof: sm_memberof_get_groups_callback result set truncated because of maxgroups limit (from computation)";
            sm_report_error_msg(config, msg);
            config->maxgroups_reached = PR_TRUE;
            rc = -1;
            goto bail;
        }
    }
    if (config->recurse && (config->maxgroups_reached == PR_FALSE)) {
        /* now recurse to find ancestors groups of e */
        sm_memberof_get_groups_r(((sm_memberof_get_groups_data *) callback_data)->config,
                group_sdn, callback_data);
    }

bail:
    return rc;
}

/* Should be called at shutdown only */
void
slapi_memberof_free_memberof_plugin_config()
{
    struct memberof_plugin_config *sav;
    if (memberof_config == NULL) {
        return;
    }
    sav = memberof_config;
    memberof_config = NULL;
    slapi_ch_array_free(sav->groupattrs);
    sav->groupattrs = NULL;

    slapi_ch_array_free(sav->include_scope);
    sav->include_scope = NULL;

    slapi_ch_array_free(sav->exclude_scope);
    sav->exclude_scope = NULL;

    slapi_ch_free_string(&sav->memberof_attr);
    slapi_ch_free((void **) &sav);
}

int
slapi_memberof_load_memberof_plugin_config()
{
    Slapi_PBlock *entry_pb = NULL;
    Slapi_Entry *config_entry = NULL;
    Slapi_DN *config_sdn = NULL;
    int rc;
    char *attrs[8];
    const char *allBackends = NULL;
    const char *skip_nested = NULL;
    const char *enabled = NULL;

    /* if already loaded, we are done */
    if (memberof_config) {
        return 0;
    }
    memberof_config = (struct memberof_plugin_config *) slapi_ch_calloc(1, sizeof (struct memberof_plugin_config));


    /* Retrieve the config entry */
    config_sdn = slapi_sdn_new_normdn_byref(MEMBEROF_CONFIG_DN);
    attrs[0] = MEMBEROF_GOUP_ATTR; /* e.g. member, uniquemember,... */
    attrs[1] = MEMBEROF_ATTR; /* e.g. memberof */
    attrs[2] = MEMBEROF_ALL_BACKENDS;
    attrs[3] = MEMBEROF_INCLUDE_SCOPE;
    attrs[4] = MEMBEROF_EXLCUDE_SCOPE;
    attrs[5] = MEMBEROF_SKIP_NESTED;
    attrs[6] = MEMBEROF_ENABLED;
    attrs[7] = NULL;
    rc = slapi_search_get_entry(&entry_pb, config_sdn, attrs, &config_entry, plugin_get_default_component_id());
    slapi_sdn_free(&config_sdn);

    if (rc != LDAP_SUCCESS || config_entry == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof",
                      "get_memberof_plugin_config - Failed to retrieve configuration entry %s: %d\n",
                      MEMBEROF_CONFIG_DN, rc);
        slapi_search_get_entry_done(&entry_pb);
        return (-1);
    }
    memberof_config->groupattrs = slapi_entry_attr_get_charray(config_entry, MEMBEROF_GOUP_ATTR);
    memberof_config->memberof_attr = slapi_entry_attr_get_charptr(config_entry, MEMBEROF_ATTR);
    allBackends = slapi_entry_attr_get_ref(config_entry, MEMBEROF_ALL_BACKENDS);
    if (allBackends) {
        if (strcasecmp(allBackends, "on") == 0) {
            memberof_config->all_backends = PR_TRUE;
        } else {
            memberof_config->all_backends = PR_FALSE;
        }
    } else {
        memberof_config->all_backends = PR_FALSE;
    }
    skip_nested = slapi_entry_attr_get_ref(config_entry, MEMBEROF_SKIP_NESTED);
    if (skip_nested) {
        if (strcasecmp(skip_nested, "on") == 0) {
            memberof_config->skip_nested = PR_TRUE;
        } else {
            memberof_config->skip_nested = PR_FALSE;
        }
    } else {
        memberof_config->skip_nested = PR_FALSE;
    }
    enabled = slapi_entry_attr_get_ref(config_entry, MEMBEROF_ENABLED);
    if (enabled) {
        if (strcasecmp(enabled, "on") == 0) {
            memberof_config->enabled = PR_TRUE;
        } else {
            memberof_config->enabled = PR_FALSE;
        }
    } else {
        memberof_config->enabled = PR_FALSE;
    }
    memberof_config->include_scope = slapi_entry_attr_get_charray(config_entry, MEMBEROF_INCLUDE_SCOPE);
    memberof_config->exclude_scope = slapi_entry_attr_get_charray(config_entry, MEMBEROF_EXLCUDE_SCOPE);

    slapi_search_get_entry_done(&entry_pb);
    return (rc);
}

static void
sm_report_error_msg(Slapi_MemberOfConfig *config, char* msg)
{
    int32_t len;

    if ((config->error_msg == NULL) || (config->errot_msg_lenght == 0) || (msg == NULL)) {
        return;
    }
    len = strlen(msg);
    if ((config->errot_msg_lenght - 1) < len) {
        len = config->errot_msg_lenght - 1;
    }
    strncpy(config->error_msg, msg, len);
}

static int
sm_entry_get_groups(Slapi_MemberOfConfig *config, Slapi_DN *member_sdn, char *memberof_attr, Slapi_ValueSet *groupvals, Slapi_ValueSet *nsuniqueidvals)
{
    Slapi_PBlock *member_pb = NULL;
    Slapi_PBlock *group_pb = NULL;
    char **groups_dn;
    Slapi_Entry *member_entry = NULL;
    Slapi_Entry *group_entry = NULL;
    Slapi_DN *group_sdn;
    Slapi_Value *sval;
    const char *nsuniqueid;
    char *attrs[2];
    int rc = 0;

    /* Retrieve the 'memberof' from the target entry */
    attrs[0] = memberof_attr;
    attrs[1] = NULL;
    rc = slapi_search_get_entry(&member_pb, member_sdn, attrs, &member_entry, plugin_get_default_component_id());
    if (rc != LDAP_SUCCESS || member_entry == NULL) {
        char *msg = "slapi_memberof: fails to retrieve the target entry";
        slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof",
                      "sm_entry_get_groups - Failed to retrieve target entry %s: %d\n",
                      slapi_sdn_get_ndn(member_sdn), rc);
        slapi_search_get_entry_done(&member_pb);
        sm_report_error_msg(config, msg);
        return (-1);
    }

    /* For each group that the target entry is memberof, retrieve its dn/nsuniqueid */
    groups_dn = slapi_entry_attr_get_charray(member_entry, memberof_attr);
    attrs[0] = "nsuniqueid";
    attrs[1] = NULL;
    for (size_t i = 0; groups_dn && groups_dn[i]; i++) {
        if ((config->maxgroups > 0) && (i >= config->maxgroups)) {
            char *msg = "slapi_memberof: result set truncated because of maxgroups limit (from memberof)";
            sm_report_error_msg(config, msg);
            config->maxgroups_reached = PR_TRUE;
            rc = -1;
            goto common;
        }
        group_pb = NULL;
        group_sdn = slapi_sdn_new_dn_byval(groups_dn[i]);
        rc = slapi_search_get_entry(&group_pb, group_sdn, attrs, &group_entry, plugin_get_default_component_id());
        if (rc != LDAP_SUCCESS || member_entry == NULL) {
            char *msg = "slapi_memberof: fails to retrieve a group";
            sm_report_error_msg(config, msg);

            slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof",
                          "sm_entry_get_groups - Failed to retrieve target entry %s: %d\n",
                          slapi_sdn_get_ndn(group_sdn), rc);
            slapi_sdn_free(&group_sdn);
            slapi_ch_array_free(groups_dn);
            rc = -1;
            goto common;
        }

        /* Now we retrieve a group */
        /* add its nsuniqueid to the valuset */
        nsuniqueid = slapi_entry_attr_get_ref(group_entry, (const char*) "nsuniqueid");
        if (nsuniqueid) {
            sval = slapi_value_new_string(nsuniqueid);
        }
        slapi_valueset_add_value_ext(nsuniqueidvals, sval, SLAPI_VALUE_FLAG_PASSIN);
        /* add its dn to the valuset */
        sval = slapi_value_new_string(slapi_sdn_get_ndn(group_sdn));
        slapi_valueset_add_value_ext(groupvals, sval, SLAPI_VALUE_FLAG_PASSIN);


        slapi_sdn_free(&group_sdn);
        slapi_search_get_entry_done(&group_pb);
    }

common:
    slapi_ch_array_free(groups_dn);
    slapi_search_get_entry_done(&member_pb);
    return rc;
}

static PRBool
sm_compare_memberof_config(const char *memberof_attr, char **groupattrs, PRBool all_backends, PRBool skip_nested, Slapi_DN **include_scope, Slapi_DN **exclude_scope, PRBool enabled_only)
{
    int32_t cnt1, cnt2;

    if ((memberof_config == NULL) || (memberof_config->enabled == PR_FALSE)) {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_compare_memberof_config: config not initialized or disabled\n");
        return PR_FALSE;
    }

    if (enabled_only) {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_compare_memberof_config: check the plugin is enabled that is %s\n",
                      memberof_config->enabled ? "SUCCEEDS" : "FAILS");
        if (memberof_config->enabled) {
            return PR_TRUE;
        } else {
            return PR_FALSE;
        }
    }

    /* Check direct flags */
    if ((all_backends != memberof_config->all_backends) || (skip_nested != memberof_config->skip_nested)) {
        /* If those flags do not match the current set of 'memberof' values is invalid */
        slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_compare_memberof_config: fails (allbackend %d vs %d, skip_nested %d vs %d)\n",
                      all_backends, memberof_config->all_backends, skip_nested, memberof_config->skip_nested);
        return PR_FALSE;
    }

    /* Check that we are dealing with the same membership attribute
     * e.g. 'memberof'
     */
    if ((memberof_attr == NULL) || (memberof_config->memberof_attr == NULL) || (strcasecmp(memberof_attr, memberof_config->memberof_attr))) {
        /* just be conservative, we should speak about the same attribute */
        slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof",
                      "sm_compare_memberof_config: fails memberof attribute differs (require %s vs config %s)\n",
                      memberof_attr ? memberof_attr : "NULL",
                      memberof_config->memberof_attr ? memberof_config->memberof_attr : NULL);
        return PR_FALSE;
    }

    /* Check that the membership attributes are identical to the one
     * in the memberof config. e.g. 'member', 'uniquemember'..
     */
    if (groupattrs == NULL) {
        /* This is a mandatory parameter */
        slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_compare_memberof_config: fails because requested group attributes is empty\n");
        return PR_FALSE;
    }
    for (cnt1 = 0; groupattrs[cnt1]; cnt1++) {
        if (charray_inlist(memberof_config->groupattrs, groupattrs[cnt1]) == 0) {
            slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof",
                          "sm_compare_memberof_config: fails because requested group attribute %s is not configured\n",
                          groupattrs[cnt1]);
            return PR_FALSE;
        }
    }
    for (cnt2 = 0; memberof_config->groupattrs && memberof_config->groupattrs[cnt2]; cnt2++);
    if (cnt1 != cnt2) {
        /* make sure groupattrs is not a subset of memberof_config->groupattrs */
        slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_compare_memberof_config: fails because number of requested group attributes differs from config\n");
        return PR_FALSE;
    }

    /* check Include scope that is optional */
    if (include_scope == NULL) {
        if (memberof_config->include_scope) {
            slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_compare_memberof_config: fails because requested include scope is empty that differs config\n");
            return PR_FALSE;
        }
    } else {
        if (memberof_config->include_scope == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_compare_memberof_config: fails because requested include scope is not empty that differs config\n");
            return PR_FALSE;
        }
    }
    /* here include scopes are both NULL or both not NULL */
    for (cnt1 = 0; include_scope && include_scope[cnt1]; cnt1++) {
        if (charray_inlist(memberof_config->include_scope, (char *) slapi_sdn_get_ndn(include_scope[cnt1])) == 0) {
            slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_compare_memberof_config: fails because requested include scope (%s) is not in config\n",
                          slapi_sdn_get_ndn(include_scope[cnt1]));
            return PR_FALSE;
        }
    }
    for (cnt2 = 0; memberof_config->include_scope && memberof_config->include_scope[cnt2]; cnt2++);
    if (cnt1 != cnt2) {
        /* make sure include_scope is not a subset of memberof_config->include_scope */
        slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_compare_memberof_config: fails because number of requested included scopes differs from config\n");
        return PR_FALSE;
    }

    /* check Exclude scope that is optional */
    if (exclude_scope == NULL) {
        if (memberof_config->exclude_scope) {
            slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_compare_memberof_config: fails because requested exclude scope is empty that differs config\n");
            return PR_FALSE;
        }
    } else {
        if (memberof_config->exclude_scope == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_compare_memberof_config: fails because requested exclude scope is not empty that differs config\n");
            return PR_FALSE;
        }
    }
    /* here exclude scopes are both NULL or both not NULL */
    for (cnt1 = 0; exclude_scope && exclude_scope[cnt1]; cnt1++) {
        if (charray_inlist(memberof_config->exclude_scope, (char *) slapi_sdn_get_ndn(exclude_scope[cnt1])) == 0) {
            slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_compare_memberof_config: fails because requested exclude scope (%s) is not in config\n",
                          slapi_sdn_get_ndn(exclude_scope[cnt1]));
            return PR_FALSE;
        }
    }
    for (cnt2 = 0; memberof_config->exclude_scope && memberof_config->exclude_scope[cnt2]; cnt2++);
    if (cnt1 != cnt2) {
        /* make sure exclude_scope is not a subset of memberof_config->exclude_scope */
        slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_compare_memberof_config: fails because number of requested included scopes differs from config\n");
        return PR_FALSE;
    }
    slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_compare_memberof_config: succeeds. requested options match config\n");
    return PR_TRUE;
}

static void
sm_add_ancestors_cbdata(sm_memberof_cached_value *ancestors, void *callback_data)
{
    Slapi_Value *sval;
    size_t val_index;
    Slapi_ValueSet *group_norm_vals_cbdata = *((sm_memberof_get_groups_data *) callback_data)->group_norm_vals;
    Slapi_ValueSet *group_vals_cbdata = *((sm_memberof_get_groups_data *) callback_data)->groupvals;
    Slapi_ValueSet *nsuniqueid_vals_cbdata = *((sm_memberof_get_groups_data *) callback_data)->nsuniqueidvals;
    Slapi_Value *memberdn_val = ((sm_memberof_get_groups_data *) callback_data)->memberdn_val;
    int added_group;
    PRBool empty_ancestor = PR_FALSE;

    for (val_index = 0, added_group = 0; ancestors[val_index].valid; val_index++) {
        /* For each of its ancestor (not already present) add it to callback_data */

        if (ancestors[val_index].group_ndn_val == NULL) {
            /* This is a node with no ancestor
             * ancestors should only contains this empty valid value
             * but just in case let the loop continue instead of a break
             */
            empty_ancestor = PR_TRUE;
            continue;
        }

        sval = slapi_value_new_string(ancestors[val_index].group_ndn_val);
        if (sval) {
            if (!slapi_valueset_find(
                    ((sm_memberof_get_groups_data *) callback_data)->config->dn_syntax_attr, group_norm_vals_cbdata, sval)) {
                /* This ancestor was not already present in the callback data
                 * => Add it to the callback_data
                 * Using slapi_valueset_add_value it consumes sval
                 * so do not free sval
                 */
                slapi_valueset_add_value_ext(group_norm_vals_cbdata, sval, SLAPI_VALUE_FLAG_PASSIN);
                sval = slapi_value_new_string(ancestors[val_index].group_dn_val);
                slapi_valueset_add_value_ext(group_vals_cbdata, sval, SLAPI_VALUE_FLAG_PASSIN);
                sval = slapi_value_new_string(ancestors[val_index].nsuniqueid_val);
                slapi_valueset_add_value_ext(nsuniqueid_vals_cbdata, sval, SLAPI_VALUE_FLAG_PASSIN);
                added_group++;
            } else {
                /* This ancestor was already present, free sval that will not be consumed */
                slapi_value_free(&sval);
            }
        }
    }
    slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof",
                  "sm_add_ancestors_cbdata: Ancestors of %s contained %ld groups. %d added. %s\n",
                  slapi_value_get_string(memberdn_val), val_index, added_group,
                  empty_ancestor ? "no ancestors" : "");
}

/*
 * Does a callback search of "type=dn" under the db suffix that "dn" is in,
 * unless all_backends is set, then we look at all the backends.  If "dn"
 * is a user, you'd want "type" to be "member".  If "dn" is a group, you
 * could want type to be either "member" or "memberOf" depending on the case.
 */
static int
sm_memberof_call_foreach_dn(Slapi_PBlock *pb __attribute__((unused)), Slapi_DN *sdn, Slapi_MemberOfConfig *config, char **types, plugin_search_entry_callback callback, void *callback_data, int *cached, PRBool use_grp_cache)
{
    Slapi_PBlock *search_pb = NULL;
    Slapi_DN *base_sdn = NULL;
    Slapi_Backend *be = NULL;
    char *escaped_filter_val;
    char *filter_str = NULL;
    char *cookie = NULL;
    int all_backends = config->allBackends;
    int dn_len = slapi_sdn_get_ndn_len(sdn);
    int free_it = 0;
    int rc = 0;

    *cached = 0;

    if (!sm_memberof_entry_in_scope(config, sdn)) {
        return (rc);
    }

    /* This flags indicates memberof_call_foreach_dn is called to retrieve ancestors (groups).
     * To improve performance, it can use a cache. (it will not in case of circular groups)
     * When this flag is true it means no circular group are detected (so far) so we can use the cache
     */
    if (use_grp_cache) {
        /* Here we will retrieve the ancestor of sdn.
         * The key access is the normalized sdn
         * This is done through recursive internal searches of parents
         * If the ancestors of sdn are already cached, just use
         * this value
         */
        sm_memberof_cached_value *ht_grp = NULL;
        const char *ndn = slapi_sdn_get_ndn(sdn);

        ht_grp = sm_ancestors_cache_lookup(config, (const void *) ndn);
        if (ht_grp) {
#if MEMBEROF_CACHE_DEBUG
            slapi_log_err(SLAPI_LOG_PLUGIN, "slapi_memberof", "sm_memberof_call_foreach_dn: Ancestors of %s already cached (%p)\n", ndn, ht_grp);
#endif
            sm_add_ancestors_cbdata(ht_grp, callback_data);
            *cached = 1;
            return (rc);
        }
    }
#if MEMBEROF_CACHE_DEBUG
    slapi_log_err(SLAPI_LOG_PLUGIN, "slapi_memberof", "sm_memberof_call_foreach_dn: Ancestors of %s not cached\n", slapi_sdn_get_ndn(sdn));
#endif

    /* Escape the dn, and build the search filter. */
    escaped_filter_val = slapi_escape_filter_value((char *) slapi_sdn_get_dn(sdn), dn_len);
    if (escaped_filter_val) {
        free_it = 1;
    } else {
        escaped_filter_val = (char *) slapi_sdn_get_dn(sdn);
    }

    for (size_t i = 0; (types[i] && (config->maxgroups_reached == PR_FALSE)); i++) {
        /* Triggers one internal search per membership attribute.
         * Assuming the attribute is indexed (eq), the search will
         * bypass the evaluation of the filter (nsslapd-search-bypass-filter-test)
         * against the candidates. This is important to bypass the filter
         * because on large valueset (static group) it is very expensive
         */
        filter_str = slapi_ch_smprintf("(%s=%s%s)", types[i], config->subtree_search ? "*" : "", escaped_filter_val);

        be = slapi_get_first_backend(&cookie);
        while ((config->maxgroups_reached == PR_FALSE) && be) {
            PRBool do_suffix_search = PR_TRUE;

            if (!all_backends) {
                be = slapi_be_select(sdn);
                if (be == NULL) {
                    break;
                }
            }
            if ((base_sdn = (Slapi_DN *) slapi_be_getsuffix(be, 0)) == NULL) {
                if (!all_backends) {
                    break;
                } else {
                    /* its ok, goto the next backend */
                    be = slapi_get_next_backend(cookie);
                    continue;
                }
            }

            search_pb = slapi_pblock_new();
            if ((config->entryScopes && config->entryScopes[0]) ||
                    (config->entryScopeExcludeSubtrees && config->entryScopeExcludeSubtrees[0])) {
                if (sm_memberof_entry_in_scope(config, base_sdn)) {
                    /* do nothing, entry scope is spanning
                     * multiple suffixes, start at suffix */
                } else if (config->entryScopes) {
                    for (size_t i = 0; config->entryScopes[i]; i++) {
                        if (slapi_sdn_issuffix(config->entryScopes[i], base_sdn)) {
                            /* Search each include scope */
                            slapi_search_internal_set_pb(search_pb, slapi_sdn_get_dn(config->entryScopes[i]),
                                    LDAP_SCOPE_SUBTREE, filter_str, 0, 0, 0, 0,
                                    plugin_get_default_component_id(), 0);
                            slapi_search_internal_callback_pb(search_pb, callback_data, 0, callback, 0);
                            /* We already did the search for this backend, don't
                             * do it again when we fall through */
                            do_suffix_search = PR_FALSE;
                        }
                    }
                } else if (!all_backends) {
                    slapi_pblock_destroy(search_pb);
                    break;
                } else {
                    /* its ok, goto the next backend */
                    be = slapi_get_next_backend(cookie);
                    slapi_pblock_destroy(search_pb);
                    continue;
                }
            }

            if (do_suffix_search) {
                slapi_search_internal_set_pb(search_pb, slapi_sdn_get_dn(base_sdn),
                        LDAP_SCOPE_SUBTREE, filter_str, 0, 0, 0, 0,
                        plugin_get_default_component_id(), 0);
                slapi_search_internal_callback_pb(search_pb, callback_data, 0, callback, 0);
                slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
                if (rc != LDAP_SUCCESS) {
                    slapi_pblock_destroy(search_pb);
                    break;
                }
            }

            if (!all_backends) {
                slapi_pblock_destroy(search_pb);
                break;
            }

            be = slapi_get_next_backend(cookie);
            slapi_pblock_destroy(search_pb);
        }
        slapi_ch_free((void **) &cookie);
        slapi_ch_free_string(&filter_str);
    }

    if (free_it) {
        slapi_ch_free_string(&escaped_filter_val);
    }
    return rc;
}

static int
sm_memberof_get_groups_r(Slapi_MemberOfConfig *config, Slapi_DN *member_sdn, sm_memberof_get_groups_data *data)
{
    Slapi_ValueSet *groupvals = slapi_valueset_new();
    Slapi_ValueSet *group_norm_vals = slapi_valueset_new();
    Slapi_ValueSet *nsuniqueidvals = slapi_valueset_new();
    Slapi_Value *member_ndn_val =
            slapi_value_new_string(slapi_sdn_get_ndn(member_sdn));
    int rc;
    int cached = 0;

    slapi_value_set_flags(member_ndn_val, SLAPI_ATTR_FLAG_NORMALIZED_CIS);

    sm_memberof_get_groups_data member_data = {config, member_ndn_val, &groupvals, &group_norm_vals, &nsuniqueidvals, data->already_seen_ndn_vals, data->use_cache};

    /* Search for any grouping attributes that point to memberdn.
     * For each match, add it to the list, recurse and do same search */
#if MEMBEROF_CACHE_DEBUG
    slapi_log_err(SLAPI_LOG_ERR, "slapi_memberof", "sm_memberof_get_groups_r: Ancestors of %s\n", slapi_sdn_get_dn(member_sdn));
#endif
    rc = sm_memberof_call_foreach_dn(NULL, member_sdn, config, config->groupattrs,
                                     sm_memberof_get_groups_callback, &member_data,
                                     &cached, member_data.use_cache);

    sm_merge_ancestors(&member_ndn_val, &member_data, data);
    if (!cached && member_data.use_cache)
        sm_cache_ancestors(config, &member_ndn_val, &member_data);

    slapi_value_free(&member_ndn_val);
    slapi_valueset_free(groupvals);
    slapi_valueset_free(group_norm_vals);
    slapi_valueset_free(nsuniqueidvals);


    return rc;
}

static PRIntn
sm_memberof_hash_compare_keys(const void *v1, const void *v2)
{
    PRIntn rc;
    if (0 == strcasecmp((const char *) v1, (const char *) v2)) {
        rc = 1;
    } else {
        rc = 0;
    }
    return rc;
}

static PRIntn
sm_memberof_hash_compare_values(const void *v1, const void *v2)
{
    PRIntn rc;
    if ((char *) v1 == (char *) v2) {
        rc = 1;
    } else {
        rc = 0;
    }
    return rc;
}

/*
 *  Hashing function using Bernstein's method
 */
static PLHashNumber
sm_memberof_hash_fn(const void *key)
{
    PLHashNumber hash = 5381;
    unsigned char *x = (unsigned char *) key;
    int c;

    while ((c = *x++)) {
        hash = ((hash << 5) + hash) ^ c;
    }
    return hash;
}

/* allocates the plugin hashtable
 * This hash table is used by operation and is protected from
 * concurrent operations with the memberof_lock (if not usetxn, memberof_lock
 * is not implemented and the hash table will be not used.
 *
 * The hash table contains all the DN of the entries for which the memberof
 * attribute has been computed/updated during the current operation
 *
 * hash table should be empty at the beginning and end of the plugin callback
 */
static PLHashTable *
sm_hashtable_new(int usetxn)
{
    if (!usetxn) {
        return NULL;
    }

    return PL_NewHashTable(1000,
                           sm_memberof_hash_fn,
                           sm_memberof_hash_compare_keys,
                           sm_memberof_hash_compare_values, NULL, NULL);
}

/* this function called for each hash node during hash destruction */
static PRIntn
sm_ancestor_hashtable_remove(PLHashEntry *he, PRIntn index __attribute__((unused)), void *arg __attribute__((unused)))
{
    sm_memberof_cached_value *group_ancestor_array;

    if (he == NULL) {
        return HT_ENUMERATE_NEXT;
    }
    group_ancestor_array = (sm_memberof_cached_value *) he->value;
    sm_ancestor_hashtable_entry_free(group_ancestor_array);
    slapi_ch_free((void **) &group_ancestor_array);

    return HT_ENUMERATE_REMOVE;
}

static void
sm_ancestor_hashtable_empty(Slapi_MemberOfConfig *config, char *msg)
{
    if (config->ancestors_cache) {
        PL_HashTableEnumerateEntries(config->ancestors_cache, sm_ancestor_hashtable_remove, msg);
    }
}

int
slapi_memberof(Slapi_MemberOfConfig *config, Slapi_DN *member_sdn, Slapi_MemberOfResult *result)
{
    Slapi_ValueSet *groupvals;
    Slapi_ValueSet *nsuniqueidvals;
    Slapi_ValueSet *group_norm_vals;
    Slapi_ValueSet *already_seen_ndn_vals;
    Slapi_Value *memberdn_val;
    Slapi_Attr *membership_slapiattrs;
    int32_t rc = 0;

    if (config == NULL || member_sdn == NULL || result == NULL) {
        return -1;
    }
    config->maxgroups_reached = PR_FALSE;
    config->current_maxgroup = 0;
    if (config->error_msg) {
        memset(config->error_msg, 0, config->errot_msg_lenght);
        strcpy(config->error_msg, "no error msg");
    }
    groupvals = slapi_valueset_new();
    nsuniqueidvals = slapi_valueset_new();
    if ((config->flag == MEMBEROF_REUSE_ONLY) && sm_compare_memberof_config(NULL,
            NULL,
            PR_FALSE,
            PR_FALSE,
            NULL,
            NULL, PR_TRUE)) {
        /* Whatever the configuration of memberof plugin as long
         * as it is enabled, return the groups referenced in the target entry
         */
        rc = sm_entry_get_groups(config, member_sdn, "memberof", groupvals, nsuniqueidvals);
    } else if ((config->flag == MEMBEROF_REUSE_IF_POSSIBLE) && sm_compare_memberof_config("memberof",
            config->groupattrs,
            config->allBackends,
            !config->recurse,
            config->entryScopes,
            config->entryScopeExcludeSubtrees, PR_FALSE)) {
        /* If the configuration of memberof plugin match the requested config
         * (and the plugin is enabled), return the groups referenced in the target entry
         */
        rc = sm_entry_get_groups(config, member_sdn, "memberof", groupvals, nsuniqueidvals);
    } else {
        /* This is RECOMPUTE mode or memberof plugin config does not satisfy
         * the requested config, then recompute the membership
         */
        group_norm_vals = slapi_valueset_new();
        already_seen_ndn_vals = slapi_valueset_new();
        memberdn_val = slapi_value_new_string(slapi_sdn_get_ndn(member_sdn));
        membership_slapiattrs = slapi_attr_new();
        slapi_attr_init(membership_slapiattrs, config->groupattrs[0]);
        config->memberof_attr = "memberof"; /* used to check if the shortcut of memberof plugin is possible */
        config->dn_syntax_attr = membership_slapiattrs; /* all groupattrs are DN syntax, get the syntax from any of them */
        config->maxgroups_reached = PR_FALSE;

        slapi_value_set_flags(memberdn_val, SLAPI_ATTR_FLAG_NORMALIZED_CIS);

        config->ancestors_cache = sm_hashtable_new(1);
        sm_memberof_get_groups_data data = {config, memberdn_val, &groupvals, &group_norm_vals, &nsuniqueidvals, &already_seen_ndn_vals, PR_TRUE};

        rc = sm_memberof_get_groups_r(config, member_sdn, &data);

        slapi_attr_free(&membership_slapiattrs);
        slapi_value_free(&memberdn_val);
        slapi_valueset_free(group_norm_vals);
        slapi_valueset_free(already_seen_ndn_vals);

        if (config->ancestors_cache) {
            sm_ancestor_hashtable_empty(config, "memberof_free_config empty group_ancestors_hashtable");
            PL_HashTableDestroy(config->ancestors_cache);
            config->ancestors_cache = NULL;
        }
    }

    result->dn_vals = groupvals;
    result->nsuniqueid_vals = nsuniqueidvals;

    return rc;
}

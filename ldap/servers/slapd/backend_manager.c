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

/* backend_manager.c - routines for dealing with back-end databases */

#include "slap.h"

#define BACKEND_GRAB_SIZE 10

/* JCM - searching the backend array is linear... */

static int defsize = SLAPD_DEFAULT_SIZELIMIT;
static int deftime = SLAPD_DEFAULT_TIMELIMIT;
static int nbackends = 0;
static Slapi_Backend **backends = NULL;
static size_t maxbackends = 0;

Slapi_Backend *
slapi_be_new(const char *type, const char *name, int isprivate, int logchanges)
{
    Slapi_Backend *be;
    int i;

    /* should add some locking here to prevent concurrent access */
    if (nbackends == maxbackends) {
        int oldsize = maxbackends;
        maxbackends += BACKEND_GRAB_SIZE;
        backends = (Slapi_Backend **)slapi_ch_realloc((char *)backends, maxbackends * sizeof(Slapi_Backend *));
        for (i = oldsize; i < maxbackends; i++){
            /* init the new be pointers so we can track empty slots */
            backends[i] = NULL;
        }
    }


    be = (Slapi_Backend *)slapi_ch_calloc(1, sizeof(Slapi_Backend));
    be->be_lock = slapi_new_rwlock();
    be_init(be, type, name, isprivate, logchanges, defsize, deftime);

    for (size_t i = 0; i < maxbackends; i++) {
        if (backends[i] == NULL) {
            backends[i] = be;
            nbackends++;
            break;
        }
    }

    slapi_log_err(SLAPI_LOG_TRACE, "slapi_be_new",
                  "Added new backend name [%s] type [%s] nbackends [%d]\n",
                  name, type, nbackends);

    return (be);
}

void
slapi_be_stopping(Slapi_Backend *be)
{
    int i;

    PR_Lock(be->be_state_lock);
    for (i = 0; ((i < maxbackends) && backends[i] != be); i++)
        ;

    PR_ASSERT(i < maxbackends);

    backends[i] = NULL;
    be->be_state = BE_STATE_DELETED;
    if (be->be_lock != NULL) {
        slapi_destroy_rwlock(be->be_lock);
        be->be_lock = NULL;
    }

    nbackends--;
    PR_Unlock(be->be_state_lock);
}


void
slapi_be_free(Slapi_Backend **be)
{
    be_done(*be);
    slapi_ch_free((void **)be);
    *be = NULL;
}

static int
be_plgfn_unwillingtoperform(Slapi_PBlock *pb)
{
    send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL, "Operation on Directory Specific Entry not allowed", 0, NULL);
    return -1;
}

/* JCM - Seems rather DSE specific... why's it here?... Should be in fedse.c... */

Slapi_Backend *
be_new_internal(struct dse *pdse, const char *type, const char *name, struct slapdplugin *plugininfo)
{
    Slapi_Backend *be = slapi_be_new(type, name, 1 /* Private */, 0 /* Do Not Log Changes */);
    be->be_database = plugininfo;
    be->be_database->plg_private = (void *)pdse;
    be->be_database->plg_bind = &dse_bind;
    be->be_database->plg_unbind = &dse_unbind;
    be->be_database->plg_search = &dse_search;
    be->be_database->plg_next_search_entry = &dse_next_search_entry;
    be->be_database->plg_search_results_release = &dse_search_set_release;
    be->be_database->plg_prev_search_results = &dse_prev_search_results;
    be->be_database->plg_compare = &dse_compare;
    be->be_database->plg_modify = &dse_modify;
    be->be_database->plg_modrdn = &be_plgfn_unwillingtoperform;
    be->be_database->plg_add = &dse_add;
    be->be_database->plg_delete = &dse_delete;
    be->be_database->plg_abandon = &be_plgfn_unwillingtoperform;
    be->be_database->plg_cleanup = dse_deletedse;
    /* All the other function pointers default to NULL */
    return be;
}

/*
 * Rule: before coming to this point, slapi_be_Wlock(be) must be acquired.
 */
void
be_replace_dse_internal(Slapi_Backend *be, struct dse *pdse)
{
    be->be_database->plg_private = (void *)pdse;
}

Slapi_Backend *
slapi_get_first_backend(char **cookie)
{
    int i;

    for (i = 0; i < maxbackends; i++) {
        if (backends[i] && (backends[i]->be_state != BE_STATE_DELETED)) {
            *cookie = (char *)slapi_ch_malloc(sizeof(int));
            memcpy(*cookie, &i, sizeof(int));
            return backends[i];
        }
    }

    return NULL;
}

Slapi_Backend *
slapi_get_next_backend(char *cookie)
{
    int i, last_be;
    if (cookie == NULL) {
        slapi_log_err(SLAPI_LOG_ARGS, "slapi_get_next_backend", "NULL argument\n");
        return NULL;
    }

    last_be = *(int *)cookie;

    if (last_be < 0 || last_be >= maxbackends) {
        slapi_log_err(SLAPI_LOG_ARGS, "slapi_get_next_backend", "argument out of range\n");
        return NULL;
    }

    if (last_be == maxbackends - 1)
        return NULL; /* done */

    for (i = last_be + 1; i < maxbackends; i++) {
        if (backends[i] && (backends[i]->be_state != BE_STATE_DELETED)) {
            memcpy(cookie, &i, sizeof(int));
            return backends[i];
        }
    }

    return NULL;
}

Slapi_Backend *
g_get_user_backend(int n)
{
    int i, useri;

    useri = 0;
    for (i = 0; i < maxbackends; i++) {
        if ((backends[i] == NULL) || (backends[i]->be_private == 1)) {
            continue;
        }

        if (useri == n) {
            if (backends[i]->be_state != BE_STATE_DELETED)
                return backends[i];
            else
                return NULL;
        }
        useri++;
    }
    return NULL;
}

void
g_set_deftime(int val)
{
    deftime = val;
}

void
g_set_defsize(int val)
{
    defsize = val;
}

int
g_get_deftime()
{
    return deftime;
}

int
g_get_defsize()
{
    return defsize;
}

int
strtrimcasecmp(const char *s1, const char *s2)
{
    char *s1bis, *s2bis;
    int len_s1 = 0, len_s2 = 0;

    if (((s1 == NULL) && (s2 != NULL)) || ((s2 == NULL) && (s1 != NULL)))
        return 1;

    if ((s1 == NULL) && (s2 == NULL))
        return 0;

    while (*s1 == ' ')
        s1++;

    while (*s2 == ' ')
        s2++;

    s1bis = (char *)s1;
    while ((*s1bis != ' ') && (*s1bis != 0)) {
        len_s1++;
        s1bis++;
    }

    s2bis = (char *)s2;
    while ((*s2bis != ' ') && (*s2bis != 0)) {
        len_s2++;
        s2bis++;
    }

    if (len_s2 != len_s1)
        return 1;

    return strncasecmp(s1, s2, len_s1);
}
/*
 * Find the backend of the given type.
 */
Slapi_Backend *
slapi_be_select_by_instance_name(const char *name)
{
    for (size_t i = 0; i < maxbackends; i++) {
        if (backends[i] && (backends[i]->be_state != BE_STATE_DELETED) &&
            strtrimcasecmp(backends[i]->be_name, name) == 0) {
            return backends[i];
        }
    }
    return NULL;
}

void
be_cleanupall()
{
    for (size_t i = 0; i < maxbackends; i++) {
        if (backends[i] && (backends[i]->be_state == BE_STATE_STOPPED || backends[i]->be_state == BE_STATE_DELETED)) {
            if (backends[i]->be_cleanup != NULL) {
                Slapi_PBlock *pb = slapi_pblock_new();
                slapi_pblock_set(pb, SLAPI_PLUGIN, backends[i]->be_database);
                slapi_pblock_set(pb, SLAPI_BACKEND, backends[i]);
                (*backends[i]->be_cleanup)(pb);
                slapi_pblock_destroy(pb);
            }
            slapi_be_free(&backends[i]);
        }
    }
    slapi_ch_free((void **)&backends);
}

void
be_unbindall(Connection *conn, Operation *op)
{
    for (size_t i = 0; i < maxbackends; i++) {
        if (backends[i] && (backends[i]->be_unbind != NULL)) {
            /* This is the modern, and faster way to do pb memset(0)
             * It also doesn't trigger the HORRIBLE stack overflows I found ...
             */
            Slapi_PBlock *pb = slapi_pblock_new();
            pblock_init_common(pb, backends[i], conn, op);

            if (plugin_call_plugins(pb, SLAPI_PLUGIN_PRE_UNBIND_FN) == 0) {
                int rc = 0;
                slapi_pblock_set(pb, SLAPI_PLUGIN, backends[i]->be_database);
                if (backends[i]->be_state != BE_STATE_DELETED && backends[i]->be_unbind != NULL) {
                    rc = (*backends[i]->be_unbind)(pb);
                }
                slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &rc);
                (void)plugin_call_plugins(pb, SLAPI_PLUGIN_POST_UNBIND_FN);
            }
            /*
             * pblock_destroy implicitly frees operation, so we need to NULL it, else
             * we cause a use-after-free.
             */
            slapi_pblock_set(pb, SLAPI_OPERATION, NULL);

            slapi_pblock_destroy(pb);
        }
    }
}

int
be_nbackends_public()
{
    int i;
    int n = 0;
    for (i = 0; i < maxbackends; i++) {
        if (backends[i] &&
            (backends[i]->be_state != BE_STATE_DELETED) &&
            (!backends[i]->be_private)) {
            n++;
        }
    }
    return n;
}

/* backend instance management */

void
slapi_be_Rlock(Slapi_Backend *be)
{
    slapi_rwlock_rdlock(be->be_lock);
}

void
slapi_be_Wlock(Slapi_Backend *be)
{
    slapi_rwlock_wrlock(be->be_lock);
}

void
slapi_be_Unlock(Slapi_Backend *be)
{
    slapi_rwlock_unlock(be->be_lock);
}

/*
 * lookup instance names by suffix.
 * if isexact == 0: returns instances including ones that associates with
 *                    its sub suffixes.
 *                    e.g., suffix: "o=<suffix>" is given, these are returned:
 *                          suffixes: o=<suffix>, ou=<ou>,o=<suffix>, ...
 *                          instances: inst of "o=<suffix>",
 *                                     inst of "ou=<ou>,o=<suffix>",
 *                                        ...
 * if isexact != 0: returns an instance that associates with the given suffix
 *                    e.g., suffix: "o=<suffix>" is given, these are returned:
 *                          suffixes: "o=<suffix>"
 *                          instances: inst of "o=<suffix>"
 * Note: if suffixes
 */
int
slapi_lookup_instance_name_by_suffix(char *suffix,
                                     char ***suffixes,
                                     char ***instances,
                                     int isexact)
{
    Slapi_Backend *be = NULL;
    char *cookie = NULL;
    const char *thisdn;
    int thisdnlen;
    int suffixlen;
    int rval = -1;

    if (instances == NULL)
        return rval;

    PR_ASSERT(suffix);

    rval = 0;
    suffixlen = strlen(suffix);
    cookie = NULL;
    be = slapi_get_first_backend(&cookie);
    while (be) {
        if (NULL == be->be_suffix) {
            be = (backend *)slapi_get_next_backend(cookie);
            continue;
        }
        thisdn = slapi_sdn_get_ndn(be->be_suffix);
        thisdnlen = slapi_sdn_get_ndn_len(be->be_suffix);
        if (isexact ? suffixlen != thisdnlen : suffixlen > thisdnlen) {
            be = (backend *)slapi_get_next_backend(cookie);
            continue;
        }
        if (isexact ? (!slapi_UTF8CASECMP(suffix, (char *)thisdn)) : (!slapi_UTF8CASECMP(suffix, (char *)thisdn + thisdnlen - suffixlen))) {
            charray_add(instances, slapi_ch_strdup(be->be_name));
            if (suffixes) {
                charray_add(suffixes, slapi_ch_strdup(thisdn));
            }
        }
        be = (backend *)slapi_get_next_backend(cookie);
    }
    slapi_ch_free((void **)&cookie);

    return rval;
}

/*
 * lookup instance names by included suffixes and excluded suffixes.
 *
 * Get instance names associated with the given included suffixes
 * as well as the excluded suffixes.
 * Subtract the excluded instances from the included instance.
 * Assign the result to instances.
 */
int
slapi_lookup_instance_name_by_suffixes(char **included, char **excluded, char ***instances)
{
    char **incl_instances, **excl_instances;
    char **p;
    int rval = -1;

    if (instances == NULL)
        return rval;

    *instances = NULL;
    incl_instances = NULL;
    for (p = included; p && *p; p++) {
        if (slapi_lookup_instance_name_by_suffix(*p, NULL, &incl_instances, 0) < 0)
            return rval;
    }

    excl_instances = NULL;
    for (p = excluded; p && *p; p++) {
        /* okay to be empty */
        slapi_lookup_instance_name_by_suffix(*p, NULL, &excl_instances, 0);
    }

    rval = 0;
    if (excl_instances) {
        charray_subtract(incl_instances, excl_instances, NULL);
        charray_free(excl_instances);
    }
    *instances = incl_instances;
    return rval;
}

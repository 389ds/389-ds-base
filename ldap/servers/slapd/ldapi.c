/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include "slap.h"
#include "getsocketpeer.h"

#if defined(ENABLE_LDAPI)

struct ldapi_mapping {
    char *dn;
    uid_t uid;
    gid_t gid;
    struct ldapi_mapping *next;
};

static struct ldapi_mapping *ldapi_mappings = NULL;
static Slapi_RWLock *dn_mapping_lock = NULL;

void
initialize_ldapi_auth_dn_mappings(slapi_ldapi_state reload)
{
    Slapi_PBlock *search_pb = NULL;
    Slapi_Entry **entries = NULL;
    struct ldapi_mapping *mappings = NULL;
    char *base_dn = config_get_ldapi_mapping_base_dn();
    char *filter = "(|(objectclass=nsLDAPIAuthMap)(objectclass=nsLDAPIFixedAuthMap))";
    int32_t result = 0;

    if (base_dn == NULL) {
        /* nothing to do */
        return;
    }

    if (reload) {
        /* Free the current mapping and rebuild it */
        free_ldapi_auth_dn_mappings(LDAPI_RELOAD);
    } else {
        /* Server is starting up, init mutex */
        if ((dn_mapping_lock = slapi_new_rwlock_prio(1 /* priority on writers */)) == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "initialize_ldapi_auth_dn_mappings",
                          "Cannot create new lock.  error %d (%s)\n",
                          result, strerror(result));
            exit(-1);
        }
    }

    /* Get all the mapping entries */
    search_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(search_pb, base_dn, LDAP_SCOPE_SUBTREE, filter,
            NULL, 0, NULL, NULL, (void *)plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);
    if (result == LDAP_SUCCESS) {
        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        slapi_rwlock_wrlock(dn_mapping_lock);
        for (size_t i = 0; entries && entries[i]; i++) {
            const char *username = slapi_entry_attr_get_ref(entries[i], CONFIG_LDAPI_AUTH_USERNAME_ATTRIBUTE);
            uid_t user_uid = slapi_entry_attr_get_uint(entries[i], "uidNumber");
            gid_t user_gid = slapi_entry_attr_get_uint(entries[i], "gidNumber");

            if (user_uid && user_gid) {
                /* There is a fixed uid/gid use that */
            	struct ldapi_mapping *new_mapping = (struct ldapi_mapping *)slapi_ch_calloc(1, sizeof(struct ldapi_mapping));
                new_mapping->uid = user_uid;
                new_mapping->gid = user_gid;
                new_mapping->dn = slapi_entry_attr_get_charptr(entries[i], CONFIG_LDAPI_AUTH_DN_ATTRIBUTE);
                if (mappings == NULL) {
                    ldapi_mappings = new_mapping; /* Set the head */
                    mappings = new_mapping;
                } else {
                    mappings->next = new_mapping;
                    mappings = mappings->next;
                }
            } else {
                /* Get the uid/gid from the system */
                struct passwd *pws;
                pws = getpwnam(username);
                if (pws) {
                    struct ldapi_mapping *new_mapping = (struct ldapi_mapping *)slapi_ch_calloc(1, sizeof(struct ldapi_mapping));
                    new_mapping->uid = pws->pw_uid;
                    new_mapping->gid = pws->pw_gid;
                    new_mapping->dn = slapi_entry_attr_get_charptr(entries[i], CONFIG_LDAPI_AUTH_DN_ATTRIBUTE);
                    if (mappings == NULL) {
                        ldapi_mappings = new_mapping; /* Set the head */
                        mappings = new_mapping;
                    } else {
                        mappings->next = new_mapping;
                        mappings = mappings->next;
                    }
                } else {
                    slapi_log_err(SLAPI_LOG_WARNING, "initialize_ldapi_auth_dn_mappings",
                            "System username (%s) in entry (%s) was not found and will be ignored.\n",
                            username, slapi_entry_get_dn(entries[i]));
                }
            }
        }
        slapi_rwlock_unlock(dn_mapping_lock);
    }
    slapi_free_search_results_internal(search_pb);
    slapi_pblock_destroy(search_pb);;
    slapi_ch_free_string(&base_dn);
}

void
free_ldapi_auth_dn_mappings(int32_t shutdown)
{
    struct ldapi_mapping *mapping = ldapi_mappings;
    struct ldapi_mapping *next_mapping = NULL;

    slapi_rwlock_wrlock(dn_mapping_lock);

    while (mapping) {
        next_mapping = mapping->next;
        slapi_ch_free_string(&mapping->dn);
        slapi_ch_free((void **)&mapping);
        mapping = next_mapping;
    }

    slapi_rwlock_unlock(dn_mapping_lock);

    if (shutdown == LDAPI_SHUTDOWN) {
        slapi_destroy_rwlock(dn_mapping_lock);
    }
}

int32_t
slapd_identify_local_user(Connection *conn)
{
    uid_t uid = 0;
    gid_t gid = 0;
    int32_t ret = -1;

    conn->c_local_valid = 0;

    if (0 == slapd_get_socket_peer(conn->c_prfd, &uid, &gid)) {
        conn->c_local_uid = uid;
        conn->c_local_gid = gid;
        conn->c_local_valid = 1;

        ret = 0;
    }

    return ret;
}

#if defined(ENABLE_AUTOBIND)
int32_t
slapd_bind_local_user(Connection *conn)
{
    uid_t uid = conn->c_local_uid;
    gid_t gid = conn->c_local_gid;
    char *auth_dn = NULL;
    int32_t ret = -1;

    uid_t proc_uid = geteuid();
    gid_t proc_gid = getegid();

    if (!conn->c_local_valid) {
        goto done;
    }

    /* observe configuration for auto binding */
    /* bind at all? */
    if (config_get_ldapi_bind_switch()) {
        /* map users to a dn root may also map to an entry */
        /* require real entry? */
        if (config_get_ldapi_map_entries()) {
            /*
             * First check if the uid/gid maps to one of our LDAPI auth mappings
             */
            slapi_rwlock_rdlock(dn_mapping_lock);
            for (struct ldapi_mapping *mapping = ldapi_mappings; mapping; mapping = mapping->next) {
                if (mapping->uid == uid && mapping->gid == gid) {
                    /* We found this user in our mappings, make sure the entry
                     * actually exists, and check if it's locked out before binding */
                    Slapi_Entry *e = NULL;
                    auth_dn = slapi_ch_strdup(mapping->dn);
                    Slapi_DN sdn;

                    slapi_sdn_init_dn_byref(&sdn, auth_dn);
                    slapi_search_internal_get_entry(&sdn, NULL, &e, plugin_get_default_component_id());
                    slapi_sdn_done(&sdn);
                    if (e) {
                        ret = slapi_check_account_lock(0, e, 0, 0, 0);
                        if (ret == 0) {
                            /* All looks good, now do the bind */
                            bind_credentials_set_nolock(conn, SLAPD_AUTH_OS, auth_dn,
                                                        NULL, NULL, NULL, NULL);
                        }
                        /* all done here */
                        slapi_rwlock_unlock(dn_mapping_lock);
                        goto done;
                    } else {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bind_local_user",
                                "LDAPI auth mapping for (%s) points to entry that does not exist\n",
                                auth_dn);
                        break;
                    }
                }
            }
            slapi_rwlock_unlock(dn_mapping_lock);

            /* get uid type to map to (e.g. uidNumber) */
            char *utype = config_get_ldapi_uidnumber_type();
            /* get gid type to map to (e.g. gidNumber) */
            char *gtype = config_get_ldapi_gidnumber_type();
            /* get base dn for search */
            char *base_dn = config_get_ldapi_search_base_dn();

            /* search vars */
            Slapi_PBlock *search_pb = NULL;
            Slapi_Entry **entries = NULL;
            int32_t result;

            /* filter manipulation vars */
            char *one_type = NULL;
            char *filter_tpl = NULL;
            char *filter = NULL;

            /* create filter, matching whatever is given */
            if (utype && gtype) {
                filter_tpl = "(&(%s=%u)(%s=%u))";
            } else {
                if (utype || gtype) {
                    filter_tpl = "(%s=%u)";
                    if (utype) {
                        one_type = utype;
                    } else {
                        one_type = gtype;
                    }
                } else {
                    goto entry_map_free;
                }
            }

            if (one_type) {
                if (one_type == utype) {
                    filter = slapi_ch_smprintf(filter_tpl, utype, uid);
                } else {
                    filter = slapi_ch_smprintf(filter_tpl, gtype, gid);
                }
            } else {
                filter = slapi_ch_smprintf(filter_tpl, utype, uid, gtype, gid);
            }

            /* search for single entry matching types */
            search_pb = slapi_pblock_new();
            slapi_search_internal_set_pb(
                search_pb,
                base_dn,
                LDAP_SCOPE_SUBTREE,
                filter,
                NULL, 0, NULL, NULL,
                (void *)plugin_get_default_component_id(),
                0);

            slapi_search_internal_pb(search_pb);
            slapi_pblock_get(
                search_pb,
                SLAPI_PLUGIN_INTOP_RESULT,
                &result);
            if (LDAP_SUCCESS == result)
                slapi_pblock_get(
                    search_pb,
                    SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                    &entries);

            if (entries) {
                /* zero or multiple entries fail */
                if (entries[0] && 0 == entries[1]) {
                    /* observe account locking */
                    ret = slapi_check_account_lock(
                        0, /* pb not req */
                        entries[0],
                        0, /* no response control */
                        0, /* don't check password policy */
                        0  /* don't send ldap result */
                        );

                    if (0 == ret) {
                        auth_dn = slapi_ch_strdup(slapi_entry_get_ndn(entries[0]));
                        bind_credentials_set_nolock(
                            conn,
                            SLAPD_AUTH_OS,
                            auth_dn,
                            NULL, NULL,
                            NULL, entries[0]);
                    }
                }
            }

        entry_map_free:
            /* auth_dn consumed by bind creds set */
            slapi_free_search_results_internal(search_pb);
            slapi_pblock_destroy(search_pb);
            slapi_ch_free_string(&filter);
            slapi_ch_free_string(&utype);
            slapi_ch_free_string(&gtype);
            slapi_ch_free_string(&base_dn);
        }

        /*
         * We map the current process uid also to directory manager.
         * This is secure as it requires local machine OR same-container volume
         * access and the correct uid access. If you have access to the uid/gid
         * and are on the same machine you could always just reset the rootdn hashes
         * anyway ... so this is no reduction in security.
         */

        if (ret && (0 == uid || proc_uid == uid || proc_gid == gid)) {
            /* map unix root (uidNumber:0)? */
            char *root_dn = config_get_ldapi_root_dn();

            if (root_dn) {
                Slapi_PBlock *entry_pb = NULL;
                Slapi_DN *edn = slapi_sdn_new_dn_byref(slapi_dn_normalize(root_dn));
                Slapi_Entry *e = NULL;

                /* root might be locked too! :) */
                ret = slapi_search_get_entry(&entry_pb, edn, 0, &e, (void *)plugin_get_default_component_id());
                if (0 == ret && e) {
                    ret = slapi_check_account_lock(
                        0, /* pb not req */
                        e,
                        0, /* no response control */
                        0, /* don't check password policy */
                        0  /* don't send ldap result */
                        );

                    if (1 == ret)
                        /* sorry root,
                         * just not cool enough
                         */
                        goto root_map_free;
                }

                /* it's ok not to find the entry,
                 * dn doesn't have to have an entry
                 * e.g. cn=Directory Manager
                 */
                bind_credentials_set_nolock(
                    conn, SLAPD_AUTH_OS, root_dn,
                    NULL, NULL, NULL, e);

            root_map_free:
                /* root_dn consumed by bind creds set */
                slapi_sdn_free(&edn);
                slapi_search_get_entry_done(&entry_pb);
                ret = 0;
            }
        }

#if defined(ENABLE_AUTO_DN_SUFFIX)
        if (ret) {
            /* create phony auth dn? */
            char *base = config_get_ldapi_auto_dn_suffix();
            if (base) {
                auth_dn = slapi_ch_smprintf("gidNumber=%u+uidNumber=%u,%s",
                                            gid, uid, base);
                auth_dn = slapi_dn_normalize(auth_dn);
                bind_credentials_set_nolock(
                    conn,
                    SLAPD_AUTH_OS,
                    auth_dn,
                    NULL, NULL, NULL, NULL);

                /* auth_dn consumed by bind creds set */
                slapi_ch_free_string(&base);
                ret = 0;
            }
        }
#endif
    }

done:
    /* if all fails, the peer is anonymous */
    if (conn->c_dn) {
        /* log the auto bind */
        slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " AUTOBIND dn=\"%s\"\n", conn->c_connid, conn->c_dn);
    }

    return ret;
}
#endif /* ENABLE_AUTOBIND */
#endif /* ENABLE_LDAPI */

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include "slap.h"
#include "slapi-plugin.h"
#include "fe.h"

/*
 * Map SASL identities to LDAP searches
 */

static char *configDN = "cn=mapping,cn=sasl,cn=config";
#define LOW_PRIORITY 100
#define LOW_PRIORITY_STR "100"

/*
 * DBDB: this is ugly, but right now there is _no_ server-wide
 * dynamic structure (like a Slapi_Server * type thing). All the code
 * that needs such a thing instead maintains a static or global variable.
 * Until we implement the 'right thing', we'll just follow suit here :(
 */

static sasl_map_private *sasl_map_static_priv = NULL;

static sasl_map_private *
sasl_map_get_global_priv(void)
{
    /* ASSERT(sasl_map_static_priv) */
    return sasl_map_static_priv;
}

static sasl_map_private *
sasl_map_new_private(void)
{
    Slapi_RWLock *new_lock = slapi_new_rwlock();
    sasl_map_private *new_priv = NULL;
    if (NULL == new_lock) {
        return NULL;
    }
    new_priv = (sasl_map_private *)slapi_ch_calloc(1, sizeof(sasl_map_private));
    new_priv->lock = new_lock;
    return new_priv;
}

static void
sasl_map_free_private(sasl_map_private **priv)
{
    slapi_destroy_rwlock((*priv)->lock);
    slapi_ch_free((void **)priv);
    *priv = NULL;
}

/* This function does a shallow copy on the payload data supplied, so the caller should not free it, and it needs to be allocated using slapi_ch_malloc() */
static sasl_map_data *
sasl_map_new_data(char *name, char *regex, char *dntemplate, char *filtertemplate, int priority)
{
    sasl_map_data *new_dp = (sasl_map_data *)slapi_ch_calloc(1, sizeof(sasl_map_data));
    new_dp->name = name;
    new_dp->regular_expression = regex;
    new_dp->template_base_dn = dntemplate;
    new_dp->template_search_filter = filtertemplate;
    new_dp->priority = priority;
    new_dp->next = NULL;
    new_dp->prev = NULL;
    return new_dp;
}

static sasl_map_data *
sasl_map_next(sasl_map_data *dp)
{
    return dp->next;
}

static void
sasl_map_free_data(sasl_map_data **dp)
{
    slapi_ch_free_string(&(*dp)->name);
    slapi_ch_free_string(&(*dp)->regular_expression);
    slapi_ch_free_string(&(*dp)->template_base_dn);
    slapi_ch_free_string(&(*dp)->template_search_filter);
    slapi_ch_free((void **)dp);
}

static int
sasl_map_remove_list_entry(sasl_map_private *priv, char *removeme)
{
    int ret = 0;
    int foundit = 0;
    sasl_map_data *current = NULL;
    sasl_map_data *prev = NULL;
    sasl_map_data *next = NULL;

    slapi_rwlock_wrlock(priv->lock);
    current = priv->map_data_list;
    while (current) {
        next = current->next;
        if (0 == strcmp(current->name, removeme)) {
            foundit = 1;
            prev = current->prev;
            if (prev) {
                /* Unlink it */
                if (next) {
                    next->prev = prev;
                }
                prev->next = next;
            } else {
                /* That was the first list entry */
                if (next) {
                    next->prev = NULL;
                }
                priv->map_data_list = next;
            }
            /* Payload free */
            sasl_map_free_data(&current);
            /* And no need to look further */
            break;
        }
        current = next;
    }
    slapi_rwlock_unlock(priv->lock);
    if (!foundit) {
        ret = -1;
    }

    return ret;
}

static int
sasl_map_cmp_data(sasl_map_data *dp0, sasl_map_data *dp1)
{
    int rc = 0;
    if (NULL == dp0) {
        if (NULL == dp1) {
            return 0;
        } else {
            return -1;
        }
    } else {
        if (NULL == dp1) {
            return 1;
        }
    }

    rc = PL_strcmp(dp0->name, dp1->name);
    if (0 != rc) {
        /* did not match */
        return rc;
    }
    rc = PL_strcmp(dp0->regular_expression, dp1->regular_expression);
    if (0 != rc) {
        /* did not match */
        return rc;
    }
    rc = PL_strcmp(dp0->template_base_dn, dp1->template_base_dn);
    if (0 != rc) {
        /* did not match */
        return rc;
    }
    rc = PL_strcmp(dp0->template_search_filter, dp1->template_search_filter);
    return rc;
}

static int
sasl_map_insert_list_entry(sasl_map_private *priv, sasl_map_data *dp)
{
    int ret = 0;
    int ishere = 0;
    sasl_map_data *current = NULL;
    sasl_map_data *last = NULL;
    sasl_map_data *prev = NULL;

    if (NULL == dp) {
        return ret;
    }

    slapi_rwlock_wrlock(priv->lock);

    /* Check to see if it's here already */
    current = priv->map_data_list;
    while (current) {
        if (0 == sasl_map_cmp_data(current, dp)) {
            ishere = 1;
            break;
        }
        if (current->next) {
            current = current->next;
        } else {
            break;
        }
    }
    if (ishere) {
        slapi_rwlock_unlock(priv->lock);
        return -1;
    }

    /* insert the map in its proper place */
    if (NULL == priv->map_data_list) {
        priv->map_data_list = dp;
    } else {
        current = priv->map_data_list;
        while (current) {
            last = current;
            if (current->priority > dp->priority) {
                prev = current->prev;
                if (prev) {
                    prev->next = dp;
                    dp->prev = prev;
                } else {
                    /* this is now the head of the list */
                    priv->map_data_list = dp;
                }
                current->prev = dp;
                dp->next = current;
                slapi_rwlock_unlock(priv->lock);
                return ret;
            }
            current = current->next;
        }
        /* add the map at the end of the list */
        last->next = dp;
        dp->prev = last;
    }
    slapi_rwlock_unlock(priv->lock);

    return ret;
}

/*
 * Functions to handle config operations
 */

/**
 * Get a list of child DNs
 * DBDB these functions should be folded into libslapd because it's a copy of a function in ssl.c
 */
static char **
getChildren(char *dn)
{
    Slapi_PBlock *new_pb = NULL;
    Slapi_Entry **e;
    int search_result = 1;
    int nEntries = 0;
    char **list = NULL;

    new_pb = slapi_search_internal(dn, LDAP_SCOPE_ONELEVEL,
                                   "(objectclass=nsSaslMapping)",
                                   NULL, NULL, 0);

    slapi_pblock_get(new_pb, SLAPI_NENTRIES, &nEntries);
    if (nEntries > 0) {
        slapi_pblock_get(new_pb, SLAPI_PLUGIN_INTOP_RESULT, &search_result);
        slapi_pblock_get(new_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &e);
        if (e != NULL) {
            int i;
            list = (char **)slapi_ch_malloc(sizeof(*list) * (nEntries + 1));
            for (i = 0; e[i] != NULL; i++) {
                list[i] = slapi_ch_strdup(slapi_entry_get_dn(e[i]));
            }
            list[nEntries] = NULL;
        }
    }
    slapi_free_search_results_internal(new_pb);
    slapi_pblock_destroy(new_pb);
    return list;
}

/**
 * Free a list of child DNs
 */
static void
freeChildren(char **list)
{
    if (list != NULL) {
        int i;
        for (i = 0; list[i] != NULL; i++) {
            slapi_ch_free((void **)(&list[i]));
        }
        slapi_ch_free((void **)(&list));
    }
}


/**
 * Get a particular entry
 */
static Slapi_Entry *
getConfigEntry(const char *dn, Slapi_Entry **e2)
{
    Slapi_DN sdn;

    slapi_sdn_init_dn_byref(&sdn, dn);
    slapi_search_internal_get_entry(&sdn, NULL, e2,
                                    plugin_get_default_component_id());
    slapi_sdn_done(&sdn);
    return *e2;
}

/**
 * Free an entry
 */
static void
freeConfigEntry(Slapi_Entry **e)
{
    if ((e != NULL) && (*e != NULL)) {
        slapi_entry_free(*e);
        *e = NULL;
    }
}

/*
 * unescape parenthesis in the regular expression.
 * E.g., ^u:\(.*\) ==> ^u:(.*)
 * This unescape is necessary for the new regex code using PCRE
 * to keep the backward compatibility.
 */
char *
_sasl_unescape_parenthesis(char *input)
{
    char *s = NULL;
    char *d = NULL;

    for (s = input, d = input; s && *s; s++) {
        if (*s == '\\' && *(s + 1) && (*(s + 1) == '(' || *(s + 1) == ')')) {
            *d++ = *(++s);
        } else {
            *d++ = *s;
        }
    }
    if (d) {
        *d = '\0';
    }
    return input;
}

static int
sasl_map_config_parse_entry(Slapi_Entry *entry, sasl_map_data **new_dp)
{
    int ret = 0;
    int priority;
    char *regex = NULL;
    char *basedntemplate = NULL;
    char *filtertemplate = NULL;
    char *priority_str = NULL;
    char *map_name = NULL;

    *new_dp = NULL;
    regex = _sasl_unescape_parenthesis(slapi_entry_attr_get_charptr(entry, "nsSaslMapRegexString"));
    basedntemplate = slapi_entry_attr_get_charptr(entry, "nsSaslMapBaseDNTemplate");
    filtertemplate = slapi_entry_attr_get_charptr(entry, "nsSaslMapFilterTemplate");
    map_name = slapi_entry_attr_get_charptr(entry, "cn");
    priority_str = slapi_entry_attr_get_charptr(entry, "nsSaslMapPriority");

    if (priority_str) {
        priority = atoi(priority_str);
    } else {
        priority = LOW_PRIORITY;
    }
    if (priority < 1 || priority > LOW_PRIORITY) {
        struct berval desc;
        struct berval *newval[2] = {0, 0};

        desc.bv_val = LOW_PRIORITY_STR;
        desc.bv_len = strlen(desc.bv_val);
        newval[0] = &desc;
        if (entry_replace_values(entry, "nsSaslMapPriority", newval) != 0) {
            slapi_log_err(SLAPI_LOG_ERR, "sasl_map_config_parse_entry", "Failed to reset entry "
                                                                        "priority to (%d) - previous value (%s)\n",
                          LOW_PRIORITY, priority_str);
        } else {
            slapi_log_err(SLAPI_LOG_ERR, "sasl_map_config_parse_entry", "Resetting invalid nsSaslMapPriority "
                                                                        " value (%s) to the lowest priority (%d)\n",
                          priority_str, LOW_PRIORITY);
        }
        priority = LOW_PRIORITY;
    }
    if ((NULL == map_name) || (NULL == regex) ||
        (NULL == basedntemplate) || (NULL == filtertemplate)) {
        /* Invalid entry */
        ret = -1;
    } else {
        /* Make the new dp */
        *new_dp = sasl_map_new_data(map_name, regex, basedntemplate, filtertemplate, priority);
    }

    if (ret) {
        slapi_ch_free_string(&map_name);
        slapi_ch_free_string(&regex);
        slapi_ch_free_string(&basedntemplate);
        slapi_ch_free_string(&filtertemplate);
    }
    slapi_ch_free_string(&priority_str);

    return ret;
}

static int
sasl_map_read_config_startup(sasl_map_private *priv)
{
    char **map_entry_list = NULL;
    int ret = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "sasl_map_read_config_startup", "=>\n");
    if ((map_entry_list = getChildren(configDN))) {
        char **map_entry = NULL;
        Slapi_Entry *entry = NULL;
        sasl_map_data *dp = NULL;

        for (map_entry = map_entry_list; *map_entry && !ret; map_entry++) {
            slapi_log_err(SLAPI_LOG_CONFIG, "sasl_map_read_config_startup", "Processing [%s]\n", *map_entry);
            getConfigEntry(*map_entry, &entry);
            if (entry == NULL) {
                continue;
            }
            ret = sasl_map_config_parse_entry(entry, &dp);
            if (ret) {
                slapi_log_err(SLAPI_LOG_ERR, "sasl_map_read_config_startup", "Failed to parse entry\n");
            } else {
                ret = sasl_map_insert_list_entry(priv, dp);
                if (ret) {
                    slapi_log_err(SLAPI_LOG_ERR, "sasl_map_read_config_startup", "Failed to insert entry\n");
                } else {
                    slapi_log_err(SLAPI_LOG_CONFIG, "sasl_map_read_config_startup", "Processed [%s]\n", *map_entry);
                }
            }
            freeConfigEntry(&entry);
        }
        freeChildren(map_entry_list);
    }
    slapi_log_err(SLAPI_LOG_TRACE, "sasl_map_read_config_startup", "<= %d\n", ret);
    return ret;
}

int
sasl_map_config_add(Slapi_PBlock *pb __attribute__((unused)),
                    Slapi_Entry *entryBefore,
                    Slapi_Entry *e __attribute__((unused)),
                    int *returncode,
                    char *returntext,
                    void *arg __attribute__((unused)))
{
    int ret = 0;
    sasl_map_data *dp = NULL;
    sasl_map_private *priv = sasl_map_get_global_priv();
    slapi_log_err(SLAPI_LOG_TRACE, "sasl_map_config_add", "=>\n");
    ret = sasl_map_config_parse_entry(entryBefore, &dp);
    if (!ret && dp) {
        ret = sasl_map_insert_list_entry(priv, dp);
    }
    if (0 == ret) {
        ret = SLAPI_DSE_CALLBACK_OK;
    } else {
        returntext = "sasl map entry rejected";
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        ret = SLAPI_DSE_CALLBACK_ERROR;
    }
    slapi_log_err(SLAPI_LOG_TRACE, "sasl_map_config_add", "<= %d\n", ret);
    return ret;
}

int
sasl_map_config_modify(Slapi_PBlock *pb __attribute__((unused)),
                       Slapi_Entry *entryBefore,
                       Slapi_Entry *e,
                       int *returncode __attribute__((unused)),
                       char *returntext __attribute__((unused)),
                       void *arg __attribute__((unused)))
{
    sasl_map_private *priv = sasl_map_get_global_priv();
    sasl_map_data *dp;
    char *map_name = NULL;
    int ret = SLAPI_DSE_CALLBACK_ERROR;

    if ((map_name = (char *)slapi_entry_attr_get_ref(entryBefore, "cn")) == NULL) {
        slapi_log_err(SLAPI_LOG_TRACE, "sasl_map_config_modify", "Could not find name of map\n");
        return ret;
    }
    if (sasl_map_remove_list_entry(priv, map_name) == 0) {
        ret = sasl_map_config_parse_entry(e, &dp);
        if (!ret && dp) {
            ret = sasl_map_insert_list_entry(priv, dp);
            if (ret == 0) {
                ret = SLAPI_DSE_CALLBACK_OK;
            }
        }
    }
    if (ret == SLAPI_DSE_CALLBACK_ERROR) {
        slapi_log_err(SLAPI_LOG_TRACE, "sasl_map_config_modify", "Failed to update map(%s)\n", map_name);
    }

    return ret;
}

int
sasl_map_config_delete(Slapi_PBlock *pb __attribute__((unused)),
                       Slapi_Entry *entryBefore,
                       Slapi_Entry *e __attribute__((unused)),
                       int *returncode,
                       char *returntext,
                       void *arg __attribute__((unused)))
{
    int ret = 0;
    sasl_map_private *priv = sasl_map_get_global_priv();
    char *entry_name = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "sasl_map_config_delete", "=>\n");
    entry_name = (char *)slapi_entry_attr_get_ref(entryBefore, "cn");
    if (entry_name) {
        /* remove this entry from the list */
        ret = sasl_map_remove_list_entry(priv, entry_name);
    }
    if (ret) {
        ret = SLAPI_DSE_CALLBACK_ERROR;
        returntext = "can't delete sasl map entry";
        *returncode = LDAP_OPERATIONS_ERROR;
    } else {
        ret = SLAPI_DSE_CALLBACK_OK;
    }
    slapi_log_err(SLAPI_LOG_TRACE, "sasl_map_config_delete", "<= %d\n", ret);
    return ret;
}

/* Start and stop the sasl mapping code */
int
sasl_map_init()
{
    int ret = 0;
    sasl_map_private *priv = NULL;
    /* Make the private structure */
    priv = sasl_map_new_private();
    if (priv) {
        /* Store in the static var */
        sasl_map_static_priv = priv;
        /* Read the config on startup */
        ret = sasl_map_read_config_startup(priv);
    } else {
        ret = -1;
    }
    return ret;
}

int
sasl_map_done()
{
    int ret = 0;
    sasl_map_private *priv = sasl_map_get_global_priv();
    sasl_map_data *dp = NULL;

    /* there is no sasl map in referral mode */
    if (!priv || !priv->lock || !priv->map_data_list) {
        return 0;
    }

    /* Free the map list */
    slapi_rwlock_wrlock(priv->lock);
    dp = priv->map_data_list;
    while (dp) {
        sasl_map_data *dp_next = dp->next;
        sasl_map_free_data(&dp);
        dp = dp_next;
    }
    slapi_rwlock_unlock(priv->lock);

    /* Free the private structure */
    sasl_map_free_private(&priv);
    return ret;
}

static int
sasl_map_check(sasl_map_data *dp, char *sasl_user_and_realm, char **ldap_search_base, char **ldap_search_filter)
{
    Slapi_Regex *re = NULL;
    int ret = 0;
    int matched = 0;
    char *recomp_result = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "sasl_map_check", "=>\n");
    /* Compiles the regex */
    re = slapi_re_comp(dp->regular_expression, &recomp_result);
    if (NULL == re) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "sasl_map_check", "slapi_re_comp failed for expression (%s): %s\n",
                      dp->regular_expression, recomp_result ? recomp_result : "unknown");
        slapi_ch_free_string(&recomp_result);
    } else {
        /* Matches the compiled regex against sasl_user_and_realm */
        matched = slapi_re_exec_nt(re, sasl_user_and_realm);
        slapi_log_err(SLAPI_LOG_TRACE, "sasl_map_check", "regex: %s, id: %s, %s\n",
                      dp->regular_expression, sasl_user_and_realm,
                      matched ? "matched" : "didn't match");
    }
    if (matched) {
        if (matched == 1) {
            char escape_base[BUFSIZ];
            char escape_filt[BUFSIZ];
            int ldap_search_base_len, ldap_search_filter_len;
            int rc = 0;

            /* Allocate buffers for the returned strings */
            /* We already computed this, so we could pass it in to speed up
             * a little */
            size_t userrealmlen = strlen(sasl_user_and_realm);
            /* These lengths could be precomputed and stored in the dp */
            ldap_search_base_len =
                userrealmlen + strlen(dp->template_base_dn) + 1;
            ldap_search_filter_len =
                userrealmlen + strlen(dp->template_search_filter) + 1;
            *ldap_search_base = (char *)slapi_ch_malloc(ldap_search_base_len);
            *ldap_search_filter =
                (char *)slapi_ch_malloc(ldap_search_filter_len);
            /* Substitutes '&' and/or "\#" in template_base_dn */
            rc = slapi_re_subs(re, sasl_user_and_realm, dp->template_base_dn,
                               ldap_search_base, ldap_search_base_len);
            if (0 != rc) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "sasl_map_check", "slapi_re_subs failed: "
                                                "subject: %s, subst str: %s (%d)\n",
                              sasl_user_and_realm, dp->template_base_dn, rc);
                slapi_ch_free_string(ldap_search_base);
                slapi_ch_free_string(ldap_search_filter);
            } else {
                /* Substitutes '&' and/or "\#" in template_search_filter */
                rc = slapi_re_subs_ext(re, sasl_user_and_realm,
                                       dp->template_search_filter, ldap_search_filter,
                                       ldap_search_filter_len, 1);
                if (0 != rc) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "sasl_map_check", "slapi_re_subs failed: "
                                                    "subject: %s, subst str: %s (%d)\n",
                                  sasl_user_and_realm, dp->template_search_filter, rc);
                    slapi_ch_free_string(ldap_search_base);
                    slapi_ch_free_string(ldap_search_filter);
                } else {
                    /* these values are internal regex representations with
                     * lots of unprintable control chars - escape for logging */
                    slapi_log_err(SLAPI_LOG_TRACE,
                                  "sasl_map_check", "mapped base dn: %s, filter: %s\n",
                                  escape_string(*ldap_search_base, escape_base),
                                  escape_string(*ldap_search_filter, escape_filt));
                    ret = 1;
                }
            }
        } else {
            slapi_log_err(SLAPI_LOG_ERR,
                          "sasl_map_check", "slapi_re_exec_nt failed: "
                                            "regex: %s, subject: %s (%d)\n",
                          dp->regular_expression, sasl_user_and_realm, matched);
        }
    }
    slapi_re_free(re);
    slapi_log_err(SLAPI_LOG_TRACE, "sasl_map_check", "<= %d\n", ret);
    return ret;
}

static char *
sasl_map_str_concat(char *s1, char *s2)
{
    if (NULL == s2) {
        return (slapi_ch_strdup(s1));
    } else {
        char *newstr = slapi_ch_smprintf("%s@%s", s1, s2);
        return newstr;
    }
}

/* Actually perform a mapping
 * Takes a sasl identity string, and returns an LDAP search spec to be used to find the entry
 * returns 1 if matched, 0 otherwise
 */
int
sasl_map_domap(sasl_map_data **map, char *sasl_user, char *sasl_realm, char **ldap_search_base, char **ldap_search_filter)
{
    sasl_map_private *priv = sasl_map_get_global_priv();
    char *sasl_user_and_realm = NULL;
    int ret = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "sasl_map_domap", "=>\n");
    if (map == NULL) {
        slapi_log_err(SLAPI_LOG_TRACE, "sasl_map_domap", "<= Internal error, mapping is NULL\n");
        return ret;
    }
    *ldap_search_base = NULL;
    *ldap_search_filter = NULL;
    sasl_user_and_realm = sasl_map_str_concat(sasl_user, sasl_realm);
    /* Walk the list of maps */
    if (*map == NULL)
        *map = priv->map_data_list;
    while (*map) {
        /* If one matches, then make the search params */
        slapi_log_err(SLAPI_LOG_TRACE, "sasl_map_domap", "Trying map [%s]\n", (*map)->name);
        if ((ret = sasl_map_check(*map, sasl_user_and_realm, ldap_search_base, ldap_search_filter))) {
            *map = sasl_map_next(*map);
            break;
        }
        *map = sasl_map_next(*map);
    }
    if (sasl_user_and_realm) {
        slapi_ch_free((void **)&sasl_user_and_realm);
    }
    slapi_log_err(SLAPI_LOG_TRACE, "sasl_map_domap", "<= (%s)\n", (1 == ret) ? "mapped" : "not mapped");
    return ret;
}

void
sasl_map_read_lock()
{
    sasl_map_private *priv = sasl_map_get_global_priv();
    slapi_rwlock_rdlock(priv->lock);
}

void
sasl_map_read_unlock()
{
    sasl_map_private *priv = sasl_map_get_global_priv();
    slapi_rwlock_unlock(priv->lock);
}

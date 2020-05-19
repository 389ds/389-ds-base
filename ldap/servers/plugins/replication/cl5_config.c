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

/* cl5_config.c - functions to process changelog configuration
 */

#include <string.h>
#include <prio.h>
#include "repl5.h"
#include "cl5.h"
#include "cl5_clcache.h" /* To configure the Changelog Cache */
#include "intrinsics.h"  /* JCMREPL - Is this bad? */
#ifdef TEST_CL5
#include "cl5_test.h"
#endif
#include "nspr.h"
#include "plstr.h"

#define CONFIG_BASE "cn=changelog5,cn=config" /*"cn=changelog,cn=supplier,cn=replication5.0,cn=replication,cn=config"*/
#define CONFIG_FILTER "(objectclass=*)"

/* the changelog config is now separate for each backend in "cn=changelog,<backend>,cn=ldbm database,cn=plugins,cn=config" */
#define CL_CONFIG_BASE "cn=ldbm database,cn=plugins,cn=config"
#define CL_CONFIG_FILTER "cn=changelog"

static Slapi_RWLock *s_configLock; /* guarantees that only on thread at a time
                                modifies changelog configuration */

/* Forward Declartions */
static int changelog5_config_add(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
static int changelog5_config_modify(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
static int changelog5_config_delete(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
static int dont_allow_that(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *e, int *returncode, char *returntext, void *arg);
static int cldb_config_add(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
static int cldb_config_modify(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
static int cldb_config_delete(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
static changelog5Config *changelog5_dup_config(changelog5Config *config);

static void replace_bslash(char *dir);

int
changelog5_config_init()
{
    /* The FE DSE *must* be initialised before we get here */

    /* create the configuration lock, if not yet created. */
    if (!s_configLock) {
        s_configLock = slapi_new_rwlock();
    }
    if (s_configLock == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "changelog5_config_init - Failed to create configuration lock; "
                      "NSPR error - %d\n",
                      PR_GetError());
        return 1;
    }

    /* callbacks to handle attempts to modify the old cn=changelog5 config */
    slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_BASE,
                                   CONFIG_FILTER, changelog5_config_add, NULL);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_BASE,
                                   CONFIG_FILTER, changelog5_config_modify, NULL);
    slapi_config_register_callback(SLAPI_OPERATION_MODRDN, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_BASE,
                                   CONFIG_FILTER, dont_allow_that, NULL);
    slapi_config_register_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_BASE,
                                   CONFIG_FILTER, changelog5_config_delete, NULL);


    return 0;
}

void
changelog5_config_cleanup()
{
    slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_BASE,
                                 CONFIG_FILTER, changelog5_config_add);
    slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_BASE,
                                 CONFIG_FILTER, changelog5_config_modify);
    slapi_config_remove_callback(SLAPI_OPERATION_MODRDN, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_BASE,
                                 CONFIG_FILTER, dont_allow_that);
    slapi_config_remove_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_BASE,
                                 CONFIG_FILTER, changelog5_config_delete);

    if (s_configLock) {
        slapi_destroy_rwlock(s_configLock);
        s_configLock = NULL;
    }
}

int
changelog5_read_config(changelog5Config *config)
{
    int rc = LDAP_SUCCESS;
    Slapi_PBlock *pb;

    pb = slapi_pblock_new();
    slapi_search_internal_set_pb(pb, CONFIG_BASE, LDAP_SCOPE_BASE,
                                 CONFIG_FILTER, NULL, 0, NULL, NULL,
                                 repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION), 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (LDAP_SUCCESS == rc) {
        Slapi_Entry **entries = NULL;
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        if (NULL != entries && NULL != entries[0]) {
            /* Extract the config info from the changelog entry */
            changelog5_extract_config(entries[0], config);
        } else {
            memset(config, 0, sizeof(*config));
            rc = LDAP_NO_SUCH_OBJECT;
        }
    } else {
        memset(config, 0, sizeof(*config));
        rc = LDAP_NO_SUCH_OBJECT;
    }

    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);

    return rc;
}

static changelog5Config *
changelog5_dup_config(changelog5Config *config)
{
    changelog5Config *dup = (changelog5Config *)slapi_ch_calloc(1, sizeof(changelog5Config));

    if (config->maxAge)
        dup->maxAge = slapi_ch_strdup(config->maxAge);

    dup->maxEntries = config->maxEntries;
    dup->trimInterval = config->trimInterval;

    return dup;
}


void
changelog5_config_done(changelog5Config *config)
{
    if (config) {
        /* slapi_ch_free_string accepts NULL pointer */
        slapi_ch_free_string(&config->maxAge);
        slapi_ch_free_string(&config->dir);
        slapi_ch_free_string(&config->symmetricKey);
        slapi_ch_free_string(&config->encryptionAlgorithm);
    }
}

void
changelog5_config_free(changelog5Config **config)
{
    changelog5_config_done(*config);
    slapi_ch_free((void **)config);
}

static int
changelog5_config_add(Slapi_PBlock *pb __attribute__((unused)),
                      Slapi_Entry *e,
                      Slapi_Entry *entryAfter __attribute__((unused)),
                      int *returncode,
                      char *returntext,
                      void *arg __attribute__((unused)))
{
    /* we no longer support a separate changelog configuration */
    slapi_log_err(SLAPI_LOG_NOTICE, repl_plugin_name_cl,
                  "changelog5_config_add - Separate changelog no longer supported; "
                  "use cn=changelog,<backend> instead\n");
 
    if (returntext) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Changelog configuration is part of the backend configuration");
    }
    *returncode = LDAP_UNWILLING_TO_PERFORM;
    return SLAPI_DSE_CALLBACK_ERROR;
}

static int
changelog5_config_modify(Slapi_PBlock *pb,
                         Slapi_Entry *entryBefore __attribute__((unused)),
                         Slapi_Entry *e,
                         int *returncode,
                         char *returntext,
                         void *arg __attribute__((unused)))
{
    /* we no longer support a separate changelog configuration */
    /* the entry does not exist and the client will be notified
     */
    slapi_log_err(SLAPI_LOG_NOTICE, repl_plugin_name_cl,
                  "changelog5_config_modify - Separate changelog no longer supported; "
                  "request ignored\n");
 
    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}

static int
changelog5_config_delete(Slapi_PBlock *pb __attribute__((unused)),
                         Slapi_Entry *e __attribute__((unused)),
                         Slapi_Entry *entryAfter __attribute__((unused)),
                         int *returncode,
                         char *returntext,
                         void *arg __attribute__((unused)))
{
    /* we no longer support a separate changelog configuration */
    /* the entry does not exist and the client will be notified
     */
    slapi_log_err(SLAPI_LOG_NOTICE, repl_plugin_name_cl,
                  "changelog5_config_delete - Separate changelog no longer supported; "
                  "request ignored\n");
 
    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}

static int
cldb_config_add(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg)
{
    return SLAPI_DSE_CALLBACK_OK;
}

static int
cldb_config_modify(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg)
{
    int rc = 0;
    LDAPMod **mods;
    *returncode = LDAP_SUCCESS;
    changelog5Config config;
    changelog5Config *originalConfig = NULL;
    Replica *replica = (Replica *)arg;


    changelog5_extract_config(e, &config);
    originalConfig = changelog5_dup_config(&config);

    /* Reset all the attributes that have been potentially modified by the current MODIFY operation */
    config.maxEntries = CL5_NUM_IGNORE;
    slapi_ch_free_string(&config.maxAge);
    config.maxAge = slapi_ch_strdup(CL5_STR_IGNORE);
    config.trimInterval = CL5_NUM_IGNORE;


    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
    for (size_t i = 0; mods && mods[i] != NULL; i++) {
        if (mods[i]->mod_op & LDAP_MOD_DELETE) {
            /* We don't support deleting changelog attributes */
        } else if (mods[i]->mod_values == NULL) {
            *returncode = LDAP_UNWILLING_TO_PERFORM;
            if (returntext) {
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                        "%s: no value provided",
                                        mods[i]->mod_type ? mods[i]->mod_type : "<unknown attribute>");
            }
            goto done;
        } else {
            int j;
            for (j = 0; ((mods[i]->mod_values[j]) && (LDAP_SUCCESS == rc)); j++) {
                char *config_attr, *config_attr_value;
                config_attr = (char *)mods[i]->mod_type;
                config_attr_value = (char *)mods[i]->mod_bvalues[j]->bv_val;

                if (slapi_attr_is_last_mod(config_attr)) {
                    continue;
                }

                /* replace existing value */
                if (strcasecmp(config_attr, CONFIG_CHANGELOG_MAXENTRIES_ATTRIBUTE) == 0) {
                    if (config_attr_value && config_attr_value[0] != '\0') {
                        config.maxEntries = atoi(config_attr_value);
                    } else {
                        config.maxEntries = 0;
                    }
                } else if (strcasecmp(config_attr, CONFIG_CHANGELOG_MAXAGE_ATTRIBUTE) == 0) {
                    if (slapi_is_duration_valid(config_attr_value)) {
                        slapi_ch_free_string(&config.maxAge);
                        config.maxAge = slapi_ch_strdup(config_attr_value);
                    } else {
                        if (returntext) {
                            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                        "%s: invalid value \"%s\", %s must range from 0 to %lld or digit[sSmMhHdD]",
                                        CONFIG_CHANGELOG_MAXAGE_ATTRIBUTE, config_attr_value ? config_attr_value : "null",
                                        CONFIG_CHANGELOG_MAXAGE_ATTRIBUTE,
                                        (long long int)LONG_MAX);
                        }
                        *returncode = LDAP_UNWILLING_TO_PERFORM;
                        goto done;
                    }
                } else if (strcasecmp(config_attr, CONFIG_CHANGELOG_TRIM_ATTRIBUTE) == 0) {
                    if (slapi_is_duration_valid(config_attr_value)) {
                        config.trimInterval = (long)slapi_parse_duration(config_attr_value);
                    } else {
                        if (returntext) {
                            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                        "%s: invalid value \"%s\", %s must range from 0 to %lld or digit[sSmMhHdD]",
                                        CONFIG_CHANGELOG_TRIM_ATTRIBUTE, config_attr_value,
                                        CONFIG_CHANGELOG_TRIM_ATTRIBUTE,
                                        (long long int)LONG_MAX);
                        }
                        *returncode = LDAP_UNWILLING_TO_PERFORM;
                        goto done;
                    }
                } else if (strcasecmp(config_attr, CONFIG_CHANGELOG_SYMMETRIC_KEY) == 0) {
                    slapi_ch_free_string(&config.symmetricKey);
                    config.symmetricKey = slapi_ch_strdup(config_attr_value);
                    /* Storing the encryption symmetric key */
                    /* no need to change any changelog configuration */
                    goto done;
                } else {
                    *returncode = LDAP_UNWILLING_TO_PERFORM;
                    if (returntext) {
                        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                    "Unwilling to apply %s mods while the server is running", config_attr);
                    }
                    goto done;
                }
            }
        }
    }
    /* Undo the reset above for all the modifiable attributes that were not modified
     * except config.dir */
    if (config.maxEntries == CL5_NUM_IGNORE)
        config.maxEntries = originalConfig->maxEntries;
    if (config.trimInterval == CL5_NUM_IGNORE)
        config.trimInterval = originalConfig->trimInterval;
    if (strcmp(config.maxAge, CL5_STR_IGNORE) == 0) {
        slapi_ch_free_string(&config.maxAge);
        if (originalConfig->maxAge)
            config.maxAge = slapi_ch_strdup(originalConfig->maxAge);
    }

    /* one of the changelog parameters is modified */
    if (config.maxEntries != CL5_NUM_IGNORE ||
        config.trimInterval != CL5_NUM_IGNORE ||
        strcmp(config.maxAge, CL5_STR_IGNORE) != 0) {
        rc = cl5ConfigTrimming(replica, config.maxEntries, config.maxAge, config.trimInterval);
        if (rc != CL5_SUCCESS) {
            *returncode = 1;
            if (returntext) {
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "failed to configure changelog trimming; error - %d", rc);
            }

            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "changelog5_config_modify - Failed to configure changelog trimming\n");
            goto done;
        }
    }

done:;
    slapi_rwlock_unlock(s_configLock);

    changelog5_config_done(&config);
    changelog5_config_free(&originalConfig);

    if (*returncode == LDAP_SUCCESS) {

        if (returntext) {
            returntext[0] = '\0';
        }

        return SLAPI_DSE_CALLBACK_OK;
    }

    return SLAPI_DSE_CALLBACK_ERROR;
}

static int
cldb_config_delete(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg)
{
    return SLAPI_DSE_CALLBACK_OK;
}


static int
dont_allow_that(Slapi_PBlock *pb __attribute__((unused)),
                Slapi_Entry *entryBefore __attribute__((unused)),
                Slapi_Entry *e __attribute__((unused)),
                int *returncode,
                char *returntext __attribute__((unused)),
                void *arg __attribute__((unused)))
{
    *returncode = LDAP_UNWILLING_TO_PERFORM;
    return SLAPI_DSE_CALLBACK_ERROR;
}

/*
 * Given the changelog configuration entry, extract the configuration directives.
 */
void
changelog5_extract_config(Slapi_Entry *entry, changelog5Config *config)
{
    const char *arg;
    char *max_age = NULL;

    memset(config, 0, sizeof(*config));
    config->dir = slapi_entry_attr_get_charptr(entry, CONFIG_CHANGELOG_DIR_ATTRIBUTE);
    replace_bslash(config->dir);

    arg = slapi_entry_attr_get_ref(entry, CONFIG_CHANGELOG_MAXENTRIES_ATTRIBUTE);
    if (arg) {
        config->maxEntries = atoi(arg);
    }

    arg = slapi_entry_attr_get_ref(entry, CONFIG_CHANGELOG_TRIM_ATTRIBUTE);
    if (arg) {
        if (slapi_is_duration_valid(arg)) {
            config->trimInterval = (long)slapi_parse_duration(arg);
        } else {
            slapi_log_err(SLAPI_LOG_NOTICE, repl_plugin_name_cl,
                          "changelog5_extract_config - %s: invalid value \"%s\", ignoring the change.\n",
                          CONFIG_CHANGELOG_TRIM_ATTRIBUTE, arg);
            config->trimInterval = CHANGELOGDB_TRIM_INTERVAL;
        }
    } else {
        config->trimInterval = CHANGELOGDB_TRIM_INTERVAL;
    }

    max_age = slapi_entry_attr_get_charptr(entry, CONFIG_CHANGELOG_MAXAGE_ATTRIBUTE);
    if (max_age) {
        if (slapi_is_duration_valid(max_age)) {
            config->maxAge = max_age;
        } else {
            slapi_ch_free_string(&max_age);
            slapi_log_err(SLAPI_LOG_NOTICE, repl_plugin_name_cl,
                          "changelog5_extract_config - %s: invalid value \"%s\", ignoring the change.\n",
                          CONFIG_CHANGELOG_MAXAGE_ATTRIBUTE, max_age);
            config->maxAge = slapi_ch_strdup(CL5_STR_IGNORE);
        }
    } else {
        config->maxAge = slapi_ch_strdup(CL5_STR_IGNORE);
    }

    /*
     * changelog encryption
     */
    arg = slapi_entry_attr_get_ref(entry, CONFIG_CHANGELOG_ENCRYPTION_ALGORITHM);
    if (arg) {
        config->encryptionAlgorithm = slapi_ch_strdup(arg);
    } else {
        config->encryptionAlgorithm = NULL; /* no encryption */
    }
    /*
     * symmetric key
     */
    arg = slapi_entry_attr_get_ref(entry, CONFIG_CHANGELOG_SYMMETRIC_KEY);
    if (arg) {
        config->symmetricKey = slapi_ch_strdup(arg);
    } else {
        config->symmetricKey = NULL; /* no symmetric key */
    }
}

/* register functions handling attempted operations on the changelog config entries */
int
changelog5_register_config_callbacks(const char *dn, Replica *replica)
{
    int rc = 0;
    /* callbacks to handle changes to the new changelog configuration in the main database */
    rc =slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn, LDAP_SCOPE_SUBTREE,
                                   CL_CONFIG_FILTER, cldb_config_add, replica);
    rc |= slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn, LDAP_SCOPE_SUBTREE,
                                   CL_CONFIG_FILTER, cldb_config_modify, replica);
    rc |= slapi_config_register_callback(SLAPI_OPERATION_MODRDN, DSE_FLAG_PREOP, dn, LDAP_SCOPE_SUBTREE,
                                   CL_CONFIG_FILTER, dont_allow_that, replica);
    rc |= slapi_config_register_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, dn, LDAP_SCOPE_SUBTREE,
                                   CL_CONFIG_FILTER, cldb_config_delete, replica);

    return rc;
}

int
changelog5_remove_config_callbacks(const char *dn)
{
    int rc = 0;

    rc =slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn, LDAP_SCOPE_SUBTREE,
                                   CL_CONFIG_FILTER, cldb_config_add);
    rc |= slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn, LDAP_SCOPE_SUBTREE,
                                   CL_CONFIG_FILTER, cldb_config_modify);
    rc |= slapi_config_remove_callback(SLAPI_OPERATION_MODRDN, DSE_FLAG_PREOP, dn, LDAP_SCOPE_SUBTREE,
                                   CL_CONFIG_FILTER, dont_allow_that);
    rc |= slapi_config_remove_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, dn, LDAP_SCOPE_SUBTREE,
                                   CL_CONFIG_FILTER, cldb_config_delete);

    return rc;
}

static void
replace_bslash(char *dir)
{
    char *bslash;

    if (dir == NULL)
        return;

    bslash = strchr(dir, '\\');
    while (bslash) {
        *bslash = '/';
        bslash = strchr(bslash, '\\');
    }
}

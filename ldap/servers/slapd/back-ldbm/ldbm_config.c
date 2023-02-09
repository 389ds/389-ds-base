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

/* ldbm_config.c - Handles configuration information that is global to all ldbm instances. */

#include "back-ldbm.h"
#include "dblayer.h"

/* Forward declarations */
static int parse_ldbm_config_entry(struct ldbminfo *li, Slapi_Entry *e, config_info *config_array);

/* Forward callback declarations */
int ldbm_config_search_entry_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int ldbm_config_modify_entry_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);

static char *ldbm_skeleton_entries[] =
    {
        "dn:cn=config, cn=%s, cn=plugins, cn=config\n"
        "objectclass:top\n"
        "objectclass:extensibleObject\n"
        "cn:config\n",

        "dn:cn=monitor, cn=%s, cn=plugins, cn=config\n"
        "objectclass:top\n"
        "objectclass:extensibleObject\n"
        "cn:monitor\n",

        "dn:cn=database, cn=monitor, cn=%s, cn=plugins, cn=config\n"
        "objectclass:top\n"
        "objectclass:extensibleObject\n"
        "cn:database\n",

        ""};

static char *ldbm_config_moved_attributes[] =
    {
        CONFIG_DB_LOCK,
        CONFIG_DBCACHESIZE,
        CONFIG_DBNCACHE,
        CONFIG_MAXPASSBEFOREMERGE,
        CONFIG_DB_LOGDIRECTORY,
        CONFIG_DB_DURABLE_TRANSACTIONS,
        CONFIG_DB_CIRCULAR_LOGGING,
        CONFIG_DB_TRANSACTION_LOGGING,
        CONFIG_DB_TRANSACTION_WAIT,
        CONFIG_DB_CHECKPOINT_INTERVAL,
        CONFIG_DB_COMPACTDB_INTERVAL,
        CONFIG_DB_TRANSACTION_BATCH,
        CONFIG_DB_TRANSACTION_BATCH_MIN_SLEEP,
        CONFIG_DB_TRANSACTION_BATCH_MAX_SLEEP,
        CONFIG_DB_LOGBUF_SIZE,
        CONFIG_DB_PAGE_SIZE,
        CONFIG_DB_INDEX_PAGE_SIZE,
        CONFIG_DB_OLD_IDL_MAXIDS,
        CONFIG_DB_LOGFILE_SIZE,
        CONFIG_DB_TRICKLE_PERCENTAGE,
        CONFIG_DB_SPIN_COUNT,
        CONFIG_DB_DEBUG,
        CONFIG_DB_VERBOSE,
        CONFIG_DB_NAMED_REGIONS,
        CONFIG_DB_LOCK,
        CONFIG_DB_PRIVATE_MEM,
        CONFIG_DB_PRIVATE_IMPORT_MEM,
        CONDIF_DB_ONLINE_IMPORT_ENCRYPT,
        CONFIG_DB_SHM_KEY,
        CONFIG_DB_CACHE,
        CONFIG_DB_DEBUG_CHECKPOINTING,
        CONFIG_DB_HOME_DIRECTORY,
        CONFIG_IMPORT_CACHE_AUTOSIZE,
        CONFIG_CACHE_AUTOSIZE,
        CONFIG_CACHE_AUTOSIZE_SPLIT,
        CONFIG_IMPORT_CACHESIZE,
        CONFIG_BYPASS_FILTER_TEST,
        CONFIG_DB_LOCKDOWN,
        CONFIG_INDEX_BUFFER_SIZE,
        CONFIG_DB_TX_MAX,
        CONFIG_SERIAL_LOCK,
        CONFIG_USE_LEGACY_ERRORCODE,
        CONFIG_DB_DEADLOCK_POLICY,
        CONFIG_DB_LOCKS_MONITORING,
        CONFIG_DB_LOCKS_THRESHOLD,
        CONFIG_DB_LOCKS_PAUSE,
        ""};

/* Used to add an array of entries, like the one above and
 * ldbm_instance_skeleton_entries in ldbm_instance_config.c, to the dse.
 * Returns 0 on success.
 */
int
ldbm_config_add_dse_entries(struct ldbminfo *li, char **entries, char *string1, char *string2, char *string3, int flags)
{
    int x;
    Slapi_Entry *e;
    Slapi_PBlock *util_pb = NULL;
    int rc;
    int result;
    char entry_string[512];
    int dont_write_file = 0;
    char ebuf[BUFSIZ];

    if (flags & LDBM_INSTANCE_CONFIG_DONT_WRITE) {
        dont_write_file = 1;
    }

    for (x = 0; strlen(entries[x]) > 0; x++) {
        util_pb = slapi_pblock_new();
        PR_snprintf(entry_string, 512, entries[x], string1, string2, string3);
        e = slapi_str2entry(entry_string, 0);
        PL_strncpyz(ebuf, slapi_entry_get_dn_const(e), sizeof(ebuf)); /* for logging */
        slapi_add_entry_internal_set_pb(util_pb, e, NULL, li->li_identity, 0);
        slapi_pblock_set(util_pb, SLAPI_DSE_DONT_WRITE_WHEN_ADDING,
                         &dont_write_file);
        rc = slapi_add_internal_pb(util_pb);
        slapi_pblock_get(util_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);
        if (!rc && (result == LDAP_SUCCESS)) {
            slapi_log_err(SLAPI_LOG_CONFIG, "ldbm_config_add_dse_entries", "Added database config entry [%s]\n", ebuf);
        } else if (result == LDAP_ALREADY_EXISTS) {
            slapi_log_err(SLAPI_LOG_TRACE, "ldbm_config_add_dse_entries", "Database config entry [%s] already exists - skipping\n", ebuf);
        } else {
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_add_dse_entries",
                          "Unable to add config entry [%s] to the DSE: %d %d\n",
                          ebuf, result, rc);
        }
        slapi_pblock_destroy(util_pb);
    }

    return 0;
}

/* used to add a single entry, special case of above */
int
ldbm_config_add_dse_entry(struct ldbminfo *li, char *entry, int flags)
{
    char *entries[] = {"%s", ""};

    return ldbm_config_add_dse_entries(li, entries, entry, NULL, NULL, flags);
}

/* Finds an entry in a config_info array with the given name.  Returns
 * the entry on success and NULL when not found.
 */
config_info *
config_info_get(config_info *config_array, char *attr_name)
{
    int x;

    for (x = 0; config_array[x].config_name != NULL; x++) {
        if (!strcasecmp(config_array[x].config_name, attr_name)) {
            return &(config_array[x]);
        }
    }
    return NULL;
}

void
config_info_print_val(void *val, int type, char *buf)
{
    switch (type) {
    case CONFIG_TYPE_INT:
        sprintf(buf, "%d", (int)((uintptr_t)val));
        break;
    case CONFIG_TYPE_INT_OCTAL:
        sprintf(buf, "%o", (int)((uintptr_t)val));
        break;
    case CONFIG_TYPE_LONG:
        sprintf(buf, "%ld", (long)val);
        break;
    case CONFIG_TYPE_SIZE_T:
        sprintf(buf, "%" PRIu32, (uint32_t)((size_t)val));
        break;
    case CONFIG_TYPE_UINT64:
        sprintf(buf, "%" PRIu64, (uint64_t)((uintptr_t)val));
        break;
    case CONFIG_TYPE_STRING:
        PR_snprintf(buf, BUFSIZ, "%s", (char *)val);
        break;
    case CONFIG_TYPE_ONOFF:
        if ((int)((uintptr_t)val)) {
            sprintf(buf, "on");
        } else {
            sprintf(buf, "off");
        }
        break;
    }
}

/*------------------------------------------------------------------------
 * Get and set functions for ldbm and dblayer variables
 *----------------------------------------------------------------------*/
static void *
ldbm_config_lookthroughlimit_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(li->li_lookthroughlimit));
}

static int
ldbm_config_lookthroughlimit_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    /* Do whatever we can to make sure the data is ok. */

    if (apply) {
        li->li_lookthroughlimit = val;
    }

    return retval;
}

static void *
ldbm_config_pagedlookthroughlimit_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(li->li_pagedlookthroughlimit));
}

static int
ldbm_config_pagedlookthroughlimit_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    /* Do whatever we can to make sure the data is ok. */

    if (apply) {
        li->li_pagedlookthroughlimit = val;
    }

    return retval;
}

static void *
ldbm_config_rangelookthroughlimit_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(li->li_rangelookthroughlimit));
}

static int
ldbm_config_rangelookthroughlimit_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    /* Do whatever we can to make sure the data is ok. */

    if (apply) {
        li->li_rangelookthroughlimit = val;
    }

    return retval;
}

static void *
ldbm_config_backend_implement_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)slapi_ch_strdup(li->li_backend_implement);
}

static int
ldbm_config_backend_implement_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;

    if (apply) {
        slapi_ch_free((void **)&(li->li_backend_implement));
        li->li_backend_implement = slapi_ch_strdup((char *)value);
    }

    return retval;
}
static void *
ldbm_config_backend_opt_level_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(li->li_backend_opt_level));
}

static int
ldbm_config_backend_opt_level_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    /* Do whatever we can to make sure the data is ok. */

    if (apply) {
        li->li_backend_opt_level = val;
    }

    return retval;
}
static void *
ldbm_config_mode_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(li->li_mode));
}

static int
ldbm_config_mode_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    /* Do whatever we can to make sure the data is ok. */

    if (apply) {
        li->li_mode = val;
    }

    return retval;
}

static void *
ldbm_config_allidsthreshold_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(li->li_allidsthreshold));
}

static int
ldbm_config_allidsthreshold_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    /* Do whatever we can to make sure the data is ok. */

    /* Catch attempts to configure a stupidly low allidsthreshold */
    if ((val > -1) && (val < 100)) {
        val = 100;
    }

    if (apply) {
        li->li_allidsthreshold = val;
    }

    return retval;
}

static void *
ldbm_config_pagedallidsthreshold_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(li->li_pagedallidsthreshold));
}

static int
ldbm_config_pagedallidsthreshold_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    /* Do whatever we can to make sure the data is ok. */

    /* Catch attempts to configure a stupidly low pagedallidsthreshold */
    /* value of 0 means turn off separate paged value and use regular allids value */
    if ((val > 0) && (val < 100)) {
        val = 100;
    }

    if (apply) {
        li->li_pagedallidsthreshold = val;
    }

    return retval;
}

static void *
ldbm_config_directory_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    /* Remember get functions of type string need to return
     * alloced memory. */
    return (void *)slapi_ch_strdup(li->li_new_directory);
}

static int
ldbm_config_directory_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    char *val = (char *)value;
    char tmpbuf[BUFSIZ];

    if (errorbuf) {
        errorbuf[0] = '\0';
    }

    if (!apply) {
        /* we should really do some error checking here. */
        return retval;
    }

    if (CONFIG_PHASE_RUNNING == phase) {
        slapi_ch_free((void **)&(li->li_new_directory));
        li->li_new_directory = rel2abspath(val); /* normalize the path;
                                                    strdup'ed in rel2abspath */
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_directory_set",
                      "New db directory location will not take affect until the server is restarted\n");
    } else {
        slapi_ch_free((void **)&(li->li_new_directory));
        slapi_ch_free((void **)&(li->li_directory));
        if (NULL == val || '\0' == *val) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "ldbm_config_directory_set", "db directory is not set; check %s in the db config: %s\n",
                          CONFIG_DIRECTORY, CONFIG_LDBM_DN);
            retval = LDAP_PARAM_ERROR;
        } else {
            if (0 == strcmp(val, "get default")) {
                /* We use this funky "get default" string for the caller to
                 * tell us that it has no idea what the db directory should
                 * be.  This code figures it out be reading "cn=config,cn=ldbm
                 * database,cn=plugins,cn=config" entry. */
                Slapi_PBlock *search_pb;
                Slapi_Entry **entries = NULL;
                Slapi_Attr *attr = NULL;
                Slapi_Value *v = NULL;
                const char *s = NULL;
                int res;

                search_pb = slapi_pblock_new();
                slapi_search_internal_set_pb(search_pb, CONFIG_LDBM_DN,
                                             LDAP_SCOPE_BASE, "objectclass=*", NULL, 0, NULL, NULL,
                                             li->li_identity, 0);
                slapi_search_internal_pb(search_pb);
                slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &res);

                if (res != LDAP_SUCCESS) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "ldbm_config_directory_set", "ldbm plugin unable to read %s\n",
                                  CONFIG_LDBM_DN);
                    retval = res;
                    goto done;
                }

                slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
                if (NULL == entries) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "ldbm_config_directory_set", "ldbm plugin unable to read %s\n",
                                  CONFIG_LDBM_DN);
                    retval = LDAP_OPERATIONS_ERROR;
                    goto done;
                }

                res = slapi_entry_attr_find(entries[0], "nsslapd-directory", &attr);
                if (res != 0 || attr == NULL) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "ldbm_config_directory_set", "ldbm plugin unable to read attribute nsslapd-directory from %s\n",
                                  CONFIG_LDBM_DN);
                    retval = LDAP_OPERATIONS_ERROR;
                    goto done;
                }

                if (slapi_attr_first_value(attr, &v) != 0 || (NULL == v) || (NULL == (s = slapi_value_get_string(v)))) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "ldbm_config_directory_set", "ldbm plugin unable to read attribute nsslapd-directory from %s\n",
                                  CONFIG_LDBM_DN);
                    retval = LDAP_OPERATIONS_ERROR;
                    goto done;
                }
                slapi_pblock_destroy(search_pb);
                if (NULL == s || '\0' == *s || 0 == PL_strcmp(s, "(null)")) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "ldbm_config_directory_set", "db directory is not set; check %s in the db config: %s\n",
                                  CONFIG_DIRECTORY, CONFIG_LDBM_DN);
                    retval = LDAP_PARAM_ERROR;
                    goto done;
                }
                PR_snprintf(tmpbuf, BUFSIZ, "%s", s);
                val = tmpbuf;
            }
            li->li_new_directory = rel2abspath(val); /* normalize the path;
                                                        strdup'ed in
                                                        rel2abspath */
            li->li_directory = rel2abspath(val);     /* ditto */
        }
    }
done:
    return retval;
}

static void *
ldbm_config_maxpassbeforemerge_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)(li->li_maxpassbeforemerge));
}

static int
ldbm_config_maxpassbeforemerge_set(void *arg, void *value, char *errorbuf __attribute__((unused)), int phase __attribute__((unused)), int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (val < 0) {
        slapi_log_err(SLAPI_LOG_NOTICE, "ldbm_config_maxpassbeforemerge_set",
                      "maxpassbeforemerge will not take negative value - setting to 100\n");
        val = 100;
    }

    if (apply) {
        li->li_maxpassbeforemerge = val;
    }

    return retval;
}

static void *
ldbm_config_db_idl_divisor_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)li->li_dblayer_private->dblayer_idl_divisor);
}

static int
ldbm_config_db_idl_divisor_set(void *arg,
                               void *value,
                               char *errorbuf __attribute__((unused)),
                               int phase __attribute__((unused)),
                               int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        li->li_dblayer_private->dblayer_idl_divisor = val;
    }

    return retval;
}

static void *
ldbm_config_db_old_idl_maxids_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)li->li_old_idl_maxids);
}

static int
ldbm_config_db_old_idl_maxids_set(void *arg,
                                  void *value,
                                  char *errorbuf,
                                  int phase __attribute__((unused)),
                                  int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (val < 0) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "Error: Invalid value for %s (%d). Value must be equal or greater than zero.",
                              CONFIG_DB_OLD_IDL_MAXIDS, val);
        return LDAP_UNWILLING_TO_PERFORM;
    }

    if (apply) {
        li->li_old_idl_maxids = val;
    }

    return retval;
}

static void *
ldbm_config_db_online_import_encrypt_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)li->li_online_import_encrypt);
}

static int
ldbm_config_db_online_import_encrypt_set(void *arg,
                                         void *value,
                                         char *errorbuf __attribute__((unused)),
                                         int phase __attribute__((unused)),
                                         int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        li->li_online_import_encrypt = val;
    }

    return retval;
}

static void *
ldbm_config_idl_get_update(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)li->li_idl_update);
}

static int
ldbm_config_idl_set_update(void *arg,
                           void *value,
                           char *errorbuf __attribute__((unused)),
                           int phase __attribute__((unused)),
                           int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int retval = LDAP_SUCCESS;
    int val = (int)((uintptr_t)value);

    if (apply) {
        li->li_idl_update = val;
    }

    return retval;
}


static void *
ldbm_config_import_cachesize_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)li->li_import_cachesize);
}

static int
ldbm_config_import_cachesize_set(void *arg,
                                 void *value,
                                 char *errorbuf,
                                 int phase __attribute__((unused)),
                                 int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    uint64_t val = (uint64_t)((uintptr_t)value);
    uint64_t delta;
    /* There is an error here. We check the new val against our current mem-alloc
     * Issue is that we already are using system pages, so while our value *might*
     * be valid, we may reject it here due to the current procs page usage.
     *
     * So how do we solve this? If we are setting a SMALLER value than we
     * currently have ALLOW it, because we already passed the cache sanity.
     * If we are setting a LARGER value, we check the delta of the two, and make
     * sure that it is sane.
     */
    if (val > li->li_import_cachesize) {
        delta = val - li->li_import_cachesize;

        util_cachesize_result sane;
        slapi_pal_meminfo *mi = spal_meminfo_get();
        sane = util_is_cachesize_sane(mi, &delta);
        spal_meminfo_destroy(mi);

        if (sane != UTIL_CACHESIZE_VALID) {
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: import cachesize value is too large.");
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_import_cachesize_set",
                          "Import cachesize value is too large.\n");
            return LDAP_UNWILLING_TO_PERFORM;
        }
    }
    if (apply) {
        li->li_import_cachesize = val;
    }
    return LDAP_SUCCESS;
}

static void *
ldbm_config_idl_get_idl_new(void *arg __attribute__((unused)))
{
    if (idl_get_idl_new()) {
        return slapi_ch_strdup("new");
    }
    return slapi_ch_strdup("old");
}

static int
ldbm_config_idl_set_tune(void *arg __attribute__((unused)),
                         void *value,
                         char *errorbuf __attribute__((unused)),
                         int phase __attribute__((unused)),
                         int apply __attribute__((unused)))
{
    if (!strcasecmp("new", value)) {
        idl_set_tune(4096);
    } else {
        idl_set_tune(0);
    }
    return LDAP_SUCCESS;
}

static void *
ldbm_config_serial_lock_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)li->li_fat_lock);
}

static int
ldbm_config_serial_lock_set(void *arg,
                            void *value,
                            char *errorbuf __attribute__((unused)),
                            int phase __attribute__((unused)),
                            int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    if (apply) {
        li->li_fat_lock = (int)((uintptr_t)value);
    }

    return LDAP_SUCCESS;
}

static void *
ldbm_config_entryrdn_switch_get(void *arg __attribute__((unused)))
{
    return (void *)((uintptr_t)entryrdn_get_switch());
}

static int
ldbm_config_entryrdn_switch_set(void *arg __attribute__((unused)),
                                void *value,
                                char *errorbuf __attribute__((unused)),
                                int phase __attribute__((unused)),
                                int apply)
{
    if (apply) {
        entryrdn_set_switch((int)((uintptr_t)value));
    }
    return LDAP_SUCCESS;
}

static void *
ldbm_config_entryrdn_noancestorid_get(void *arg __attribute__((unused)))
{
    return (void *)((uintptr_t)entryrdn_get_noancestorid());
}

static int
ldbm_config_entryrdn_noancestorid_set(void *arg __attribute__((unused)),
                                      void *value,
                                      char *errorbuf __attribute__((unused)),
                                      int phase __attribute__((unused)),
                                      int apply)
{
    if (apply) {
        entryrdn_set_noancestorid((int)((uintptr_t)value));
    }
    return LDAP_SUCCESS;
}

static void *
ldbm_config_legacy_errcode_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)li->li_legacy_errcode);
}

static int
ldbm_config_legacy_errcode_set(void *arg,
                               void *value,
                               char *errorbuf __attribute__((unused)),
                               int phase __attribute__((unused)),
                               int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    if (apply) {
        li->li_legacy_errcode = (int)((uintptr_t)value);
    }

    return LDAP_SUCCESS;
}

static int
ldbm_config_set_bypass_filter_test(void *arg,
                                   void *value,
                                   char *errorbuf __attribute__((unused)),
                                   int phase __attribute__((unused)),
                                   int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    if (apply) {
        char *myvalue = (char *)value;

        if (0 == strcasecmp(myvalue, "on")) {
            li->li_filter_bypass = 1;
            li->li_filter_bypass_check = 0;
        } else if (0 == strcasecmp(myvalue, "verify")) {
            li->li_filter_bypass = 1;
            li->li_filter_bypass_check = 1;
        } else {
            li->li_filter_bypass = 0;
            li->li_filter_bypass_check = 0;
        }
    }
    return LDAP_SUCCESS;
}

static void *
ldbm_config_get_bypass_filter_test(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    char *retstr = NULL;

    if (li->li_filter_bypass) {
        if (li->li_filter_bypass_check) {
            /* meaningful only if is bypass filter test called */
            retstr = slapi_ch_strdup("verify");
        } else {
            retstr = slapi_ch_strdup("on");
        }
    } else {
        retstr = slapi_ch_strdup("off");
    }
    return (void *)retstr;
}

static int
ldbm_config_set_use_vlv_index(void *arg,
                              void *value,
                              char *errorbuf __attribute__((unused)),
                              int phase __attribute__((unused)),
                              int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int val = (int)((uintptr_t)value);

    if (apply) {
        if (val) {
            li->li_use_vlv = 1;
        } else {
            li->li_use_vlv = 0;
        }
    }
    return LDAP_SUCCESS;
}

static void *
ldbm_config_get_use_vlv_index(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)((uintptr_t)li->li_use_vlv);
}

static int
ldbm_config_exclude_from_export_set(void *arg,
                                    void *value,
                                    char *errorbuf __attribute__((unused)),
                                    int phase __attribute__((unused)),
                                    int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    if (apply) {
        if (NULL != li->li_attrs_to_exclude_from_export) {
            charray_free(li->li_attrs_to_exclude_from_export);
            li->li_attrs_to_exclude_from_export = NULL;
        }

        if (NULL != value) {
            char *dupvalue = slapi_ch_strdup(value);
            li->li_attrs_to_exclude_from_export = slapi_str2charray(dupvalue, " ");
            slapi_ch_free((void **)&dupvalue);
        }
    }

    return LDAP_SUCCESS;
}

static void *
ldbm_config_exclude_from_export_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    char *p, *retstr = NULL;
    size_t len = 0;

    if (NULL != li->li_attrs_to_exclude_from_export &&
        NULL != li->li_attrs_to_exclude_from_export[0]) {
        int i;

        for (i = 0; li->li_attrs_to_exclude_from_export[i] != NULL; ++i) {
            len += strlen(li->li_attrs_to_exclude_from_export[i]) + 1;
        }
        p = retstr = slapi_ch_malloc(len);
        for (i = 0; li->li_attrs_to_exclude_from_export[i] != NULL; ++i) {
            if (i > 0) {
                *p++ = ' ';
            }
            strcpy(p, li->li_attrs_to_exclude_from_export[i]);
            p += strlen(p);
        }
        *p = '\0';
    } else {
        retstr = slapi_ch_strdup("");
    }

    return (void *)retstr;
}


/*------------------------------------------------------------------------
 * Configuration array for ldbm and dblayer variables
 *----------------------------------------------------------------------*/
static config_info ldbm_config[] = {
    {CONFIG_LOOKTHROUGHLIMIT, CONFIG_TYPE_INT, "5000", &ldbm_config_lookthroughlimit_get, &ldbm_config_lookthroughlimit_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_MODE, CONFIG_TYPE_INT_OCTAL, "0600", &ldbm_config_mode_get, &ldbm_config_mode_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_IDLISTSCANLIMIT, CONFIG_TYPE_INT, "2147483646", &ldbm_config_allidsthreshold_get, &ldbm_config_allidsthreshold_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_DIRECTORY, CONFIG_TYPE_STRING, "", &ldbm_config_directory_get, &ldbm_config_directory_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE | CONFIG_FLAG_SKIP_DEFAULT_SETTING},
    {CONFIG_MAXPASSBEFOREMERGE, CONFIG_TYPE_INT, "100", &ldbm_config_maxpassbeforemerge_get, &ldbm_config_maxpassbeforemerge_set, 0},

    /* dblayer config attributes */
    {CONFIG_DB_IDL_DIVISOR, CONFIG_TYPE_INT, "0", &ldbm_config_db_idl_divisor_get, &ldbm_config_db_idl_divisor_set, 0},
    {CONFIG_DB_OLD_IDL_MAXIDS, CONFIG_TYPE_INT, "0", &ldbm_config_db_old_idl_maxids_get, &ldbm_config_db_old_idl_maxids_set, 0},
    {CONDIF_DB_ONLINE_IMPORT_ENCRYPT, CONFIG_TYPE_ONOFF, "on", &ldbm_config_db_online_import_encrypt_get, &ldbm_config_db_online_import_encrypt_set, 0},
    {CONFIG_IMPORT_CACHESIZE, CONFIG_TYPE_UINT64, "16777216", &ldbm_config_import_cachesize_get, &ldbm_config_import_cachesize_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_IDL_SWITCH, CONFIG_TYPE_STRING, "new", &ldbm_config_idl_get_idl_new, &ldbm_config_idl_set_tune, CONFIG_FLAG_ALWAYS_SHOW},
    {CONFIG_IDL_UPDATE, CONFIG_TYPE_ONOFF, "on", &ldbm_config_idl_get_update, &ldbm_config_idl_set_update, 0},
    {CONFIG_BYPASS_FILTER_TEST, CONFIG_TYPE_STRING, "on", &ldbm_config_get_bypass_filter_test, &ldbm_config_set_bypass_filter_test, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_USE_VLV_INDEX, CONFIG_TYPE_ONOFF, "on", &ldbm_config_get_use_vlv_index, &ldbm_config_set_use_vlv_index, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_EXCLUDE_FROM_EXPORT, CONFIG_TYPE_STRING, CONFIG_EXCLUDE_FROM_EXPORT_DEFAULT_VALUE, &ldbm_config_exclude_from_export_get, &ldbm_config_exclude_from_export_set, CONFIG_FLAG_ALWAYS_SHOW},
    {CONFIG_SERIAL_LOCK, CONFIG_TYPE_ONOFF, "on", &ldbm_config_serial_lock_get, &ldbm_config_serial_lock_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_USE_LEGACY_ERRORCODE, CONFIG_TYPE_ONOFF, "off", &ldbm_config_legacy_errcode_get, &ldbm_config_legacy_errcode_set, 0},
    {CONFIG_ENTRYRDN_SWITCH, CONFIG_TYPE_ONOFF, "on", &ldbm_config_entryrdn_switch_get, &ldbm_config_entryrdn_switch_set, CONFIG_FLAG_ALWAYS_SHOW},
    {CONFIG_ENTRYRDN_NOANCESTORID, CONFIG_TYPE_ONOFF, "off", &ldbm_config_entryrdn_noancestorid_get, &ldbm_config_entryrdn_noancestorid_set, 0 /* no show */},
    {CONFIG_PAGEDLOOKTHROUGHLIMIT, CONFIG_TYPE_INT, "0", &ldbm_config_pagedlookthroughlimit_get, &ldbm_config_pagedlookthroughlimit_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_PAGEDIDLISTSCANLIMIT, CONFIG_TYPE_INT, "0", &ldbm_config_pagedallidsthreshold_get, &ldbm_config_pagedallidsthreshold_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_RANGELOOKTHROUGHLIMIT, CONFIG_TYPE_INT, "5000", &ldbm_config_rangelookthroughlimit_get, &ldbm_config_rangelookthroughlimit_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_BACKEND_OPT_LEVEL, CONFIG_TYPE_INT, "1", &ldbm_config_backend_opt_level_get, &ldbm_config_backend_opt_level_set, CONFIG_FLAG_ALWAYS_SHOW},
    {CONFIG_BACKEND_IMPLEMENT, CONFIG_TYPE_STRING, "bdb", &ldbm_config_backend_implement_get, &ldbm_config_backend_implement_set, CONFIG_FLAG_ALWAYS_SHOW},
    {NULL, 0, NULL, NULL, NULL, 0}};

void
ldbm_config_setup_default(struct ldbminfo *li)
{
    config_info *config;
    char err_buf[SLAPI_DSE_RETURNTEXT_SIZE];

    for (config = ldbm_config; config->config_name != NULL; config++) {
        ldbm_config_set((void *)li, config->config_name, ldbm_config, NULL /* use default */, err_buf, CONFIG_PHASE_INITIALIZATION, 1 /* apply */, LDAP_MOD_REPLACE);
    }
}

int
ldbm_config_read_instance_entries(struct ldbminfo *li, const char *backend_type)
{
    Slapi_PBlock *tmp_pb;
    Slapi_Entry **entries = NULL;
    char *basedn = NULL;
    int rc = 0;

    /* Construct the base dn of the subtree that holds the instance entries. */
    basedn = slapi_create_dn_string("cn=%s,cn=plugins,cn=config", backend_type);
    if (NULL == basedn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "ldbm_config_read_instance_entries",
                      "failed to create backend dn for %s\n", backend_type);
        return 1;
    }

    /* Do a search of the subtree containing the instance entries */
    tmp_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(tmp_pb, basedn, LDAP_SCOPE_SUBTREE, "(objectclass=nsBackendInstance)", NULL, 0, NULL, NULL, li->li_identity, 0);
    slapi_search_internal_pb(tmp_pb);
    slapi_pblock_get(tmp_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if (entries != NULL) {
        int i;
        for (i = 0; entries[i] != NULL; i++) {
            rc = ldbm_instance_add_instance_entry_callback(NULL,
                                                           entries[i], NULL, NULL, NULL, li);
            if (SLAPI_DSE_CALLBACK_ERROR == rc) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "ldbm_config_read_instance_entries",
                              "Failed to add instance entry %s\n",
                              slapi_entry_get_dn_const(entries[i]));
                break;
            }
            rc = 0;
        }
    }

    slapi_free_search_results_internal(tmp_pb);
    slapi_pblock_destroy(tmp_pb);
    slapi_ch_free_string(&basedn);

    return rc;
}

/* Reads in any config information held in the dse for the ldbm plugin.
 * Creates dse entries used to configure the ldbm plugin and dblayer
 * if they don't already exist.  Registers dse callback functions to
 * maintain those dse entries.  Returns 0 on success.
 */
int
ldbm_config_load_dse_info(struct ldbminfo *li)
{
    Slapi_PBlock *search_pb;
    Slapi_Entry **entries = NULL;
    char *dn = NULL;
    int rval = 0;

    /* We try to read the entry
     * cn=config, cn=ldbm database, cn=plugins, cn=config.  If the entry is
     * there, then we process the config information it stores.
     */
    dn = slapi_create_dn_string("cn=config,cn=%s,cn=plugins,cn=config",
                                li->li_plugin->plg_name);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "ldbm_config_load_dse_info",
                      "failed create config dn for %s\n",
                      li->li_plugin->plg_name);
        rval = 1;
        goto bail;
    }

    search_pb = slapi_pblock_new();
    if (!search_pb) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_load_dse_info", "Out of memory\n");
        rval = 1;
        goto bail;
    }

    slapi_search_internal_set_pb(search_pb, dn, LDAP_SCOPE_BASE,
                                 "objectclass=*", NULL, 0, NULL, NULL, li->li_identity, 0);
    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rval);

    if (LDAP_NO_SUCH_OBJECT == rval) {
        /* Add skeleten dse entries for the ldbm plugin */
        ldbm_config_add_dse_entries(li, ldbm_skeleton_entries,
                                    li->li_plugin->plg_name, NULL, NULL, 0);
    } else if (rval != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_load_dse_info",
                      "Error accessing the ldbm config DSE 1\n");
        rval = 1;
        goto bail;
    } else {
        /* Need to parse the configuration information for the ldbm
         * plugin that is held in the DSE. */
        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                         &entries);
        if (NULL == entries || entries[0] == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_load_dse_info", "Error accessing the ldbm config DSE 2\n");
            rval = 1;
            goto bail;
        }
        if (0 != parse_ldbm_config_entry(li, entries[0], ldbm_config)) {
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_load_dse_info", "Error parsing the ldbm config DSE\n");
            rval = 1;
            goto bail;
        }
    }

    if (search_pb) {
        slapi_free_search_results_internal(search_pb);
        slapi_pblock_destroy(search_pb);
    }

    rval = ldbm_config_read_instance_entries(li, li->li_plugin->plg_name);
    if (rval) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "bdb_config_load_dse_info",
                      "failed to read instance entries\n");
        goto bail;
    }

    /* setup the dse callback functions for the ldbm backend config entry */
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", ldbm_config_search_entry_callback,
                                   (void *)li);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", ldbm_config_modify_entry_callback,
                                   (void *)li);
    slapi_config_register_callback(DSE_OPERATION_WRITE, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", ldbm_config_search_entry_callback,
                                   (void *)li);
    slapi_ch_free_string(&dn);

    /* setup the dse callback functions for the ldbm backend instance
     * entries */
    dn = slapi_create_dn_string("cn=%s,cn=plugins,cn=config",
                                li->li_plugin->plg_name);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "ldbm_config_load_dse_info",
                      "failed create plugin dn for %s\n",
                      li->li_plugin->plg_name);
        rval = 1;
        goto bail;
    }
    slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_SUBTREE, "(objectclass=nsBackendInstance)",
                                   ldbm_instance_add_instance_entry_callback, (void *)li);
    slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_POSTOP, dn,
                                   LDAP_SCOPE_SUBTREE, "(objectclass=nsBackendInstance)",
                                   ldbm_instance_postadd_instance_entry_callback, (void *)li);
    slapi_config_register_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_POSTOP, dn,
                                   LDAP_SCOPE_SUBTREE, "(objectclass=nsBackendInstance)",
                                   ldbm_instance_post_delete_instance_entry_callback, (void *)li);
    slapi_config_register_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_SUBTREE, "(objectclass=nsBackendInstance)",
                                   ldbm_instance_delete_instance_entry_callback, (void *)li);
bail:
    slapi_ch_free_string(&dn);
    return rval;
}


/* Utility function used in creating config entries.  Using the
 * config_info, this function gets info and formats in the correct
 * way.
 * buf is char[BUFSIZ]
 */
void
ldbm_config_get(void *arg, config_info *config, char *buf)
{
    void *val = NULL;

    if (config == NULL) {
        buf[0] = '\0';
        return;
    }

    val = config->config_get_fn(arg);
    config_info_print_val(val, config->config_type, buf);

    if (config->config_type == CONFIG_TYPE_STRING) {
        slapi_ch_free((void **)&val);
    }
}

int
ldbm_config_moved_attr(char *attr_name)
{
    /* These are the names of attributes that are no longer
     * in the ldbm config entry. They are specific to the BDB backend
     * implementation. Need to handle them for legacy clients
     */
    for (size_t i = 0; ldbm_config_moved_attributes[i] && *ldbm_config_moved_attributes[i]; i++) {
        if (!strcasecmp(ldbm_config_moved_attributes[i], attr_name)) {
            return 1;
        }
    }

    return 0;
}

int
ldbm_config_ignored_attr(char *attr_name)
{
    /* These are the names of attributes that are in the
     * config entries but are not config attributes. */
    if (!strcasecmp("objectclass", attr_name) ||
        !strcasecmp("cn", attr_name) ||
        !strcasecmp("creatorsname", attr_name) ||
        !strcasecmp("createtimestamp", attr_name) ||
        !strcasecmp(LDBM_NUMSUBORDINATES_STR, attr_name) ||
        slapi_attr_is_last_mod(attr_name)) {
        return 1;
    } else {
        return 0;
    }
}

/*
 * Returns:
 *   SLAPI_DSE_CALLBACK_ERROR on failure
 *   SLAPI_DSE_CALLBACK_OK on success
 */
int
ldbm_config_search_entry_callback(Slapi_PBlock *pb __attribute__((unused)),
                                  Slapi_Entry *e,
                                  Slapi_Entry *entryAfter __attribute__((unused)),
                                  int *returncode,
                                  char *returntext,
                                  void *arg)
{
    char buf[BUFSIZ];
    struct berval *vals[2];
    struct berval val;
    struct ldbminfo *li = (struct ldbminfo *)arg;
    config_info *config;
    int scope;

    vals[0] = &val;
    vals[1] = NULL;

    returntext[0] = '\0';

    PR_Lock(li->li_config_mutex);

    if (pb) {
        slapi_pblock_get(pb, SLAPI_SEARCH_SCOPE, &scope);
        if (scope == LDAP_SCOPE_BASE) {
            char **attrs = NULL;
            slapi_pblock_get(pb, SLAPI_SEARCH_ATTRS, &attrs);
            if (attrs) {
                for (size_t i = 0; attrs[i]; i++) {
                    if (ldbm_config_moved_attr(attrs[i])) {
                        slapi_pblock_set(pb, SLAPI_RESULT_TEXT, "at least one required attribute has been moved to the BDB scecific configuration entry");
                        break;
                    }
                }
            }
        
        }
    }

    for (config = ldbm_config; config->config_name != NULL; config++) {
        /* Go through the ldbm_config table and fill in the entry. */

        if (!(config->config_flags & (CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_PREVIOUSLY_SET))) {
            /* This config option shouldn't be shown */
            continue;
        }

        ldbm_config_get((void *)li, config, buf);

        val.bv_val = buf;
        val.bv_len = strlen(buf);
        slapi_entry_attr_replace(e, config->config_name, vals);
    }

    PR_Unlock(li->li_config_mutex);

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}


/* Returns LDAP_SUCCESS on success */
int
ldbm_config_set(void *arg, char *attr_name, config_info *config_array, struct berval *bval, char *err_buf, int phase, int apply_mod, int mod_op)
{
    config_info *config;
    int use_default;
    int int_val;
    long long_val;
    size_t sz_val;
    PRInt64 llval;
    int maxint = (int)(((unsigned int)~0) >> 1);
    int minint = ~maxint;
    PRInt64 llmaxint;
    PRInt64 llminint;
    int err = 0;
    char *str_val;
    int retval = 0;

    LL_I2L(llmaxint, maxint);
    LL_I2L(llminint, minint);

    config = config_info_get(config_array, attr_name);
    if (NULL == config) {
        slapi_log_err(SLAPI_LOG_CONFIG, "ldbm_config_set", "Unknown config attribute %s\n", attr_name);
        slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Unknown config attribute %s\n", attr_name);
        return LDAP_SUCCESS; /* Ignore unknown attributes */
    }

    /* Some config attrs can't be changed while the server is running. */
    if (phase == CONFIG_PHASE_RUNNING &&
        !(config->config_flags & CONFIG_FLAG_ALLOW_RUNNING_CHANGE)) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_set", "%s can't be modified while the server is running.\n", attr_name);
        slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "%s can't be modified while the server is running.\n", attr_name);
        return LDAP_UNWILLING_TO_PERFORM;
    }

    /* If the config phase is initialization or if bval is NULL or if we are deleting
       the value, we will use the default value for the attribute. */
    if ((CONFIG_PHASE_INITIALIZATION == phase) || (NULL == bval) || SLAPI_IS_MOD_DELETE(mod_op)) {
        if (CONFIG_FLAG_SKIP_DEFAULT_SETTING & config->config_flags) {
            return LDAP_SUCCESS; /* Skipping the default config setting */
        }
        use_default = 1;
    } else {
        use_default = 0;

        /* cannot use mod add on a single valued attribute if the attribute was
           previously set to a non-default value */
        if (SLAPI_IS_MOD_ADD(mod_op) && apply_mod &&
            (config->config_flags & CONFIG_FLAG_PREVIOUSLY_SET)) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "cannot add a value to single valued attribute %s.\n", attr_name);
            return LDAP_OBJECT_CLASS_VIOLATION;
        }
    }

    /* if delete, and a specific value was provided to delete, the existing value must
       match that value, or return LDAP_NO_SUCH_ATTRIBUTE */
    if (SLAPI_IS_MOD_DELETE(mod_op) && bval && bval->bv_len && bval->bv_val) {
        char buf[BUFSIZ];
        ldbm_config_get(arg, config, buf);
        if (PL_strncmp(buf, bval->bv_val, bval->bv_len)) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE,
                                  "value [%s] for attribute %s does not match existing value [%s].\n", bval->bv_val, attr_name, buf);
            return LDAP_NO_SUCH_ATTRIBUTE;
        }
    }

    switch (config->config_type) {
    case CONFIG_TYPE_INT:
        if (use_default) {
            str_val = config->config_default_value;
        } else {
            str_val = bval->bv_val;
        }
        /* get the value as a 64 bit value */
        llval = db_atoi(str_val, &err);
        /* check for parsing error (e.g. not a number) */
        if (err) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is not a number\n", str_val, attr_name);
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_set", "Value %s for attr %s is not a number\n", str_val, attr_name);
            return LDAP_UNWILLING_TO_PERFORM;
            /* check for overflow */
        } else if (LL_CMP(llval, >, llmaxint)) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is greater than the maximum %d\n",
                                  str_val, attr_name, maxint);
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_set", "Value %s for attr %s is greater than the maximum %d\n",
                          str_val, attr_name, maxint);
            return LDAP_UNWILLING_TO_PERFORM;
            /* check for underflow */
        } else if (LL_CMP(llval, <, llminint)) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is less than the minimum %d\n",
                                  str_val, attr_name, minint);
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_set", "Value %s for attr %s is less than the minimum %d\n",
                          str_val, attr_name, minint);
            return LDAP_UNWILLING_TO_PERFORM;
        }
        /* convert 64 bit value to 32 bit value */
        LL_L2I(int_val, llval);
        retval = config->config_set_fn(arg, (void *)((uintptr_t)int_val), err_buf, phase, apply_mod);
        break;
    case CONFIG_TYPE_INT_OCTAL:
        if (use_default) {
            int_val = (int)strtol(config->config_default_value, NULL, 8);
        } else {
            int_val = (int)strtol((char *)bval->bv_val, NULL, 8);
        }
        retval = config->config_set_fn(arg, (void *)((uintptr_t)int_val), err_buf, phase, apply_mod);
        break;
    case CONFIG_TYPE_LONG:
        if (use_default) {
            str_val = config->config_default_value;
        } else {
            str_val = bval->bv_val;
        }
        /* get the value as a 64 bit value */
        llval = db_atoi(str_val, &err);
        /* check for parsing error (e.g. not a number) */
        if (err) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is not a number\n",
                                  str_val, attr_name);
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_set", "Value %s for attr %s is not a number\n",
                          str_val, attr_name);
            return LDAP_UNWILLING_TO_PERFORM;
            /* check for overflow */
        } else if (LL_CMP(llval, >, llmaxint)) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is greater than the maximum %d\n",
                                  str_val, attr_name, maxint);
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_set", "Value %s for attr %s is greater than the maximum %d\n",
                          str_val, attr_name, maxint);
            return LDAP_UNWILLING_TO_PERFORM;
            /* check for underflow */
        } else if (LL_CMP(llval, <, llminint)) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is less than the minimum %d\n",
                                  str_val, attr_name, minint);
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_set", "Value %s for attr %s is less than the minimum %d\n",
                          str_val, attr_name, minint);
            return LDAP_UNWILLING_TO_PERFORM;
        }
        /* convert 64 bit value to 32 bit value */
        LL_L2I(long_val, llval);
        retval = config->config_set_fn(arg, (void *)long_val, err_buf, phase, apply_mod);
        break;
    case CONFIG_TYPE_SIZE_T:
        if (use_default) {
            str_val = config->config_default_value;
        } else {
            str_val = bval->bv_val;
        }

        /* get the value as a size_t value */
        sz_val = db_strtoul(str_val, &err);

        /* check for parsing error (e.g. not a number) */
        if (err == EINVAL) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is not a number\n",
                                  str_val, attr_name);
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_set", "Value %s for attr %s is not a number\n",
                          str_val, attr_name);
            return LDAP_UNWILLING_TO_PERFORM;
            /* check for overflow */
        } else if (err == ERANGE) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is outside the range of representable values\n",
                                  str_val, attr_name);
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_set", "Value %s for attr %s is outside the range of representable values\n",
                          str_val, attr_name);
            return LDAP_UNWILLING_TO_PERFORM;
        }
        retval = config->config_set_fn(arg, (void *)sz_val, err_buf, phase, apply_mod);
        break;


    case CONFIG_TYPE_UINT64:
        if (use_default) {
            str_val = config->config_default_value;
        } else {
            str_val = bval->bv_val;
        }
        /* get the value as a size_t value */
        sz_val = db_strtoull(str_val, &err);

        /* check for parsing error (e.g. not a number) */
        if (err == EINVAL) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is not a number\n",
                                  str_val, attr_name);
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_set", "Value %s for attr %s is not a number\n",
                          str_val, attr_name);
            return LDAP_UNWILLING_TO_PERFORM;
        /* check for overflow */
        } else if (err == ERANGE) {
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is outside the range of representable values\n",
                                  str_val, attr_name);
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_set", "Value %s for attr %s is outside the range of representable values\n",
                          str_val, attr_name);
            return LDAP_UNWILLING_TO_PERFORM;
        }
        retval = config->config_set_fn(arg, (void *)sz_val, err_buf, phase, apply_mod);
        break;
    case CONFIG_TYPE_STRING:
        if (use_default) {
            retval = config->config_set_fn(arg, config->config_default_value, err_buf, phase, apply_mod);
        } else {
            retval = config->config_set_fn(arg, bval->bv_val, err_buf, phase, apply_mod);
        }
        break;
    case CONFIG_TYPE_ONOFF:
        if (use_default) {
            int_val = !strcasecmp(config->config_default_value, "on");
        } else {
            int_val = !strcasecmp((char *)bval->bv_val, "on");
        }
        retval = config->config_set_fn(arg, (void *)((uintptr_t)int_val), err_buf, phase, apply_mod);
        break;
    }

    /* operation was successful and we applied the value? */
    if (!retval && apply_mod) {
        /* Since we are setting the value for the config attribute, we
         * need to turn on the CONFIG_FLAG_PREVIOUSLY_SET flag to make
         * sure this attribute is shown. */
        if (use_default) {
            /* attr deleted or we are using the default value */
            config->config_flags &= ~CONFIG_FLAG_PREVIOUSLY_SET;
        } else {
            /* attr set explicitly */
            config->config_flags |= CONFIG_FLAG_PREVIOUSLY_SET;
        }
    }

    return retval;
}


static int
parse_ldbm_config_entry(struct ldbminfo *li, Slapi_Entry *e, config_info *config_array)
{
    Slapi_Attr *attr = NULL;

    for (slapi_entry_first_attr(e, &attr); attr; slapi_entry_next_attr(e, attr, &attr)) {
        char *attr_name = NULL;
        Slapi_Value *sval = NULL;
        struct berval *bval;
        char err_buf[SLAPI_DSE_RETURNTEXT_SIZE];

        slapi_attr_get_type(attr, &attr_name);

        /* There are some attributes that we don't care about, like objectclass. */
        if (ldbm_config_ignored_attr(attr_name)) {
            continue;
        }
        slapi_attr_first_value(attr, &sval);
        bval = (struct berval *)slapi_value_get_berval(sval);

        if (ldbm_config_set(li, attr_name, config_array, bval, err_buf, CONFIG_PHASE_STARTUP, 1 /* apply */, LDAP_MOD_REPLACE) != LDAP_SUCCESS) {
            slapi_log_err(SLAPI_LOG_ERR, "parse_ldbm_config_entry", "Error with config attribute %s : %s\n", attr_name, err_buf);
            return 1;
        }
    }
    return 0;
}

/* helper for deleting mods (we do not want to be applied) from the mods array */
static void
mod_free(LDAPMod *mod)
{
    ber_bvecfree(mod->mod_bvalues);
    slapi_ch_free((void **)&(mod->mod_type));
    slapi_ch_free((void **)&mod);
}

/*
 * Returns:
 *   SLAPI_DSE_CALLBACK_ERROR on failure
 *   SLAPI_DSE_CALLBACK_OK on success
 */
int
ldbm_config_modify_entry_callback(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *e, int *returncode, char *returntext, void *arg)
{
    int i;
    char *attr_name;
    LDAPMod **mods;
    Slapi_Mods smods_moved;
    int rc = LDAP_SUCCESS;
    int apply_mod = 0;
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int reapply_mods = 0;
    int move_mods = 0;
    int idx = 0;
    int internal_op = 0;
    Slapi_Operation *operation = NULL;

    /* This lock is probably way too conservative, but we don't expect much
     * contention for it. */
    PR_Lock(li->li_config_mutex);

    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;
    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
    slapi_mods_init(&smods_moved, 0);

    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    internal_op = operation_is_flag_set(operation, OP_FLAG_INTERNAL);

    returntext[0] = '\0';

    slapi_log_err(SLAPI_LOG_CONFIG, "ldbm_config_modify_entry_callback", "Executing for entry (%s) with flags (%d) operation is internal: %d\n",
                    slapi_entry_get_dn_const(e), li->li_flags, internal_op);
    /*
     * First pass: set apply mods to 0 so only input validation will be done;
     * 2nd pass: set apply mods to 1 to apply changes to internal storage
     */
    for (apply_mod = 0; apply_mod <= 1 && LDAP_SUCCESS == rc; apply_mod++) {
        for (i = 0; mods && mods[i] && LDAP_SUCCESS == rc; i++) {
            attr_name = mods[i]->mod_type;

            /* There are some attributes that we don't care about, like modifiersname. */
            if (ldbm_config_ignored_attr(attr_name)) {
                if (apply_mod) {
                    Slapi_Attr *origattr = NULL;
                    Slapi_ValueSet *origvalues = NULL;
                    mods[idx++] = mods[i];
                    /* we also need to restore the entryAfter e to its original
                       state, because the dse code will attempt to reapply
                       the mods again */
                    slapi_entry_attr_find(entryBefore, attr_name, &origattr);
                    if (NULL != origattr) {
                        slapi_attr_get_valueset(origattr, &origvalues);
                        if (NULL != origvalues) {
                            slapi_entry_add_valueset(e, attr_name, origvalues);
                            slapi_valueset_free(origvalues);
                        }
                    }
                    reapply_mods = 1; /* there is at least one mod we removed */
                }
                continue;
            }


            if (ldbm_config_moved_attr(attr_name) && !internal_op) {
                rc = priv->dblayer_config_set_fn(li, attr_name, apply_mod, mods[i]->mod_op,
                                            CONFIG_PHASE_RUNNING,
                                            (mods[i]->mod_bvalues == NULL) ? NULL
                                            : mods[i]->mod_bvalues[0]->bv_val);
                if (apply_mod) {
                    slapi_entry_attr_delete(e, attr_name);
                    slapi_mods_add_ldapmod(&smods_moved, mods[i]);
                    move_mods++;
                    reapply_mods = 1; /* there is at least one mod we removed */
                }
                continue;
            }
            /* when deleting a value, and this is the last or only value, set
               the config param to its default value
               when adding a value, if the value is set to its default value, replace
               it with the new value - otherwise, if it is single valued, reject the
               operation with TYPE_OR_VALUE_EXISTS */
            /* This assumes there is only one bval for this mod. */
            rc = ldbm_config_set((void *)li, attr_name, ldbm_config,
                                 (mods[i]->mod_bvalues == NULL) ? NULL
                                                                : mods[i]->mod_bvalues[0],
                                 returntext,
                                 ((li->li_flags & LI_FORCE_MOD_CONFIG) ? CONFIG_PHASE_INTERNAL : CONFIG_PHASE_RUNNING),
                                 apply_mod, mods[i]->mod_op);
            if (rc) {
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_config_modify_entry_callback", "Modifying config attribute %s failed (err=%d)\n", attr_name, rc);
            }
            if (apply_mod) {
                mod_free(mods[i]);
                mods[i] = NULL;
            }
        }
    }

    PR_Unlock(li->li_config_mutex);

    if (reapply_mods) {
        mods[idx] = NULL;
        slapi_pblock_set(pb, SLAPI_DSE_REAPPLY_MODS, &reapply_mods);
    }

    if (move_mods) {
        char *dn = slapi_ch_smprintf("cn=bdb,%s",CONFIG_LDBM_DN);
        Slapi_PBlock *mod_pb = slapi_pblock_new();
        slapi_modify_internal_set_pb(mod_pb, dn,
                                     slapi_mods_get_ldapmods_byref(&smods_moved),
                                     NULL, NULL, li->li_identity, 0);
        slapi_modify_internal_pb(mod_pb);
        slapi_pblock_destroy(mod_pb);
        slapi_ch_free_string(&dn);
        slapi_mods_done(&smods_moved);
    }

    *returncode = rc;
    if (LDAP_SUCCESS == rc) {
        return SLAPI_DSE_CALLBACK_OK;
    } else {
        return SLAPI_DSE_CALLBACK_ERROR;
    }
}


/* This function is used to set config attributes. It can be used as a
 * shortcut to doing an internal modify operation on the config DSE.
 */
void
ldbm_config_internal_set(struct ldbminfo *li, char *attrname, char *value)
{
    char err_buf[SLAPI_DSE_RETURNTEXT_SIZE];
    struct berval bval;

    bval.bv_val = value;
    bval.bv_len = strlen(value);

    if (ldbm_config_set((void *)li, attrname, ldbm_config, &bval,
                        err_buf, CONFIG_PHASE_INTERNAL, 1 /* apply */,
                        LDAP_MOD_REPLACE) != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "ldbm_config_internal_set", "Error setting instance config attr %s to %s: %s\n",
                      attrname, value, err_buf);
        exit(1);
    }
}

/*
 * replace_ldbm_config_value:
 * - update an ldbm database config value
 */
void
replace_ldbm_config_value(char *conftype, char *val, struct ldbminfo *li)
{
    Slapi_PBlock *pb = slapi_pblock_new();
    Slapi_Mods smods;

    slapi_mods_init(&smods, 1);
    slapi_mods_add(&smods, LDAP_MOD_REPLACE, conftype, strlen(val), val);
    slapi_modify_internal_set_pb(pb, CONFIG_LDBM_DN,
                                 slapi_mods_get_ldapmods_byref(&smods),
                                 NULL, NULL, li->li_identity, 0);
    slapi_modify_internal_pb(pb);
    slapi_mods_done(&smods);
    slapi_pblock_destroy(pb);
}

/* Dispose of an ldbminfo struct for good */
void
ldbm_config_destroy(struct ldbminfo *li)
{
    if (li->li_attrs_to_exclude_from_export != NULL) {
        charray_free(li->li_attrs_to_exclude_from_export);
    }
    slapi_ch_free((void **)&(li->li_new_directory));
    slapi_ch_free((void **)&(li->li_directory));
    slapi_ch_free((void **)&(li->li_backend_implement));
    /* Destroy the mutexes and cond var */
    PR_DestroyLock(li->li_shutdown_mutex);
    PR_DestroyLock(li->li_config_mutex);

    /* Finally free the ldbminfo */
    slapi_ch_free((void **)&li);
}

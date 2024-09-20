/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * memberof_config.c - configuration-related code for memberOf plug-in
 *
 */
#include "plhash.h"
#include <plstr.h>
#include "memberof.h"

#define MEMBEROF_CONFIG_FILTER "(objectclass=*)"
#define MEMBEROF_HASHTABLE_SIZE 1000

/*
 * The configuration attributes are contained in the plugin entry e.g.
 * cn=MemberOf Plugin,cn=plugins,cn=config
 *
 * Configuration is a two step process.  The first pass is a validation step which
 * occurs pre-op - check inputs and error out if bad.  The second pass actually
 * applies the changes to the run time config.
 */


/*
 * function prototypes
 */
static void fixup_hashtable_empty( MemberOfConfig *config, char *msg);
static void ancestor_hashtable_empty(MemberOfConfig *config, char *msg);
static int memberof_validate_config (Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e,
                                     int *returncode, char *returntext, void *arg);
static int memberof_search (Slapi_PBlock *pb __attribute__((unused)),
                            Slapi_Entry* entryBefore __attribute__((unused)),
                            Slapi_Entry* e __attribute__((unused)),
                            int *returncode __attribute__((unused)),
                            char *returntext __attribute__((unused)),
                            void *arg __attribute__((unused)))
{
    return SLAPI_DSE_CALLBACK_OK;
}

/*
 * static variables
 */
/* This is the main configuration which is updated from dse.ldif.  The
 * config will be copied when it is used by the plug-in to prevent it
 * being changed out from under a running memberOf operation. */
static MemberOfConfig theConfig = {0};
static Slapi_RWLock *memberof_config_lock = 0;
static int inited = 0;


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

static void
memberof_free_scope(Slapi_DN ***scopes, int *count)
{
    size_t i = 0;

    while (*scopes && (*scopes)[i]) {
        slapi_sdn_free(&(*scopes)[i]);
        i++;
    }
    slapi_ch_free((void **)scopes);
    *count = 0;
}

/*
 * memberof_config()
 *
 * Read configuration and create a configuration data structure.
 * This is called after the server has configured itself so we can
 * perform checks with regards to suffixes if it ever becomes
 * necessary.
 * Returns an LDAP error code (LDAP_SUCCESS if all goes well).
 */
int
memberof_config(Slapi_Entry *config_e, Slapi_PBlock *pb)
{
    int returncode = LDAP_SUCCESS;
    char returntext[SLAPI_DSE_RETURNTEXT_SIZE];

    if (inited) {
        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_config - Only one memberOf plugin instance can be used\n");
        return (LDAP_PARAM_ERROR);
    }

    /* initialize the RW lock to protect the main config */
    memberof_config_lock = slapi_new_rwlock();

    /* initialize fields */
    if (SLAPI_DSE_CALLBACK_OK == memberof_validate_config(NULL, NULL, config_e,
                                                          &returncode, returntext, NULL)) {
        memberof_apply_config(NULL, NULL, config_e, &returncode, returntext, NULL);
    }

    /*
     * config DSE must be initialized before we get here we only need the dse callbacks
     * for the plugin entry, but not the shared config entry.
     */
    if (returncode == LDAP_SUCCESS) {
        const char *config_dn = slapi_sdn_get_dn(memberof_get_plugin_area());
        slapi_config_register_callback_plugin(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP | DSE_FLAG_PLUGIN,
                                              config_dn, LDAP_SCOPE_BASE, MEMBEROF_CONFIG_FILTER,
                                              memberof_validate_config, NULL, pb);
        slapi_config_register_callback_plugin(SLAPI_OPERATION_MODIFY, DSE_FLAG_POSTOP | DSE_FLAG_PLUGIN,
                                              config_dn, LDAP_SCOPE_BASE, MEMBEROF_CONFIG_FILTER,
                                              memberof_apply_config, NULL, pb);
        slapi_config_register_callback_plugin(SLAPI_OPERATION_MODRDN, DSE_FLAG_PREOP | DSE_FLAG_PLUGIN,
                                              config_dn, LDAP_SCOPE_BASE, MEMBEROF_CONFIG_FILTER,
                                              dont_allow_that, NULL, pb);
        slapi_config_register_callback_plugin(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP | DSE_FLAG_PLUGIN,
                                              config_dn, LDAP_SCOPE_BASE, MEMBEROF_CONFIG_FILTER,
                                              dont_allow_that, NULL, pb);
        slapi_config_register_callback_plugin(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP | DSE_FLAG_PLUGIN,
                                              config_dn, LDAP_SCOPE_BASE, MEMBEROF_CONFIG_FILTER,
                                              memberof_search, NULL, pb);
    }

    inited = 1;

    if (returncode != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_config - Error %d: %s\n", returncode, returntext);
    }

    return returncode;
}

void
memberof_release_config()
{
    const char *config_dn = slapi_sdn_get_dn(memberof_get_plugin_area());

    slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP,
                                 config_dn, LDAP_SCOPE_BASE, MEMBEROF_CONFIG_FILTER,
                                 memberof_validate_config);
    slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_POSTOP,
                                 config_dn, LDAP_SCOPE_BASE, MEMBEROF_CONFIG_FILTER,
                                 memberof_apply_config);
    slapi_config_remove_callback(SLAPI_OPERATION_MODRDN, DSE_FLAG_PREOP,
                                 config_dn, LDAP_SCOPE_BASE, MEMBEROF_CONFIG_FILTER,
                                 dont_allow_that);
    slapi_config_remove_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP,
                                 config_dn, LDAP_SCOPE_BASE, MEMBEROF_CONFIG_FILTER,
                                 dont_allow_that);
    slapi_config_remove_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP,
                                 config_dn, LDAP_SCOPE_BASE, MEMBEROF_CONFIG_FILTER,
                                 memberof_search);

    slapi_destroy_rwlock(memberof_config_lock);
    memberof_config_lock = NULL;
    inited = 0;
}

/*
 * memberof_validate_config()
 *
 * Validate the pending changes in the e entry.
 */
int
memberof_validate_config(Slapi_PBlock *pb,
                         Slapi_Entry *entryBefore __attribute__((unused)),
                         Slapi_Entry *e,
                         int *returncode,
                         char *returntext,
                         void *arg __attribute__((unused)))
{
    Slapi_Attr *memberof_attr = NULL;
    Slapi_Attr *group_attr = NULL;
    Slapi_DN *config_sdn = NULL;
    Slapi_DN **include_dn = NULL;
    Slapi_DN **exclude_dn = NULL;
    char *syntaxoid = NULL;
    char *config_dn = NULL;
    const char *skip_nested = NULL;
    const char *auto_add_oc = NULL;
    char **entry_scopes = NULL;
    char **entry_exclude_scopes = NULL;
    int not_dn_syntax = 0;
    int num_vals = 0;

    *returncode = LDAP_UNWILLING_TO_PERFORM; /* be pessimistic */

    /* Make sure both the group attr and the memberOf attr
     * config atributes are supplied.  We don't care about &attr
     * here, but slapi_entry_attr_find() requires us to pass it. */
    if (!slapi_entry_attr_find(e, MEMBEROF_GROUP_ATTR, &group_attr) &&
        !slapi_entry_attr_find(e, MEMBEROF_ATTR, &memberof_attr)) {
        Slapi_Attr *test_attr = NULL;
        Slapi_Value *value = NULL;
        int hint = 0;

        /* Loop through each group attribute to see if the syntax is correct. */
        hint = slapi_attr_first_value(group_attr, &value);
        while (value && (not_dn_syntax == 0)) {
            /* We need to create an attribute to find the syntax. */
            test_attr = slapi_attr_new();
            slapi_attr_init(test_attr, slapi_value_get_string(value));

            /* Get the syntax OID and see if it's the Distinguished Name or
             * Name and Optional UID syntax. */
            slapi_attr_get_syntax_oid_copy(test_attr, &syntaxoid);
            not_dn_syntax = strcmp(syntaxoid, DN_SYNTAX_OID) & strcmp(syntaxoid, NAME_OPT_UID_SYNTAX_OID);
            slapi_ch_free_string(&syntaxoid);

            /* Print an error if the current attribute is not using the Distinguished
             * Name syntax, otherwise get the next group attribute. */
            if (not_dn_syntax) {
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "The %s configuration attribute must be set to "
                            "an attribute defined to use either the Distinguished "
                            "Name or Name and Optional UID syntax. (illegal value: %s)",
                            slapi_value_get_string(value), MEMBEROF_GROUP_ATTR);
            } else {
                hint = slapi_attr_next_value(group_attr, hint, &value);
            }

            /* Free the group attribute. */
            slapi_attr_free(&test_attr);
        }

        if (not_dn_syntax == 0) {
            /* Check the syntax of the memberof attribute. */
            slapi_attr_first_value(memberof_attr, &value);
            test_attr = slapi_attr_new();
            slapi_attr_init(test_attr, slapi_value_get_string(value));
            slapi_attr_get_syntax_oid_copy(test_attr, &syntaxoid);
            not_dn_syntax = strcmp(syntaxoid, DN_SYNTAX_OID);
            slapi_ch_free_string(&syntaxoid);
            slapi_attr_free(&test_attr);

            if (not_dn_syntax) {
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "The %s configuration attribute must be set to "
                            "an attribute defined to use the Distinguished "
                            "Name syntax.  (illegal value: %s)",
                            slapi_value_get_string(value), MEMBEROF_ATTR);
                goto done;
            } else {
                *returncode = LDAP_SUCCESS;
            }
        }
    } else {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                    "The %s and %s configuration attributes must be provided",
                    MEMBEROF_GROUP_ATTR, MEMBEROF_ATTR);
        goto done;
    }

    if ((skip_nested = slapi_entry_attr_get_ref(e, MEMBEROF_SKIP_NESTED_ATTR))) {
        if (strcasecmp(skip_nested, "on") != 0 && strcasecmp(skip_nested, "off") != 0) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                        "The %s configuration attribute must be set to "
                        "\"on\" or \"off\".  (illegal value: %s)",
                        MEMBEROF_SKIP_NESTED_ATTR, skip_nested);
            goto done;
        }
    }

    /* Setup a default auto add OC */
    auto_add_oc = slapi_entry_attr_get_ref(e, MEMBEROF_AUTO_ADD_OC);
    if (auto_add_oc == NULL) {
        auto_add_oc = NSMEMBEROF;
    }

    if (auto_add_oc != NULL) {
        char *sup = NULL;

        /* Check if the objectclass exists by looking for its superior oc */
        if ((sup = slapi_schema_get_superior_name(auto_add_oc)) == NULL) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                        "The %s configuration attribute must be set "
                        "to an existing objectclass  (unknown: %s)",
                        MEMBEROF_AUTO_ADD_OC, auto_add_oc);
            *returncode = LDAP_UNWILLING_TO_PERFORM;
            goto done;
        } else {
            slapi_ch_free_string(&sup);
        }
    }

    if ((config_dn = (char *)slapi_entry_attr_get_ref(e, SLAPI_PLUGIN_SHARED_CONFIG_AREA))) {
        /* Now check the shared config attribute, validate it now */
        Slapi_Entry *config_e = NULL;
        int rc = 0;

        rc = slapi_dn_syntax_check(pb, config_dn, 1);
        if (rc) { /* syntax check failed */
            slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM, "memberof_validate_config - "
                                                                    "%s does not contain a valid DN (%s)\n",
                          SLAPI_PLUGIN_SHARED_CONFIG_AREA, config_dn);
            *returncode = LDAP_INVALID_DN_SYNTAX;
            goto done;
        }
        config_sdn = slapi_sdn_new_dn_byval(config_dn);

        slapi_search_internal_get_entry(config_sdn, NULL, &config_e, memberof_get_plugin_id());
        if (config_e) {
            slapi_entry_free(config_e);
            *returncode = LDAP_SUCCESS;
        } else {
            /* config area does not exist! */
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                        "The %s configuration attribute points to an entry that  "
                        "can not be found.  (%s)",
                        SLAPI_PLUGIN_SHARED_CONFIG_AREA, config_dn);
            *returncode = LDAP_UNWILLING_TO_PERFORM;
        }
    }
    /*
     * Check the entry scopes
     */
    entry_scopes = slapi_entry_attr_get_charray_ext(e, MEMBEROF_ENTRY_SCOPE_ATTR, &num_vals);
    if (entry_scopes) {
        int i = 0;

        /* Validate the syntax before we create our DN array */
        for (i = 0; i < num_vals; i++) {
            if (slapi_dn_syntax_check(pb, entry_scopes[i], 1)) {
                /* invalid dn syntax */
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "%s: Invalid DN (%s) for include suffix.",
                            MEMBEROF_PLUGIN_SUBSYSTEM, entry_scopes[i]);
                slapi_ch_array_free(entry_scopes);
                entry_scopes = NULL;
                theConfig.entryScopeCount = 0;
                *returncode = LDAP_UNWILLING_TO_PERFORM;
                goto done;
            }
        }
        /* Now create our SDN array for conflict checking */
        include_dn = (Slapi_DN **)slapi_ch_calloc(sizeof(Slapi_DN *), num_vals + 1);
        for (i = 0; i < num_vals; i++) {
            include_dn[i] = slapi_sdn_new_dn_passin(entry_scopes[i]);
        }
    }
    /*
     * Check and process the entry exclude scopes
     */
    entry_exclude_scopes =
        slapi_entry_attr_get_charray_ext(e, MEMBEROF_ENTRY_SCOPE_EXCLUDE_SUBTREE, &num_vals);
    if (entry_exclude_scopes) {
        int i = 0;

        /* Validate the syntax before we create our DN array */
        for (i = 0; i < num_vals; i++) {
            if (slapi_dn_syntax_check(pb, entry_exclude_scopes[i], 1)) {
                /* invalid dn syntax */
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "%s: Invalid DN (%s) for exclude suffix.",
                            MEMBEROF_PLUGIN_SUBSYSTEM, entry_exclude_scopes[i]);
                slapi_ch_array_free(entry_exclude_scopes);
                entry_exclude_scopes = NULL;
                *returncode = LDAP_UNWILLING_TO_PERFORM;
                goto done;
            }
        }
        /* Now create our SDN array for conflict checking */
        exclude_dn = (Slapi_DN **)slapi_ch_calloc(sizeof(Slapi_DN *), num_vals + 1);
        for (i = 0; i < num_vals; i++) {
            exclude_dn[i] = slapi_sdn_new_dn_passin(entry_exclude_scopes[i]);
        }
    }
    /*
     * Need to do conflict checking
     */
    if (include_dn && exclude_dn) {
        /*
         * Make sure we haven't mixed the same suffix, and there are no
         * conflicts between the includes and excludes
         */
        int i = 0;

        while (include_dn[i]) {
            int x = 0;
            while (exclude_dn[x]) {
                if (slapi_sdn_compare(include_dn[i], exclude_dn[x]) == 0) {
                    /* we have a conflict */
                    PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                "%s: include suffix (%s) is also listed as an exclude suffix list",
                                MEMBEROF_PLUGIN_SUBSYSTEM, slapi_sdn_get_dn(include_dn[i]));
                    *returncode = LDAP_UNWILLING_TO_PERFORM;
                    goto done;
                }
                x++;
            }
            i++;
        }

        /* Check for parent/child conflicts */
        i = 0;
        while (include_dn[i]) {
            int x = 0;
            while (exclude_dn[x]) {
                if (slapi_sdn_issuffix(include_dn[i], exclude_dn[x])) {
                    /* we have a conflict */
                    PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                "%s: include suffix (%s) is a child of the exclude suffix(%s)",
                                MEMBEROF_PLUGIN_SUBSYSTEM,
                                slapi_sdn_get_dn(include_dn[i]),
                                slapi_sdn_get_dn(exclude_dn[i]));
                    *returncode = LDAP_UNWILLING_TO_PERFORM;
                    goto done;
                }
                x++;
            }
            i++;
        }
    }

done:
    memberof_free_scope(&exclude_dn, &num_vals);
    memberof_free_scope(&include_dn, &num_vals);
    slapi_ch_free((void **)&entry_scopes);
    slapi_ch_free((void **)&entry_exclude_scopes);
    slapi_sdn_free(&config_sdn);

    if (*returncode != LDAP_SUCCESS) {
        return SLAPI_DSE_CALLBACK_ERROR;
    } else {
        return SLAPI_DSE_CALLBACK_OK;
    }
}

/*
 * memberof_apply_config()
 *
 * Apply the pending changes in the e entry to our config struct.
 * memberof_validate_config()  must have already been called.
 */
int
memberof_apply_config(Slapi_PBlock *pb __attribute__((unused)),
                      Slapi_Entry *entryBefore __attribute__((unused)),
                      Slapi_Entry *e,
                      int *returncode,
                      char *returntext,
                      void *arg __attribute__((unused)))
{
    Slapi_Entry *config_entry = NULL;
    Slapi_DN *config_sdn = NULL;
    char **groupattrs = NULL;
    char *memberof_attr = NULL;
    char *filter_str = NULL;
    int num_groupattrs = 0;
    int groupattr_name_len = 0;
    const char *allBackends = NULL;
    char **entryScopes = NULL;
    char **entryScopeExcludeSubtrees = NULL;
    char *sharedcfg = NULL;
    const char *skip_nested = NULL;
    const char *deferred_update = NULL;
    char *auto_add_oc = NULL;
    const char *needfixup = NULL;
    int num_vals = 0;

    *returncode = LDAP_SUCCESS;

    /*
     * Check if this is a shared config entry
     */
    sharedcfg = (char *)slapi_entry_attr_get_ref(e, SLAPI_PLUGIN_SHARED_CONFIG_AREA);
    if (sharedcfg) {
        if ((config_sdn = slapi_sdn_new_dn_byval(sharedcfg))) {
            slapi_search_internal_get_entry(config_sdn, NULL, &config_entry, memberof_get_plugin_id());
            if (config_entry) {
                /* Set the entry to be the shared config entry.  Validation was done in preop */
                e = config_entry;
            } else {
                /* This should of been checked in preop validation */
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "memberof_apply_config - Failed to locate shared config entry (%s)",
                            sharedcfg);
                slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM, "%s\n", returntext);
                *returncode = LDAP_UNWILLING_TO_PERFORM;
                goto done;
            }
        }
    }

    /*
     * Apply the config settings
     */
    groupattrs = slapi_entry_attr_get_charray(e, MEMBEROF_GROUP_ATTR);
    memberof_attr = slapi_entry_attr_get_charptr(e, MEMBEROF_ATTR);
    allBackends = slapi_entry_attr_get_ref(e, MEMBEROF_BACKEND_ATTR);
    skip_nested = slapi_entry_attr_get_ref(e, MEMBEROF_SKIP_NESTED_ATTR);
    deferred_update = slapi_entry_attr_get_ref(e, MEMBEROF_DEFERRED_UPDATE_ATTR);
    auto_add_oc = slapi_entry_attr_get_charptr(e, MEMBEROF_AUTO_ADD_OC);
    needfixup = slapi_entry_attr_get_ref(e, MEMBEROF_NEED_FIXUP);

    if (auto_add_oc == NULL) {
        auto_add_oc = slapi_ch_strdup(NSMEMBEROF);
    }

    /*
     * We want to be sure we don't change the config in the middle of
     * a memberOf operation, so we obtain an exclusive lock here
     */
    memberof_wlock_config();
    theConfig.need_fixup = (needfixup != NULL);

    if (groupattrs) {
        int i = 0;

        slapi_ch_array_free(theConfig.groupattrs);
        theConfig.groupattrs = groupattrs;
        groupattrs = NULL; /* config now owns memory */

        /*
         * We allocate a list of Slapi_Attr using the groupattrs for
         * convenience in our memberOf comparison functions
         */
        for (i = 0; theConfig.group_slapiattrs && theConfig.group_slapiattrs[i]; i++) {
            slapi_attr_free(&theConfig.group_slapiattrs[i]);
        }

        /* Count the number of groupattrs. */
        for (num_groupattrs = 0; theConfig.groupattrs && theConfig.groupattrs[num_groupattrs]; num_groupattrs++) {
            /*
             * Add up the total length of all attribute names.  We need
             * to know this for building the group check filter later.
             */
            groupattr_name_len += strlen(theConfig.groupattrs[num_groupattrs]);
        }

        /* Realloc the list of Slapi_Attr if necessary. */
        if (i < num_groupattrs) {
            theConfig.group_slapiattrs = (Slapi_Attr **)slapi_ch_realloc((char *)theConfig.group_slapiattrs,
                                                                         sizeof(Slapi_Attr *) * (num_groupattrs + 1));
        }

        /* Build the new list */
        for (i = 0; theConfig.group_slapiattrs &&
                    theConfig.groupattrs &&
                    theConfig.groupattrs[i]; i++)
        {
            theConfig.group_slapiattrs[i] = slapi_attr_new();
            slapi_attr_init(theConfig.group_slapiattrs[i], theConfig.groupattrs[i]);
        }

        /* Terminate the list. */
        if (theConfig.group_slapiattrs) {
            theConfig.group_slapiattrs[i] = NULL;
        }

        /* The filter is based off of the groupattr, so we update it here too. */
        slapi_filter_free(theConfig.group_filter, 1);

        if (num_groupattrs > 1) {
            int bytes_out = 0;
            int filter_str_len = groupattr_name_len + (num_groupattrs * 4) + 4;

            /* Allocate enough space for the filter */
            filter_str = slapi_ch_malloc(filter_str_len);

            /* Add beginning of filter. */
            bytes_out = snprintf(filter_str, filter_str_len - bytes_out, "(|");

            /* Add filter section for each groupattr. */
            for (i = 0; theConfig.groupattrs && theConfig.groupattrs[i]; i++) {
                bytes_out += snprintf(filter_str + bytes_out, filter_str_len - bytes_out, "(%s=*)", theConfig.groupattrs[i]);
            }

            /* Add end of filter. */
            snprintf(filter_str + bytes_out, filter_str_len - bytes_out, ")");
        } else {
            filter_str = slapi_ch_smprintf("(%s=*)", theConfig.groupattrs[0]);
        }

        /*
         * Log an error if we were unable to build the group filter for some
         * reason.  If this happens, the memberOf plugin will not be able to
         * check if an entry is a group, causing it to not catch changes.  This
         * shouldn't happen, but there may be some garbage configuration that
         * could trigger this.
         */
        if ((theConfig.group_filter = slapi_str2filter(filter_str)) == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                          "memberof_apply_config - Unable to create the group check filter.  The memberOf "
                          "plug-in will not operate on changes to groups.  Please check "
                          "your %s configuration settings. (filter: %s)\n",
                          MEMBEROF_GROUP_ATTR, filter_str);
        }

        slapi_ch_free_string(&filter_str);
    }

    if (memberof_attr) {
        slapi_ch_free_string(&theConfig.memberof_attr);
        theConfig.memberof_attr = memberof_attr;
        memberof_attr = NULL; /* config now owns memory */
    }

    if (skip_nested) {
        if (strcasecmp(skip_nested, "on") == 0) {
            theConfig.skip_nested = 1;
        } else {
            theConfig.skip_nested = 0;
        }
    }


    if (deferred_update) {
        if (strcasecmp(deferred_update, "on") == 0) {
            theConfig.deferred_update = PR_TRUE;
        } else {
            theConfig.deferred_update = PR_FALSE;
        }
    }

    if (allBackends) {
        if (strcasecmp(allBackends, "on") == 0) {
            theConfig.allBackends = 1;
        } else {
            theConfig.allBackends = 0;
        }
    } else {
        theConfig.allBackends = 0;
    }

    slapi_ch_free_string(&(theConfig.auto_add_oc));
    theConfig.auto_add_oc = auto_add_oc;

    /*
     * Check and process the entry scopes
     */
    memberof_free_scope(&(theConfig.entryScopes), &theConfig.entryScopeCount);
    entryScopes = slapi_entry_attr_get_charray_ext(e, MEMBEROF_ENTRY_SCOPE_ATTR, &num_vals);
    if (entryScopes) {
        int i = 0;

        /* Validation has already been performed in preop, just build the DN's */
        theConfig.entryScopes = (Slapi_DN **)slapi_ch_calloc(sizeof(Slapi_DN *), num_vals + 1);
        for (i = 0; i < num_vals; i++) {
            theConfig.entryScopes[i] = slapi_sdn_new_dn_passin(entryScopes[i]);
        }
        theConfig.entryScopeCount = num_vals; /* shortcut for config copy */
    }
    /*
     * Check and process the entry exclude scopes
     */
    memberof_free_scope(&(theConfig.entryScopeExcludeSubtrees),
                        &theConfig.entryExcludeScopeCount);
    entryScopeExcludeSubtrees =
        slapi_entry_attr_get_charray_ext(e, MEMBEROF_ENTRY_SCOPE_EXCLUDE_SUBTREE, &num_vals);
    if (entryScopeExcludeSubtrees) {
        int i = 0;

        /* Validation has already been performed in preop, just build the DN's */
        theConfig.entryScopeExcludeSubtrees =
            (Slapi_DN **)slapi_ch_calloc(sizeof(Slapi_DN *), num_vals + 1);
        for (i = 0; i < num_vals; i++) {
            theConfig.entryScopeExcludeSubtrees[i] =
                slapi_sdn_new_dn_passin(entryScopeExcludeSubtrees[i]);
        }
        theConfig.entryExcludeScopeCount = num_vals; /* shortcut for config copy */
    }

    /* release the lock */
    memberof_unlock_config();

done:
    slapi_sdn_free(&config_sdn);
    slapi_entry_free(config_entry);
    slapi_ch_array_free(groupattrs);
    slapi_ch_free_string(&memberof_attr);
    slapi_ch_free((void **)&entryScopes);
    slapi_ch_free((void **)&entryScopeExcludeSubtrees);

    if (*returncode != LDAP_SUCCESS) {
        return SLAPI_DSE_CALLBACK_ERROR;
    } else {
        return SLAPI_DSE_CALLBACK_OK;
    }
}

/*
 * memberof_copy_config()
 *
 * Makes a copy of the config in src.  This function will free the
 * elements of dest if they already exist.  This should only be called
 * if you hold the memberof config lock if src was obtained with
 * memberof_get_config().
 */
void
memberof_copy_config(MemberOfConfig *dest, MemberOfConfig *src)
{
    if (dest && src) {

        /* Allocate our caches here since we only copy the config at the start of an op */
        if (memberof_use_txn() == 1){
            dest->ancestors_cache = hashtable_new(1);
            dest->fixup_cache = hashtable_new(1);
        }

        /* Check if the copy is already up to date */
        if (src->groupattrs) {
            int i = 0, j = 0;

            /* Copy group attributes string list. */
            slapi_ch_array_free(dest->groupattrs);
            dest->groupattrs = slapi_ch_array_dup(src->groupattrs);

            /* Copy group check filter. */
            slapi_filter_free(dest->group_filter, 1);
            dest->group_filter = slapi_filter_dup(src->group_filter);

            /* Copy group attributes Slapi_Attr list.
             * First free the old list. */
            for (i = 0; dest->group_slapiattrs && dest->group_slapiattrs[i]; i++) {
                slapi_attr_free(&dest->group_slapiattrs[i]);
            }

            /* Count how many values we have in the source list. */
            for (j = 0; src->group_slapiattrs && src->group_slapiattrs[j]; j++) {
                /* Do nothing. */
            }

            /* Realloc dest if necessary. */
            if (i < j) {
                dest->group_slapiattrs = (Slapi_Attr **)slapi_ch_realloc((char *)dest->group_slapiattrs, sizeof(Slapi_Attr *) * (j + 1));
            }

            /* Copy the attributes. */
            for (i = 0; dest->group_slapiattrs && src->group_slapiattrs && src->group_slapiattrs[i]; i++) {
                dest->group_slapiattrs[i] = slapi_attr_dup(src->group_slapiattrs[i]);
            }

            /* Terminate the array. */
            if (dest->group_slapiattrs) {
                dest->group_slapiattrs[i] = NULL;
            }
        }

        if (src->memberof_attr) {
            slapi_ch_free_string(&dest->memberof_attr);
            dest->memberof_attr = slapi_ch_strdup(src->memberof_attr);
        }

        if (src->skip_nested) {
            dest->skip_nested = src->skip_nested;
        }

        if (src->allBackends) {
            dest->allBackends = src->allBackends;
        }

        slapi_ch_free_string(&dest->auto_add_oc);
        dest->auto_add_oc = slapi_ch_strdup(src->auto_add_oc);

        dest->deferred_update = src->deferred_update;
        dest->need_fixup = src->need_fixup;
        /*
         * deferred_list, ancestors_cache, fixup_cache are not config parameters
         *  but simple global parameters and should not be copied as
         *  and they are only meaningful in the original config (i.e: theConfig)
         */

        if (src->entryScopes) {
            int num_vals = 0;

            dest->entryScopes = (Slapi_DN **)slapi_ch_calloc(sizeof(Slapi_DN *), src->entryScopeCount + 1);
            for (num_vals = 0; src->entryScopes[num_vals]; num_vals++) {
                dest->entryScopes[num_vals] = slapi_sdn_dup(src->entryScopes[num_vals]);
            }
        }
        if (src->entryScopeExcludeSubtrees) {
            int num_vals = 0;

            dest->entryScopeExcludeSubtrees = (Slapi_DN **)slapi_ch_calloc(sizeof(Slapi_DN *), src->entryExcludeScopeCount + 1);
            for (num_vals = 0; src->entryScopeExcludeSubtrees[num_vals]; num_vals++) {
                dest->entryScopeExcludeSubtrees[num_vals] = slapi_sdn_dup(src->entryScopeExcludeSubtrees[num_vals]);
            }
        }
    }
}

/*
 * memberof_free_config()
 *
 * Free's the contents of a config structure.
 */
void
memberof_free_config(MemberOfConfig *config)
{
    if (config) {
        int i = 0;

        slapi_ch_array_free(config->groupattrs);
        slapi_filter_free(config->group_filter, 1);

        for (i = 0; config->group_slapiattrs && config->group_slapiattrs[i]; i++) {
            slapi_attr_free(&config->group_slapiattrs[i]);
        }
        slapi_ch_free((void **)&config->group_slapiattrs);
        slapi_ch_free_string(&config->auto_add_oc);
        slapi_ch_free_string(&config->memberof_attr);
        memberof_free_scope(&(config->entryScopes), &config->entryScopeCount);
        memberof_free_scope(&(config->entryScopeExcludeSubtrees), &config->entryExcludeScopeCount);
        if (config->fixup_cache) {
            fixup_hashtable_empty(config, "memberof_free_config empty fixup_entry_hastable");
            PL_HashTableDestroy(config->fixup_cache);
        }
        if (config->ancestors_cache) {
            ancestor_hashtable_empty(config, "memberof_free_config empty group_ancestors_hashtable");
            PL_HashTableDestroy(config->ancestors_cache);
        }
    }
}

/*
 * memberof_get_config()
 *
 * Returns a pointer to the main config.  You should call
 * memberof_rlock_config() first so the main config doesn't
 * get modified out from under you.
 */
MemberOfConfig *
memberof_get_config()
{
    return &theConfig;
}

/*
 * memberof_rlock_config()
 *
 * Gets a non-exclusive lock on the main config.  This will
 * prevent the config from being changed out from under you
 * while you read it, but it will still allow other threads
 * to read the config at the same time.
 */
void
memberof_rlock_config()
{
    slapi_rwlock_rdlock(memberof_config_lock);
}

/*
 * memberof_wlock_config()
 *
 * Gets an exclusive lock on the main config.  This should
 * be called if you need to write to the main config.
 */
void
memberof_wlock_config()
{
    slapi_rwlock_wrlock(memberof_config_lock);
}

/*
 * memberof_unlock_config()
 *
 * Unlocks the main config.
 */
void
memberof_unlock_config()
{
    slapi_rwlock_unlock(memberof_config_lock);
}

int
memberof_config_get_all_backends()
{
    int all_backends;

    slapi_rwlock_rdlock(memberof_config_lock);
    all_backends = theConfig.allBackends;
    slapi_rwlock_unlock(memberof_config_lock);

    return all_backends;
}

/*
 * Check if we are modifying the config, or changing the shared config entry
 */
int
memberof_shared_config_validate(Slapi_PBlock *pb)
{
    Slapi_Entry *e = 0;
    Slapi_Entry *resulting_e = 0;
    Slapi_Entry *config_entry = NULL;
    Slapi_DN *sdn = NULL;
    Slapi_DN *config_sdn = NULL;
    Slapi_Mods *smods = 0;
    Slapi_Mod *smod = NULL, *nextmod = NULL;
    LDAPMod **mods = NULL;
    char returntext[SLAPI_DSE_RETURNTEXT_SIZE];
    char *configarea_dn = NULL;
    int ret = SLAPI_PLUGIN_SUCCESS;

    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);

    if (slapi_sdn_compare(sdn, memberof_get_plugin_area()) == 0 ||
        slapi_sdn_compare(sdn, memberof_get_config_area()) == 0) {
        slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &e);
        if (e) {
            /*
             * Create a copy of the entry and apply the
             * mods to create the resulting entry.
             */
            slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
            smods = slapi_mods_new();
            slapi_mods_init_byref(smods, mods);
            resulting_e = slapi_entry_dup(e);
            if (mods && (slapi_entry_apply_mods(resulting_e, mods) != LDAP_SUCCESS)) {
                /* we don't care about this, the update is invalid and will be caught later */
                goto bail;
            }

            if (slapi_sdn_compare(sdn, memberof_get_plugin_area())) {
                /*
                 * This entry is a plugin config area entry, validate it.
                 */
                if (SLAPI_DSE_CALLBACK_ERROR == memberof_validate_config(pb, NULL, resulting_e, &ret, returntext, 0)) {
                    ret = LDAP_UNWILLING_TO_PERFORM;
                }
            } else {
                /*
                 * This is the memberOf plugin entry, check if we are adding/replacing the
                 * plugin config area.
                 */
                nextmod = slapi_mod_new();
                for (smod = slapi_mods_get_first_smod(smods, nextmod);
                     smod != NULL;
                     smod = slapi_mods_get_next_smod(smods, nextmod)) {
                    if (PL_strcasecmp(SLAPI_PLUGIN_SHARED_CONFIG_AREA, slapi_mod_get_type(smod)) == 0) {
                        /*
                         * Okay, we are modifying the plugin config area, we only care about
                         * adds and replaces.
                         */
                        if (SLAPI_IS_MOD_REPLACE(slapi_mod_get_operation(smod)) ||
                            SLAPI_IS_MOD_ADD(slapi_mod_get_operation(smod))) {
                            struct berval *bv = NULL;
                            int rc = 0;

                            bv = slapi_mod_get_first_value(smod);
                            configarea_dn = slapi_ch_strdup(bv->bv_val);
                            if (configarea_dn) {
                                /* Check the DN syntax */
                                rc = slapi_dn_syntax_check(pb, configarea_dn, 1);
                                if (rc) { /* syntax check failed */
                                    PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                                "%s does not contain a valid DN (%s)",
                                                SLAPI_PLUGIN_SHARED_CONFIG_AREA, configarea_dn);
                                    ret = LDAP_UNWILLING_TO_PERFORM;
                                    goto bail;
                                }

                                /* Check if the plugin config area entry exists */
                                if ((config_sdn = slapi_sdn_new_dn_byval(configarea_dn))) {
                                    rc = slapi_search_internal_get_entry(config_sdn, NULL, &config_entry,
                                                                         memberof_get_plugin_id());
                                    if (config_entry) {
                                        int err = 0;
                                        /*
                                         * Validate the settings from the new config area.
                                         */
                                        if (memberof_validate_config(pb, NULL, config_entry, &err, returntext, 0) == SLAPI_DSE_CALLBACK_ERROR) {
                                            ret = LDAP_UNWILLING_TO_PERFORM;
                                            goto bail;
                                        }
                                    } else {
                                        /* The config area does not exist */
                                        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                                    "Unable to locate shared config entry (%s) error %d",
                                                    slapi_sdn_get_dn(memberof_get_config_area()), rc);
                                        ret = LDAP_UNWILLING_TO_PERFORM;
                                        goto bail;
                                    }
                                }
                            }
                            slapi_ch_free_string(&configarea_dn);
                            slapi_sdn_free(&config_sdn);
                        }
                    }
                }
            }
        } else {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Unable to locate shared config entry (%s)",
                        slapi_sdn_get_dn(memberof_get_config_area()));
            ret = LDAP_UNWILLING_TO_PERFORM;
        }
    }

bail:

    if (ret) {
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ret);
        slapi_pblock_set(pb, SLAPI_PB_RESULT_TEXT, returntext);
        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM, "memberof_shared_config_validate - %s/n",
                      returntext);
    }
    slapi_sdn_free(&config_sdn);
    if (nextmod)
        slapi_mod_free(&nextmod);
    slapi_mods_free(&smods);
    slapi_entry_free(resulting_e);
    slapi_entry_free(config_entry);
    slapi_ch_free_string(&configarea_dn);

    return ret;
}


static PRIntn memberof_hash_compare_keys(const void *v1, const void *v2)
{
    PRIntn rc;
    if (0 == strcasecmp((const char *) v1, (const char *) v2)) {
        rc = 1;
    } else {
        rc = 0;
    }
    return rc;
}

static PRIntn memberof_hash_compare_values(const void *v1, const void *v2)
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
static PLHashNumber memberof_hash_fn(const void *key)
{
    PLHashNumber hash = 5381;
    unsigned char *x = (unsigned char *)key;
    int c;

    while ((c = *x++)){
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
PLHashTable *hashtable_new(int usetxn)
{
    if (!usetxn) {
        return NULL;
    }

    return PL_NewHashTable(MEMBEROF_HASHTABLE_SIZE,
        memberof_hash_fn,
        memberof_hash_compare_keys,
        memberof_hash_compare_values, NULL, NULL);
}

/* this function called for each hash node during hash destruction */
static PRIntn fixup_hashtable_remove(PLHashEntry *he, PRIntn index __attribute__((unused)), void *arg __attribute__((unused)))
{
	char *dn_copy;

	if (he == NULL) {
		return HT_ENUMERATE_NEXT;
	}
	dn_copy = (char*) he->value;
	slapi_ch_free_string(&dn_copy);

	return HT_ENUMERATE_REMOVE;
}

static void fixup_hashtable_empty(MemberOfConfig *config, char *msg)
{
    if (config->fixup_cache) {
        PL_HashTableEnumerateEntries(config->fixup_cache, fixup_hashtable_remove, msg);
    }
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

void ancestor_hashtable_entry_free(memberof_cached_value *entry)
{
    int i;

    for (i = 0; entry[i].valid; i++) {
        slapi_ch_free((void **) &entry[i].group_dn_val);
        slapi_ch_free((void **) &entry[i].group_ndn_val);
    }
    /* Here we are at the ending element containing the key */
    slapi_ch_free((void**) &entry[i].key);
}

/* this function called for each hash node during hash destruction */
static PRIntn ancestor_hashtable_remove(PLHashEntry *he, PRIntn index __attribute__((unused)), void *arg __attribute__((unused)))
{
    memberof_cached_value *group_ancestor_array;

    if (he == NULL) {
        return HT_ENUMERATE_NEXT;
    }
    group_ancestor_array = (memberof_cached_value *) he->value;
    ancestor_hashtable_entry_free(group_ancestor_array);
    slapi_ch_free((void **)&group_ancestor_array);

    return HT_ENUMERATE_REMOVE;
}

static void ancestor_hashtable_empty(MemberOfConfig *config, char *msg)
{
    if (config->ancestors_cache) {
        PL_HashTableEnumerateEntries(config->ancestors_cache, ancestor_hashtable_remove, msg);
    }
}

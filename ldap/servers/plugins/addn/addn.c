/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2016 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * AD DN bind plugin
 *
 * This plugin allows violation of the LDAP v3 standard for binds to occur
 * as "uid" or "uid@example.com". This plugin re-maps those terms to a real
 * dn such as uid=user,ou=People,dc=example,dc=com.
 */

#include "addn.h"

/* is there a better type we can use here? */
#define ADDN_FAILURE 1

static void *plugin_identity = NULL;
static char *plugin_name = "addn_plugin";

static Slapi_PluginDesc addn_description = {
    "addn",
    VENDOR,
    DS_PACKAGE_VERSION,
    "Allow AD DN style bind names to LDAP"};

int
addn_filter_validate(char *config_filter)
{
    if (config_filter == NULL) {
        return 1;
    }

    char *lptr = NULL;
    char *rptr = NULL;

    lptr = PL_strstr(config_filter, "%s");
    rptr = PL_strrstr(config_filter, "%s");

    /* There is one instance, and one only! */
    if (lptr == rptr) {
        return LDAP_SUCCESS;
    }

    /* Is there a way to now compile and check the filter syntax? */

    return 1;
}


/* These will probably move to slapi_ hopefully, they make plugin writing easier ... */
/* Get all the configs under cn=<name>,cn=plugins,cn=config */

/* Slapi_Entry **
addn_get_all_subconfigs(Slapi_PBlock *pb) */

/* Filter configs? */
/* Slapi_Entry **
addn_filter_subconfigs(Slapi_PBlock *pb, Slapi_Entry * (*func)(Slapi_PBlock *, Slapi_Entry *) ) */

/* Map on the configs? */
/* Slapi_Entry **
add_filter_subconfigs(Slapi_PBlock *pb, Slapi_Entry * (*func)(Slapi_PBlock *, Slapi_Entry *)) */

/* Get a specific config under cn=<name>,cn=plugins,cn=config */
Slapi_Entry *
addn_get_subconfig(Slapi_PBlock *pb, char *identifier)
{
    char *config_dn = NULL;
    char *filter = NULL;
    int search_result = 0;
    int entry_count = 0;
    Slapi_DN *config_sdn = NULL;
    Slapi_PBlock *search_pblock = NULL;
    Slapi_Entry *result = NULL;
    Slapi_Entry **entries = NULL;

    /* Get our config DN base. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_DN, &config_dn);
    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_get_subconfig: Getting config for %s\n", config_dn);
    config_sdn = slapi_sdn_new_dn_byval(config_dn);

    filter = slapi_ch_smprintf("(cn=%s)", identifier);
    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_get_subconfig: Searching with filter %s\n", filter);

    /* Search the backend, and apply our filter given the username */
    search_pblock = slapi_pblock_new();

    if (search_pblock == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "addn_get_subconfig: Unable to allocate search_pblock!!!\n");
        goto out;
    }

    slapi_search_internal_set_pb_ext(search_pblock, config_sdn, LDAP_SCOPE_ONELEVEL,
                                     filter, NULL, 0 /* attrs only */,
                                     NULL /* controls */, NULL /* uniqueid */,
                                     plugin_identity, 0 /* actions */);
    slapi_search_internal_pb(search_pblock);

    search_result = slapi_pblock_get(search_pblock, SLAPI_PLUGIN_INTOP_RESULT, &search_result);
    if (search_result != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "addn_get_subconfig: Internal search pblock get failed!!!\n");
        goto out;
    }

    if (search_result == LDAP_NO_SUCH_OBJECT) {
        slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_get_subconfig: LDAP_NO_SUCH_OBJECT \n");
        goto out;
    }

    /* On all other errors, just fail out. */
    if (search_result != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "addn_get_subconfig: Internal search error occurred %d \n", search_result);
        goto out;
    }


    search_result = slapi_pblock_get(search_pblock, SLAPI_NENTRIES, &entry_count);

    if (search_result != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "addn_get_subconfig: Unable to retrieve number of entries from pblock!\n");
        goto out;
    }
    /* If there are multiple results, we should also fail as we cannot */
    /* be sure of the real configuration we are about to use */

    if (entry_count != 1) {
        /*  Is there a better log level for this? */
        slapi_log_err(SLAPI_LOG_WARNING, plugin_name, "addn_get_subconfig: multiple or no results returned. Failing to auth ...\n");
        goto out;
    }

    search_result = slapi_pblock_get(search_pblock, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if (search_result != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "addn_get_subconfig: Unable to retrieve entries from pblock!\n");
        goto out;
    }

    result = slapi_entry_dup(entries[0]);

out:
    if (search_pblock != NULL) {
        slapi_free_search_results_internal(search_pblock);
        slapi_pblock_destroy(search_pblock);
    }
    slapi_sdn_free(&config_sdn);
    slapi_ch_free_string(&filter);
    return result;
}

int
addn_prebind(Slapi_PBlock *pb)
{
    struct addn_config *config = NULL;
    Slapi_Entry *domain_config = NULL;
    Slapi_DN *pb_sdn_target = NULL;
    Slapi_DN *pb_sdn_mapped = NULL;
    char *dn = NULL;

    char *dn_bind = NULL;
    int dn_bind_len = 0;
    char *dn_bind_escaped = NULL;

    char *dn_domain = NULL;
    int dn_domain_len = 0;
    char *dn_domain_escaped = NULL;

    char *be_suffix = NULL;
    Slapi_DN *be_suffix_dn = NULL;
    char *config_filter = NULL;
    char *filter = NULL;
    int result = 0;
    static char *attrs[] = {"dn", NULL};

    Slapi_PBlock *search_pblock = NULL;
    int search_result = 0;
    Slapi_Entry **entries = NULL;
    int entry_count = 0;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_prebind: --> begin\n");
    slapi_pblock_get(pb, SLAPI_BIND_TARGET_SDN, &pb_sdn_target);
    /* We need to discard the const because later functions require char * only */
    dn = (char *)slapi_sdn_get_dn(pb_sdn_target);
    if (dn == NULL) {
        result = ADDN_FAILURE;
        goto out;
    }
    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_prebind: Recieved %s\n", dn);

    result = slapi_dn_syntax_check(NULL, dn, 0);
    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_prebind: Dn validation %d\n", result);

    if (result == LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_prebind: Dn syntax is correct, do not alter\n");
        /* This is a syntax correct bind dn. Leave it alone! */
        goto out;
    }
    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_prebind: Dn syntax is incorrect, it may need ADDN mangaling\n");


    result = slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &config);
    if (result != LDAP_SUCCESS || config == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "addn_prebind: Unable to retrieve plugin configuration!\n");
        result = ADDN_FAILURE;
        goto out;
    }

    /* Right, the dn is invalid! This means it *could* be addn syntax */
    /* Do we have any other validation to run here? */
    /* Split the @domain component (if any) */

    dn_bind = strtok(dn, "@");

    /* Filter escape the name, and the domain parts */
    if (dn_bind != NULL) {
        dn_bind_len = strlen(dn_bind);
        dn_bind_escaped = slapi_escape_filter_value(dn_bind, dn_bind_len);
    }

    dn_domain = strtok(NULL, "@");

    /* You have a domain! Lets use it. */
    if (dn_domain != NULL) {
        dn_domain_len = strlen(dn_domain);
        dn_domain_escaped = slapi_escape_filter_value(dn_domain, dn_domain_len);
        slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_prebind: Selected bind submitted domain %s\n", dn_domain_escaped);
    } else {
        /* Or we could do the escape earlier. */
        dn_domain_escaped = slapi_ch_strdup(config->default_domain);
        slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_prebind: Selected default domain %s\n", dn_domain_escaped);
    }

    /* If we don't have the @domain. get a default from config */
    if (dn_domain_escaped == NULL) {
        /* This could alternately be domain, then we do the same domain -> suffix conversion .... */
        /* Select the correct backend. If no backend, bail. */
    }

    /* Now get the config that matches. */

    domain_config = addn_get_subconfig(pb, dn_domain_escaped);

    if (domain_config == NULL) {
        slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_prebind: no matching domain configuration for %s\n", dn_domain_escaped);
        result = ADDN_FAILURE;
        goto out;
    }

    /* Now we can get our suffix. */
    be_suffix = (char *)slapi_entry_attr_get_ref(domain_config, "addn_base");
    be_suffix_dn = slapi_sdn_new_dn_byval(be_suffix);

    /* Get our filter. From the config */
    config_filter = slapi_entry_attr_get_charptr(domain_config, "addn_filter");

    /* Validate the filter_template only has one %s */
    if (addn_filter_validate(config_filter) != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "addn_prebind: Failed to validate addn_filter %s for domain %s\n", config_filter, dn_domain_escaped);
        result = ADDN_FAILURE;
        goto out;
    }

    /* Put the search term into the filter. */
    /* Current design relies on there only being a single search term! */
    filter = slapi_ch_smprintf(config_filter, dn_bind_escaped);
    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_prebind: Searching with filter %s\n", filter);

    /* Search the backend, and apply our filter given the username */
    search_pblock = slapi_pblock_new();

    if (search_pblock == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "addn_prebind: Unable to allocate search_pblock!!!\n");
        result = ADDN_FAILURE;
        goto out;
    }

    slapi_search_internal_set_pb_ext(search_pblock, be_suffix_dn, LDAP_SCOPE_SUBTREE,
                                     filter, attrs, 0 /* attrs only */,
                                     NULL /* controls */, NULL /* uniqueid */,
                                     plugin_identity, 0 /* actions */);
    slapi_search_internal_pb(search_pblock);

    result = slapi_pblock_get(search_pblock, SLAPI_PLUGIN_INTOP_RESULT, &search_result);
    if (result != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "addn_prebind: Internal search pblock get failed!!!\n");
        result = ADDN_FAILURE;
        goto out;
    }

    /* No results are an error: Allow the plugin to continue, because */
    /* the bind will fail anyway ....? */

    /* Do we want to return a fail here? Or pass, and allow the auth code
     * to fail us. That will prevent some attacks I feel as this could be a
     * disclosure attack ....
     */
    if (search_result == LDAP_NO_SUCH_OBJECT) {
        slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_prebind: LDAP_NO_SUCH_OBJECT \n");
        result = LDAP_SUCCESS;
        goto out;
    }

    /* On all other errors, just fail out. */
    if (search_result != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "addn_prebind: Internal search error occurred %d \n", search_result);
        result = ADDN_FAILURE;
        goto out;
    }


    result = slapi_pblock_get(search_pblock, SLAPI_NENTRIES, &entry_count);

    if (result != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "addn_prebind: Unable to retrieve number of entries from pblock!\n");
        result = ADDN_FAILURE;
        goto out;
    }
    /* If there are multiple results, we should also fail as we cannot */
    /* be sure of the real object we want to bind to */

    if (entry_count > 1) {
        /*  Is there a better log level for this? */
        slapi_log_err(SLAPI_LOG_WARNING, plugin_name, "addn_prebind: multiple results returned. Failing to auth ...\n");
        result = ADDN_FAILURE;
        goto out;
    }

    result = slapi_pblock_get(search_pblock, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if (result != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "addn_prebind: Unable to retrieve entries from pblock!\n");
        result = ADDN_FAILURE;
        goto out;
    }

    /* We should only have one entry here! */

    pb_sdn_mapped = slapi_sdn_dup(slapi_entry_get_sdn(*entries));

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_prebind: SEARCH entry dn=%s is mapped from addn=%s\n", slapi_sdn_get_dn(pb_sdn_mapped), dn);


    /* If there is a result, take the DN and make the SDN for PB */

    /* Free the original SDN */
    result = slapi_pblock_set(pb, SLAPI_TARGET_SDN, pb_sdn_mapped);
    if (result != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "addn_prebind: Unable to set new mapped DN to pblock!\n");
        /* We have to free the mapped SDN here */
        slapi_sdn_free(&pb_sdn_mapped);
        result = ADDN_FAILURE;
        goto out;
    }

    slapi_sdn_free(&pb_sdn_target);

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_prebind: <-- complete\n");

    result = LDAP_SUCCESS;

out:
    if (search_pblock != NULL) {
        slapi_free_search_results_internal(search_pblock);
        slapi_pblock_destroy(search_pblock);
    }
    slapi_entry_free(domain_config);
    slapi_sdn_free(&be_suffix_dn);
    slapi_ch_free_string(&dn_bind_escaped);
    slapi_ch_free_string(&dn_domain_escaped);
    slapi_ch_free_string(&filter);

    return result;
}


/*
 * addn_start
 *
 * This is called when the plugin is started. It allows a configuration change
 * To be made while the directory server is live.
 */
int
addn_start(Slapi_PBlock *pb)
{
    Slapi_Entry *plugin_entry = NULL;
    struct addn_config *config = NULL;
    char *domain = NULL;
    int result = LDAP_SUCCESS;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_start: starting ...\n");

    /* It looks like we mis-use the SLAPI_ADD_ENTRY space in PB during plugin startup  */
    result = slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &plugin_entry);
    if (result != LDAP_SUCCESS || plugin_entry == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "addn_start: Failed to retrieve config entry!\n");
        return SLAPI_PLUGIN_FAILURE;
    }

    /* Now set the values into the config */
    /* Probably need to get the config DN we are using too. */
    /* Are there some error cases here we should be checking? */
    config = (struct addn_config *)slapi_ch_malloc(sizeof(struct addn_config));
    domain = slapi_entry_attr_get_charptr(plugin_entry, "addn_default_domain");

    if (domain == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name,
                      "addn_start: No default domain in configuration, you must set addn_default_domain!\n");
        slapi_ch_free((void **)&config);
        return SLAPI_PLUGIN_FAILURE;
    }

    config->default_domain = slapi_escape_filter_value(domain, strlen(domain));
    config->default_domain_len = strlen(config->default_domain);

    /* Set into the pblock */
    slapi_pblock_set(pb, SLAPI_PLUGIN_PRIVATE, (void *)config);

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_start: startup complete\n");

    return LDAP_SUCCESS;
}

/*
 * addn_close
 *
 * Called when the plugin is stopped. Frees the configs as needed
 */
int
addn_close(Slapi_PBlock *pb)
{
    struct addn_config *config = NULL;

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_close: stopping ...\n");

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &config);
    if (config != NULL) {
        slapi_ch_free_string(&config->default_domain);
        slapi_ch_free((void **)&config);
        slapi_pblock_set(pb, SLAPI_PLUGIN_PRIVATE, NULL);
    }

    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_close: stop complete\n");

    return LDAP_SUCCESS;
}


/*
 * addn_init
 *
 * Initialise and register the addn plugin to the directory server.
 */
int
addn_init(Slapi_PBlock *pb)
{
    int result = 0;

    result = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_03);
    if (result != LDAP_SUCCESS) {
        goto out;
    }

    /* Get and stash our plugin identity */
    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_identity);

    result = slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&addn_description);
    if (result != LDAP_SUCCESS) {
        goto out;
    }

    result = slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN, (void *)addn_start);
    if (result != LDAP_SUCCESS) {
        goto out;
    }

    result = slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN, (void *)addn_close);
    if (result != LDAP_SUCCESS) {
        goto out;
    }

    result = slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_BIND_FN, (void *)addn_prebind);
    if (result != LDAP_SUCCESS) {
        goto out;
    }

out:
    if (result == LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_init: Success: plugin loaded.\n");
        slapi_log_err(SLAPI_LOG_WARNING, plugin_name, "addn_init: The use of this plugin violates the LDAPv3 specification RFC4511 section 4.2 BindDN specification. You have been warned ...\n");
    } else {
        slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "addn_init: Error: %d. \n", result);
    }
    return result;
}

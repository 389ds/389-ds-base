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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * ptconfig.c - configuration-related code for Pass Through Authentication
 *
 */

#include <plstr.h>

#include "pam_passthru.h"

#define PAM_PT_CONFIG_FILTER "(objectclass=*)"

/*
 * The configuration attributes are contained in the plugin entry e.g.
 * cn=PAM Pass Through,cn=plugins,cn=config, or an alternate config area.
 *
 * Configuration is a two step process.  The first pass is a validation step which
 * occurs pre-op - check inputs and error out if bad.  The second pass actually
 * applies the changes to the run time config.
 */

static Slapi_DN *_ConfigArea = NULL;

/*
 * function prototypes
 */ 
static int pam_passthru_apply_config (Slapi_Entry* e);

/*
 * Read and load configuration.  Validation will also
 * be performed unless skip_validate is set to non-0.
 * Returns PAM_PASSTHRU_SUCCESS if all is well.
 */
int
pam_passthru_load_config(int skip_validate)
{
    int status = PAM_PASSTHRU_SUCCESS;
    int result;
    int i;
    int alternate = 0;
    Slapi_PBlock *search_pb;
    Slapi_Entry **entries = NULL;

    slapi_log_error( SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                     "=> pam_passthru_load_config\n");

    pam_passthru_write_lock();
    pam_passthru_delete_config();

    search_pb = slapi_pblock_new();

    /* Find all entries in the active config area. */
    slapi_search_internal_set_pb(search_pb, slapi_sdn_get_ndn(pam_passthru_get_config_area()),
                                 LDAP_SCOPE_SUBTREE, "objectclass=*",
                                 NULL, 0, NULL, NULL,
                                 pam_passthruauth_get_plugin_identity(), 0);
    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);

    if (LDAP_SUCCESS != result) {
        status = PAM_PASSTHRU_FAILURE;
        goto cleanup;
    }

    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                     &entries);
    if (NULL == entries || NULL == entries[0]) {
        status = PAM_PASSTHRU_FAILURE;
        goto cleanup;
    }

    /* Check if we are using an alternate config area.  We do this here
     * so we don't have to check each every time in the loop below. */
    if (slapi_sdn_compare(pam_passthru_get_config_area(),
            pam_passthruauth_get_plugin_sdn()) != 0) {
        alternate = 1;
    }

    /* Validate and apply config if valid.  If skip_validate is set, we skip
     * validation and just apply the config.  This should only be done if the
     * configuration has already been validated. */
    for (i = 0; (entries[i] != NULL); i++) {
        /* If this is the alternate config container, skip it since
         * we don't consider it to be an actual config entry. */
        if (alternate && (slapi_sdn_compare(pam_passthru_get_config_area(),
                slapi_entry_get_sdn(entries[i])) == 0)) {
            continue;
        }

        if (skip_validate || (PAM_PASSTHRU_SUCCESS == pam_passthru_validate_config(entries[i], NULL))) {
            if (PAM_PASSTHRU_FAILURE == pam_passthru_apply_config(entries[i])) {
                slapi_log_error( SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                                 "pam_passthru_load_config: unable to apply config "
                                 "for entry \"%s\"\n", slapi_entry_get_ndn(entries[i]));
            }
        } else {
            slapi_log_error( SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                             "pam_passthru_load_config: skipping invalid config "
                             "entry \"%s\"\n", slapi_entry_get_ndn(entries[i]));
        }
    }

  cleanup:
    slapi_free_search_results_internal(search_pb);
    slapi_pblock_destroy(search_pb);
    pam_passthru_unlock();
    slapi_log_error(SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                    "<= pam_passthru_load_config\n");

    return status;
}

static void
Delete_Pam_PassthruSuffix(Pam_PassthruSuffix *one)
{
    if (one) {
        slapi_sdn_free(&one->pamptsuffix_dn);
        slapi_ch_free((void **)&one);
    }
}

static void
pam_ptconfig_free_suffixes(Pam_PassthruSuffix *list)
{
    while (list) {
        Pam_PassthruSuffix *next = list->pamptsuffix_next;
        Delete_Pam_PassthruSuffix(list);
        list = next;
    }
}

/*
 * Free a config struct.
 */
static void
pam_passthru_free_config_entry(Pam_PassthruConfig **entry)
{
    Pam_PassthruConfig *e = *entry;

    if (e == NULL) {
        return;
    }

    slapi_ch_free_string(&e->dn);
    pam_ptconfig_free_suffixes(e->pamptconfig_includes);
    pam_ptconfig_free_suffixes(e->pamptconfig_excludes);
    slapi_ch_free_string(&e->pamptconfig_pam_ident_attr);
    slapi_ch_free_string(&e->pamptconfig_service);
    slapi_ch_free_string(&e->filter_str);
    slapi_filter_free(e->slapi_filter, 1);

    slapi_ch_free((void **) entry);
}

/*
 * Free and remove a single config item from the list.
 */
static void
pam_passthru_delete_configEntry(PRCList *entry)
{
    PR_REMOVE_LINK(entry);
    pam_passthru_free_config_entry((Pam_PassthruConfig **) &entry);
}

/*
 * Delete the entire config list contents.
 */
void
pam_passthru_delete_config()
{
    PRCList *list;

    while (!PR_CLIST_IS_EMPTY(pam_passthru_global_config)) {
        list = PR_LIST_HEAD(pam_passthru_global_config);
        pam_passthru_delete_configEntry(list);
    }

    return;
}

static int
missing_suffix_to_int(char *missing_suffix)
{
	int retval = -1; /* -1 is error */
	if (!PL_strcasecmp(missing_suffix, PAMPT_MISSING_SUFFIX_ERROR_STRING)) {
		retval = PAMPT_MISSING_SUFFIX_ERROR;
	} else if (!PL_strcasecmp(missing_suffix, PAMPT_MISSING_SUFFIX_ALLOW_STRING)) {
		retval = PAMPT_MISSING_SUFFIX_ALLOW;
	} else if (!PL_strcasecmp(missing_suffix, PAMPT_MISSING_SUFFIX_IGNORE_STRING)) {
		retval = PAMPT_MISSING_SUFFIX_IGNORE;
	}

	return retval;
}

static PRBool
check_missing_suffix_flag(int val) {
	if (val == PAMPT_MISSING_SUFFIX_ERROR ||
		val == PAMPT_MISSING_SUFFIX_ALLOW ||
		val == PAMPT_MISSING_SUFFIX_IGNORE) {
		return PR_TRUE;
	}

	return PR_FALSE;
}

static char *get_missing_suffix_values()
{
	return PAMPT_MISSING_SUFFIX_ERROR_STRING ", " PAMPT_MISSING_SUFFIX_ALLOW_STRING ", "
		PAMPT_MISSING_SUFFIX_IGNORE_STRING;
}

static char *get_map_method_values()
{
	return PAMPT_MAP_METHOD_DN_STRING " or " PAMPT_MAP_METHOD_RDN_STRING " or " PAMPT_MAP_METHOD_ENTRY_STRING;
}

static int
meth_to_int(char **map_method, int *err)
{
	char *end;
	int len;
	int ret = PAMPT_MAP_METHOD_NONE;

	*err = 0;
	if (!map_method || !*map_method) {
		return ret;
	}

	end = strchr(*map_method, ' ');
	if (!end) {
		len = strlen(*map_method);
	} else {
		len = end - *map_method;
	}
	if (!PL_strncasecmp(*map_method, PAMPT_MAP_METHOD_DN_STRING, len)) {
		ret = PAMPT_MAP_METHOD_DN;
	} else if (!PL_strncasecmp(*map_method, PAMPT_MAP_METHOD_RDN_STRING, len)) {
		ret = PAMPT_MAP_METHOD_RDN;
	} else if (!PL_strncasecmp(*map_method, PAMPT_MAP_METHOD_ENTRY_STRING, len)) {
		ret = PAMPT_MAP_METHOD_ENTRY;
	} else {
		*err = 1;
	}

	if (!*err) {
		if (end && *end) {
			*map_method = end + 1;
		} else {
			*map_method = NULL;
		}
	}

	return ret;
}

static int
parse_map_method(char *map_method, int *one, int *two, int *three, char *returntext)
{
	int err = PAM_PASSTHRU_SUCCESS;
	char **ptr = &map_method;

	*one = *two = *three = PAMPT_MAP_METHOD_NONE;
	*one = meth_to_int(ptr, &err);
	if (err) {
		if (returntext) {
			PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
						"The map method in the string [%s] is invalid: must be "
						"one of %s", map_method, get_map_method_values());
		} else {
			slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
						"The map method in the string [%s] is invalid: must be "
						"one of %s\n", map_method, get_map_method_values());
		}
		err = PAM_PASSTHRU_FAILURE;
		goto bail;
	}
	*two = meth_to_int(ptr, &err);
	if (err) {
		if (returntext) {
			PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
						"The map method in the string [%s] is invalid: must be "
						"one of %s", map_method, get_map_method_values());
		} else {
			slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
						"The map method in the string [%s] is invalid: must be "
						"one of %s\n", map_method, get_map_method_values());
		}
		err = PAM_PASSTHRU_FAILURE;
                goto bail;
	}
	*three = meth_to_int(ptr, &err);
	if (err) {
		if (returntext) {
			PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
						"The map method in the string [%s] is invalid: must be "
						"one of %s", map_method, get_map_method_values());
		} else {
			slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
						"The map method in the string [%s] is invalid: must be "
						"one of %s\n", map_method, get_map_method_values());
		}
		err = PAM_PASSTHRU_FAILURE;
                goto bail;
	}
	if ((meth_to_int(ptr, &err) != PAMPT_MAP_METHOD_NONE) || err) {
		if (returntext) {
			PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
						"Invalid extra text [%s] after last map method",
						((ptr && *ptr) ? *ptr : "(null)"));
		} else {
			slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
						"Invalid extra text [%s] after last map method\n",
						((ptr && *ptr) ? *ptr : "(null)"));
		}
		err = PAM_PASSTHRU_FAILURE;
                goto bail;
	}

  bail:
	return err;
}

static void
print_suffixes()
{
	void *cookie = NULL;
	Slapi_DN *sdn = NULL;
	slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
					"The following is the list of valid suffixes to use with "
					PAMPT_EXCLUDES_ATTR " and " PAMPT_INCLUDES_ATTR ":\n");
	for (sdn = slapi_get_first_suffix(&cookie, 1);
		 sdn && cookie;
		 sdn = slapi_get_next_suffix(&cookie, 1)) {
		slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
						"\t%s\n", slapi_sdn_get_dn(sdn));
	}
}

/*
 * Validate the pending changes in the e entry.
 * If returntext is NULL, we log messages about invalid config
 * to the errors log.
 */
int 
pam_passthru_validate_config (Slapi_Entry* e, char *returntext)
{
	int rc = PAM_PASSTHRU_FAILURE;
	char *missing_suffix_str = NULL;
	int missing_suffix;
	int ii;
	char **excludes = NULL;
	char **includes = NULL;
	char *pam_ident_attr = NULL;
	char *map_method = NULL;
	char *pam_filter_str = NULL;
	Slapi_Filter *pam_filter = NULL;

	/* first, get the missing_suffix flag and validate it */
	missing_suffix_str = slapi_entry_attr_get_charptr(e, PAMPT_MISSING_SUFFIX_ATTR);
	if ((missing_suffix = missing_suffix_to_int(missing_suffix_str)) < 0 ||
		!check_missing_suffix_flag(missing_suffix)) {
		if (returntext) {
			PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
					"Error: valid values for %s are %s",
					PAMPT_MISSING_SUFFIX_ATTR, get_missing_suffix_values());
		} else {
			slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
					"Error: valid values for %s are %s\n",
					PAMPT_MISSING_SUFFIX_ATTR, get_missing_suffix_values());
		}
		goto done;
	}

	if (missing_suffix != PAMPT_MISSING_SUFFIX_IGNORE) {
		char **missing_list = NULL;

		/* get the list of excluded suffixes */
		excludes = slapi_entry_attr_get_charray(e, PAMPT_EXCLUDES_ATTR);
		for (ii = 0; excludes && excludes[ii]; ++ii) {
			/* The excludes DNs are already normalized. */
			Slapi_DN *comp_dn = slapi_sdn_new_normdn_byref(excludes[ii]);
			if (!slapi_be_exist(comp_dn)) {
				charray_add(&missing_list, slapi_ch_strdup(excludes[ii]));
			}
			slapi_sdn_free(&comp_dn);
		}

		/* get the list of included suffixes */
		includes = slapi_entry_attr_get_charray(e, PAMPT_INCLUDES_ATTR);
		for (ii = 0; includes && includes[ii]; ++ii) {
			/* The includes DNs are already normalized. */
			Slapi_DN *comp_dn = slapi_sdn_new_normdn_byref(includes[ii]);
			if (!slapi_be_exist(comp_dn)) {
				charray_add(&missing_list, slapi_ch_strdup(includes[ii]));
			}
			slapi_sdn_free(&comp_dn);
		}

		if (missing_list) {
			if (returntext) {
				PRUint32 size =
					PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
								"The following suffixes listed in %s or %s are not present in this "
								"server: ", PAMPT_EXCLUDES_ATTR, PAMPT_INCLUDES_ATTR);
				for (ii = 0; missing_list[ii]; ++ii) {
					if (size < SLAPI_DSE_RETURNTEXT_SIZE) {
						size += PR_snprintf(returntext+size, SLAPI_DSE_RETURNTEXT_SIZE-size,
											"%s%s", (ii > 0) ? "; " : "",
											missing_list[ii]);
					}
				}
			} else {
				slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
							"The suffixes listed in %s or %s are not present in "
							"this server\n", PAMPT_EXCLUDES_ATTR, PAMPT_INCLUDES_ATTR);
			}

			slapi_ch_array_free(missing_list);
			missing_list = NULL;
			print_suffixes();
			if (missing_suffix != PAMPT_MISSING_SUFFIX_ERROR) {
				if (returntext) {
					slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
									"Warning: %s\n", returntext);
					*returntext = 0; /* log error, don't report back to user */
				}
			} else {
				goto done;
			}
		}
	}

	pam_ident_attr = slapi_entry_attr_get_charptr(e, PAMPT_PAM_IDENT_ATTR);
	map_method = slapi_entry_attr_get_charptr(e, PAMPT_MAP_METHOD_ATTR);
	if (map_method) {
		int one, two, three;
		if (PAM_PASSTHRU_SUCCESS !=
			(rc = parse_map_method(map_method, &one, &two, &three, returntext))) {
			goto done; /* returntext set already (or error logged) */
		}
		if (!pam_ident_attr &&
			((one == PAMPT_MAP_METHOD_ENTRY) || (two == PAMPT_MAP_METHOD_ENTRY) ||
			 (three == PAMPT_MAP_METHOD_ENTRY))) {
			if (returntext) {
				PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Error: the %s method"
							" was specified, but no %s was given",
							PAMPT_MAP_METHOD_ENTRY_STRING, PAMPT_PAM_IDENT_ATTR);
			} else {
				slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
							"Error: the %s method was specified, but no %s was given\n",
							PAMPT_MAP_METHOD_ENTRY_STRING, PAMPT_PAM_IDENT_ATTR);
			}
			rc = PAM_PASSTHRU_FAILURE;
			goto done;
		}
		if ((one == PAMPT_MAP_METHOD_NONE) && (two == PAMPT_MAP_METHOD_NONE) &&
			(three == PAMPT_MAP_METHOD_NONE)) {
			if (returntext) {
				PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Error: no method(s)"
							" specified for %s, should be one or more of %s",
							PAMPT_MAP_METHOD_ATTR, get_map_method_values());
			} else {
				slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
							"Error: no method(s) specified for %s, should be "
							"one or more of %s\n", PAMPT_MAP_METHOD_ATTR,
							get_map_method_values());
			}
			rc = PAM_PASSTHRU_FAILURE;
			goto done;
		}
	}

	/* Validate filter by converting to Slapi_Filter */
	pam_filter_str = slapi_entry_attr_get_charptr(e, PAMPT_FILTER_ATTR);
	if (pam_filter_str) {
		pam_filter = slapi_str2filter(pam_filter_str);
		if (pam_filter == NULL) {
			if (returntext) {
				PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Error: invalid "
							"filter specified for %s (filter: \"%s\")",
							PAMPT_FILTER_ATTR, pam_filter_str);
			} else {
				slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
							"Error: invalid filter specified for %s "
							"(filter: \"%s\")\n", PAMPT_FILTER_ATTR,
							pam_filter_str);
			}
			rc = PAM_PASSTHRU_FAILURE;
			goto done;
		}
	}

	/* success */
	rc = PAM_PASSTHRU_SUCCESS;

done:
	slapi_ch_free_string(&map_method);
	slapi_ch_free_string(&pam_ident_attr);
	slapi_ch_array_free(excludes);
	excludes = NULL;
	slapi_ch_array_free(includes);
	includes = NULL;
	slapi_ch_free_string(&missing_suffix_str);
	slapi_ch_free_string(&pam_filter_str);
	slapi_filter_free(pam_filter, 1);

	return rc;
}

static Pam_PassthruSuffix *
New_Pam_PassthruSuffix(char *suffix)
{
	Pam_PassthruSuffix *newone = NULL;
	if (suffix) {
		newone = (Pam_PassthruSuffix *)slapi_ch_malloc(sizeof(Pam_PassthruSuffix));
		/* The passed in suffix should already be normalized. */
		newone->pamptsuffix_dn = slapi_sdn_new_normdn_byval(suffix);
		newone->pamptsuffix_next = NULL;
	}
	return newone;
}

static Pam_PassthruSuffix *
pam_ptconfig_add_suffixes(char **str_list)
{
	Pam_PassthruSuffix *head = NULL;
	Pam_PassthruSuffix *suffixent = NULL;

	if (str_list && *str_list) {
		int ii;
		for (ii = 0; str_list[ii]; ++ii) {
			Pam_PassthruSuffix *tmp = New_Pam_PassthruSuffix(str_list[ii]);
			if (!suffixent) {
				head = suffixent = tmp;
			} else {
				suffixent->pamptsuffix_next = tmp;
				suffixent = suffixent->pamptsuffix_next;
			}
		}
	}
	return head;
}

/*
  Apply the pending changes in the e entry to our config struct.
  validate must have already been called
*/
static int 
pam_passthru_apply_config (Slapi_Entry* e)
{
    int rc = PAM_PASSTHRU_SUCCESS;
    char **excludes = NULL;
    char **includes = NULL;
    char *new_service = NULL;
    char *pam_ident_attr = NULL;
    char *map_method = NULL;
    char *dn = NULL;
    PRBool fallback;
    PRBool secure;
    Pam_PassthruConfig *entry = NULL;
    PRCList *list;
    Slapi_Attr *a = NULL;
    char *filter_str = NULL;

    pam_ident_attr = slapi_entry_attr_get_charptr(e, PAMPT_PAM_IDENT_ATTR);
    map_method = slapi_entry_attr_get_charptr(e, PAMPT_MAP_METHOD_ATTR);
    new_service = slapi_entry_attr_get_charptr(e, PAMPT_SERVICE_ATTR);
    excludes = slapi_entry_attr_get_charray(e, PAMPT_EXCLUDES_ATTR);
    includes = slapi_entry_attr_get_charray(e, PAMPT_INCLUDES_ATTR);
    fallback = slapi_entry_attr_get_bool(e, PAMPT_FALLBACK_ATTR);
    filter_str = slapi_entry_attr_get_charptr(e, PAMPT_FILTER_ATTR);
    /* Require SSL/TLS if the secure attr is not specified.  We
     * need to check if the attribute is present to make this
     * determiniation. */
    if (slapi_entry_attr_find(e, PAMPT_SECURE_ATTR, &a) == 0) {
        secure = slapi_entry_attr_get_bool(e, PAMPT_SECURE_ATTR);
    } else {
        secure = PR_TRUE;
    }

    /* Allocate a config struct. */
    entry = (Pam_PassthruConfig *)
        slapi_ch_calloc(1, sizeof(Pam_PassthruConfig));
    if (NULL == entry) {
        rc = PAM_PASSTHRU_FAILURE;
        goto bail;
    }

    /* use the RDN method to derive the PAM identity by default*/
    entry->pamptconfig_map_method1 = PAMPT_MAP_METHOD_RDN;
    entry->pamptconfig_map_method2 = PAMPT_MAP_METHOD_NONE;
    entry->pamptconfig_map_method3 = PAMPT_MAP_METHOD_NONE;

    /* Fill in the struct. */
    dn = slapi_entry_get_ndn(e);
    if (dn) {
        entry->dn = slapi_ch_strdup(dn);
    }

    entry->pamptconfig_fallback = fallback;
    entry->pamptconfig_secure = secure;

    if (!entry->pamptconfig_service ||
        (new_service && PL_strcmp(entry->pamptconfig_service, new_service))) {
        slapi_ch_free_string(&entry->pamptconfig_service);
        entry->pamptconfig_service = new_service;
        new_service = NULL; /* config now owns memory */
    }

    /* get the list of excluded suffixes */
    pam_ptconfig_free_suffixes(entry->pamptconfig_excludes);
    entry->pamptconfig_excludes = pam_ptconfig_add_suffixes(excludes);

    /* get the list of included suffixes */
    pam_ptconfig_free_suffixes(entry->pamptconfig_includes);
    entry->pamptconfig_includes = pam_ptconfig_add_suffixes(includes);

    if (!entry->pamptconfig_pam_ident_attr ||
        (pam_ident_attr && PL_strcmp(entry->pamptconfig_pam_ident_attr, pam_ident_attr))) {
        slapi_ch_free_string(&entry->pamptconfig_pam_ident_attr);
        entry->pamptconfig_pam_ident_attr = pam_ident_attr;
        pam_ident_attr = NULL; /* config now owns memory */
    }

    if (map_method) {
        parse_map_method(map_method,
        &entry->pamptconfig_map_method1,
        &entry->pamptconfig_map_method2,
        &entry->pamptconfig_map_method3,
        NULL);
    }

    if (filter_str) {
        entry->filter_str = filter_str;
        filter_str = NULL; /* config now owns memory */
        entry->slapi_filter = slapi_str2filter(entry->filter_str);
    }

    /* Add config to list.  We just store at the tail. */
    if (!PR_CLIST_IS_EMPTY(pam_passthru_global_config)) {
        list = PR_LIST_HEAD(pam_passthru_global_config);
        while (list != pam_passthru_global_config) {
            list = PR_NEXT_LINK(list);

            if (pam_passthru_global_config == list) {
                /* add to tail */
                PR_INSERT_BEFORE(&(entry->list), list);
                slapi_log_error(SLAPI_LOG_CONFIG, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                                "store [%s] at tail\n", entry->dn);
                break;
            }
        }
    } else {
        /* first entry */
        PR_INSERT_LINK(&(entry->list), pam_passthru_global_config);
        slapi_log_error(SLAPI_LOG_CONFIG, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                        "store [%s] at head \n", entry->dn);
    }

  bail:
    slapi_ch_free_string(&new_service);
    slapi_ch_free_string(&map_method);
    slapi_ch_free_string(&pam_ident_attr);
    slapi_ch_free_string(&filter_str);
    slapi_ch_array_free(excludes);
    slapi_ch_array_free(includes);

    return rc;
}

static int
pam_passthru_check_suffix(Pam_PassthruConfig *cfg, const Slapi_DN *bindsdn)
{
	Pam_PassthruSuffix *try;
	int ret = LDAP_SUCCESS;

	if (!cfg->pamptconfig_includes && !cfg->pamptconfig_excludes) {
		goto done; /* NULL means allow */
	}

	/* exclude trumps include - if suffix is on exclude list, then
	   deny */
	for (try = cfg->pamptconfig_excludes; try; try = try->pamptsuffix_next) {
		if (slapi_sdn_issuffix(bindsdn, try->pamptsuffix_dn)) {
			ret = LDAP_UNWILLING_TO_PERFORM; /* suffix is excluded */
			goto done;
		}
	}

	/* ok, now flip it - deny access unless dn is on include list */
	if (cfg->pamptconfig_includes) {
		ret = LDAP_UNWILLING_TO_PERFORM; /* suffix is excluded */
		for (try = cfg->pamptconfig_includes; try; try = try->pamptsuffix_next) {
			if (slapi_sdn_issuffix(bindsdn, try->pamptsuffix_dn)) {
				ret = LDAP_SUCCESS; /* suffix is included */
				goto done;
			}
		}
	}
		
done:

	return ret;
}


/*
 * Find the config entry that matches the passed in bind DN
 */
Pam_PassthruConfig *
pam_passthru_get_config( Slapi_DN *bind_sdn )
{
    PRCList *list = NULL;
    Pam_PassthruConfig *cfg = NULL;

    /* Loop through config list to see if there is a match. */
    if (!PR_CLIST_IS_EMPTY(pam_passthru_global_config)) {
        list = PR_LIST_HEAD(pam_passthru_global_config);
        while (list != pam_passthru_global_config) {
            cfg = (Pam_PassthruConfig *)list;
            if (pam_passthru_check_suffix( cfg, bind_sdn ) == LDAP_SUCCESS) {
                if (cfg->slapi_filter) {
                    /* A filter is configured, so see if the bind entry is a match. */
                    Slapi_Entry *test_e = NULL;

                    /* Fetch the bind entry */
                    slapi_search_internal_get_entry(bind_sdn, NULL, &test_e,
                                                    pam_passthruauth_get_plugin_identity());

                    /* If the entry doesn't exist, just fall through to the main server code */
                    if (test_e) {
                        /* Evaluate the filter. */
                        if (LDAP_SUCCESS == slapi_filter_test_simple(test_e, cfg-> slapi_filter)) {
                            /* This is a match. */
                            slapi_entry_free(test_e);
                            goto done;
                        }

                        slapi_entry_free(test_e);
                    }
                } else {
                    /* There is no filter to check, so this is a match. */
                    goto done;
                }
            }

            cfg = NULL;
            list = PR_NEXT_LINK(list);
        }
    }

  done:
    return(cfg);
}

/*
 * Check if the DN is considered to be a config entry.
 *
 * If the config is stored in cn=config, the top-level plug-in
 * entry and it's children are considered to be config.  If an
 * alternate plug-in config area is being used, only the children
 * of the alternate config container are considered to be config.
 *
 * Returns 1 if DN is a config entry.
 */
int
pam_passthru_dn_is_config(Slapi_DN *sdn)
{
    int rc = 0;

    if (sdn == NULL) {
        goto bail;
    }

    /* Check if we're using the standard config area. */
    if (slapi_sdn_compare(pam_passthru_get_config_area(),
            pam_passthruauth_get_plugin_sdn()) == 0) {
        /* We're using the standard config area, so both
         * the container and the children are considered
         * to be config entries. */
        if (slapi_sdn_issuffix(sdn, pam_passthru_get_config_area())) {
            rc = 1;
        }
    } else {
        /* We're using an alternative config area, so only
         * the children are considered to be config entries. */
        if (slapi_sdn_issuffix(sdn, pam_passthru_get_config_area()) &&
                slapi_sdn_compare(sdn, pam_passthru_get_config_area())) {
            rc = 1;
        }
    }

  bail:
    return rc;
}

/*
 * Set the active config area.
 */
void
pam_passthru_set_config_area(Slapi_DN *sdn)
{
    _ConfigArea = sdn;
}

/*
 * Return the active config area.
 */
Slapi_DN *
pam_passthru_get_config_area()
{
    return _ConfigArea;
}


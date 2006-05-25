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
/*
 * ptconfig.c - configuration-related code for Pass Through Authentication
 *
 */

#include <plstr.h>

#include "pam_passthru.h"

#define PAM_PT_CONFIG_FILTER "(objectclass=*)"

/*
 * The configuration attributes are contained in the plugin entry e.g.
 * cn=PAM Pass Through,cn=plugins,cn=config
 *
 * Configuration is a two step process.  The first pass is a validation step which
 * occurs pre-op - check inputs and error out if bad.  The second pass actually
 * applies the changes to the run time config.
 */


/*
 * function prototypes
 */ 
static int pam_passthru_validate_config (Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, 
										 int *returncode, char *returntext, void *arg);
static int pam_passthru_apply_config (Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, 
										 int *returncode, char *returntext, void *arg);
static int pam_passthru_search (Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, 
								int *returncode, char *returntext, void *arg)
{
	return SLAPI_DSE_CALLBACK_OK;
}

/*
 * static variables
 */
/* for now, there is only one configuration and it is global to the plugin  */
static Pam_PassthruConfig	theConfig;
static int		inited = 0;


static int dont_allow_that(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, 
						   int *returncode, char *returntext, void *arg)
{
	*returncode = LDAP_UNWILLING_TO_PERFORM;
    return SLAPI_DSE_CALLBACK_ERROR;
}

/*
 * Read configuration and create a configuration data structure.
 * This is called after the server has configured itself so we can check
 *   for things like collisions between our suffixes and backend's suffixes.
 * Returns an LDAP error code (LDAP_SUCCESS if all goes well).
 */
int
pam_passthru_config(Slapi_Entry *config_e)
{
	int returncode = LDAP_SUCCESS;
	char returntext[SLAPI_DSE_RETURNTEXT_SIZE];

    if ( inited ) {
		slapi_log_error( SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
						 "only one PAM pass through plugin instance can be used\n" );
		return( LDAP_PARAM_ERROR );
    }

	/* initialize fields */
	if ((theConfig.lock = slapi_new_mutex()) == NULL) {
	    return( LDAP_LOCAL_ERROR );
	}
	/* do not fallback to regular bind */
	theConfig.pamptconfig_fallback = PR_FALSE;
	/* require TLS/SSL security */
	theConfig.pamptconfig_secure = PR_TRUE;
	/* use the RDN method to derive the PAM identity */
	theConfig.pamptconfig_map_method1 = PAMPT_MAP_METHOD_RDN;
	theConfig.pamptconfig_map_method2 = PAMPT_MAP_METHOD_NONE;
	theConfig.pamptconfig_map_method3 = PAMPT_MAP_METHOD_NONE;

	if (SLAPI_DSE_CALLBACK_OK == pam_passthru_validate_config(NULL, NULL, config_e,
															  &returncode, returntext, NULL)) {
		pam_passthru_apply_config(NULL, NULL, config_e,
								  &returncode, returntext, NULL);
	}

	/* config DSE must be initialized before we get here */
	if (returncode == LDAP_SUCCESS) {
		const char *config_dn = slapi_entry_get_dn_const(config_e);
		slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, config_dn, LDAP_SCOPE_BASE,
									   PAM_PT_CONFIG_FILTER, pam_passthru_validate_config,NULL);
		slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_POSTOP, config_dn, LDAP_SCOPE_BASE,
									   PAM_PT_CONFIG_FILTER, pam_passthru_apply_config,NULL);
		slapi_config_register_callback(SLAPI_OPERATION_MODRDN, DSE_FLAG_PREOP, config_dn, LDAP_SCOPE_BASE,
									   PAM_PT_CONFIG_FILTER, dont_allow_that, NULL);
		slapi_config_register_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, config_dn, LDAP_SCOPE_BASE,
									   PAM_PT_CONFIG_FILTER, dont_allow_that, NULL);
		slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, config_dn, LDAP_SCOPE_BASE,
									   PAM_PT_CONFIG_FILTER, pam_passthru_search,NULL);
	}

    inited = 1;

	if (returncode != LDAP_SUCCESS) {
		slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
						"Error %d: %s\n", returncode, returntext);
	}

    return returncode;
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

#define MAKE_STR(x) #x
static char *get_missing_suffix_values()
{
	return MAKE_STR(PAMPT_MISSING_SUFFIX_ERROR) ", " MAKE_STR(PAMPT_MISSING_SUFFIX_ALLOW) ", "
		MAKE_STR(PAMPT_MISSING_SUFFIX_IGNORE);
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
	int ret;

	*err = 0;
	if (!map_method || !*map_method) {
		return PAMPT_MAP_METHOD_NONE;
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

	if (!err) {
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
	int err = 0;
	int extra;

	*one = *two = *three = PAMPT_MAP_METHOD_NONE;
	*one = meth_to_int(&map_method, &err);
	if (err) {
		PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
					"The map method in the string [%s] is invalid: must be "
					"one of %s", map_method, get_map_method_values());
		return LDAP_UNWILLING_TO_PERFORM;
	}
	*two = meth_to_int(&map_method, &err);
	if (err) {
		PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
					"The map method in the string [%s] is invalid: must be "
					"one of %s", map_method, get_map_method_values());
		return LDAP_UNWILLING_TO_PERFORM;
	}
	*three = meth_to_int(&map_method, &err);
	if (err) {
		PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
					"The map method in the string [%s] is invalid: must be "
					"one of %s", map_method, get_map_method_values());
		return LDAP_UNWILLING_TO_PERFORM;
	}
	if (((extra = meth_to_int(&map_method, &err)) != PAMPT_MAP_METHOD_NONE) ||
		err) {
		PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
					"Invalid extra text [%s] after last map method",
					map_method);
		return LDAP_UNWILLING_TO_PERFORM;		
	}

	return err;
}
		
/*
  Validate the pending changes in the e entry.
*/
static int 
pam_passthru_validate_config (Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, 
	int *returncode, char *returntext, void *arg)
{
	char *missing_suffix_str = NULL;
	int missing_suffix;
	int ii;
	char **excludes = NULL;
	char **includes = NULL;
	char *pam_ident_attr = NULL;
	char *map_method = NULL;

	*returncode = LDAP_UNWILLING_TO_PERFORM; /* be pessimistic */
	/* first, get the missing_suffix flag and validate it */
	missing_suffix_str = slapi_entry_attr_get_charptr(e, PAMPT_MISSING_SUFFIX_ATTR);
	if ((missing_suffix = missing_suffix_to_int(missing_suffix_str)) < 0 ||
		!check_missing_suffix_flag(missing_suffix)) {
		PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
					"Error: valid values for %s are %s",
					PAMPT_MISSING_SUFFIX_ATTR, get_missing_suffix_values());
		goto done;
	}

	if (missing_suffix != PAMPT_MISSING_SUFFIX_IGNORE) {
		char **missing_list = NULL;
		Slapi_DN *comp_dn = slapi_sdn_new();

		/* get the list of excluded suffixes */
		excludes = slapi_entry_attr_get_charray(e, PAMPT_EXCLUDES_ATTR);
		for (ii = 0; excludes && excludes[ii]; ++ii) {
			slapi_sdn_init_dn_byref(comp_dn, excludes[ii]);
			if (!slapi_be_exist(comp_dn)) {
				charray_add(&missing_list, slapi_ch_strdup(excludes[ii]));
			}
			slapi_sdn_done(comp_dn);
		}

		/* get the list of included suffixes */
		includes = slapi_entry_attr_get_charray(e, PAMPT_INCLUDES_ATTR);
		for (ii = 0; includes && includes[ii]; ++ii) {
			slapi_sdn_init_dn_byref(comp_dn, includes[ii]);
			if (!slapi_be_exist(comp_dn)) {
				charray_add(&missing_list, slapi_ch_strdup(includes[ii]));
			}
			slapi_sdn_done(comp_dn);
		}

		slapi_sdn_free(&comp_dn);

		if (missing_list) {
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
			slapi_ch_array_free(missing_list);
			missing_list = NULL;
			if (missing_suffix != PAMPT_MISSING_SUFFIX_ERROR) {
				slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
								"Warning: %s\n", returntext);
				*returntext = 0; /* log error, don't report back to user */
			} else {
				goto done;
			}
		}
	}

	pam_ident_attr = slapi_entry_attr_get_charptr(e, PAMPT_PAM_IDENT_ATTR);
	map_method = slapi_entry_attr_get_charptr(e, PAMPT_MAP_METHOD_ATTR);
	if (map_method) {
		int one, two, three;
		*returncode = parse_map_method(map_method, &one, &two, &three, returntext);
		if (!pam_ident_attr &&
			((one == PAMPT_MAP_METHOD_ENTRY) || (two == PAMPT_MAP_METHOD_ENTRY) ||
			 (three == PAMPT_MAP_METHOD_ENTRY))) {
			PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Error: the %s method"
						" was specified, but no %s was given",
						PAMPT_MAP_METHOD_ENTRY_STRING, PAMPT_PAM_IDENT_ATTR);
			*returncode = LDAP_UNWILLING_TO_PERFORM;
			goto done;
		}
		if (one == two == three == PAMPT_MAP_METHOD_NONE) {
			PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Error: no method(s)"
						" specified for %s, should be one or more of %s",
						PAMPT_MAP_METHOD_ATTR, get_map_method_values());
			*returncode = LDAP_UNWILLING_TO_PERFORM;
			goto done;
		}
	}

	/* success */
	*returncode = LDAP_SUCCESS;

done:
	slapi_ch_free_string(&map_method);
	slapi_ch_free_string(&pam_ident_attr);
	slapi_ch_array_free(excludes);
	excludes = NULL;
	slapi_ch_array_free(includes);
	includes = NULL;
	slapi_ch_free_string(&missing_suffix_str);

	if (*returncode != LDAP_SUCCESS)
	{
		return SLAPI_DSE_CALLBACK_ERROR;
	}
	else
    {
	    return SLAPI_DSE_CALLBACK_OK;
    }
}

static Pam_PassthruSuffix *
New_Pam_PassthruSuffix(char *suffix)
{
	Pam_PassthruSuffix *newone = NULL;
	if (suffix) {
		newone = (Pam_PassthruSuffix *)slapi_ch_malloc(sizeof(Pam_PassthruSuffix));
		newone->pamptsuffix_dn = slapi_sdn_new();
		slapi_sdn_init_dn_byval(newone->pamptsuffix_dn, suffix);
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
  Apply the pending changes in the e entry to our config struct.
  validate must have already been called
*/
static int 
pam_passthru_apply_config (Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, 
	int *returncode, char *returntext, void *arg)
{
	char **excludes = NULL;
	char **includes = NULL;
	char *new_service = NULL;
	char *pam_ident_attr = NULL;
	char *map_method = NULL;
	PRBool fallback;
	PRBool secure;

	*returncode = LDAP_SUCCESS;

	pam_ident_attr = slapi_entry_attr_get_charptr(e, PAMPT_PAM_IDENT_ATTR);
	map_method = slapi_entry_attr_get_charptr(e, PAMPT_MAP_METHOD_ATTR);
	new_service = slapi_entry_attr_get_charptr(e, PAMPT_SERVICE_ATTR);
	excludes = slapi_entry_attr_get_charray(e, PAMPT_EXCLUDES_ATTR);
	includes = slapi_entry_attr_get_charray(e, PAMPT_INCLUDES_ATTR);
	fallback = slapi_entry_attr_get_bool(e, PAMPT_FALLBACK_ATTR);
	secure = slapi_entry_attr_get_bool(e, PAMPT_SECURE_ATTR);

	/* lock config here */
	slapi_lock_mutex(theConfig.lock);

	theConfig.pamptconfig_fallback = fallback;
	theConfig.pamptconfig_secure = secure;
	if (!theConfig.pamptconfig_service ||
		(new_service && PL_strcmp(theConfig.pamptconfig_service, new_service))) {
		slapi_ch_free_string(&theConfig.pamptconfig_service);
		theConfig.pamptconfig_service = new_service;
		new_service = NULL; /* config now owns memory */
	}

	/* get the list of excluded suffixes */
	pam_ptconfig_free_suffixes(theConfig.pamptconfig_excludes);
	theConfig.pamptconfig_excludes = pam_ptconfig_add_suffixes(excludes);

	/* get the list of included suffixes */
	pam_ptconfig_free_suffixes(theConfig.pamptconfig_includes);
	theConfig.pamptconfig_includes = pam_ptconfig_add_suffixes(includes);

	if (!theConfig.pamptconfig_pam_ident_attr ||
		(pam_ident_attr && PL_strcmp(theConfig.pamptconfig_pam_ident_attr, pam_ident_attr))) {
		slapi_ch_free_string(&theConfig.pamptconfig_pam_ident_attr);
		theConfig.pamptconfig_pam_ident_attr = pam_ident_attr;
		pam_ident_attr = NULL; /* config now owns memory */
	}

	if (map_method) {
		parse_map_method(map_method,
						 &theConfig.pamptconfig_map_method1,
						 &theConfig.pamptconfig_map_method2,
						 &theConfig.pamptconfig_map_method3,
						 NULL);
	}

	/* unlock config here */
	slapi_unlock_mutex(theConfig.lock);

	slapi_ch_free_string(&new_service);
	slapi_ch_free_string(&map_method);
	slapi_ch_free_string(&pam_ident_attr);
	slapi_ch_array_free(excludes);
	slapi_ch_array_free(includes);

	if (*returncode != LDAP_SUCCESS)
	{
		return SLAPI_DSE_CALLBACK_ERROR;
	}
	else
    {
	    return SLAPI_DSE_CALLBACK_OK;
    }
}

int
pam_passthru_check_suffix(Pam_PassthruConfig *cfg, char *binddn)
{
	Slapi_DN *comp_dn;
	Pam_PassthruSuffix *try;
	int ret = LDAP_SUCCESS;

	comp_dn = slapi_sdn_new();
	slapi_sdn_init_dn_byref(comp_dn, binddn);

	slapi_lock_mutex(cfg->lock);
	if (!cfg->pamptconfig_includes && !cfg->pamptconfig_excludes) {
		goto done; /* NULL means allow */
	}

	/* exclude trumps include - if suffix is on exclude list, then
	   deny */
	for (try = cfg->pamptconfig_excludes; try; try = try->pamptsuffix_next) {
		if (slapi_sdn_issuffix(comp_dn, try->pamptsuffix_dn)) {
			ret = LDAP_UNWILLING_TO_PERFORM; /* suffix is excluded */
			goto done;
		}
	}

	/* ok, now flip it - deny access unless dn is on include list */
	if (cfg->pamptconfig_includes) {
		ret = LDAP_UNWILLING_TO_PERFORM; /* suffix is excluded */
		for (try = cfg->pamptconfig_includes; try; try = try->pamptsuffix_next) {
			if (slapi_sdn_issuffix(comp_dn, try->pamptsuffix_dn)) {
				ret = LDAP_SUCCESS; /* suffix is included */
				goto done;
			}
		}
	}
		
done:
	slapi_unlock_mutex(cfg->lock);
	slapi_sdn_free(&comp_dn);

	return ret;
}

/*
 * Get the pass though configuration data.  For now, there is only one
 * configuration and it is global to the plugin.
 */
Pam_PassthruConfig *
pam_passthru_get_config( void )
{
    return( &theConfig );
}

/******************************************************************************
Copyright (C) 2009 Hewlett-Packard Development Company, L.P.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
version 2 as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

Contributors:
Hewlett-Packard Development Company, L.P.
******************************************************************************/

#include <stdio.h>
#include <string.h>
#include "slapi-plugin.h"
#include "acctpolicy.h"
#include "nspr.h"

/* Globals */
static acctPluginCfg globalcfg;

/* Local function prototypes */
static int acct_policy_entry2config( Slapi_Entry *e,
	acctPluginCfg *newcfg );

/*
  Creates global config structure from config entry at plugin startup
*/
int
acct_policy_load_config_startup( Slapi_PBlock* pb, void* plugin_id ) {
	acctPluginCfg *newcfg;
	Slapi_Entry *config_entry = NULL;
	Slapi_DN *config_sdn = NULL;
	int rc;

	/* Retrieve the config entry */
	config_sdn = slapi_sdn_new_normdn_byref( PLUGIN_CONFIG_DN );
	rc = slapi_search_internal_get_entry( config_sdn, NULL, &config_entry,
		plugin_id);
	slapi_sdn_free( &config_sdn );

	if( rc != LDAP_SUCCESS || config_entry == NULL ) {
		slapi_log_error( SLAPI_LOG_FATAL, PLUGIN_NAME,
			"Failed to retrieve configuration entry %s: %d\n",
			PLUGIN_CONFIG_DN, rc );
		return( -1 );
	}

	newcfg = get_config();
	rc = acct_policy_entry2config( config_entry, newcfg );

	slapi_entry_free( config_entry );

	return( rc );
}

/*
   Parses config entry into config structure, caller is responsible for
   allocating the config structure memory
*/
static int
acct_policy_entry2config( Slapi_Entry *e, acctPluginCfg *newcfg ) {
	char *config_val;
	int rc = 0;

	if( newcfg == NULL ) {
		slapi_log_error( SLAPI_LOG_FATAL, PLUGIN_NAME,
			"Failed to allocate configuration structure\n" );
		return( -1 );
	}

	memset( newcfg, 0, sizeof( acctPluginCfg ) );

	newcfg->state_attr_name = get_attr_string_val( e, CFG_LASTLOGIN_STATE_ATTR );
	if( newcfg->state_attr_name == NULL ) {
		newcfg->state_attr_name = slapi_ch_strdup( DEFAULT_LASTLOGIN_STATE_ATTR );
	} else if (!update_is_allowed_attr(newcfg->state_attr_name)) {
		/* log a warning that this attribute cannot be updated */
		slapi_log_error( SLAPI_LOG_FATAL, PLUGIN_NAME,
							 "The configured state attribute [%s] cannot be updated, accounts will always become inactive.\n",
							 newcfg->state_attr_name );
	}

	newcfg->alt_state_attr_name = get_attr_string_val( e, CFG_ALT_LASTLOGIN_STATE_ATTR );
	/* alt_state_attr_name should be optional, but for backward compatibility, 
	 * if not specified use a default. If the attribute is "1.1", no fallback 
	 * will be used
	 */ 
	if( newcfg->alt_state_attr_name == NULL ) {
		newcfg->alt_state_attr_name = slapi_ch_strdup( DEFAULT_ALT_LASTLOGIN_STATE_ATTR );
	} else if ( !strcmp( newcfg->alt_state_attr_name, "1.1" ) ) {
                 slapi_ch_free_string( &newcfg->alt_state_attr_name ); /*none - NULL */
	} /* else use configured value */

	newcfg->spec_attr_name = get_attr_string_val( e, CFG_SPEC_ATTR );
	if( newcfg->spec_attr_name == NULL ) {
		newcfg->spec_attr_name = slapi_ch_strdup( DEFAULT_SPEC_ATTR );
	}

	newcfg->limit_attr_name = get_attr_string_val( e, CFG_INACT_LIMIT_ATTR );
	if( newcfg->limit_attr_name == NULL ) {
		newcfg->limit_attr_name = slapi_ch_strdup( DEFAULT_INACT_LIMIT_ATTR );
	}

	config_val = get_attr_string_val( e, CFG_RECORD_LOGIN );
	if( config_val &&
		( strcasecmp( config_val, "true" ) == 0 ||
		  strcasecmp( config_val, "yes" ) == 0 ||
		  strcasecmp( config_val, "on" ) == 0 ||
		  strcasecmp( config_val, "1" ) == 0 ) ) {
		newcfg->always_record_login = 1;
	} else {
		newcfg->always_record_login = 0;
	}
	slapi_ch_free_string(&config_val);

	/* the default limit if not set in the acctPolicySubentry */
	config_val = get_attr_string_val( e, newcfg->limit_attr_name );
	if( config_val ) {
		char *endptr = NULL;
		newcfg->inactivitylimit = strtoul(config_val, &endptr, 10);
		if (endptr && (*endptr != '\0')) {
			slapi_log_error( SLAPI_LOG_FATAL, PLUGIN_NAME,
							 "Failed to parse [%s] from the config entry: [%s] is not a valid unsigned long value\n",
							 newcfg->limit_attr_name, config_val );

			rc = -1;
			newcfg->inactivitylimit = ULONG_MAX;
		}
	} else {
		newcfg->inactivitylimit = ULONG_MAX;
	}
	slapi_ch_free_string(&config_val);

	return( rc );
}

/*
  Returns a pointer to config structure for use by any code needing to look
  at, for example, attribute mappings
*/
acctPluginCfg*
get_config() {
	return( &globalcfg );
}


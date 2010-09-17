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

/* Account Policy plugin */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "slapi-plugin.h"
#include "acctpolicy.h"

/*
  Checks bind entry for last login state and compares current time with last
  login time plus the limit to decide whether to deny the bind.
*/
static int
acct_inact_limit( Slapi_PBlock *pb, char *dn, Slapi_Entry *target_entry, acctPolicy *policy )
{
	char *lasttimestr = NULL;
	time_t lim_t, last_t, cur_t;
	int rc = 0; /* Optimistic default */
	acctPluginCfg *cfg;
	void *plugin_id;

	cfg = get_config();
	plugin_id = get_identity();
	if( ( lasttimestr = get_attr_string_val( target_entry,
		cfg->state_attr_name ) ) != NULL ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
			"\"%s\" login timestamp is %s\n", dn, lasttimestr );
	} else if( ( lasttimestr = get_attr_string_val( target_entry,
		cfg->alt_state_attr_name ) ) != NULL ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
			"\"%s\" alternate timestamp is %s\n", dn, lasttimestr );
	} else {
		slapi_log_error( SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
			"\"%s\" has no login or creation timestamp\n", dn );
		rc = -1;
		goto done;
	}

	last_t = gentimeToEpochtime( lasttimestr );
	cur_t = time( (time_t*)0 );
	lim_t = policy->inactivitylimit;

	/* Finally do the time comparison */
	if( cur_t > last_t + lim_t ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
			"\"%s\" has exceeded inactivity limit  (%ld > (%ld + %ld))\n",
			dn, cur_t, last_t, lim_t );
		rc = 1;
		goto done;
	} else {
		slapi_log_error( SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
			"\"%s\" is within inactivity limit (%ld < (%ld + %ld))\n",
			dn, cur_t, last_t, lim_t );
	}

done:
	/* Deny bind; the account has exceeded the inactivity limit */
	if( rc == 1 ) {
		slapi_send_ldap_result( pb, LDAP_CONSTRAINT_VIOLATION, NULL,
			"Account inactivity limit exceeded."
			" Contact system administrator to reset.", 0, NULL );
	}

    slapi_ch_free_string( &lasttimestr );

	return( rc );
}

/*
  This is called after binds, it updates an attribute in the account
  with the current time.
*/
static int
acct_record_login( Slapi_PBlock *modpb, char *dn )
{
	int ldrc;
	int rc = 0; /* Optimistic default */
	LDAPMod *mods[2];
	LDAPMod mod;
	struct berval *vals[2];
	struct berval val;
	char *timestr = NULL;
	acctPluginCfg *cfg;
	void *plugin_id;

	cfg = get_config();
	plugin_id = get_identity();

	timestr = epochtimeToGentime( time( (time_t*)0 ) );
	val.bv_val = timestr;
	val.bv_len = strlen( val.bv_val );

	vals [0] = &val;
	vals [1] = NULL;

	mod.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
	mod.mod_type = cfg->state_attr_name;
	mod.mod_bvalues = vals;

	mods[0] = &mod;
	mods[1] = NULL;

	modpb = slapi_pblock_new();

	slapi_modify_internal_set_pb( modpb, dn, mods, NULL, NULL,
	 	plugin_id, SLAPI_OP_FLAG_NO_ACCESS_CHECK |
			SLAPI_OP_FLAG_BYPASS_REFERRALS );
	slapi_modify_internal_pb( modpb );

	slapi_pblock_get( modpb, SLAPI_PLUGIN_INTOP_RESULT, &ldrc );

	if (ldrc != LDAP_SUCCESS) {
		slapi_log_error( SLAPI_LOG_FATAL, POST_PLUGIN_NAME,
			"Recording %s=%s failed on \"%s\" err=%d\n", cfg->state_attr_name,
			timestr, dn, ldrc );
		rc = -1;
		goto done;
	} else {
		slapi_log_error( SLAPI_LOG_PLUGIN, POST_PLUGIN_NAME,
			"Recorded %s=%s on \"%s\"\n", cfg->state_attr_name, timestr, dn );
	}

done:
	if( timestr ) {
		slapi_ch_free_string( &timestr );
	}

	return( rc );
}

/*
  Handles bind preop callbacks
*/
int
acct_bind_preop( Slapi_PBlock *pb )
{
	char *dn = NULL;
	Slapi_DN *sdn = NULL;
	Slapi_Entry *target_entry = NULL;
	int rc = 0; /* Optimistic default */
	int ldrc;
	acctPolicy *policy = NULL;
	void *plugin_id;

	slapi_log_error( SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
		"=> acct_bind_preop\n" );

	plugin_id = get_identity();

	/* This does not give a copy, so don't free it */
	if( slapi_pblock_get( pb, SLAPI_BIND_TARGET, &dn ) != 0 ) {
		slapi_log_error( SLAPI_LOG_FATAL, PRE_PLUGIN_NAME,
			"Error retrieving target DN\n" );
		rc = -1;
		goto done;
	}

	/* The plugin wouldn't get called for anonymous binds but let's check */
	if ( dn == NULL ) {
		goto done;
	}

	sdn = slapi_sdn_new_dn_byref( dn );

	ldrc = slapi_search_internal_get_entry( sdn, NULL, &target_entry,
		plugin_id );

	/* There was a problem retrieving the entry */
	if( ldrc != LDAP_SUCCESS ) {
		if( ldrc != LDAP_NO_SUCH_OBJECT ) {
			/* The problem is not a bad bind or virtual entry; halt bind */
			slapi_log_error( SLAPI_LOG_FATAL, PRE_PLUGIN_NAME,
				"Failed to retrieve entry \"%s\": %d\n", dn, ldrc );
			rc = -1;
		}
		goto done;
	}

	if( get_acctpolicy( pb, target_entry, plugin_id, &policy ) ) {
		slapi_log_error( SLAPI_LOG_FATAL, PRE_PLUGIN_NAME,
			"Account Policy object for \"%s\" is missing\n", dn );
		rc = -1;
		goto done;
	}

	/* Null policy means target isnt's under the influence of a policy */
	if( policy == NULL ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
			"\"%s\" is not governed by an account policy\n", dn);
		goto done;
	}

	/* Check whether the account is in violation of inactivity limit */
	rc = acct_inact_limit( pb, dn, target_entry, policy );

	/* ...Any additional account policy enforcement goes here... */

done:
	/* Internal error */
	if( rc == -1 ) {
		slapi_send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL, NULL, 0, NULL );
	}

	if( target_entry ) {
		slapi_entry_free( target_entry );
	}

	if( sdn ) {
		slapi_sdn_free( &sdn );
	}

	if( policy ) {
		free_acctpolicy( &policy );
	}

	slapi_log_error( SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
		"<= acct_bind_preop\n" );

	return( rc == 0 ? CALLBACK_OK : CALLBACK_ERR );
}

/*
  This is called after binds, it updates an attribute in the entry that the
  bind DN corresponds to with the current time if it has an account policy
  specifier.
*/
int
acct_bind_postop( Slapi_PBlock *pb )
{
	char *dn = NULL;
	Slapi_PBlock *modpb = NULL;
	int ldrc, tracklogin = 0;
	int rc = 0; /* Optimistic default */
	Slapi_DN *sdn = NULL;
	Slapi_Entry *target_entry = NULL;
	acctPluginCfg *cfg;
	void *plugin_id;

	slapi_log_error( SLAPI_LOG_PLUGIN, POST_PLUGIN_NAME,
		"=> acct_bind_postop\n" );

	plugin_id = get_identity();

	/* Retrieving SLAPI_CONN_DN from the pb gives a copy */
	if( slapi_pblock_get( pb, SLAPI_CONN_DN, &dn ) != 0 ) {
		slapi_log_error( SLAPI_LOG_FATAL, POST_PLUGIN_NAME,
			"Error retrieving bind DN\n" );
		rc = -1;
		goto done;
	}

	/* Client is anonymously bound */
	if( dn == NULL ) {
		goto done;
	}

	cfg = get_config();
	tracklogin = cfg->always_record_login;

	/* We're not always tracking logins, so check whether the entry is
	   covered by an account policy to decide whether we should track */
	if( tracklogin == 0 ) {
		sdn = slapi_sdn_new_dn_byref( dn );
		ldrc = slapi_search_internal_get_entry( sdn, NULL, &target_entry,
			plugin_id );

		if( ldrc != LDAP_SUCCESS ) {
			slapi_log_error( SLAPI_LOG_FATAL, POST_PLUGIN_NAME,
				"Failed to retrieve entry \"%s\": %d\n", dn, ldrc );
			rc = -1;
			goto done;
		} else {
			if( target_entry && has_attr( target_entry,
				cfg->spec_attr_name, NULL ) ) {
				/* This account has a policy specifier */
				tracklogin = 1;
			}
		}
	}

	if( tracklogin ) {
		rc = acct_record_login( modpb, dn );
	}

	/* ...Any additional account policy postops go here... */

done:
	if( rc == -1 ) {
		slapi_send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL, NULL, 0, NULL );
	}

	if( modpb ) {
		slapi_pblock_destroy( modpb );
	}

	if( target_entry ) {
		slapi_entry_free( target_entry );
	}

	if( sdn ) {
		slapi_sdn_free( &sdn );
	}

	if( dn ) {
		slapi_ch_free_string( &dn );
	}

	slapi_log_error( SLAPI_LOG_PLUGIN, POST_PLUGIN_NAME,
		"<= acct_bind_postop\n" );

	return( rc == 0 ? CALLBACK_OK : CALLBACK_ERR );
}

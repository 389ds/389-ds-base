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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include "slapi-plugin.h"
#include "acctpolicy.h"

/* Globals */
static void* plugin_id = NULL;

/*
   Checks whether an entry has a particular attribute type, and optionally
   returns the value.  Only for use with single-valued attributes - it returns
   the first value it finds.
*/
int
has_attr( Slapi_Entry* target_entry, char* attr_name, char** val ) {
	Slapi_ValueSet *values = NULL;
	Slapi_Value* sval;
	char *actual_type_name = NULL;
	int type_name_disposition = 0, attr_free_flags = 0, rc = 0;

	/* Use vattr interface to support virtual attributes, e.g.
	   acctPolicySubentry has a good chance of being supplied by CoS */
	if ( slapi_vattr_values_get( target_entry, attr_name, &values, &type_name_disposition, &actual_type_name, 0, &attr_free_flags) == 0) {
		if( slapi_valueset_first_value( values, &sval ) == -1 ) {
			rc = 0;
		} else {
			rc = 1;
			if( val ) {
				/* Caller wants a copy of the found attribute's value */
				*val = slapi_ch_strdup( slapi_value_get_string( sval ) );
			}
		}
	} else {
		rc = 0;
	}

	slapi_vattr_values_free(&values, &actual_type_name, attr_free_flags);
	return( rc );
}

/*
  Lazy wrapper for has_attr()
*/
char*
get_attr_string_val( Slapi_Entry* target_entry, char* attr_name ) {
	char* ret = NULL;
	has_attr( target_entry, attr_name, &ret );
	return( ret );
}

/*
  Given an entry, provide the account policy in effect for that entry.
  Returns non-0 if function fails.  If account policy comes back NULL, it's
  not an error; the entry is simply not covered by a policy.
*/
int
get_acctpolicy( Slapi_PBlock *pb, Slapi_Entry *target_entry, void *plugin_id,
		acctPolicy **policy ) {
	Slapi_DN *sdn = NULL;
	Slapi_Entry *policy_entry = NULL;
	Slapi_Attr *attr;
	Slapi_Value *sval = NULL;
	int ldrc;
	char *attr_name;
	char *policy_dn = NULL;
	acctPluginCfg *cfg;
    int rc = 0;

	cfg = get_config();

	if( policy == NULL ) {
		/* Bad parameter */
		return( -1 );
	}

	*policy = NULL;

	/* Return success and NULL policy */
	policy_dn = get_attr_string_val( target_entry, cfg->spec_attr_name );
	if( policy_dn == NULL ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, PLUGIN_NAME,
				"\"%s\" is not governed by an account inactivity "
				" policy\n", slapi_entry_get_ndn( target_entry ) );
		return( rc );
	}

	sdn = slapi_sdn_new_dn_byref( policy_dn );
	ldrc = slapi_search_internal_get_entry( sdn, NULL, &policy_entry,
		plugin_id );
	slapi_sdn_free( &sdn );

	/* There should be a policy but it can't be retrieved; fatal error */
	if( policy_entry == NULL ) {
		if( ldrc != LDAP_NO_SUCH_OBJECT ) {
			slapi_log_error( SLAPI_LOG_FATAL, PLUGIN_NAME,
				"Error retrieving policy entry \"%s\": %d\n", policy_dn, ldrc );
		} else {
			slapi_log_error( SLAPI_LOG_PLUGIN, PLUGIN_NAME,
				"Policy entry \"%s\" is missing: %d\n", policy_dn, ldrc );
		}
		rc = -1;
        goto done;
	}

	*policy = (acctPolicy *)slapi_ch_calloc( 1, sizeof( acctPolicy ) );

	for( slapi_entry_first_attr( policy_entry, &attr ); attr != NULL;
			slapi_entry_next_attr( policy_entry, attr, &attr ) ) {
		slapi_attr_get_type(attr, &attr_name);
		if( !strcasecmp( attr_name, cfg->limit_attr_name ) ) {
			if( slapi_attr_first_value( attr, &sval ) == 0 ) {
				(*policy)->inactivitylimit = slapi_value_get_ulong( sval );
			}
		}
	}
done:
    slapi_ch_free_string( &policy_dn );
	slapi_entry_free( policy_entry );
	return( rc );
}

/*
  Frees an account policy allocated by get_acctpolicy()
*/
void
free_acctpolicy( acctPolicy **policy ) {
	slapi_ch_free( (void**)policy );
	return;
}

/*
  Plugin plumbing
*/
void
set_identity(void *identity) {
	plugin_id = identity;
}

/*
  Plugin plumbing
*/
void*
get_identity() {
	return( plugin_id );
}

/*
  A more flexible atoi(), converts to integer and returns the characters
  between (src+offset) and (src+offset+len). No support for negative numbers,
  which doesn't affect our time parsing.
*/
int
antoi( char *src, int offset, int len ) {
	int pow = 1, res = 0;

	if( len < 0 ) {
		return( -1 );
	}
	while( --len != -1 ) {
		if( !isdigit( src[offset+len] ) ) {
			res = -1;
			break;
		} else {
			res += ( src[offset+len] - '0' ) * pow ;
			pow *= 10;
		}
	}
	return( res );
}

/*
  Converts generalized time to UNIX GMT time.  For example:
  "20060807211257Z" -> 1154981577
*/
time_t
gentimeToEpochtime( char *gentimestr ) {
	time_t epochtime, cur_local_epochtime, cur_gm_epochtime, zone_offset;
	struct tm t, *cur_gm_time;

	/* Find the local offset from GMT */
	cur_gm_time = (struct tm*)slapi_ch_calloc( 1, sizeof( struct tm ) );
	cur_local_epochtime = time( (time_t)0 );
	gmtime_r( &cur_local_epochtime, cur_gm_time );
	cur_gm_epochtime = mktime( cur_gm_time );
	free( cur_gm_time );
	zone_offset = cur_gm_epochtime - cur_local_epochtime;

	/* Parse generalizedtime string into a tm struct */
	t.tm_year = antoi( gentimestr, 0, 4 ) - 1900;
	t.tm_mon = antoi( gentimestr, 4, 2 ) - 1;
	t.tm_mday = antoi( gentimestr, 6, 2 );
	t.tm_hour = antoi( gentimestr, 8, 2 );
	t.tm_min = antoi( gentimestr, 10, 2 );
	t.tm_sec = antoi( gentimestr, 12, 2 );
	t.tm_isdst = 0; /* DST does not apply to UTC */

	/* Turn tm object into local epoch time */
	epochtime = mktime( &t );

	/* Turn local epoch time into GMT epoch time */
	epochtime -= zone_offset;

	return( epochtime );
}

/*
  Converts UNIX time to generalized time.  For example:
  1154981577 -> "20060807211257Z"
*/
char*
epochtimeToGentime( time_t epochtime ) {
	char *gentimestr;
	struct tm t;

	gmtime_r( &epochtime, &t );
	gentimestr = slapi_ch_malloc( 20 );
	/* Format is YYYYmmddHHMMSSZ (15+1 chars) */
	strftime( gentimestr, 16, "%Y%m%d%H%M%SZ", &t );

	return( gentimestr );
}


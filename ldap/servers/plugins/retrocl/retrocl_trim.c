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
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif



#include "retrocl.h"

typedef struct _trim_status {
    time_t		ts_c_max_age;		/* Constraint  - max age of a changelog entry */
    time_t		ts_s_last_trim;		/* Status - last time we trimmed */
    int			ts_s_initialized;	/* Status - non-zero if initialized */
    int			ts_s_trimming;		/* non-zero if trimming in progress */
    PRLock		*ts_s_trim_mutex;	/* protects ts_s_trimming */
} trim_status;
static trim_status ts = {0L, 0L, 0, 0, NULL};

/*
 * All standard changeLogEntry attributes (initialized in get_cleattrs)
 */
static const char *cleattrs[ 10 ] = { NULL, NULL, NULL, NULL, NULL, NULL,
				      NULL, NULL, NULL };

static int retrocl_trimming = 0;
static Slapi_Eq_Context retrocl_trim_ctx = NULL;

/*
 * Function: get_cleattrs
 *
 * Returns: an array of pointers to attribute names.
 *
 * Arguments: None.
 *
 * Description: Initializes, if necessary, and returns an array of char *s
 *              with attribute names used for retrieving changeLogEntry
 *              entries from the directory.
 */
static const char **get_cleattrs(void)
{
    if ( cleattrs[ 0 ] == NULL ) {
        cleattrs[ 0 ] = attr_objectclass;
        cleattrs[ 1 ] = attr_changenumber;
        cleattrs[ 2 ] = attr_targetdn;
        cleattrs[ 3 ] = attr_changetype;
	cleattrs[ 4 ] = attr_newrdn;
	cleattrs[ 5 ] = attr_deleteoldrdn;
	cleattrs[ 6 ] = attr_changes;
	cleattrs[ 7 ] = attr_newsuperior;
	cleattrs[ 8 ] = attr_changetime;
	cleattrs[ 9 ] = NULL;
    }
    return cleattrs;
}

/*
 * Function: delete_changerecord
 *
 * Returns: LDAP_ error code
 * 
 * Arguments: the number of the change to delete
 *
 * Description:
 *
 */

static int
delete_changerecord( changeNumber cnum )
{
    Slapi_PBlock *pb;
    char	*dnbuf;
    int		delrc;

    dnbuf = slapi_ch_smprintf("%s=%ld, %s", attr_changenumber, cnum, 
	     RETROCL_CHANGELOG_DN);
    pb = slapi_pblock_new ();
    slapi_delete_internal_set_pb ( pb, dnbuf, NULL /*controls*/, NULL /* uniqueid */,
								   g_plg_identity[PLUGIN_RETROCL], 0 /* actions */ );
    slapi_delete_internal_pb (pb);
    slapi_pblock_get( pb, SLAPI_PLUGIN_INTOP_RESULT, &delrc );
    slapi_pblock_destroy( pb );
    
    if ( delrc != LDAP_SUCCESS ) {
	slapi_log_error( SLAPI_LOG_FATAL, RETROCL_PLUGIN_NAME, "delete_changerecord: could not delete "
		"change record %lu\n", cnum );
    } else {
	slapi_log_error( SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
		"delete_changerecord: deleted changelog entry \"%s\"\n", dnbuf);
    }
    slapi_ch_free((void **) &dnbuf );
    return delrc;
}

/*
 * Function: handle_getchangerecord_result
 * Arguments: op - pointer to Operation struct for this operation
 *            err - error code returned from search
 * Returns: nothing
 * Description: result handler for get_changerecord().  Sets the crt_err
 *              field of the cnum_result_t struct to the error returned
 *              from the backend.
 */
static void
handle_getchangerecord_result( int err, void *callback_data )
{
    cnum_result_t *crt = callback_data;

    if ( crt == NULL ) {
	slapi_log_error( SLAPI_LOG_FATAL, RETROCL_PLUGIN_NAME,
		"handle_getchangerecord_result: callback_data NULL\n" );
    } else {
	crt->crt_err = err;
    }
}

/*
 * Function: handle_getchangerecord_search
 * Arguments: op - pointer to Operation struct for this operation
 *            e - entry returned by backend
 * Returns: 0 in all cases
 * Description: Search result operation handler for get_changerecord().
 *              Sets fields in the cnum_result_t struct pointed to by
 *              op->o_handler_data.
 */
static int
handle_getchangerecord_search( Slapi_Entry *e, void *callback_data)
{
    cnum_result_t *crt = callback_data;

    if ( crt == NULL ) {
	slapi_log_error( SLAPI_LOG_FATAL, RETROCL_PLUGIN_NAME,
		"handle_getchangerecord_search: op->o_handler_data NULL\n" );
    } else if ( crt->crt_nentries > 0 ) {
	/* only return the first entry, I guess */
	slapi_log_error( SLAPI_LOG_FATAL, RETROCL_PLUGIN_NAME,
		"handle_getchangerecord_search: multiple entries returned\n" );
    } else {
	crt->crt_nentries++;
	crt->crt_entry = e;
    }

    return 0;
}


/*
 * Function: get_changerecord
 * Arguments: cnum - number of change record to retrieve
 * Returns: Pointer to an entry structure.  The caller must free the entry.
 *          If "err" is non-NULL, an error code is returned in the memory
 *          location it points to.
 * Description: Retrieve the change record entry whose number is "cnum".
 */
static Slapi_Entry *get_changerecord( changeNumber cnum, int *err )
{
    cnum_result_t	crt, *crtp = &crt;
    char		fstr[ 16 + CNUMSTR_LEN + 2 ];
    Slapi_PBlock *pb;

    if ( cnum == 0UL ) {
	if ( err != NULL ) {
	    *err = LDAP_PARAM_ERROR;
	}
	return NULL;
    }
    crtp->crt_nentries = crtp->crt_err = 0; crtp->crt_entry = NULL;
    PR_snprintf( fstr, sizeof(fstr), "%s=%ld", attr_changenumber, cnum );
    
    pb = slapi_pblock_new ();
    slapi_search_internal_set_pb (pb, RETROCL_CHANGELOG_DN, 
				  LDAP_SCOPE_SUBTREE, fstr,
				  (char **)get_cleattrs(),  /* cast const */
				  0 /* attrsonly */,
				  NULL /* controls */, NULL /* uniqueid */,
				  g_plg_identity[PLUGIN_RETROCL], 
				  0 /* actions */);

    slapi_search_internal_callback_pb (pb, crtp, 
				       handle_getchangerecord_result, 
				       handle_getchangerecord_search, NULL );
    if ( err != NULL ) {
	*err = crtp->crt_err;
    }

    slapi_pblock_destroy (pb);

    return( crtp->crt_entry );
}

/*
 * Function: trim_changelog
 *
 * Arguments: none
 *
 * Returns: 0 on success, -1 on failure
 *
 * Description: Trims the changelog, according to the constraints
 * described by the ts structure.
 */
static int trim_changelog(void)
{
    int			rc = 0, ldrc, done;
    time_t		now;
    changeNumber	first_in_log = 0, last_in_log = 0;
    Slapi_Entry		*e = NULL;
    int			num_deleted = 0;
    int me,lt;
    

    now = current_time();

    PR_Lock( ts.ts_s_trim_mutex );
    me = ts.ts_c_max_age;
    lt = ts.ts_s_last_trim;
    PR_Unlock( ts.ts_s_trim_mutex );

    if ( now - lt >= (CHANGELOGDB_TRIM_INTERVAL / 1000) ) {

	/*
	 * Trim the changelog.  Read sequentially through all the
	 * entries, deleting any which do not meet the criteria
	 * described in the ts structure.
	 */
	done = 0;

	while ( !done && retrocl_trimming == 1 ) {
	    int		did_delete;
	    Slapi_Attr	*attr;

	    did_delete = 0;
	    first_in_log = retrocl_get_first_changenumber();
	    if ( 0UL == first_in_log ) {
	        slapi_log_error( SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME, 
				 "trim_changelog: no changelog records "
				 "to trim\n" );
		/* Bail out - we can't do any useful work */
		break;
	    }

	    last_in_log = retrocl_get_last_changenumber();
	    if ( last_in_log == first_in_log ) {
		/* Always leave at least one entry in the change log */
		break;
	    }
	    if ( me > 0L ) {
	        e = get_changerecord( first_in_log, &ldrc );
		if ( NULL != e ) {
		    Slapi_Value *sval = NULL;
		    const struct berval *val = NULL;
		    rc = slapi_entry_attr_find( e, attr_changetime, &attr );
		    /* Bug 624442: Logic checking for lack of timestamp was
		       reversed. */
		    if ( 0 != rc  || slapi_attr_first_value( attr,&sval ) == -1 ||
			    (val = slapi_value_get_berval ( sval )) == NULL ||
				NULL == val->bv_val ) {
			/* What to do if there's no timestamp? Just delete it. */
		      retrocl_set_first_changenumber( first_in_log + 1 );
		      ldrc = delete_changerecord( first_in_log );
		      num_deleted++;
		      did_delete = 1;
		    } else {
			time_t change_time = parse_localTime( val->bv_val );
			if ( change_time + me < now ) {
			    retrocl_set_first_changenumber( first_in_log + 1 );
			    ldrc = delete_changerecord( first_in_log );
			    num_deleted++;
			    did_delete = 1;
			}
		    /* slapi_entry_free( e ); */ /* XXXggood should we be freeing this? */
		    }
		}
	    }
	    if ( !did_delete ) {
		done = 1;
	    }
	}
    } else {
       LDAPDebug(LDAP_DEBUG_PLUGIN, "not yet time to trim: %ld < (%d+%d)\n",
		 now,lt,(CHANGELOGDB_TRIM_INTERVAL/1000));
    }
    PR_Lock( ts.ts_s_trim_mutex );
    ts.ts_s_trimming = 0;
    ts.ts_s_last_trim = now;
    PR_Unlock( ts.ts_s_trim_mutex );
    if ( num_deleted > 0 ) {
	slapi_log_error( SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME, 
			 "trim_changelog: removed %d change records\n",
			 num_deleted );
    }
    return rc;
}

static int retrocl_active_threads;

/*
 * Function: changelog_trim_thread_fn
 *
 * Returns: nothing
 * 
 * Arguments: none
 *
 * Description: the thread creation callback.  retrocl_active_threads is 
 * provided for debugging purposes.
 *
 */
 
static void
changelog_trim_thread_fn( void *arg )
{
    PR_AtomicIncrement(&retrocl_active_threads);
    trim_changelog();
    PR_AtomicDecrement(&retrocl_active_threads);
}



/*
 * Function: retrocl_housekeeping
 * Arguments: cur_time - the current time
 * Returns: nothing
 * Description: Determines if it is time to trim the changelog database,
 *              and if so, determines if the changelog database needs to
 *              be trimmed.  If so, a thread is started which will trim
 *              the database.  
 */

void retrocl_housekeeping ( time_t cur_time, void *noarg )
{
    int			ldrc;

    if (retrocl_be_changelog == NULL) {
        LDAPDebug0Args(LDAP_DEBUG_TRACE,"not housekeeping if no cl be\n");
	return;
    }

    if ( !ts.ts_s_initialized ) {
	slapi_log_error( SLAPI_LOG_FATAL, RETROCL_PLUGIN_NAME, "changelog_housekeeping called before "
		"trimming constraints set\n" );
	return;
    }

    PR_Lock( ts.ts_s_trim_mutex );
    if ( !ts.ts_s_trimming ) {
	int	must_trim = 0;
	/* See if we need to trim */
	/* Has enough time elapsed since our last check? */
	if ( cur_time - ts.ts_s_last_trim >= (ts.ts_c_max_age) ) {
		/* Is the first entry too old? */
		time_t first_time;
		/*
		 * good we could avoid going to the database to retrieve
		 * this time information if we cached the last value we'd read.
		 * But a client might have deleted it over protocol.
		 */
		first_time = retrocl_getchangetime( SLAPI_SEQ_FIRST, &ldrc );
		LDAPDebug(LDAP_DEBUG_PLUGIN,
			  "cltrim: ldrc=%d, first_time=%ld, cur_time=%ld\n",
			  ldrc,first_time,cur_time);
		if ( LDAP_SUCCESS == ldrc && first_time > (time_t) 0L &&
		     first_time + ts.ts_c_max_age < cur_time ) {
		    must_trim = 1;
		}
	}
	if ( must_trim ) {
	    LDAPDebug0Args(LDAP_DEBUG_TRACE,"changelog about to create thread\n");
	    /* Start a thread to trim the changelog */
	    ts.ts_s_trimming = 1;
	    if ( PR_CreateThread( PR_USER_THREAD,
		    changelog_trim_thread_fn, NULL,
		    PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD,
		    RETROCL_DLL_DEFAULT_THREAD_STACKSIZE ) == NULL ) {
		slapi_log_error( SLAPI_LOG_FATAL, RETROCL_PLUGIN_NAME, "unable to create changelog trimming thread\n" );
	    }
	} else {
	    LDAPDebug0Args(LDAP_DEBUG_PLUGIN,
		      "changelog does not need to be trimmed\n");
	}
    }
    PR_Unlock( ts.ts_s_trim_mutex );
}


/*
 * Function: age_str2time
 *
 * Returns: time_t
 * 
 * Arguments: string representation of age (digits and unit s,m,h,d or w)
 *
 * Description:
 * convert time from string like 1h (1 hour) to corresponding time in seconds
 *
 */

static time_t
age_str2time (const char *age)
{
    char *maxage;
    char unit;
    time_t ageval;
    
    if (age == NULL || age[0] == '\0' || strcmp (age, "0") == 0) {
	return 0; 
    }
    
    maxage = slapi_ch_strdup ( age );
    unit = maxage[ strlen( maxage ) - 1 ];
    maxage[ strlen( maxage ) - 1 ] = '\0';
    ageval = strntoul( maxage, strlen( maxage ), 10 );
    if ( maxage) {
        slapi_ch_free ( (void **) &maxage );
    }
    switch ( unit ) {
    case 's':
      break;
    case 'm':
      ageval *= 60;
      break;
    case 'h':
      ageval *= ( 60 * 60 );
      break;
    case 'd':
      ageval *= ( 24 * 60 * 60 );
      break;
    case 'w':
      ageval *= ( 7 * 24 * 60 * 60 );
      break;
    default:
      slapi_log_error( SLAPI_LOG_PLUGIN, "retrocl",
		       "age_str2time: unknown unit \"%c\" "
		       "for maxiumum changelog age\n", unit );
      ageval = -1;
    }
    
    return ageval;
}

/*
 * Function: retrocl_init_trimming
 *
 * Returns: none, exits on fatal error
 * 
 * Arguments: none
 *
 * Description: called during startup
 *
 */

void retrocl_init_trimming (void)
{
    const char *cl_maxage;
    time_t ageval;
    
    cl_maxage = retrocl_get_config_str(CONFIG_CHANGELOG_MAXAGE_ATTRIBUTE);
    
    if (cl_maxage == NULL) {
      LDAPDebug0Args(LDAP_DEBUG_TRACE,"No maxage, not trimming retro changelog.\n");
      return;
    }
    ageval = age_str2time (cl_maxage);
    slapi_ch_free ((void **)&cl_maxage);
    
    ts.ts_c_max_age = ageval;
    ts.ts_s_last_trim = (time_t) 0L;
    ts.ts_s_trimming = 0;
    if (( ts.ts_s_trim_mutex = PR_NewLock()) == NULL ) {
	slapi_log_error( SLAPI_LOG_FATAL, RETROCL_PLUGIN_NAME, "set_changelog_trim_constraints: "
		"cannot create new lock.\n" );
	exit( 1 );
    }
    ts.ts_s_initialized = 1;
    retrocl_trimming = 1;
    
    retrocl_trim_ctx = slapi_eq_repeat(retrocl_housekeeping,
				       NULL,(time_t)0,
				       CHANGELOGDB_TRIM_INTERVAL * 1000);

}

/*
 * Function: retrocl_stop_trimming
 *
 * Returns: none
 * 
 * Arguments: none
 *
 * Description: called when server is shutting down to ensure trimming stops
 * eventually.
 *
 */

void retrocl_stop_trimming(void)
{
    retrocl_trimming = 0;
    if (retrocl_trim_ctx) {
      slapi_eq_cancel(retrocl_trim_ctx);
      retrocl_trim_ctx = NULL;
    }
}


/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* resourcelimit.c - binder-based resource limits implementation */


/*
 * Implementation notes:
 *
 * At present this code only provides support for integer-based
 * resource limits.
 *
 * When a successful bind occurs (i.e., when bind_credentials_set() is
 * called), reslimit_update_from_dn() or reslimit_update_from_entry()
 * must be called.  These functions look in the binder entry and pull
 * out attribute values that correspond to resource limits.  Typically
 * operational attributes are used, e.g., nsSizeLimit to hold a
 * binder-specific search size limit.  The attributes should be single
 * valued; if not, this code ignores all but the first value it finds.
 * The virtual attribute interface is used to retrieve the binder entry
 * values, so they can be based on COS, etc.
 *
 * Any resource limits found in the binder entry are cached in the
 * connection structure.  A connection object extension is used for this
 * purpose.  This means that if the attributes that correspond to binder
 * entry are changed the resource limit won't be affected until the next
 * bind occurs as that entry.  The data in the connection extension is
 * protected using a single writer/multiple reader locking scheme.
 *
 * A plugin or server subsystem that wants to use the resource limit
 * subsystem should call slapi_reslimit_register() once for each limit it
 * wants tracked.  Note that slapi_reslimit_register() should be called
 * early, i.e., before any client connections are accepted.
 * slapi_reslimit_register() gives back an integer handle that is used
 * later to refer to the limit in question.  Here's a sample call:
 */
#if SLAPI_RESLIMIT_SAMPLE_CODE
	static int  sizelimit_reslimit_handle = -1;

	if ( slapi_reslimit_register( SLAPI_RESLIMIT_TYPE_INT, "nsSizeLimit",
			&sizelimit_reslimit_handle ) != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
		/* limit could not be registered -- fatal error? */
	}
 	...
#endif

/*
 * A successful call to slapi_reslimit_register() results in a new
 * entry in the reslimit_map, which is private to this source file.
 * The map data structure is protected using a single writer/multiple
 * reader locking scheme.
 *
 * To retrieve a binder-based limit, simple call
 * slapi_reslimit_get_integer_limit().  If a value was present in the
 * binder entry, it will be given back to the caller and
 * SLAPI_RESLIMIT_STATUS_SUCCESS will be returned.  If no value was
 * present or the connection is NULL, SLAPI_RESLIMIT_STATUS_NOVALUE is
 * returned.  Other errors may be returned also.  Here's a sample call:
 */
#if SLAPI_RESLIMIT_SAMPLE_CODE
	int	rc, sizelimit;

	rc = slapi_reslimit_get_integer_limit( conn, sizelimit_reslimit_handle,
			&sizelimit );

	switch( rc ) {
	case SLAPI_RESLIMIT_STATUS_SUCCESS:		/* got a value */
		break;
	case SLAPI_RESLIMIT_STATUS_NOVALUE:		/* no limit value available */
		sizelimit = 500;	/* use a default value */
		break;
	default:								/* some other error occurred */
		sizelimit = 500;	/* use a default value */
	}
#endif

/*
 * The function reslimit_cleanup() is called from main() to dispose of
 * memory, locks, etc. so tools like Purify() don't report leaks at exit.
 */
/* End of implementation notes */

#include "slap.h"


/*
 * Macros.
 */
#define SLAPI_RESLIMIT_MODULE		"binder-based resource limits"
/* #define SLAPI_RESLIMIT_DEBUG */	/* define this to enable extra logging */
									/* also forces trace log messages to */
									/* always be logged */

#ifdef SLAPI_RESLIMIT_DEBUG
#define SLAPI_RESLIMIT_TRACELEVEL	LDAP_DEBUG_ANY
#else /* SLAPI_RESLIMIT_DEBUG */
#define SLAPI_RESLIMIT_TRACELEVEL	LDAP_DEBUG_TRACE
#endif /* SLAPI_RESLIMIT_DEBUG */


/*
 * Structures and types.
 */
/* Per-connection resource limits data */
typedef struct slapi_reslimit_conndata {
	PRRWLock	*rlcd_rwlock;			 /* to serialize access to the rest */
	int			rlcd_integer_count;		 /* size of rlcd_integer_limit array */
	PRBool		*rlcd_integer_available; /* array that says whether each */
										 /*           value is available */
	int			*rlcd_integer_value;	 /* array that holds limit values */
} SLAPIResLimitConnData;

/* Mapping between attribute and limit */
typedef struct slapi_reslimit_map {
	int		rlmap_type;				/* always SLAPI_RESLIMIT_TYPE_INT for now */
	char	*rlmap_at;				/* attribute type name */
} SLAPIResLimitMap;


/*
 * Static variables (module globals).
 */
static int							reslimit_inited = 0;
static int							reslimit_connext_objtype = 0;
static int							reslimit_connext_handle = 0;
static struct slapi_reslimit_map	*reslimit_map = NULL;
static int							reslimit_map_count = 0;
static struct slapi_componentid 				*reslimit_componentid=NULL;

/*
 * reslimit_map_rwlock is used to serialize access to
 * reslimit_map and reslimit_map_count
 */
static PRRWLock   					*reslimit_map_rwlock = NULL;

/*
 * Static functions.
 */
static int reslimit_init( void );
static void *reslimit_connext_constructor( void *object, void *parent );
static void reslimit_connext_destructor( void *extension, void *object,
		void *parent );
static int reslimit_get_ext( Slapi_Connection *conn, const char *logname,
		SLAPIResLimitConnData **rlcdpp );
static int reslimit_bv2int( const struct berval *bvp );
static char ** reslimit_get_registered_attributes();


/*
 * reslimit_init() must be called before any resource related work
 * is done.  It is safe to call this more than once, but reslimit_inited
 * can be tested to avoid a call.
 *
 * Returns zero if all goes well and non-zero if not.
 */
static int
reslimit_init( void )
{
	if ( reslimit_inited == 0 ) {
		if ( slapi_register_object_extension( SLAPI_RESLIMIT_MODULE,
				SLAPI_EXT_CONNECTION, reslimit_connext_constructor,
				reslimit_connext_destructor,
				&reslimit_connext_objtype, &reslimit_connext_handle )
				!= 0 ) {
			slapi_log_error( SLAPI_LOG_FATAL, SLAPI_RESLIMIT_MODULE,
					"reslimit_init: slapi_register_object_extension()"
					" failed\n" );
			return( -1 );
		}

		if (( reslimit_map_rwlock = PR_NewRWLock( PR_RWLOCK_RANK_NONE,
					"resourcelimit map rwlock" )) == NULL ) {
			slapi_log_error( SLAPI_LOG_FATAL, SLAPI_RESLIMIT_MODULE,
					"reslimit_init: PR_NewRWLock() failed\n" );
			return( -1 );
		}

		reslimit_inited = 1;
	}

	reslimit_componentid=generate_componentid(NULL,COMPONENT_RESLIMIT);

	return( 0 );
}




/*
 * Dispose of any allocated memory, locks, other resources.  Called when
 * server is shutting down.
 */
void
reslimit_cleanup( void )
{
	int			i;

	if ( reslimit_map != NULL ) {
		for ( i = 0; i < reslimit_map_count; ++i ) {
			if ( reslimit_map[ i ].rlmap_at != NULL ) {
				slapi_ch_free( (void **)&reslimit_map[ i ].rlmap_at );
			}
		}
		slapi_ch_free( (void **)&reslimit_map );
	}

	if ( reslimit_map_rwlock != NULL ) {
		PR_DestroyRWLock( reslimit_map_rwlock );
	}

	if ( reslimit_componentid != NULL ) {
		release_componentid( reslimit_componentid );
	}
}


/*
 * constructor for the connection object extension.
 */
static void *
reslimit_connext_constructor( void *object, void *parent )
{
	SLAPIResLimitConnData	*rlcdp;
	PRRWLock				*rwlock;

	if (( rwlock = PR_NewRWLock( PR_RWLOCK_RANK_NONE,
			"resource limit connection data rwlock" )) == NULL ) {
		slapi_log_error( SLAPI_LOG_FATAL, SLAPI_RESLIMIT_MODULE,
				"reslimit_connext_constructor: PR_NewRWLock() failed\n" );
		return( NULL );
	}

	rlcdp = (SLAPIResLimitConnData *)slapi_ch_calloc( 1,
			sizeof( SLAPIResLimitConnData ));
	rlcdp->rlcd_rwlock = rwlock;

	return( rlcdp );
}


/*
 * destructor for the connection object extension.
 */
static void
reslimit_connext_destructor( void *extension, void *object, void *parent )
{
	SLAPIResLimitConnData	*rlcdp = (SLAPIResLimitConnData *)extension;

	if ( rlcdp->rlcd_integer_available != NULL ) {
			slapi_ch_free( (void **)&rlcdp->rlcd_integer_available );
	}
	if ( rlcdp->rlcd_integer_value != NULL ) {
			slapi_ch_free( (void **)&rlcdp->rlcd_integer_value );
	}
	PR_DestroyRWLock( rlcdp->rlcd_rwlock );
	slapi_ch_free( (void **)&rlcdp );
}


/*
 * utility function to retrieve the connection object extension.
 *
 * if logname is non-NULL, errors are logged.
 */
static int
reslimit_get_ext( Slapi_Connection *conn, const char *logname,
		SLAPIResLimitConnData **rlcdpp )
{
	if ( !reslimit_inited && reslimit_init() != 0 ) {
		if ( NULL != logname ) {
			slapi_log_error( SLAPI_LOG_FATAL, SLAPI_RESLIMIT_MODULE,
				"%s: reslimit_init() failed\n", logname );
		}
		return( SLAPI_RESLIMIT_STATUS_INIT_FAILURE );
	}

	if (( *rlcdpp = (SLAPIResLimitConnData *)slapi_get_object_extension(
			reslimit_connext_objtype, conn,
			reslimit_connext_handle )) == NULL ) {
		if ( NULL != logname ) {
			slapi_log_error( SLAPI_LOG_FATAL, SLAPI_RESLIMIT_MODULE,
					"%s: slapi_get_object_extension() returned NULL\n", logname );
		}
		return( SLAPI_RESLIMIT_STATUS_INTERNAL_ERROR );
	}

	return( SLAPI_RESLIMIT_STATUS_SUCCESS );
}


/*
 * utility function to convert a string-represented integer to an int.
 *
 * XXXmcs: wouldn't need this if slapi_value_get_int() returned a signed int!
 */
static int
reslimit_bv2int( const struct berval *bvp )
{
	int		rc = 0;
	char	smallbuf[ 25 ], *buf;

	if ( bvp != NULL ) {
		/* make a copy to ensure it is zero-terminated */
		if ( bvp->bv_len < sizeof( smallbuf )) {
			buf = smallbuf;
		} else {
			buf = slapi_ch_malloc( bvp->bv_len + 1 );
		}
		memcpy( buf, bvp->bv_val, bvp->bv_len);
		buf[ bvp->bv_len ] = '\0';

		rc = atoi( buf );

		if ( buf != smallbuf ) {
			slapi_ch_free( (void **)&smallbuf );
		}
	}

	return( rc );
}


/**** Semi-public functions start here ***********************************/
/*
 * These functions are exposed to other parts of the server only, i.e.,
 * they are NOT part of the official SLAPI API.
 */

/*
 * Set the resource limits associated with connection `conn' based on the
 * entry named by `dn'.  If `dn' is NULL, limits are returned to their
 * default state.
 *
 * A SLAPI_RESLIMIT_STATUS_... code is returned.
 */ 
int
reslimit_update_from_dn( Slapi_Connection *conn, Slapi_DN *dn )
{
	Slapi_Entry		*e;
	int				rc;

	e = NULL;
	if ( dn != NULL ) {

		char ** attrs = reslimit_get_registered_attributes();
		(void) slapi_search_internal_get_entry( dn, attrs, &e , reslimit_componentid);
		charray_free(attrs);
	}

	rc = reslimit_update_from_entry( conn, e );

	if ( NULL != e ) {
		slapi_entry_free( e );
	}

	return( rc );
}


/*
 * Set the resource limits associated with connection `conn' based on the
 * entry `e'.  If `e' is NULL, limits are returned to their default state.
 * If `conn' is NULL, nothing is done.
 *
 * A SLAPI_RESLIMIT_STATUS_... code is returned.
 */ 
int
reslimit_update_from_entry( Slapi_Connection *conn, Slapi_Entry *e )
{
	char					*fnname = "reslimit_update_from_entry()";
	char					*actual_type_name, *get_ext_logname;
	int						i, rc, type_name_disposition, free_flags;
	SLAPIResLimitConnData	*rlcdp;
	Slapi_ValueSet			*vs;

	LDAPDebug( SLAPI_RESLIMIT_TRACELEVEL, "=> %s conn=0x%x, entry=0x%x\n",
			fnname, conn, e );

	rc = SLAPI_RESLIMIT_STATUS_SUCCESS;		/* optimistic */

	/* if conn is NULL, there is nothing to be done */
	if ( conn == NULL ) {
		goto log_and_return;
	}

	
	if ( NULL == e ) {
		get_ext_logname = NULL; /* do not log errors if resetting limits */
	} else {
		get_ext_logname = fnname;
	}
	if (( rc = reslimit_get_ext( conn, get_ext_logname, &rlcdp )) !=
			SLAPI_RESLIMIT_STATUS_SUCCESS ) {
		goto log_and_return;
	}

	/* LOCK FOR READ -- map lock */
	PR_RWLock_Rlock( reslimit_map_rwlock );
	/* LOCK FOR WRITE -- conn. data lock */
	PR_RWLock_Wlock( rlcdp->rlcd_rwlock );

	if ( rlcdp->rlcd_integer_value == NULL ) {
		rlcdp->rlcd_integer_count = reslimit_map_count;
		if ( rlcdp->rlcd_integer_count > 0 ) {
			rlcdp->rlcd_integer_available = (PRBool *)slapi_ch_calloc(
					rlcdp->rlcd_integer_count, sizeof( PRBool ));
			rlcdp->rlcd_integer_value = (int *)slapi_ch_calloc(
					rlcdp->rlcd_integer_count, sizeof( int ));
		}
	}

	for ( i = 0; i < rlcdp->rlcd_integer_count; ++i ) {
		if ( reslimit_map[ i ].rlmap_type != SLAPI_RESLIMIT_TYPE_INT ) {
			continue;
		}

		LDAPDebug( SLAPI_RESLIMIT_TRACELEVEL,
				"%s: setting limit for handle %d (based on %s)\n",
				fnname, i, reslimit_map[ i ].rlmap_at );

		rlcdp->rlcd_integer_available[ i ] = PR_FALSE;

		if ( NULL != e && 0 == slapi_vattr_values_get( e,
				reslimit_map[ i ].rlmap_at, &vs, &type_name_disposition,
				&actual_type_name, 0, &free_flags )) {
			Slapi_Value			*v;
			int					index;
			const struct berval	*bvp;

			if (( index = slapi_valueset_first_value( vs, &v )) != -1 &&
					( bvp = slapi_value_get_berval( v )) != NULL ) {
				rlcdp->rlcd_integer_value[ i ] = reslimit_bv2int( bvp );
				rlcdp->rlcd_integer_available[ i ] = PR_TRUE;

				LDAPDebug( SLAPI_RESLIMIT_TRACELEVEL,
						"%s: set limit based on %s to %d\n",
						fnname, reslimit_map[ i ].rlmap_at,
						rlcdp->rlcd_integer_value[ i ] );

				if ( slapi_valueset_next_value( vs, index, &v ) != -1 ) {
					char ebuf[ BUFSIZ ];
					slapi_log_error( SLAPI_LOG_FATAL, SLAPI_RESLIMIT_MODULE,
							"%s: ignoring multiple values for %s in entry \n",
							fnname, reslimit_map[ i ].rlmap_at,
							escape_string( slapi_entry_get_dn_const( e ),
							ebuf ));
				}
			}

			slapi_vattr_values_free( &vs, &actual_type_name, free_flags);
		}
	}

	PR_RWLock_Unlock( rlcdp->rlcd_rwlock );
	/* UNLOCKED -- conn. data lock */
	PR_RWLock_Unlock( reslimit_map_rwlock );
	/* UNLOCKED -- map lock */

log_and_return:
	LDAPDebug( SLAPI_RESLIMIT_TRACELEVEL, "<= %s returning status %d\n",
			fnname, rc, 0 );

	return( rc );
}

/* return the list of registered attributes */

static char ** reslimit_get_registered_attributes() 
{

	int 		i;
	char 		**attrs=NULL;

    	/* LOCK FOR READ -- map lock */
        PR_RWLock_Rlock( reslimit_map_rwlock );

        for ( i = 0; i < reslimit_map_count; ++i ) {
                if ( reslimit_map[ i ].rlmap_at != NULL ) {
			charray_add(&attrs, slapi_ch_strdup(reslimit_map[ i ].rlmap_at));
                }
        }
 
        PR_RWLock_Unlock( reslimit_map_rwlock );

	return attrs;
}


/**** Public functions can be found below this point *********************/
/*
 * These functions are exposed to plugins, i.e., they are part of the 
 * official SLAPI API.
 */

/*
 * Register a new resource to be tracked.  `type' must be
 * SLAPI_RESLIMIT_TYPE_INT and `attrname' is an LDAP attribute type that
 * is consulted in the bound entry to determine the limit's value.
 *
 * A SLAPI_RESLIMIT_STATUS_... code is returned.  If it is ...SUCCESS, then
 * `*handlep' is set to an opaque integer value that should be used in
 * subsequent calls to slapi_reslimit_get_integer_limit().
 */
int
slapi_reslimit_register( int type, const char *attrname, int *handlep )
{
	char	*fnname = "slapi_reslimit_register()";
	int		i, rc;

	LDAPDebug( SLAPI_RESLIMIT_TRACELEVEL, "=> %s attrname=%s\n",
			fnname, attrname, 0 );

	rc = SLAPI_RESLIMIT_STATUS_SUCCESS;		/* optimistic */

	/* initialize if necessary */
	if ( !reslimit_inited && reslimit_init() != 0 ) {
		slapi_log_error( SLAPI_LOG_FATAL, SLAPI_RESLIMIT_MODULE,
				"%s: reslimit_init() failed\n", fnname );
		rc = SLAPI_RESLIMIT_STATUS_INIT_FAILURE;
		goto log_and_return;
	}

	/* sanity check parameters */
	if ( type != SLAPI_RESLIMIT_TYPE_INT || attrname == NULL
			|| handlep == NULL ) {
		slapi_log_error( SLAPI_LOG_FATAL, SLAPI_RESLIMIT_MODULE,
				"%s: parameter error\n", fnname );
		rc = SLAPI_RESLIMIT_STATUS_PARAM_ERROR;
		goto log_and_return;
	}

	/* LOCK FOR WRITE -- map lock */
	PR_RWLock_Wlock( reslimit_map_rwlock );

	/*
	 * check that attrname is not already registered
	 */
	for ( i = 0; i < reslimit_map_count; ++i ) {
		if ( 0 == slapi_attr_type_cmp( reslimit_map[ i ].rlmap_at,
				attrname, SLAPI_TYPE_CMP_EXACT )) {
			slapi_log_error( SLAPI_LOG_FATAL, SLAPI_RESLIMIT_MODULE,
					"%s: parameter error (%s already registered)\n",
					attrname, fnname );
			rc = SLAPI_RESLIMIT_STATUS_PARAM_ERROR;
			goto unlock_and_return;
		}
	}

	/*
     * expand the map array and add the new element
	 */
    reslimit_map = (SLAPIResLimitMap *)slapi_ch_realloc(
			(char *)reslimit_map,
			(1 + reslimit_map_count) * sizeof( SLAPIResLimitMap ));
	reslimit_map[ reslimit_map_count ].rlmap_type = type;
	reslimit_map[ reslimit_map_count ].rlmap_at
			= slapi_ch_strdup( attrname );
	*handlep = reslimit_map_count;
	++reslimit_map_count;

unlock_and_return:
	PR_RWLock_Unlock( reslimit_map_rwlock );
	/* UNLOCKED -- map lock */

log_and_return:
	LDAPDebug( SLAPI_RESLIMIT_TRACELEVEL,
			"<= %s returning status=%d, handle=%d\n", fnname, rc,
			(handlep == NULL) ? -1 : *handlep );

	return( rc );
}


/*
 * Retrieve the integer limit associated with connection `conn' for
 * the resource identified by `handle'.
 *
 * A SLAPI_RESLIMIT_STATUS_... code is returned:
 *
 *  SLAPI_RESLIMIT_STATUS_SUCCESS -- `*limitp' is set to the limit value.
 *  SLAPI_RESLIMIT_STATUS_NOVALUE -- no limit value is available (use default).
 *  Another SLAPI_RESLIMIT_STATUS_... code -- some more fatal error occurred.
 *
 * If `conn' is NULL, SLAPI_RESLIMIT_STATUS_NOVALUE is returned.
 */ 
int
slapi_reslimit_get_integer_limit( Slapi_Connection *conn, int handle,
		int *limitp )
{
	char					*fnname = "slapi_reslimit_get_integer_limit()";
	int						rc;
	SLAPIResLimitConnData	*rlcdp;

	LDAPDebug( SLAPI_RESLIMIT_TRACELEVEL, "=> %s conn=0x%x, handle=%d\n",
			fnname, conn, handle );

	rc = SLAPI_RESLIMIT_STATUS_SUCCESS;		/* optimistic */

	/* sanity check parameters */
	if ( limitp == NULL ) {
		slapi_log_error( SLAPI_LOG_FATAL, SLAPI_RESLIMIT_MODULE,
				"%s: parameter error\n", fnname );
		rc = SLAPI_RESLIMIT_STATUS_PARAM_ERROR;
		goto log_and_return;
	}

	if ( conn == NULL ) {
		rc = SLAPI_RESLIMIT_STATUS_NOVALUE;
		goto log_and_return;
	}

	if (( rc = reslimit_get_ext( conn, fnname, &rlcdp )) !=
			SLAPI_RESLIMIT_STATUS_SUCCESS ) {
		goto log_and_return;
	}
	if(rlcdp->rlcd_integer_count==0) { /* peek at it to avoid lock */
		rc = SLAPI_RESLIMIT_STATUS_NOVALUE;
	} else {
		PR_RWLock_Rlock( rlcdp->rlcd_rwlock );
		if(rlcdp->rlcd_integer_count==0) {
			rc = SLAPI_RESLIMIT_STATUS_NOVALUE;
		} else if ( handle < 0 || handle >= rlcdp->rlcd_integer_count ) {
			slapi_log_error( SLAPI_LOG_FATAL, SLAPI_RESLIMIT_MODULE,
				"%s: unknown handle %d\n", fnname, handle );
			rc = SLAPI_RESLIMIT_STATUS_UNKNOWN_HANDLE;
		} else if ( rlcdp->rlcd_integer_available[ handle ] ) {
			*limitp = rlcdp->rlcd_integer_value[ handle ];
		} else {
			rc = SLAPI_RESLIMIT_STATUS_NOVALUE;
		}
		PR_RWLock_Unlock( rlcdp->rlcd_rwlock );
	}


log_and_return:
	if ( LDAPDebugLevelIsSet( LDAP_DEBUG_TRACE )) {
		if ( rc == SLAPI_RESLIMIT_STATUS_SUCCESS ) {
			LDAPDebug( SLAPI_RESLIMIT_TRACELEVEL,
					"<= %s returning SUCCESS, value=%d\n", fnname, *limitp, 0 );
		} else if ( rc == SLAPI_RESLIMIT_STATUS_NOVALUE ) {
			LDAPDebug( SLAPI_RESLIMIT_TRACELEVEL, "<= %s returning NO VALUE\n",
					fnname, 0, 0 );
		} else {
			LDAPDebug( SLAPI_RESLIMIT_TRACELEVEL, "<= %s returning ERROR %d\n",
					fnname, rc, 0 );
		}
	}

	return( rc );
}
/**** Public functions END ***********************************************/

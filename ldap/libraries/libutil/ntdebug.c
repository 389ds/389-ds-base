/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/******************************************************
 *
 *  ntdebug.c - Sends debug output to window and stdout
 *			    on Win32 platforms.
 *
 ******************************************************/

#if defined( _WIN32 )
#include <windows.h>
#include <time.h>
#include <stdio.h>		
#if defined( SLAPD_LOGGING )
#include "slap.h"
#include "proto-slap.h"
#else
#include "ldap.h"
#include "ldaplog.h"
#endif
int slapd_ldap_debug = LDAP_DEBUG_ANY;
FILE *error_logfp = NULL;

void LDAPDebug( int level, char *fmt, ... )
{
	va_list arg_ptr;
	va_start( arg_ptr, fmt ); 
	if ( slapd_ldap_debug & level ) 
	{ 
		char szFormattedString[512]; 
		_vsnprintf( szFormattedString, sizeof( szFormattedString ), fmt, arg_ptr ); 

#if defined( LDAP_DEBUG )
		/* Send to debug window ...*/
		OutputDebugString( szFormattedString );

		/* ... and to stderr */
		fprintf( stderr, szFormattedString );
#endif
#if defined( SLAPD_LOGGING )
	    if ( error_logfp != NULL ) 
			slapd_log_error( error_logfp, szFormattedString ); 
#endif
	}  
	va_end( arg_ptr );

}
#endif

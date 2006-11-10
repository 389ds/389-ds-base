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
		PR_vsnprintf( szFormattedString, sizeof( szFormattedString ), fmt, arg_ptr ); 

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

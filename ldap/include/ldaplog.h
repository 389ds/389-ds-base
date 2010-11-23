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

#ifndef _LDAPLOG_H
#define _LDAPLOG_H

#ifdef __cplusplus
extern "C" {
#endif

#define LDAP_DEBUG_TRACE	0x00001		/*     1 */
#define LDAP_DEBUG_PACKETS	0x00002		/*     2 */
#define LDAP_DEBUG_ARGS		0x00004		/*     4 */
#define LDAP_DEBUG_CONNS	0x00008		/*     8 */
#define LDAP_DEBUG_BER		0x00010		/*    16 */
#define LDAP_DEBUG_FILTER	0x00020		/*    32 */
#define LDAP_DEBUG_CONFIG	0x00040		/*    64 */
#define LDAP_DEBUG_ACL		0x00080		/*   128 */
#define LDAP_DEBUG_STATS	0x00100		/*   256 */
#define LDAP_DEBUG_STATS2	0x00200		/*   512 */
#define LDAP_DEBUG_SHELL	0x00400		/*  1024 */
#define LDAP_DEBUG_PARSE	0x00800		/*  2048 */
#define LDAP_DEBUG_HOUSE        0x01000		/*  4096 */
#define LDAP_DEBUG_REPL         0x02000		/*  8192 */
#define LDAP_DEBUG_ANY          0x04000		/* 16384 */
#define LDAP_DEBUG_CACHE        0x08000		/* 32768 */
#define LDAP_DEBUG_PLUGIN	0x10000		/* 65536 */
#define LDAP_DEBUG_TIMING	0x20000		/*131072 */
#define LDAP_DEBUG_ACLSUMMARY	0x40000		/*262144 */
#define LDAP_DEBUG_BACKLDBM		0x80000		/*524288 */

#define LDAP_DEBUG_ALL_LEVELS	0xFFFFF

/* debugging stuff */
/* Disable by default */
#define LDAPDebug( level, fmt, arg1, arg2, arg3 )
#define LDAPDebugLevelIsSet( level ) (0)
#define LDAPDebug0Args( level, fmt )
#define LDAPDebug1Arg( level, fmt, arg )
#define LDAPDebug2Args( level, fmt, arg1, arg2 )

#ifdef LDAP_DEBUG
#  undef LDAPDebug
#  undef LDAPDebug0Args
#  undef LDAPDebug1Arg
#  undef LDAPDebug2Args
#  undef LDAPDebugLevelIsSet

#    ifdef _WIN32
#      ifndef DONT_DECLARE_SLAPD_LDAP_DEBUG /* see libglobs.c for info */
       extern __declspec(dllimport) int	slapd_ldap_debug;
#      endif /* DONT_DECLARE_SLAPD_LDAP_DEBUG */
#      define LDAPDebug( level, fmt, arg1, arg2, arg3 )	\
       { \
		if ( *module_ldap_debug & level ) { \
		        slapd_log_error_proc( NULL, fmt, arg1, arg2, arg3 ); \
	    } \
       }
#      define LDAPDebug0Args( level, fmt )	\
       { \
		if ( *module_ldap_debug & level ) { \
		        slapd_log_error_proc( NULL, fmt ); \
	    } \
       }
#      define LDAPDebug1Arg( level, fmt, arg )      \
       { \
		if ( *module_ldap_debug & level ) { \
		        slapd_log_error_proc( NULL, fmt, arg ); \
	    } \
       }
#      define LDAPDebug2Args( level, fmt, arg1, arg2 )    \
       { \
		if ( *module_ldap_debug & level ) { \
		        slapd_log_error_proc( NULL, fmt, arg1, arg2 ); \
	    } \
       }
#      define LDAPDebugLevelIsSet( level ) (0 != (*module_ldap_debug & level))
#    else /* Not _WIN32 */
       extern int	slapd_ldap_debug;
#      define LDAPDebug( level, fmt, arg1, arg2, arg3 )	\
       { \
		if ( slapd_ldap_debug & level ) { \
		        slapd_log_error_proc( NULL, fmt, arg1, arg2, arg3 ); \
	    } \
       }
#      define LDAPDebug0Args( level, fmt )	\
       { \
		if ( slapd_ldap_debug & level ) { \
		        slapd_log_error_proc( NULL, fmt ); \
	    } \
       }
#      define LDAPDebug1Arg( level, fmt, arg )      \
       { \
		if ( slapd_ldap_debug & level ) { \
		        slapd_log_error_proc( NULL, fmt, arg ); \
	    } \
       }
#      define LDAPDebug2Args( level, fmt, arg1, arg2 )    \
       { \
		if ( slapd_ldap_debug & level ) { \
		        slapd_log_error_proc( NULL, fmt, arg1, arg2 ); \
	    } \
       }
#      define LDAPDebugLevelIsSet( level ) (0 != (slapd_ldap_debug & level))
#    endif /* Win32 */
#endif /* LDAP_DEBUG */

#ifdef __cplusplus
}
#endif

#endif /* _LDAP_H */

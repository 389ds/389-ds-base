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

#define LDAP_DEBUG_TRACE      0x000001  /*       1 */
#define LDAP_DEBUG_PACKETS    0x000002  /*       2 */
#define LDAP_DEBUG_ARGS       0x000004  /*       4 */
#define LDAP_DEBUG_CONNS      0x000008  /*       8 */
#define LDAP_DEBUG_BER        0x000010  /*      16 */
#define LDAP_DEBUG_FILTER     0x000020  /*      32 */
#define LDAP_DEBUG_CONFIG     0x000040  /*      64 */
#define LDAP_DEBUG_ACL        0x000080  /*     128 */
#define LDAP_DEBUG_STATS      0x000100  /*     256 */
#define LDAP_DEBUG_STATS2     0x000200  /*     512 */
#define LDAP_DEBUG_SHELL      0x000400  /*    1024 */
#define LDAP_DEBUG_PARSE      0x000800  /*    2048 */
#define LDAP_DEBUG_HOUSE      0x001000  /*    4096 */
#define LDAP_DEBUG_REPL       0x002000  /*    8192 */
#define LDAP_DEBUG_ANY        0x004000  /*   16384 */
#define LDAP_DEBUG_CACHE      0x008000  /*   32768 */
#define LDAP_DEBUG_PLUGIN     0x010000  /*   65536 */
#define LDAP_DEBUG_TIMING     0x020000  /*  131072 */
#define LDAP_DEBUG_ACLSUMMARY 0x040000  /*  262144 */
#define LDAP_DEBUG_BACKLDBM   0x080000  /*  524288 */
#define LDAP_DEBUG_NUNCSTANS  0x100000  /* 1048576 */

#define LDAP_DEBUG_ALL_LEVELS	0xFFFFFF

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
#endif /* LDAP_DEBUG */

#ifdef __cplusplus
}
#endif

#endif /* _LDAP_H */

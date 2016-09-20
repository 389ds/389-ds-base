/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef _LDAPLOG_H
#define _LDAPLOG_H

#ifdef __cplusplus
extern "C" {
#endif

#define LDAP_DEBUG_TRACE      0x00000001  /*         1 */
#define LDAP_DEBUG_PACKETS    0x00000002  /*         2 */
#define LDAP_DEBUG_ARGS       0x00000004  /*         4 */
#define LDAP_DEBUG_CONNS      0x00000008  /*         8 */
#define LDAP_DEBUG_BER        0x00000010  /*        16 */
#define LDAP_DEBUG_FILTER     0x00000020  /*        32 */
#define LDAP_DEBUG_CONFIG     0x00000040  /*        64 */
#define LDAP_DEBUG_ACL        0x00000080  /*       128 */
#define LDAP_DEBUG_STATS      0x00000100  /*       256 */
#define LDAP_DEBUG_STATS2     0x00000200  /*       512 */
#define LDAP_DEBUG_SHELL      0x00000400  /*      1024 */
#define LDAP_DEBUG_PARSE      0x00000800  /*      2048 */
#define LDAP_DEBUG_HOUSE      0x00001000  /*      4096 */
#define LDAP_DEBUG_REPL       0x00002000  /*      8192 */
#define LDAP_DEBUG_ANY        0x00004000  /*     16384 */
#define LDAP_DEBUG_CACHE      0x00008000  /*     32768 */
#define LDAP_DEBUG_PLUGIN     0x00010000  /*     65536 */
#define LDAP_DEBUG_TIMING     0x00020000  /*    131072 */
#define LDAP_DEBUG_ACLSUMMARY 0x00040000  /*    262144 */
#define LDAP_DEBUG_BACKLDBM   0x00080000  /*    524288 */
#define LDAP_DEBUG_NUNCSTANS  0x00100000  /*   1048576 */
#define LDAP_DEBUG_EMERG      0x00200000  /*   2097152 */
#define LDAP_DEBUG_ALERT      0x00400000  /*   4194304 */
#define LDAP_DEBUG_CRIT       0x00800000  /*   8388608 */
#define LDAP_DEBUG_ERR        0x01000000  /*  16777216 */
#define LDAP_DEBUG_WARNING    0x02000000  /*  33554432 */
#define LDAP_DEBUG_NOTICE     0x04000000  /*  67108864 */
#define LDAP_DEBUG_INFO       0x08000000  /* 134217728 */
#define LDAP_DEBUG_DEBUG      0x10000000  /* 268435456 */

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
		        slapd_log_error_proc( level, NULL, fmt, arg1, arg2, arg3 ); \
	    } \
       }
#      define LDAPDebug0Args( level, fmt )	\
       { \
		if ( slapd_ldap_debug & level ) { \
		        slapd_log_error_proc( level, NULL, fmt ); \
	    } \
       }
#      define LDAPDebug1Arg( level, fmt, arg )      \
       { \
		if ( slapd_ldap_debug & level ) { \
		        slapd_log_error_proc( level, NULL, fmt, arg ); \
	    } \
       }
#      define LDAPDebug2Args( level, fmt, arg1, arg2 )    \
       { \
		if ( slapd_ldap_debug & level ) { \
		        slapd_log_error_proc( level, NULL, fmt, arg1, arg2 ); \
	    } \
       }
#      define LDAPDebugLevelIsSet( level ) (0 != (slapd_ldap_debug & level))
#endif /* LDAP_DEBUG */

#ifdef __cplusplus
}
#endif

#endif /* _LDAP_H */

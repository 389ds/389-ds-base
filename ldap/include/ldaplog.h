/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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

#define LDAP_DEBUG_ALL_LEVELS	0xFFFFF

/* debugging stuff */
/* Disable by default */
#define LDAPDebug( level, fmt, arg1, arg2, arg3 )
#define LDAPDebugLevelIsSet( level ) (0)

#ifdef LDAP_DEBUG
#  undef LDAPDebug
#  undef LDAPDebugLevelIsSet

/* SLAPD_LOGGING should not be on for WINSOCK (16-bit Windows) */
#  if defined(SLAPD_LOGGING)
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
#      define LDAPDebugLevelIsSet( level ) (0 != (*module_ldap_debug & level))
#    else /* Not _WIN32 */
       extern int	slapd_ldap_debug;
#      define LDAPDebug( level, fmt, arg1, arg2, arg3 )	\
       { \
		if ( slapd_ldap_debug & level ) { \
		        slapd_log_error_proc( NULL, fmt, arg1, arg2, arg3 ); \
	    } \
       }
#      define LDAPDebugLevelIsSet( level ) (0 != (slapd_ldap_debug & level))
#    endif /* Win32 */
#  else /* no SLAPD_LOGGING */
     extern void ber_err_print( char * );
     extern int	slapd_ldap_debug;
#    define LDAPDebug( level, fmt, arg1, arg2, arg3 ) \
		if ( slapd_ldap_debug & level ) { \
			char msg[256]; \
			sprintf( msg, fmt, arg1, arg2, arg3 ); \
			ber_err_print( msg ); \
		}
#    define LDAPDebugLevelIsSet( level )	(0 != (slapd_ldap_debug & level))
#  endif /* SLAPD_LOGGING */
#endif /* LDAP_DEBUG */

#ifdef __cplusplus
}
#endif

#endif /* _LDAP_H */

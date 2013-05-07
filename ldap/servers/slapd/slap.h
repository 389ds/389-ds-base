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
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * Contributors:
 *   Hewlett-Packard Development Company, L.P.
 *     Bugfix for bug #195302
 *
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* slap.h - stand alone ldap server include file */

#ifndef _SLDAPD_H_
#define _SLDAPD_H_

 
/* Used by SSL and DES plugin */
#ifdef NEED_TOK_DES
static char  tokDes[34] = "Communicator Generic Crypto Svcs";
static char ptokDes[34] = "Internal (Software) Token        ";
#endif

/*
 * The slapd executable can function in on of several modes.
 */
#define SLAPD_EXEMODE_UNKNOWN		0
#define SLAPD_EXEMODE_SLAPD		1
#define SLAPD_EXEMODE_DB2LDIF		2
#define SLAPD_EXEMODE_LDIF2DB		3
#define SLAPD_EXEMODE_DB2ARCHIVE	4
#define SLAPD_EXEMODE_ARCHIVE2DB	5
#define SLAPD_EXEMODE_DBTEST		6
#define SLAPD_EXEMODE_DB2INDEX		7
#define SLAPD_EXEMODE_REFERRAL		8
#define SLAPD_EXEMODE_SUFFIX2INSTANCE	9
#define SLAPD_EXEMODE_PRINTVERSION  10
#define SLAPD_EXEMODE_UPGRADEDB     11
#define SLAPD_EXEMODE_DBVERIFY      12
#define SLAPD_EXEMODE_UPGRADEDNFORMAT     13

#ifdef _WIN32
#ifndef DONT_DECLARE_SLAPD_LDAP_DEBUG
extern __declspec(dllimport) int	slapd_ldap_debug;	/* XXXmcs: should eliminate this */
#endif /* DONT_DECLARE_SLAPD_LDAP_DEBUG */
typedef char *caddr_t;
void *dlsym(void *a, char *b);
#define LOG_PID		0x01
#define LOG_NOWAIT	0x10
#define LOG_DEBUG	7
#define POLL_STRUCT PRPollDesc
#define POLL_FN PR_Poll
#define RLIM_TYPE  int
#else /* _WIN32 */
#define LDAP_SYSLOG
#include <syslog.h>
#define RLIM_TYPE int
#include <poll.h>
#define POLL_STRUCT PRPollDesc
#define POLL_FN PR_Poll
#endif /* _WIN32 */

#include <stdio.h>  /* for FILE */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#if defined(SOLARIS)
#include <limits.h> /* for LONG_MAX */

#endif

/* there's a bug in the dbm code we import (from where?) -- FIXME */
#ifdef LINUX
#define LITTLE_ENDIAN __LITTLE_ENDIAN
#endif
#include <cert.h> 

#ifndef _WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif /* _WIN32 */

#ifdef _WIN32
#define LDAP_IOCP
#endif

/* Required to get portable printf/scanf format macros */
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>

#else
#error Need to define portable format macros such as PRIu64
#endif /* HAVE_INTTYPES_H */

#define LOG_INTERNAL_OP_CON_ID      "Internal"
#define LOG_INTERNAL_OP_OP_ID       -1

#define MAX_SERVICE_NAME 25

#define SLAPD_TYPICAL_ATTRIBUTE_NAME_MAX_LENGTH 256

typedef struct symbol_t {
    const char* name;
    unsigned number;
} symbol_t;

#define SLAPD_SSLCLIENTAUTH_OFF      0
#define SLAPD_SSLCLIENTAUTH_ALLOWED  1 /* server asks for cert, but client need not send one */
#define SLAPD_SSLCLIENTAUTH_REQUIRED 2 /* server will refuse SSL session unless client sends cert */
#define SLAPD_SSLCLIENTAUTH_DEFAULT  SLAPD_SSLCLIENTAUTH_ALLOWED

#define NUM_SNMP_INT_TBL_ROWS 5
#define SNMP_FIELD_LENGTH 100

/* include NSPR header files */
#include "nspr.h"
#include "plhash.h"

/* include NSS header files */
#include "ssl.h"

#include <sys/types.h> /* this should be moved into avl.h */

#include "avl.h"
#include "ldap.h"
#include "ldaprot.h"
#include "ldif.h"
#include "ldaplog.h"
#include "portable.h"
#include "disconnect_errors.h"

#include "csngen.h"
#include "uuid.h"

#if defined(OS_solaris)
#  include <thread.h>
#  define GET_THREAD_ID() thr_self()
#else 
#  if defined(_WIN32)
#    define GET_THREAD_ID() GetCurrentThreadId()
#  else
#    include <pthread.h>
#    define GET_THREAD_ID() pthread_self()
#  endif
#endif

/*
 * XXXmcs: these are defined by ldap.h or ldap-extension.h,
 * but only in a newer release than we use with DS today.
 */
#ifndef LDAP_CONTROL_AUTH_RESPONSE
#define LDAP_CONTROL_AUTH_RESPONSE	"2.16.840.1.113730.3.4.15"
#endif
#ifndef LDAP_CONTROL_REAL_ATTRS_ONLY
#define LDAP_CONTROL_REAL_ATTRS_ONLY	"2.16.840.1.113730.3.4.17"
#endif
#ifndef LDAP_CONTROL_VIRT_ATTRS_ONLY
#define LDAP_CONTROL_VIRT_ATTRS_ONLY	"2.16.840.1.113730.3.4.19"
#endif
#ifndef LDAP_CONTROL_GET_EFFECTIVE_RIGHTS
#define LDAP_CONTROL_GET_EFFECTIVE_RIGHTS "1.3.6.1.4.1.42.2.27.9.5.2"
#endif

/* PAGED RESULTS control (shared by request and response) */
#ifndef LDAP_CONTROL_PAGEDRESULTS
#define LDAP_CONTROL_PAGEDRESULTS      "1.2.840.113556.1.4.319"
#endif

#define SLAPD_VENDOR_NAME	VENDOR
#define SLAPD_VERSION_STR	CAPBRAND "-Directory/" DS_PACKAGE_VERSION
#define SLAPD_SHORT_VERSION_STR	DS_PACKAGE_VERSION

typedef void	(*VFP)(void *);
typedef void	(*VFPP)(void **);
typedef void	(*VFP0)(void);
typedef void	(*VFPV)(); /* takes undefined arguments */
#define LDAPI_INTERNAL	1
#include "slapi-private.h"
#include "pw.h"

/*
 * call the appropriate signal() function.
 */
#if ( defined( hpux ) || defined ( irix ))
/* 
 * we should not mix POSIX signal library function (sigaction)
 * with SYSV's (sigset) on IRIX.  nspr uses POSIX internally.
 */
#define SIGNAL( s, a ) signal2sigaction( s, (void *) a )
#elif ( defined( SYSV ) || defined( aix ))
#define SIGNAL sigset
#else
#define SIGNAL signal
#endif

/*
 * SLAPD_PR_WOULD_BLOCK_ERROR() returns non-zero if prerrno is an NSPR
 *	error code that indicates a temporary non-blocking I/O error,
 *	e.g., PR_WOULD_BLOCK_ERROR.
 */
#define SLAPD_PR_WOULD_BLOCK_ERROR( prerrno )	\
	((prerrno) == PR_WOULD_BLOCK_ERROR || (prerrno) == PR_IO_TIMEOUT_ERROR)

/*
 * SLAPD_SYSTEM_WOULD_BLOCK_ERROR() returns non-zero if syserrno is an OS
 *	error code that indicates a temporary non-blocking I/O error,
 *	e.g., EAGAIN.
 */
#define SLAPD_SYSTEM_WOULD_BLOCK_ERROR( syserrno )	\
	((syserrno)==EAGAIN || (syserrno)==EWOULDBLOCK)


#define LDAP_ON		1
#define LDAP_OFF	0
#define LDAP_UNDEFINED (-1)

#ifndef SLAPD_INVALID_SOCKET
#ifdef _WIN32
#define SLAPD_INVALID_SOCKET	0
#else
#define SLAPD_INVALID_SOCKET	0
#endif
#endif

#define SLAPD_INVALID_SOCKET_INDEX	(-1)

#ifdef _WIN32
#define SLAPD_DEFAULT_FILE_MODE				S_IREAD | S_IWRITE
#define SLAPD_DEFAULT_DIR_MODE  			0
#else /* _WIN32 */
#define SLAPD_DEFAULT_FILE_MODE				S_IRUSR | S_IWUSR
#define SLAPD_DEFAULT_DIR_MODE				S_IRWXU
#endif

#define SLAPD_DEFAULT_IDLE_TIMEOUT			0		/* seconds - 0 == never */
#define SLAPD_DEFAULT_SIZELIMIT				2000	/* use -1 for no limit */
#define SLAPD_DEFAULT_TIMELIMIT				3600	/* use -1 for no limit */
#define SLAPD_DEFAULT_LOOKTHROUGHLIMIT		5000	/* use -1 for no limit */
#define SLAPD_DEFAULT_GROUPNESTLEVEL		5
#define SLAPD_DEFAULT_MAX_FILTER_NEST_LEVEL	40		/* use -1 for no limit */
#define SLAPD_DEFAULT_MAX_SASLIO_SIZE		2097152 /* 2MB in bytes.  Use -1 for no limit */
#define SLAPD_DEFAULT_IOBLOCK_TIMEOUT		1800000 /* half hour in ms */
#define SLAPD_DEFAULT_OUTBOUND_LDAP_IO_TIMEOUT	300000 /* 5 minutes in ms */
#define SLAPD_DEFAULT_RESERVE_FDS			64
#define SLAPD_DEFAULT_MAX_THREADS			30		/* connection pool threads */
#define SLAPD_DEFAULT_MAX_THREADS_PER_CONN	5		/* allowed per connection */
#define SLAPD_DEFAULT_SCHEMA_IGNORE_TRAILING_SPACES	LDAP_OFF
#define SLAPD_DEFAULT_LOCAL_SSF			71	/* assume local connections are secure */
#define SLAPD_DEFAULT_MIN_SSF			0	/* allow unsecured connections (no privacy or integrity) */

							/* We'd like this number to be prime for 
							    the hash into the Connection table */
#define SLAPD_DEFAULT_CONNTABLESIZE		4093	/* connection table size */

#define SLAPD_MONITOR_DN		"cn=monitor"
#define SLAPD_SCHEMA_DN			"cn=schema"
#define SLAPD_CONFIG_DN			"cn=config"

#define EGG_OBJECT_CLASS		"directory-team-extensible-object"
#define EGG_FILTER				"(objectclass=directory-team-extensible-object)"

#define BE_LIST_SIZE 1000 /* used by mapping tree code to hold be_list stuff */

#define	REPL_DBTYPE		"ldbm"
#define	REPL_DBTAG		"repl"

#define ATTR_NETSCAPEMDSUFFIX "netscapemdsuffix"

#define REFERRAL_REMOVE_CMD "remove"

/* Filenames for DSE storage */
#define	DSE_FILENAME	"dse.ldif"
#define	DSE_TMPFILE	"dse.ldif.tmp"
#define	DSE_BACKFILE	"dse.ldif.bak"
#define	DSE_STARTOKFILE	"dse.ldif.startOK"
#define DSE_LDBM_FILENAME "ldbm.ldif"
#define DSE_LDBM_TMPFILE "ldbm.ldif.tmp"
/* for now, we are using the dse file for the base config file */
#define CONFIG_FILENAME DSE_FILENAME
/* the default configuration sub directory of the instance directory */
#define CONFIG_SUBDIR_NAME "config"
/* the default schema sub directory of the config sub directory */
#define SCHEMA_SUBDIR_NAME "schema"

/* LDAPI default configuration */
#define SLAPD_LDAPI_DEFAULT_FILENAME "/var/run/ldapi"
#define SLAPD_LDAPI_DEFAULT_STATUS "off"

/* Anonymous access */
#define SLAPD_ANON_ACCESS_OFF           0
#define SLAPD_ANON_ACCESS_ON            1
#define SLAPD_ANON_ACCESS_ROOTDSE       2

/* Server certificate validation */
#define SLAPD_VALIDATE_CERT_OFF         0
#define SLAPD_VALIDATE_CERT_ON          1
#define SLAPD_VALIDATE_CERT_WARN        2

/* if the use of atomic get/set for config parameters is enabled, enable the use for the specific parameters defined below */
#define USE_ATOMIC_GETSET 1
#ifdef USE_ATOMIC_GETSET
#define ATOMIC_GETSET_MAXTHREADSPERCONN 1
#define ATOMIC_GETSET_IOBLOCKTIMEOUT 1
#define ATOMIC_GETSET_FILTER_NEST_LEVEL 1
#define ATOMIC_GETSET_ONOFF 1
typedef PRInt32 slapi_onoff_t;
typedef PRInt32 slapi_int_t;
#else
typedef int slapi_onoff_t;
typedef int slapi_int_t;
#endif

struct subfilt {
	char	*sf_type;
	char	*sf_initial;
	char	**sf_any;
	char	*sf_final;
	void	*sf_private;	/* data private to syntax handler */
};

#include "filter.h" /* mr_filter_t */

/*
 * represents a search filter
 */
struct slapi_filter {
	int f_flags;
	unsigned long	f_choice;	/* values taken from ldap.h */
	PRUint32	f_hash;		/* for quick comparisons */
	void *assigned_decoder;

	union {
		/* present */
		char		*f_un_type;

		/* equality, lessorequal, greaterorequal, approx */
		struct ava	f_un_ava;

		/* and, or, not */
		struct slapi_filter	*f_un_complex;

		/* substrings */
		struct subfilt	f_un_sub;

		/* extended -- v3 only */
		mr_filter_t	f_un_extended;
	} f_un;
#define f_type		f_un.f_un_type
#define f_ava		f_un.f_un_ava
#define f_avtype	f_un.f_un_ava.ava_type
#define f_avvalue	f_un.f_un_ava.ava_value
#define f_and		f_un.f_un_complex
#define f_or		f_un.f_un_complex
#define f_not		f_un.f_un_complex
#define f_list		f_un.f_un_complex
#define f_sub		f_un.f_un_sub
#define f_sub_type	f_un.f_un_sub.sf_type
#define f_sub_initial	f_un.f_un_sub.sf_initial
#define f_sub_any	f_un.f_un_sub.sf_any
#define f_sub_final	f_un.f_un_sub.sf_final
#define f_mr		f_un.f_un_extended
#define f_mr_oid	f_un.f_un_extended.mrf_oid
#define f_mr_type	f_un.f_un_extended.mrf_type
#define f_mr_value	f_un.f_un_extended.mrf_value
#define f_mr_dnAttrs	f_un.f_un_extended.mrf_dnAttrs

	struct slapi_filter	*f_next;
};

struct csn
{
    time_t tstamp;
    PRUint16 seqnum;
    ReplicaId rid;
	PRUint16 subseqnum;
};

struct csnset_node
{
	CSNType type;
	CSN csn;
	CSNSet *next;
};

struct slapi_value
{
    struct berval bv;
    CSNSet *v_csnset;
    unsigned long v_flags;
};

/*
 * JCM: This structure, slapi_value_set, seems useless,
 * but in the future we could:
 *
 *	{
 *	unsigned char flag;
 *	union single
 *	{
 *		struct slapi_value *va;
 *	};
 *	union multiple_array
 *	{
 *		short num;
 *		short max;
 *		struct slapi_value **va;
 *	};
 *	union multiple_tree
 *	{
 *		struct slapi_value_tree *vt;
 *	};
 */
struct slapi_value_set
{
	struct slapi_value **va;
};

struct valuearrayfast
{
	int num; /* The number of values in the array */
	int max; /* The number of slots in the array */
	struct slapi_value **va;
};

struct bervals2free {
    struct berval **bvals;
    struct bervals2free *next;
};

/*
 * represents an attribute instance (type + values + syntax)
 */

struct slapi_attr {
	char					*a_type;
	struct slapi_value_set  a_present_values;
	unsigned long		    a_flags;		/* SLAPI_ATTR_FLAG_... */
	struct slapdplugin	    *a_plugin; /* for the attribute syntax */
	struct slapi_value_set  a_deleted_values;
	struct bervals2free     *a_listtofree; /* JCM: EVIL... For DS4 Slapi compatibility. */
	struct slapi_attr		*a_next;
	CSN                     *a_deletioncsn; /* The point in time at which this attribute was last deleted */
	struct slapdplugin	    *a_mr_eq_plugin; /* for the attribute EQUALITY matching rule, if any */
	struct slapdplugin	    *a_mr_ord_plugin; /* for the attribute ORDERING matching rule, if any */
	struct slapdplugin	    *a_mr_sub_plugin; /* for the attribute SUBSTRING matching rule, if any */
};

typedef struct oid_item {
    char *oi_oid;
    struct slapdplugin	*oi_plugin;
    struct oid_item	*oi_next;
} oid_item_t;

/* schema extension item: X-ORIGIN, X-CSN, etc */
typedef struct schemaext {
    char *term;
    char **values;
    int value_count;
    struct schemaext *next;
} schemaext;

/* attribute description (represents an attribute, but not the value) */
typedef struct asyntaxinfo {
    char    			*asi_oid;			/* OID */
	char				*asi_name;			/* normalized name */
	char				**asi_aliases;		/* alternative names */
    char    			*asi_desc;			/* textual description */
	char				*asi_superior;		/* derived from */
	char				*asi_mr_equality;	/* equality matching rule */
	char				*asi_mr_ordering;	/* ordering matching rule */
	char				*asi_mr_substring;	/* substring matching rule */
	schemaext			*asi_extensions;	/* schema extensions (X-ORIGIN, X-?????, ...) */
	struct slapdplugin	*asi_plugin;		/* syntax */
	unsigned long		asi_flags;			/* SLAPI_ATTR_FLAG_... */
	int					asi_syntaxlength;	/* length associated w/syntax */
	int					asi_refcnt;			/* outstanding references */
	PRBool				asi_marked_for_delete;	/* delete at next opportunity */
	struct slapdplugin	*asi_mr_eq_plugin;	/* EQUALITY matching rule plugin */
	struct slapdplugin	*asi_mr_sub_plugin;	/* SUBSTR matching rule plugin */
	struct slapdplugin	*asi_mr_ord_plugin;	/* ORDERING matching rule plugin */
} asyntaxinfo;

/*
 * Note: most of the asi_flags values are defined in slapi-plugin.h, but
 * these ones are private to the DS.
 */
#define SLAPI_ATTR_FLAG_OVERRIDE	0x0010	/* when adding a new attribute,
											   override the existing attribute,
											   if any */
#define SLAPI_ATTR_FLAG_NOLOCKING	0x0020	/* the init code doesn't lock the
											   tables */
#define SLAPI_ATTR_FLAG_KEEP		0x8000 /* keep when replacing all */

/* This is the type of the function passed into attr_syntax_enumerate_attrs */
typedef int (*AttrEnumFunc)(struct asyntaxinfo *asi, void *arg);
/* Possible return values for an AttrEnumFunc */
#define ATTR_SYNTAX_ENUM_NEXT	0	/* continue */
#define ATTR_SYNTAX_ENUM_STOP	1	/* halt the enumeration */
#define ATTR_SYNTAX_ENUM_REMOVE	2	/* unhash current node and continue */

/* flags for slapi_attr_syntax_normalize_ext */
#define ATTR_SYNTAX_NORM_ORIG_ATTR 0x1 /* a space and following characters are
                                          removed from the given string */

/* This is the type of the function passed into plugin_syntax_enumerate */
typedef int (*SyntaxEnumFunc)(char **names, Slapi_PluginDesc *plugindesc,
							void *arg);

/* OIDs for some commonly used syntaxes */
#define BINARY_SYNTAX_OID    		"1.3.6.1.4.1.1466.115.121.1.5"
#define BITSTRING_SYNTAX_OID		"1.3.6.1.4.1.1466.115.121.1.6"
#define BOOLEAN_SYNTAX_OID		"1.3.6.1.4.1.1466.115.121.1.7"
#define COUNTRYSTRING_SYNTAX_OID	"1.3.6.1.4.1.1466.115.121.1.11"
#define DN_SYNTAX_OID        		"1.3.6.1.4.1.1466.115.121.1.12"
#define DELIVERYMETHOD_SYNTAX_OID	"1.3.6.1.4.1.1466.115.121.1.14"
#define DIRSTRING_SYNTAX_OID		"1.3.6.1.4.1.1466.115.121.1.15"
#define ENHANCEDGUIDE_SYNTAX_OID	"1.3.6.1.4.1.1466.115.121.1.21"
#define FACSIMILE_SYNTAX_OID		"1.3.6.1.4.1.1466.115.121.1.22"
#define FAX_SYNTAX_OID			"1.3.6.1.4.1.1466.115.121.1.23"
#define GENERALIZEDTIME_SYNTAX_OID	"1.3.6.1.4.1.1466.115.121.1.24"
#define GUIDE_SYNTAX_OID		"1.3.6.1.4.1.1466.115.121.1.25"
#define IA5STRING_SYNTAX_OID		"1.3.6.1.4.1.1466.115.121.1.26"
#define INTEGER_SYNTAX_OID   		"1.3.6.1.4.1.1466.115.121.1.27"
#define JPEG_SYNTAX_OID			"1.3.6.1.4.1.1466.115.121.1.28"
#define NAMEANDOPTIONALUID_SYNTAX_OID	"1.3.6.1.4.1.1466.115.121.1.34"
#define NUMERICSTRING_SYNTAX_OID	"1.3.6.1.4.1.1466.115.121.1.36"
#define OID_SYNTAX_OID			"1.3.6.1.4.1.1466.115.121.1.38"
#define OCTETSTRING_SYNTAX_OID		"1.3.6.1.4.1.1466.115.121.1.40"
#define POSTALADDRESS_SYNTAX_OID	"1.3.6.1.4.1.1466.115.121.1.41"
#define PRINTABLESTRING_SYNTAX_OID	"1.3.6.1.4.1.1466.115.121.1.44"
#define TELEPHONE_SYNTAX_OID		"1.3.6.1.4.1.1466.115.121.1.50"
#define TELETEXTERMID_SYNTAX_OID	"1.3.6.1.4.1.1466.115.121.1.51"
#define TELEXNUMBER_SYNTAX_OID		"1.3.6.1.4.1.1466.115.121.1.52"
#define SPACE_INSENSITIVE_STRING_SYNTAX_OID	"2.16.840.1.113730.3.7.1"

/* OIDs for some commonly used matching rules */
#define DNMATCH_OID				"2.5.13.1"	/* distinguishedNameMatch */
#define CASEIGNOREMATCH_OID		"2.5.13.2"	/* caseIgnoreMatch */
#define INTFIRSTCOMPMATCH_OID	"2.5.13.29"	/* integerFirstComponentMatch */
#define OIDFIRSTCOMPMATCH_OID	"2.5.13.30"	/* objectIdentifierFirstComponentMatch */

/* Names for some commonly used matching rules */
#define DNMATCH_NAME			"distinguishedNameMatch"
#define CASEIGNOREMATCH_NAME	"caseIgnoreMatch"
#define INTFIRSTCOMPMATCH_NAME	"integerFirstComponentMatch"
#define OIDFIRSTCOMPMATCH_NAME	"objectIdentifierFirstComponentMatch"

#define ATTR_STANDARD_STRING	"Standard Attribute"
#define ATTR_USERDEF_STRING		"User Defined Attribute"
#define OC_STANDARD_STRING		"Standard ObjectClass"
#define OC_USERDEF_STRING		"User Defined ObjectClass"

/* modifiers used to define attributes */
#define ATTR_MOD_OPERATIONAL "operational"
#define ATTR_MOD_OVERRIDE    "override"
#define ATTR_MOD_SINGLE      "single"

/* extended operations supported by the server */
#define EXTOP_BULK_IMPORT_START_OID     "2.16.840.1.113730.3.5.7"
#define EXTOP_BULK_IMPORT_DONE_OID      "2.16.840.1.113730.3.5.8"
#define EXTOP_PASSWD_OID		"1.3.6.1.4.1.4203.1.11.1"

/* 
 * Represents a Distinguished Name of an entry
 * WARNING, if you change this stucture you MUST update dn_size()
 * function in entry.c
 */
struct slapi_dn
{
    unsigned char flag;
    const char *udn; /* DN [original] */
    const char *dn;  /* Normalised DN */
    const char *ndn; /* Case Normalised DN */
    int ndn_len;     /* normalized dn length */
};

/* 
 * Represents a Relative Distinguished Name.
 */
#define FLAG_RDNS 0
#define FLAG_ALL_RDNS 1
#define FLAG_ALL_NRDNS 2

struct slapi_rdn
{
    unsigned char flag;
	char *rdn;
	char **rdns;       /* Valid when FLAG_RDNS is set. */
	int butcheredupto; /* How far through rdns we've gone converting '=' to '\0' */
	char *nrdn;        /* normalized rdn */
	char **all_rdns;   /* Valid when FLAG_ALL_RDNS is set. */
	char **all_nrdns;  /* Valid when FLAG_ALL_NRDNS is set. */
};

/* 
 * representation of uniqueID. Defined in uuid.h
 */
#define UID_SIZE 16	/* size of unique id in bytes */

/*
 * max 1G attr values per entry
 * in case, libdb returned bogus entry string from db (blackflag #623569)
 */
#define ENTRY_MAX_ATTRIBUTE_VALUE_COUNT 1073741824 

/*
 * represents an entry in core
 * WARNING, if you change this stucture you MUST update slapi_entry_size()
 * function
 */
struct slapi_entry {
    struct slapi_dn e_sdn;       /* DN of this entry */
    struct slapi_rdn e_srdn;     /* RDN of this entry */
    char *e_uniqueid;            /* uniqueID of this entry */
    CSNSet *e_dncsnset;          /* The set of DN CSNs for this entry */
    CSN *e_maxcsn;               /* maximum CSN of the entry */
    Slapi_Attr *e_attrs;         /* list of attributes and values   */
    Slapi_Attr *e_deleted_attrs; /* deleted list of attributes and values */
    Slapi_Attr *e_virtual_attrs; /* list of virtual attributes */
    time_t e_virtual_watermark;  /* indicates cache consistency when compared
                                    to global watermark */ 
    Slapi_RWLock *e_virtual_lock;    /* for access to cached vattrs */
    void *e_extension;           /* A list of entry object extensions */
    unsigned char e_flags;
    Slapi_Attr *e_aux_attrs;     /* Attr list used for upgrade */
};

struct attrs_in_extension {
    char *ext_type;
    IFP ext_get;
    IFP ext_set;
    IFP ext_copy;
};

extern struct attrs_in_extension attrs_in_extension[];

/*
 * represents schema information for a database
 */
/* values for oc_flags (only space for 8 of these right now!) */
#define OC_FLAG_STANDARD_OC		1
#define OC_FLAG_USER_OC			2
#define OC_FLAG_REDEFINED_OC	4
#define OC_FLAG_OBSOLETE		8

/* values for oc_kind */
#define OC_KIND_ABSTRACT		0
#define OC_KIND_STRUCTURAL		1
#define OC_KIND_AUXILIARY		2


/* XXXmcs: ../plugins/cos/cos_cache.c has its own copy of this definition! */
struct objclass {
    char                *oc_name;       /* NAME */
    char                *oc_desc;       /* DESC */
    char                *oc_oid;        /* object identifier */
    char                *oc_superior;   /* SUP -- XXXmcs: should be an array */
    PRUint8             oc_kind;        /* ABSTRACT/STRUCTURAL/AUXILIARY */
    PRUint8             oc_flags;       /* misc. flags, e.g., OBSOLETE */
    char                **oc_required;
    char                **oc_allowed;
    char                **oc_orig_required; /* MUST */
    char                **oc_orig_allowed;  /* MAY */
    schemaext           *oc_extensions; /* schema extensions (X-ORIGIN, X-?????, ...) */
    struct objclass     *oc_next;
};

struct matchingRuleList {
    Slapi_MatchingRuleEntry *mr_entry;
    struct matchingRuleList *mrl_next;
};
 
/* List of the plugin index numbers */

/* Backend & Global Plugins */
#define PLUGIN_LIST_DATABASE 0
#define PLUGIN_LIST_PREOPERATION 1
#define PLUGIN_LIST_POSTOPERATION 2
#define PLUGIN_LIST_BEPREOPERATION 3
#define PLUGIN_LIST_BEPOSTOPERATION 4
#define PLUGIN_LIST_INTERNAL_PREOPERATION 5
#define PLUGIN_LIST_INTERNAL_POSTOPERATION 6
#define PLUGIN_LIST_EXTENDED_OPERATION 7
#define PLUGIN_LIST_BACKEND_MAX 8

/* Global Plugins */
#define PLUGIN_LIST_ACL 9
#define PLUGIN_LIST_MATCHINGRULE 10
#define PLUGIN_LIST_SYNTAX 11
#define PLUGIN_LIST_ENTRY 12
#define PLUGIN_LIST_OBJECT 13
#define PLUGIN_LIST_PWD_STORAGE_SCHEME 14
#define PLUGIN_LIST_VATTR_SP 15	/* DBDB */
#define PLUGIN_LIST_REVER_PWD_STORAGE_SCHEME 16
#define PLUGIN_LIST_LDBM_ENTRY_FETCH_STORE 17
#define PLUGIN_LIST_INDEX 18
#define PLUGIN_LIST_BETXNPREOPERATION 19
#define PLUGIN_LIST_BETXNPOSTOPERATION 20
#define PLUGIN_LIST_GLOBAL_MAX 21

/* plugin configuration attributes */
#define ATTR_PLUGIN_PATH				"nsslapd-pluginPath"
#define ATTR_PLUGIN_INITFN				"nsslapd-pluginInitFunc"
#define ATTR_PLUGIN_TYPE				"nsslapd-pluginType"
#define ATTR_PLUGIN_PLUGINID			"nsslapd-pluginId"
#define ATTR_PLUGIN_VERSION				"nsslapd-pluginVersion"
#define ATTR_PLUGIN_VENDOR				"nsslapd-pluginVendor"
#define ATTR_PLUGIN_DESC				"nsslapd-pluginDescription"
#define ATTR_PLUGIN_ENABLED				"nsslapd-pluginEnabled"
#define ATTR_PLUGIN_ARG					"nsslapd-pluginArg"
#define ATTR_PLUGIN_CONFIG_AREA			"nsslapd-pluginConfigArea"
#define ATTR_PLUGIN_BACKEND				"nsslapd-backend"
#define ATTR_PLUGIN_SCHEMA_CHECK		"nsslapd-schemaCheck"
#define ATTR_PLUGIN_LOG_ACCESS			"nsslapd-logAccess"
#define ATTR_PLUGIN_LOG_AUDIT			"nsslapd-logAudit"
#define ATTR_PLUGIN_TARGET_SUBTREE		"nsslapd-targetSubtree"
#define ATTR_PLUGIN_EXCLUDE_TARGET_SUBTREE	"nsslapd-exclude-targetSubtree"
#define ATTR_PLUGIN_BIND_SUBTREE		"nsslapd-bindSubtree"
#define ATTR_PLUGIN_EXCLUDE_BIND_SUBTREE	"nsslapd-exclude-bindSubtree"
#define ATTR_PLUGIN_INVOKE_FOR_REPLOP	"nsslapd-invokeForReplOp"
#define ATTR_PLUGIN_LOAD_NOW            "nsslapd-pluginLoadNow"
#define ATTR_PLUGIN_LOAD_GLOBAL         "nsslapd-pluginLoadGlobal"
#define ATTR_PLUGIN_PRECEDENCE			"nsslapd-pluginPrecedence"

/* plugin precedence defines */
#define PLUGIN_DEFAULT_PRECEDENCE 50
#define PLUGIN_MIN_PRECEDENCE 1
#define PLUGIN_MAX_PRECEDENCE 99

/* plugin action states */
enum
{
	PLGC_OFF,			/* internal operation action is on */
	PLGC_ON,			/* internal operation action is off */
	PLGC_UPTOPLUGIN		/* internal operation action is left up to plugin */
};

/* special data specifications */
enum
{
	PLGC_DATA_LOCAL,				/* plugin has access to all data hosted by this server */
	PLGC_DATA_REMOTE,				/* plugin has access to all requests for data not hosted by this server */
	PLGC_DATA_BIND_ANONYMOUS,		/* plugin bind function should be invoked for anonymous binds */
	PLGC_DATA_BIND_ROOT,			/* plugin bind function should be invoked for directory manager binds */
	PLGC_DATA_MAX	
};

/* DataList definition */
struct datalist
{
	void	 **elements;		/* array of elements */
	int		 element_count;		/* number of elements in the array */
	int		 alloc_count;		/* number of allocated nodes in the array */
}datalist;

/* data available to plugins */
typedef struct target_data
{
	DataList	subtrees;						/* regular DIT subtrees acessible to the plugin */
	PRBool		special_data [PLGC_DATA_MAX];	/* array of special data specification */
}PluginTargetData;

struct pluginconfig{
	PluginTargetData plgc_target_subtrees;		/* list of subtrees accessible by the plugin */
	PluginTargetData plgc_excluded_target_subtrees;	/* list of subtrees inaccessible by the plugin */
	PluginTargetData plgc_bind_subtrees;		/* the list of subtrees for which plugin is invoked during bind operation */
	PluginTargetData plgc_excluded_bind_subtrees;	/* the list of subtrees for which plugin is not invoked during bind operation */
	PRBool		     plgc_schema_check;		/* inidcates whether schema check is performed during internal op */
	PRBool		     plgc_log_change;		/* indicates whether changes are logged during internal op */
	PRBool		     plgc_log_access;		/* indicates whether internal op is recorded in access log */
	PRBool		     plgc_log_audit;		/* indicates whether internal op is recorded in audit log */	
	PRBool		     plgc_invoke_for_replop;/* indicates that plugin should be invoked for internal operations */
};

struct slapdplugin {
	void				*plg_private;	/* data private to plugin */
	char				*plg_version;	/* version of this plugin */
	int                 plg_argc;       /* argc from config file */
	char				**plg_argv;		/* args from config file */
	char				*plg_libpath;	/* library path for dll/so */
	char				*plg_initfunc;  /* init symbol */
	IFP					plg_close;		/* close function */
	Slapi_PluginDesc	plg_desc;		/* vendor's info */
	char				*plg_name;		/* used for plugin rdn in cn=config */
	struct slapdplugin	*plg_next;		/* for plugin lists */
	int					plg_type;		/* discriminates union */
	char				*plg_dn;		/* config dn for this plugin */
	int					plg_precedence;	/* for plugin execution ordering */
	struct slapdplugin  *plg_group;		/* pointer to the group to which this plugin belongs */
	struct pluginconfig plg_conf;		/* plugin configuration parameters */
	IFP					plg_cleanup;	/* cleanup function */
	IFP					plg_start;		/* start function */
	IFP					plg_poststart;	/* poststart function */
	int					plg_closed;		/* mark plugin as closed */

/* NOTE: These LDIF2DB and DB2LDIF fn pointers are internal only for now.
   I don't believe you can get these functions from a plug-in and
   then call them without knowing what IFP or VFP0 are.  (These aren't
   declared in slapi-plugin.h.)  More importantly, it's a pretty ugly
   way to get to these functions. (Do we want people to get locked into
   this?)

   The correct way to do this would be to expose these functions as 
   front-end API functions. We can fix this for the next release.
   (No one has the time right now.)
 */
	union { /* backend database plugin structure */
		struct plg_un_database_backend {
			IFP	plg_un_db_bind;	  	  /* bind */
			IFP	plg_un_db_unbind;	  /* unbind */
			IFP	plg_un_db_search;	  /* search */
			IFP	plg_un_db_next_search_entry;	/* iterate */
	        IFP plg_un_db_next_search_entry_ext;
			VFPP plg_un_db_search_results_release; /* PAGED RESULTS */
			VFP plg_un_db_prev_search_results;     /* PAGED RESULTS */
	        IFP plg_un_db_entry_release;
			IFP	plg_un_db_compare;	  /* compare */
			IFP	plg_un_db_modify;	  /* modify */
			IFP	plg_un_db_modrdn;	  /* modrdn */
			IFP	plg_un_db_add;		  /* add */
			IFP	plg_un_db_delete;	  /* delete */
			IFP	plg_un_db_abandon;	  /* abandon */
			IFP	plg_un_db_config;	  /* config */
			IFP	plg_un_db_flush;	  /* close */
			IFP	plg_un_db_seq;	  	  /* sequence */
			IFP	plg_un_db_entry;	  /* entry send */
			IFP	plg_un_db_referral;   /* referral send */
			IFP	plg_un_db_result;	  /* result send */
			IFP	plg_un_db_ldif2db;	  /* ldif 2 database */
			IFP	plg_un_db_db2ldif;	  /* database 2 ldif */
			IFP	plg_un_db_db2index;	  /* database 2 index */
			IFP	plg_un_db_archive2db; /* ldif 2 database */
			IFP	plg_un_db_db2archive; /* database 2 ldif */
			IFP	plg_un_db_upgradedb;  /* convert old idl to new */
			IFP	plg_un_db_upgradednformat;  /* convert old dn format to new */
			IFP	plg_un_db_begin;	  /* dbase txn begin */
			IFP	plg_un_db_commit;	  /* dbase txn commit */
			IFP	plg_un_db_abort;	  /* dbase txn abort */
			IFP	plg_un_db_dbsize;	  /* database size */
			IFP	plg_un_db_dbtest;	  /* database size */
			IFP	plg_un_db_rmdb;		  /* database remove */
			IFP	plg_un_db_register_dn_callback; /* Register a function to call when a operation is applied to a given DN */
			IFP	plg_un_db_register_oc_callback; /* Register a function to call when a operation is applied to a given ObjectClass */
			IFP	plg_un_db_init_instance;  /* initializes new db instance */
			IFP	plg_un_db_wire_import;    /* fast replica update */
			IFP	plg_un_db_verify;	      /* verify db files */
			IFP	plg_un_db_add_schema;     /* add schema */
			IFP	plg_un_db_get_info;       /* get info */
			IFP	plg_un_db_set_info;       /* set info */
			IFP	plg_un_db_ctrl_info;      /* ctrl info */
		} plg_un_db;
#define plg_bind		plg_un.plg_un_db.plg_un_db_bind
#define plg_unbind		plg_un.plg_un_db.plg_un_db_unbind
#define plg_search		plg_un.plg_un_db.plg_un_db_search
#define plg_next_search_entry	plg_un.plg_un_db.plg_un_db_next_search_entry
#define plg_next_search_entry_ext plg_un.plg_un_db.plg_un_db_next_search_entry_ext
#define plg_search_results_release plg_un.plg_un_db.plg_un_db_search_results_release
#define plg_prev_search_results plg_un.plg_un_db.plg_un_db_prev_search_results
#define plg_entry_release       plg_un.plg_un_db.plg_un_db_entry_release
#define plg_compare		plg_un.plg_un_db.plg_un_db_compare
#define plg_modify		plg_un.plg_un_db.plg_un_db_modify
#define plg_modrdn		plg_un.plg_un_db.plg_un_db_modrdn
#define plg_add			plg_un.plg_un_db.plg_un_db_add
#define plg_delete		plg_un.plg_un_db.plg_un_db_delete
#define plg_abandon		plg_un.plg_un_db.plg_un_db_abandon
#define plg_config		plg_un.plg_un_db.plg_un_db_config
#define plg_flush		plg_un.plg_un_db.plg_un_db_flush
#define plg_seq			plg_un.plg_un_db.plg_un_db_seq
#define plg_entry		plg_un.plg_un_db.plg_un_db_entry
#define plg_referral		plg_un.plg_un_db.plg_un_db_referral
#define plg_result		plg_un.plg_un_db.plg_un_db_result
#define plg_ldif2db		plg_un.plg_un_db.plg_un_db_ldif2db
#define plg_db2ldif		plg_un.plg_un_db.plg_un_db_db2ldif
#define plg_db2index		plg_un.plg_un_db.plg_un_db_db2index
#define plg_archive2db		plg_un.plg_un_db.plg_un_db_archive2db
#define plg_db2archive		plg_un.plg_un_db.plg_un_db_db2archive
#define plg_upgradedb		plg_un.plg_un_db.plg_un_db_upgradedb
#define plg_upgradednformat	plg_un.plg_un_db.plg_un_db_upgradednformat
#define plg_dbverify		plg_un.plg_un_db.plg_un_db_verify
#define plg_dbsize		plg_un.plg_un_db.plg_un_db_dbsize
#define plg_dbtest		plg_un.plg_un_db.plg_un_db_dbtest
#define plg_rmdb		plg_un.plg_un_db.plg_un_db_rmdb
#define plg_init_instance       plg_un.plg_un_db.plg_un_db_init_instance
#define plg_wire_import         plg_un.plg_un_db.plg_un_db_wire_import
#define plg_add_schema          plg_un.plg_un_db.plg_un_db_add_schema
#define plg_get_info            plg_un.plg_un_db.plg_un_db_get_info
#define plg_set_info            plg_un.plg_un_db.plg_un_db_set_info
#define plg_ctrl_info            plg_un.plg_un_db.plg_un_db_ctrl_info

		/* extended operation plugin structure */
		struct plg_un_protocol_extension {
			char	**plg_un_pe_exoids;	  /* exop oids */
			char	**plg_un_pe_exnames;  /* exop names (may be NULL) */
			IFP	plg_un_pe_exhandler;	  /* handler */
		} plg_un_pe;
#define plg_exoids		plg_un.plg_un_pe.plg_un_pe_exoids
#define plg_exnames		plg_un.plg_un_pe.plg_un_pe_exnames
#define plg_exhandler		plg_un.plg_un_pe.plg_un_pe_exhandler


		/* pre-operation plugin structure */
		struct plg_un_pre_operation {
			IFP	plg_un_pre_bind;  	  /* bind */
			IFP	plg_un_pre_unbind;	  /* unbind */
			IFP	plg_un_pre_search;	  /* search */
			IFP	plg_un_pre_compare;	  /* compare */
			IFP	plg_un_pre_modify;	  /* modify */
			IFP	plg_un_pre_modrdn;	  /* modrdn */
			IFP	plg_un_pre_add;		  /* add */
			IFP	plg_un_pre_delete;	  /* delete */
			IFP	plg_un_pre_abandon;	  /* abandon */
			IFP	plg_un_pre_entry;	  /* entry send */
			IFP	plg_un_pre_referral;	  /* referral send */
			IFP	plg_un_pre_result;	  /* result send */
		} plg_un_pre;
#define plg_prebind	plg_un.plg_un_pre.plg_un_pre_bind
#define plg_preunbind	plg_un.plg_un_pre.plg_un_pre_unbind
#define plg_presearch	plg_un.plg_un_pre.plg_un_pre_search
#define plg_precompare	plg_un.plg_un_pre.plg_un_pre_compare
#define plg_premodify	plg_un.plg_un_pre.plg_un_pre_modify
#define plg_premodrdn	plg_un.plg_un_pre.plg_un_pre_modrdn
#define plg_preadd	plg_un.plg_un_pre.plg_un_pre_add
#define plg_predelete	plg_un.plg_un_pre.plg_un_pre_delete
#define plg_preabandon	plg_un.plg_un_pre.plg_un_pre_abandon
#define plg_preentry	plg_un.plg_un_pre.plg_un_pre_entry
#define plg_prereferral	plg_un.plg_un_pre.plg_un_pre_referral
#define plg_preresult	plg_un.plg_un_pre.plg_un_pre_result

		/* post-operation plugin structure */
		struct plg_un_post_operation {
			IFP	plg_un_post_bind;  	  /* bind */
			IFP	plg_un_post_unbind;	  /* unbind */
			IFP	plg_un_post_search;	  /* search */
			IFP	plg_un_post_searchfail;	  /* failed search */
			IFP	plg_un_post_compare;	  /* compare */
			IFP	plg_un_post_modify;	  /* modify */
			IFP	plg_un_post_modrdn;	  /* modrdn */
			IFP	plg_un_post_add;	  /* add */
			IFP	plg_un_post_delete;	  /* delete */
			IFP	plg_un_post_abandon;	  /* abandon */
			IFP	plg_un_post_entry;	  /* entry send */
			IFP	plg_un_post_referral;	  /* referral send */
			IFP	plg_un_post_result;	  /* result send */
		} plg_un_post;
#define plg_postbind		plg_un.plg_un_post.plg_un_post_bind
#define plg_postunbind		plg_un.plg_un_post.plg_un_post_unbind
#define plg_postsearch		plg_un.plg_un_post.plg_un_post_search
#define plg_postsearchfail	plg_un.plg_un_post.plg_un_post_searchfail
#define plg_postcompare		plg_un.plg_un_post.plg_un_post_compare
#define plg_postmodify		plg_un.plg_un_post.plg_un_post_modify
#define plg_postmodrdn		plg_un.plg_un_post.plg_un_post_modrdn
#define plg_postadd		plg_un.plg_un_post.plg_un_post_add
#define plg_postdelete		plg_un.plg_un_post.plg_un_post_delete
#define plg_postabandon		plg_un.plg_un_post.plg_un_post_abandon
#define plg_postentry		plg_un.plg_un_post.plg_un_post_entry
#define plg_postreferral	plg_un.plg_un_post.plg_un_post_referral
#define plg_postresult		plg_un.plg_un_post.plg_un_post_result

        /* backend pre-operation plugin structure */
		struct plg_un_bepre_operation {
			IFP	plg_un_bepre_modify;	  /* modify */
			IFP	plg_un_bepre_modrdn;	  /* modrdn */
			IFP	plg_un_bepre_add;		  /* add */
			IFP	plg_un_bepre_delete;	  /* delete */
			IFP	plg_un_bepre_delete_tombstone;	  /* tombstone creation */
			IFP	plg_un_bepre_close;		  /* close */
			IFP	plg_un_bepre_backup;	  /* backup */
		} plg_un_bepre;
#define plg_bepremodify	plg_un.plg_un_bepre.plg_un_bepre_modify
#define plg_bepremodrdn	plg_un.plg_un_bepre.plg_un_bepre_modrdn
#define plg_bepreadd	plg_un.plg_un_bepre.plg_un_bepre_add
#define plg_bepredelete	plg_un.plg_un_bepre.plg_un_bepre_delete
#define plg_bepreclose	plg_un.plg_un_bepre.plg_un_bepre_close
#define plg_beprebackup	plg_un.plg_un_bepre.plg_un_bepre_backup

		/* backend post-operation plugin structure */
		struct plg_un_bepost_operation {
			IFP	plg_un_bepost_modify;	  /* modify */
			IFP	plg_un_bepost_modrdn;	  /* modrdn */
			IFP	plg_un_bepost_add;	  /* add */
			IFP	plg_un_bepost_delete;	  /* delete */
			IFP	plg_un_bepost_open;		  /* open */
			IFP	plg_un_bepost_backup;	  /* backup */
		} plg_un_bepost;
#define plg_bepostmodify		plg_un.plg_un_bepost.plg_un_bepost_modify
#define plg_bepostmodrdn		plg_un.plg_un_bepost.plg_un_bepost_modrdn
#define plg_bepostadd			plg_un.plg_un_bepost.plg_un_bepost_add
#define plg_bepostdelete		plg_un.plg_un_bepost.plg_un_bepost_delete
#define plg_bepostopen			plg_un.plg_un_bepost.plg_un_bepost_open
#define plg_bepostbackup		plg_un.plg_un_bepost.plg_un_bepost_backup

        /* internal  pre-operation plugin structure */
		struct plg_un_internal_pre_operation {
			IFP	plg_un_internal_pre_modify;	  /* modify */
			IFP	plg_un_internal_pre_modrdn;	  /* modrdn */
			IFP	plg_un_internal_pre_add;		  /* add */
			IFP	plg_un_internal_pre_delete;	  /* delete */
			IFP	plg_un_internal_pre_bind;	  /* bind */
		} plg_un_internal_pre;
#define plg_internal_pre_modify	plg_un.plg_un_internal_pre.plg_un_internal_pre_modify
#define plg_internal_pre_modrdn	plg_un.plg_un_internal_pre.plg_un_internal_pre_modrdn
#define plg_internal_pre_add	plg_un.plg_un_internal_pre.plg_un_internal_pre_add
#define plg_internal_pre_delete	plg_un.plg_un_internal_pre.plg_un_internal_pre_delete
#define plg_internal_pre_bind	plg_un.plg_un_internal_pre.plg_un_internal_pre_bind

		/* internal post-operation plugin structure */
		struct plg_un_internal_post_operation {
			IFP	plg_un_internal_post_modify;	  /* modify */
			IFP	plg_un_internal_post_modrdn;	  /* modrdn */
			IFP	plg_un_internal_post_add;	  /* add */
			IFP	plg_un_internal_post_delete;	  /* delete */
		} plg_un_internal_post;
#define plg_internal_post_modify		plg_un.plg_un_internal_post.plg_un_internal_post_modify
#define plg_internal_post_modrdn		plg_un.plg_un_internal_post.plg_un_internal_post_modrdn
#define plg_internal_post_add			plg_un.plg_un_internal_post.plg_un_internal_post_add
#define plg_internal_post_delete		plg_un.plg_un_internal_post.plg_un_internal_post_delete

		/* matching rule plugin structure */
		struct plg_un_matching_rule {
			IFP	plg_un_mr_filter_create; /* factory function */
			IFP	plg_un_mr_indexer_create; /* factory function */
			/* new style syntax plugin functions */
			/* not all functions will apply to all matching rule types */
			/* e.g. a SUBSTR rule will not have a filter_ava func */
			IFP	plg_un_mr_filter_ava;
			IFP	plg_un_mr_filter_sub;
			IFP	plg_un_mr_values2keys;
			IFP	plg_un_mr_assertion2keys_ava;
			IFP	plg_un_mr_assertion2keys_sub;
			int	plg_un_mr_flags;
			char	**plg_un_mr_names;
			IFP	plg_un_mr_compare; /* only for ORDERING */
			VFPV	plg_un_mr_normalize;
		} plg_un_mr;
#define plg_mr_filter_create	plg_un.plg_un_mr.plg_un_mr_filter_create
#define plg_mr_indexer_create	plg_un.plg_un_mr.plg_un_mr_indexer_create
#define plg_mr_filter_ava		plg_un.plg_un_mr.plg_un_mr_filter_ava
#define plg_mr_filter_sub		plg_un.plg_un_mr.plg_un_mr_filter_sub
#define plg_mr_values2keys		plg_un.plg_un_mr.plg_un_mr_values2keys
#define plg_mr_assertion2keys_ava	plg_un.plg_un_mr.plg_un_mr_assertion2keys_ava
#define plg_mr_assertion2keys_sub	plg_un.plg_un_mr.plg_un_mr_assertion2keys_sub
#define plg_mr_flags		plg_un.plg_un_mr.plg_un_mr_flags
#define plg_mr_names		plg_un.plg_un_mr.plg_un_mr_names
#define plg_mr_compare		plg_un.plg_un_mr.plg_un_mr_compare
#define plg_mr_normalize	plg_un.plg_un_mr.plg_un_mr_normalize

		/* syntax plugin structure */
		struct plg_un_syntax_struct {
			IFP	plg_un_syntax_filter_ava;
		    IFP plg_un_syntax_filter_ava_sv;
			IFP	plg_un_syntax_filter_sub;
			IFP	plg_un_syntax_filter_sub_sv;
			IFP	plg_un_syntax_values2keys;
		    IFP plg_un_syntax_values2keys_sv; 
			IFP	plg_un_syntax_assertion2keys_ava;
			IFP	plg_un_syntax_assertion2keys_sub;
			int	plg_un_syntax_flags;
/*
   from slapi-plugin.h
#define SLAPI_PLUGIN_SYNTAX_FLAG_ORKEYS		1
#define SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING	2
*/
			char	**plg_un_syntax_names;
			char	*plg_un_syntax_oid;
			IFP	plg_un_syntax_compare;
			IFP	plg_un_syntax_validate;
			VFPV plg_un_syntax_normalize;
		} plg_un_syntax;
#define plg_syntax_filter_ava		plg_un.plg_un_syntax.plg_un_syntax_filter_ava
#define plg_syntax_filter_sub		plg_un.plg_un_syntax.plg_un_syntax_filter_sub
#define plg_syntax_values2keys		plg_un.plg_un_syntax.plg_un_syntax_values2keys
#define plg_syntax_assertion2keys_ava	plg_un.plg_un_syntax.plg_un_syntax_assertion2keys_ava
#define plg_syntax_assertion2keys_sub	plg_un.plg_un_syntax.plg_un_syntax_assertion2keys_sub
#define plg_syntax_flags		plg_un.plg_un_syntax.plg_un_syntax_flags
#define plg_syntax_names		plg_un.plg_un_syntax.plg_un_syntax_names
#define plg_syntax_oid			plg_un.plg_un_syntax.plg_un_syntax_oid
#define plg_syntax_compare		plg_un.plg_un_syntax.plg_un_syntax_compare
#define plg_syntax_validate		plg_un.plg_un_syntax.plg_un_syntax_validate
#define plg_syntax_normalize	plg_un.plg_un_syntax.plg_un_syntax_normalize

		struct plg_un_acl_struct {
			IFP	plg_un_acl_init;
			IFP	plg_un_acl_syntax_check;
			IFP	plg_un_acl_access_allowed;
			IFP	plg_un_acl_mods_allowed;
			IFP	plg_un_acl_mods_update;
		} plg_un_acl;
#define plg_acl_init 			plg_un.plg_un_acl.plg_un_acl_init
#define plg_acl_syntax_check		plg_un.plg_un_acl.plg_un_acl_syntax_check
#define plg_acl_access_allowed 		plg_un.plg_un_acl.plg_un_acl_access_allowed
#define plg_acl_mods_allowed		plg_un.plg_un_acl.plg_un_acl_mods_allowed
#define plg_acl_mods_update		plg_un.plg_un_acl.plg_un_acl_mods_update

		/* password storage scheme (kexcoff) */
		struct plg_un_pwd_storage_scheme_struct {
			char *plg_un_pwd_storage_scheme_name;	/* SHA, SSHA...*/
			CFP plg_un_pwd_storage_scheme_enc;
			IFP plg_un_pwd_storage_scheme_dec;
			IFP plg_un_pwd_storage_scheme_cmp;
		} plg_un_pwd_storage_scheme;
#define plg_pwdstorageschemename plg_un.plg_un_pwd_storage_scheme.plg_un_pwd_storage_scheme_name
#define plg_pwdstorageschemeenc plg_un.plg_un_pwd_storage_scheme.plg_un_pwd_storage_scheme_enc
#define plg_pwdstorageschemedec plg_un.plg_un_pwd_storage_scheme.plg_un_pwd_storage_scheme_dec
#define plg_pwdstorageschemecmp plg_un.plg_un_pwd_storage_scheme.plg_un_pwd_storage_scheme_cmp

		/* entry fetch/store */
		struct plg_un_entry_fetch_store_struct {
			IFP plg_un_entry_fetch_func;
			IFP plg_un_entry_store_func;
		} plg_un_entry_fetch_store;
#define plg_entryfetchfunc plg_un.plg_un_entry_fetch_store.plg_un_entry_fetch_func
#define plg_entrystorefunc plg_un.plg_un_entry_fetch_store.plg_un_entry_store_func

        /* backend txn pre-operation plugin structure */
		struct plg_un_betxnpre_operation {
			IFP	plg_un_betxnpre_modify;	  /* modify */
			IFP	plg_un_betxnpre_modrdn;	  /* modrdn */
			IFP	plg_un_betxnpre_add;		  /* add */
			IFP	plg_un_betxnpre_delete;	  /* delete */
			IFP	plg_un_betxnpre_delete_tombstone;	  /* delete tombstone */
		} plg_un_betxnpre;
#define plg_betxnpremodify	plg_un.plg_un_betxnpre.plg_un_betxnpre_modify
#define plg_betxnpremodrdn	plg_un.plg_un_betxnpre.plg_un_betxnpre_modrdn
#define plg_betxnpreadd	plg_un.plg_un_betxnpre.plg_un_betxnpre_add
#define plg_betxnpredelete	plg_un.plg_un_betxnpre.plg_un_betxnpre_delete
#define plg_betxnpredeletetombstone	plg_un.plg_un_betxnpre.plg_un_betxnpre_delete_tombstone

        /* backend txn post-operation plugin structure */
		struct plg_un_betxnpost_operation {
			IFP	plg_un_betxnpost_modify;	  /* modify */
			IFP	plg_un_betxnpost_modrdn;	  /* modrdn */
			IFP	plg_un_betxnpost_add;		  /* add */
			IFP	plg_un_betxnpost_delete;	  /* delete */
		} plg_un_betxnpost;
#define plg_betxnpostmodify	plg_un.plg_un_betxnpost.plg_un_betxnpost_modify
#define plg_betxnpostmodrdn	plg_un.plg_un_betxnpost.plg_un_betxnpost_modrdn
#define plg_betxnpostadd	plg_un.plg_un_betxnpost.plg_un_betxnpost_add
#define plg_betxnpostdelete	plg_un.plg_un_betxnpost.plg_un_betxnpost_delete

	} plg_un;
};

struct suffixlist {
	Slapi_DN *be_suffix;
	struct suffixlist *next;
};

/*
 * represents a "database"
 */
typedef struct backend {
    struct suffixlist *be_suffixlist; /* linked list of DN suffixes in this backend */
    PRLock *be_suffixlock;
    Slapi_Counter *be_suffixcounter;
    char *be_basedn;     /* The base dn for the config & monitor dns */
	char *be_configdn;   /* The config dn for this backend          */
	char *be_monitordn;  /* The monitor dn for this backend          */
	int	be_readonly;     /* 1 => db is in "read only" mode	   */
	int	be_sizelimit;    /* size limit for this backend   	   */
	int	be_timelimit;    /* time limit for this backend       	   */
	int	be_maxnestlevel; /* Max nest level for acl group evaluation */
	int	be_noacl;        /* turn off front end acl for this be      */
	int	be_lastmod;      /* keep track of lastmodified{by,time}	   */
	char *be_type;       /* type of database			   */
	char *be_backendconfig; /* backend config filename */
	char **be_include;   /* include files within this db definition */
	int be_private;      /* Internal backends use this to hide from the user */
	int be_logchanges;   /* changes to this backend should be logged in the changelog */
    int (*be_writeconfig)(Slapi_PBlock *pb); /* function to call to make this
												backend write its conf file */
	/*
	 * backend database api function ptrs and args (to do operations)
	 */
	struct slapdplugin	*be_database;	/* single plugin */
#define be_bind			be_database->plg_bind
#define be_unbind		be_database->plg_unbind
#define be_search		be_database->plg_search
#define	be_next_search_entry be_database->plg_next_search_entry
#define be_next_search_entry_ext be_database->plg_next_search_entry_ext
#define be_entry_release be_database->plg_entry_release
#define be_search_results_release be_database->plg_search_results_release
#define be_prev_search_results be_database->plg_prev_search_results
#define be_compare		be_database->plg_compare
#define be_modify		be_database->plg_modify
#define be_modrdn		be_database->plg_modrdn
#define be_add			be_database->plg_add
#define be_delete		be_database->plg_delete
#define be_abandon		be_database->plg_abandon
#define be_config		be_database->plg_config
#define be_close		be_database->plg_close
#define be_flush		be_database->plg_flush
#define be_start		be_database->plg_start
#define be_poststart		be_database->plg_poststart
#define be_seq			be_database->plg_seq
#define be_ldif2db		be_database->plg_ldif2db
#define be_upgradedb		be_database->plg_upgradedb
#define be_upgradednformat	be_database->plg_upgradednformat
#define be_db2ldif		be_database->plg_db2ldif
#define be_db2index		be_database->plg_db2index
#define be_archive2db	be_database->plg_archive2db
#define be_db2archive	be_database->plg_db2archive
#define be_dbsize		be_database->plg_dbsize
#define be_dbtest		be_database->plg_dbtest
#define be_rmdb			be_database->plg_rmdb
#define be_result		be_database->plg_result
#define be_init_instance be_database->plg_init_instance
#define be_cleanup		be_database->plg_cleanup
#define be_wire_import          be_database->plg_wire_import
#define be_get_info             be_database->plg_get_info
#define be_set_info             be_database->plg_set_info
#define be_ctrl_info            be_database->plg_ctrl_info

	void *be_instance_info;		/* If the database plugin pointed to by
								 * be_database supports more than one instance,
								 * it can use this to keep track of the 
								 * multiple instances. */

	char *be_name;				/* The mapping tree and command line utils 
								 * refer to backends by name. */
	int be_mapped;				/* True if the be is represented by a node
								 * in the mapping tree. */

	/*struct slapdplugin	*be_plugin_list[PLUGIN_LIST_BACKEND_MAX]; list of plugins */
	
    int    be_delete_on_exit;	/* marks db for deletion - used to remove changelog*/
    int    be_state;			/* indicates current database state */ 
	PRLock *be_state_lock;		/* lock under which to modify the state */

    int     be_flags;		/* misc properties. See BE_FLAG_xxx defined in slapi-private.h */
	Slapi_RWLock    *be_lock;	
	Slapi_RWLock    *vlvSearchList_lock;
	void        *vlvSearchList;
	Slapi_Counter *be_usn_counter; /* USN counter; one counter per backend */
	int	be_pagedsizelimit;    /* size limit for this backend for simple paged result searches */
} backend;

enum
{
	BE_STATE_STOPPED = 1,	/* backend is initialized but not started */
	BE_STATE_STARTED,		/* backend is started */
	BE_STATE_CLEANED,		/* backend was cleaned up */
	BE_STATE_DELETED,		/* backend is removed */
	BE_STATE_STOPPING       /* told to stop but not yet stopped */
};

struct conn;
struct op;

typedef void (*result_handler)( struct conn *, struct op *, int, char *,
	char *, int, struct berval ** );
typedef int (*search_entry_handler)( Slapi_Backend *, struct conn *, struct op *,
	struct slapi_entry * );
typedef int (*search_referral_handler)( Slapi_Backend *, struct conn *, struct op *,
	struct berval ** );
typedef CSN * (*csngen_handler)( Slapi_PBlock *pb, const CSN *basecsn );
typedef int (*replica_attr_handler)( Slapi_PBlock *pb, const char *type, void **value );

/*
 * LDAP Operation results.
 */
typedef struct slapi_operation_results
{
	unsigned long operation_type;

	int opreturn;

	LDAPControl	**result_controls;/* ctrls to be returned w/result  */

	int result_code;
	char*  result_text;
	char*  result_matched;

	union
	{
		struct bind_results
		{
			struct berval *bind_ret_saslcreds; /* v3 serverSaslCreds */
		} r_bind;

		struct search_results
		{
			/*
			 * Search results set - opaque cookie passed between backend
			 * and frontend to iterate over search results.
			 */
			void *search_result_set;
			/* Next entry returned from iterating */
			Slapi_Entry *search_result_entry;
		    /* opaque pointer owned by the backend.  Used in searches with 
			 * lookahead */
		    void *opaque_backend_ptr;
			/* number of entries sent in response to this search request */
			int nentries;
			/* Any referrals encountered during the search */
			struct berval **search_referrals;
			/* estimated search result set size */
			int estimate;
		} r_search;

		struct extended_results
		{
			char *exop_ret_oid;
			struct berval *exop_ret_value;
		} r_extended;
	} r;
} slapi_operation_results;

/*
 * represents an operation pending from an ldap client
 */
typedef struct op {
	BerElement	*o_ber;		/* ber of the request		  */
	ber_int_t	o_msgid;	/* msgid of the request		  */
	ber_tag_t	o_tag;		/* tag of the request		  */
	time_t		o_time;		/* time op was initiated	  */
	PRIntervalTime	o_interval;	/* precise time op was initiated  */
	int		o_isroot;	/* requestor is manager		  */
	Slapi_DN	o_sdn;		/* dn bound when op was initiated */
	char		*o_authtype;	/* auth method used to bind dn	  */
	int		o_ssf;		/* ssf for this operation (highest between SASL and TLS/SSL) */
	int		o_opid;		/* id of this operation		  */
	PRUint64	o_connid;	/* id of conn initiating this op; for logging only */
	void		*o_handler_data;
	result_handler		o_result_handler;
	search_entry_handler	o_search_entry_handler;
	search_referral_handler	o_search_referral_handler;
	csngen_handler		o_csngen_handler;
	replica_attr_handler	o_replica_attr_handler;
	struct op	*o_next;	/* next operation pending	  */
	int		o_status;		/* status (see SLAPI_OP_STATUS_... below */
	char		**o_searchattrs;/* original attr names requested  */ /* JCM - Search Param */
	unsigned long	o_flags;	/* flags for this operation	  */
	void *o_extension; /* plugins are able to extend the Operation object */
	Slapi_DN *o_target_spec; /* used to decide which plugins should be called for the operation */
	unsigned long o_abandoned_op; /* operation abandoned by this operation - used to decide which plugins to invoke */
	struct slapi_operation_parameters o_params;
	struct slapi_operation_results o_results;
	int o_pagedresults_sizelimit;
} Operation;

/*
 * Operation status (o_status) values.
 * The normal progression is from PROCESSING to RESULT_SENT, with
 *   WILL_COMPLETE as an optional intermediate state.
 * For operations that are abandoned, the progression is from PROCESSING
 *   to ABANDONED.
 */
#define SLAPI_OP_STATUS_PROCESSING		0	/* the normal state */
#define SLAPI_OP_STATUS_ABANDONED		1	/* op. has been abandoned */
#define SLAPI_OP_STATUS_WILL_COMPLETE	2	/* no more abandon checks
												will be done */
#define SLAPI_OP_STATUS_RESULT_SENT		3	/* result has been sent to the
												client (or we tried to do
												so and failed) */


/* simple paged structure */
typedef struct _paged_results {
    Slapi_Backend *pr_current_be;         /* backend being used */
    void          *pr_search_result_set;  /* search result set for paging */
    int           pr_search_result_count; /* search result count */
    int           pr_search_result_set_size_estimate; /* estimated search result set size */
    int           pr_sort_result_code;    /* sort result put in response */
    time_t        pr_timelimit;           /* time limit for this request */
    int           pr_flags;
    ber_int_t     pr_msgid;               /* msgid of the request; to abandon */
    PRLock        *pr_mutex;              /* protect each conn structure    */
} PagedResults;

/* array of simple paged structure stashed in connection */
typedef struct _paged_results_list {
    int prl_maxlen;          /* size of the PagedResults array */ 
    int prl_count;           /* count of the list in use */ 
    PagedResults *prl_list;  /* pointer to pr_maxlen length PageResults array */
} PagedResultsList;

/*
 * represents a connection from an ldap client
 */

typedef int (*Conn_IO_Layer_cb)(struct conn*, void *data);

struct Conn_Private;
typedef struct Conn_private Conn_private;

typedef struct conn {
	Sockbuf		*c_sb;		/* ber connection stuff		  */
	int		c_sd;		/* the actual socket descriptor	  */
	int		c_ldapversion;	/* version of LDAP protocol       */
	char		*c_dn;		/* current DN bound to this conn  */
	int		c_isroot;	/* c_dn was rootDN at time of bind? */
	int		c_isreplication_session;	/* this connection is a replication session */
	char		*c_authtype;	/* auth method used to bind c_dn  */
	char		*c_external_dn;	/* client DN of this SSL session  */
	char		*c_external_authtype; /* used for c_external_dn   */
        PRNetAddr	*cin_addr;	/* address of client on this conn */
	PRNetAddr	*cin_destaddr;	/* address client connected to    */
	struct berval	**c_domain;	/* DNS names of client            */
	Operation		*c_ops;		/* list of pending operations	  */
	int				c_gettingber;	/* in the middle of ber_get_next  */
	BerElement		*c_currentber;	/* ber we're getting              */
	time_t			c_starttime;	/* when the connection was opened */
	PRUint64	c_connid;	/* id of this connection for stats*/
	int				c_opsinitiated;	/* # ops initiated/next op id	  */
	PRInt32			c_opscompleted;	/* # ops completed		  */
	PRInt32			c_threadnumber; /* # threads used in this conn    */
	int				c_refcnt;	/* # ops refering to this conn    */
	PRLock			*c_mutex;	/* protect each conn structure    */
	PRLock			*c_pdumutex;	/* only write one pdu at a time   */
	time_t			c_idlesince;	/* last time of activity on conn  */
	Conn_private	*c_private;	/* data which is not shared outside*/
								/* connection.c 		  */
	int				c_flags;	/* Misc flags used only for SSL   */
								/* status currently               */
	int				c_needpw;	/* need new password 		  */
	CERTCertificate *c_client_cert;	/* Client's Cert		  */
	PRFileDesc	*	c_prfd;	/* NSPR 2.1 FileDesc		  */
	int             c_ci;       /* An index into the Connection array. For printing. */
	int             c_fdi;      /* An index into the FD array. The FD this connection is using. */
    struct conn *   c_next;         /* Pointer to the next and previous */
    struct conn *   c_prev;         /* active connections in the table*/
        Slapi_Backend *c_bi_backend;    /* which backend is doing the import */
	void *c_extension; /* plugins are able to extend the Connection object */
    void *c_sasl_conn;   /* sasl library connection sasl_conn_t */
    int				c_local_ssf; /* flag to tell us the local SSF */
    int				c_sasl_ssf; /* flag to tell us the SASL SSF */
    int				c_ssl_ssf; /* flag to tell us the SSL/TLS SSF */
    int				c_unix_local; /* flag true for LDAPI */
    int				c_local_valid; /* flag true if the uid/gid are valid */
    uid_t			c_local_uid;  /* uid of connecting process */
    gid_t			c_local_gid;  /* gid of connecting process */
    PagedResultsList c_pagedresults; /* PAGED_RESULTS */
    /* IO layer push/pop */
    Conn_IO_Layer_cb c_push_io_layer_cb; /* callback to push an IO layer on the conn->c_prfd */
    Conn_IO_Layer_cb c_pop_io_layer_cb; /* callback to pop an IO layer off of the conn->c_prfd */
    void             *c_io_layer_cb_data; /* callback data */

} Connection;
#define CONN_FLAG_SSL	1	/* Is this connection an SSL connection or not ? 
							 * Used to direct I/O code when SSL is handled differently 
							 */
#define CONN_FLAG_CLOSING 2    /* If this flag is set, then the connection has
				* been marked for closing by a worker thread 
				* and the listener thread should close it. */
#define CONN_FLAG_IMPORT 4      /* This connection has begun a bulk import
                                 * (aka "fast replica init" aka "wire import"),
                                 * so it can only accept adds & ext-ops.
                                 */


#define CONN_FLAG_SASL_CONTINUE 8     /* We're in a multi-stage sasl bind */

#define CONN_FLAG_START_TLS 16   /* Flag set when an SSL connection is so after an 
				  * Start TLS request operation. 
				  */

#define CONN_FLAG_SASL_COMPLETE 32  /* Flag set when a sasl bind has been 
                                     * successfully completed.
                                     */

#define CONN_FLAG_PAGEDRESULTS_WITH_SORT   64/* paged results control is
                                              * sent with server side sorting
                                              */

#define CONN_FLAG_PAGEDRESULTS_UNINDEXED  128/* If the search is unindexed,
                                              * store the info in c_flags
                                              */
#define CONN_FLAG_PAGEDRESULTS_PROCESSING 256/* there is an operation
                                              * processing a pagedresults search
                                              */
#define CONN_FLAG_PAGEDRESULTS_ABANDONED  512/* pagedresults abandoned */
#define CONN_GET_SORT_RESULT_CODE (-1)

#define START_TLS_OID    "1.3.6.1.4.1.1466.20037"


#ifndef _WIN32
#define SLAPD_POLL_FLAGS	(POLLIN)
#else
#define SLAPD_POLL_FLAGS	(PR_POLL_READ)
#endif

/******************************************************************************
 *  * Online tasks interface (to support import, export, etc)
 *   * After some cleanup, we could consider making these public.
 *    */
struct slapi_task {
    struct slapi_task *next;
    char *task_dn;
    int task_exitcode;          /* for the end user */
    int task_state;             /* current state of task */
    int task_progress;          /* number between 0 and task_work */
    int task_work;              /* "units" of work to be done */
    int task_flags;             /* (see above) */
    char *task_status;          /* transient status info */
    char *task_log;             /* appended warnings, etc */
    void *task_private;         /* allow opaque data to be stashed in the task */
    TaskCallbackFn cancel;      /* task has been cancelled by user */
    TaskCallbackFn destructor;  /* task entry is being destroyed */
    int task_refcount;
    PRLock *task_log_lock;      /* To protect task_log to be realloced if 
                                   it's in use */
} slapi_task;
/* End of interface to support online tasks **********************************/

typedef struct passwordpolicyarray {
  slapi_onoff_t pw_change;        /* 1 - indicates that users are allowed to change the pwd */
  slapi_onoff_t pw_must_change;   /* 1 - indicates that users must change pwd upon reset */
  slapi_onoff_t pw_syntax;
  int pw_minlength;
  int pw_mindigits;
  int pw_minalphas;
  int pw_minuppers;
  int pw_minlowers;
  int pw_minspecials;
  int pw_min8bit;
  int pw_maxrepeats;
  int pw_mincategories;
  int pw_mintokenlength;
  slapi_onoff_t pw_exp;
  long pw_maxage;
  long pw_minage;
  long pw_warning;
  slapi_onoff_t pw_history;
  int pw_inhistory;
  slapi_onoff_t pw_lockout;
  int pw_maxfailure;
  slapi_onoff_t pw_unlock;
  long pw_lockduration;
  long pw_resetfailurecount;
  int pw_gracelimit;
  slapi_onoff_t pw_is_legacy;
  slapi_onoff_t pw_track_update_time;
  struct pw_scheme *pw_storagescheme;
  Slapi_DN *pw_admin;
  Slapi_DN **pw_admin_user;
} passwdPolicy;

typedef struct slapi_pblock {
	/* common */
	Slapi_Backend		*pb_backend;
	Connection	*pb_conn;
	Operation	*pb_op;
	struct slapdplugin	*pb_plugin;	/* plugin being called */
	int		pb_opreturn;
	void*		pb_object;	/* points to data private to plugin */
	IFP		pb_destroy_fn;
	int		pb_requestor_isroot;
	/* config file */
	char		*pb_config_fname;
	int		pb_config_lineno;
	int		pb_config_argc;
	char		**pb_config_argv;
	int		plugin_tracking;

	/* [pre|post]add arguments */
	struct slapi_entry	*pb_target_entry; /* JCM - Duplicated */
	struct slapi_entry	*pb_existing_dn_entry;
	struct slapi_entry	*pb_existing_uniqueid_entry;
	struct slapi_entry	*pb_parent_entry;
	struct slapi_entry	*pb_newparent_entry;

	/* state of entry before and after add/delete/modify/moddn/modrdn */
	struct slapi_entry	*pb_pre_op_entry;
	struct slapi_entry	*pb_post_op_entry;
	/* seq access arguments */
	int             pb_seq_type;
	char            *pb_seq_attrname;
	char            *pb_seq_val;
	/* ldif2db arguments */
	char		*pb_ldif_file;
	int		pb_removedupvals;
	char		**pb_db2index_attrs;
	int		pb_ldif2db_noattrindexes;
	/* db2ldif arguments */
	int		pb_ldif_printkey;
	/* ldif2db/db2ldif/db2bak/bak2db args */
	char *pb_instance_name;
	Slapi_Task      *pb_task;
	int		pb_task_flags;
	/* matching rule arguments */
	mrFilterMatchFn	pb_mr_filter_match_fn;
	IFP		pb_mr_filter_index_fn;
	IFP		pb_mr_filter_reset_fn;
	IFP		pb_mr_index_fn; /* values and keys are struct berval ** */
	char*		pb_mr_oid;
	char*		pb_mr_type;
	struct berval*	pb_mr_value;
	struct berval**	pb_mr_values;
	struct berval**	pb_mr_keys;
	unsigned int	pb_mr_filter_reusable;
	int		pb_mr_query_operator;
	unsigned int	pb_mr_usage;

	/* arguments for password storage scheme (kexcoff) */
	char *pb_pwd_storage_scheme_user_passwd;
	char *pb_pwd_storage_scheme_db_passwd;

	/* controls we know about */
	int		pb_managedsait;

    /* additional fields for plugin_internal_ldap_ops */
	/* result code of internal ldap_operation */
    int		pb_internal_op_result;
	/* pointer to array of results returned on search */
	Slapi_Entry	**pb_plugin_internal_search_op_entries;
	char		**pb_plugin_internal_search_op_referrals;
	void		*pb_plugin_identity; /* identifies plugin for internal operation */
	char		*pb_plugin_config_area; /* optional config area */
	void		*pb_parent_txn;	/* parent transaction ID */
	void		*pb_txn;		/* transaction ID */
	IFP		pb_txn_ruv_mods_fn; /* Function to fetch RUV mods for txn */

	/* Size of the database on disk, in kilobytes */
	unsigned int	pb_dbsize;

	/* THINGS BELOW THIS LINE EXIST ONLY IN SLAPI v2 (slapd 4.0+) */

	/* ldif2db: array of files to import all at once */
	char **pb_ldif_files;

	char		**pb_ldif_include;
	char		**pb_ldif_exclude;
	int		pb_ldif_dump_replica;
	int		pb_ldif_dump_uniqueid;		/* dump uniqueid during db2ldif */
	int		pb_ldif_generate_uniqueid;	/* generate uniqueid during db2ldif */
	char*     pb_ldif_namespaceid;		/* used for name based uniqueid generation */ 
	int     pb_ldif_encrypt;		/* used to enable encrypt/decrypt on import and export */ 
	/*
	 * notes to log with RESULT line in the access log
	 * these are actually stored as a bitmap; see slapi-plugin.h for
	 *	defined notes.
	 */
	unsigned int	pb_operation_notes;
	/*
	 * slapd command line arguments
	 */
	int pb_slapd_argc;
	char** pb_slapd_argv;
	char *pb_slapd_configdir; /* the config directory passed to slapd on the command line */
	LDAPControl	**pb_ctrls_arg; /* allows to pass controls as arguments before
								   operation object is created  */
	int pb_dse_dont_add_write; /* if true, the dse is not written when an entry is added */
	int pb_dse_add_merge; /* if true, if a duplicate entry is found when adding, the 
							 new values are merged into the old entry */
	int pb_dse_dont_check_dups; /* if false, use the "enhanced" version of str2entry to catch
								   more errors when adding dse entries; this can only be done
								   after the schema and syntax and matching rule plugins are
								   running */
	int pb_dse_is_primary_file;	/* for read callbacks: non-zero for primary file */
	int pb_schema_flags; 		/* schema flags */
								/* . check/load info (schema reload task) */
								/* . refresh user defined schema */

	/* NEW in 5.0 for getting back the backend result in frontend */
	int pb_result_code;			/* operation result code */
	char * pb_result_text;		/* result text when available */
	char * pb_result_matched;	/* macthed dn when NO SUCH OBJECT  error */
	int pb_nentries;			/* number of entries to be returned */
	struct berval **urls;		/* urls of referrals to be returned */

    /*
     * wire import (fast replica init) arguments
     */
    struct slapi_entry *pb_import_entry;
    int pb_import_state;

    int pb_destroy_content;     /* flag to indicate that pblock content should be
                                   destroyed when pblock is destroyed */
	int pb_dse_reapply_mods; /* if true, dse_modify will reapply mods after modify callback */
	char * pb_urp_naming_collision_dn;	/* replication naming conflict removal */
	char * pb_urp_tombstone_uniqueid;	/* replication change tombstone */
	int		pb_server_running; /* indicate that server is running */
	int		pb_backend_count;  /* instance count involved in the op */

	/* For password policy control */
	int		pb_pwpolicy_ctrl;
	void	*pb_vattr_context;      /* hold the vattr_context for roles/cos */

	int		*pb_substrlens; /* user specified minimum substr search key lengths:
							 * nsSubStrBegin, nsSubStrMiddle, nsSubStrEnd
							 */
	int		pb_plugin_enabled; /* nsslapd-pluginEnabled: on|off */
							   /* used in plugin init; pb_plugin is not ready, then */
	LDAPControl	**pb_search_ctrls; /* for search operations, allows plugins to provide
									  controls to pass for each entry or referral returned */
	IFP		pb_mr_index_sv_fn; /* values and keys are Slapi_Value ** */
	int		pb_syntax_filter_normalized; /* the syntax filter types/values are already normalized */
	void		*pb_syntax_filter_data; /* extra data to pass to a syntax plugin function */
	int	pb_paged_results_index;    /* stash SLAPI_PAGED_RESULTS_INDEX */
	passwdPolicy *pwdpolicy;
	void *op_stack_elem;
} slapi_pblock;

/* index if substrlens */
#define INDEX_SUBSTRBEGIN	0
#define INDEX_SUBSTRMIDDLE	1
#define INDEX_SUBSTREND		2
#define INDEX_SUBSTRLEN		3	/* size of the substrlens */

/* The referral element */
typedef struct ref {
     char          *ref_dn;       /* The DN of the entry that contains the referral */
     struct berval *ref_referral; /* The referral. It looks like: ldap://host:port */
     int            ref_reads;    /* 1 if refer searches, 0 else */
     int            ref_writes;   /* 1 if refer modifications, 0 else */
} Ref;

/* The head of the referral array. */
typedef struct ref_array {
    Slapi_RWLock	     *ra_rwlock;    /* Read-write lock struct to protect this thing */
    int       ra_size;      /* The size of this puppy (NOT the number of entries)*/
    int       ra_nextindex; /* The next free index */
    int       ra_readcount; /* The number of copyingfroms in the list */
    Ref     **ra_refs;      /* The array of referrals*/
} Ref_Array;

#define GR_LOCK_READ()    slapi_rwlock_rdlock(grefs->ra_rwlock)
#define GR_UNLOCK_READ()  slapi_rwlock_unlock(grefs->ra_rwlock)
#define GR_LOCK_WRITE()   slapi_rwlock_wrlock(grefs->ra_rwlock)
#define GR_UNLOCK_WRITE() slapi_rwlock_unlock(grefs->ra_rwlock)

/*
 * This structure is used to pass a pair of port numbers to the daemon
 * function. The daemon is the root of a forked thread.
 */

typedef struct daemon_ports_s {
	int			n_port;
	int			s_port;
	PRNetAddr	**n_listenaddr;
	PRNetAddr	**s_listenaddr;
#if defined( XP_WIN32 )
	int		n_socket;
	int		s_socket_native;
#else
	PRFileDesc	**n_socket;
#if defined(ENABLE_LDAPI)
	/* ldapi */
	PRNetAddr       **i_listenaddr;
	int             i_port; /* used as a flag only */
	PRFileDesc      **i_socket;
#endif
#endif
	PRFileDesc	**s_socket;
} daemon_ports_t;


/* Definition for plugin syntax compare routine */
typedef int (*value_compare_fn_type)(const struct berval *,const struct berval *);

/* Definition for plugin syntax validate routine */
typedef int (*value_validate_fn_type)(const struct berval *);

#include "proto-slap.h"
LDAPMod** entry2mods(Slapi_Entry *, LDAPMod **, int *, int);

/* SNMP Counter Variables */
struct snmp_ops_tbl_t{
    Slapi_Counter *dsAnonymousBinds;
    Slapi_Counter *dsUnAuthBinds;
    Slapi_Counter *dsSimpleAuthBinds;
    Slapi_Counter *dsStrongAuthBinds;
    Slapi_Counter *dsBindSecurityErrors;
    Slapi_Counter *dsInOps;
    Slapi_Counter *dsReadOps;
    Slapi_Counter *dsCompareOps;
    Slapi_Counter *dsAddEntryOps;
    Slapi_Counter *dsRemoveEntryOps;
    Slapi_Counter *dsModifyEntryOps;
    Slapi_Counter *dsModifyRDNOps;
    Slapi_Counter *dsListOps;
    Slapi_Counter *dsSearchOps;
    Slapi_Counter *dsOneLevelSearchOps;
    Slapi_Counter *dsWholeSubtreeSearchOps;
    Slapi_Counter *dsReferrals;
    Slapi_Counter *dsChainings;
    Slapi_Counter *dsSecurityErrors;
    Slapi_Counter *dsErrors;
    Slapi_Counter *dsConnections;	 /* Number of currently connected clients */
    Slapi_Counter *dsConnectionSeq; /* Monotonically increasing number bumped on each new conn est */
    Slapi_Counter *dsBytesRecv;	/* Count of bytes read from clients */
    Slapi_Counter *dsBytesSent;	/* Count of bytes sent to clients */
    Slapi_Counter *dsEntriesReturned;
    Slapi_Counter *dsReferralsReturned;
};

struct snmp_entries_tbl_t{
    /* entries table */
    Slapi_Counter *dsMasterEntries;
    Slapi_Counter *dsCopyEntries;
    Slapi_Counter *dsCacheEntries;
    Slapi_Counter *dsCacheHits;
    Slapi_Counter *dsSlaveHits;
};

struct snmp_int_tbl_t{
   /* interaction table */
   PRUint32 dsIntIndex;
   char dsName[SNMP_FIELD_LENGTH];
   time_t dsTimeOfCreation;          
   time_t dsTimeOfLastAttempt;      
   time_t dsTimeOfLastSuccess;      
   PRUint32 dsFailuresSinceLastSuccess;
   PRUint32 dsFailures;
   PRUint32 dsSuccesses;
   char dsURL[SNMP_FIELD_LENGTH];
};

/* operation statistics */
struct snmp_vars_t{
    struct snmp_ops_tbl_t ops_tbl;
    struct snmp_entries_tbl_t entries_tbl;
    struct snmp_int_tbl_t int_tbl[NUM_SNMP_INT_TBL_ROWS];
};

#define ENTRY_POINT_PS_WAKEUP_ALL	102
#define ENTRY_POINT_PS_SERVICE		105
#define ENTRY_POINT_DISCONNECT_SERVER	107
#define ENTRY_POINT_SLAPD_SSL_CLIENT_INIT	108
#define ENTRY_POINT_SLAPD_SSL_INIT	109
#define ENTRY_POINT_SLAPD_SSL_INIT2	110

typedef void (*ps_wakeup_all_fn_ptr)( void );
typedef void (*ps_service_fn_ptr)(Slapi_Entry *, Slapi_Entry *, int, int );
typedef char *(*get_config_dn_fn_ptr)();
typedef void (*get_disconnect_server_fn_ptr)(Connection *conn, PRUint64 opconnid, int opid, PRErrorCode reason, PRInt32 error );
typedef int (*modify_config_dse_fn_ptr)( Slapi_PBlock *pb );
typedef int (*slapd_ssl_init_fn_ptr)( void );
typedef int (*slapd_ssl_init_fn_ptr2)( PRFileDesc **s, int StartTLS);

/*
 * A structure of entry points in the NT exe which need
 * to be available to DLLs.
 */
typedef struct _slapdEntryPoints {
    caddr_t	sep_ps_wakeup_all;
    caddr_t	sep_ps_service;
    caddr_t	sep_disconnect_server;
    caddr_t	sep_slapd_ssl_init;
    caddr_t	sep_slapd_ssl_init2;
} slapdEntryPoints;

#if defined( XP_WIN32 )
#define DLL_IMPORT_DATA _declspec( dllimport )
#else
#define DLL_IMPORT_DATA
#endif

/* Log types */
#define SLAPD_ACCESS_LOG 0x1
#define SLAPD_ERROR_LOG  0x2
#define SLAPD_AUDIT_LOG  0x4

#define CONFIG_DATABASE_ATTRIBUTE       "nsslapd-database"
#define CONFIG_PLUGIN_ATTRIBUTE         "nsslapd-plugin"
#define CONFIG_SIZELIMIT_ATTRIBUTE      "nsslapd-sizelimit"
#define CONFIG_PAGEDSIZELIMIT_ATTRIBUTE "nsslapd-pagedsizelimit"
#define CONFIG_TIMELIMIT_ATTRIBUTE      "nsslapd-timelimit"
#define CONFIG_SUFFIX_ATTRIBUTE         "nsslapd-suffix"
#define CONFIG_READONLY_ATTRIBUTE       "nsslapd-readonly"
#define CONFIG_REFERRAL_ATTRIBUTE       "nsslapd-referral"
#define CONFIG_OBJECTCLASS_ATTRIBUTE    "nsslapd-objectclass"
#define CONFIG_ATTRIBUTE_ATTRIBUTE      "nsslapd-attribute"
#define CONFIG_SCHEMACHECK_ATTRIBUTE    "nsslapd-schemacheck"
#define CONFIG_SCHEMAMOD_ATTRIBUTE      "nsslapd-schemamod"
#define CONFIG_SYNTAXCHECK_ATTRIBUTE	"nsslapd-syntaxcheck"
#define CONFIG_SYNTAXLOGGING_ATTRIBUTE	"nsslapd-syntaxlogging"
#define CONFIG_DN_VALIDATE_STRICT_ATTRIBUTE     "nsslapd-dn-validate-strict"
#define CONFIG_DS4_COMPATIBLE_SCHEMA_ATTRIBUTE  "nsslapd-ds4-compatible-schema"
#define CONFIG_SCHEMA_IGNORE_TRAILING_SPACES    "nsslapd-schema-ignore-trailing-spaces"
#define CONFIG_SCHEMAREPLACE_ATTRIBUTE	"nsslapd-schemareplace"
#define CONFIG_LOGLEVEL_ATTRIBUTE       "nsslapd-errorlog-level"
#define CONFIG_ACCESSLOGLEVEL_ATTRIBUTE "nsslapd-accesslog-level"
#define CONFIG_ACCESSLOG_MODE_ATTRIBUTE	"nsslapd-accesslog-mode"
#define CONFIG_ERRORLOG_MODE_ATTRIBUTE	"nsslapd-errorlog-mode"
#define CONFIG_AUDITLOG_MODE_ATTRIBUTE	"nsslapd-auditlog-mode"
#define CONFIG_ACCESSLOG_MAXNUMOFLOGSPERDIR_ATTRIBUTE "nsslapd-accesslog-maxlogsperdir"
#define CONFIG_ERRORLOG_MAXNUMOFLOGSPERDIR_ATTRIBUTE  "nsslapd-errorlog-maxlogsperdir"
#define CONFIG_AUDITLOG_MAXNUMOFLOGSPERDIR_ATTRIBUTE  "nsslapd-auditlog-maxlogsperdir"
#define CONFIG_ACCESSLOG_MAXLOGSIZE_ATTRIBUTE "nsslapd-accesslog-maxlogsize"
#define CONFIG_ERRORLOG_MAXLOGSIZE_ATTRIBUTE "nsslapd-errorlog-maxlogsize"
#define CONFIG_AUDITLOG_MAXLOGSIZE_ATTRIBUTE "nsslapd-auditlog-maxlogsize"
#define CONFIG_ACCESSLOG_LOGROTATIONSYNCENABLED_ATTRIBUTE "nsslapd-accesslog-logrotationsync-enabled"
#define CONFIG_ERRORLOG_LOGROTATIONSYNCENABLED_ATTRIBUTE "nsslapd-errorlog-logrotationsync-enabled"
#define CONFIG_AUDITLOG_LOGROTATIONSYNCENABLED_ATTRIBUTE "nsslapd-auditlog-logrotationsync-enabled"
#define CONFIG_ACCESSLOG_LOGROTATIONSYNCHOUR_ATTRIBUTE "nsslapd-accesslog-logrotationsynchour"
#define CONFIG_ERRORLOG_LOGROTATIONSYNCHOUR_ATTRIBUTE "nsslapd-errorlog-logrotationsynchour"
#define CONFIG_AUDITLOG_LOGROTATIONSYNCHOUR_ATTRIBUTE "nsslapd-auditlog-logrotationsynchour"
#define CONFIG_ACCESSLOG_LOGROTATIONSYNCMIN_ATTRIBUTE "nsslapd-accesslog-logrotationsyncmin"
#define CONFIG_ERRORLOG_LOGROTATIONSYNCMIN_ATTRIBUTE "nsslapd-errorlog-logrotationsyncmin"
#define CONFIG_AUDITLOG_LOGROTATIONSYNCMIN_ATTRIBUTE "nsslapd-auditlog-logrotationsyncmin"
#define CONFIG_ACCESSLOG_LOGROTATIONTIME_ATTRIBUTE "nsslapd-accesslog-logrotationtime"
#define CONFIG_ERRORLOG_LOGROTATIONTIME_ATTRIBUTE "nsslapd-errorlog-logrotationtime"
#define CONFIG_AUDITLOG_LOGROTATIONTIME_ATTRIBUTE "nsslapd-auditlog-logrotationtime"
#define CONFIG_ACCESSLOG_LOGROTATIONTIMEUNIT_ATTRIBUTE "nsslapd-accesslog-logrotationtimeunit"
#define CONFIG_ERRORLOG_LOGROTATIONTIMEUNIT_ATTRIBUTE "nsslapd-errorlog-logrotationtimeunit"
#define CONFIG_AUDITLOG_LOGROTATIONTIMEUNIT_ATTRIBUTE "nsslapd-auditlog-logrotationtimeunit"
#define CONFIG_ACCESSLOG_MAXLOGDISKSPACE_ATTRIBUTE "nsslapd-accesslog-logmaxdiskspace"
#define CONFIG_ERRORLOG_MAXLOGDISKSPACE_ATTRIBUTE "nsslapd-errorlog-logmaxdiskspace"
#define CONFIG_AUDITLOG_MAXLOGDISKSPACE_ATTRIBUTE "nsslapd-auditlog-logmaxdiskspace"
#define CONFIG_ACCESSLOG_MINFREEDISKSPACE_ATTRIBUTE "nsslapd-accesslog-logminfreediskspace"
#define CONFIG_ERRORLOG_MINFREEDISKSPACE_ATTRIBUTE "nsslapd-errorlog-logminfreediskspace"
#define CONFIG_AUDITLOG_MINFREEDISKSPACE_ATTRIBUTE "nsslapd-auditlog-logminfreediskspace"
#define CONFIG_ACCESSLOG_LOGEXPIRATIONTIME_ATTRIBUTE "nsslapd-accesslog-logexpirationtime"
#define CONFIG_ERRORLOG_LOGEXPIRATIONTIME_ATTRIBUTE "nsslapd-errorlog-logexpirationtime"
#define CONFIG_AUDITLOG_LOGEXPIRATIONTIME_ATTRIBUTE "nsslapd-auditlog-logexpirationtime"
#define CONFIG_ACCESSLOG_LOGEXPIRATIONTIMEUNIT_ATTRIBUTE "nsslapd-accesslog-logexpirationtimeunit"
#define CONFIG_ERRORLOG_LOGEXPIRATIONTIMEUNIT_ATTRIBUTE "nsslapd-errorlog-logexpirationtimeunit"
#define CONFIG_AUDITLOG_LOGEXPIRATIONTIMEUNIT_ATTRIBUTE "nsslapd-auditlog-logexpirationtimeunit"
#define CONFIG_ACCESSLOG_LOGGING_ENABLED_ATTRIBUTE "nsslapd-accesslog-logging-enabled"
#define CONFIG_ERRORLOG_LOGGING_ENABLED_ATTRIBUTE "nsslapd-errorlog-logging-enabled"
#define CONFIG_AUDITLOG_LOGGING_ENABLED_ATTRIBUTE "nsslapd-auditlog-logging-enabled"
#define CONFIG_AUDITLOG_LOGGING_HIDE_UNHASHED_PW "nsslapd-auditlog-logging-hide-unhashed-pw"
#define CONFIG_UNHASHED_PW_SWITCH_ATTRIBUTE "nsslapd-unhashed-pw-switch"
#define CONFIG_ROOTDN_ATTRIBUTE "nsslapd-rootdn"
#define CONFIG_ROOTPW_ATTRIBUTE "nsslapd-rootpw"
#define CONFIG_ROOTPWSTORAGESCHEME_ATTRIBUTE "nsslapd-rootpwstoragescheme"
#define CONFIG_AUDITFILE_ATTRIBUTE "nsslapd-auditlog"
#define CONFIG_LASTMOD_ATTRIBUTE   "nsslapd-lastmod"
#define CONFIG_INCLUDE_ATTRIBUTE   "nsslapd-include"
#define CONFIG_DYNAMICCONF_ATTRIBUTE "nsslapd-dynamicconf"
#define CONFIG_USEROC_ATTRIBUTE "nsslapd-useroc"
#define CONFIG_USERAT_ATTRIBUTE "nsslapd-userat"
#define CONFIG_SVRTAB_ATTRIBUTE "nsslapd-svrtab"
#define CONFIG_UNAUTH_BINDS_ATTRIBUTE "nsslapd-allow-unauthenticated-binds"
#define CONFIG_REQUIRE_SECURE_BINDS_ATTRIBUTE "nsslapd-require-secure-binds"
#define CONFIG_ANON_ACCESS_ATTRIBUTE "nsslapd-allow-anonymous-access"
#define CONFIG_LOCALSSF_ATTRIBUTE "nsslapd-localssf"
#define CONFIG_MINSSF_ATTRIBUTE "nsslapd-minssf"
#define CONFIG_MINSSF_EXCLUDE_ROOTDSE "nsslapd-minssf-exclude-rootdse"
#define CONFIG_VALIDATE_CERT_ATTRIBUTE "nsslapd-validate-cert"
#ifndef _WIN32
#define CONFIG_LOCALUSER_ATTRIBUTE "nsslapd-localuser"
#endif /* !_WIN32 */
#define CONFIG_LOCALHOST_ATTRIBUTE "nsslapd-localhost"
#define CONFIG_PORT_ATTRIBUTE "nsslapd-port"
#define CONFIG_WORKINGDIR_ATTRIBUTE "nsslapd-workingdir"
#define CONFIG_LISTENHOST_ATTRIBUTE "nsslapd-listenhost"
#define CONFIG_SNMP_INDEX_ATTRIBUTE "nsslapd-snmp-index"
#define CONFIG_LDAPI_FILENAME_ATTRIBUTE "nsslapd-ldapifilepath"
#define CONFIG_LDAPI_SWITCH_ATTRIBUTE "nsslapd-ldapilisten"
#define CONFIG_LDAPI_BIND_SWITCH_ATTRIBUTE "nsslapd-ldapiautobind"
#define CONFIG_LDAPI_ROOT_DN_ATTRIBUTE "nsslapd-ldapimaprootdn"
#define CONFIG_LDAPI_MAP_ENTRIES_ATTRIBUTE "nsslapd-ldapimaptoentries"
#define CONFIG_LDAPI_UIDNUMBER_TYPE_ATTRIBUTE "nsslapd-ldapiuidnumbertype"
#define CONFIG_LDAPI_GIDNUMBER_TYPE_ATTRIBUTE "nsslapd-ldapigidnumbertype"
#define CONFIG_LDAPI_SEARCH_BASE_DN_ATTRIBUTE "nsslapd-ldapientrysearchbase"
#define CONFIG_LDAPI_AUTO_DN_SUFFIX_ATTRIBUTE "nsslapd-ldapiautodnsuffix"
#define CONFIG_ANON_LIMITS_DN_ATTRIBUTE "nsslapd-anonlimitsdn"
#define CONFIG_SLAPI_COUNTER_ATTRIBUTE "nsslapd-counters"
#define CONFIG_SECURITY_ATTRIBUTE "nsslapd-security"
#define CONFIG_SSL3CIPHERS_ATTRIBUTE "nsslapd-SSL3ciphers"
#define CONFIG_ACCESSLOG_ATTRIBUTE "nsslapd-accesslog"
#define CONFIG_ERRORLOG_ATTRIBUTE "nsslapd-errorlog"
#define CONFIG_SECUREPORT_ATTRIBUTE "nsslapd-securePort"
#define CONFIG_SECURELISTENHOST_ATTRIBUTE "nsslapd-securelistenhost"
#define CONFIG_THREADNUMBER_ATTRIBUTE "nsslapd-threadnumber"
#define CONFIG_MAXTHREADSPERCONN_ATTRIBUTE "nsslapd-maxthreadsperconn"
#if !defined(_WIN32) && !defined(AIX)
#define CONFIG_MAXDESCRIPTORS_ATTRIBUTE "nsslapd-maxdescriptors"
#endif /* !_WIN32 && ! AIX */
#define CONFIG_CONNTABLESIZE_ATTRIBUTE "nsslapd-conntablesize"
#define CONFIG_RESERVEDESCRIPTORS_ATTRIBUTE "nsslapd-reservedescriptors"
#define CONFIG_IDLETIMEOUT_ATTRIBUTE "nsslapd-idletimeout"
#define CONFIG_IOBLOCKTIMEOUT_ATTRIBUTE "nsslapd-ioblocktimeout"
#define CONFIG_ACCESSCONTROL_ATTRIBUTE "nsslapd-accesscontrol"
#define CONFIG_GROUPEVALNESTLEVEL_ATTRIBUTE "nsslapd-groupevalnestlevel"
#define CONFIG_NAGLE_ATTRIBUTE "nsslapd-nagle"
#define CONFIG_PWPOLICY_LOCAL_ATTRIBUTE "nsslapd-pwpolicy-local"
#define CONFIG_PW_CHANGE_ATTRIBUTE "passwordChange"
#define CONFIG_PW_MUSTCHANGE_ATTRIBUTE "passwordMustChange"
#define CONFIG_PW_SYNTAX_ATTRIBUTE "passwordCheckSyntax"
#define CONFIG_PW_MINLENGTH_ATTRIBUTE "passwordMinLength"
#define CONFIG_PW_MINDIGITS_ATTRIBUTE "passwordMinDigits"
#define CONFIG_PW_MINALPHAS_ATTRIBUTE "passwordMinAlphas"
#define CONFIG_PW_MINUPPERS_ATTRIBUTE "passwordMinUppers"
#define CONFIG_PW_MINLOWERS_ATTRIBUTE "passwordMinLowers"
#define CONFIG_PW_MINSPECIALS_ATTRIBUTE "passwordMinSpecials"
#define CONFIG_PW_MIN8BIT_ATTRIBUTE "passwordMin8Bit"
#define CONFIG_PW_MAXREPEATS_ATTRIBUTE "passwordMaxRepeats"
#define CONFIG_PW_MINCATEGORIES_ATTRIBUTE "passwordMinCategories"
#define CONFIG_PW_MINTOKENLENGTH_ATTRIBUTE "passwordMinTokenLength"
#define CONFIG_PW_EXP_ATTRIBUTE "passwordExp"
#define CONFIG_PW_MAXAGE_ATTRIBUTE "passwordMaxAge"
#define CONFIG_PW_MINAGE_ATTRIBUTE "passwordMinAge"
#define CONFIG_PW_WARNING_ATTRIBUTE "passwordWarning"
#define CONFIG_PW_HISTORY_ATTRIBUTE "passwordHistory"
#define CONFIG_PW_INHISTORY_ATTRIBUTE "passwordInHistory"
#define CONFIG_PW_LOCKOUT_ATTRIBUTE "passwordLockout"
#define CONFIG_PW_STORAGESCHEME_ATTRIBUTE "passwordStorageScheme"
#define CONFIG_PW_MAXFAILURE_ATTRIBUTE "passwordMaxFailure"
#define CONFIG_PW_UNLOCK_ATTRIBUTE "passwordUnlock"
#define CONFIG_PW_LOCKDURATION_ATTRIBUTE "passwordLockoutDuration"
#define CONFIG_PW_RESETFAILURECOUNT_ATTRIBUTE "passwordResetFailureCount"
#define CONFIG_PW_ISGLOBAL_ATTRIBUTE "passwordIsGlobalPolicy"
#define CONFIG_PW_GRACELIMIT_ATTRIBUTE "passwordGraceLimit"
#define CONFIG_PW_IS_LEGACY "passwordLegacyPolicy"
#define CONFIG_PW_TRACK_LAST_UPDATE_TIME "passwordTrackUpdateTime"
#define CONFIG_PW_ADMIN_DN_ATTRIBUTE "passwordAdminDN"
#define CONFIG_ACCESSLOG_BUFFERING_ATTRIBUTE "nsslapd-accesslog-logbuffering"
#define CONFIG_CSNLOGGING_ATTRIBUTE "nsslapd-csnlogging"
#define CONFIG_RETURN_EXACT_CASE_ATTRIBUTE "nsslapd-return-exact-case"
#define CONFIG_RESULT_TWEAK_ATTRIBUTE "nsslapd-result-tweak"
#define CONFIG_REFERRAL_MODE_ATTRIBUTE		"nsslapd-referralmode"
#define CONFIG_ATTRIBUTE_NAME_EXCEPTION_ATTRIBUTE "nsslapd-attribute-name-exceptions"
#define CONFIG_MAXBERSIZE_ATTRIBUTE "nsslapd-maxbersize"
#define CONFIG_MAXSASLIOSIZE_ATTRIBUTE "nsslapd-maxsasliosize"
#define CONFIG_MAX_FILTER_NEST_LEVEL_ATTRIBUTE "nsslapd-max-filter-nest-level"
#define CONFIG_VERSIONSTRING_ATTRIBUTE "nsslapd-versionstring"
#define CONFIG_ENQUOTE_SUP_OC_ATTRIBUTE "nsslapd-enquote-sup-oc"
#define CONFIG_BASEDN_ATTRIBUTE "nsslapd-certmap-basedn"
#define CONFIG_ACCESSLOG_LIST_ATTRIBUTE "nsslapd-accesslog-list"
#define CONFIG_ERRORLOG_LIST_ATTRIBUTE "nsslapd-errorlog-list"
#define CONFIG_AUDITLOG_LIST_ATTRIBUTE "nsslapd-auditlog-list"
#define CONFIG_REWRITE_RFC1274_ATTRIBUTE "nsslapd-rewrite-rfc1274"
#define CONFIG_PLUGIN_BINDDN_TRACKING_ATTRIBUTE "nsslapd-plugin-binddn-tracking"

#define CONFIG_CONFIG_ATTRIBUTE "nsslapd-config"
#define CONFIG_INSTDIR_ATTRIBUTE "nsslapd-instancedir"
#define CONFIG_SCHEMADIR_ATTRIBUTE "nsslapd-schemadir"
#define CONFIG_LOCKDIR_ATTRIBUTE "nsslapd-lockdir"
#define CONFIG_TMPDIR_ATTRIBUTE "nsslapd-tmpdir"
#define CONFIG_CERTDIR_ATTRIBUTE "nsslapd-certdir"
#define CONFIG_LDIFDIR_ATTRIBUTE "nsslapd-ldifdir"
#define CONFIG_BAKDIR_ATTRIBUTE "nsslapd-bakdir"
#define CONFIG_SASLPATH_ATTRIBUTE "nsslapd-saslpath"
#define CONFIG_RUNDIR_ATTRIBUTE "nsslapd-rundir"
#define CONFIG_SSLCLIENTAUTH_ATTRIBUTE "nsslapd-SSLclientAuth"
#define CONFIG_SSL_CHECK_HOSTNAME_ATTRIBUTE "nsslapd-ssl-check-hostname"
#define CONFIG_HASH_FILTERS_ATTRIBUTE "nsslapd-hash-filters"
#define CONFIG_OUTBOUND_LDAP_IO_TIMEOUT_ATTRIBUTE "nsslapd-outbound-ldap-io-timeout"
#define CONFIG_FORCE_SASL_EXTERNAL_ATTRIBUTE "nsslapd-force-sasl-external"
#define CONFIG_ENTRYUSN_GLOBAL	"nsslapd-entryusn-global"
#define CONFIG_ENTRYUSN_IMPORT_INITVAL	"nsslapd-entryusn-import-initval"
#define CONFIG_ALLOWED_TO_DELETE_ATTRIBUTE	"nsslapd-allowed-to-delete-attrs"
#define CONFIG_DEFAULT_NAMING_CONTEXT "nsslapd-defaultnamingcontext"
#define CONFIG_DISK_MONITORING "nsslapd-disk-monitoring"
#define CONFIG_DISK_THRESHOLD "nsslapd-disk-monitoring-threshold"
#define CONFIG_DISK_GRACE_PERIOD "nsslapd-disk-monitoring-grace-period"
#define CONFIG_DISK_PRESERVE_LOGGING "nsslapd-disk-monitoring-preserve-logging"
#define CONFIG_DISK_LOGGING_CRITICAL "nsslapd-disk-monitoring-logging-critical"
#define CONFIG_NDN_CACHE "nsslapd-ndn-cache-enabled"
#define CONFIG_NDN_CACHE_SIZE "nsslapd-ndn-cache-max-size"
#define CONFIG_ALLOWED_SASL_MECHS "nsslapd-allowed-sasl-mechanisms"
#define CONFIG_IGNORE_VATTRS "nsslapd-ignore-virtual-attrs"
#define CONFIG_SASL_MAPPING_FALLBACK "nsslapd-sasl-mapping-fallback"
#define CONFIG_SASL_MAXBUFSIZE "nsslapd-sasl-max-buffer-size"
#define CONFIG_SEARCH_RETURN_ORIGINAL_TYPE "nsslapd-search-return-original-type-switch"
#define CONFIG_ENABLE_TURBO_MODE "nsslapd-enable-turbo-mode"
#define CONFIG_CONNECTION_BUFFER "nsslapd-connection-buffer"
#define CONFIG_CONNECTION_NOCANON "nsslapd-connection-nocanon"

#ifdef MEMPOOL_EXPERIMENTAL
#define CONFIG_MEMPOOL_SWITCH_ATTRIBUTE "nsslapd-mempool"
#define CONFIG_MEMPOOL_MAXFREELIST_ATTRIBUTE "nsslapd-mempool-maxfreelist"
#endif /* MEMPOOL_EXPERIMENTAL */

/* flag used to indicate that the change to the config parameter should be saved */
#define CONFIG_APPLY 1

#define SLAPI_CFG_USE_RWLOCK 0
#if SLAPI_CFG_USE_RWLOCK == 0
#define CFG_LOCK_READ(cfg)    PR_Lock(cfg->cfg_lock)
#define CFG_UNLOCK_READ(cfg)  PR_Unlock(cfg->cfg_lock)
#define CFG_LOCK_WRITE(cfg)   PR_Lock(cfg->cfg_lock)
#define CFG_UNLOCK_WRITE(cfg) PR_Unlock(cfg->cfg_lock)
#else
#define CFG_LOCK_READ(cfg)    slapi_rwlock_rdlock(cfg->cfg_rwlock)
#define CFG_UNLOCK_READ(cfg)  slapi_rwlock_unlock(cfg->cfg_rwlock)
#define CFG_LOCK_WRITE(cfg)   slapi_rwlock_wrlock(cfg->cfg_rwlock)
#define CFG_UNLOCK_WRITE(cfg) slapi_rwlock_unlock(cfg->cfg_rwlock)
#endif

#ifdef ATOMIC_GETSET_ONOFF
#define CFG_ONOFF_LOCK_READ(cfg)
#define CFG_ONOFF_UNLOCK_READ(cfg)
#define CFG_ONOFF_LOCK_WRITE(cfg)
#define CFG_ONOFF_UNLOCK_WRITE(cfg)
#else
#define CFG_ONOFF_LOCK_READ(cfg) CFG_LOCK_READ(cfg)
#define CFG_ONOFF_UNLOCK_READ(cfg) CFG_UNLOCK_READ(cfg)
#define CFG_ONOFF_LOCK_WRITE(cfg) CFG_LOCK_WRITE(cfg)
#define CFG_ONOFF_UNLOCK_WRITE(cfg) CFG_UNLOCK_WRITE(cfg)
#endif

#define REFER_MODE_OFF 0 
#define REFER_MODE_ON 1

#define MAX_ALLOWED_TIME_IN_SECS	2147483647

typedef struct _slapdFrontendConfig {
#if SLAPI_CFG_USE_RWLOCK == 1
  Slapi_RWLock     *cfg_rwlock;       /* read/write lock to serialize access */
#else
  PRLock           *cfg_lock;
#endif
  struct pw_scheme *rootpwstoragescheme;
  slapi_onoff_t accesscontrol;
  int groupevalnestlevel;
  int idletimeout;
  slapi_int_t ioblocktimeout;
  slapi_onoff_t lastmod;
#if !defined(_WIN32) && !defined(AIX) 
  int maxdescriptors;
#endif /* !_WIN32 && !AIX */
  int conntablesize;
  slapi_int_t maxthreadsperconn;
  int outbound_ldap_io_timeout;
  slapi_onoff_t nagle;
  int port;
  slapi_onoff_t readonly;
  int reservedescriptors;
  slapi_onoff_t schemacheck;
  slapi_onoff_t schemamod;
  slapi_onoff_t syntaxcheck;
  slapi_onoff_t syntaxlogging;
  slapi_onoff_t dn_validate_strict;
  slapi_onoff_t ds4_compatible_schema;
  slapi_onoff_t schema_ignore_trailing_spaces;
  int secureport;
  slapi_onoff_t security;
  int SSLclientAuth;
  slapi_onoff_t ssl_check_hostname;
  int validate_cert;
  int sizelimit;
  int SNMPenabled;
  char *SNMPdescription;
  char *SNMPorganization;
  char *SNMPlocation;
  char *SNMPcontact;
  int threadnumber;
  int timelimit;
  char *accesslog;
  struct berval **defaultreferral;
  char *encryptionalias;
  char *errorlog;
  char *listenhost;
  int snmp_index;
#ifndef _WIN32
  char *localuser;
#endif /* _WIN32 */
  char *localhost;
  char *rootdn;
  char *rootpw;
  char *securelistenhost;
  char *srvtab;
  char *SSL3ciphers;
  char *userat;
  char *useroc;
  char *versionstring;
  char **backendconfig;
  char **include;
  char **plugin;
  slapi_onoff_t plugin_track;
  struct pw_scheme *pw_storagescheme;

  slapi_onoff_t pwpolicy_local;
  slapi_onoff_t pw_is_global_policy;
  passwdPolicy pw_policy;

  /* ACCESS LOG */
  slapi_onoff_t accesslog_logging_enabled;
  char *accesslog_mode;
  int  accesslog_maxnumlogs;
  int  accesslog_maxlogsize;
  slapi_onoff_t accesslog_rotationsync_enabled;
  int  accesslog_rotationsynchour;
  int  accesslog_rotationsyncmin;
  int  accesslog_rotationtime;
  char *accesslog_rotationunit;
  int  accesslog_maxdiskspace;
  int  accesslog_minfreespace;
  int  accesslog_exptime;
  char *accesslog_exptimeunit;
  int	accessloglevel;
  slapi_onoff_t accesslogbuffering;
  slapi_onoff_t csnlogging;

   /* ERROR LOG */
  slapi_onoff_t errorlog_logging_enabled;
  char *errorlog_mode;
  int  errorlog_maxnumlogs;
  int  errorlog_maxlogsize;
  slapi_onoff_t errorlog_rotationsync_enabled;
  int  errorlog_rotationsynchour;
  int  errorlog_rotationsyncmin;
  int  errorlog_rotationtime;
  char *errorlog_rotationunit;
  int  errorlog_maxdiskspace;
  int  errorlog_minfreespace;
  int  errorlog_exptime;
  char *errorlog_exptimeunit;
  int	errorloglevel;

  /* AUDIT LOG */
  char *auditlog;		/* replication audit file */
  int  auditloglevel;
  slapi_onoff_t auditlog_logging_enabled;
  char *auditlog_mode;
  int  auditlog_maxnumlogs;
  int  auditlog_maxlogsize;
  slapi_onoff_t auditlog_rotationsync_enabled;
  int  auditlog_rotationsynchour;
  int  auditlog_rotationsyncmin;
  int  auditlog_rotationtime;
  char *auditlog_rotationunit;
  int  auditlog_maxdiskspace;
  int  auditlog_minfreespace;
  int  auditlog_exptime;
  char *auditlog_exptimeunit;
  slapi_onoff_t auditlog_logging_hide_unhashed_pw;

  slapi_onoff_t return_exact_case;	/* Return attribute names with the same case
                                       as they appear in at.conf */

  slapi_onoff_t result_tweak;
  char *refer_url;		/* for referral mode */
  int refer_mode;       /* for quick test */
  int	slapd_type;		/* Directory type; Full or Lite */
  
  ber_len_t maxbersize; /* Maximum BER element size we'll accept */
  slapi_int_t max_filter_nest_level;/* deepest nested filter we will accept */
  slapi_onoff_t enquote_sup_oc;       /* put single quotes around an oc's
                                         superior oc in cn=schema */

  char *certmap_basedn;	    /* Default Base DN for certmap */

  char *workingdir;	/* full path of directory before detach */
  char *configdir;  /* full path name of directory containing configuration files */
  char *schemadir;  /* full path name of directory containing schema files */
  char *instancedir;/* full path name of instance directory */
  char *lockdir;    /* full path name of directory containing lock files */
  char *tmpdir;     /* full path name of directory containing tmp files */
  char *certdir;    /* full path name of directory containing cert files */
  char *ldifdir;    /* full path name of directory containing ldif files */
  char *bakdir;     /* full path name of directory containing bakup files */
  char *rundir;     /* where pid, snmp stats, and ldapi files go */
  char *saslpath;   /* full path name of directory containing sasl plugins */
  slapi_onoff_t attrname_exceptions;  /* if true, allow questionable attribute names */
  slapi_onoff_t rewrite_rfc1274;		/* return attrs for both v2 and v3 names */
  char *schemareplace;		/* see CONFIG_SCHEMAREPLACE_* #defines below */
  char *ldapi_filename;		/* filename for ldapi socket */
  slapi_onoff_t ldapi_switch;             /* switch to turn ldapi on/off */
  slapi_onoff_t ldapi_bind_switch;        /* switch to turn ldapi auto binding on/off */
  char *ldapi_root_dn;          /* DN to map root to over LDAPI */
  slapi_onoff_t ldapi_map_entries;        /* turns ldapi entry bind mapping on/off */
  char *ldapi_uidnumber_type;   /* type that contains uid number */
  char *ldapi_gidnumber_type;   /* type that contains gid number */
  char *ldapi_search_base_dn;   /* base dn to search for mapped entries */
  char *ldapi_auto_dn_suffix;   /* suffix to be appended to auto gen DNs */
  slapi_onoff_t slapi_counters;           /* switch to turn slapi_counters on/off */
  slapi_onoff_t allow_unauth_binds;       /* switch to enable/disable unauthenticated binds */
  slapi_onoff_t require_secure_binds;	/* switch to require simple binds to use a secure channel */
  slapi_onoff_t allow_anon_access;	/* switch to enable/disable anonymous access */
  int localssf;			/* the security strength factor to assign to local conns (ldapi) */
  int minssf;			/* minimum security strength factor (for SASL and SSL/TLS) */
  slapi_onoff_t minssf_exclude_rootdse; /* ON: minssf is ignored when searching rootdse */
  size_t maxsasliosize;         /* limit incoming SASL IO packet size */
  char *anon_limits_dn;		/* template entry for anonymous resource limits */
#ifndef _WIN32
  struct passwd *localuserinfo; /* userinfo of localuser */
#endif /* _WIN32 */
#ifdef MEMPOOL_EXPERIMENTAL
  slapi_onoff_t mempool_switch;           /* switch to turn memory pool on/off */
  int mempool_maxfreelist;      /* max free list length per memory pool item */
  long system_page_size;		/* system page size */
  int system_page_bits;			/* bit count to shift the system page size */
#endif /* MEMPOOL_EXPERIMENTAL */
  slapi_onoff_t force_sasl_external;      /* force SIMPLE bind to be SASL/EXTERNAL if client cert credentials were supplied */
  slapi_onoff_t entryusn_global;          /* Entry USN: Use global counter */
  char *allowed_to_delete_attrs;/* list of config attrs allowed to delete */
  char *entryusn_import_init;   /* Entry USN: determine the initital value of import */
  int pagedsizelimit;
  char *default_naming_context; /* Default naming context (normalized) */
  char *allowed_sasl_mechs;     /* comma/space separated list of allowed sasl mechs */
  int sasl_max_bufsize;         /* The max receive buffer size for SASL */

  /* disk monitoring */
  slapi_onoff_t disk_monitoring;
  int disk_threshold;
  int disk_grace_period;
  slapi_onoff_t disk_preserve_logging;
  slapi_onoff_t disk_logging_critical;

  /* normalized dn cache */
  slapi_onoff_t ndn_cache_enabled;
  size_t ndn_cache_max_size;

  slapi_onoff_t return_orig_type; /* if on, search returns original type set in attr list */

  /* atomic settings */
  Slapi_Counter *ignore_vattrs;
  slapi_onoff_t sasl_mapping_fallback;
  slapi_onoff_t unhashed_pw_switch;	/* switch to on/off/nolog unhashed pw */
  slapi_onoff_t enable_turbo_mode;
  slapi_int_t connection_buffer; /* values are CONNECTION_BUFFER_* below */
  slapi_onoff_t connection_nocanon; /* if "on" sets LDAP_OPT_X_SASL_NOCANON */
} slapdFrontendConfig_t;

/* possible values for slapdFrontendConfig_t.schemareplace */
#define CONFIG_SCHEMAREPLACE_STR_OFF				"off"
#define CONFIG_SCHEMAREPLACE_STR_ON					"on"
#define CONFIG_SCHEMAREPLACE_STR_REPLICATION_ONLY	"replication-only"

#define CONNECTION_BUFFER_OFF 0
#define CONNECTION_BUFFER_ON 1
#define CONNECTION_BUFFER_ADAPT 2

 
slapdFrontendConfig_t *getFrontendConfig();

int slapd_bind_local_user(Connection *conn);

/* LP: NO_TIME cannot be -1, it generates wrong GeneralizedTime
 * And causes some errors on AIX also 
 */
/* #if defined( XP_WIN32 ) */
#define NO_TIME (time_t)0 /* cannot be -1, NT's localtime( -1 ) returns NULL */
/* #else */
/* #define NO_TIME (time_t)-1 / * a value that time() does not return  */
/* #endif */
#define NOT_FIRST_TIME (time_t)1 /* not the first logon */
#define SLAPD_END_TIME (time_t)2147483647  /* (2^31)-1, in 2038 */

extern char	*attr_dataversion;
#define	ATTR_DATAVERSION				"dataVersion"
#define ATTR_WITH_OCTETSTRING_SYNTAX		"userPassword"

#define SLAPD_SNMP_UPDATE_INTERVAL (10 * 1000) /* 10 seconds */

#ifndef LDAP_AUTH_KRBV41
#define LDAP_AUTH_KRBV41	0x81L
#endif
#ifndef LDAP_AUTH_KRBV42
#define LDAP_AUTH_KRBV42	0x82L
#endif

/* for timing certain operations */
#ifdef USE_TIMERS

#ifndef _WIN32
#include <sys/time.h>
#ifdef LINUX
#define GTOD(t) gettimeofday(t, NULL)
#else
#define GTOD(t) gettimeofday(t)
#endif
#define TIMER_DECL(x)   struct timeval x##_start, x##_end
#define TIMER_START(x)  GTOD(&(x##_start))
#define TIMER_STOP(x)   GTOD(&(x##_end))
#define TIMER_GET_US(x) (unsigned)(((x##_end).tv_sec - (x##_start).tv_sec)  * 100000L + \
                         ((x##_end).tv_usec - (x##_start).tv_usec))
#else
#define TIMER_DECL(x)   LARGE_INTEGER x##_freq, x##_start, x##_end
#define TIMER_START(x)  do { \
    QueryPerformanceFrequency(&(x##_freq)); \
    QueryPerformanceCounter(&(x##_start)); \
} while(0)
#define TIMER_STOP(x)   QueryPerformanceCounter(&(x##_end))
#define TIMER_GET_US(x) (unsigned int)((x##_end.QuadPart - x##_start.QuadPart) / x##_freq.QuadPart)
#endif   /* _WIN32 */

#define TIMER_AVG_DECL(x) \
    TIMER_DECL(x); static unsigned int x##_total, x##_count
#define TIMER_AVG_START(x) \
    TIMER_START(x)
#define TIMER_AVG_STOP(x) do { \
    TIMER_STOP(x); \
    if (TIMER_GET_US(x) < 10000) { \
        x##_count++; \
        x##_total += TIMER_GET_US(x); \
    } \
} while (0)
#define TIMER_AVG_GET_US(x)     (unsigned int)(x##_total / x##_count)
#define TIMER_AVG_CHECK(x) do { \
    if (x##_count >= 1000) { \
        printf("timer %s: %d\n", #x, TIMER_AVG_GET_US(x)); \
        x##_total = x##_count = 0; \
    } \
} while (0)

#else

#define TIMER_DECL(x)
#define TIMER_START(x)
#define TIMER_STOP(x)
#define TIMER_GET_US(x) 0L
 
#endif   /* USE_TIMERS */
 
#define LDIF_CSNPREFIX_MAXLENGTH 6 /* sizeof(xxcsn-) */

#include "intrinsics.h"

/* printkey: import & export */
#define	EXPORT_PRINTKEY			0x1
#define	EXPORT_NOWRAP			0x2
#define	EXPORT_APPENDMODE		0x4
#define	EXPORT_MINIMAL_ENCODING	0x8
#define	EXPORT_ID2ENTRY_ONLY	0x10
#define	EXPORT_NOVERSION		0x20
#define	EXPORT_APPENDMODE_1		0x40
#define	EXPORT_INTERNAL			0x100

#define MTN_CONTROL_USE_ONE_BACKEND_OID	"2.16.840.1.113730.3.4.14"
#define MTN_CONTROL_USE_ONE_BACKEND_EXT_OID	"2.16.840.1.113730.3.4.20"
#if defined(USE_OLD_UNHASHED)
#define PSEUDO_ATTR_UNHASHEDUSERPASSWORD_OID "2.16.840.1.113730.3.1.2110"
#endif

/* virtualListViewError is a relatively new concept that was added long 
 * after we implemented VLV. Until added to LDAP SDK, we define 
 * virtualListViewError here.  Once it's added, this define would go away. */
#ifndef LDAP_VIRTUAL_LIST_VIEW_ERROR
#define LDAP_VIRTUAL_LIST_VIEW_ERROR    0x4C      /* 76 */
#endif

#define CHAR_OCTETSTRING (char)0x04

/* copied from replication/repl5.h */
#define RUV_STORAGE_ENTRY_UNIQUEID "ffffffff-ffffffff-ffffffff-ffffffff"

#endif /* _slap_h_ */

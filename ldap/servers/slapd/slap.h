/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * Contributors:
 *   Hewlett-Packard Development Company, L.P.
 *     Bugfix for bug #195302
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#pragma once

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* slap.h - stand alone ldap server include file */

#ifndef _SLDAPD_H_
#define _SLDAPD_H_

/* Used by SSL and DES plugin */
#ifdef NEED_TOK_PBE
static char tokPBE[34] = "Communicator Generic Crypto Svcs";
static char ptokPBE[34] = "Internal (Software) Token        ";
#endif

/*
 * The slapd executable can function in on of several modes.
 */
#define SLAPD_EXEMODE_UNKNOWN          0
#define SLAPD_EXEMODE_SLAPD            1
#define SLAPD_EXEMODE_DB2LDIF          2
#define SLAPD_EXEMODE_LDIF2DB          3
#define SLAPD_EXEMODE_DB2ARCHIVE       4
#define SLAPD_EXEMODE_ARCHIVE2DB       5
#define SLAPD_EXEMODE_DBTEST           6
#define SLAPD_EXEMODE_DB2INDEX         7
#define SLAPD_EXEMODE_REFERRAL         8
#define SLAPD_EXEMODE_SUFFIX2INSTANCE  9
#define SLAPD_EXEMODE_PRINTVERSION    10
#define SLAPD_EXEMODE_UPGRADEDB       11
#define SLAPD_EXEMODE_DBVERIFY        12
#define SLAPD_EXEMODE_UPGRADEDNFORMAT 13

#define DEFBACKEND_TYPE "default"
#define DEFBACKEND_NAME "DirectoryServerDefaultBackend"

#define LDAP_SYSLOG
#include <syslog.h>
#define RLIM_TYPE int
#include <poll.h>
#define POLL_STRUCT PRPollDesc
#define POLL_FN PR_Poll

#include <stdio.h> /* for FILE */
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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <time.h> /* For timespec definitions */

/* Provides our int types and platform specific requirements. */
#include <slapi_pal.h>

#define LOG_INTERNAL_OP_CON_ID "Internal"
#define LOG_INTERNAL_OP_OP_ID  -1

#define MAX_SERVICE_NAME 25

#define SLAPD_TYPICAL_ATTRIBUTE_NAME_MAX_LENGTH 256

typedef struct symbol_t
{
    const char *name;
    unsigned number;
} symbol_t;

#define SLAPD_SSLCLIENTAUTH_OFF      0
#define SLAPD_SSLCLIENTAUTH_ALLOWED  1 /* server asks for cert, but client need not send one */
#define SLAPD_SSLCLIENTAUTH_REQUIRED 2 /* server will refuse SSL session unless client sends cert */

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
#include "portable.h"
#include "disconnect_errors.h"

#include "csngen.h"
#include "uuid.h"

/* Because we provide getFrontendConfig, and that contains localuserinfo, we
 * need to provide pwd.h to allow resolution of the passwd struct.
 */
#include <pwd.h>

#ifdef WITH_SYSTEMD
#ifdef HAVE_JOURNALD
#include <systemd/sd-journal.h>
#endif
#include <systemd/sd-daemon.h>
#endif

#if defined(OS_solaris)
#include <thread.h>
#define GET_THREAD_ID() thr_self()
#else
#include <pthread.h>
#define GET_THREAD_ID() pthread_self()
#endif

/*
 * XXXmcs: these are defined by ldap.h or ldap-extension.h,
 * but only in a newer release than we use with DS today.
 */
#ifndef LDAP_CONTROL_AUTH_RESPONSE
#define LDAP_CONTROL_AUTH_RESPONSE "2.16.840.1.113730.3.4.15"
#endif
#ifndef LDAP_CONTROL_REAL_ATTRS_ONLY
#define LDAP_CONTROL_REAL_ATTRS_ONLY "2.16.840.1.113730.3.4.17"
#endif
#ifndef LDAP_CONTROL_VIRT_ATTRS_ONLY
#define LDAP_CONTROL_VIRT_ATTRS_ONLY "2.16.840.1.113730.3.4.19"
#endif
#ifndef LDAP_CONTROL_GET_EFFECTIVE_RIGHTS
#define LDAP_CONTROL_GET_EFFECTIVE_RIGHTS "1.3.6.1.4.1.42.2.27.9.5.2"
#endif

/* PAGED RESULTS control (shared by request and response) */
#ifndef LDAP_CONTROL_PAGEDRESULTS
#define LDAP_CONTROL_PAGEDRESULTS "1.2.840.113556.1.4.319"
#endif

/* LDAP Subentries Control (RFC 3672) */
#ifndef LDAP_CONTROL_SUBENTRIES
#define LDAP_CONTROL_SUBENTRIES	"1.3.6.1.4.1.4203.1.10.1"
#endif

#define SLAPD_VENDOR_NAME VENDOR
#define SLAPD_VERSION_STR CAPBRAND "-Directory/" DS_PACKAGE_VERSION
#define SLAPD_SHORT_VERSION_STR DS_PACKAGE_VERSION

typedef void (*VFP)(void *);
typedef void (*VFPP)(void **);
typedef void (*VFP0)(void);

#if defined(__GNUC__) && (((__GNUC__ == 4) && (__GNUC_MINOR__ >= 4)) || (__GNUC__ > 4))
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#endif
typedef void (*VFPV)(); /* takes undefined arguments */
#if defined(__GNUC__) && (((__GNUC__ == 4) && (__GNUC_MINOR__ >= 4)) || (__GNUC__ > 4))
#pragma GCC diagnostic pop
#endif


#define LDAPI_INTERNAL 1
#include "slapi-private.h"
#include "pw.h"

/*
 * call the appropriate signal() function.
 */
#if defined(hpux)
/*
 * we should not mix POSIX signal library function (sigaction)
 * with SYSV's (sigset) on IRIX.  nspr uses POSIX internally.
 */
#define SIGNAL(s, a) signal2sigaction(s, (void *)a)
#elif (defined(SYSV) || defined(aix))
#define SIGNAL sigset
#else
#define SIGNAL signal
#endif

/*
 * SLAPD_PR_WOULD_BLOCK_ERROR() returns non-zero if prerrno is an NSPR
 *    error code that indicates a temporary non-blocking I/O error,
 *    e.g., PR_WOULD_BLOCK_ERROR.
 */
#define SLAPD_PR_WOULD_BLOCK_ERROR(prerrno) \
    ((prerrno) == PR_WOULD_BLOCK_ERROR || (prerrno) == PR_IO_TIMEOUT_ERROR)

/*
 * SLAPD_SYSTEM_WOULD_BLOCK_ERROR() returns non-zero if syserrno is an OS
 *    error code that indicates a temporary non-blocking I/O error,
 *    e.g., EAGAIN.
 */
#define SLAPD_SYSTEM_WOULD_BLOCK_ERROR(syserrno) \
    ((syserrno) == EAGAIN || (syserrno) == EWOULDBLOCK)


#define LDAP_ON 1
#define LDAP_OFF 0
#define LDAP_UNDEFINED (-1)

#ifndef SLAPD_INVALID_SOCKET
#define SLAPD_INVALID_SOCKET 0
#endif

#define SLAPD_INVALID_SOCKET_INDEX (-1)

#define ETIME_BUFSIZ 42 /* room for struct timespec */

/* ============================================================================
 *       CONFIGURATION DEFAULTS
 *
 * All our server defaults are defined here. Sometimes these are in pairs of a
 * type and a str type. These are largely consumed in libglobs.c
 * The reason for their inclusion here is cleanliness of libglobs, centralisation
 * There were a few values that differed between the libglobs and the define,
 * so this also helps to eliminate that.
 */

#define SLAPD_DEFAULT_FILE_MODE S_IRUSR | S_IWUSR
#define SLAPD_DEFAULT_DIR_MODE S_IRWXU
#define SLAPD_DEFAULT_IDLE_TIMEOUT 3600 /* seconds - 0 == never */
#define SLAPD_DEFAULT_IDLE_TIMEOUT_STR "3600"
#define SLAPD_DEFAULT_SIZELIMIT 2000 /* use -1 for no limit */
#define SLAPD_DEFAULT_SIZELIMIT_STR "2000"
#define SLAPD_DEFAULT_TIMELIMIT 3600 /* use -1 for no limit */
#define SLAPD_DEFAULT_TIMELIMIT_STR "3600"
#define SLAPD_DEFAULT_LOOKTHROUGHLIMIT 5000 /* use -1 for no limit */
#define SLAPD_DEFAULT_GROUPNESTLEVEL 5
#define SLAPD_DEFAULT_MAX_FILTER_NEST_LEVEL 40 /* use -1 for no limit */
#define SLAPD_DEFAULT_MAX_SASLIO_SIZE 2097152  /* 2MB in bytes.  Use -1 for no limit */
#define SLAPD_DEFAULT_MAX_SASLIO_SIZE_STR "2097152"
#define SLAPD_DEFAULT_IOBLOCK_TIMEOUT 10000 /* 10 second in ms */
#define SLAPD_DEFAULT_IOBLOCK_TIMEOUT_STR "10000"
#define SLAPD_DEFAULT_OUTBOUND_LDAP_IO_TIMEOUT 300000 /* 5 minutes in ms */
#define SLAPD_DEFAULT_OUTBOUND_LDAP_IO_TIMEOUT_STR "300000"
#define SLAPD_DEFAULT_RESERVE_FDS 64
#define SLAPD_DEFAULT_RESERVE_FDS_STR "64"
#define SLAPD_DEFAULT_MAX_THREADS -1 /* connection pool threads */
#define SLAPD_DEFAULT_MAX_THREADS_STR "-1"
#define SLAPD_DEFAULT_MAX_THREADS_PER_CONN 5 /* allowed per connection */
#define SLAPD_DEFAULT_MAX_THREADS_PER_CONN_STR "5"
#define SLAPD_DEFAULT_MAX_BERSIZE_STR "0"
#define SLAPD_DEFAULT_SCHEMA_IGNORE_TRAILING_SPACES LDAP_OFF
#define SLAPD_DEFAULT_LOCAL_SSF 71 /* assume local connections are secure */
#define SLAPD_DEFAULT_LOCAL_SSF_STR "71"
#define SLAPD_DEFAULT_MIN_SSF 0 /* allow unsecured connections (no privacy or integrity) */
#define SLAPD_DEFAULT_MIN_SSF_STR "0"
#define SLAPD_DEFAULT_SASL_MAXBUFSIZE 2097152
#define SLAPD_DEFAULT_SASL_MAXBUFSIZE_STR "2097152"
#define SLAPD_DEFAULT_MAXBERSIZE 2097152
#define SLAPD_DEFAULT_MAXBERSIZE_STR "2097152"
#define SLAPD_DEFAULT_MAXSIMPLEPAGED_PER_CONN (-1)
#define SLAPD_DEFAULT_MAXSIMPLEPAGED_PER_CONN_STR "-1"
#define SLAPD_DEFAULT_LDAPSSOTOKEN_TTL 3600
#define SLAPD_DEFAULT_LDAPSSOTOKEN_TTL_STR "3600"

#define SLAPD_DEFAULT_NDN_SIZE     20971520
#define SLAPD_DEFAULT_NDN_SIZE_STR "20971520"

#define SLAPD_DEFAULT_DIRECTORY_MANAGER "cn=Directory Manager"
#define SLAPD_DEFAULT_UIDNUM_TYPE       "uidNumber"
#define SLAPD_DEFAULT_GIDNUM_TYPE       "gidNumber"
#define SLAPD_ENTRYUSN_IMPORT_INIT      "0"
#define SLAPD_INIT_LOGGING_BACKEND_INTERNAL "dirsrv-log"

#define SLAPD_DEFAULT_SSLCLIENTAUTH SLAPD_SSLCLIENTAUTH_ALLOWED
#define SLAPD_DEFAULT_SSLCLIENTAUTH_STR "allowed"

#define SLAPD_DEFAULT_ALLOW_ANON_ACCESS SLAPD_ANON_ACCESS_ON
#define SLAPD_DEFAULT_ALLOW_ANON_ACCESS_STR "on"

#define SLAPD_DEFAULT_VALIDATE_CERT SLAPD_VALIDATE_CERT_WARN
#define SLAPD_DEFAULT_VALIDATE_CERT_STR "warn"

#define SLAPD_DEFAULT_UNHASHED_PW_SWITCH SLAPD_UNHASHED_PW_OFF
#define SLAPD_DEFAULT_UNHASHED_PW_SWITCH_STR "off"

#define SLAPD_DEFAULT_LDAPI_SEARCH_BASE "dc=example,dc=com"
#define SLAPD_DEFAULT_LDAPI_AUTO_DN     "cn=peercred,cn=external,cn=auth"
#define SLAPD_DEFAULT_LDAPI_MAPPING_DN  "cn=auto_bind,cn=config"

#define SLAPD_SCHEMA_DN  "cn=schema"
#define SLAPD_CONFIG_DN  "cn=config"

#define SLAPD_INIT_LOG_MODE "600"
#define SLAPD_INIT_ACCESSLOG_ROTATIONUNIT    "day"
#define SLAPD_INIT_SECURITYLOG_ROTATIONUNIT  "week"
#define SLAPD_INIT_ERRORLOG_ROTATIONUNIT     "week"
#define SLAPD_INIT_AUDITLOG_ROTATIONUNIT     "week"
#define SLAPD_INIT_AUDITFAILLOG_ROTATIONUNIT "week"
#define SLAPD_INIT_LOG_EXPTIMEUNIT           "month"

#define SLAPD_DEFAULT_LOG_ROTATIONSYNCHOUR 0
#define SLAPD_DEFAULT_LOG_ROTATIONSYNCHOUR_STR "0"
#define SLAPD_DEFAULT_LOG_ROTATIONSYNCMIN 0
#define SLAPD_DEFAULT_LOG_ROTATIONSYNCMIN_STR "0"
#define SLAPD_DEFAULT_LOG_ROTATIONTIME 1
#define SLAPD_DEFAULT_LOG_ROTATIONTIME_STR "1"
#define SLAPD_DEFAULT_LOG_ACCESS_MAXNUMLOGS 10
#define SLAPD_DEFAULT_LOG_ACCESS_MAXNUMLOGS_STR "10"
#define SLAPD_DEFAULT_LOG_SECURITY_MAXNUMLOGS 10
#define SLAPD_DEFAULT_LOG_SECURITY_MAXNUMLOGS_STR "10"
#define SLAPD_DEFAULT_LOG_MAXNUMLOGS 2
#define SLAPD_DEFAULT_LOG_MAXNUMLOGS_STR "2"
#define SLAPD_DEFAULT_LOG_EXPTIME 1
#define SLAPD_DEFAULT_LOG_EXPTIME_STR "1"
/* This is in MB */
#define SLAPD_DEFAULT_LOG_ACCESS_MAXDISKSPACE 500
#define SLAPD_DEFAULT_LOG_ACCESS_MAXDISKSPACE_STR "500"
#define SLAPD_DEFAULT_LOG_SECURITY_MAXDISKSPACE 500
#define SLAPD_DEFAULT_LOG_SECURITY_MAXDISKSPACE_STR "500"
#define SLAPD_DEFAULT_LOG_MAXDISKSPACE 100
#define SLAPD_DEFAULT_LOG_MAXDISKSPACE_STR "100"
#define SLAPD_DEFAULT_LOG_MAXLOGSIZE 100
#define SLAPD_DEFAULT_LOG_MAXLOGSIZE_STR "100"
#define SLAPD_DEFAULT_LOG_MINFREESPACE 5
#define SLAPD_DEFAULT_LOG_MINFREESPACE_STR "5"

/* The default log levels:
 * (LDAP_DEBUG_ANY | LDAP_DEBUG_EMERG | LDAP_DEBUG_ALERT | LDAP_DEBUG_CRIT | LDAP_DEBUG_ERR |
 *  LDAP_DEBUG_WARNING | LDAP_DEBUG_NOTICE | LDAP_DEBUG_INFO)
 */
#define SLAPD_DEFAULT_ERRORLOG_LEVEL 266354688
#define SLAPD_DEFAULT_FE_ERRORLOG_LEVEL 16384 /* frontend log level */
#define SLAPD_DEFAULT_FE_ERRORLOG_LEVEL_STR "16384"
#define SLAPD_DEFAULT_ACCESSLOG_LEVEL 256
#define SLAPD_DEFAULT_ACCESSLOG_LEVEL_STR "256"
#define SLAPD_DEFAULT_STATLOG_LEVEL 0
#define SLAPD_DEFAULT_STATLOG_LEVEL_STR "0"
#define SLAPD_DEFAULT_SECURITYLOG_LEVEL 256
#define SLAPD_DEFAULT_SECURITYLOG_LEVEL_STR "256"

#define SLAPD_DEFAULT_DISK_THRESHOLD 2097152
#define SLAPD_DEFAULT_DISK_THRESHOLD_STR "2097152"
#define SLAPD_DEFAULT_DISK_GRACE_PERIOD 60
#define SLAPD_DEFAULT_DISK_GRACE_PERIOD_STR "60"

#define SLAPD_DEFAULT_PAGEDSIZELIMIT 0
#define SLAPD_DEFAULT_PAGEDSIZELIMIT_STR "0"
#define SLAPD_DEFAULT_MAXDESCRIPTORS 1048576
#define SLAPD_DEFAULT_MAXDESCRIPTORS_STR "1048576"
#define SLAPD_DEFAULT_MAX_FILTER_NEST_LEVEL 40
#define SLAPD_DEFAULT_MAX_FILTER_NEST_LEVEL_STR "40"
#define SLAPD_DEFAULT_GROUPEVALNESTLEVEL 0
#define SLAPD_DEFAULT_GROUPEVALNESTLEVEL_STR "0"
#define SLAPD_DEFAULT_SNMP_INDEX 0
#define SLAPD_DEFAULT_SNMP_INDEX_STR "0"
#define SLAPD_DEFAULT_NUM_LISTENERS 1
#define SLAPD_DEFAULT_NUM_LISTENERS_STR "1"

#define SLAPD_DEFAULT_PW_INHISTORY 6
#define SLAPD_DEFAULT_PW_INHISTORY_STR "6"
#define SLAPD_DEFAULT_PW_GRACELIMIT 0
#define SLAPD_DEFAULT_PW_GRACELIMIT_STR "0"
#define SLAPD_DEFAULT_PW_MINLENGTH 8
#define SLAPD_DEFAULT_PW_MINLENGTH_STR "8"
#define SLAPD_DEFAULT_PW_MINDIGITS 0
#define SLAPD_DEFAULT_PW_MINDIGITS_STR "0"
#define SLAPD_DEFAULT_PW_MINALPHAS 0
#define SLAPD_DEFAULT_PW_MINALPHAS_STR "0"
#define SLAPD_DEFAULT_PW_MINUPPERS 0
#define SLAPD_DEFAULT_PW_MINUPPERS_STR "0"
#define SLAPD_DEFAULT_PW_MINLOWERS 0
#define SLAPD_DEFAULT_PW_MINLOWERS_STR "0"
#define SLAPD_DEFAULT_PW_MINSPECIALS 0
#define SLAPD_DEFAULT_PW_MINSPECIALS_STR "0"
#define SLAPD_DEFAULT_PW_MIN8BIT 0
#define SLAPD_DEFAULT_PW_MIN8BIT_STR "0"
#define SLAPD_DEFAULT_PW_MAXREPEATS 0
#define SLAPD_DEFAULT_PW_MAXREPEATS_STR "0"
#define SLAPD_DEFAULT_PW_MINCATEGORIES 3
#define SLAPD_DEFAULT_PW_MINCATEGORIES_STR "3"
#define SLAPD_DEFAULT_PW_MINTOKENLENGTH 3
#define SLAPD_DEFAULT_PW_MINTOKENLENGTH_STR "3"
#define SLAPD_DEFAULT_PW_MAXAGE 8640000
#define SLAPD_DEFAULT_PW_MAXAGE_STR "8640000"
#define SLAPD_DEFAULT_PW_MINAGE 0
#define SLAPD_DEFAULT_PW_MINAGE_STR "0"
#define SLAPD_DEFAULT_PW_WARNING 86400
#define SLAPD_DEFAULT_PW_WARNING_STR "86400"
#define SLAPD_DEFAULT_PW_MAXFAILURE 3
#define SLAPD_DEFAULT_PW_MAXFAILURE_STR "3"
#define SLAPD_DEFAULT_PW_RESETFAILURECOUNT 600
#define SLAPD_DEFAULT_PW_RESETFAILURECOUNT_STR "600"
#define SLAPD_DEFAULT_PW_TPR_MAXUSE -1
#define SLAPD_DEFAULT_PW_TPR_MAXUSE_STR "-1"
#define SLAPD_DEFAULT_PW_TPR_DELAY_EXPIRE_AT -1
#define SLAPD_DEFAULT_PW_TPR_DELAY_EXPIRE_AT_STR "-1"
#define SLAPD_DEFAULT_PW_TPR_DELAY_VALID_FROM -1
#define SLAPD_DEFAULT_PW_TPR_DELAY_VALID_FROM_STR "-1"
#define SLAPD_DEFAULT_PW_LOCKDURATION 3600
#define SLAPD_DEFAULT_PW_LOCKDURATION_STR "3600"

#define SLAPD_DEFAULT_PW_MAX_SEQ_ATTRIBUTE 0
#define SLAPD_DEFAULT_PW_MAX_SEQ_ATTRIBUTE_STR "0"
#define SLAPD_DEFAULT_PW_MAX_SEQ_SETS_ATTRIBUTE 0
#define SLAPD_DEFAULT_PW_MAX_SEQ_SETS_ATTRIBUTE_STR "0"
#define SLAPD_DEFAULT_PW_MAX_CLASS_CHARS_ATTRIBUTE 0
#define SLAPD_DEFAULT_PW_MAX_CLASS_CHARS_ATTRIBUTE_STR "0"

#define SLAPD_DEFAULT_TCP_FIN_TIMEOUT 30
#define SLAPD_DEFAULT_TCP_FIN_TIMEOUT_STR "30"
#define SLAPD_DEFAULT_TCP_KEEPALIVE_TIME 300
#define SLAPD_DEFAULT_TCP_KEEPALIVE_TIME_STR "300"

#define SLAPD_DEFAULT_REFERRAL_CHECK_PERIOD 300
#define SLAPD_DEFAULT_REFERRAL_CHECK_PERIOD_STR "300"

#define MIN_THREADS 16
#define MAX_THREADS 512


/* Default password values. */

/* ================ END CONFIGURATION DEFAULTS ============================ */

#define EGG_OBJECT_CLASS "directory-team-extensible-object"
#define EGG_FILTER "(objectclass=directory-team-extensible-object)"

#define BE_LIST_SIZE 1000 /* used by mapping tree code to hold be_list stuff */

#define REPL_DBTYPE "ldbm"
#define REPL_DBTAG "repl"

#define ATTR_NETSCAPEMDSUFFIX "netscapemdsuffix"

#define PWD_PBE_DELIM '-'

#define REFERRAL_REMOVE_CMD "remove"

/* Filenames for DSE storage */
#define DSE_FILENAME "dse.ldif"
#define DSE_TMPFILE "dse.ldif.tmp"
#define DSE_BACKFILE "dse.ldif.bak"
#define DSE_STARTOKFILE "dse.ldif.startOK"
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
#define SLAPD_ANON_ACCESS_OFF 0
#define SLAPD_ANON_ACCESS_ON 1
#define SLAPD_ANON_ACCESS_ROOTDSE 2

/* Server certificate validation */
#define SLAPD_VALIDATE_CERT_OFF 0
#define SLAPD_VALIDATE_CERT_ON 1
#define SLAPD_VALIDATE_CERT_WARN 2

typedef int32_t slapi_onoff_t;
typedef int32_t slapi_int_t;

typedef enum _tls_check_crl_t {
    TLS_CHECK_NONE = 0,
    TLS_CHECK_PEER = 1,
    TLS_CHECK_ALL = 2,
} tls_check_crl_t;

typedef enum _slapi_special_filter_verify_t {
    SLAPI_STRICT = 0,
    SLAPI_WARN_SAFE = 1,
    SLAPI_WARN_UNSAFE = 2,
    SLAPI_OFF_UNSAFE = 3,
} slapi_special_filter_verify_t;

struct subfilt
{
    char *sf_type;
    char *sf_initial;
    char **sf_any;
    char *sf_final;
    void *sf_private; /* data private to syntax handler */
};

#include "filter.h" /* mr_filter_t */

/*
 * represents a search filter
 */
struct slapi_filter
{
    int f_flags;
    unsigned long f_choice; /* values taken from ldap.h */
    PRUint32 f_hash;        /* for quick comparisons */
    void *assigned_decoder;

    union
    {
        /* present */
        char *f_un_type;

        /* equality, lessorequal, greaterorequal, approx */
        struct ava f_un_ava;

        /* and, or, not */
        struct slapi_filter *f_un_complex;

        /* substrings */
        struct subfilt f_un_sub;

        /* extended -- v3 only */
        mr_filter_t f_un_extended;
    } f_un;
#define f_type f_un.f_un_type
#define f_ava f_un.f_un_ava
#define f_avtype f_un.f_un_ava.ava_type
#define f_avvalue f_un.f_un_ava.ava_value
#define f_and f_un.f_un_complex
#define f_or f_un.f_un_complex
#define f_not f_un.f_un_complex
#define f_list f_un.f_un_complex
#define f_sub f_un.f_un_sub
#define f_sub_type f_un.f_un_sub.sf_type
#define f_sub_initial f_un.f_un_sub.sf_initial
#define f_sub_any f_un.f_un_sub.sf_any
#define f_sub_final f_un.f_un_sub.sf_final
#define f_mr f_un.f_un_extended
#define f_mr_oid f_un.f_un_extended.mrf_oid
#define f_mr_type f_un.f_un_extended.mrf_type
#define f_mr_value f_un.f_un_extended.mrf_value
#define f_mr_dnAttrs f_un.f_un_extended.mrf_dnAttrs

    struct slapi_filter *f_next;
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
 *    {
 *    unsigned char flag;
 *    union single
 *    {
 *        struct slapi_value *va;
 *    };
 *    union multiple_array
 *    {
 *        short num;
 *        short max;
 *        struct slapi_value **va;
 *    };
 *    union multiple_tree
 *    {
 *        struct slapi_value_tree *vt;
 *    };
 */

/* It is a useless layer, always use the valuarray fast version */
#define VALUE_SORT_THRESHOLD 10
struct slapi_value_set
{
    size_t num;     /* The number of values in the array */
    size_t max;     /* The number of slots in the array */
    size_t *sorted; /* sorted array of indices, if NULL va is not sorted */
    struct slapi_value **va;
};

struct valuearrayfast
{
    int num; /* The number of values in the array */
    int max; /* The number of slots in the array */
    struct slapi_value **va;
};

struct bervals2free
{
    struct berval **bvals;
    struct bervals2free *next;
};

/*
 * represents an attribute instance (type + values + syntax)
 */

struct slapi_attr
{
    char *a_type;
    struct slapi_value_set a_present_values;
    unsigned long a_flags;        /* SLAPI_ATTR_FLAG_... */
    struct slapdplugin *a_plugin; /* for the attribute syntax */
    struct slapi_value_set a_deleted_values;
    struct bervals2free *a_listtofree; /* JCM: EVIL... For DS4 Slapi compatibility. */
    struct slapi_attr *a_next;
    CSN *a_deletioncsn;                  /* The point in time at which this attribute was last deleted */
    struct slapdplugin *a_mr_eq_plugin;  /* for the attribute EQUALITY matching rule, if any */
    struct slapdplugin *a_mr_ord_plugin; /* for the attribute ORDERING matching rule, if any */
    struct slapdplugin *a_mr_sub_plugin; /* for the attribute SUBSTRING matching rule, if any */
};

typedef struct oid_item
{
    char *oi_oid;
    struct slapdplugin *oi_plugin;
    struct oid_item *oi_next;
} oid_item_t;

/* schema extension item: X-ORIGIN, X-CSN, etc */
typedef struct schemaext
{
    char *term;
    char **values;
    int value_count;
    struct schemaext *next;
} schemaext;

/* attribute description (represents an attribute, but not the value) */
typedef struct asyntaxinfo
{
    char *asi_oid;                         /* OID */
    char *asi_name;                        /* normalized name */
    char **asi_aliases;                    /* alternative names */
    char *asi_desc;                        /* textual description */
    char *asi_superior;                    /* derived from */
    char *asi_mr_equality;                 /* equality matching rule */
    char *asi_mr_ordering;                 /* ordering matching rule */
    char *asi_mr_substring;                /* substring matching rule */
    schemaext *asi_extensions;             /* schema extensions (X-ORIGIN, X-?????, ...) */
    struct slapdplugin *asi_plugin;        /* syntax */
    char *asi_syntax_oid;                  /* syntax oid */
    unsigned long asi_flags;               /* SLAPI_ATTR_FLAG_... */
    int asi_syntaxlength;                  /* length associated w/syntax */
    uint64_t asi_refcnt;                   /* outstanding references */
    PRBool asi_marked_for_delete;          /* delete at next opportunity */
    struct slapdplugin *asi_mr_eq_plugin;  /* EQUALITY matching rule plugin */
    struct slapdplugin *asi_mr_sub_plugin; /* SUBSTR matching rule plugin */
    struct slapdplugin *asi_mr_ord_plugin; /* ORDERING matching rule plugin */
    struct asyntaxinfo *asi_next;
    struct asyntaxinfo *asi_prev;
} asyntaxinfo;

/*
 * Note: most of the asi_flags values are defined in slapi-plugin.h, but
 * these ones are private to the DS.
 */
#define SLAPI_ATTR_FLAG_OVERRIDE 0x0010             /* when adding a new attribute,   \
                                                     override the existing attribute, \
                                                     if any */
#define SLAPI_ATTR_FLAG_NOLOCKING 0x0020            /* the init code doesn't lock the \
                                                    tables */
#define SLAPI_ATTR_FLAG_KEEP 0x8000                 /* keep when replacing all */
#define SLAPI_ATTR_FLAG_SYNTAX_LOOKUP_DONE 0x010000 /* syntax lookup done, flag set */
#define SLAPI_ATTR_FLAG_SYNTAX_IS_DN 0x020000       /* syntax lookup done, flag set */

/* This is the type of the function passed into attr_syntax_enumerate_attrs */
typedef int (*AttrEnumFunc)(struct asyntaxinfo *asi, void *arg);
/* Possible return values for an AttrEnumFunc */
#define ATTR_SYNTAX_ENUM_NEXT 0   /* continue */
#define ATTR_SYNTAX_ENUM_STOP 1   /* halt the enumeration */
#define ATTR_SYNTAX_ENUM_REMOVE 2 /* unhash current node and continue */

/* flags for slapi_attr_syntax_normalize_ext */
#define ATTR_SYNTAX_NORM_ORIG_ATTR 0x1 /* a space and following characters are \
                                          removed from the given string */

/* This is the type of the function passed into plugin_syntax_enumerate */
typedef int (*SyntaxEnumFunc)(char **names, Slapi_PluginDesc *plugindesc, void *arg);

/* OIDs for some commonly used syntaxes */
#define BINARY_SYNTAX_OID             "1.3.6.1.4.1.1466.115.121.1.5"
#define BITSTRING_SYNTAX_OID          "1.3.6.1.4.1.1466.115.121.1.6"
#define BOOLEAN_SYNTAX_OID            "1.3.6.1.4.1.1466.115.121.1.7"
#define COUNTRYSTRING_SYNTAX_OID      "1.3.6.1.4.1.1466.115.121.1.11"
#define DN_SYNTAX_OID                 "1.3.6.1.4.1.1466.115.121.1.12"
#define DELIVERYMETHOD_SYNTAX_OID     "1.3.6.1.4.1.1466.115.121.1.14"
#define DIRSTRING_SYNTAX_OID          "1.3.6.1.4.1.1466.115.121.1.15"
#define ENHANCEDGUIDE_SYNTAX_OID      "1.3.6.1.4.1.1466.115.121.1.21"
#define FACSIMILE_SYNTAX_OID          "1.3.6.1.4.1.1466.115.121.1.22"
#define FAX_SYNTAX_OID                "1.3.6.1.4.1.1466.115.121.1.23"
#define GENERALIZEDTIME_SYNTAX_OID    "1.3.6.1.4.1.1466.115.121.1.24"
#define GUIDE_SYNTAX_OID              "1.3.6.1.4.1.1466.115.121.1.25"
#define IA5STRING_SYNTAX_OID          "1.3.6.1.4.1.1466.115.121.1.26"
#define INTEGER_SYNTAX_OID            "1.3.6.1.4.1.1466.115.121.1.27"
#define JPEG_SYNTAX_OID               "1.3.6.1.4.1.1466.115.121.1.28"
#define NAMEANDOPTIONALUID_SYNTAX_OID "1.3.6.1.4.1.1466.115.121.1.34"
#define NUMERICSTRING_SYNTAX_OID      "1.3.6.1.4.1.1466.115.121.1.36"
#define OID_SYNTAX_OID                "1.3.6.1.4.1.1466.115.121.1.38"
#define OCTETSTRING_SYNTAX_OID        "1.3.6.1.4.1.1466.115.121.1.40"
#define POSTALADDRESS_SYNTAX_OID      "1.3.6.1.4.1.1466.115.121.1.41"
#define PRINTABLESTRING_SYNTAX_OID    "1.3.6.1.4.1.1466.115.121.1.44"
#define TELEPHONE_SYNTAX_OID          "1.3.6.1.4.1.1466.115.121.1.50"
#define TELETEXTERMID_SYNTAX_OID      "1.3.6.1.4.1.1466.115.121.1.51"
#define TELEXNUMBER_SYNTAX_OID        "1.3.6.1.4.1.1466.115.121.1.52"
#define SPACE_INSENSITIVE_STRING_SYNTAX_OID "2.16.840.1.113730.3.7.1"

/* OIDs for some commonly used matching rules */
#define DNMATCH_OID              "2.5.13.1"   /* distinguishedNameMatch */
#define CASEIGNOREMATCH_OID      "2.5.13.2"   /* caseIgnoreMatch */
#define INTEGERMATCH_OID         "2.5.13.14"  /* integerMatch */
#define INTEGERORDERINGMATCH_OID "2.5.13.15"  /* integerOrderingMatch */
#define INTFIRSTCOMPMATCH_OID    "2.5.13.29"  /* integerFirstComponentMatch */
#define OIDFIRSTCOMPMATCH_OID    "2.5.13.30"  /* objectIdentifierFirstComponentMatch */

/* Names for some commonly used matching rules */
#define DNMATCH_NAME              "distinguishedNameMatch"
#define CASEIGNOREMATCH_NAME      "caseIgnoreMatch"
#define INTEGERMATCH_NAME         "integerMatch"
#define INTEGERORDERINGMATCH_NAME "integerOrderingMatch"
#define INTFIRSTCOMPMATCH_NAME    "integerFirstComponentMatch"
#define OIDFIRSTCOMPMATCH_NAME    "objectIdentifierFirstComponentMatch"

#define ATTR_STANDARD_STRING "Standard Attribute"
#define ATTR_USERDEF_STRING  "User Defined Attribute"
#define OC_STANDARD_STRING   "Standard ObjectClass"
#define OC_USERDEF_STRING    "User Defined ObjectClass"

/* modifiers used to define attributes */
#define ATTR_MOD_OPERATIONAL "operational"
#define ATTR_MOD_OVERRIDE    "override"
#define ATTR_MOD_SINGLE      "single"

/* extended operations supported by the server */
#define EXTOP_BULK_IMPORT_START_OID "2.16.840.1.113730.3.5.7"
#define EXTOP_BULK_IMPORT_DONE_OID  "2.16.840.1.113730.3.5.8"
#define EXTOP_PASSWD_OID            "1.3.6.1.4.1.4203.1.11.1"
#define EXTOP_LDAPSSOTOKEN_REQUEST_OID  "2.16.840.1.113730.3.5.14"
#define EXTOP_LDAPSSOTOKEN_RESPONSE_OID "2.16.840.1.113730.3.5.15"
#define EXTOP_LDAPSSOTOKEN_REVOKE_OID   "2.16.840.1.113730.3.5.16"

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
#define UID_SIZE 16 /* size of unique id in bytes */

/*
 * max 1G attr values per entry
 * in case, libdb returned bogus entry string from db (blackflag #623569)
 */
#define ENTRY_MAX_ATTRIBUTE_VALUE_COUNT 1073741824

typedef struct _entry_vattr Slapi_Vattr;
/*
 * represents an entry in core
 * WARNING, if you change this stucture you MUST update slapi_entry_size()
 * function
 */
struct slapi_entry
{
    struct slapi_dn e_sdn;        /* DN of this entry */
    struct slapi_rdn e_srdn;      /* RDN of this entry */
    char *e_uniqueid;             /* uniqueID of this entry */
    CSNSet *e_dncsnset;           /* The set of DN CSNs for this entry */
    CSN *e_maxcsn;                /* maximum CSN of the entry */
    Slapi_Attr *e_attrs;          /* list of attributes and values   */
    Slapi_Attr *e_deleted_attrs;  /* deleted list of attributes and values */
    Slapi_Vattr *e_virtual_attrs; /* cache of virtual attributes */
    uint32_t e_virtual_watermark; /* indicates cache consistency when compared
                                     to global watermark */
    Slapi_RWLock *e_virtual_lock; /* for access to cached vattrs */
    void *e_extension;            /* A list of entry object extensions */
    unsigned char e_flags;
    Slapi_Attr *e_aux_attrs;      /* Attr list used for upgrade */
};

struct attrs_in_extension
{
    char *ext_type;
    IFP ext_get;
    IFP ext_set;
    IFP ext_copy;
    IFP ext_get_size;
};

extern struct attrs_in_extension attrs_in_extension[];

/*
 * represents schema information for a database
 */
/* values for oc_flags (only space for 8 of these right now!) */
#define OC_FLAG_STANDARD_OC  1
#define OC_FLAG_USER_OC      2
#define OC_FLAG_REDEFINED_OC 4
#define OC_FLAG_OBSOLETE     8

/* values for oc_kind */
#define OC_KIND_ABSTRACT   0
#define OC_KIND_STRUCTURAL 1
#define OC_KIND_AUXILIARY  2


/* XXXmcs: ../plugins/cos/cos_cache.c has its own copy of this definition! */
struct objclass
{
    char *oc_name;     /* NAME */
    char *oc_desc;     /* DESC */
    char *oc_oid;      /* object identifier */
    char *oc_superior; /* SUP -- XXXmcs: should be an array */
    PRUint8 oc_kind;   /* ABSTRACT/STRUCTURAL/AUXILIARY */
    PRUint8 oc_flags;  /* misc. flags, e.g., OBSOLETE */
    char **oc_required;
    char **oc_allowed;
    char **oc_orig_required;  /* MUST */
    char **oc_orig_allowed;   /* MAY */
    schemaext *oc_extensions; /* schema extensions (X-ORIGIN, X-?????, ...) */
    struct objclass *oc_next;
};

struct matchingRuleList
{
    Slapi_MatchingRuleEntry *mr_entry;
    struct matchingRuleList *mrl_next;
};

/* List of the plugin index numbers */

/* Backend & Global Plugins */
#define PLUGIN_LIST_DATABASE                   0
#define PLUGIN_LIST_PREOPERATION               1
#define PLUGIN_LIST_POSTOPERATION              2
#define PLUGIN_LIST_BEPREOPERATION             3
#define PLUGIN_LIST_BEPOSTOPERATION            4
#define PLUGIN_LIST_INTERNAL_PREOPERATION      5
#define PLUGIN_LIST_INTERNAL_POSTOPERATION     6
#define PLUGIN_LIST_EXTENDED_OPERATION         7
#define PLUGIN_LIST_BE_TXN_EXTENDED_OPERATION  8
#define PLUGIN_LIST_PREEXTENDED_OPERATION      9
#define PLUGIN_LIST_POSTEXTENDED_OPERATION    10
#define PLUGIN_LIST_BACKEND_MAX               11

/* Global Plugins */
#define PLUGIN_LIST_ACL                      10
#define PLUGIN_LIST_MATCHINGRULE             11
#define PLUGIN_LIST_SYNTAX                   12
#define PLUGIN_LIST_ENTRY                    13
#define PLUGIN_LIST_OBJECT                   14
#define PLUGIN_LIST_PWD_STORAGE_SCHEME       15
#define PLUGIN_LIST_VATTR_SP                 16 /* DBDB */
#define PLUGIN_LIST_REVER_PWD_STORAGE_SCHEME 17
#define PLUGIN_LIST_LDBM_ENTRY_FETCH_STORE   18
#define PLUGIN_LIST_INDEX                    19
#define PLUGIN_LIST_BETXNPREOPERATION        20
#define PLUGIN_LIST_BETXNPOSTOPERATION       21
#define PLUGIN_LIST_MMR                      22
#define PLUGIN_LIST_GLOBAL_MAX               23

/* plugin configuration attributes */
#define ATTR_PLUGIN_PATH                   "nsslapd-pluginPath"
#define ATTR_PLUGIN_INITFN                 "nsslapd-pluginInitFunc"
#define ATTR_PLUGIN_TYPE                   "nsslapd-pluginType"
#define ATTR_PLUGIN_PLUGINID               "nsslapd-pluginId"
#define ATTR_PLUGIN_VERSION                "nsslapd-pluginVersion"
#define ATTR_PLUGIN_VENDOR                 "nsslapd-pluginVendor"
#define ATTR_PLUGIN_DESC                   "nsslapd-pluginDescription"
#define ATTR_PLUGIN_ENABLED                "nsslapd-pluginEnabled"
#define ATTR_PLUGIN_ARG                    "nsslapd-pluginArg"
#define ATTR_PLUGIN_CONFIG_AREA            "nsslapd-pluginConfigArea"
#define ATTR_PLUGIN_BACKEND                "nsslapd-backend"
#define ATTR_PLUGIN_SCHEMA_CHECK           "nsslapd-schemaCheck"
#define ATTR_PLUGIN_LOG_ACCESS             "nsslapd-logAccess"
#define ATTR_PLUGIN_LOG_AUDIT              "nsslapd-logAudit"
#define ATTR_PLUGIN_TARGET_SUBTREE         "nsslapd-targetSubtree"
#define ATTR_PLUGIN_EXCLUDE_TARGET_SUBTREE "nsslapd-exclude-targetSubtree"
#define ATTR_PLUGIN_BIND_SUBTREE           "nsslapd-bindSubtree"
#define ATTR_PLUGIN_EXCLUDE_BIND_SUBTREE   "nsslapd-exclude-bindSubtree"
#define ATTR_PLUGIN_INVOKE_FOR_REPLOP      "nsslapd-invokeForReplOp"
#define ATTR_PLUGIN_LOAD_NOW               "nsslapd-pluginLoadNow"
#define ATTR_PLUGIN_LOAD_GLOBAL            "nsslapd-pluginLoadGlobal"
#define ATTR_PLUGIN_PRECEDENCE             "nsslapd-pluginPrecedence"
#define ATTR_PLUGIN_DEPENDS_ON_TYPE        "nsslapd-plugin-depends-on-type"
#define ATTR_PLUGIN_DEPENDS_ON_NAMED       "nsslapd-plugin-depends-on-named"
#define ATTR_PLUGIN_BE_TXN                 "nsslapd-pluginbetxn"

/* plugin precedence defines */
#define PLUGIN_DEFAULT_PRECEDENCE 50
#define PLUGIN_MIN_PRECEDENCE      1
#define PLUGIN_MAX_PRECEDENCE     99

/* plugin action states */
enum
{
    PLGC_OFF,       /* internal operation action is on */
    PLGC_ON,        /* internal operation action is off */
    PLGC_UPTOPLUGIN /* internal operation action is left up to plugin */
};

/* special data specifications */
enum
{
    PLGC_DATA_LOCAL,          /* plugin has access to all data hosted by this server */
    PLGC_DATA_REMOTE,         /* plugin has access to all requests for data not hosted by this server */
    PLGC_DATA_BIND_ANONYMOUS, /* plugin bind function should be invoked for anonymous binds */
    PLGC_DATA_BIND_ROOT,      /* plugin bind function should be invoked for directory manager binds */
    PLGC_DATA_MAX
};

/* DataList definition */
typedef struct datalist
{
    void **elements;   /* array of elements */
    int element_count; /* number of elements in the array */
    int alloc_count;   /* number of allocated nodes in the array */
} datalist;

/* data available to plugins */
typedef struct target_data
{
    DataList subtrees;                  /* regular DIT subtrees acessible to the plugin */
    PRBool special_data[PLGC_DATA_MAX]; /* array of special data specification */
} PluginTargetData;

struct pluginconfig
{
    PluginTargetData plgc_target_subtrees;          /* list of subtrees accessible by the plugin */
    PluginTargetData plgc_excluded_target_subtrees; /* list of subtrees inaccessible by the plugin */
    PluginTargetData plgc_bind_subtrees;            /* the list of subtrees for which plugin is invoked during bind operation */
    PluginTargetData plgc_excluded_bind_subtrees;   /* the list of subtrees for which plugin is not invoked during bind operation */
    PRBool plgc_schema_check;                       /* inidcates whether schema check is performed during internal op */
    PRBool plgc_log_change;                         /* indicates whether changes are logged during internal op */
    PRBool plgc_log_access;                         /* indicates whether internal op is recorded in access log */
    PRBool plgc_log_audit;                          /* indicates whether internal op is recorded in audit log */
    PRBool plgc_invoke_for_replop;                  /* indicates that plugin should be invoked for internal operations */
};

struct slapdplugin
{
    void *plg_private;                      /* data private to plugin */
    char *plg_version;                      /* version of this plugin */
    int plg_argc;                           /* argc from config file */
    char **plg_argv;                        /* args from config file */
    char *plg_libpath;                      /* library path for dll/so */
    char *plg_initfunc;                     /* init symbol */
    IFP plg_close;                          /* close function */
    Slapi_PluginDesc plg_desc;              /* vendor's info */
    char *plg_name;                         /* used for plugin rdn in cn=config */
    struct slapdplugin *plg_next;           /* for plugin lists */
    int plg_type;                           /* discriminates union */
    char *plg_dn;                           /* config dn for this plugin */
    char *plg_id;                           /* plugin id, used when adding/removing plugins */
    struct slapi_componentid *plg_identity; /* Slapi component id */
    int plg_precedence;                     /* for plugin execution ordering */
    struct slapdplugin *plg_group;          /* pointer to the group to which this plugin belongs */
    struct pluginconfig plg_conf;           /* plugin configuration parameters */
    IFP plg_cleanup;                        /* cleanup function */
    IFP plg_start;                          /* start function */
    IFP plg_poststart;                      /* poststart function */
    int plg_closed;                         /* mark plugin as closed */
    int plg_removed;                        /* mark plugin as removed/deleted */
    PRUint64 plg_started;                   /* plugin is started/running */
    PRUint64 plg_stopped;                   /* plugin has been fully shutdown */
    Slapi_Counter *plg_op_counter;          /* operation counter, used for shutdown */

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
    union
    { /* backend database plugin structure */
        struct plg_un_database_backend
        {
            IFP plg_un_db_bind;              /* bind */
            IFP plg_un_db_unbind;            /* unbind */
            IFP plg_un_db_search;            /* search */
            IFP plg_un_db_next_search_entry; /* iterate */
            IFP plg_un_db_next_search_entry_ext;
            VFPP plg_un_db_search_results_release; /* PAGED RESULTS */
            VFP plg_un_db_prev_search_results;     /* PAGED RESULTS */
            IFP plg_un_db_entry_release;
            IFP plg_un_db_compare;              /* compare */
            IFP plg_un_db_modify;               /* modify */
            IFP plg_un_db_modrdn;               /* modrdn */
            IFP plg_un_db_add;                  /* add */
            IFP plg_un_db_delete;               /* delete */
            IFP plg_un_db_abandon;              /* abandon */
            IFP plg_un_db_config;               /* config */
            IFP plg_un_db_seq;                  /* sequence */
            IFP plg_un_db_entry;                /* entry send */
            IFP plg_un_db_referral;             /* referral send */
            IFP plg_un_db_result;               /* result send */
            IFP plg_un_db_ldif2db;              /* ldif 2 database */
            IFP plg_un_db_db2ldif;              /* database 2 ldif */
            IFP plg_un_db_db2index;             /* database 2 index */
            IFP plg_un_db_dbcompact;            /* compact database */
            IFP plg_un_db_archive2db;           /* ldif 2 database */
            IFP plg_un_db_db2archive;           /* database 2 ldif */
            IFP plg_un_db_upgradedb;            /* convert old idl to new */
            IFP plg_un_db_upgradednformat;      /* convert old dn format to new */
            IFP plg_un_db_begin;                /* dbase txn begin */
            IFP plg_un_db_commit;               /* dbase txn commit */
            IFP plg_un_db_abort;                /* dbase txn abort */
            IFP plg_un_db_dbsize;               /* database size */
            IFP plg_un_db_dbtest;               /* database size */
            IFP plg_un_db_rmdb;                 /* database remove */
            IFP plg_un_db_register_dn_callback; /* Register a function to call when a operation is applied to a given DN */
            IFP plg_un_db_register_oc_callback; /* Register a function to call when a operation is applied to a given ObjectClass */
            IFP plg_un_db_init_instance;        /* initializes new db instance */
            IFP plg_un_db_wire_import;          /* fast replica update */
            IFP plg_un_db_verify;               /* verify db files */
            IFP plg_un_db_add_schema;           /* add schema */
            IFP plg_un_db_get_info;             /* get info */
            IFP plg_un_db_set_info;             /* set info */
            IFP plg_un_db_ctrl_info;            /* ctrl info */
        } plg_un_db;
#define plg_bind plg_un.plg_un_db.plg_un_db_bind
#define plg_unbind plg_un.plg_un_db.plg_un_db_unbind
#define plg_search plg_un.plg_un_db.plg_un_db_search
#define plg_next_search_entry plg_un.plg_un_db.plg_un_db_next_search_entry
#define plg_next_search_entry_ext plg_un.plg_un_db.plg_un_db_next_search_entry_ext
#define plg_search_results_release plg_un.plg_un_db.plg_un_db_search_results_release
#define plg_prev_search_results plg_un.plg_un_db.plg_un_db_prev_search_results
#define plg_entry_release plg_un.plg_un_db.plg_un_db_entry_release
#define plg_compare plg_un.plg_un_db.plg_un_db_compare
#define plg_modify plg_un.plg_un_db.plg_un_db_modify
#define plg_modrdn plg_un.plg_un_db.plg_un_db_modrdn
#define plg_add plg_un.plg_un_db.plg_un_db_add
#define plg_delete plg_un.plg_un_db.plg_un_db_delete
#define plg_abandon plg_un.plg_un_db.plg_un_db_abandon
#define plg_config plg_un.plg_un_db.plg_un_db_config
#define plg_seq plg_un.plg_un_db.plg_un_db_seq
#define plg_entry plg_un.plg_un_db.plg_un_db_entry
#define plg_referral plg_un.plg_un_db.plg_un_db_referral
#define plg_result plg_un.plg_un_db.plg_un_db_result
#define plg_ldif2db plg_un.plg_un_db.plg_un_db_ldif2db
#define plg_db2ldif plg_un.plg_un_db.plg_un_db_db2ldif
#define plg_dbcompact plg_un.plg_un_db.plg_un_db_dbcompact
#define plg_db2index plg_un.plg_un_db.plg_un_db_db2index
#define plg_archive2db plg_un.plg_un_db.plg_un_db_archive2db
#define plg_db2archive plg_un.plg_un_db.plg_un_db_db2archive
#define plg_upgradedb plg_un.plg_un_db.plg_un_db_upgradedb
#define plg_upgradednformat plg_un.plg_un_db.plg_un_db_upgradednformat
#define plg_dbverify plg_un.plg_un_db.plg_un_db_verify
#define plg_dbsize plg_un.plg_un_db.plg_un_db_dbsize
#define plg_dbtest plg_un.plg_un_db.plg_un_db_dbtest
#define plg_rmdb plg_un.plg_un_db.plg_un_db_rmdb
#define plg_init_instance plg_un.plg_un_db.plg_un_db_init_instance
#define plg_wire_import plg_un.plg_un_db.plg_un_db_wire_import
#define plg_add_schema plg_un.plg_un_db.plg_un_db_add_schema
#define plg_get_info plg_un.plg_un_db.plg_un_db_get_info
#define plg_set_info plg_un.plg_un_db.plg_un_db_set_info
#define plg_ctrl_info plg_un.plg_un_db.plg_un_db_ctrl_info

        /* extended operation plugin structure */
        struct plg_un_protocol_extension
        {
            char **plg_un_pe_exoids;      /* exop oids */
            char **plg_un_pe_exnames;     /* exop names (may be NULL) */
            IFP plg_un_pe_exhandler;      /* handler */
            IFP plg_un_pe_pre_exhandler;  /* pre extop */
            IFP plg_un_pe_post_exhandler; /* post extop */
            IFP plg_un_pe_be_exhandler;   /* handler to retrieve the be name for the operation */
        } plg_un_pe;
#define plg_exoids plg_un.plg_un_pe.plg_un_pe_exoids
#define plg_exnames plg_un.plg_un_pe.plg_un_pe_exnames
#define plg_exhandler plg_un.plg_un_pe.plg_un_pe_exhandler
#define plg_preextop plg_un.plg_un_pe.plg_un_pe_pre_exhandler
#define plg_postextop plg_un.plg_un_pe.plg_un_pe_post_exhandler
#define plg_be_exhandler plg_un.plg_un_pe.plg_un_pe_be_exhandler


        /* pre-operation plugin structure */
        struct plg_un_pre_operation
        {
            IFP plg_un_pre_bind;     /* bind */
            IFP plg_un_pre_unbind;   /* unbind */
            IFP plg_un_pre_search;   /* search */
            IFP plg_un_pre_compare;  /* compare */
            IFP plg_un_pre_modify;   /* modify */
            IFP plg_un_pre_modrdn;   /* modrdn */
            IFP plg_un_pre_add;      /* add */
            IFP plg_un_pre_delete;   /* delete */
            IFP plg_un_pre_abandon;  /* abandon */
            IFP plg_un_pre_entry;    /* entry send */
            IFP plg_un_pre_referral; /* referral send */
            IFP plg_un_pre_result;   /* result send */
        } plg_un_pre;
#define plg_prebind plg_un.plg_un_pre.plg_un_pre_bind
#define plg_preunbind plg_un.plg_un_pre.plg_un_pre_unbind
#define plg_presearch plg_un.plg_un_pre.plg_un_pre_search
#define plg_precompare plg_un.plg_un_pre.plg_un_pre_compare
#define plg_premodify plg_un.plg_un_pre.plg_un_pre_modify
#define plg_premodrdn plg_un.plg_un_pre.plg_un_pre_modrdn
#define plg_preadd plg_un.plg_un_pre.plg_un_pre_add
#define plg_predelete plg_un.plg_un_pre.plg_un_pre_delete
#define plg_preabandon plg_un.plg_un_pre.plg_un_pre_abandon
#define plg_preentry plg_un.plg_un_pre.plg_un_pre_entry
#define plg_prereferral plg_un.plg_un_pre.plg_un_pre_referral
#define plg_preresult plg_un.plg_un_pre.plg_un_pre_result

        /* post-operation plugin structure */
        struct plg_un_post_operation
        {
            IFP plg_un_post_bind;       /* bind */
            IFP plg_un_post_unbind;     /* unbind */
            IFP plg_un_post_search;     /* search */
            IFP plg_un_post_searchfail; /* failed search */
            IFP plg_un_post_compare;    /* compare */
            IFP plg_un_post_modify;     /* modify */
            IFP plg_un_post_modrdn;     /* modrdn */
            IFP plg_un_post_add;        /* add */
            IFP plg_un_post_delete;     /* delete */
            IFP plg_un_post_abandon;    /* abandon */
            IFP plg_un_post_entry;      /* entry send */
            IFP plg_un_post_referral;   /* referral send */
            IFP plg_un_post_result;     /* result send */
        } plg_un_post;
#define plg_postbind plg_un.plg_un_post.plg_un_post_bind
#define plg_postunbind plg_un.plg_un_post.plg_un_post_unbind
#define plg_postsearch plg_un.plg_un_post.plg_un_post_search
#define plg_postsearchfail plg_un.plg_un_post.plg_un_post_searchfail
#define plg_postcompare plg_un.plg_un_post.plg_un_post_compare
#define plg_postmodify plg_un.plg_un_post.plg_un_post_modify
#define plg_postmodrdn plg_un.plg_un_post.plg_un_post_modrdn
#define plg_postadd plg_un.plg_un_post.plg_un_post_add
#define plg_postdelete plg_un.plg_un_post.plg_un_post_delete
#define plg_postabandon plg_un.plg_un_post.plg_un_post_abandon
#define plg_postentry plg_un.plg_un_post.plg_un_post_entry
#define plg_postreferral plg_un.plg_un_post.plg_un_post_referral
#define plg_postresult plg_un.plg_un_post.plg_un_post_result

        /* backend pre-operation plugin structure */
        struct plg_un_bepre_operation
        {
            IFP plg_un_bepre_modify;           /* modify */
            IFP plg_un_bepre_modrdn;           /* modrdn */
            IFP plg_un_bepre_add;              /* add */
            IFP plg_un_bepre_delete;           /* delete */
            IFP plg_un_bepre_delete_tombstone; /* tombstone creation */
            IFP plg_un_bepre_close;            /* close */
        } plg_un_bepre;
#define plg_bepremodify plg_un.plg_un_bepre.plg_un_bepre_modify
#define plg_bepremodrdn plg_un.plg_un_bepre.plg_un_bepre_modrdn
#define plg_bepreadd plg_un.plg_un_bepre.plg_un_bepre_add
#define plg_bepredelete plg_un.plg_un_bepre.plg_un_bepre_delete
#define plg_bepreclose plg_un.plg_un_bepre.plg_un_bepre_close

        /* backend post-operation plugin structure */
        struct plg_un_bepost_operation
        {
            IFP plg_un_bepost_modify; /* modify */
            IFP plg_un_bepost_modrdn; /* modrdn */
            IFP plg_un_bepost_add;    /* add */
            IFP plg_un_bepost_delete; /* delete */
            IFP plg_un_bepost_open;   /* open */
            IFP plg_un_bepost_import; /* import */
            IFP plg_un_bepost_export; /* export */
        } plg_un_bepost;
#define plg_bepostmodify plg_un.plg_un_bepost.plg_un_bepost_modify
#define plg_bepostmodrdn plg_un.plg_un_bepost.plg_un_bepost_modrdn
#define plg_bepostadd plg_un.plg_un_bepost.plg_un_bepost_add
#define plg_bepostdelete plg_un.plg_un_bepost.plg_un_bepost_delete
#define plg_bepostopen plg_un.plg_un_bepost.plg_un_bepost_open
#define plg_bepostimport plg_un.plg_un_bepost.plg_un_bepost_import
#define plg_bepostexport plg_un.plg_un_bepost.plg_un_bepost_export

        /* internal  pre-operation plugin structure */
        struct plg_un_internal_pre_operation
        {
            IFP plg_un_internal_pre_modify; /* modify */
            IFP plg_un_internal_pre_modrdn; /* modrdn */
            IFP plg_un_internal_pre_add;    /* add */
            IFP plg_un_internal_pre_delete; /* delete */
            IFP plg_un_internal_pre_bind;   /* bind */
        } plg_un_internal_pre;
#define plg_internal_pre_modify plg_un.plg_un_internal_pre.plg_un_internal_pre_modify
#define plg_internal_pre_modrdn plg_un.plg_un_internal_pre.plg_un_internal_pre_modrdn
#define plg_internal_pre_add plg_un.plg_un_internal_pre.plg_un_internal_pre_add
#define plg_internal_pre_delete plg_un.plg_un_internal_pre.plg_un_internal_pre_delete
#define plg_internal_pre_bind plg_un.plg_un_internal_pre.plg_un_internal_pre_bind

        /* internal post-operation plugin structure */
        struct plg_un_internal_post_operation
        {
            IFP plg_un_internal_post_modify; /* modify */
            IFP plg_un_internal_post_modrdn; /* modrdn */
            IFP plg_un_internal_post_add;    /* add */
            IFP plg_un_internal_post_delete; /* delete */
        } plg_un_internal_post;
#define plg_internal_post_modify plg_un.plg_un_internal_post.plg_un_internal_post_modify
#define plg_internal_post_modrdn plg_un.plg_un_internal_post.plg_un_internal_post_modrdn
#define plg_internal_post_add plg_un.plg_un_internal_post.plg_un_internal_post_add
#define plg_internal_post_delete plg_un.plg_un_internal_post.plg_un_internal_post_delete

        /* matching rule plugin structure */
        struct plg_un_matching_rule
        {
            IFP plg_un_mr_filter_create;  /* factory function */
            IFP plg_un_mr_indexer_create; /* factory function */
            /* new style syntax plugin functions */
            /* not all functions will apply to all matching rule types */
            /* e.g. a SUBSTR rule will not have a filter_ava func */
            IFP plg_un_mr_filter_ava;
            IFP plg_un_mr_filter_sub;
            IFP plg_un_mr_values2keys;
            IFP plg_un_mr_assertion2keys_ava;
            IFP plg_un_mr_assertion2keys_sub;
            int plg_un_mr_flags;
            char **plg_un_mr_names;
            IFP plg_un_mr_compare; /* only for ORDERING */
            VFPV plg_un_mr_normalize;
        } plg_un_mr;
#define plg_mr_filter_create plg_un.plg_un_mr.plg_un_mr_filter_create
#define plg_mr_indexer_create plg_un.plg_un_mr.plg_un_mr_indexer_create
#define plg_mr_filter_ava plg_un.plg_un_mr.plg_un_mr_filter_ava
#define plg_mr_filter_sub plg_un.plg_un_mr.plg_un_mr_filter_sub
#define plg_mr_values2keys plg_un.plg_un_mr.plg_un_mr_values2keys
#define plg_mr_assertion2keys_ava plg_un.plg_un_mr.plg_un_mr_assertion2keys_ava
#define plg_mr_assertion2keys_sub plg_un.plg_un_mr.plg_un_mr_assertion2keys_sub
#define plg_mr_flags plg_un.plg_un_mr.plg_un_mr_flags
#define plg_mr_names plg_un.plg_un_mr.plg_un_mr_names
#define plg_mr_compare plg_un.plg_un_mr.plg_un_mr_compare
#define plg_mr_normalize plg_un.plg_un_mr.plg_un_mr_normalize

        /* syntax plugin structure */
        struct plg_un_syntax_struct
        {
            IFP plg_un_syntax_filter_ava;
            IFP plg_un_syntax_filter_ava_sv;
            IFP plg_un_syntax_filter_sub;
            IFP plg_un_syntax_filter_sub_sv;
            IFP plg_un_syntax_values2keys;
            IFP plg_un_syntax_values2keys_sv;
            IFP plg_un_syntax_assertion2keys_ava;
            IFP plg_un_syntax_assertion2keys_sub;
            int plg_un_syntax_flags;
            /*
   from slapi-plugin.h
#define SLAPI_PLUGIN_SYNTAX_FLAG_ORKEYS        1
#define SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING    2
*/
            char **plg_un_syntax_names;
            char *plg_un_syntax_oid;
            IFP plg_un_syntax_compare;
            IFP plg_un_syntax_validate;
            VFPV plg_un_syntax_normalize;
        } plg_un_syntax;
#define plg_syntax_filter_ava plg_un.plg_un_syntax.plg_un_syntax_filter_ava
#define plg_syntax_filter_sub plg_un.plg_un_syntax.plg_un_syntax_filter_sub
#define plg_syntax_values2keys plg_un.plg_un_syntax.plg_un_syntax_values2keys
#define plg_syntax_assertion2keys_ava plg_un.plg_un_syntax.plg_un_syntax_assertion2keys_ava
#define plg_syntax_assertion2keys_sub plg_un.plg_un_syntax.plg_un_syntax_assertion2keys_sub
#define plg_syntax_flags plg_un.plg_un_syntax.plg_un_syntax_flags
#define plg_syntax_names plg_un.plg_un_syntax.plg_un_syntax_names
#define plg_syntax_oid plg_un.plg_un_syntax.plg_un_syntax_oid
#define plg_syntax_compare plg_un.plg_un_syntax.plg_un_syntax_compare
#define plg_syntax_validate plg_un.plg_un_syntax.plg_un_syntax_validate
#define plg_syntax_normalize plg_un.plg_un_syntax.plg_un_syntax_normalize

        struct plg_un_acl_struct
        {
            IFP plg_un_acl_init;
            IFP plg_un_acl_syntax_check;
            IFP plg_un_acl_access_allowed;
            IFP plg_un_acl_mods_allowed;
            IFP plg_un_acl_mods_update;
        } plg_un_acl;
#define plg_acl_init plg_un.plg_un_acl.plg_un_acl_init
#define plg_acl_syntax_check plg_un.plg_un_acl.plg_un_acl_syntax_check
#define plg_acl_access_allowed plg_un.plg_un_acl.plg_un_acl_access_allowed
#define plg_acl_mods_allowed plg_un.plg_un_acl.plg_un_acl_mods_allowed
#define plg_acl_mods_update plg_un.plg_un_acl.plg_un_acl_mods_update

        struct plg_un_mmr_struct
        {
            IFP plg_un_mmr_betxn_preop;
            IFP plg_un_mmr_betxn_postop;
        } plg_un_mmr;
#define plg_mmr_betxn_preop 		plg_un.plg_un_mmr.plg_un_mmr_betxn_preop
#define plg_mmr_betxn_postop 		plg_un.plg_un_mmr.plg_un_mmr_betxn_postop

        /* password storage scheme (kexcoff) */
        struct plg_un_pwd_storage_scheme_struct
        {
            char *plg_un_pwd_storage_scheme_name; /* SHA, SSHA...*/
            CFP plg_un_pwd_storage_scheme_enc;
            IFP plg_un_pwd_storage_scheme_dec;
            IFP plg_un_pwd_storage_scheme_cmp;
        } plg_un_pwd_storage_scheme;
#define plg_pwdstorageschemename plg_un.plg_un_pwd_storage_scheme.plg_un_pwd_storage_scheme_name
#define plg_pwdstorageschemeenc plg_un.plg_un_pwd_storage_scheme.plg_un_pwd_storage_scheme_enc
#define plg_pwdstorageschemedec plg_un.plg_un_pwd_storage_scheme.plg_un_pwd_storage_scheme_dec
#define plg_pwdstorageschemecmp plg_un.plg_un_pwd_storage_scheme.plg_un_pwd_storage_scheme_cmp

        /* entry fetch/store */
        struct plg_un_entry_fetch_store_struct
        {
            IFP plg_un_entry_fetch_func;
            IFP plg_un_entry_store_func;
        } plg_un_entry_fetch_store;
#define plg_entryfetchfunc plg_un.plg_un_entry_fetch_store.plg_un_entry_fetch_func
#define plg_entrystorefunc plg_un.plg_un_entry_fetch_store.plg_un_entry_store_func

        /* backend txn pre-operation plugin structure */
        struct plg_un_betxnpre_operation
        {
            IFP plg_un_betxnpre_modify;           /* modify */
            IFP plg_un_betxnpre_modrdn;           /* modrdn */
            IFP plg_un_betxnpre_add;              /* add */
            IFP plg_un_betxnpre_delete;           /* delete */
            IFP plg_un_betxnpre_delete_tombstone; /* delete tombstone */
        } plg_un_betxnpre;
#define plg_betxnpremodify plg_un.plg_un_betxnpre.plg_un_betxnpre_modify
#define plg_betxnpremodrdn plg_un.plg_un_betxnpre.plg_un_betxnpre_modrdn
#define plg_betxnpreadd plg_un.plg_un_betxnpre.plg_un_betxnpre_add
#define plg_betxnpredelete plg_un.plg_un_betxnpre.plg_un_betxnpre_delete
#define plg_betxnpredeletetombstone plg_un.plg_un_betxnpre.plg_un_betxnpre_delete_tombstone

        /* backend txn post-operation plugin structure */
        struct plg_un_betxnpost_operation
        {
            IFP plg_un_betxnpost_modify; /* modify */
            IFP plg_un_betxnpost_modrdn; /* modrdn */
            IFP plg_un_betxnpost_add;    /* add */
            IFP plg_un_betxnpost_delete; /* delete */
        } plg_un_betxnpost;
#define plg_betxnpostmodify plg_un.plg_un_betxnpost.plg_un_betxnpost_modify
#define plg_betxnpostmodrdn plg_un.plg_un_betxnpost.plg_un_betxnpost_modrdn
#define plg_betxnpostadd plg_un.plg_un_betxnpost.plg_un_betxnpost_add
#define plg_betxnpostdelete plg_un.plg_un_betxnpost.plg_un_betxnpost_delete

    } plg_un;
};

struct suffixlist
{
    Slapi_DN *be_suffix;
    struct suffixlist *next;
};

/*
 * represents a "database"
 */
typedef struct backend
{
    Slapi_DN *be_suffix;                     /* Suffix for this backend */
    char *be_basedn;                         /* The base dn for the config & monitor dns */
    char *be_configdn;                       /* The config dn for this backend */
    char *be_monitordn;                      /* The monitor dn for this backend */
    int be_readonly;                         /* 1 => db is in "read only" mode */
    int be_sizelimit;                        /* size limit for this backend */
    int be_timelimit;                        /* time limit for this backend */
    int be_maxnestlevel;                     /* Max nest level for acl group evaluation */
    int be_noacl;                            /* turn off front end acl for this be */
    int be_lastmod;                          /* keep track of lastmodified{by,time} */
    char *be_type;                           /* type of database */
    char *be_backendconfig;                  /* backend config filename */
    char **be_include;                       /* include files within this db definition */
    int be_private;                          /* Internal backends use this to hide from the user */
    int be_logchanges;                       /* changes to this backend should be logged in the changelog */
    int (*be_writeconfig)(Slapi_PBlock *pb); /* function to call to make this backend write its conf file */
    /*
     * backend database api function ptrs and args (to do operations)
     */
    struct slapdplugin *be_database; /* single plugin */
#define be_bind be_database->plg_bind
#define be_unbind be_database->plg_unbind
#define be_search be_database->plg_search
#define be_next_search_entry be_database->plg_next_search_entry
#define be_next_search_entry_ext be_database->plg_next_search_entry_ext
#define be_entry_release be_database->plg_entry_release
#define be_search_results_release be_database->plg_search_results_release
#define be_prev_search_results be_database->plg_prev_search_results
#define be_compare be_database->plg_compare
#define be_modify be_database->plg_modify
#define be_modrdn be_database->plg_modrdn
#define be_add be_database->plg_add
#define be_delete be_database->plg_delete
#define be_abandon be_database->plg_abandon
#define be_config be_database->plg_config
#define be_close be_database->plg_close
#define be_start be_database->plg_start
#define be_poststart be_database->plg_poststart
#define be_seq be_database->plg_seq
#define be_ldif2db be_database->plg_ldif2db
#define be_upgradedb be_database->plg_upgradedb
#define be_upgradednformat be_database->plg_upgradednformat
#define be_db2ldif be_database->plg_db2ldif
#define be_db2index be_database->plg_db2index
#define be_archive2db be_database->plg_archive2db
#define be_db2archive be_database->plg_db2archive
#define be_dbsize be_database->plg_dbsize
#define be_dbtest be_database->plg_dbtest
#define be_rmdb be_database->plg_rmdb
#define be_result be_database->plg_result
#define be_init_instance be_database->plg_init_instance
#define be_cleanup be_database->plg_cleanup
#define be_wire_import be_database->plg_wire_import
#define be_get_info be_database->plg_get_info
#define be_set_info be_database->plg_set_info
#define be_ctrl_info be_database->plg_ctrl_info

    void *be_instance_info; /* If the database plugin pointed to by
                                 * be_database supports more than one instance,
                                 * it can use this to keep track of the
                                 * multiple instances. */

    char *be_name; /* The mapping tree and command line utils
                                 * refer to backends by name. */
    int be_mapped; /* True if the be is represented by a node
                                 * in the mapping tree. */

    /*struct slapdplugin    *be_plugin_list[PLUGIN_LIST_BACKEND_MAX]; list of plugins */

    int be_delete_on_exit; /* marks db for deletion - used to remove changelog*/
    int be_state;          /* indicates current database state */
    PRLock *be_state_lock; /* lock under which to modify the state */

    int be_flags; /* misc properties. See BE_FLAG_xxx defined in slapi-private.h */
    Slapi_RWLock *be_lock;
    Slapi_RWLock *vlvSearchList_lock;
    void *vlvSearchList;
    Slapi_Counter *be_usn_counter; /* USN counter; one counter per backend */
    int be_pagedsizelimit;         /* size limit for this backend for simple paged result searches */
} backend;

enum
{
    BE_STATE_STOPPED = 1, /* backend is initialized but not started */
    BE_STATE_STARTED,     /* backend is started */
    BE_STATE_CLEANED,     /* backend was cleaned up */
    BE_STATE_DELETED,     /* backend is removed */
    BE_STATE_STOPPING     /* told to stop but not yet stopped */
};

struct conn;
struct op;

typedef void (*result_handler)(struct conn *, struct op *, int, char *, char *, int, struct berval **);
typedef int (*search_entry_handler)(Slapi_Backend *, struct conn *, struct op *, struct slapi_entry *);
typedef int (*search_referral_handler)(Slapi_Backend *, struct conn *, struct op *, struct berval **);
typedef int32_t *(*csngen_handler)(Slapi_PBlock *pb, const CSN *basecsn, CSN **opcsn);
typedef int (*replica_attr_handler)(Slapi_PBlock *pb, const char *type, void **value);

/*
 * LDAP Operation results.
 */
typedef struct slapi_operation_results
{
    unsigned long operation_type;

    int opreturn;

    LDAPControl **result_controls; /* ctrls to be returned w/result  */

    int result_code;
    char *result_text;
    char *result_matched;

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
typedef struct op
{
    BerElement *o_ber;             /* ber of the request */
    ber_int_t o_msgid;             /* msgid of the request */
    ber_tag_t o_tag;               /* tag of the request */
    struct timespec o_hr_time_rel; /* internal system time op initiated */
    struct timespec o_hr_time_utc; /* utc system time op initiated */
    struct timespec o_hr_time_started_rel; /* internal system time op started */
    int o_isroot;                  /* requestor is manager */
    Slapi_DN o_sdn;                /* dn bound when op was initiated */
    char *o_authtype;              /* auth method used to bind dn */
    int o_ssf;                     /* ssf for this operation (highest between SASL and TLS/SSL) */
    int o_opid;                    /* id of this operation */
    PRUint64 o_connid;             /* id of conn initiating this op; for logging only */
    void *o_handler_data;
    result_handler o_result_handler;
    search_entry_handler o_search_entry_handler;
    search_referral_handler o_search_referral_handler;
    csngen_handler o_csngen_handler;
    replica_attr_handler o_replica_attr_handler;
    struct op *o_next;                                         /* next operation pending      */
    int o_status;                                              /* status (see SLAPI_OP_STATUS_... below */
    char **o_searchattrs; /* original attr names requested  */ /* JCM - Search Param */
    unsigned long o_flags;                                     /* flags for this operation      */
    void *o_extension;                                         /* plugins are able to extend the Operation object */
    Slapi_DN *o_target_spec;                                   /* used to decide which plugins should be called for the operation */
    void *o_target_entry;                                      /* Only used for SEARCH operation
                                                                * reference of search target entry (base search) in the entry cache
                                                                * When it is set the refcnt (of the entry in the entry cache) as been increased
                                                                */
    u_int32_t o_target_entry_id;                               /* Only used for SEARCH operation
                                                                * contains the ID of the o_target_entry. In send_result we have ID of the candidates, this
                                                                * accelerates the tests as we have not to retrieve for each candidate the
                                                                * ep_id inside the o_target_entry.
                                                                */
    unsigned long o_abandoned_op;                              /* operation abandoned by this operation - used to decide which plugins to invoke */
    struct slapi_operation_parameters o_params;
    struct slapi_operation_results o_results;
    int o_pagedresults_sizelimit;
    int o_reverse_search_state;
} Operation;

/*
 * Operation status (o_status) values.
 * The normal progression is from PROCESSING to RESULT_SENT, with
 *   WILL_COMPLETE as an optional intermediate state.
 * For operations that are abandoned, the progression is from PROCESSING
 *   to ABANDONED.
 */
#define SLAPI_OP_STATUS_PROCESSING    0 /* the normal state */
#define SLAPI_OP_STATUS_ABANDONED     1 /* op. has been abandoned */
#define SLAPI_OP_STATUS_WILL_COMPLETE 2 /* no more abandon checks will be done */
#define SLAPI_OP_STATUS_RESULT_SENT   3   /* result has been sent to the client (or we tried to do so and failed) */


/* simple paged structure */
typedef struct _paged_results
{
    Slapi_Backend *pr_current_be;           /* backend being used */
    void *pr_search_result_set;             /* search result set for paging */
    int pr_search_result_count;             /* search result count */
    int pr_search_result_set_size_estimate; /* estimated search result set size */
    int pr_sort_result_code;                /* sort result put in response */
    struct timespec pr_timelimit_hr;        /* expiry time of this request rel to clock monotonic */
    int pr_flags;
    ber_int_t pr_msgid; /* msgid of the request; to abandon */
    PRLock *pr_mutex;   /* protect each conn structure    */
} PagedResults;

/* array of simple paged structure stashed in connection */
typedef struct _paged_results_list
{
    int prl_maxlen;         /* size of the PagedResults array */
    int prl_count;          /* count of the list in use */
    PagedResults *prl_list; /* pointer to pr_maxlen length PageResults array */
} PagedResultsList;

/*
 * represents a connection from an ldap client
 */

typedef int (*Conn_IO_Layer_cb)(struct conn *, void *data);

struct Conn_Private;
typedef struct Conn_private Conn_private;

typedef enum _conn_state {
    CONN_STATE_FREE = 0,
    CONN_STATE_INIT = 1,
} conn_state;

typedef struct conn
{
    Sockbuf *c_sb;                   /* ber connection stuff          */
    conn_state c_state;              /* Used in connection table and done to see what's free or not. Later we could use this for other state handlings. */
    int c_sd;                        /* the actual socket descriptor      */
    int c_ldapversion;               /* version of LDAP protocol       */
    char *c_dn;                      /* current DN bound to this conn  */
    int c_isroot;                    /* c_dn was rootDN at time of bind? */
    int c_isreplication_session;     /* this connection is a replication session */
    char *c_authtype;                /* auth method used to bind c_dn  */
    char *c_external_dn;             /* client DN of this SSL session  */
    char *c_external_authtype;       /* used for c_external_dn   */
    PRNetAddr *cin_addr;             /* address of client on this conn */
    PRNetAddr *cin_addr_aclip;       /* address of client allocated by acl with 'ip' subject */
    PRNetAddr *cin_destaddr;         /* address client connected to    */
    struct berval **c_domain;        /* DNS names of client            */
    Operation *c_ops;                /* list of pending operations      */
    int c_gettingber;                /* in the middle of ber_get_next  */
    BerElement *c_currentber;        /* ber we're getting              */
    time_t c_starttime;              /* when the connection was opened */
    uint64_t c_connid;               /* id of this connection for stats*/
    PRUint64 c_maxthreadscount;      /* # of times a conn hit max threads */
    PRUint64 c_maxthreadsblocked;    /* # of operations blocked by maxthreads */
    int c_opsinitiated;              /* # ops initiated/next op id      */
    PRInt32 c_opscompleted;          /* # ops completed          */
    uint64_t c_anonlimits_set;       /* default anon limits are set */
    PRInt32 c_threadnumber;          /* # threads used in this conn    */
    int c_refcnt;                    /* # ops refering to this conn    */
    pthread_mutex_t c_mutex;         /* protect each conn structure; need to be re-entrant */
    PRLock *c_pdumutex;              /* only write one pdu at a time   */
    time_t c_idlesince;              /* last time of activity on conn  */
    int c_idletimeout;               /* local copy of idletimeout */
    int c_idletimeout_handle;        /* the resource limits handle */
    Conn_private *c_private;         /* data which is not shared outside connection.c */
    int c_flags;                     /* Misc flags used only for SSL status currently */
    int c_needpw;                    /* need new password           */
    CERTCertificate *c_client_cert;  /* Client's Cert          */
    PRFileDesc *c_prfd;              /* NSPR 2.1 FileDesc          */
    int c_ci;                        /* An index into the Connection array. For printing. */
    int c_fdi;                       /* An index into the FD array. The FD this connection is using. */
    struct conn *c_next;             /* Pointer to the next and previous */
    struct conn *c_prev;             /* active connections in the table*/
    Slapi_Backend *c_bi_backend;     /* which backend is doing the import */
    void *c_extension;               /* plugins are able to extend the Connection object */
    void *c_sasl_conn;               /* sasl library connection sasl_conn_t */
    int c_local_ssf;                 /* flag to tell us the local SSF */
    int c_sasl_ssf;                  /* flag to tell us the SASL SSF */
    int c_ssl_ssf;                   /* flag to tell us the SSL/TLS SSF */
    int c_unix_local;                /* flag true for LDAPI */
    int c_local_valid;               /* flag true if the uid/gid are valid */
    uid_t c_local_uid;               /* uid of connecting process */
    gid_t c_local_gid;               /* gid of connecting process */
    PagedResultsList c_pagedresults; /* PAGED_RESULTS */
    /* IO layer push/pop */
    Conn_IO_Layer_cb c_push_io_layer_cb; /* callback to push an IO layer on the conn->c_prfd */
    Conn_IO_Layer_cb c_pop_io_layer_cb;  /* callback to pop an IO layer off of the conn->c_prfd */
    void *c_io_layer_cb_data;        /* callback data */
    struct connection_table *c_ct;   /* connection table that this connection belongs to */
    int c_ct_list;                   /* ct active list this conn is part of */
    int c_ns_close_jobs;             /* number of current close jobs */
    char *c_ipaddr;                  /* ip address str - used by monitor */
    char *c_serveripaddr;            /* server ip address str - used by monitor */
    /* per conn static config */
    ber_len_t c_maxbersize;
    int32_t c_ioblocktimeout;
    int32_t c_minssf;
    int32_t c_enable_nagle;
    int32_t c_minssf_exclude_rootdse;
    int32_t c_anon_access;
    int32_t c_max_threads_per_conn;
    int32_t c_bind_auth_token;
} Connection;
#define CONN_FLAG_SSL 1     /* Is this connection an SSL connection or not ?         \
                           * Used to direct I/O code when SSL is handled differently \
                           */
#define CONN_FLAG_CLOSING 2 /* If this flag is set, then the connection has \
             * been marked for closing by a worker thread                   \
             * and the listener thread should close it. */
#define CONN_FLAG_IMPORT 4  /* This connection has begun a bulk import      \
                             * (aka "fast replica init" aka "wire import"), \
                             * so it can only accept adds & ext-ops.        \
                             */


#define CONN_FLAG_SASL_CONTINUE 8 /* We're in a multi-stage sasl bind */

#define CONN_FLAG_START_TLS 16 /* Flag set when an SSL connection is so after an \
                * Start TLS request operation.                                   \
                */

#define CONN_FLAG_SASL_COMPLETE 32 /* Flag set when a sasl bind has been \
                                    * successfully completed.            \
                                    */

#define CONN_FLAG_PAGEDRESULTS_WITH_SORT 64 /* paged results control is      \
                                             * sent with server side sorting \
                                             */

#define CONN_FLAG_PAGEDRESULTS_UNINDEXED 128  /* If the search is unindexed, \
                                               * store the info in c_flags   \
                                               */
#define CONN_FLAG_PAGEDRESULTS_PROCESSING 256 /* there is an operation            \
                                               * processing a pagedresults search \
                                               */
#define CONN_FLAG_PAGEDRESULTS_ABANDONED 512  /* pagedresults abandoned */

#define CONN_FLAG_MAX_THREADS 1024 /* Flag set when connection is at the maximum number of threads */

#define CONN_GET_SORT_RESULT_CODE (-1)

#define START_TLS_OID "1.3.6.1.4.1.1466.20037"

#define SLAPD_POLL_FLAGS (POLLIN)


/******************************************************************************
 *  * Online tasks interface (to support import, export, etc)
 *   * After some cleanup, we could consider making these public.
 *    */
typedef struct slapi_task
{
    struct slapi_task *next;
    char *task_dn;
    int task_exitcode;         /* for the end user */
    int task_state;            /* current state of task */
    int task_progress;         /* number between 0 and task_work */
    int task_work;             /* "units" of work to be done */
    int task_flags;            /* (see above) */
    task_warning task_warn;    /* task warning */
    char *task_status;         /* transient status info */
    char *task_log;            /* appended warnings, etc */
    char task_date[SLAPI_TIMESTAMP_BUFSIZE]; /* Date/time when task was created */
    void *task_private;        /* allow opaque data to be stashed in the task */
    TaskCallbackFn cancel;     /* task has been cancelled by user */
    TaskCallbackFn destructor; /* task entry is being destroyed */
    int task_refcount;
    void *origin_plugin;       /* If this is a plugin create task, store the plugin object */
    PRLock *task_log_lock;     /* To protect task_log to be realloced if it's in use */
} slapi_task;
/* End of interface to support online tasks **********************************/

/*
 * structure for holding password scheme info.
 */
struct pw_scheme
{
    /* case-insensitive name used in prefix of passwords that use scheme */
    char *pws_name;

    /* length of pws_name */
    int pws_len;

    /* thread-safe comparison function; returns 0 for positive matches */
    /* userpwd is value sent over LDAP bind; dbpwd is from the database */
    int (*pws_cmp)(char *userpwd, char *dbpwd);

    /* thread-safe encoding function (returns pointer to malloc'd string) */
    char *(*pws_enc)(char *pwd);

    /* thread-safe decoding function (returns pointer to malloc'd string) */
    char *(*pws_dec)(char *pwd, char *algid);
};

typedef struct passwordpolicyarray
{
    slapi_onoff_t pw_change;      /* 1 - indicates that users are allowed to change the pwd */
    slapi_onoff_t pw_must_change; /* 1 - indicates that users must change pwd upon reset */
    slapi_onoff_t pw_syntax;
    int32_t pw_minlength;
    int32_t pw_mindigits;
    int32_t pw_minalphas;
    int32_t pw_minuppers;
    int32_t pw_minlowers;
    int32_t pw_minspecials;
    int32_t pw_min8bit;
    int32_t pw_maxrepeats;
    int32_t pw_mincategories;
    int32_t pw_mintokenlength;

    slapi_onoff_t pw_palindrome;  /* Reject passwords that are palindromes */
    int32_t pw_max_seq;           /* max number of monotonic sequences: 2345, lmnop */
    int32_t pw_seq_char_sets;     /* max number of identical monotonic sequences that are
                                   * allowed multiple times.  For example: "az12of12", if
                                   * value is set to 2, meaning sequence of two
                                   * characters, then this password would be blocked.  If
                                   * it's set to 3, then the password is allowed. */
    int32_t pw_max_class_repeats; /* The maximum number of consecutive characters from
                                     the same character class. */
    slapi_onoff_t pw_check_dict;
    char *pw_dict_path;           /* custom dictionary */
    char *pw_cmp_attrs;           /* Comma-separated list of attributes to see if the
                                     attribute values (and reversed values) in the entry
                                     are contained in the new password. */
    char **pw_cmp_attrs_array;    /* Array of password user attributes */
    char *pw_bad_words;           /* Comma-separated list of words to reject */
    char **pw_bad_words_array;    /* Array of words to reject */

    slapi_onoff_t pw_exp;
    slapi_onoff_t pw_send_expiring;
    time_t pw_maxage;
    time_t pw_minage;
    time_t pw_warning;
    slapi_onoff_t pw_history;
    int pw_inhistory;
    slapi_onoff_t pw_lockout;
    int pw_maxfailure;
    slapi_onoff_t pw_unlock;
    time_t pw_lockduration;
    long pw_resetfailurecount;
    int pw_tpr_maxuse;
    int pw_tpr_delay_expire_at;
    int pw_tpr_delay_valid_from;
    int pw_gracelimit;
    slapi_onoff_t pw_is_legacy;
    slapi_onoff_t pw_track_update_time;
    struct pw_scheme *pw_storagescheme;
    Slapi_DN *pw_admin;
    Slapi_DN **pw_admin_user;
    slapi_onoff_t pw_admin_skip_info;  /* Skip updating password information in target entry */
    char *pw_local_dn; /* DN of the subtree/user policy */

} passwdPolicy;
#define PWDPOLICY_DEBUG "PWDPOLICY_DEBUG"

void pwpolicy_init_defaults (passwdPolicy *pw_policy);

Slapi_PBlock *slapi_pblock_clone(Slapi_PBlock *pb); /* deprecated */

passwdPolicy *slapi_pblock_get_pwdpolicy(Slapi_PBlock *pb);
void slapi_pblock_set_pwdpolicy(Slapi_PBlock *pb, passwdPolicy *pwdpolicy);

int32_t slapi_pblock_get_ldif_dump_replica(Slapi_PBlock *pb);
void slapi_pblock_set_ldif_dump_replica(Slapi_PBlock *pb, int32_t dump_replica);

void *slapi_pblock_get_vattr_context(Slapi_PBlock *pb);
void slapi_pblock_set_vattr_context(Slapi_PBlock *pb, void *vattr_ctx);


void *slapi_pblock_get_op_stack_elem(Slapi_PBlock *pb);
void slapi_pblock_set_op_stack_elem(Slapi_PBlock *pb, void *stack_elem);

/* index if substrlens */
#define INDEX_SUBSTRBEGIN  0
#define INDEX_SUBSTRMIDDLE 1
#define INDEX_SUBSTREND    2
#define INDEX_SUBSTRLEN    3 /* size of the substrlens */

/* The referral element */
typedef struct ref
{
    char *ref_dn;                /* The DN of the entry that contains the referral */
    struct berval *ref_referral; /* The referral. It looks like: ldap://host:port */
    int ref_reads;               /* 1 if refer searches, 0 else */
    int ref_writes;              /* 1 if refer modifications, 0 else */
} Ref;

/* The head of the referral array. */
typedef struct ref_array
{
    Slapi_RWLock *ra_rwlock; /* Read-write lock struct to protect this thing */
    int ra_size;             /* The size of this puppy (NOT the number of entries)*/
    int ra_nextindex;        /* The next free index */
    int ra_readcount;        /* The number of copyingfroms in the list */
    Ref **ra_refs;           /* The array of referrals*/
} Ref_Array;

#define GR_LOCK_READ()    slapi_rwlock_rdlock(grefs->ra_rwlock)
#define GR_UNLOCK_READ()  slapi_rwlock_unlock(grefs->ra_rwlock)
#define GR_LOCK_WRITE()   slapi_rwlock_wrlock(grefs->ra_rwlock)
#define GR_UNLOCK_WRITE() slapi_rwlock_unlock(grefs->ra_rwlock)

/*
 * This structure is used to pass a pair of port numbers to the daemon
 * function. The daemon is the root of a forked thread.
 */

typedef struct daemon_ports_s
{
    int n_port;
    int s_port;
    PRNetAddr **n_listenaddr;
    PRNetAddr **s_listenaddr;
    PRFileDesc **n_socket;
#if defined(ENABLE_LDAPI)
    /* ldapi */
    PRNetAddr **i_listenaddr;
    int i_port; /* used as a flag only */
    PRFileDesc **i_socket;
#endif
    PRFileDesc **s_socket;
} daemon_ports_t;


/* Definition for plugin syntax compare routine */
typedef int (*value_compare_fn_type)(const struct berval *, const struct berval *);

/* Definition for plugin syntax validate routine */
typedef int (*value_validate_fn_type)(const struct berval *);

#include "proto-slap.h"
LDAPMod **entry2mods(Slapi_Entry *, LDAPMod **, int *, int);

/* SNMP Counter Variables */
struct snmp_ops_tbl_t
{
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
    Slapi_Counter *dsConnections;   /* Number of currently connected clients */
    Slapi_Counter *dsConnectionSeq; /* Monotonically increasing number bumped on each new conn est */
    Slapi_Counter *dsBytesRecv;     /* Count of bytes read from clients */
    Slapi_Counter *dsBytesSent;     /* Count of bytes sent to clients */
    Slapi_Counter *dsEntriesReturned;
    Slapi_Counter *dsReferralsReturned;
    Slapi_Counter *dsMaxThreadsHits;
    Slapi_Counter *dsConnectionsInMaxThreads;
};

struct snmp_entries_tbl_t
{
    /* entries table */
    Slapi_Counter *dsSupplierEntries;
    Slapi_Counter *dsCopyEntries;
    Slapi_Counter *dsCacheEntries;
    Slapi_Counter *dsCacheHits;
    Slapi_Counter *dsConsumerHits;
};

struct snmp_server_tbl_t
{
    /* general purpose counters */
    Slapi_Counter *dsOpInitiated;
    Slapi_Counter *dsOpCompleted;
    Slapi_Counter *dsEntriesSent;
    Slapi_Counter *dsBytesSent;
};
struct snmp_int_tbl_t
{
    /* interaction table */
    int32_t dsIntIndex;
    char dsName[SNMP_FIELD_LENGTH];
    time_t dsTimeOfCreation;
    time_t dsTimeOfLastAttempt;
    time_t dsTimeOfLastSuccess;
    uint64_t dsFailuresSinceLastSuccess;
    uint64_t dsFailures;
    uint64_t dsSuccesses;
    char dsURL[SNMP_FIELD_LENGTH];
};

/* operation statistics */
struct snmp_vars_t
{
    struct snmp_ops_tbl_t ops_tbl;
    struct snmp_entries_tbl_t entries_tbl;
    struct snmp_server_tbl_t server_tbl;
    struct snmp_int_tbl_t int_tbl[NUM_SNMP_INT_TBL_ROWS];
};

#define ENTRY_POINT_PS_WAKEUP_ALL 102
#define ENTRY_POINT_PS_SERVICE 105
#define ENTRY_POINT_DISCONNECT_SERVER 107
#define ENTRY_POINT_SLAPD_SSL_CLIENT_INIT 108
#define ENTRY_POINT_SLAPD_SSL_INIT 109
#define ENTRY_POINT_SLAPD_SSL_INIT2 110

typedef void (*ps_wakeup_all_fn_ptr)(void);
typedef void (*ps_service_fn_ptr)(Slapi_Entry *, Slapi_Entry *, int, int);
typedef char *(*get_config_dn_fn_ptr)(void);
typedef void (*get_disconnect_server_fn_ptr)(Connection *conn, PRUint64 opconnid, int opid, PRErrorCode reason, PRInt32 error);
typedef int (*modify_config_dse_fn_ptr)(Slapi_PBlock *pb);
typedef int (*slapd_ssl_init_fn_ptr)(void);
typedef int (*slapd_ssl_init_fn_ptr2)(PRFileDesc **s, int StartTLS);

/*
 * A structure of entry points in the NT exe which need
 * to be available to DLLs.
 */
typedef struct _slapdEntryPoints
{
    caddr_t sep_ps_wakeup_all;
    caddr_t sep_ps_service;
    caddr_t sep_disconnect_server;
    caddr_t sep_slapd_ssl_init;
    caddr_t sep_slapd_ssl_init2;
} slapdEntryPoints;

#define DLL_IMPORT_DATA

/* Log types */
#define SLAPD_ACCESS_LOG 0x1
#define SLAPD_ERROR_LOG 0x2
#define SLAPD_AUDIT_LOG 0x4
#define SLAPD_AUDITFAIL_LOG 0x8
#define SLAPD_SECURITY_LOG 0x10

/* Security log events */
#define SECURITY_BIND_SUCCESS "BIND_SUCCESS"
#define SECURITY_BIND_FAILED "BIND_FAILED"
#define SECURITY_AUTHZ_ERROR "AUTHZ_ERROR"
#define SECURITY_TCP_ERROR "TCP_ERROR"

/* Security log messages */
#define SECURITY_MSG_INVALID_PASSWD "INVALID_PASSWORD"
#define SECURITY_MSG_NO_ENTRY "NO_SUCH_ENTRY"
#define SECURITY_MSG_CERT_MAP_FAILED "CERT_MAP_FAILED"
#define SECURITY_MSG_ACCOUNT_LOCKED "ACCOUNT_LOCKED"
#define SECURITY_MSG_ANONYMOUS_BIND "ANONYMOUS_BIND"

#define LOGGING_BACKEND_INTERNAL 0x1
#define LOGGING_BACKEND_SYSLOG 0x2
#ifdef HAVE_JOURNALD
#define LOGGING_BACKEND_JOURNALD 0x4
#endif

#define CONFIG_DATABASE_ATTRIBUTE "nsslapd-database"
#define CONFIG_PLUGIN_ATTRIBUTE "nsslapd-plugin"
#define CONFIG_SIZELIMIT_ATTRIBUTE "nsslapd-sizelimit"
#define CONFIG_PAGEDSIZELIMIT_ATTRIBUTE "nsslapd-pagedsizelimit"
#define CONFIG_TIMELIMIT_ATTRIBUTE "nsslapd-timelimit"
#define CONFIG_SUFFIX_ATTRIBUTE "nsslapd-suffix"
#define CONFIG_READONLY_ATTRIBUTE "nsslapd-readonly"
#define CONFIG_REFERRAL_ATTRIBUTE "nsslapd-referral"
#define CONFIG_OBJECTCLASS_ATTRIBUTE "nsslapd-objectclass"
#define CONFIG_ATTRIBUTE_ATTRIBUTE "nsslapd-attribute"
#define CONFIG_SCHEMACHECK_ATTRIBUTE "nsslapd-schemacheck"
#define CONFIG_SCHEMAMOD_ATTRIBUTE "nsslapd-schemamod"
#define CONFIG_SYNTAXCHECK_ATTRIBUTE "nsslapd-syntaxcheck"
#define CONFIG_SYNTAXLOGGING_ATTRIBUTE "nsslapd-syntaxlogging"
#define CONFIG_DN_VALIDATE_STRICT_ATTRIBUTE "nsslapd-dn-validate-strict"
#define CONFIG_DS4_COMPATIBLE_SCHEMA_ATTRIBUTE "nsslapd-ds4-compatible-schema"
#define CONFIG_SCHEMA_IGNORE_TRAILING_SPACES "nsslapd-schema-ignore-trailing-spaces"
#define CONFIG_SCHEMAREPLACE_ATTRIBUTE "nsslapd-schemareplace"
#define CONFIG_LOGLEVEL_ATTRIBUTE "nsslapd-errorlog-level"
#define CONFIG_ACCESSLOGLEVEL_ATTRIBUTE "nsslapd-accesslog-level"
#define CONFIG_STATLOGLEVEL_ATTRIBUTE "nsslapd-statlog-level"
#define CONFIG_SECURITYLOGLEVEL_ATTRIBUTE "nsslapd-securitylog-level"
#define CONFIG_ACCESSLOG_MODE_ATTRIBUTE "nsslapd-accesslog-mode"
#define CONFIG_SECURITYLOG_MODE_ATTRIBUTE "nsslapd-securitylog-mode"
#define CONFIG_ERRORLOG_MODE_ATTRIBUTE "nsslapd-errorlog-mode"
#define CONFIG_AUDITLOG_MODE_ATTRIBUTE "nsslapd-auditlog-mode"
#define CONFIG_AUDITFAILLOG_MODE_ATTRIBUTE "nsslapd-auditfaillog-mode"
#define CONFIG_ACCESSLOG_MAXNUMOFLOGSPERDIR_ATTRIBUTE "nsslapd-accesslog-maxlogsperdir"
#define CONFIG_SECURITYLOG_MAXNUMOFLOGSPERDIR_ATTRIBUTE "nsslapd-securitylog-maxlogsperdir"
#define CONFIG_ERRORLOG_MAXNUMOFLOGSPERDIR_ATTRIBUTE "nsslapd-errorlog-maxlogsperdir"
#define CONFIG_AUDITLOG_MAXNUMOFLOGSPERDIR_ATTRIBUTE "nsslapd-auditlog-maxlogsperdir"
#define CONFIG_AUDITFAILLOG_MAXNUMOFLOGSPERDIR_ATTRIBUTE "nsslapd-auditfaillog-maxlogsperdir"
#define CONFIG_ACCESSLOG_MAXLOGSIZE_ATTRIBUTE "nsslapd-accesslog-maxlogsize"
#define CONFIG_SECURITYLOG_MAXLOGSIZE_ATTRIBUTE "nsslapd-securitylog-maxlogsize"
#define CONFIG_ERRORLOG_MAXLOGSIZE_ATTRIBUTE "nsslapd-errorlog-maxlogsize"
#define CONFIG_AUDITLOG_MAXLOGSIZE_ATTRIBUTE "nsslapd-auditlog-maxlogsize"
#define CONFIG_AUDITFAILLOG_MAXLOGSIZE_ATTRIBUTE "nsslapd-auditfaillog-maxlogsize"
#define CONFIG_ACCESSLOG_LOGROTATIONSYNCENABLED_ATTRIBUTE "nsslapd-accesslog-logrotationsync-enabled"
#define CONFIG_SECURITYLOG_LOGROTATIONSYNCENABLED_ATTRIBUTE "nsslapd-securitylog-logrotationsync-enabled"
#define CONFIG_ERRORLOG_LOGROTATIONSYNCENABLED_ATTRIBUTE "nsslapd-errorlog-logrotationsync-enabled"
#define CONFIG_AUDITLOG_LOGROTATIONSYNCENABLED_ATTRIBUTE "nsslapd-auditlog-logrotationsync-enabled"
#define CONFIG_AUDITFAILLOG_LOGROTATIONSYNCENABLED_ATTRIBUTE "nsslapd-auditfaillog-logrotationsync-enabled"
#define CONFIG_ACCESSLOG_LOGROTATIONSYNCHOUR_ATTRIBUTE "nsslapd-accesslog-logrotationsynchour"
#define CONFIG_SECURITYLOG_LOGROTATIONSYNCHOUR_ATTRIBUTE "nsslapd-securitylog-logrotationsynchour"
#define CONFIG_ERRORLOG_LOGROTATIONSYNCHOUR_ATTRIBUTE "nsslapd-errorlog-logrotationsynchour"
#define CONFIG_AUDITLOG_LOGROTATIONSYNCHOUR_ATTRIBUTE "nsslapd-auditlog-logrotationsynchour"
#define CONFIG_AUDITFAILLOG_LOGROTATIONSYNCHOUR_ATTRIBUTE "nsslapd-auditfaillog-logrotationsynchour"
#define CONFIG_ACCESSLOG_LOGROTATIONSYNCMIN_ATTRIBUTE "nsslapd-accesslog-logrotationsyncmin"
#define CONFIG_SECURITYLOG_LOGROTATIONSYNCMIN_ATTRIBUTE "nsslapd-securitylog-logrotationsyncmin"
#define CONFIG_ERRORLOG_LOGROTATIONSYNCMIN_ATTRIBUTE "nsslapd-errorlog-logrotationsyncmin"
#define CONFIG_AUDITLOG_LOGROTATIONSYNCMIN_ATTRIBUTE "nsslapd-auditlog-logrotationsyncmin"
#define CONFIG_AUDITFAILLOG_LOGROTATIONSYNCMIN_ATTRIBUTE "nsslapd-auditfaillog-logrotationsyncmin"
#define CONFIG_ACCESSLOG_LOGROTATIONTIME_ATTRIBUTE "nsslapd-accesslog-logrotationtime"
#define CONFIG_SECURITYLOG_LOGROTATIONTIME_ATTRIBUTE "nsslapd-securitylog-logrotationtime"
#define CONFIG_ERRORLOG_LOGROTATIONTIME_ATTRIBUTE "nsslapd-errorlog-logrotationtime"
#define CONFIG_AUDITLOG_LOGROTATIONTIME_ATTRIBUTE "nsslapd-auditlog-logrotationtime"
#define CONFIG_AUDITFAILLOG_LOGROTATIONTIME_ATTRIBUTE "nsslapd-auditfaillog-logrotationtime"
#define CONFIG_ACCESSLOG_LOGROTATIONTIMEUNIT_ATTRIBUTE "nsslapd-accesslog-logrotationtimeunit"
#define CONFIG_SECURITYLOG_LOGROTATIONTIMEUNIT_ATTRIBUTE "nsslapd-securitylog-logrotationtimeunit"
#define CONFIG_ERRORLOG_LOGROTATIONTIMEUNIT_ATTRIBUTE "nsslapd-errorlog-logrotationtimeunit"
#define CONFIG_AUDITLOG_LOGROTATIONTIMEUNIT_ATTRIBUTE "nsslapd-auditlog-logrotationtimeunit"
#define CONFIG_AUDITFAILLOG_LOGROTATIONTIMEUNIT_ATTRIBUTE "nsslapd-auditfaillog-logrotationtimeunit"
#define CONFIG_ACCESSLOG_MAXLOGDISKSPACE_ATTRIBUTE "nsslapd-accesslog-logmaxdiskspace"
#define CONFIG_SECURITYLOG_MAXLOGDISKSPACE_ATTRIBUTE "nsslapd-securitylog-logmaxdiskspace"
#define CONFIG_ERRORLOG_MAXLOGDISKSPACE_ATTRIBUTE "nsslapd-errorlog-logmaxdiskspace"
#define CONFIG_AUDITLOG_MAXLOGDISKSPACE_ATTRIBUTE "nsslapd-auditlog-logmaxdiskspace"
#define CONFIG_AUDITFAILLOG_MAXLOGDISKSPACE_ATTRIBUTE "nsslapd-auditfaillog-logmaxdiskspace"
#define CONFIG_ACCESSLOG_MINFREEDISKSPACE_ATTRIBUTE "nsslapd-accesslog-logminfreediskspace"
#define CONFIG_SECURITYLOG_MINFREEDISKSPACE_ATTRIBUTE "nsslapd-securitylog-logminfreediskspace"
#define CONFIG_ERRORLOG_MINFREEDISKSPACE_ATTRIBUTE "nsslapd-errorlog-logminfreediskspace"
#define CONFIG_AUDITLOG_MINFREEDISKSPACE_ATTRIBUTE "nsslapd-auditlog-logminfreediskspace"
#define CONFIG_AUDITFAILLOG_MINFREEDISKSPACE_ATTRIBUTE "nsslapd-auditfaillog-logminfreediskspace"
#define CONFIG_ACCESSLOG_LOGEXPIRATIONTIME_ATTRIBUTE "nsslapd-accesslog-logexpirationtime"
#define CONFIG_SECURITYLOG_LOGEXPIRATIONTIME_ATTRIBUTE "nsslapd-securitylog-logexpirationtime"
#define CONFIG_ERRORLOG_LOGEXPIRATIONTIME_ATTRIBUTE "nsslapd-errorlog-logexpirationtime"
#define CONFIG_AUDITLOG_LOGEXPIRATIONTIME_ATTRIBUTE "nsslapd-auditlog-logexpirationtime"
#define CONFIG_AUDITFAILLOG_LOGEXPIRATIONTIME_ATTRIBUTE "nsslapd-auditfaillog-logexpirationtime"
#define CONFIG_ACCESSLOG_LOGEXPIRATIONTIMEUNIT_ATTRIBUTE "nsslapd-accesslog-logexpirationtimeunit"
#define CONFIG_SECURITYLOG_LOGEXPIRATIONTIMEUNIT_ATTRIBUTE "nsslapd-securitylog-logexpirationtimeunit"
#define CONFIG_ERRORLOG_LOGEXPIRATIONTIMEUNIT_ATTRIBUTE "nsslapd-errorlog-logexpirationtimeunit"
#define CONFIG_AUDITLOG_LOGEXPIRATIONTIMEUNIT_ATTRIBUTE "nsslapd-auditlog-logexpirationtimeunit"
#define CONFIG_AUDITFAILLOG_LOGEXPIRATIONTIMEUNIT_ATTRIBUTE "nsslapd-auditfaillog-logexpirationtimeunit"
#define CONFIG_ACCESSLOG_LOGGING_ENABLED_ATTRIBUTE "nsslapd-accesslog-logging-enabled"
#define CONFIG_SECURITYLOG_LOGGING_ENABLED_ATTRIBUTE "nsslapd-securitylog-logging-enabled"
#define CONFIG_ERRORLOG_LOGGING_ENABLED_ATTRIBUTE "nsslapd-errorlog-logging-enabled"
#define CONFIG_EXTERNAL_LIBS_DEBUG_ENABLED "nsslapd-external-libs-debug-enabled"
#define CONFIG_AUDITLOG_LOGGING_ENABLED_ATTRIBUTE "nsslapd-auditlog-logging-enabled"
#define CONFIG_AUDITFAILLOG_LOGGING_ENABLED_ATTRIBUTE "nsslapd-auditfaillog-logging-enabled"
#define CONFIG_AUDITLOG_LOGGING_HIDE_UNHASHED_PW "nsslapd-auditlog-logging-hide-unhashed-pw"
#define CONFIG_AUDITFAILLOG_LOGGING_HIDE_UNHASHED_PW "nsslapd-auditfaillog-logging-hide-unhashed-pw"
#define CONFIG_ACCESSLOG_COMPRESS_ENABLED_ATTRIBUTE "nsslapd-accesslog-compress"
#define CONFIG_SECURITYLOG_COMPRESS_ENABLED_ATTRIBUTE "nsslapd-securitylog-compress"
#define CONFIG_AUDITLOG_COMPRESS_ENABLED_ATTRIBUTE "nsslapd-auditlog-compress"
#define CONFIG_AUDITFAILLOG_COMPRESS_ENABLED_ATTRIBUTE "nsslapd-auditfaillog-compress"
#define CONFIG_ERRORLOG_COMPRESS_ENABLED_ATTRIBUTE "nsslapd-errorlog-compress"
#define CONFIG_UNHASHED_PW_SWITCH_ATTRIBUTE "nsslapd-unhashed-pw-switch"
#define CONFIG_ROOTDN_ATTRIBUTE "nsslapd-rootdn"
#define CONFIG_ROOTPW_ATTRIBUTE "nsslapd-rootpw"
#define CONFIG_ROOTPWSTORAGESCHEME_ATTRIBUTE "nsslapd-rootpwstoragescheme"
#define CONFIG_AUDITFILE_ATTRIBUTE "nsslapd-auditlog"
#define CONFIG_AUDITFAILFILE_ATTRIBUTE "nsslapd-auditfaillog"
#define CONFIG_SECURITYFILE_ATTRIBUTE "nsslapd-securitylog"
#define CONFIG_LASTMOD_ATTRIBUTE "nsslapd-lastmod"
#define CONFIG_INCLUDE_ATTRIBUTE "nsslapd-include"
#define CONFIG_DYNAMICCONF_ATTRIBUTE "nsslapd-dynamicconf"
#define CONFIG_USEROC_ATTRIBUTE "nsslapd-useroc"
#define CONFIG_USERAT_ATTRIBUTE "nsslapd-userat"
#define CONFIG_SVRTAB_ATTRIBUTE "nsslapd-svrtab"
#define CONFIG_UNAUTH_BINDS_ATTRIBUTE "nsslapd-allow-unauthenticated-binds"
#define CONFIG_REQUIRE_SECURE_BINDS_ATTRIBUTE "nsslapd-require-secure-binds"
#define CONFIG_CLOSE_ON_FAILED_BIND "nsslapd-close-on-failed-bind"
#define CONFIG_ANON_ACCESS_ATTRIBUTE "nsslapd-allow-anonymous-access"
#define CONFIG_LOCALSSF_ATTRIBUTE "nsslapd-localssf"
#define CONFIG_MINSSF_ATTRIBUTE "nsslapd-minssf"
#define CONFIG_MINSSF_EXCLUDE_ROOTDSE "nsslapd-minssf-exclude-rootdse"
#define CONFIG_VALIDATE_CERT_ATTRIBUTE "nsslapd-validate-cert"
#define CONFIG_LOCALUSER_ATTRIBUTE "nsslapd-localuser"
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
#define CONFIG_LDAPI_AUTH_MAP_BASE_ATTRIBUTE "nsslapd-ldapiDNMappingBase"
#define CONFIG_LDAPI_AUTH_USERNAME_ATTRIBUTE "nsslapd-ldapiUsername"
#define CONFIG_LDAPI_AUTH_DN_ATTRIBUTE "nsslapd-authenticateAsDN"
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
#define CONFIG_MAXDESCRIPTORS_ATTRIBUTE "nsslapd-maxdescriptors"
#define CONFIG_NUM_LISTENERS_ATTRIBUTE "nsslapd-numlisteners"
#define CONFIG_RESERVEDESCRIPTORS_ATTRIBUTE "nsslapd-reservedescriptors"
#define CONFIG_IDLETIMEOUT_ATTRIBUTE "nsslapd-idletimeout"
#define CONFIG_IOBLOCKTIMEOUT_ATTRIBUTE "nsslapd-ioblocktimeout"
#define CONFIG_ACCESSCONTROL_ATTRIBUTE "nsslapd-accesscontrol"
#define CONFIG_GROUPEVALNESTLEVEL_ATTRIBUTE "nsslapd-groupevalnestlevel"
#define CONFIG_NAGLE_ATTRIBUTE "nsslapd-nagle"
#define CONFIG_PWPOLICY_LOCAL_ATTRIBUTE "nsslapd-pwpolicy-local"
#define CONFIG_PWPOLICY_INHERIT_GLOBAL_ATTRIBUTE "nsslapd-pwpolicy-inherit-global"
#define CONFIG_ALLOW_HASHED_PW_ATTRIBUTE "nsslapd-allow-hashed-passwords"
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
#define CONFIG_PW_PALINDROME_ATTRIBUTE "passwordPalindrome"
#define CONFIG_PW_MAX_SEQ_ATTRIBUTE "passwordMaxSequence"
#define CONFIG_PW_MAX_SEQ_SETS_ATTRIBUTE "passwordMaxSeqSets"
#define CONFIG_PW_MAX_CLASS_CHARS_ATTRIBUTE "passwordMaxClassChars"
#define CONFIG_PW_CHECK_DICT_ATTRIBUTE "passwordDictCheck"
#define CONFIG_PW_DICT_PATH_ATTRIBUTE "passwordDictPath"
#define CONFIG_PW_USERATTRS_ATTRIBUTE "passwordUserAttributes"
#define CONFIG_PW_BAD_WORDS_ATTRIBUTE "passwordBadWords"
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
#define CONFIG_PW_TPR_MAXUSE "passwordTPRMaxUse"
#define CONFIG_PW_TPR_DELAY_EXPIRE_AT "passwordTPRDelayExpireAt"
#define CONFIG_PW_TPR_DELAY_VALID_FROM "passwordTPRDelayValidFrom"
#define CONFIG_PW_ISGLOBAL_ATTRIBUTE "passwordIsGlobalPolicy"
#define CONFIG_PW_GRACELIMIT_ATTRIBUTE "passwordGraceLimit"
#define CONFIG_PW_IS_LEGACY "passwordLegacyPolicy"
#define CONFIG_PW_TRACK_LAST_UPDATE_TIME "passwordTrackUpdateTime"
#define CONFIG_PW_ADMIN_DN_ATTRIBUTE "passwordAdminDN"
#define CONFIG_PW_ADMIN_SKIP_INFO_ATTRIBUTE "passwordAdminSkipInfoUpdate"
#define CONFIG_PW_SEND_EXPIRING "passwordSendExpiringTime"
#define CONFIG_ACCESSLOG_BUFFERING_ATTRIBUTE "nsslapd-accesslog-logbuffering"
#define CONFIG_SECURITYLOG_BUFFERING_ATTRIBUTE "nsslapd-securitylog-logbuffering"
#define CONFIG_CSNLOGGING_ATTRIBUTE "nsslapd-csnlogging"
#define CONFIG_RETURN_EXACT_CASE_ATTRIBUTE "nsslapd-return-exact-case"
#define CONFIG_RESULT_TWEAK_ATTRIBUTE "nsslapd-result-tweak"
#define CONFIG_REFERRAL_MODE_ATTRIBUTE "nsslapd-referralmode"
#define CONFIG_ATTRIBUTE_NAME_EXCEPTION_ATTRIBUTE "nsslapd-attribute-name-exceptions"
#define CONFIG_MAXBERSIZE_ATTRIBUTE "nsslapd-maxbersize"
#define CONFIG_MAXSASLIOSIZE_ATTRIBUTE "nsslapd-maxsasliosize"
#define CONFIG_MAX_FILTER_NEST_LEVEL_ATTRIBUTE "nsslapd-max-filter-nest-level"
#define CONFIG_VERSIONSTRING_ATTRIBUTE "nsslapd-versionstring"
#define CONFIG_ENQUOTE_SUP_OC_ATTRIBUTE "nsslapd-enquote-sup-oc"
#define CONFIG_BASEDN_ATTRIBUTE "nsslapd-certmap-basedn"
#define CONFIG_ACCESSLOG_LIST_ATTRIBUTE "nsslapd-accesslog-list"
#define CONFIG_SECURITYLOG_LIST_ATTRIBUTE "nsslapd-securitylog-list"
#define CONFIG_ERRORLOG_LIST_ATTRIBUTE "nsslapd-errorlog-list"
#define CONFIG_AUDITLOG_LIST_ATTRIBUTE "nsslapd-auditlog-list"
#define CONFIG_AUDITLOG_DISPLAY_ATTRS "nsslapd-auditlog-display-attrs"
#define CONFIG_AUDITFAILLOG_LIST_ATTRIBUTE "nsslapd-auditfaillog-list"
#define CONFIG_REWRITE_RFC1274_ATTRIBUTE "nsslapd-rewrite-rfc1274"
#define CONFIG_PLUGIN_BINDDN_TRACKING_ATTRIBUTE "nsslapd-plugin-binddn-tracking"
#define CONFIG_MODDN_ACI_ATTRIBUTE "nsslapd-moddn-aci"
#define CONFIG_TARGETFILTER_CACHE_ATTRIBUTE "nsslapd-targetfilter-cache"
#define CONFIG_GLOBAL_BACKEND_LOCK "nsslapd-global-backend-lock"
#define CONFIG_ENABLE_NUNC_STANS "nsslapd-enable-nunc-stans"
#define CONFIG_ENABLE_UPGRADE_HASH "nsslapd-enable-upgrade-hash"
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
#define CONFIG_TLS_CHECK_CRL_ATTRIBUTE "nsslapd-tls-check-crl"
#define CONFIG_HASH_FILTERS_ATTRIBUTE "nsslapd-hash-filters"
#define CONFIG_OUTBOUND_LDAP_IO_TIMEOUT_ATTRIBUTE "nsslapd-outbound-ldap-io-timeout"
#define CONFIG_FORCE_SASL_EXTERNAL_ATTRIBUTE "nsslapd-force-sasl-external"
#define CONFIG_ENTRYUSN_GLOBAL "nsslapd-entryusn-global"
#define CONFIG_ENTRYUSN_IMPORT_INITVAL "nsslapd-entryusn-import-initval"
#define CONFIG_DEFAULT_NAMING_CONTEXT "nsslapd-defaultnamingcontext"
#define CONFIG_DISK_MONITORING "nsslapd-disk-monitoring"
#define CONFIG_DISK_THRESHOLD_READONLY "nsslapd-disk-monitoring-readonly-on-threshold"
#define CONFIG_DISK_THRESHOLD "nsslapd-disk-monitoring-threshold"
#define CONFIG_DISK_GRACE_PERIOD "nsslapd-disk-monitoring-grace-period"
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
#define CONFIG_PLUGIN_LOGGING "nsslapd-plugin-logging"
#define CONFIG_LISTEN_BACKLOG_SIZE "nsslapd-listen-backlog-size"
#define CONFIG_DYNAMIC_PLUGINS "nsslapd-dynamic-plugins"
#define CONFIG_RETURN_DEFAULT_OPATTR "nsslapd-return-default-opattr"
#define CONFIG_REFERRAL_CHECK_PERIOD "nsslapd-referral-check-period"
#define CONFIG_RETURN_ENTRY_DN "nsslapd-return-original-entrydn"

#define CONFIG_CN_USES_DN_SYNTAX_IN_DNS "nsslapd-cn-uses-dn-syntax-in-dns"

#define CONFIG_MAXSIMPLEPAGED_PER_CONN_ATTRIBUTE "nsslapd-maxsimplepaged-per-conn"
#define CONFIG_LOGGING_BACKEND "nsslapd-logging-backend"

#define CONFIG_EXTRACT_PEM "nsslapd-extract-pemfiles"

#ifdef HAVE_CLOCK_GETTIME
#define CONFIG_LOGGING_HR_TIMESTAMPS "nsslapd-logging-hr-timestamps-enabled"
#endif

/* getenv alternative */
#define CONFIG_MALLOC_MXFAST         "nsslapd-malloc-mxfast"
#define CONFIG_MALLOC_TRIM_THRESHOLD "nsslapd-malloc-trim-threshold"
#define CONFIG_MALLOC_MMAP_THRESHOLD "nsslapd-malloc-mmap-threshold"

#define DEFAULT_MALLOC_UNSET_STR "-10"
#define DEFAULT_MALLOC_UNSET (-10)

#define CONFIG_VERIFY_FILTER_SCHEMA  "nsslapd-verify-filter-schema"
#define CONFIG_ENABLE_LDAPSSOTOKEN   "nsslapd-enable-ldapssotoken"
#define CONFIG_LDAPSSOTOKEN_SECRET   "nsslapd-ldapssotoken-secret"
#define CONFIG_LDAPSSOTOKEN_TTL      "nsslapd-ldapssotoken-ttl-secs"

#define CONFIG_TCP_FIN_TIMEOUT       "nsslapd-tcp-fin-timeout"
#define CONFIG_TCP_KEEPALIVE_TIME    "nsslapd-tcp-keepalive-time"

/*
 * Define the backlog number for use in listen() call.
 * We use the same definition as in ldapserver/include/base/systems.h
 */
#ifndef DAEMON_LISTEN_SIZE
#define DAEMON_LISTEN_SIZE 128
#define DAEMON_LISTEN_SIZE_STR "128"
#endif
#define CONFIG_IGNORE_TIME_SKEW "nsslapd-ignore-time-skew"

/* flag used to indicate that the change to the config parameter should be saved */
#define CONFIG_APPLY 1

/* This should be cleaned up and REMOVED.
 * Apparently it's SLOWER than just straight lock.
 */
#define SLAPI_CFG_USE_RWLOCK 0
#if SLAPI_CFG_USE_RWLOCK == 0
#define CFG_LOCK_READ(cfg) PR_Lock(cfg->cfg_lock)
#define CFG_UNLOCK_READ(cfg) PR_Unlock(cfg->cfg_lock)
#define CFG_LOCK_WRITE(cfg) PR_Lock(cfg->cfg_lock)
#define CFG_UNLOCK_WRITE(cfg) PR_Unlock(cfg->cfg_lock)
#else
#define CFG_LOCK_READ(cfg) slapi_rwlock_rdlock(cfg->cfg_rwlock)
#define CFG_UNLOCK_READ(cfg) slapi_rwlock_unlock(cfg->cfg_rwlock)
#define CFG_LOCK_WRITE(cfg) slapi_rwlock_wrlock(cfg->cfg_rwlock)
#define CFG_UNLOCK_WRITE(cfg) slapi_rwlock_unlock(cfg->cfg_rwlock)
#endif

#define REFER_MODE_OFF 0
#define REFER_MODE_ON 1

#define MAX_ALLOWED_TIME_IN_SECS             2147483647
#define MAX_ALLOWED_TIME_IN_SECS_64 9223372036854775807

/*
 * DO NOT CHANGE THIS VALUE.
 *
 * if you want to update the default password hash you need to
 * edit pw.c pw_name2scheme. This is because we use environment
 * factors to dynamically determine this.
 */
#define DEFAULT_PASSWORD_SCHEME_NAME "DEFAULT"

typedef struct _slapdFrontendConfig
{
#if SLAPI_CFG_USE_RWLOCK == 1
    Slapi_RWLock *cfg_rwlock; /* read/write lock to serialize access */
#else
    PRLock *cfg_lock;
#endif
    struct pw_scheme *rootpwstoragescheme;
    slapi_onoff_t accesscontrol;
    int groupevalnestlevel;
    int idletimeout;
    slapi_int_t ioblocktimeout;
    slapi_onoff_t lastmod;
    int64_t maxdescriptors;
    int num_listeners;
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
    tls_check_crl_t tls_check_crl;
    int validate_cert;
    int sizelimit;
    int SNMPenabled;
    char *SNMPdescription;
    char *SNMPorganization;
    char *SNMPlocation;
    char *SNMPcontact;
    int32_t threadnumber;
    int timelimit;
    char *accesslog;
    struct berval **defaultreferral;
    char *encryptionalias;
    char *errorlog;
    char *listenhost;
    int snmp_index;
    char *localuser;
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
    slapi_onoff_t moddn_aci;
    slapi_onoff_t targetfilter_cache;
    struct pw_scheme *pw_storagescheme;
    slapi_onoff_t pwpolicy_local;
    slapi_onoff_t pw_is_global_policy;
    slapi_onoff_t pwpolicy_inherit_global;
    slapi_onoff_t allow_hashed_pw;
    passwdPolicy pw_policy;
    slapi_onoff_t pw_policy_inherit_global;

    /* ACCESS LOG */
    slapi_onoff_t accesslog_logging_enabled;
    char *accesslog_mode;
    int accesslog_maxnumlogs;
    int accesslog_maxlogsize;
    slapi_onoff_t accesslog_rotationsync_enabled;
    int accesslog_rotationsynchour;
    int accesslog_rotationsyncmin;
    int accesslog_rotationtime;
    char *accesslog_rotationunit;
    int accesslog_maxdiskspace;
    int accesslog_minfreespace;
    int accesslog_exptime;
    char *accesslog_exptimeunit;
    int accessloglevel;
    slapi_onoff_t accesslogbuffering;
    slapi_onoff_t csnlogging;
    slapi_onoff_t accesslog_compress;
    int statloglevel;

    /* SECURITY LOG */
    char *securitylog;
    slapi_onoff_t securitylog_logging_enabled;
    char *securitylog_mode;
    int securitylog_maxnumlogs;
    int securitylog_maxlogsize;
    slapi_onoff_t securitylog_rotationsync_enabled;
    int securitylog_rotationsynchour;
    int securitylog_rotationsyncmin;
    int securitylog_rotationtime;
    char *securitylog_rotationunit;
    int securitylog_maxdiskspace;
    int securitylog_minfreespace;
    int securitylog_exptime;
    char *securitylog_exptimeunit;
    int securityloglevel;
    slapi_onoff_t securitylogbuffering;
    slapi_onoff_t securitylog_compress;

    /* ERROR LOG */
    slapi_onoff_t errorlog_logging_enabled;
    char *errorlog_mode;
    int errorlog_maxnumlogs;
    int errorlog_maxlogsize;
    slapi_onoff_t errorlog_rotationsync_enabled;
    int errorlog_rotationsynchour;
    int errorlog_rotationsyncmin;
    int errorlog_rotationtime;
    char *errorlog_rotationunit;
    int errorlog_maxdiskspace;
    int errorlog_minfreespace;
    int errorlog_exptime;
    char *errorlog_exptimeunit;
    int errorloglevel;
    slapi_onoff_t external_libs_debug_enabled;
    slapi_onoff_t errorlog_compress;

    /* AUDIT LOG */
    char *auditlog; /* replication audit file */
    int auditloglevel;
    slapi_onoff_t auditlog_logging_enabled;
    char *auditlog_mode;
    int auditlog_maxnumlogs;
    int auditlog_maxlogsize;
    slapi_onoff_t auditlog_rotationsync_enabled;
    int auditlog_rotationsynchour;
    int auditlog_rotationsyncmin;
    int auditlog_rotationtime;
    char *auditlog_rotationunit;
    int auditlog_maxdiskspace;
    int auditlog_minfreespace;
    int auditlog_exptime;
    char *auditlog_exptimeunit;
    slapi_onoff_t auditlog_logging_hide_unhashed_pw;
    slapi_onoff_t auditlog_compress;

    /* AUDIT FAIL LOG */
    char *auditfaillog;
    int auditfailloglevel;
    slapi_onoff_t auditfaillog_logging_enabled;
    char *auditfaillog_mode;
    int auditfaillog_maxnumlogs;
    int auditfaillog_maxlogsize;
    slapi_onoff_t auditfaillog_rotationsync_enabled;
    int auditfaillog_rotationsynchour;
    int auditfaillog_rotationsyncmin;
    int auditfaillog_rotationtime;
    char *auditfaillog_rotationunit;
    int auditfaillog_maxdiskspace;
    int auditfaillog_minfreespace;
    int auditfaillog_exptime;
    char *auditfaillog_exptimeunit;
    slapi_onoff_t auditfaillog_logging_hide_unhashed_pw;
    slapi_onoff_t auditfaillog_compress;

    char *logging_backend;
#ifdef HAVE_CLOCK_GETTIME
    slapi_onoff_t logging_hr_timestamps;
#endif
    slapi_onoff_t return_exact_case; /* Return attribute names with the same case
                                       as they appear in at.conf */

    slapi_onoff_t result_tweak;
    char *refer_url; /* for referral mode */
    int refer_mode;  /* for quick test */
    int slapd_type;  /* Directory type; Full or Lite */

    ber_len_t maxbersize;              /* Maximum BER element size we'll accept */
    slapi_int_t max_filter_nest_level; /* deepest nested filter we will accept */
    slapi_onoff_t enquote_sup_oc;      /* put single quotes around an oc's
                                         superior oc in cn=schema */

    char *certmap_basedn; /* Default Base DN for certmap */

    char *workingdir;                     /* full path of directory before detach */
    char *configdir;                      /* full path name of directory containing configuration files */
    char *schemadir;                      /* full path name of directory containing schema files */
    char *instancedir;                    /* full path name of instance directory */
    char *lockdir;                        /* full path name of directory containing lock files */
    char *tmpdir;                         /* full path name of directory containing tmp files */
    char *certdir;                        /* full path name of directory containing cert files */
    char *ldifdir;                        /* full path name of directory containing ldif files */
    char *bakdir;                         /* full path name of directory containing bakup files */
    char *rundir;                         /* where pid, snmp stats, and ldapi files go */
    char *saslpath;                       /* full path name of directory containing sasl plugins */
    slapi_onoff_t attrname_exceptions;    /* if true, allow questionable attribute names */
    slapi_onoff_t rewrite_rfc1274;        /* return attrs for both v2 and v3 names */
    char *schemareplace;                  /* see CONFIG_SCHEMAREPLACE_* #defines below */
    char *ldapi_filename;                 /* filename for ldapi socket */
    slapi_onoff_t ldapi_switch;           /* switch to turn ldapi on/off */
    slapi_onoff_t ldapi_bind_switch;      /* switch to turn ldapi auto binding on/off */
    char *ldapi_root_dn;                  /* DN to map root to over LDAPI. Obsolete setting. rootds is used instead */
    slapi_onoff_t ldapi_map_entries;      /* turns ldapi entry bind mapping on/off */
    char *ldapi_uidnumber_type;           /* type that contains uid number */
    char *ldapi_gidnumber_type;           /* type that contains gid number */
    char *ldapi_search_base_dn;           /* base dn to search for mapped entries */
    char *ldapi_auto_dn_suffix;           /* suffix to be appended to auto gen DNs */
    char *ldapi_auto_mapping_base;        /* suffix/subtree containing LDAPI mapping entries */
    slapi_onoff_t slapi_counters;         /* switch to turn slapi_counters on/off */
    slapi_onoff_t allow_unauth_binds;     /* switch to enable/disable unauthenticated binds */
    slapi_onoff_t require_secure_binds;   /* switch to require simple binds to use a secure channel */
    slapi_onoff_t allow_anon_access;      /* switch to enable/disable anonymous access */
    int localssf;                         /* the security strength factor to assign to local conns (ldapi) */
    int minssf;                           /* minimum security strength factor (for SASL and SSL/TLS) */
    slapi_onoff_t minssf_exclude_rootdse; /* ON: minssf is ignored when searching rootdse */
    int32_t maxsasliosize;                /* limit incoming SASL IO packet size */
    char *anon_limits_dn;                 /* template entry for anonymous resource limits */
    slapi_int_t listen_backlog_size;      /* size of backlog parameter to PR_Listen */
    struct passwd *localuserinfo;         /* userinfo of localuser */
    slapi_onoff_t force_sasl_external;    /* force SIMPLE bind to be SASL/EXTERNAL if client cert credentials were supplied */
    slapi_onoff_t entryusn_global;        /* Entry USN: Use global counter */
    char *entryusn_import_init;           /* Entry USN: determine the initital value of import */
    int pagedsizelimit;
    char *default_naming_context;    /* Default naming context (normalized) */
    char *allowed_sasl_mechs;        /* comma/space separated list of allowed sasl mechs */
    char **allowed_sasl_mechs_array; /* Array of allow sasl mechs */
    int sasl_max_bufsize;            /* The max receive buffer size for SASL */
    slapi_onoff_t close_on_failed_bind;   /* Close connection following a failed bind */

    /* disk monitoring */
    slapi_onoff_t disk_monitoring;
    slapi_onoff_t disk_threshold_readonly;
    uint64_t disk_threshold;
    int disk_grace_period;
    slapi_onoff_t disk_logging_critical;

    /* normalized dn cache */
    slapi_onoff_t ndn_cache_enabled;
    uint64_t ndn_cache_max_size;

    slapi_onoff_t return_orig_type; /* if on, search returns original type set in attr list */
    slapi_onoff_t sasl_mapping_fallback;
    slapi_onoff_t ignore_vattrs;
    slapi_onoff_t unhashed_pw_switch; /* switch to on/off/nolog unhashed pw */
    slapi_onoff_t enable_turbo_mode;
    slapi_int_t connection_buffer;    /* values are CONNECTION_BUFFER_* below */
    slapi_onoff_t connection_nocanon; /* if "on" sets LDAP_OPT_X_SASL_NOCANON */
    slapi_onoff_t plugin_logging;     /* log all internal plugin operations */
    slapi_onoff_t ignore_time_skew;
    slapi_onoff_t dynamic_plugins;          /* allow plugins to be dynamically enabled/disabled */
    slapi_onoff_t cn_uses_dn_syntax_in_dns; /* indicates the cn value in dns has dn syntax */
    slapi_onoff_t global_backend_lock;
    slapi_int_t maxsimplepaged_per_conn; /* max simple paged results reqs handled per connection */
    slapi_onoff_t enable_nunc_stans; /* Despite the removal of NS, we have to leave the value in
                                      * case someone was setting it.
                                      */
#if defined(LINUX)
    int malloc_mxfast;         /* mallopt M_MXFAST */
    int malloc_trim_threshold; /* mallopt M_TRIM_THRESHOLD */
    int malloc_mmap_threshold; /* mallopt M_MMAP_THRESHOLD */
#endif
    slapi_onoff_t extract_pem; /* If "on", export key/cert as pem files */
    slapi_onoff_t enable_upgrade_hash; /* If on, upgrade hashes for PW at bind */
    /*
     * Do we verify the filters we recieve by schema?
     * reject-invalid - reject filter if there is anything invalid
     * process-safe - allow the filter, warn about what's invalid, and then idl_alloc(0) with rfc compliance
     * warn-invalid - allow the filter, warn about the invalid, and then do a ALLIDS (may lead to full table scan)
     * off - don't warn, just allow anything. This is the legacy behaviour.
     */
    slapi_special_filter_verify_t verify_filter_schema;
    /*
     * Do we enable generation of ldapssotokens (cookies) for re-binding?
     */
    slapi_onoff_t enable_ldapssotoken;
    char *ldapssotoken_secret;
    slapi_int_t ldapssotoken_ttl;

    slapi_int_t tcp_fin_timeout;
    slapi_int_t tcp_keepalive_time;
    int32_t referral_check_period;
    slapi_onoff_t return_orig_dn;
    slapi_onoff_t pw_admin_skip_info;
    char *auditlog_display_attrs;
} slapdFrontendConfig_t;

/* possible values for slapdFrontendConfig_t.schemareplace */
#define CONFIG_SCHEMAREPLACE_STR_OFF "off"
#define CONFIG_SCHEMAREPLACE_STR_ON "on"
#define CONFIG_SCHEMAREPLACE_STR_REPLICATION_ONLY "replication-only"

/* Those define are used when applying the policies for the replication of the schema */
/* Used to determine if we are looking at the policies from the supplier (push) or consumer (receive) side */
#define REPL_SCHEMA_AS_CONSUMER 0
#define REPL_SCHEMA_AS_SUPPLIER 1
/* Used to determine if we are looking at objectclass/attribute policies */
#define REPL_SCHEMA_OBJECTCLASS 0
#define REPL_SCHEMA_ATTRIBUTE 1
/* Used to determine the schema replication: never or based on values */
#define REPL_SCHEMA_UPDATE_NEVER 1
#define REPL_SCHEMA_UPDATE_ON_VALUE 2
/* Used to determine the action when schema replition is based on values */
#define REPL_SCHEMA_UPDATE_ACCEPT_VALUE 3
#define REPL_SCHEMA_UPDATE_REJECT_VALUE 4
#define REPL_SCHEMA_UPDATE_UNKNOWN_VALUE 5

/* entries containing the policies */
#define ENTRY_REPL_SCHEMA_TOP "cn=replSchema, cn=config"
#define ENTRY_REPL_SCHEMA_SUPPLIER "cn=supplierUpdatePolicy," ENTRY_REPL_SCHEMA_TOP
#define ENTRY_REPL_SCHEMA_CONSUMER "cn=consumerUpdatePolicy," ENTRY_REPL_SCHEMA_TOP
/*
 * attribute name containing the policy action for
 * a given objectclass/attribute
 */
#define ATTR_SCHEMA_UPDATE_OBJECTCLASS_ACCEPT "schemaUpdateObjectclassAccept"
#define ATTR_SCHEMA_UPDATE_OBJECTCLASS_REJECT "schemaUpdateObjectclassReject"
#define ATTR_SCHEMA_UPDATE_ATTRIBUTE_ACCEPT "schemaUpdateAttributeAccept"
#define ATTR_SCHEMA_UPDATE_ATTRIBUTE_REJECT "schemaUpdateAttributeReject"
#define SUPPLIER_POLICY_1 ATTR_SCHEMA_UPDATE_OBJECTCLASS_ACCEPT ": printer-uri-oid\n"
#if defined(USE_OLD_UNHASHED)
#define SUPPLIER_POLICY_2 ATTR_SCHEMA_UPDATE_ATTRIBUTE_ACCEPT ": PSEUDO_ATTR_UNHASHEDUSERPASSWORD_OID\n"
#else
#define SUPPLIER_POLICY_2 ATTR_SCHEMA_UPDATE_ATTRIBUTE_ACCEPT ": 2.16.840.1.113730.3.1.2110\n"
#endif
#define DEFAULT_SUPPLIER_POLICY SUPPLIER_POLICY_1 SUPPLIER_POLICY_2
#define DEFAULT_CONSUMER_POLICY DEFAULT_SUPPLIER_POLICY

#define CONNECTION_BUFFER_OFF 0
#define CONNECTION_BUFFER_ON 1
#define CONNECTION_BUFFER_ADAPT 2


slapdFrontendConfig_t *getFrontendConfig(void);

/* LP: NO_TIME cannot be -1, it generates wrong GeneralizedTime
 * And causes some errors on AIX also
 */
#define NO_TIME (time_t)0                 /* cannot be -1, NT's localtime( -1 ) returns NULL */
#define NOT_FIRST_TIME (time_t)1          /* not the first logon */
#define SLAPD_END_TIME (time_t)2147483647 /* (2^31)-1, in 2038 */

extern char *attr_dataversion;
#define ATTR_DATAVERSION "dataVersion"
#define ATTR_WITH_OCTETSTRING_SYNTAX "userPassword"

#define SLAPD_SNMP_UPDATE_INTERVAL (10 * 1000) /* 10 seconds */

#ifndef LDAP_AUTH_KRBV41
#define LDAP_AUTH_KRBV41 0x81L
#endif
#ifndef LDAP_AUTH_KRBV42
#define LDAP_AUTH_KRBV42 0x82L
#endif

/* for timing certain operations */
#ifdef USE_TIMERS

#include <sys/time.h>
#ifdef LINUX
#define GTOD(t) gettimeofday(t, NULL)
#else
#define GTOD(t) gettimeofday(t)
#endif
#define TIMER_DECL(x) struct timeval x##_start, x##_end
#define TIMER_START(x) GTOD(&(x##_start))
#define TIMER_STOP(x) GTOD(&(x##_end))
#define TIMER_GET_US(x) (unsigned)(((x##_end).tv_sec - (x##_start).tv_sec) * 100000L + \
                                   ((x##_end).tv_usec - (x##_start).tv_usec))

#define TIMER_AVG_DECL(x) \
    TIMER_DECL(x);        \
    static unsigned int x##_total, x##_count
#define TIMER_AVG_START(x) \
    TIMER_START(x)
#define TIMER_AVG_STOP(x)                 \
    do {                                  \
        TIMER_STOP(x);                    \
        if (TIMER_GET_US(x) < 10000) {    \
            x##_count++;                  \
            x##_total += TIMER_GET_US(x); \
        }                                 \
    } while (0)
#define TIMER_AVG_GET_US(x) (unsigned int)(x##_total / x##_count)
#define TIMER_AVG_CHECK(x)                                     \
    do {                                                       \
        if (x##_count >= 1000) {                               \
            printf("timer %s: %d\n", #x, TIMER_AVG_GET_US(x)); \
            x##_total = x##_count = 0;                         \
        }                                                      \
    } while (0)

#else

#define TIMER_DECL(x)
#define TIMER_START(x)
#define TIMER_STOP(x)
#define TIMER_GET_US(x) 0L

#endif /* USE_TIMERS */

#define LDIF_CSNPREFIX_MAXLENGTH 6 /* sizeof(xxcsn-) */

#include "intrinsics.h"

/* printkey: import & export */
#define EXPORT_PRINTKEY 0x1
#define EXPORT_NOWRAP 0x2
#define EXPORT_APPENDMODE 0x4
#define EXPORT_MINIMAL_ENCODING 0x8
#define EXPORT_ID2ENTRY_ONLY 0x10
#define EXPORT_NOVERSION 0x20
#define EXPORT_APPENDMODE_1 0x40
#define EXPORT_INTERNAL 0x100

#define MTN_CONTROL_USE_ONE_BACKEND_OID     "2.16.840.1.113730.3.4.14"
#define MTN_CONTROL_USE_ONE_BACKEND_EXT_OID "2.16.840.1.113730.3.4.20"
#if defined(USE_OLD_UNHASHED)
#define PSEUDO_ATTR_UNHASHEDUSERPASSWORD_OID "2.16.840.1.113730.3.1.2110"
#endif

/* virtualListViewError is a relatively new concept that was added long
 * after we implemented VLV. Until added to LDAP SDK, we define
 * virtualListViewError here.  Once it's added, this define would go away. */
#ifndef LDAP_VIRTUAL_LIST_VIEW_ERROR
#define LDAP_VIRTUAL_LIST_VIEW_ERROR 0x4C /* 76 */
#endif

#define CHAR_OCTETSTRING (char)0x04

/* copied from replication/repl5.h */
#define RUV_STORAGE_ENTRY_UNIQUEID "ffffffff-ffffffff-ffffffff-ffffffff"

#define _SEC_PER_DAY 86400
#define _MAX_SHADOW 99999

/*
 * SERVER UPGRADE INTERNALS
 */
typedef enum _upgrade_status {
    UPGRADE_SUCCESS = 0,
    UPGRADE_FAILURE = 1,
} upgrade_status;

upgrade_status upgrade_server(void);
upgrade_status upgrade_repl_plugin_name(Slapi_Entry *plugin_entry, struct slapdplugin *plugin);
PRBool upgrade_plugin_removed(char *plg_libpath);

/* ldapi.c */
#if defined(ENABLE_LDAPI)
typedef enum {
    LDAPI_STARTUP,
    LDAPI_RELOAD,
    LDAPI_SHUTDOWN
} slapi_ldapi_state;

void initialize_ldapi_auth_dn_mappings(slapi_ldapi_state reload);
void free_ldapi_auth_dn_mappings(int32_t shutdown);
int32_t slapd_identify_local_user(Connection *conn);
int32_t slapd_bind_local_user(Connection *conn);
#endif

#endif /* _slap_h_ */

/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef PUBLIC_NSAPI_H
#define PUBLIC_NSAPI_H

/*
 * File:        nsapi.h
 *
 * Description:
 *
 *      This file defines an interface for extending the server with
 *      in-process plug-ins.
 */

#include "base/systems.h"

#if defined(FILE_UNIX_MMAP) || defined(FILE_WIN32_MMAP)
#define FILE_MMAP
#endif

/* --- Begin miscellaneous definitions --- */

/* Used in some places as a length limit on error messages */
#define MAGNUS_ERROR_LEN 1024

/* Carriage return and line feed */
#define CR 13
#define LF 10
#ifdef XP_WIN32
#define ENDLINE "\r\n"
#else
#define ENDLINE "\n"
#endif

/* mime.types file identification line */
#define NCC_MT_MAGIC "#--Netscape Communications Corporation MIME Information"
#define NCC_MT_MAGIC_LEN 55

/* Deprecated */
#define MCC_MT_MAGIC "#--Mosaic Communications Corporation MIME Information"
#define MCC_MT_MAGIC_LEN 53

/* The character which separates extensions with cinfo_find */
#define CINFO_SEPARATOR '.'

/* The maximum length of a line in a mime.types file */
#define CINFO_MAX_LEN 1024

/*
 * The maximum length of an error message. NOT RUN-TIME CHECKED
 */

#define MAX_ERROR_LEN 1024

/* A warning is a minor mishap, such as a 404 being issued. */
#define LOG_WARN 0

/* 
 * A misconfig is when there is a syntax error or permission violation in
 * a config. file.
 */
#define LOG_MISCONFIG 1

/* 
 * Security warnings are issued when authentication fails, or a host is
 * given a 403 return code.
 */
#define LOG_SECURITY 2

/*
 * A failure is when a request could not be fulfilled due to an internal
 * problem, such as a CGI script exiting prematurely, or a filesystem 
 * permissions problem.
 */
#define LOG_FAILURE 3

/*
 * A catastrophe is a fatal server error such as running out of
 * memory or processes, or a system call failing, or even a server crash. 
 * The server child cannot recover from a catastrophe.
 */
#define LOG_CATASTROPHE 4

/*
 * Informational message, of no concern.
 */
#define LOG_INFORM 5

/*
 * Internal log messages to be logged.  Internal use only.
 * Enable with "LogVerbose on" in magnus.conf
 */
#define LOG_VERBOSE 6

/*
 * The time format to use in the error log
 */

#define ERR_TIMEFMT "[%d/%b/%Y:%H:%M:%S]"


/* The fd you will get if you are reporting errors to SYSLOG */

#define ERRORS_TO_SYSLOG -1

/* Return codes from file I/O routines */
#define IO_OKAY 1
#define IO_ERROR -1
#define IO_EOF 0
#define NETBUF_EOF	-1
#define NETBUF_ERROR	-2

/* The disk page size on this machine. */
#define FILE_BUFFERSIZE 4096

#ifdef XP_UNIX

#define FILE_PATHSEP '/'
#define FILE_PARENT "../"

#elif defined(XP_WIN32)

#define FILE_PATHSEP '/'
#define FILE_PARENT "..\\"

#endif /* XP_WIN32 */

#define NET_INFINITE_TIMEOUT 0
#define NET_ZERO_TIMEOUT -1


/*
 * The following macros allow code to be written to use either the
 * shell expression API defined here, or the regular expression
 * pattern matching API defined in regexp.h.  Regular expressions
 * have a more complicated syntax, but also are more powerful.
 * Define the symbol, USE_REGEX to use regular expressions.  You
 * can include either or both of shexp.h and regexp.h, regardless
 * of whether USE_REGEX is defined or not, and the WILDPAT macros
 * will be defined appropriately.
 */

#ifdef USE_REGEX

#define WILDPAT_VALID(exp)              regexp_valid(exp)
#define WILDPAT_MATCH(str, exp)         regexp_match(str, exp)
#define WILDPAT_CMP(str, exp)           regexp_cmp(str, exp)
#define WILDPAT_CASECMP(str, exp)       regexp_casecmp(str, exp)
#define WILDPAT_USES_REGEXP             1

/* Define return codes from WILDPAT_VALID */
#define NON_WILDPAT     -1              /* exp is ordinary string */
#define INVALID_WILDPAT -2              /* exp is an invalid pattern */
#define VALID_WILDPAT   1               /* exp is a valid pattern */

#else

/* WILDPAT uses shell expressions */
#define WILDPAT_VALID(exp)              shexp_valid(exp)
#define WILDPAT_MATCH(str, exp)         shexp_match(str, exp)
#define WILDPAT_CMP(str, exp)           shexp_cmp(str, exp)
#define WILDPAT_CASECMP(str, exp)       shexp_casecmp(str, exp)
#define WILDPAT_USES_SHEXP              1

/* Define return codes from WILDPAT_VALID */
#define NON_WILDPAT     -1              /* exp is ordinary string */
#define INVALID_WILDPAT -2              /* exp is an invalid pattern */
#define VALID_WILDPAT   1               /* exp is a valid pattern */

#endif /* USE_REGEX */

/* Define return codes from regexp_valid and shexp_valid */
#define NON_SXP         NON_WILDPAT     /* exp is an ordinary string */
#define INVALID_SXP     INVALID_WILDPAT /* exp is an invalid shell exp */
#define VALID_SXP       VALID_WILDPAT   /* exp is a valid shell exp */

#ifdef USE_REGEX
/* and regexp versions */
#define NON_REGEXP      NON_SXP
#define INVALID_REGEXP  INVALID_SXP
#define VALID_REGEXP    VALID_SXP
#endif

#define SYSTHREAD_DEFAULT_PRIORITY 16

/* The longest line in the configuration file */
#define CONF_MAXLEN 16384

#define HTTP_DATE_LEN 128
#ifdef XP_UNIX
#define HTTP_DATE_FMT "%A, %d-%b-%y %T GMT"
#else /* XP_WIN32 */
#define HTTP_DATE_FMT "%A, %d-%b-%y %H:%M:%S GMT"
#endif /* XP_WIN32 */

/* HTTP status codes */
#define PROTOCOL_CONTINUE 100		/* HTTP/1.1 */
#define PROTOCOL_SWITCHING 101		/* HTTP/1.1 */
#define PROTOCOL_OK 200
#define PROTOCOL_CREATED 201
#define PROTOCOL_NO_RESPONSE 204
#define PROTOCOL_PARTIAL_CONTENT 206
#define PROTOCOL_REDIRECT 302
#define PROTOCOL_NOT_MODIFIED 304
#define PROTOCOL_BAD_REQUEST 400
#define PROTOCOL_UNAUTHORIZED 401
#define PROTOCOL_FORBIDDEN 403
#define PROTOCOL_NOT_FOUND 404
#define PROTOCOL_METHOD_NOT_ALLOWED 405	/*HTTP/1.1 */
#define PROTOCOL_PROXY_UNAUTHORIZED 407
#define PROTOCOL_CONFLICT 409			/* HTTP/1.1 */
#define PROTOCOL_LENGTH_REQUIRED 411	/*HTTP/1.1 */
#define PROTOCOL_PRECONDITION_FAIL 412 	/*HTTP/1.1 */
#define PROTOCOL_ENTITY_TOO_LARGE 413	/*HTTP/1.1 */
#define PROTOCOL_URI_TOO_LARGE 414		/*HTTP/1.1 */
#define PROTOCOL_SERVER_ERROR 500
#define PROTOCOL_NOT_IMPLEMENTED 501
#define PROTOCOL_VERSION_NOT_SUPPORTED 505 /*HTTP/1.1 */

#define CURRENT_PROTOCOL_VERSION 101

/* Definitions for HTTP over SSL */
#define HTTPS_PORT 443
#define HTTPS_URL "https"

/* Definitions for HTTP over TCP */
#define HTTP_PORT 80
#define HTTP_URL "http"


#define REQ_MAX_LINE 4096
    
/*
 * The REQ_ return codes. These codes are used to determine what the server
 * should do after a particular module completes its task.
 *
 * Func type functions return these as do many internal functions.
 */

/* The function performed its task, proceed with the request */
#define REQ_PROCEED 0
/* The entire request should be aborted: An error occurred */
#define REQ_ABORTED -1
/* The function performed no task, but proceed anyway. */
#define REQ_NOACTION -2
/* Tear down the session and exit */
#define REQ_EXIT -3
/* Restart the entire request-response process */
#define REQ_RESTART -4

/* --- End miscellaneous definitions --- */

/* --- Begin native platform includes --- */

#if defined(FILE_UNIX) || defined(FILE_UNIX_MMAP)
#include <sys/types.h>                  /* caddr_t */
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef FILE_WIN32
#include <direct.h>
#endif /* FILE_WIN32 */

#ifdef NET_WINSOCK
#include <winsock.h>
struct iovec {
	char		*iov_base;
    unsigned	iov_len;
};
#else
#if !defined(SUNOS4) && !defined(HPUX) && !defined(LINUX)
#include <sys/select.h>
#endif
#include <sys/time.h>    /* struct timeval */
#include <sys/socket.h>
#include <netinet/in.h> /* sockaddr and in_addr */
#include <sys/uio.h>
#endif /* NET_WINSOCK */

#include <sys/stat.h>

#include <ctype.h>  /* isspace */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#ifdef XP_UNIX
#include <dirent.h>
#include <pwd.h>                /* struct passwd */
#endif /* XP_UNIX */
#include "libadminutil/psetc.h"

/* --- End native platform includes --- */

/* --- Begin type definitions --- */

#ifndef SYS_FILE_T
typedef void *SYS_FILE;
#define SYS_FILE_T void *
#endif /* !SYS_FILE_T */

#define SYS_ERROR_FD ((SYS_FILE)-1)

#ifndef SYS_NETFD_T
typedef void *SYS_NETFD;
#define SYS_NETFD_T void *
#endif /* !SYS_NETFD_T */

/* Error value for a SYS_NETFD */
#ifndef SYS_NET_ERRORFD
#define SYS_NET_ERRORFD ((SYS_NETFD)-1)
#endif /* !SYS_NET_ERRORFD */

/*
 * Type:        filebuffer, filebuf_t
 *
 * Description:
 *
 *      This structure is used to represent a buffered file.  On some
 *      systems the file may be memory-mapped.  A filebuffer is created
 *      by filebuf_open(), and destroyed by filebuf_close().
 *
 * Notes:
 *
 *      Direct access to the members of this structure, not using
 *      macros defined here, is discouraged.
 *
 *      The filebuf alias that used to be defined for this type was
 *      found to conflict with a C++ class of the same name, so it
 *      has been renamed to filebuf_t.
 *
 *      The inbuf field used to be (char *), but is now (unsigned char *)
 *      to simplify handling of 8-bit data.  The value returned by the
 *      filebuf_getc() macro is the (unsigned char) casted to (int), or
 *      an error code.  Unfortunately, IO_EOF cannot be distinguished
 *      from a zero byte, but a new function, filebuf_iseof(), is
 *      provided that will indicate definitively whether EOF has been
 *      reached.
 */

#ifdef FILE_MMAP

/* Version of filebuffer when memory-mapped files are supported */
typedef struct {
    SYS_FILE fd;
#ifdef FILE_UNIX_MMAP
    caddr_t fp;
#else /* FILE_WIN32_MMAP */
    HANDLE fdmap;
    char *fp;
#endif /* FILE_UNIX_MMAP */
    int len;

    unsigned char *inbuf;   /* for buffer_grab */
    int cursize;

    int pos;
    char *errmsg;
} filebuffer;

/* Return next character or IO_EOF */
#define filebuf_getc(b) ((b)->pos == (b)->len ? IO_EOF : (int)((unsigned char *)(b)->fp)[(b)->pos++])

#define filebuf_iseof(b) ((b)->pos == (b)->len)

#else

/* Version of filebuffer with no memory-mapped file support */
typedef struct {
    SYS_FILE fd;

    int pos, cursize, maxsize;
    unsigned char *inbuf;
    char *errmsg;
} filebuffer;

/* Return next character, IO_EOF, or IO_ERROR */
#define filebuf_getc(b) \
 ((b)->pos != (b)->cursize ? (int)((b)->inbuf[(b)->pos++]) : filebuf_next(b,1))

#endif /* FILE_MMAP */

/* C++ streamio defines a filebuf class. */
typedef filebuffer filebuf_t;

#ifdef XP_WIN32
/* Use a filebuffer to read data from a pipe */
#define pipebuf_getc(b) \
 ((b)->pos != (b)->cursize ? (int)((b)->inbuf[(b)->pos++]) : pipebuf_next(b,1))
#endif /* XP_WIN32 */

/*
 * Type:        netbuf
 *
 * Description:
 *
 *      This structure is used to represent a buffered network socket.
 *      It is created by netbuf_open(), and destroyed by netbuf_close().
 *
 * Notes:
 *
 *      Direct access to the members of this structure, not using
 *      macros defined here, is discouraged.
 *
 *      The inbuf field used to be (unsigned char *), but is now
 *      simply (char *).  The value returned by the netbuf_getc()
 *      macro is (int).
 */

typedef struct {
    SYS_NETFD sd;

    int pos, cursize, maxsize, rdtimeout;
#ifdef XP_WIN32
    CHAR address[64];
#endif /* XP_WIN32 */
    unsigned char *inbuf;
    char *errmsg;
#ifndef XP_WIN32
    char address[64];
#endif /* !XP_WIN32 */
} netbuf;

/*
 * netbuf_getc gets a character from the given network buffer and returns 
 * it. (as an integer).
 * 
 * It will return (int) IO_ERROR for an error and (int) IO_EOF for
 * an error condition or EOF respectively.
 */

#define netbuf_getc(b) \
 ((b)->pos != (b)->cursize ? (int)((b)->inbuf[(b)->pos++]) : netbuf_next(b,1))

/*
 * buffer_error returns the last error that occurred with buffer. Don't use
 * this unless you know an error occurred. Independent of network/file type.
 */

#define buffer_error(b) ((b)->errmsg)

/*
 * Type:        cinfo
 *
 * Description:
 *
 *      This is a structure that captures the information in the name/value
 *      pairs on one line of a mime.types file.  A cinfo structure is
 *      stored in the memory-resident database, indexed by each of the
 *      file extensions specified in the "exts" name/value pair.  It
 *      defines various attributes of resources with names containing
 *      the specified file extensions.
 *
 * Notes:
 *
 *      Pointers to cinfo structures returned by this API may or may not
 *      need to freed by the caller.  See the individual function
 *      descriptions.
 *
 *      The strings referenced by the fields of cinfo structures returned
 *      by this API should be considered read-only, and do not need to be
 *      freed by the caller, even when the cinfo structure does.
 */

typedef struct {
    char *type;
    char *encoding;
    char *language;
} cinfo;


typedef void* CONDVAR;
typedef void *COUNTING_SEMAPHORE;
typedef void* CRITICAL;

#ifdef XP_UNIX
typedef struct passwd *PASSWD;
typedef DIR* SYS_DIR;
typedef struct dirent SYS_DIRENT;
#endif /* XP_UNIX */

#ifdef XP_WIN32

typedef struct {
    char *d_name;
} dirent_s;

typedef struct {
    HANDLE dp;
    WIN32_FIND_DATA fdata;
    dirent_s de;
} dir_s;

typedef dir_s* SYS_DIR;
typedef dirent_s SYS_DIRENT;

#endif /* XP_WIN32 */

typedef struct {
    char *name,*value;
} pb_param;

struct pb_entry {
    pb_param *param;
    struct pb_entry *next;
};

typedef struct {
    int hsize;
    struct pb_entry **ht;
} pblock;

#ifndef POOL_HANDLE_T
#define POOL_HANDLE_T
typedef void *pool_handle_t;
#endif

#ifndef SEMAPHORE_T
typedef void *SEMAPHORE;
#define SEMAPHORE_T void *
#endif /* !SEMAPHORE_T */

#define SESSION_HASHSIZE 5

typedef struct PListStruct_s PListStruct_s;
typedef struct ACLListHandle ACLListHandle;

typedef struct Session {
    /* Client-specific information */
    pblock *client;

    SYS_NETFD csd;
    netbuf *inbuf;
    int csd_open;

    struct in_addr iaddr;

#ifdef MCC_PROXY
    int req_cnt;
#endif

#ifdef MALLOC_POOLS
    pool_handle_t *pool;
#endif /* MALLOC_POOLS */

    void *clauth;		/* ACL client authentication information */
    struct Session *next;
	int fill;
    struct sockaddr_in local_addr;   /* local addr for this session*/

    PListStruct_s *subject;
} Session;

#if defined (SHMEM_UNIX_MMAP) || defined (SHMEM_WIN32_MMAP)
typedef struct {
    void *data;   /* the data */
#ifdef SHMEM_WIN32_MMAP
    HANDLE fdmap;
#endif /* SHMEM_WIN32_MMAP */
    int size;     /* the maximum length of the data */

    char *name;   /* internal use: filename to unlink if exposed */
    SYS_FILE fd;  /* internal use: file descriptor for region */
} shmem_s;
#endif  /* SHMEM_UNIX_MMAP || SHMEM_WIN32_MMAP */


/* Define a handle for a thread */
typedef void* SYS_THREAD;

/* Define an error value for the thread handle */
#define SYS_THREAD_ERROR NULL

/*
 * Hierarchy of httpd_object
 *
 * An object contains dtables. 
 * 
 * Each dtable is a table of directives that were entered of a certain type.
 * There is one dtable for each unique type of directive.
 *
 * Each dtable contains an array of directives, each of which is equivalent
 * to one directive that occurred in a config. file.
 *
 * It is up to the caller to determine how many dtables will be allocated
 * and to keep track of which of their directive types maps to which dtable
 * number.
 */


/*
 * directive is a structure containing the protection and parameters to an
 * instance of a directive within an httpd_object.
 *
 * param is the parameters, client is the protection.
 */

typedef struct {
    pblock *param;
    pblock *client;
} directive;

/*
 * dtable is a structure for creating tables of directives
 */

typedef struct {
    int ni;
    directive *inst;
} dtable;

/*
 * The httpd_object structure.
 *
 * The name pblock array contains the names for this object, such as its
 * virtual location, its physical location, or its identifier.
 * 
 * tmpl contains any templates allocated to this object.
 */

typedef struct {
    pblock *name;

    int nd;
    dtable *dt;
} httpd_object;

/*
 * httpd_objset is a container for a bunch of objects. obj is a 
 * NULL-terminated array of objects. pos points to the entry after the last
 * one in the array. You should not mess with pos, but can read it to find
 * the last entry.
 *
 * The initfns array is a NULL-terminated array of the Init functions 
 * associated with this object set. If there are no Init functions associated
 * with this object set, initfns can be NULL. Each pblock specifies the
 * parameters which are passed to the function when it's executed.
 */

typedef struct {
    int pos;
    httpd_object **obj;

    pblock **initfns;
} httpd_objset;


typedef struct {

    /* What port we listen to */
    int Vport;
#define server_portnum conf_getglobals()->Vport

    /* What address to bind to */
    char *Vaddr;

    /* User to run as */
#define user_pw conf_getglobals()->Vuserpw
    struct passwd *Vuserpw;

    /* Directory to chroot to */
    char *Vchr;

    /* Where to log our pid to */
    char *Vpidfn;

#define pool_max conf_getglobals()->Vpool_max
    int Vpool_max;                              /* OBSOLETE */
#define pool_min conf_getglobals()->Vpool_min
    int Vpool_min;                              /* OBSOLETE */
#define pool_life conf_getglobals()->Vpool_life
    int Vpool_life;                             /* OBSOLETE */

    /* For multiprocess UNIX servers, the maximum threads per process */
#define pool_maxthreads conf_getglobals()->Vpool_maxthreads
    int Vpool_maxthreads;

#define pool_minthreads conf_getglobals()->Vpool_minthreads
    int Vpool_minthreads;                       /* OBSOLETE */

    char *Vsecure_keyfn;
    char *Vsecure_certfn;

#define security_active conf_getglobals()->Vsecurity_active
    int Vsecurity_active;
#define ssl3_active conf_getglobals()->Vssl3_active
    int Vssl3_active;
#define ssl2_active conf_getglobals()->Vssl2_active
    int Vssl2_active;
    int Vsecure_auth;
#define security_session_timeout conf_getglobals()->Vsecurity_session_timeout
    int Vsecurity_session_timeout;
#define ssl3_session_timeout conf_getglobals()->Vssl3_session_timeout
    long Vssl3_session_timeout;

    /* The server's hostname as should be reported in self-ref URLs */
#define server_hostname conf_getglobals()->Vserver_hostname
    char *Vserver_hostname;

    /* The main object from which all are derived */
#define root_object conf_getglobals()->Vroot_object
    char *Vroot_object;

    /* The object set the administrator has asked us to load */
#define std_os conf_getglobals()->Vstd_os
    httpd_objset *Vstd_os;

    /* The root of ACL data structures */
    void *Vacl_root;
#define acl_root conf_getglobals()->Vacl_root

    /* The main error log, where all errors are logged */
#define master_error_log conf_getglobals()->Vmaster_error_log
    char *Vmaster_error_log;

    /* The server root ( in which the server sits while executing ) */
#define server_root conf_getglobals()->Vserver_root
    char *Vserver_root;

    /* This server's id */
#define server_id conf_getglobals()->Vserver_id
    char *Vserver_id;

    int single_accept;		/* daemon mode  Internal use only */
    int num_keep_alives;	/* number of KA threads Internal use only */
    int log_verbose;		/* Flag to log LOG_VERBOSE messages */
    int mmap_flags;		/* mmap flags for file cache mmaping - internal use only */
    int mmap_prots;		/* mmap prots for file cache mmaping - internal use only */
    int unused1;
    int unused2;

    /* Begin Enterprise 3.0 fields */
    int accept_language;       /* turn accept-language on/off */

    char *mtahost;              /* Hostname of the Mail Transport Agent :
				 * typically "localhost" for the Unix boxes,
				 * or the hostname of a POP server otherwise.
				 * Needed by the Agents subsystem.
				 */
    char *nntphost;              /* NNTP server -> for Agents */

    /* The root of ACL data structures */
    void *Vacl_root_30;
#define acl_root_30 conf_getglobals()->Vacl_root_30

    /* This is for the agent subsystem */
    /* The name of tha configuration parameter is: "AgentsFilePath" */
    char*	agentFilePath; /* path to agent.conf */
#define AgentFilePath (conf_getglobals()->agentFilePath)

    /* Default allowed methods - */
    int Allowed;
#define AllowedMethods (conf_getglobals()->Allowed)

    pblock *genericGlobals;    /* See conf_api.h for details on accessing 
                                * these 
                                */

    /*Agents ACL file */
    char* agentsACLFile;
    int wait_for_cgi;
    int cgiwatch_timeout;

#ifdef FORTEZZA
    uint32 fortezza_card_mask;
    char *fortezza_personality;
    char *krlname;
#endif
} conf_global_vars_s;

typedef struct {
    /* Server working variables */
    pblock *vars;

    /* The method, URI, and protocol revision of this request */
    pblock *reqpb;
    /* Protocol specific headers */
    int loadhdrs;
    pblock *headers;

    /* Server's response headers */
    int senthdrs;
    pblock *srvhdrs;

    /* The object set constructed to fulfill this request */
    httpd_objset *os;
    /* Array of objects that were created from .nsconfig files */
    httpd_objset *tmpos;

    /* The stat last returned by request_stat_path */
    char *statpath;
    char *staterr;
    struct stat *finfo;

    /* access control state */
    int aclstate;		/* ACL decision state */
    int acldirno;		/* deciding ACL directive number */
    char * aclname;		/* name of deciding ACL */
    pblock * aclpb;		/* parameter block for ACL PathCheck */
    /* 3.0 ACL list pointer */
    ACLListHandle *acllist;

#ifdef MCC_PROXY
    struct hostent *hp;	/* proxy NSAPI: DNS resolution result */
    char * host;	/* proxy NSAPI: host to resolve/connect to */
    int    port;	/* proxy NSAPI: port to connect to */

    void * socks_rq;	/* SOCKS request data */
#endif

    int request_is_cacheable;  /* default TRUE */
    int directive_is_cacheable; /* default FALSE */

    char *cached_headers;
    int cached_headers_len;	/* length of the valid headers */
    char *unused;

	/* HTTP/1.1 features */
	time_t	req_start;	/* Time request arrived - used for determining weak or*/
						/* strong cache validation */
#define REQ_TIME(x)		(x)->req_start
	short protv_num;		/* Protocol Version number */
	short method_num;		/* Method number */
	struct rq_attr {
#ifdef AIX
		unsigned abs_uri:1;	/* 1=Absolute URI was used */
		unsigned chunked:1;	/* chunked transfer-coding */
		unsigned keep_alive:1;	/* connection keek-alive */
		unsigned pipelined:1;	/* request packet is pipelined */
		unsigned reserved:28;	/* If you add a bit flag, make */
						/* sure to subtract one from this */
#else
		unsigned long abs_uri:1;	/* 1=Absolute URI was used */
		unsigned long chunked:1;	/* chunked transfer-coding */
		unsigned long keep_alive:1;	/* connection keek-alive */
		unsigned pipelined:1;	    /* request packet is pipelined */
		unsigned long reserved:28;	/* If you add a bit flag, make */
						/* sure to subtract one from this */
#endif
	}rq_attr;
	char * hostname;			/* Not NULL if abs_uri */
	int allowed;				/* Allowed METHODs for this server */
	int byterange;				/* number of byte ranges */
	short status_num;			/* Status code */

    int staterrno; /* used for rqstat */
} Request;

/* Request attribute macros */
#define ABS_URI(x) 		(x)->rq_attr.abs_uri
#define CHUNKED(x) 		(x)->rq_attr.chunked
#define KEEP_ALIVE(x) 	(x)->rq_attr.keep_alive 
#define PIPELINED(x) 	(x)->rq_attr.pipelined 

/* Define METHODS for HTTP/1.1 */
#define METHOD_HEAD		0
#define METHOD_GET 		1
#define METHOD_PUT		2
#define METHOD_POST 	3
#define	METHOD_DELETE	4
#define METHOD_TRACE	5
#define METHOD_OPTIONS	6
/* The following methods are Netscape method extensions */
#define	METHOD_MOVE		7
#define METHOD_INDEX	8
#define METHOD_MKDIR	9
#define METHOD_RMDIR	10
#define METHOD_COPY		11
#define	METHOD_MAX		12	/* Number of methods available on this server */

#define ISMGET(r)		((r)->method_num == METHOD_GET)
#define ISMHEAD(r)		((r)->method_num == METHOD_HEAD)
#define ISMPUT(r)		((r)->method_num == METHOD_PUT)
#define ISMPOST(r)		((r)->method_num == METHOD_POST)
#define ISMDELETE(r)	((r)->method_num == METHOD_DELETE)
#define ISMMOVE(r)		((r)->method_num == METHOD_MOVE)
#define ISMINDEX(r)		((r)->method_num == METHOD_INDEX)
#define ISMMKDIR(r)		((r)->method_num == METHOD_MKDIR)
#define ISMRMDIR(r)		((r)->method_num == METHOD_RMDIR)
#define ISMCOPY(r)		((r)->method_num == METHOD_COPY)
#define ISMTRACE(r)     ((r)->method_num == METHOD_TRACE)
#define ISMOPTIONS(r)   ((r)->method_num == METHOD_OPTIONS)

/*
 * FuncPtr is a pointer to our kind of functions
 */

#ifdef XP_UNIX
typedef int Func(pblock *, Session *, Request *);
#else /* XP_WIN32 */
typedef int _cdecl Func(pblock *, Session *, Request *);
#endif /* XP_WIN32 */

typedef Func *FuncPtr;

/*
 * FuncStruct is a structure used in the static declaration of the 
 * functions. This static declaration is parsed into a hash table at 
 * startup. You should initialize the next entry to NULL.
 */

struct FuncStruct {
    char *name;
    FuncPtr func;
    struct FuncStruct *next;
    int flags;
};

/* --- End type definitions --- */

/* --- Begin dispatch vector table definition --- */

typedef struct nsapi_dispatch_s nsapi_dispatch_t;
struct nsapi_dispatch_s {
    char *(*f_system_version)();
    void *(*f_system_malloc)(int size);
    void *(*f_system_calloc)(int size);
    void *(*f_system_realloc)(void *ptr, int size);
    void (*f_system_free)(void *ptr);
    char *(*f_system_strdup)(const char *ptr);
    void *(*f_system_malloc_perm)(int size);
    void *(*f_system_calloc_perm)(int size);
    void *(*f_system_realloc_perm)(void *ptr, int size);
    void (*f_system_free_perm)(void *ptr);
    char *(*f_system_strdup_perm)(const char *ptr);
    int (*f_getThreadMallocKey)(void);
    void (*f_magnus_atrestart)(void (*fn)(void *), void *data);
    filebuf_t *(*f_filebuf_open)(SYS_FILE fd, int sz);
    netbuf *(*f_netbuf_open)(SYS_NETFD sd, int sz);
    filebuf_t *(*f_filebuf_create)(SYS_FILE fd, caddr_t mmap_ptr,
                                   int mmap_len, int bufsz);
    void (*f_filebuf_close_buffer)(filebuf_t *buf, int clean_mmap);
#ifdef FILE_MMAP
    filebuf_t *(*f_filebuf_open_nostat)(SYS_FILE fd, int sz,
                                        struct stat *finfo);
#else
    void *(*f_filebuf_open_nostat)(void);
#endif
#ifdef XP_WIN32
    filebuf_t *(*f_pipebuf_open)(SYS_FILE fd, int sz, struct stat *finfo);
#else
    void *(*f_pipebuf_open)(void);
#endif /* XP_WIN32 */
#ifndef FILE_MMAP
    int (*f_filebuf_next)(filebuf_t *buf, int advance);
#else
    int (*f_filebuf_next)(void);
#endif /* !FILE_MMAP */
    int (*f_netbuf_next)(netbuf *buf, int advance);
#ifdef XP_WIN32 
    int (*f_pipebuf_next)(filebuf_t *buf, int advance);
#else
    int (*f_pipebuf_next)(void);
#endif /* XP_WIN32 */
    void (*f_filebuf_close)(filebuf_t *buf);
    void (*f_netbuf_close)(netbuf *buf);
#ifdef XP_WIN32
    void	(*f_pipebuf_close)(filebuf_t *buf);
#else
    void (*f_pipebuf_close)(void);
#endif /* XP_WIN32 */
    int (*f_filebuf_grab)(filebuf_t *buf, int sz);
    int (*f_netbuf_grab)(netbuf *buf, int sz);
#ifdef XP_WIN32
    int (*f_pipebuf_grab)(filebuf_t *buf, int sz);
#else
    int (*f_pipebuf_grab)(void);
#endif /* XP_WIN32 */
    int (*f_netbuf_buf2sd)(netbuf *buf, SYS_NETFD sd, int len);
    int (*f_filebuf_buf2sd)(filebuf_t *buf, SYS_NETFD sd);
#ifdef XP_WIN32
    int (*f_pipebuf_buf2sd)(filebuf_t *buf, SYS_NETFD sd, int len);
    int (*f_pipebuf_netbuf2sd)(netbuf *buf, SYS_FILE sd, int len);
    int (*f_pipebuf_netbuf2pipe)(netbuf *buf, SYS_NETFD sd, int len);
#else
    int (*f_pipebuf_buf2sd)(void);
    int (*f_pipebuf_netbuf2sd)(void);
    int (*f_pipebuf_netbuf2pipe)(void);
#endif /* XP_WIN32 */
    void (*f_cinfo_init)(void);
    void (*f_cinfo_terminate)(void);
    char *(*f_cinfo_merge)(char *fn);
    cinfo *(*f_cinfo_find)(char *uri);
    cinfo *(*f_cinfo_lookup)(char *type);
    void (*f_cinfo_dump_database)(FILE *dump);
    CRITICAL (*f_crit_init)(void);
    void (*f_crit_enter)(CRITICAL id);
    void (*f_crit_exit)(CRITICAL id);
    void (*f_crit_terminate)(CRITICAL id);
    CONDVAR (*f_condvar_init)(CRITICAL id);
    void (*f_condvar_wait)(CONDVAR cv);
    void (*f_condvar_notify)(CONDVAR cv);
    void (*f_condvar_notifyAll)(CONDVAR cv);
    void (*f_condvar_terminate)(CONDVAR cv);
    COUNTING_SEMAPHORE (*f_cs_init)(int initial_count);
    void (*f_cs_terminate)(COUNTING_SEMAPHORE csp);
    int (*f_cs_wait)(COUNTING_SEMAPHORE csp);
    int (*f_cs_trywait)(COUNTING_SEMAPHORE csp);
    int (*f_cs_release)(COUNTING_SEMAPHORE csp);
    void (*f_daemon_atrestart)(void (*fn)(void *), void *data);
#ifdef FORTEZZA
    /* THIS IS BOGUS... WHO Decided to export servssl_init!!! It should not
     * be a public function. 
     */
    void (*f_servssl_init)(void);
#else
    void (*f_servssl_init)(PsetHndl pset, PRFileDesc *fd, char *admRoot);
#endif
    int (*f_ereport)(int degree, char *fmt, ...);
    int (*f_ereport_v)(int degree, char *fmt, va_list args);
    char *(*f_ereport_init)(char *err_fn, char *email,
                            PASSWD pwuser, char *version);
    void (*f_ereport_terminate)(void);
    SYS_FILE (*f_ereport_getfd)(void);
    SYS_FILE (*f_system_fopenRO)(char *path);
    SYS_FILE (*f_system_fopenWA)(char *path);
    SYS_FILE (*f_system_fopenRW)(char *path);
    SYS_FILE (*f_system_fopenWT)(char *path);
    int (*f_system_fread)(SYS_FILE fd, char *buf, int sz);
    int (*f_system_fwrite)(SYS_FILE fd,char *buf,int sz);
    int (*f_system_fwrite_atomic)(SYS_FILE fd, char *buf, int sz);
    int (*f_system_lseek)(SYS_FILE fd, int off, int wh);
    int (*f_system_fclose)(SYS_FILE fd);
    int (*f_system_stat)(char *name, struct stat *finfo);
    int (*f_system_rename)(char *oldpath, char *newpath);
    int (*f_system_unlink)(char *path);
    int (*f_system_tlock)(SYS_FILE fd);
    int (*f_system_flock)(SYS_FILE fd);
    int (*f_system_ulock)(SYS_FILE fd);
#ifdef XP_WIN32
    SYS_DIR (*f_dir_open)(char *path);
    SYS_DIRENT *(*f_dir_read)(SYS_DIR ds);
    void (*f_dir_close)(SYS_DIR ds);
#else
    void *(*f_dir_open)(void);
    void *(*f_dir_read)(void);
    void (*f_dir_close)(void);
#endif /* XP_WIN32 */
    int (*f_dir_create_all)(char *dir);
#ifdef XP_WIN32
    char *(*f_system_winsockerr)(void);
    char *(*f_system_winerr)(void);
    int (*f_system_pread)(SYS_FILE fd, char *buf, int sz);
    int (*f_system_pwrite)(SYS_FILE fd, char *buf, int sz);
    void (*f_file_unix2local)(char *path, char *p2);
#else
    void *(*f_system_winsockerr)(void);
    void *(*f_system_winerr)(void);
    int (*f_system_pread)(void);
    int (*f_system_pwrite)(void);
    void (*f_file_unix2local)(void);
#endif /* XP_WIN32 */
    int (*f_system_nocoredumps)(void);
    int (*f_file_setinherit)(SYS_FILE fd, int value);
    int (*f_file_notfound)(void);
    char *(*f_system_errmsg)(void);
    int (*f_system_errmsg_fn)(char **buff, size_t maxlen);
    SYS_NETFD (*f_net_socket)(int domain, int type, int protocol);
    int (*f_net_listen)(SYS_NETFD s, int backlog);
    SYS_NETFD (*f_net_create_listener)(char *ipaddr, int port);
    int (*f_net_connect)(SYS_NETFD s, const void *sockaddr, int namelen);
    int (*f_net_getpeername)(SYS_NETFD s, struct sockaddr *name, int *namelen);
    int (*f_net_close)(SYS_NETFD s);
    int (*f_net_bind)(SYS_NETFD s, const struct sockaddr *name, int namelen);
    SYS_NETFD (*f_net_accept)(SYS_NETFD s, struct sockaddr *addr, int *addrlen);
    int (*f_net_read)(SYS_NETFD sd, char *buf, int sz, int timeout);
    int (*f_net_write)(SYS_NETFD sd, char *buf, int sz);
    int (*f_net_writev)(SYS_NETFD sd, struct iovec *iov, int iovlen);
    int (*f_net_isalive)(SYS_NETFD sd);
    char *(*f_net_ip2host)(char *ip, int verify);
    int (*f_net_getsockopt)(SYS_NETFD s, int level, int optname,
                            void *optval, int *optlen);
    int (*f_net_setsockopt)(SYS_NETFD s, int level, int optname,
                            const void *optval, int optlen);
    int (*f_net_select)(int nfds, fd_set *r, fd_set *w, fd_set *e, 
                        struct timeval *timeout);
    int (*f_net_ioctl)(SYS_NETFD s, int tag, void *result);
    pb_param *(*f_param_create)(char *name, char *value);
    int (*f_param_free)(pb_param *pp);
    pblock *(*f_pblock_create)(int n);
    void (*f_pblock_free)(pblock *pb);
    char *(*f_pblock_findval)(const char *name, pblock *pb);
    pb_param *(*f_pblock_nvinsert)(const char *name, const char *value, pblock *pb);
    pb_param *(*f_pblock_nninsert)(const char *name, int value, pblock *pb);
    void (*f_pblock_pinsert)(pb_param *pp, pblock *pb);
    int (*f_pblock_str2pblock)(const char *str, pblock *pb);
    char *(*f_pblock_pblock2str)(pblock *pb, char *str);
    void (*f_pblock_copy)(pblock *src, pblock *dst);
    pblock *(*f_pblock_dup)(pblock *src);
    char **(*f_pblock_pb2env)(pblock *pb, char **env);
    pb_param *(*f_pblock_fr)(const char *name, pblock *pb, int remove);
    char * (*f_pblock_replace)(const char *name,char * new_value,pblock *pb);
    pool_handle_t *(*f_pool_create)(void);
    void (*f_pool_destroy)(pool_handle_t *pool_handle);
    int (*f_pool_enabled)(void);
    void *(*f_pool_malloc)(pool_handle_t *pool_handle, size_t size );
    void (*f_pool_free)(pool_handle_t *pool_handle, void *ptr );
    void *(*f_pool_calloc)(pool_handle_t *pool_handle, size_t nelem, size_t elsize);
    void *(*f_pool_realloc)(pool_handle_t *pool_handle, void *ptr, size_t size );
    char *(*f_pool_strdup)(pool_handle_t *pool_handle, const char *orig_str );
#if defined(MCC_PROXY) && defined(USE_REGEX)
    int (*f_regexp_valid)(char *exp);
    int (*f_regexp_match)(char *str, char *exp);
    int (*f_regexp_cmp)(char *str, char *exp);
    int (*f_regexp_casecmp)(char *str, char *exp);
#else
    int (*f_regexp_valid)(void);
    int (*f_regexp_match)(void);
    int (*f_regexp_cmp)(void);
    int (*f_regexp_casecmp)(void);
#endif
    SEMAPHORE (*f_sem_init)(char *name, int number);
    void (*f_sem_terminate)(SEMAPHORE id);
    int (*f_sem_grab)(SEMAPHORE id);
    int (*f_sem_tgrab)(SEMAPHORE id);
    int (*f_sem_release)(SEMAPHORE id);
    Session *(*f_session_alloc)(SYS_NETFD csd, struct sockaddr_in *sac); /* internal */
    Session *(*f_session_fill)(Session *sn); /* internal */
    Session *(*f_session_create)(SYS_NETFD csd, struct sockaddr_in *sac);
    void (*f_session_free)(Session *sn);
    char *(*f_session_dns_lookup)(Session *sn, int verify);
    int (*f_shexp_valid)(char *exp);
    int (*f_shexp_match)(char *str, char *exp);
    int (*f_shexp_cmp)(char *str, char *exp);
    int (*f_shexp_casecmp)(char *str, char *exp);
#if defined (SHMEM_UNIX_MMAP) || defined (SHMEM_WIN32_MMAP)
    shmem_s *(*f_shmem_alloc)(char *name, int size, int expose);
    void (*f_shmem_free)(shmem_s *region);
#else
    void *(*f_shmem_alloc)(void);
    void (*f_shmem_free)(void);
#endif  /* SHMEM_UNIX_MMAP || SHMEM_WIN32_MMAP */
    SYS_THREAD (*f_systhread_start)(int prio, int stksz, void (*fn)(void *), void *arg);
    SYS_THREAD (*f_systhread_current)(void);
    void (*f_systhread_yield)(void);
    SYS_THREAD (*f_systhread_attach)(void);
    void (*f_systhread_detach)(SYS_THREAD thr);
    void (*f_systhread_terminate)(SYS_THREAD thr);
    void (*f_systhread_sleep)(int milliseconds);
    void (*f_systhread_init)(char *name);
    void (*f_systhread_timerset)(int usec);
    int (*f_systhread_newkey)(void);
    void *(*f_systhread_getdata)(int key);
    void (*f_systhread_setdata)(int key, void *data);
    void (*f_systhread_set_default_stacksize)(unsigned long size);
    int (*f_util_getline)(filebuffer *buf, int lineno, int maxlen, char *l);
    char **(*f_util_env_create)(char **env, int n, int *pos);
    char *(*f_util_env_str)(char *name, char *value);
    void (*f_util_env_replace)(char **env, char *name, char *value);
    void (*f_util_env_free)(char **env);
    char **(*f_util_env_copy)(char **src, char **dst);
    char *(*f_util_env_find)(char **env, char *name);
    char *(*f_util_hostname)(void);
    int (*f_util_chdir2path)(char *path);
    int (*f_util_is_mozilla)(char *ua, char *major, char *minor);
    int (*f_util_is_url)(char *url);
    int (*f_util_later_than)(struct tm *lms, char *ims);
    int (*f_util_time_equal)(struct tm *lms, char *ims);
    int (*f_util_str_time_equal)(char *t1, char *t2);
    int (*f_util_uri_is_evil)(char *t);
    void (*f_util_uri_parse)(char *uri);
    void (*f_util_uri_unescape)(char *s);
    char *(*f_util_uri_escape)(char *d, char *s);
    char *(*f_util_url_escape)(char *d, char *s);
    char *(*f_util_sh_escape)(char *s);
    int (*f_util_mime_separator)(char *sep);
    int (*f_util_itoa)(int i, char *a);
    int (*f_util_vsprintf)(char *s, register const char *fmt, va_list args);
    int (*f_util_sprintf)(char *s, const char *fmt, ...);
    int (*f_util_vsnprintf)(char *s, int n, register const char *fmt, 
                            va_list args);
    int (*f_util_snprintf)(char *s, int n, const char *fmt, ...);
    int (*f_util_strftime)(char *s, const char *format, const struct tm *t);
    char *(*f_util_strtok)(char *s1, const char *s2, char **lasts);
    struct tm *(*f_util_localtime)(const time_t *clock, struct tm *res);
    char *(*f_util_ctime)(const time_t *clock, char *buf, int buflen);
    char *(*f_util_strerror)(int errnum, char *msg, int buflen);
    struct tm *(*f_util_gmtime)(const time_t *clock, struct tm *res);
    char *(*f_util_asctime)(const struct tm *tm,char *buf, int buflen);
#ifdef NEED_STRCASECMP
    int (*f_util_strcasecmp)(CASECMPARG_T char *one, CASECMPARG_T char *two);
#else
    int (*f_util_strcasecmp)(void);
#endif /* NEED_STRCASECMP */
#ifdef NEED_STRNCASECMP
    int (*f_util_strncasecmp)(CASECMPARG_T char *one, CASECMPARG_T char *two, int n);
#else
    int (*f_util_strncasecmp)(void);
#endif /* NEED_STRNCASECMP */
#ifdef XP_UNIX
    int (*f_util_can_exec)(struct stat *finfo, uid_t uid, gid_t gid);
    struct passwd *(*f_util_getpwnam)(const char *name, struct passwd
                                      *result, char *buffer,  int buflen);
    pid_t (*f_util_waitpid)(pid_t pid, int *statptr, int options);
#else
    int (*f_util_can_exec)(void);
    void *(*f_util_getpwnam)(void);
    int (*f_util_waitpid)(void);
#endif /* XP_UNIX */
#ifdef XP_WIN32
    VOID (*f_util_delete_directory)(char *FileName, BOOL delete_directory);
#else
    void (*f_util_delete_directory)(void);
#endif /* XP_WIN32 */
    char *(*f_conf_init)(char *cfn);
    char *(*f_conf_run_init_functions)(void);
    void (*f_conf_terminate)(void);
    conf_global_vars_s *(*f_conf_getglobals)(void);
    void (*f_func_init)(struct FuncStruct *func_standard);
    FuncPtr (*f_func_find)(char *name);
    int (*f_func_exec)(pblock *pb, Session *sn, Request *rq);
    struct FuncStruct *(*f_func_insert)(char *name, FuncPtr fn);
    int (*f_object_execute)(directive *inst, Session *sn, void *rq);
    Request *(*f_http_find_request)(netbuf *buf, Session *sn, int *reply);
    int (*f_http_parse_request)(char *t, Request *rq, Session *sn);
    int (*f_http_scan_headers)(Session *sn, netbuf *buf, char *t,
                               pblock *headers);
    int (*f_http_start_response)(Session *sn, Request *rq);
    char **(*f_http_hdrs2env)(pblock *pb);
    void (*f_http_status)(Session *sn, Request *rq, int n, char *r);
    int (*f_http_set_finfo)(Session *sn, Request *rq, struct stat *finfo);
    char *(*f_http_dump822)(pblock *pb, char *t, int *pos, int tsz);
    void (*f_http_finish_request)(Session *sn, Request *rq);
    void (*f_http_handle_session)(Session *sn);
    char *(*f_http_uri2url)(const char *prefix, const char *suffix);
    char *(*f_http_uri2url_dynamic)(const char *prefix, const char *suffix,
                                    Session *sn, Request *rq);
    void (*f_http_set_keepalive_timeout)(int secs);
    int (*f_log_error_v)(int degree, char *func, Session *sn, Request *rq,
                         char *fmt, va_list args);
    int (*f_log_error)(int degree, char *func, Session *sn, Request *rq,
                       char *fmt, ...);
    int (*f_log_ereport_v)(int degree, char *fmt, va_list args);
    int (*f_log_ereport)(int degree, char *fmt, ...);
    httpd_object *(*f_object_create)(int nd, pblock *name);
    void (*f_object_free)(httpd_object *obj);
    void (*f_object_add_directive)(int dc, pblock *p, pblock *c,
                                   httpd_object *obj);
    httpd_objset *(*f_objset_scan_buffer)(filebuffer *buf, char *errstr,
                                          httpd_objset *os);
    httpd_objset *(*f_objset_create)(void);
    void (*f_objset_free)(httpd_objset *os);
    void (*f_objset_free_setonly)(httpd_objset *os);
    httpd_object *(*f_objset_new_object)(pblock *name, httpd_objset *os);
    void (*f_objset_add_object)(httpd_object *obj, httpd_objset *os);
    void (*f_objset_add_init)(pblock *initfn, httpd_objset *os);
    httpd_object *(*f_objset_findbyname)(char *name, httpd_objset *ign,
                                         httpd_objset *os);
    httpd_object *(*f_objset_findbyppath)(char *ppath, httpd_objset *ign,
                                          httpd_objset *os);
    Request *(*f_request_create)(void);
    void (*f_request_free)(Request *req);
    Request *(*f_request_restart_internal)(char *uri, Request *rq);
    int (*f_request_header)(char *name, char **value, Session *sn,
                            Request *rq);
    struct stat *(*f_request_stat_path)(char *path, Request *rq);
    char *(*f_conf_getServerString)(void);
    FuncPtr (*f_func_replace)(char *funcname, FuncPtr fn);
    int (*f_net_socketpair)(SYS_NETFD *pair);
#ifdef XP_UNIX
    SYS_NETFD (*f_net_dup2)(SYS_NETFD prfd, int osfd);
    int (*f_net_is_STDOUT)(SYS_NETFD prfd);
    int (*f_net_is_STDIN)(SYS_NETFD prfd);
#else
    void *(*f_net_dup2)(void);
    int (*f_net_is_STDOUT)(void);
    int (*f_net_is_STDIN)(void);
#endif /* XP_UNIX */
    int (*f_func_set_native_thread_flag)(char *name, int flags);
    void *(*f_random_create)(void);
    void (*f_random_update)(void *rctx, unsigned char *inbuf, int length);
    void (*f_random_generate)(void *rctx, unsigned char *outbuf, int length);
    void (*f_random_destroy)(void *rctx);
    void *(*f_md5hash_create)(void);
    void *(*f_md5hash_copy)(void *hctx);
    void (*f_md5hash_begin)(void *hctx);
    void (*f_md5hash_update)(void *hctx, unsigned char *inbuf, int length);
    void (*f_md5hash_end)(void *hctx, unsigned char *outbuf);
    void (*f_md5hash_destroy)(void *hctx);
    void (*f_md5hash_data)(unsigned char *outbuf, unsigned char *src, int length);
    int (*f_ACL_SetupEval)(struct ACLListHandle *acllist, Session *sn, Request *rq, char **rights, char **map_generic, const char *user);
    int (*f_netbuf_getbytes)(netbuf *buf, char *buffer, int size);
    char *(*f_servact_translate_uri)(char *uri, Session *sn);
    
};

/* --- End dispatch vector table definition --- */

/* --- Begin API macro definitions --- */

#ifndef INTNSAPI

#define system_version (*__nsapi30_table->f_system_version)

/*
   Depending on the system, memory allocated via these macros may come from 
   an arena. If these functions are called from within an Init function, they 
   will be allocated from permanent storage. Otherwise, they will be freed 
   when the current request is finished.
 */

#define MALLOC (*__nsapi30_table->f_system_malloc)
#define system_malloc (*__nsapi30_table->f_system_malloc)

#define CALLOC (*__nsapi30_table->f_system_calloc)
#define system_calloc (*__nsapi30_table->f_system_calloc)

#define REALLOC (*__nsapi30_table->f_system_realloc)
#define system_realloc (*__nsapi30_table->f_system_realloc)

#define FREE (*__nsapi30_table->f_system_free)
#define system_free (*__nsapi30_table->f_system_free)

#define STRDUP (*__nsapi30_table->f_system_strdup)
#define system_strdup (*__nsapi30_table->f_system_strdup)

#ifndef NS_MALLOC_DEBUG

/*
   These macros always provide permanent storage, for use in global variables
   and such. They are checked at runtime to prevent them from returning NULL.
 */

#define PERM_MALLOC (*__nsapi30_table->f_system_malloc_perm)
#define system_malloc_perm (*__nsapi30_table->f_system_malloc_perm)

#define PERM_CALLOC (*__nsapi30_table->f_system_calloc_perm)
#define system_calloc_perm (*__nsapi30_table->f_system_calloc_perm)

#define PERM_REALLOC (*__nsapi30_table->f_system_realloc_perm)
#define system_realloc_perm (*__nsapi30_table->f_system_realloc_perm)

#define PERM_FREE (*__nsapi30_table->f_system_free_perm)
#define system_free_perm (*__nsapi30_table->f_system_free_perm)

#define PERM_STRDUP (*__nsapi30_table->f_system_strdup_perm)
#define system_strdup_perm (*__nsapi30_table->f_system_strdup_perm)

#endif /* !NS_MALLOC_DEBUG */

/* Thread-Private data key index for accessing the thread-private memory pool.
 * Each thread creates its own pool for allocating data.  The MALLOC/FREE/etc
 * macros have been defined to check the thread private data area with the
 * thread_malloc_key index to find the address for the pool currently in use.
 *
 * If a thread wants to use a different pool, it must change the thread-local-
 * storage[thread_malloc_key].
 */

#define getThreadMallocKey (*__nsapi30_table->f_getThreadMallocKey)

#define magnus_atrestart (*__nsapi30_table->f_magnus_atrestart)

/*
 * buffer_open opens a new buffer reading the specified file, with an I/O
 * buffer of size sz, and returns a new buffer structure which will hold
 * the data.
 *
 * If FILE_UNIX_MMAP is defined, this may return NULL. If it does, check 
 * system_errmsg to get a message about the error.
 */

#define filebuf_open (*__nsapi30_table->f_filebuf_open)
#define netbuf_open (*__nsapi30_table->f_netbuf_open)

/*
 * filebuf_open_nostat is a convenience function for mmap() buffer opens,
 * if you happen to have the stat structure already.
 */

#ifdef FILE_MMAP
#define filebuf_open_nostat (*__nsapi30_table->f_filebuf_open_nostat)
#endif /* FILE_MMAP */

/*
 * filebuf_create is a convenience function if the file is already open
 * or mmap'd.  It creates a new filebuf for use with the mmap'd file.
 * If mmap_ptr is NULL, or MMAP is not supported on this system, it 
 * creates a buffer with buffer size bufsz.
 */

#define filebuf_create (*__nsapi30_table->f_filebuf_create)

/* 
 * filebuf_close_buffer is provided to cleanup a filebuf without closing
 * the underlying file.  If clean_mmap is 1, and the file is memory mapped,
 * the file will be unmapped.  If clean_mmap is 0, the file will not
 * be unmapped.
 */
#define filebuf_close_buffer (*__nsapi30_table->f_filebuf_close_buffer)

#ifdef FILE_MMAP
#define filebuf_open_nostat (*__nsapi30_table->f_filebuf_open_nostat)
#endif

#ifdef XP_WIN32
#define pipebuf_open (*__nsapi30_table->f_pipebuf_open)
#endif /* XP_WIN32 */

/*
 * buffer_next loads size more bytes into the given buffer and returns the
 * first one, or BUFFER_EOF on EOF and BUFFER_ERROR on error.
 */

#ifndef FILE_MMAP
#define filebuf_next (*__nsapi30_table->f_filebuf_next)
#endif /* !FILE_MMAP */
#define netbuf_next (*__nsapi30_table->f_netbuf_next)
#ifdef XP_WIN32 
#define pipebuf_next (*__nsapi30_table->f_pipebuf_next)
#endif /* XP_WIN32 */

/*
 * buffer_close deallocates a buffer and closes its associated files
 * (does not close a network socket).
 */

#define filebuf_close (*__nsapi30_table->f_filebuf_close)
#define netbuf_close (*__nsapi30_table->f_netbuf_close)
#ifdef XP_WIN32
#define pipebuf_close (*__nsapi30_table->f_pipebuf_close)
#endif /* XP_WIN32 */

/*
 * buffer_grab will set the buffer's inbuf array to an array of sz bytes 
 * from the buffer's associated object. It returns the number of bytes 
 * actually read (between 1 and sz). It returns IO_EOF upon EOF or IO_ERROR
 * upon error. The cursize entry of the structure will reflect the size
 * of the iobuf array.
 * 
 * The buffer will take care of allocation and deallocation of this array.
 */

#define filebuf_grab (*__nsapi30_table->f_filebuf_grab)
#define netbuf_grab (*__nsapi30_table->f_netbuf_grab)
#ifdef XP_WIN32
#define pipebuf_grab (*__nsapi30_table->f_pipebuf_grab)
#endif /* XP_WIN32 */

/*
 * netbuf_getbytes will read bytes from the netbuf into the user
 * supplied buffer.  Up to size bytes will be read.
 * If the call is successful, the number of bytes read is returned.  
 * NETBUF_EOF is returned when no more data will arrive on the socket,
 * and NETBUF_ERROR is returned in the event of an error.
 *
 */
#define netbuf_getbytes (*__nsapi30_table->f_netbuf_getbytes)


/*
 * netbuf_buf2sd will send n bytes from the (probably previously read)
 * buffer and send them to sd. If sd is -1, they are discarded. If n is
 * -1, it will continue until EOF is recieved. Returns IO_ERROR on error
 * and the number of bytes sent any other time.
 */

#define netbuf_buf2sd (*__nsapi30_table->f_netbuf_buf2sd)

/*
 * filebuf_buf2sd assumes that nothing has been read from the filebuf, 
 * and just sends the file out to the given socket. Returns IO_ERROR on error
 * and the number of bytes sent otherwise.
 *
 * Does not currently support you having read from the buffer previously. This
 * can be changed transparently.
 */

#define filebuf_buf2sd (*__nsapi30_table->f_filebuf_buf2sd)

#ifdef XP_WIN32

/*
 * NT pipe version of filebuf_buf2sd.
 */
#define pipebuf_buf2sd (*__nsapi30_table->f_pipebuf_buf2sd)

/*
 * NT pipe version of netbuf_buf2sd.
 */

#define pipebuf_netbuf2sd (*__nsapi30_table->f_pipebuf_netbuf2sd)
#define pipebuf_netbuf2pipe (*__nsapi30_table->f_pipebuf_netbuf2pipe)
#endif /* XP_WIN32 */

/*
 * Notes:
 *
 *      Format of a mime.types file
 *
 *          A mime.types file passed to cinfo_merge() should begin with
 *          a line containing NCC_MT_MAGIC as defined below (MCC_MT_MAGIC
 *          is also still supported).  Lines containing only whitespace
 *          and lines beginning with "#" are ignored.  Other lines contain
 *          one or more name/value pairs, separated by whitespace.  The
 *          format of each of these is NAME=VALUE, where NAME is one of:
 *
 *              type - VALUE is a mime type, e.g. text/html
 *              exts - VALUE is a list of file extensions, e.g. htm,html
 *              enc - VALUE specifies a content encoding, e.g. x-gzip
 *              lang - VALUE specifies a content language, e.g. en-US
 *                      (see RFC1766)
 *
 *         The VALUE may be enclosed in quotes (" "), and should be if it
 *         contains whitespace.
 *
 *         Each line in the file that contains at least one name/value
 *         pair causes a cinfo structure to be created, containing the
 *         specified information.  These structures are kept in memory
 *         and can be queried using cinfo_find().
 *
 *      Related information
 *
 *          See the NSAPI "load-types" function.
 */

/*
 * Function:    cinfo_init
 *
 * Description:
 *
 *      This function initializes the memory resident content information
 *      database to be empty.
 *
 * Notes:
 *
 *      This function is called by the NSAPI "load-types" function,
 *      which also sets up a call to cinfo_terminate() on server restart.
 *      If "load-types" is used, it should be unnecessary to call this
 *      function.
 */

#define cinfo_init (*__nsapi30_table->f_cinfo_init)

/*
 * Function:    cinfo_terminate
 *
 * Description:
 *
 *      This function frees all dynamic memory associated with the
 *      memory resident content information database, and leaves the
 *      database empty.  cinfo_init() should be called before using
 *      the database again.
 */

#define cinfo_terminate (*__nsapi30_table->f_cinfo_terminate)

/*
 * Function:    cinfo_merge
 *
 * Description:
 *
 *      This function reads a mime.types formatted file of the specified
 *      name.  Content information in the file is merged into the
 *      memory resident content information database.
 *
 * Arguments:
 *
 *      fn              - name of mime.types formatted file
 *
 * Returns:
 *
 *      If successful, NULL is returned.  If an error occurs, the return
 *      value is a pointer to a descriptive error string, which should
 *      be freed by the caller (using FREE()).  Information processed
 *      prior to the error will still be added to the database.
 */

#define cinfo_merge (*__nsapi30_table->f_cinfo_merge)


/*
 * Function:    cinfo_find
 *
 * Description:
 *
 *      This function is used to retrieve content information based on
 *      a file extension contained in the argument string, which might
 *      be either a URI or a file name.  It ignores any text up to and
 *      including the last FILE_PATHSEP character in the argument
 *      string.  It looks for one or more file extensions in the
 *      remaining string, each one starting with a CINFO_SEPARATOR
 *      character.  Each file extension is looked up in the database
 *      and any information found there is used to build a composite
 *      cinfo structure, containing information derived from each of
 *      the file extensions.  If more than one file extensions present
 *      have a particular piece of information associated with them
 *      in the database (e.g. type or language), the value returned in
 *      the composite cinfo structure is taken from the data for the
 *      last file extension to provide that attribute.
 *
 * Arguments:
 *
 *      uri             - pointer to the argument string
 *
 * Returns:
 *
 *      If successful, a pointer to the composite cinfo structure is
 *      returned.  The fields of this structure reference values of
 *      those fields in one or more cinfo structures in the database,
 *      so these values should be copied if they are going to be
 *      modified.  The cinfo structure itself should be freed by the
 *      caller using FREE().  If an error occurs, a null pointer is
 *      returned.  If no information is found for any of the extensions,
 *      a null pointer is returned, although this is not necessarily
 *      an error.
 *
 * Notes:
 *
 *      The matching of extensions in the argument string to extensions
 *      in the database is case-insensitive.
 *
 *      The argument string is modified during parsing, but only
 *      temporarily.  It should be intact on return.
 */

#define cinfo_find (*__nsapi30_table->f_cinfo_find)

/*
 * Function:    cinfo_lookup
 *
 * Description:
 *
 *      This function returns a pointer to the last cinfo structure
 *      added to the database for a specified mime type.  Unlike
 *      cinfo_find(), the returned pointer references a cinfo structure
 *      that is part of the database, and corresponds to a single line
 *      in a mime.types file.  This structure should be considered
 *      read-only, and should not be freed by the caller.
 *
 * Arguments:
 *
 *      type            - a pointer to a mime type string
 *
 * Returns:
 *
 *      If the specified type is found, a pointer to the cinfo structure
 *      returned.  Otherwise a null pointer is returned.
 *
 * Notes:
 *
 *      The search for the type is case-insensitive.
 */

#define cinfo_lookup (*__nsapi30_table->f_cinfo_lookup)

/*
 * Function:    cinfo_dump_database
 *
 * Description:
 *
 *      This function outputs, to a specified file, formatted text
 *      describing the contents of the content information database.
 *      This is intended for debugging purposes.  It is not in mime.types
 *      format.
 *
 * Arguments:
 *
 *      dump            - handle for already open output file
 */

#define cinfo_dump_database (*__nsapi30_table->f_cinfo_dump_database)

/*
 *      Critical section abstraction. Used in threaded servers to protect
 *      areas where two threads can interfere with each other.
 *
 *      Condvars are condition variables that are used for thread-thread 
 *      synchronization.
 */

/*
 * crit_init creates and returns a new critical section variable. At the 
 * time of creation no one has entered it.
 */
#define crit_init (*__nsapi30_table->f_crit_init)

/*
 * crit_enter enters a critical section. If someone is already in the
 * section, the calling thread is blocked until that thread exits.
 */
#define crit_enter (*__nsapi30_table->f_crit_enter)

/*
 * crit_exit exits a critical section. If another thread is blocked waiting
 * to enter, it will be unblocked and given ownership of the section.
 */
#define crit_exit (*__nsapi30_table->f_crit_exit)

/*
 * crit_terminate removes a previously allocated critical section variable.
 */
#define crit_terminate (*__nsapi30_table->f_crit_terminate)

/*
 * condvar_init initializes and returns a new condition variable. You 
 * must provide a critical section to be associated with this condition 
 * variable.
 */
#define condvar_init (*__nsapi30_table->f_condvar_init)

/*
 * condvar_wait blocks on the given condition variable. The calling thread
 * will be blocked until another thread calls condvar_notify on this variable.
 * The caller must have entered the critical section associated with this
 * condition variable prior to waiting for it.
 */
#define condvar_wait (*__nsapi30_table->f_condvar_wait)

/*
 * condvar_notify awakens any threads blocked on the given condition
 * variable. The caller must have entered the critical section associated
 * with this variable first.
 */
#define condvar_notify (*__nsapi30_table->f_condvar_notify)

/*
 * condvar_notify awakens all threads blocked on the given condition
 * variable. The caller must have entered the critical section associated
 * with this variable first.
 */
#define condvar_notifyAll (*__nsapi30_table->f_condvar_notifyAll)

/*
 * condvar_terminate frees the given previously allocated condition variable
 */
#define condvar_terminate (*__nsapi30_table->f_condvar_terminate)

/*
 * Create a counting semaphore.  
 * Return non-zero on success, 0 on failure.
 */
#define cs_init (*__nsapi30_table->f_cs_init)

/*
 * Destroy a counting semaphore 
 */
#define cs_terminate (*__nsapi30_table->f_cs_terminate)

/*
 * Wait to "enter" the semaphore.
 * Return 0 on success, -1 on failure.
 */
#define cs_wait (*__nsapi30_table->f_cs_wait)

/*
 * Enter the semaphore if the count is > 0.  Otherwise return -1.
 *
 */
#define cs_trywait (*__nsapi30_table->f_cs_trywait)

/*
 * Release the semaphore- allowing a thread to enter.
 * Return 0 on success, -1 on failure.
 */
#define cs_release (*__nsapi30_table->f_cs_release)

/*
 * daemon_atrestart registers a function to be called fn, with the given
 * void pointer as an argument, when the server is restarted.
 */
#define daemon_atrestart (*__nsapi30_table->f_daemon_atrestart)

#define servssl_init (*__nsapi30_table->f_servssl_init)

/*
 * ereport logs an error of the given degree and formats the arguments with 
 * the printf() style fmt. Returns whether the log was successful. Records 
 * the current date.
 */

#define ereport (*__nsapi30_table->f_ereport)
#define ereport_v (*__nsapi30_table->f_ereport_v)

/*
 * ereport_init initializes the error logging subsystem and opens the static
 * file descriptors. It returns NULL upon success and an error string upon
 * error. If a userpw is given, the logs will be chowned to that user.
 * 
 * email is the address of a person to mail upon catastrophic error. It
 * can be NULL if no e-mail is desired. ereport_init will not duplicate
 * its own copy of this string; you must make sure it stays around and free
 * it when you shut down the server.
 */

#define ereport_init (*__nsapi30_table->f_ereport_init)

/*
 * log_terminate closes the error and common log file descriptors.
 */
#define ereport_terminate (*__nsapi30_table->f_ereport_terminate)

/* For restarts */
#define ereport_getfd (*__nsapi30_table->f_ereport_getfd)

/* FUNCTION: system_fopenXX
 * DESCRIPTION:
 *    system_fopenRO - Open a disk file for read-only access.
 *    system_fopenWA - Open a disk file for write and append access.
 *    system_fopenRW - Open a disk file for read-write access.
 *    system_fopenWT - Open a disk file for write and truncate access.
 * INPUTS:
 *    path- the name of the file
 * OUTPUTS:
 *    none
 * RETURNS:
 *    SYS_ERRORFD on failure
 *    A opaque file handle on success
 * RESTRICTIONS:
 *    The return from this function should not be interpreted as a native
 *    file handle.  It may only be used in other calls to system_xxx().
 *
 *    Note for unix programmers: Although this routine is called 
 *      "system_fopen" do not consider it to be equivalent to the unix 
 *      "fopen" call, which opens a buffered-IO file.
 */
#define system_fopenRO (*__nsapi30_table->f_system_fopenRO)
#define system_fopenWA (*__nsapi30_table->f_system_fopenWA)
#define system_fopenRW (*__nsapi30_table->f_system_fopenRW)
#define system_fopenWT (*__nsapi30_table->f_system_fopenWT)

/* FUNCTION: system_fread
 * DESCRIPTION:
 *    Read from a file
 * INPUTS:
 *    fd- an open file handle to read from
 *    buf- a buffer to receive data
 *    sz- the number of bytes to read from the file
 * OUTPUTS:
 *    none
 * RETURNS:
 *    less-than-zero on failure
 *    0 for end-of-file
 *    positive for number of bytes read
 * RESTRICTIONS:
 */
#define system_fread (*__nsapi30_table->f_system_fread)

/* FUNCTION: system_write
 * DESCRIPTION:
 *    Writes sz bytes from buf to file fd.  
 * INPUTS:
 *    fd- the file to write to
 *    buf- the buffer containing data to be written
 *    sz-  the size of data to write.
 * OUTPUTS:
 *    none
 * RETURNS:
 *    IO_OKAY on success
 *    IO_ERROR on failure
 * RESTRICTIONS:
 *    There is no way to determine if bytes were written when this call fails.
 *    For more specific information, use PR_WRITE().
 */
#define system_fwrite (*__nsapi30_table->f_system_fwrite)

/* FUNCTION: system_fwrite_atomic
 * DESCRIPTION:
 *    system_fwrite_atomic locks the given fd before writing to it. This
 *    avoids interference between simultaneous writes.
 * INPUTS:
 *    fd- the file to write to
 *    buf- the buffer containing data to be written
 *    sz-  the size of data to write.
 * OUTPUTS:
 *    none
 * RETURNS:
 *    IO_OKAY on success
 *    IO_ERROR on failure
 * RESTRICTIONS:
 */
#define system_fwrite_atomic (*__nsapi30_table->f_system_fwrite_atomic)

/* FUNCTION:  system_lseek
 * DESCRIPTION:
 *    Seek to a position in a file
 * INPUTS:
 *    fd-  an open file handle
 *    off
 *    wh
 * OUTPUTS:
 * RETURNS:
 * RESTRICTIONS:
 */
#define system_lseek (*__nsapi30_table->f_system_lseek)

/* FUNCTION: system_fclose
 * DESCRIPTION:
 *    Close a file.
 * INPUTS:
 *    fd- an open file handle from a previous system_fopenXX call.
 * OUTPUTS:
 *    none
 * RETURNS:
 *    IO_OKAY on success
 *    IO_ERROR on failure
 * RESTRICTIONS:
 */
#define system_fclose (*__nsapi30_table->f_system_fclose)

/* FUNCTION: system_stat
 * DESCRIPTION: 
 *    Get information about a file on disk
 * INPUTS:
 *    name-    the name of the file to be queried
 * OUTPUTS:
 *    finfo-   a buffer to receive the file information.
 * RETURN:
 *    less-than-zero on failure.
 *    0 on success.
 * RESTRICTIONS:
 *    Within the stat structure, this routine is only guaranteed to return
 *    st_mode, st_size, st_ctime, and st_mtime. Other stat fields are 
 *    undefined.
 */
#define system_stat (*__nsapi30_table->f_system_stat)

/* --- File manipulation --------------------------------------------- */

/* FUNCTION:  system_rename
 * DESCRIPTION:
 *    Rename a file on disk.
 * INPUTS:
 *    oldpath- old file name
 *    newpath- new file name
 * OUTPUTS:
 *    none
 * RETURNS:
 *    less-than-zero on failure
 *    0 on success
 * RESTRICTIONS:
 *    Not guaranteed to work portably on files which are in use.
 */
#define system_rename (*__nsapi30_table->f_system_rename)

/* FUNCTION: system_unlink
 * DESCRIPTION:
 *    Remove a file from disk.
 * INPUTS:
 *    path- the file to delete
 * OUTPUTS:
 *    none
 * RETURNS:
 *    less-than-zero on failure
 *    0 on success
 * RESTRICTIONS:
 *    Not guaranteed to work portably on files which are in use.
 */
#define system_unlink (*__nsapi30_table->f_system_unlink)

/* --- File locking -------------------------------------------------- */

#define system_initlock(fd) (0)

/* FUNCTION: system_tlock
 * DESCRIPTION:
 *    Test for a file lock and grab it if it is available
 * INPUTS:
 *    fd- the file to lock
 * OUTPUTS:
 *    none
 * RETURNS:
 *    0 - if we now hold the lock
 *    less-than-zero if the lock is held by someone else
 * RESTRICTIONS:
 *    1. file locking is process based.  Two threads in the same process
 *    requesting exclusive access will both be allowed to access the file
 *    at the same time.
 */
#define system_tlock (*__nsapi30_table->f_system_tlock)

/* FUNCTION:  system_flock
 * DESCRIPTION:
 *    Wait for exclusive access to a file
 * INPUTS:
 *    fd- the file to lock
 * OUTPUTS:
 *    none
 * RETURNS:
 *    0 - if we now hold the lock
 *    less-than-zero if an error occurred
 * RESTRICTIONS:
 *    1. file locking is process based.  Two threads in the same process
 *    requesting exclusive access will both be allowed to access the file
 *    at the same time.
 */
#define system_flock (*__nsapi30_table->f_system_flock)

/* FUNCTION: system_ulock
 * DESCRIPTION:
 *    Release exclusive access to a file
 * INPUTS:
 *    fd- the file to lock
 * OUTPUTS:
 *    none
 * RETURNS:
 *    0 - if we released the lock
 *    less-than-zero if an error occurred
 * RESTRICTIONS:
 *    1. file locking is process based.  Two threads in the same process
 *    requesting exclusive access will both be allowed to access the file
 *    at the same time.
 */
#define system_ulock (*__nsapi30_table->f_system_ulock)

/* --- Directory manipulation routines ---------------------------------- */

#ifdef XP_WIN32
#define dir_open (*__nsapi30_table->f_dir_open)
#define dir_read (*__nsapi30_table->f_dir_read)
#define dir_close (*__nsapi30_table->f_dir_close)
#endif /* XP_WIN32 */

/*
 * create a directory and any of its parents
 */
#define dir_create_all (*__nsapi30_table->f_dir_create_all)

#ifdef XP_WIN32
#define system_winsockerr (*__nsapi30_table->f_system_winsockerr)
#define system_winerr (*__nsapi30_table->f_system_winerr)
#define system_pread (*__nsapi30_table->f_system_pread)
#define system_pwrite (*__nsapi30_table->f_system_pwrite)
#define file_unix2local (*__nsapi30_table->f_file_unix2local)
#endif /* XP_WIN32 */

#define system_nocoredumps (*__nsapi30_table->f_system_nocoredumps)
#define file_setinherit (*__nsapi30_table->f_file_setinherit)
#define file_notfound (*__nsapi30_table->f_file_notfound)
#define system_errmsg (*__nsapi30_table->f_system_errmsg)
#define system_errmsg_fn (*__nsapi30_table->f_system_errmsg_fn)

/* FUNCTION: net_socket
 * DESCRIPTION:
 *    Create a new socket.
 * INPUTS:
 *    domain-   only supported type is AF_INET
 *    type-     only supported type is SOCK_STREAM
 *    protocol- only supported value is 0
 * OUTPUTS:
 *    none
 * RETURNS:
 *    SYS_NET_ERRORFD on errro
 *    a valid SYS_NETFD on success
 * RESTRICTIONS:
 */
#define net_socket (*__nsapi30_table->f_net_socket)

/* FUNCTION: net_listen
 * DESCRIPTION:
 *   Set listen backlog for a socket.
 * INPUTS:
 *   s- the socket
 *   backlog- value of backlog to set
 * OUTPUTS:
 * RETURNS:
 *   0 on success
 *   less-than-zero on failure
 * RESTRICTIONS:
 *   Some systems do not return errors even when the requested backlog
 *   cannot be set (it is too high).  Consult your system manual for 
 *   details on the maximum value of the backlog.
 */
#define net_listen (*__nsapi30_table->f_net_listen)

/* FUNCTION: net_create_listener
 * DESCRIPTION:
 *   Creates a socket for accepting new connection.
 * INPUTS:
 * OUTPUTS:
 * RETURNS:
 *   SYS_NET_ERRORFD on error.
 * RESTRICTIONS:
 *   This is a convenience routine which creates a socket, binds to
 *   an IP address and port, and sets the listen backlog to 
 *   net_listenqsize.
 */
#define net_create_listener (*__nsapi30_table->f_net_create_listener)

/* FUNCTION: net_connect
 * DESCRIPTION:
 *    Connect a socket to a remote listener
 * INPUTS:
 * OUTPUTS:
 * RETURNS:
 *    0 on success
 *    less-than-zero on error
 * RESTRICTIONS:
 */
#define net_connect (*__nsapi30_table->f_net_connect)

/* FUNCTION: net_getpeername
 * DESCRIPTION:
 *    Get the socket address (IP address/port) of the remote host.
 * INPUTS:
 *    s-     the socket
 *    name - the socket address of the remote
 * OUTPUTS:
 * RETURNS:
 * RESTRICTIONS:
 */
#define net_getpeername (*__nsapi30_table->f_net_getpeername)

/* FUNCTION: net_close
 * DESCRIPTION:
 *    Close an open socket
 * INPUTS:
 *    s- the socket to close
 * OUTPUTS:
 *    none
 * RETURNS:
 *    0 on success
 *    less-than-zero on failure
 * RESTRICTIONS:
 */
#define net_close (*__nsapi30_table->f_net_close)

/* FUNCTION: net_bind
 * DESCRIPTION:
 *    Bind to a socket address
 * INPUTS:
 *    s-
 *    name-
 *    namelen-
 * OUTPUTS:
 * RETURNS:
 * RESTRICTIONS:
 */
#define net_bind (*__nsapi30_table->f_net_bind)

/* FUNCTION: net_accept
 * DESCRIPTION:
 *    Accept a connection on a listener socket.
 * INPUTS:
 *    s
 *    addr
 * OUTPUTS:
 *    addrlen
 * RETURNS:
 * RESTRICTIONS:
 */
#define net_accept (*__nsapi30_table->f_net_accept)

/* FUNCTION: net_read
 * DESCRIPTION:
 *    sd
 *    buf
 *    sz
 *    timeout
 * INPUTS:
 * OUTPUTS:
 * RETURNS:
 *    IO_ERROR on error
 *    0 if the remote closes the socket
 *    positive representing the number of bytes successfully read
 * RESTRICTIONS:
 *    timeout must be NET_ZERO_TIMEOUT, NET_INFINITE_TIMEOUT, or a positive
 *    value in seconds
 */
#define net_read (*__nsapi30_table->f_net_read)

/* FUNCTION: net_write
 * DESCRIPTION:
 *    Write data to a socket.
 * INPUTS:
 *    sd
 *    buf
 *    sz
 * OUTPUTS:
 * RETURNS:
 * RESTRICTIONS:
 *    net_write() is unbuffered.  Specifying many calls to net_write() for 
 *    small amounts of data is inefficient.
 */
#define net_write (*__nsapi30_table->f_net_write)

/* FUNCTION: net_writev
 * DESCRIPTION:
 *    Write vectored data to the socket.
 * INPUTS:
 * OUTPUTS:
 * RETURNS:
 * RESTRICTIONS:
 *    net_writev() is unbuffered.  Specifying many calls to net_writev() for 
 *    small amounts of data is inefficient.
 */
#define net_writev (*__nsapi30_table->f_net_writev)

/* FUNCTION: net_isalive
 * DESCRIPTION:
 *    Checks to see if the given socket is still connected to a remote
 *    host.  The remote host does not see any side effects from this call.
 * INPUTS:
 *    sd - the socket
 * OUTPUTS:
 *    none
 * RETURNS:
 *    0 if the socket is no longer connected
 *    1 if the socket is still connected.
 * RESTRICTIONS:
 */
#define net_isalive (*__nsapi30_table->f_net_isalive)

/* FUNCTION: net_ip2host
 * DESCRIPTION:
 *    Transforms the given textual IP number into a fully qualified domain
 *    name (FQDN).   This is similar to calling gethostbyaddr().
 * INPUTS:
 *    verify- If 1, specifies that the function should verify the hostname
 *            returned from the lookup.  This is similar to calling 
 *            gethostbyname() on the result of the call to gethostbyaddr().
 * OUTPUTS:
 * RETURNS:
 *    The fully qualified domain name, or whatever it can find.
 *    If it cannot resolve the name at all, returns NULL.
 * RESTRICTIONS:
 *    This function is governed by the use of the Server DNS cache. If caching
 *    is enabled it can take as long as 20 minutes before this function 
 *    does the lookup again.
 */
#define net_ip2host (*__nsapi30_table->f_net_ip2host)


/* --- OBSOLETE ----------------------------------------------------------
 * The following macros/functions are obsolete and are only maintained for
 * compatibility.  Do not use them.
 * -----------------------------------------------------------------------
 */

/* FUNCTION: net_getsockopt
 * DESCRIPTION:
 *    Get socket options
 * INPUTS:
 * OUTPUTS:
 * RETURNS:
 * RESTRICTIONS:
 *    Because this function is not portable (not all systems support the
 *    same options), it should be used with caution.
 */
#define net_getsockopt (*__nsapi30_table->f_net_getsockopt)

/* FUNCTION: net_setsockopt
 * DESCRIPTION:
 *    Set socket options
 * INPUTS:
 * OUTPUTS:
 * RETURNS:
 * RESTRICTIONS:
 *    Because this function is not portable (not all systems support the
 *    same options), it should be used with caution.
 */
#define net_setsockopt (*__nsapi30_table->f_net_setsockopt)

/* FUNCTION: net_select
 * DESCRIPTION:
 *    Wait for IO on a set of sockets.
 * INPUTS:
 * OUTPUTS:
 * RETURNS:
 *    -1 on error
 *     0 on timeout
 *    positive value representing the number of IOs ready
 * RESTRICTIONS:
 *    Because this function is not portable (not all systems support the
 *    same options), it should be used with caution.
 */
#define net_select (*__nsapi30_table->f_net_select)

/* FUNCTION: net_ioctl
 * DESCRIPTION:
 *    Set socket options.
 * INPUTS:
 * OUTPUTS:
 * RETURNS:
 * RESTRICTIONS:
 *    Because this function is not portable (not all systems support the
 *    same options), it should be used with caution.
 */
#define net_ioctl (*__nsapi30_table->f_net_ioctl)

/* FUNCTION: net_socketpair
 * DESCRIPTION:
 *    Creates a TCP socketpair.
 * INPUTS:
 * OUTPUTS:
 * RETURNS:
 *    0 on success
 *   -1 on failure
 * RESTRICTIONS:
 *    Because this function is not portable (not all systems support the
 *    same options), it should be used with caution.
 */
#define net_socketpair (*__nsapi30_table->f_net_socketpair)

#ifdef XP_UNIX
/* FUNCTION: net_dup2
 * DESCRIPTION:
 *    Duplicates a socket to a specific file descriptor
 * INPUTS:
 * OUTPUTS:
 * RETURNS:
 * RESTRICTIONS:
 *    Because this function is not portable (not all systems support the
 *    same options), it should be used with caution.
 */
#define net_dup2 (*__nsapi30_table->f_net_dup2)

/* FUNCTION: net_is_STDOUT
 * DESCRIPTION:
 *    Checks if the underlying OS file descriptor for the given 
 *    SYS_NETFD is the STDOUT filedescriptor
 * INPUTS:
 * OUTPUTS:
 * RETURNS:
 *    1 if it is STDOUT (1)
 *    0 otherwise
 * RESTRICTIONS:
 *    Because this function is not portable (not all systems support the
 *    same options), it should be used with caution.
 */
#define net_is_STDOUT (*__nsapi30_table->f_net_is_STDOUT)

/* FUNCTION: net_is_STDIN
 * DESCRIPTION:
 *    Checks if the underlying OS file descriptor for the given 
 *    SYS_NETFD is the STDIN filedescriptor
 * INPUTS:
 * OUTPUTS:
 * RETURNS:
 *    1 if it is STDIN (0)
 *    0 otherwise
 * RESTRICTIONS:
 *    Because this function is not portable (not all systems support the
 *    same options), it should be used with caution.
 */
#define net_is_STDIN (*__nsapi30_table->f_net_is_STDIN)
#endif /* XP_UNIX */

/*
 * A parameter block is a set of name=value pairs which are generally used 
 * as parameters, but can be anything. They are kept in a hash table for 
 * reasonable speed, but if you are doing any intensive modification or
 * access of them you should probably make a local copy of each parameter
 * while working.
 *
 * When creating a pblock, you specify the hash table size for that pblock.
 * You should set this size larger if you know that many items will be in 
 * that pblock, and smaller if only a few will be used or if speed is not
 * a concern.
 */

/*
 * param_create creates a parameter with the given name and value. If name
 * and value are non-NULL, they are copied and placed into the new pb_param
 * struct.
 */

#define param_create (*__nsapi30_table->f_param_create)

/*
 * param_free frees a given parameter if it's non-NULL, and returns 1 if
 * p was non-NULL, and 0 if p was NULL.
 * 
 * Useful for error checking pblock_remove.
 */

#define param_free (*__nsapi30_table->f_param_free)

/* 
 * pblock_create creates a new pblock with hash table size n.
 * 
 * It returns the newly allocated pblock.
 */

#define pblock_create (*__nsapi30_table->f_pblock_create)

/*
 * pblock_free frees the given pblock and any entries inside it.
 * 
 * If you want to save anything in a pblock, remove its entities with 
 * pblock_remove first and save the pointers you get.
 */

#define pblock_free (*__nsapi30_table->f_pblock_free)

/*
 * pblock_findval finds the entry with the given name in pblock pb, and
 * returns its value, otherwise returns NULL.
 */

#define pblock_findval (*__nsapi30_table->f_pblock_findval)

/*
 * pblock_nvinsert creates a new parameter with the given name and value
 * and inserts it into pblock pb. The name and value in the parameter are
 * also newly allocated. Returns the pb_param it allocated (in case you 
 * need it).
 *
 * pblock_nninsert inserts a numerical value.
 */

#define pblock_nvinsert (*__nsapi30_table->f_pblock_nvinsert)
#define pblock_nninsert (*__nsapi30_table->f_pblock_nninsert)

/*
 * pblock_pinsert inserts a pb_param into a pblock.
 */

#define pblock_pinsert (*__nsapi30_table->f_pblock_pinsert)

/*
 * pblock_str2pblock scans the given string str for parameter pairs
 * name=value, or name="value". Any \ must be followed by a literal 
 * character. If a string value is found, with no unescaped = signs, it
 * will be added with the name 1, 2, 3, etc. depending on whether it was
 * first, second, third, etc. in the stream (zero doesn't count).
 * 
 * Returns the number of parameters added to the table, or -1 upon error.
 */

#define pblock_str2pblock (*__nsapi30_table->f_pblock_str2pblock)

/*
 * pblock_pblock2str places all of the parameters in the given pblock 
 * into the given string (NULL if it needs creation). It will re-allocate
 * more space for the string. Each parameter is separated by a space and of
 * the form name="value"
 */

#define pblock_pblock2str (*__nsapi30_table->f_pblock_pblock2str)

/*
 * pblock_copy copies the entries in the given source pblock to the 
 * destination one. The entries are newly allocated so that the original
 * pblock may be freed or the new one changed without affecting the other.
 */

#define pblock_copy (*__nsapi30_table->f_pblock_copy)

/*
 * pblock_dup creates a new pblock and copies the given source pblock
 * into it.  The entries are newly allocated so that the original pblock
 * may be freed or the new one changed without affecting the other.
 */

#define pblock_dup (*__nsapi30_table->f_pblock_dup)

/*
 * pblock_pb2env copies the given pblock into the given environment, with
 * one new env entry for each name/value pair in the pblock.
 */

#define pblock_pb2env (*__nsapi30_table->f_pblock_pb2env)

/* --------------------------- Internal things ---------------------------- */
#define pblock_fr (*__nsapi30_table->f_pblock_fr)
#define pblock_replace (*__nsapi30_table->f_pblock_replace)

/* pool_create()
 * Function to create a new pool.
 * Returns non-NULL on success, NULL on failure.
 */
#define pool_create (*__nsapi30_table->f_pool_create)

/* pool_destroy()
 * Frees all memory associated with a pool and destroys the pool.
 */
#define pool_destroy (*__nsapi30_table->f_pool_destroy)

/* pool_enabled()
 * Check if the pools are enabled and a pool is currently set
 * for this thread.  Return 1 if enabled, 0 if not enabled.
 */
#define pool_enabled (*__nsapi30_table->f_pool_enabled)

#define pool_malloc (*__nsapi30_table->f_pool_malloc)
#define pool_free (*__nsapi30_table->f_pool_free)
#define pool_calloc (*__nsapi30_table->f_pool_calloc)
#define pool_realloc (*__nsapi30_table->f_pool_realloc)
#define pool_strdup (*__nsapi30_table->f_pool_strdup)

/*
 * regexp_valid takes a regular expression exp as input. It returns:
 * 
 *  NON_REGEXP      if exp is a standard string
 *                  (above not used -- always returns VALID_REGEXP!!)
 *  INVALID_REGEXP  if exp is a regular expression, but invalid
 *  VALID_REGEXP    if exp is a valid regular expression
 */

#define regexp_valid (*__nsapi30_table->f_regexp_valid)

/*
 * regexp_match 
 * 
 * Takes a prevalidated shell expression exp, and a string str.
 *
 * Returns 0 on match and 1 on non-match.
 */

#define regexp_match (*__nsapi30_table->f_regexp_match)

/*
 * regexp_cmp
 * 
 * Same as above, but validates the exp first. 0 on match, 1 on non-match,
 * -1 on invalid exp. regexp_casecmp does the same thing but is case 
 * insensitive.
 */

#define regexp_cmp (*__nsapi30_table->f_regexp_cmp)
#define regexp_casecmp (*__nsapi30_table->f_regexp_casecmp)

/*
 * sem_init creates a semaphore using the given name and unique 
 * identification number. filename should be a file accessible to the 
 * process. Returns SEM_ERROR on error.
 */

#define sem_init (*__nsapi30_table->f_sem_init)

/*
 * sem_terminate de-allocates the given semaphore.
 */

#define sem_terminate (*__nsapi30_table->f_sem_terminate)

/*
 * sem_grab attempts to gain exclusive access to the given semaphore. If
 * it can't get it, the caller will block. Returns -1 on error.
 */

#define sem_grab (*__nsapi30_table->f_sem_grab)
#define sem_tgrab (*__nsapi30_table->f_sem_tgrab)

/*
 * sem_release releases this process's exclusive control over the given
 * semaphore. Returns -1 on error.
 */

#define sem_release (*__nsapi30_table->f_sem_release)

/*
 * session_create creates a new request structure for the client with the
 * given socket descriptor and sockaddr.
 */

#define session_alloc (*__nsapi30_table->f_session_alloc)
#define session_fill (*__nsapi30_table->f_session_fill)
#define session_create (*__nsapi30_table->f_session_create)

/*
 * session_free frees the given session
 */

#define session_free (*__nsapi30_table->f_session_free)

#define session_dns_lookup (*__nsapi30_table->f_session_dns_lookup)

/*
 *      This describes the API for matching a string with a "shell expression".
 *      The expressions accepted are based loosely on the expressions accepted
 *      by zsh.  A shell expression is a string pattern made up of ordinary
 *      characters and any of the types of pattern sequences listed below.
 *
 *      Pattern                 Matches
 *      *                       zero or more characters
 *      ?                       exactly one character
 *      $                       matches the end of string
 *      [abc]                   matches one instance of any of the characters
 *                              enclosed in []
 *      [a-z]                   matches one instance of any character in the
 *                              specified range of characters
 *      [^abc]                  matches one instance of any character not
 *                              in the enclosed set
 *
 *      Backslash (\) is used to quote a character that would otherwise be
 *      considered part of a pattern sequence, e.g. "2\*2=4".
 *
 *      The following composite shell expression structures are also
 *      recognized:
 *
 *      shexp1~shexp2           matches any string that matches shexp1,
 *                              unless the string also matches shexp2
 *
 *              Example:  "*~*.netscape.com" matches any string that does
 *                              not end with ".netscape.com"
 *
 *      (shexp1|...|shexpN)     matches any string that matches any one of
 *                              the ()-enclosed, |-separated shell
 *                              expressions.
 */

/* Determine whether exp is a valid shell expression */
#define shexp_valid (*__nsapi30_table->f_shexp_valid)

/*
 * shexp_match 
 * 
 * Takes a prevalidated shell expression exp, and a string str.
 *
 * Returns 0 on match and 1 on non-match.
 */

#define shexp_match (*__nsapi30_table->f_shexp_match)


/*
 * shexp_cmp
 * 
 * Same as above, but validates the exp first. 0 on match, 1 on non-match,
 * -1 on invalid exp. shexp_casecmp does the same thing but is case 
 * insensitive.
 */

#define shexp_cmp (*__nsapi30_table->f_shexp_cmp)
#define shexp_casecmp (*__nsapi30_table->f_shexp_casecmp)

/*
 * Regular expression API - Analogous to shell expression API 
 */

#define regexp_valid (*__nsapi30_table->f_regexp_valid)
#define regexp_match (*__nsapi30_table->f_regexp_match)
#define regexp_cmp (*__nsapi30_table->f_regexp_cmp)
#define regexp_casecmp (*__nsapi30_table->f_regexp_casecmp)


#if defined (SHMEM_UNIX_MMAP) || defined (SHMEM_WIN32_MMAP)

/*
 * shmem_alloc allocates a region of shared memory of the given size, using
 * the given name to avoid conflicts between multiple regions within the
 * program. The region will not be automatically grown if its boundaries 
 * are over-run, use shmem_realloc for that. 
 *
 * If expose is non-zero and the underlying system supports it, the
 * file used to create the shared region will be visible to other processes
 * running on the system.
 *
 * name should be unique to the program which calls this routine, otherwise
 * conflicts will arise.
 *
 * Returns a new shared memory region, with the data element being a 
 * pointer to the shared memory. This function must be called before any
 * daemon workers are spawned, in order for the handle to the shared region
 * to be inherited by the children.
 *
 * Because of the requirement that the region must be inherited by the
 * children, the region cannot be re-allocated with a larger size when
 * necessary.
 */
#define shmem_alloc (*__nsapi30_table->f_shmem_alloc)

/*
 * shmem_free de-allocates the specified region of shared memory.
 */
#define shmem_free (*__nsapi30_table->f_shmem_free)

#endif  /* SHMEM_UNIX_MMAP || SHMEM_WIN32_MMAP */

/*
 * systhread_start creates a thread with the given priority, will allocate
 * a stack of stksz bytes, and calls fn with arg as its argument. stksz
 * of zero will allocate a default stack size. 
 * 
 * Returns a new SYS_THREAD pointer on success, SYS_THREAD_ERROR on failure.
 * XXX Priorities are system dependent
 */

#define systhread_start (*__nsapi30_table->f_systhread_start)

/* 
 * systhread_current returns a handle for the current thread.
 */

#define systhread_current (*__nsapi30_table->f_systhread_current)

/*
 * systhread_yield yields the processor to another thread
 */

#define systhread_yield (*__nsapi30_table->f_systhread_yield)

/*
 * systhread_attach makes an existing thread an NSPR thread.
 */
#define systhread_attach (*__nsapi30_table->f_systhread_attach)

/*
 * Detaches a thread that was attached.
 */

#define systhread_detach (*__nsapi30_table->f_systhread_detach)

/* 
 * systhread_terminate terminates the thread that is passed in.
 */
#define systhread_terminate (*__nsapi30_table->f_systhread_terminate)

/*
 * systhread_sleep puts the calling thread to sleep for the given number
 * of milliseconds.
 */
#define systhread_sleep (*__nsapi30_table->f_systhread_sleep)

/*
 * systhread_init initializes the threading system. name is a name for the
 * program for debugging.
 */

#define systhread_init (*__nsapi30_table->f_systhread_init)

/*
 * systhread_timerset starts or re-sets the interrupt timer for a thread
 * system. This should be considered a suggestion as most systems don't allow
 * the timer interval to be changed.
 */

#define systhread_timerset (*__nsapi30_table->f_systhread_timerset)

/*
 * newkey allocates a new integer id for thread-private data. Use this
 * key to identify a variable which you want to appear differently 
 * between threads, and then use setdata to associate a value with this
 * key for each thread.
 */
#define systhread_newkey (*__nsapi30_table->f_systhread_newkey)

/*
 * Get data that has been previously associated with key in this thread.
 * Returns NULL if setkey has not been called with this key by this 
 * thread previously, or the data that was previously used with setkey
 * by this thread with this key.
 */
#define systhread_getdata (*__nsapi30_table->f_systhread_getdata)

/*
 * Associate data with the given key number in this thread.
 */
#define systhread_setdata (*__nsapi30_table->f_systhread_setdata)

/*
 * Set the default stack size for threads created via systhr_start
 */
#define systhread_set_default_stacksize (*__nsapi30_table->f_systhread_set_default_stacksize)

/*
 *      A hodge podge of utility functions and standard functions which 
 *      are unavailable on certain systems
 */

/*
 * getline scans in buf until it finds a LF or CRLF, storing the string in
 * l. It will terminate the string and return:
 * 
 *  0 when done, with the scanned line (minus CR or LF) in l
 *  1 upon EOF, with the scanned line (minus CR or LF) in l
 * -1 on error with the error description in l (uses lineno for information)
 */

#define util_getline (*__nsapi30_table->f_util_getline)

/*
 * env_create creates a new environment with the given env, with n new
 * entries, and places the current position that you should add your
 * entries with at pos.
 * 
 * If env is NULL, it will allocate a new one. If not, it will reallocate
 * that one.
 */

#define util_env_create (*__nsapi30_table->f_util_env_create)

/*
 * util_env_str allocates a string from the given name and value and
 * returns it. It does not check for things like = signs in name.
 */

#define util_env_str (*__nsapi30_table->f_util_env_str)

/*
 * env_replace replaces the occurrence of the given variable with the 
 * value you give.
 */

#define util_env_replace (*__nsapi30_table->f_util_env_replace)

/*
 * util_env_free frees an environment.
 */

#define util_env_free (*__nsapi30_table->f_util_env_free)

/*
 * util_env_copy copies an env
 */
#define util_env_copy (*__nsapi30_table->f_util_env_copy)

/*
 * util_env_find looks through env for the named string. Returns the
 * corresponding value if the named string is found, or NULL if not.
 */
#define util_env_find (*__nsapi30_table->f_util_env_find)

/*
 * hostname gets the local hostname. Returns NULL if it can't find a FQDN.
 * You are free to realloc or free this string.
 */

#define util_hostname (*__nsapi30_table->f_util_hostname)

/*
 * chdir2path changes the current directory to the one that the file
 * path is in. path should point to a file. Caveat: path must be a writable
 * string. It won't get modified permanently.
 */

#define util_chdir2path (*__nsapi30_table->f_util_chdir2path)

/*
 * is_mozilla checks if the given user-agent is mozilla, of at least
 * the given major and minor revisions. These are strings to avoid 
 * ambiguities like 1.56 > 1.5
 */

#define util_is_mozilla (*__nsapi30_table->f_util_is_mozilla)

/*
 * is_url will return 1 if the given string seems to be a URL, or will 
 * return 0 otherwise. 
 * 
 * Because of stupid news URLs, this will return 1 if the string has 
 * all alphabetic characters up to the first colon and will not check for 
 * the double slash.
 */

#define util_is_url (*__nsapi30_table->f_util_is_url)

/*
 * util_later_than checks the date in the string ims, and if that date is 
 * later than or equal to the one in the tm struct lms, then it returns 1.
 *
 * util_time_equal is above, but checks for exact equality.
 *
 * Handles RFC 822, 850, and ctime formats.
 */

#define util_later_than (*__nsapi30_table->f_util_later_than)
#define util_time_equal (*__nsapi30_table->f_util_time_equal)

/* 
 * util_str_time_equal checks the character-string dates are equal.
 * Supports rfc1123 and rfc850 formats.  t1 must be rfc1123
 * Returns 0 if equal, -1 otherwise
 */
#define util_str_time_equal (*__nsapi30_table->f_util_str_time_equal)

/*
 * util_uri_is_evil returns 1 if a URL has ../ or // in it.
 */
#define util_uri_is_evil (*__nsapi30_table->f_util_uri_is_evil)

/*
 * util_uri_parse gets rid of /../, /./, and //.
 * 
 * Assumes that either the string starts with a /, or the string will
 * not .. right off of its beginning.  As such, ../foo.gif will
 * not be changed, although /../foo.gif will become /foo.gif.
 */

#define util_uri_parse (*__nsapi30_table->f_util_uri_parse)

/*
 * util_uri_unescape unescapes the given URI in place (% conversions only).
 */

#define util_uri_unescape (*__nsapi30_table->f_util_uri_unescape)

/*
 * util_uri_escape escapes any nasty chars in s and copies the string into d.
 * If d is NULL, it will allocate and return a properly sized string.
 * Warning: does not check bounds on a given d.
 *
 * util_url_escape does the same thing but does it for a url, i.e. ?:+ is 
 * not escaped.
 */

#define util_uri_escape (*__nsapi30_table->f_util_uri_escape)
#define util_url_escape (*__nsapi30_table->f_util_url_escape)

/*
 * util_sh_escape places a \ in front of any shell-special characters.
 * Returns a newly-allocated copy of the string.
 */

#define util_sh_escape (*__nsapi30_table->f_util_sh_escape)

/*
 * util_mime_separator generates a new MIME separator into the given buffer.
 * The buffer should be more than 4 + 3*10 + 1 bytes long. A CRLF is prepended
 * to the beginning of the string, along with two dashes. The string is null
 * terminated, with no CRLF. The intent is that you create your content-type
 * header by accessing &sep[4], and afterwards print sep followed by CRLF
 * for message boundaries.
 *
 * Returns the length of the string.
 */
#define util_mime_separator (*__nsapi30_table->f_util_mime_separator)

/*
 * util_itoa converts the given integer to a string into a.
 */

#define util_itoa (*__nsapi30_table->f_util_itoa)

/*
 * util_vsprintf and util_sprintf are simplified clones of the System V 
 * vsprintf and sprintf routines.
 * 
 * Returns the number of characters printed. Only handles %d and %s,
 * does not handle any width or precision.
 */

#define util_vsprintf (*__nsapi30_table->f_util_vsprintf)
#define util_sprintf (*__nsapi30_table->f_util_sprintf)

/* These routines perform bounds checks. */
#define util_vsnprintf (*__nsapi30_table->f_util_vsnprintf)
#define util_snprintf (*__nsapi30_table->f_util_snprintf)

/* util_strftime()
 * Thread safe version of strftime.
 * No bounds checking is done s.  t must be a valid tm structure.
 */
#define util_strftime (*__nsapi30_table->f_util_strftime)

/* Various thread safe routines. */

#define util_strtok (*__nsapi30_table->f_util_strtok)
#define util_localtime (*__nsapi30_table->f_util_localtime)
#define util_ctime (*__nsapi30_table->f_util_ctime)
#define util_strerror (*__nsapi30_table->f_util_strerror)
#define util_gmtime (*__nsapi30_table->f_util_gmtime)
#define util_asctime (*__nsapi30_table->f_util_asctime)

#ifdef NEED_STRCASECMP
#define util_strcasecmp (*__nsapi30_table->f_util_strcasecmp)
#define strcasecmp(s1, s2) util_strcasecmp(s1, s2)
#endif /* NEED_STRCASECMP */

#ifdef NEED_STRNCASECMP
#define util_strncasecmp (*__nsapi30_table->f_util_strncasecmp)
#define strncasecmp(s1, s2, n) util_strncasecmp(s1, s2, n)
#endif /* NEED_STRNCASECMP */

#ifdef XP_UNIX

/*
 * can_exec returns 1 if you can execute the file described by finfo, and 
 * 0 if you can't.
 */

#define util_can_exec (*__nsapi30_table->f_util_can_exec)

/*
 * Thread safe getpwnam
 */
#define util_getpwnam (*__nsapi30_table->f_util_getpwnam)

#define util_waitpid (*__nsapi30_table->f_util_waitpid)

#endif /* XP_UNIX */

#ifdef XP_WIN32

/* util_delete_directory()
 * This routine deletes all the files in a directory.  If delete_directory is
 * TRUE it will also delete the directory itself.
 */
#define util_delete_directory (*__nsapi30_table->f_util_delete_directory)

#endif /* XP_WIN32 */

/*
 * conf_init reads the given configuration file and sets any non-default
 * parameters to their given setting.
 */
#define conf_init (*__nsapi30_table->f_conf_init)
#define conf_run_init_functions (*__nsapi30_table->f_conf_run_init_functions)

/*
 * conf_terminate frees any data the conf routines may be holding.
 */
#define conf_terminate (*__nsapi30_table->f_conf_terminate)

/*
 * conf_getServerString returns the Server ID string
 */
#define conf_getServerString (*__nsapi30_table->f_conf_getServerString)

/*
 * Get a structure with the global variables for this server.
 */
#define conf_getglobals (*__nsapi30_table->f_conf_getglobals)

/*
 * func_init reads the static FuncStruct arrays and creates the global 
 * function table from them.
 *
 * func_init will only read from the static arrays defined in func.c.
 */
#define func_init (*__nsapi30_table->f_func_init)

/*
 * func_find returns a pointer to the function named name, or NULL if none
 * exists.
 */
#define func_find (*__nsapi30_table->f_func_find)

/* DO NOT USE this function.
 */
#define func_set_native_thread_flag (*__nsapi30_table->f_func_set_native_thread_flag)

/*
 * func_exec will try to execute the function whose name is the "fn" entry
 * in the given pblock. If name is not found, it will log a misconfig of
 * missing fn parameter. If it can't find it, it will log that. In these
 * cases it will return REQ_ABORTED. Otherwise, it will return what the 
 * function being executed returns.
 */
#define func_exec (*__nsapi30_table->f_func_exec)

/*
 * func_replace will replace a function in the server's function table with
 * another.  Returns the FuncPtr to the old function if it replaces the 
 * function, otherwise it returns 0.
 */
#define func_replace (*__nsapi30_table->f_func_replace)

/*
 * func_insert dynamically inserts a named function into the server's
 * table of functions. Returns the FuncStruct it keeps in internal 
 * databases, because on server restart you are responsible for freeing 
 * (or not) its contents.
 */
#define func_insert (*__nsapi30_table->f_func_insert)
#define object_execute (*__nsapi30_table->f_object_execute)

/*
 * gets the first line of an HTTP request
 */
#define http_find_request (*__nsapi30_table->f_http_find_request)

/*
 * parses the first line of an HTTP request
 */
#define http_parse_request (*__nsapi30_table->f_http_parse_request)

/*
 * Scans HTTP headers from the given netbuf, and places them in headers.
 * If netbuf is NULL, the session's inbuf is used.
 * 
 * Folded lines are joined and the linefeed removed (but not the whitespace).
 * If there are any repeat headers they are joined and the two field bodies
 * separated by a comma and space.
 *
 * t should be a string of length REQ_MAX_LINE. This is a convenience to
 * req.c so that we don't use too much runtime stack.
 *
 * Session is an optional parameter. Use NULL if you wish. It's used for
 * error logs.
 */
#define http_scan_headers (*__nsapi30_table->f_http_scan_headers)

/*
 * Starts the HTTP response. If HTTP/0.9, does nothing. If 1.0, sends header.
 * If this returns REQ_NOACTION, the method was head and no body should be
 * sent. Otherwise, it will return REQ_PROCEED.
 */
#define http_start_response (*__nsapi30_table->f_http_start_response)

/*
 * http_hdrs2env takes the entries from the given pblock and converts them
 * to an environment. 
 *
 * Each name entry will be made uppercase, prefixed with HTTP_ and any
 * occurrence of - will be converted to _.
 */
#define http_hdrs2env (*__nsapi30_table->f_http_hdrs2env)

/*
 * http_status sets status to the code n, with reason string r. If r is
 * NULL, the server will attempt to find one for the given status code.
 * If it finds none, it will give "Because I felt like it."
 */
#define http_status (*__nsapi30_table->f_http_status)

/*
 * http_set_finfo sets content-length and last-modified
 */

#define http_set_finfo (*__nsapi30_table->f_http_set_finfo)

/*
 * Takes the given pblock and prints headers into the given buffer at 
 * position pos. Returns the buffer, reallocated if needed. Modifies pos.
 */
#define http_dump822 (*__nsapi30_table->f_http_dump822)

/*
 * Finishes a request. For HTTP, this just closes the socket.
 */
#define http_finish_request (*__nsapi30_table->f_http_finish_request)

/*
 * http_handle_session processes each request generated by Session
 */
#define http_handle_session (*__nsapi30_table->f_http_handle_session)

/*
 * http_uri2url takes the give URI prefix and URI suffix and creates a 
 * newly-allocated full URL from them of the form
 * http://(server):(port)(prefix)(suffix)
 * 
 * If you want either prefix or suffix to be skipped, use "" instead of NULL.
 *
 * Normally, the server hostname is taken from the ServerName parameter in
 * magnus.conf. The newer function http_uri2url_dynamic should be used when 
 * a Session and Request structure are available, to ensure that the browser
 * gets redirected to the exact host they were originally referencing.
 */

#define http_uri2url (*__nsapi30_table->f_http_uri2url)
#define http_uri2url_dynamic (*__nsapi30_table->f_http_uri2url_dynamic)

/*
 * http_set_keepalive_timeout sets the number of seconds to wait for a new
 * request to come from a persistent connection. Returns nothing. Intended
 * to be called at server startup only.
 *
 * Specifying a timeout of zero will disable persistent connections and allow
 * browsers to request only one file per connection.
 */
#define http_set_keepalive_timeout (*__nsapi30_table->f_http_set_keepalive_timeout)

/*
 * log_error logs an error of the given degree from the function func
 * and formats the arguments with the printf() style fmt. Returns whether the
 * log was successful. Records the current date.
 *
 * sn and rq are optional parameters. If given, information about the client
 * will be reported.
 */
#define log_error_v (*__nsapi30_table->f_log_error_v)
#define log_error (*__nsapi30_table->f_log_error)

/*
 *  Internal use only 
 */
#define log_ereport_v (*__nsapi30_table->f_log_ereport_v)
#define log_ereport (*__nsapi30_table->f_log_ereport)

/*
 * object_create will create a new object and return a pointer to it.
 * It will allocate space for nd directive types and set name accordingly.
 */
#define object_create (*__nsapi30_table->f_object_create)

/*
 * object_free will free an object and any data associated with it.
 */
#define object_free (*__nsapi30_table->f_object_free)

/*
 * object_add_directive will add a new directive to the dtable for 
 * the directive class at position dc.
 */
#define object_add_directive (*__nsapi30_table->f_object_add_directive)

/*
 * Executes the directive specified by inst within the context of the
 * given session and request structures. Returns what the executed function
 * returned (one of the REQ_* codes defined in req.h).
 *
 * This prototype uses void * for Request * in order to avoid including
 * all of req.h. 
 *
 */

/*
 * objset_scan_buffer will scan through buffer, looking for object 
 * configuration information, and adding them to the object set os if it 
 * finds any. If os is NULL it will allocate a new object set.
 *
 * If any error occurs (syntax error, premature EOF) this function will
 * free os, print an error message into errstr, and return NULL.
 * This is because a config. file error is viewed as a catastrophic error
 * from which httpd should not try to recover. If httpd were to continue
 * after an error, it would not behave as the admin. expected and he/she
 * may not notice until it's too late.
 *
 * Upon EOF the file will not be closed.
 */
#define objset_scan_buffer (*__nsapi30_table->f_objset_scan_buffer)

/*
 * objset_create creates a new object set and returns a pointer to it.
 */
#define objset_create (*__nsapi30_table->f_objset_create)

/*
 * objset_free will free an object set, any associated objects, and any
 * associated Init functions.
 */
#define objset_free (*__nsapi30_table->f_objset_free)

/*
 * objset_free_setonly frees only the object set, and not the associated
 * objects or init functions.
 */
#define objset_free_setonly (*__nsapi30_table->f_objset_free_setonly)

/*
 * objset_new_object will add a new object to objset with the specified
 * name. It returns a pointer to the new object (which may be anywhere in 
 * the objset).
 */
#define objset_new_object (*__nsapi30_table->f_objset_new_object)

/*
 * objset_add_object will add the existing object to os.
 */
#define objset_add_object (*__nsapi30_table->f_objset_add_object)

/*
 * objset_add_init will add the initialization function specified by 
 * initfn to the given object set. Modifies os->initfns.
 */
#define objset_add_init (*__nsapi30_table->f_objset_add_init)

/*
 * objset_findbyname will find the object in objset having the given name,
 * and return the object if found, and NULL otherwise.
 * ign is a set of objects to ignore.
 */
#define objset_findbyname (*__nsapi30_table->f_objset_findbyname)

/*
 * objset_findbyppath will find the object in objset having the given 
 * partial path entry. Returns object if found, NULL otherwise.
 * ign is a set of objects to ignore.
 */
#define objset_findbyppath (*__nsapi30_table->f_objset_findbyppath)

/*
 * request_create creates a new request structure.
 */
#define request_create (*__nsapi30_table->f_request_create)

/*
 * request_free destroys a request structure.
 */
#define request_free (*__nsapi30_table->f_request_free)

/*
 * Restarts a request for a given URI internally. If rq is non-NULL, the
 * function will keep the old request's headers and protocol, but with a new 
 * URI and method of GET. If the previous method was HEAD, this is preserved.
 * Any other method becomes GET. You may assume that if you give it a request
 * structure that it will use the same structure.
 *
 * Once you have this new Request, you must then do what you want with
 * it (e.g. send the object back, perform uri2path translation, etc.)
 */
#define request_restart_internal (*__nsapi30_table->f_request_restart_internal)

/*
 * request_header finds the named header depending on the requesting 
 * protocol. If possible, it will not load headers until the first is 
 * requested. You have to watch out because this can return REQ_ABORTED.
 */
#define request_header (*__nsapi30_table->f_request_header)

/*
 * request_loadheaders just makes sure the headers have been loaded.
 */
#define request_loadheaders (*__nsapi30_table->f_request_loadheaders)

/*
 * request_stat_path tries to stat path. If path is NULL, it will look in
 * the vars pblock for "path". If the stat is successful, it returns the stat 
 * structure. If not, returns NULL and leaves a message in rq->staterr. If a 
 * previous call to this function was successful, and path is the same, the 
 * function will simply return the previously found value.
 *
 * User functions should not free this structure.
 */

#define request_stat_path (*__nsapi30_table->f_request_stat_path)

/*
 * Random number generation
 *
 *      random_create - create a new random number context
 *      random_update - update a context with random data
 *      random_generate - generate random bytes
 *      random_destroy - destroy a random number context
 */

#define random_create (*__nsapi30_table->f_random_create)
#define random_update (*__nsapi30_table->f_random_update)
#define random_generate (*__nsapi30_table->f_random_generate)
#define random_destroy (*__nsapi30_table->f_random_destroy)

/*
 * MD5 hash routines
 *
 *      md5hash_create - create an MD5 hash context
 *      md5hash_copy - make a copy of an MD5 hash context
 *      md5hash_begin - initialize an MD5 hash context
 *      md5hash_update - update MD5 hash with more input data
 *      md5hash_end - finalize MD5 hash and get result
 *      md5hash_destroy - destroy an MD5 hash context
 *      md5hash_data - compute MD5 hash of data in one step
 */

#define md5hash_create (*__nsapi30_table->f_md5hash_create)
#define md5hash_copy (*__nsapi30_table->f_md5hash_copy)
#define md5hash_begin (*__nsapi30_table->f_md5hash_begin)
#define md5hash_update (*__nsapi30_table->f_md5hash_update)
#define md5hash_end (*__nsapi30_table->f_md5hash_end)
#define md5hash_destroy (*__nsapi30_table->f_md5hash_destroy)
#define md5hash_data (*__nsapi30_table->f_md5hash_data)

/*
 * ACL_SetupEval -
 *  Setup environment and call ACL_EvalTestRights.
 */
#define ACL_SetupEval (*__nsapi30_table->f_ACL_SetupEval)

/*
 * servact_translate_uri
 * Returns the translated path (filename) for the given uri, NULL otherwise.
 * If authentication is required for the given uri, nothing is returned even
 * if the current user has authenticated to that area.
 */
#define servact_translate_uri (*__nsapi30_table->f_servact_translate_uri)

#endif /* !INTNSAPI */

#ifndef FILE_MMAP
#define filebuf_open_nostat(fd,sz,finfo) filebuf_open(fd,sz)
#endif

#ifdef XP_UNIX
#define dir_open opendir
#define dir_read readdir
#define dir_close closedir
#define dir_create(path) mkdir(path, 0755)
#define dir_remove rmdir
#define system_chdir chdir
#define file_unix2local(path,p2) strcpy(p2,path)
#endif /* XP_UNIX */

#ifdef XP_WIN32
#define dir_create _mkdir
#define dir_remove _rmdir
#define system_chdir SetCurrentDirectory
#endif /* XP_WIN32 */

/*
 * Thread-safe variants of localtime and gmtime
 */
#define system_localtime(curtime, ret) util_localtime(curtime, ret)
#define system_gmtime(curtime, ret) util_gmtime(curtime, ret)

/* 
 * pblock_find finds the entry with the given name in pblock pb.
 *
 * If it is successful, it returns the param block. If not, it returns NULL.
 */

#define pblock_find(name, pb) (pblock_fr(name,pb,0))

/*
 * pblock_remove behaves exactly like pblock_find, but removes the given
 * entry from pb.
 */

#define pblock_remove(name, pb) (pblock_fr(name,pb,1))

/*
 * session_dns returns the DNS hostname of the client of this session,
 * and inserts it into the client pblock. Returns NULL if unavailable.
 */

#define session_dns(sn) session_dns_lookup(sn, 0)

/*
 * session_maxdns looks up a hostname from an IP address, and then verifies 
 * that the host is really who they claim to be. 
 */

#define session_maxdns(sn) session_dns_lookup(sn, 1)

#define protocol_find_request http_find_request
#define protocol_parse_request http_parse_request
#define protocol_scan_headers http_scan_headers
#define protocol_start_response http_start_response
#define protocol_status http_status
#define protocol_set_finfo http_set_finfo
#define protocol_finish_request http_finish_request
#define protocol_handle_session http_handle_session
#define protocol_uri2url http_uri2url
#define protocol_uri2url_dynamic http_uri2url_dynamic
#define protocol_set_keepalive_timeout http_set_keepalive_timeout

/* XXXrobm temporary compatibility */
#define request_uri2path servact_uri2path
#define request_pathchecks servact_pathchecks
#define request_fileinfo servact_fileinfo
#define request_service servact_service

#define request_handle_processed servact_handle_processed
#define request_translate_uri servact_translate_uri
#define request_finderror servact_finderror

/* --- OBSOLETE ----------------------------------------------------------
 * The following macros/functions are obsolete and are only maintained for
 * compatibility.  Do not use them. 11-19-96
 * -----------------------------------------------------------------------
 */

#define SYS_STDERR STDERR_FILENO

#ifdef XP_WIN32

typedef HANDLE pid_t;

#define ERROR_PIPE \
	(ERROR_BROKEN_PIPE | ERROR_BAD_PIPE |\
	 ERROR_PIPE_BUSY | ERROR_PIPE_LISTENING | ERROR_PIPE_NOT_CONNECTED)
#define CONVERT_TO_PRINTABLE_FORMAT(Filename) 	\
{									   		\
	register char *s;					   	\
	if (Filename)				   			\
	for (s = Filename; *s; s++) 	   		\
		if ( *s	== '\\')				   	\
			*s = '/';				   		\
}
#define CONVERT_TO_NATIVE_FS(Filename) 	   \
{									   	   \
	register char *s;				   	   \
	if (Filename)						   \
		for (s = Filename; *s; s++) 	   \
			if ( *s	== '/')				   \
				*s = '\\';				   \
}									 

#ifdef INTNSAPI
NSAPI_PUBLIC extern nsapi_dispatch_t *__nsapi30_table;
#else
__declspec(dllimport) nsapi_dispatch_t *__nsapi30_table;
#endif /* INTNSAPI */

#else /* !XP_WIN32 */

NSAPI_PUBLIC extern nsapi_dispatch_t *__nsapi30_table;

#endif /* XP_WIN32 */

#endif /* !PUBLIC_NSAPI_H */

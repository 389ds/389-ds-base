/** --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
  --- END COPYRIGHT BLOCK ---  */
/*
 * dsgw.h -- defines for HTTP gateway 
 */

#if !defined( DSGW_NO_SSL ) && !defined( NET_SSL )
#define DSGW_NO_SSL
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#ifdef LINUX
#include <sys/param.h>
#endif
#include <ldap.h>
#include <litekey.h>
#include <ssl.h>
#ifndef DSGW_NO_SSL
#include <ldap_ssl.h>
#endif
#include "../../include/srchpref.h"

#if defined( XP_WIN32 )

#include "base/systems.h"
#include "proto-ntutil.h"

#endif

#include <prprf.h>

#ifdef AIXV4
#include <strings.h>
#endif /* AIXV4 */

#include "base/util.h"
#include "libadmin/libadmin.h"
#include "i18n.h"

#include <unicode/ucnv.h>
#include <unicode/ucol.h>
#include <unicode/ustring.h>

#if defined( XP_WIN32 )
#define DSGW_PATHSEP_CHAR	'\\'
#define DSGW_PATHSEP_STR        "\\"
#define DSGW_NULL_DEVICE	"nul:"
#define DSGW_DELETE_CMD		"del /Q"
#else
#define DSGW_PATHSEP_CHAR	'/'
#define DSGW_PATHSEP_STR        "/"
#define DSGW_NULL_DEVICE	"/dev/null"
#define DSGW_DELETE_CMD		"rm"
#endif

#define MSIE40_DEFAULT_CHARSET  "iso-8859-1,*,utf-8"

/* Used to name the converter used to convert from the users charset to UTF8 */
#define UNICODE_ENCODING_UTF_8 "UTF-8"
#define ISO_8859_1_ENCODING "ISO_8859-1"

extern char            *context ;
extern char            *langwich; /* The language chosen by libsi18n. */
extern char            *countri; /* The language chosen by libsi18n. */

/*
 * define DSGW_DEBUG to cause extensive debugging output to be written
 * to /tmp/CGINAME and CGI's output written to /tmp/CGINAME.out 
 */
/* #define DSGW_DEBUG  */		/* turn on debugging output */

#define DSGW_UTF8_NBSP "\302\240" /* u00A8, in UTF-8 */

/*
 * XXX the next group of #defines assume that HTTP server has cd'd to
 * our CGI dir.
 */
#define SERVER_ROOT_PATH "../../.."
#define DSGW_CONFIGDIR_HTTP	"../config/"
#define DSGW_CONFIGDIR_ADMSERV	"../config/"
/*#define DSGW_CONFIGDIR_ADMSERV	SERVER_ROOT_PATH "/admin-serv/config/"*/
#define	DSGW_DBSWITCH_FILE	"dbswitch.conf"
#define	DSGW_DBSWITCH_TMPFILE	"dbswitch.tmp"
#define DSGW_TMPLDIR_HTTP	"../config/"
#define DSGW_TMPLDIR_ADMSERV	"../html/"
#define DSGW_DOCDIR_HTTP        "../html"
#define DSGW_CONTEXTDIR_HTTP    "../context/"
#define	DSGW_HTMLDIR		"../html"
#define DSGW_MANROOT	        SERVER_ROOT_PATH "/manual/"
#define DSGW_MANUALSHORTCUT	".MANUAL"
#define DSGW_MANUALSHORTCUT_LEN	7
#define	DSGW_ADMSERV_BINDIR	"/admin-serv/bin/"
#define	DSGW_USER_ADM_BINDIR	"/user-environment/bin/"
#define	DSGW_LCACHECONF_PPATH	"ldap/config/"	/* partial path from /userdb */
#define DSGW_LCACHECONF_FILE	"lcache.conf"
#define DSGW_TOOLSDIR           "/ldap/tools"
#define DSGW_LDAPSEARCH         "ldapsearch"
#define DSGW_LDAPMODIFY         "ldapmodify"

#define DSGW_SEARCHPREFSFILE	"dsgwsearchprefs.conf"
#define DSGW_FILTERFILE		"dsgwfilter.conf"
#define	DSGW_CONFIGFILE		"dsgw.conf"
#define DSGW_DEFSECURITYPATH	"../ssl"

#define DSGW_CONFIG_LISTPREFIX		"list-"
#define DSGW_CONFIG_DISPLAYPREFIX	"display-"
#define DSGW_CONFIG_EDITPREFIX		"edit-"
#define DSGW_CONFIG_ADDPREFIX		"add-"

#define DSGW_SRCHMODE_SMART		"smart"
#define DSGW_SRCHMODE_SMART_ID		1
#define DSGW_SRCHMODE_COMPLEX		"complex"
#define DSGW_SRCHMODE_COMPLEX_ID	2
#define DSGW_SRCHMODE_PATTERN		"pattern"
#define DSGW_SRCHMODE_PATTERN_ID	3
#define DSGW_SRCHMODE_AUTH		"auth"
#define DSGW_SRCHMODE_AUTH_ID		4

#define DSGW_SRCHTYPE_AUTH		"auth"

#define LDAP_URL_PREFIX		"ldap://"
#define LDAP_URL_PREFIX_LEN     7
#define	LDAPDB_URL_PREFIX	"ldapdb://"
#define LDAPDB_URL_PREFIX_LEN     9

/* attribute types */
#define DSGW_ATTRTYPE_OBJECTCLASS	"objectClass"
#define DSGW_ATTRTYPE_HASUBORDINATES	"hasSubordinates"
#define DSGW_ATTRTYPE_USERPASSWORD	"userPassword"

#define DSGW_ATTRTYPE_NTUSERDOMAINID	"nTUserDomainId"
#define DSGW_ATTRTYPE_USERID		"uid"

#define DSGW_OC_NTUSER			"ntuser"

#define DSGW_ATTRTYPE_NTGROUPDOMAINID	"nTGroupDomainId"
#define DSGW_ATTRTYPE_NTGROUPNAME	"nTGroupName"
#define DSGW_ATTRTYPE_AIMSTATUSTEXT     "nsaimstatustext"

#if defined( XP_WIN32 )
#include <lmaccess.h>
#else
/* 
 * For Gateway's running on UNIX Platforms. 
 * These are all defined in <lmaccess.h> on Win32.
 */

/*
 * Special Values and Constants - User
 */

/*
 * Privilege levels (USER_INFO_X field usriX_priv (X = 0/1)).
 */

#define USER_PRIV_MASK      0x3
#define USER_PRIV_GUEST     0
#define USER_PRIV_USER      1
#define USER_PRIV_ADMIN     2

/*
 *  Bit masks for field usriX_flags of USER_INFO_X (X = 0/1).
 */

#define UF_SCRIPT               0x0001
#define UF_ACCOUNTDISABLE       0x0002
#define UF_HOMEDIR_REQUIRED     0x0008
#define UF_LOCKOUT              0x0010
#define UF_PASSWD_NOTREQD       0x0020
#define UF_PASSWD_CANT_CHANGE   0x0040

/*
 * Account type bits as part of usri_flags.
 */

#define UF_TEMP_DUPLICATE_ACCOUNT       0x0100
#define UF_NORMAL_ACCOUNT               0x0200
#define UF_INTERDOMAIN_TRUST_ACCOUNT    0x0800
#define UF_WORKSTATION_TRUST_ACCOUNT    0x1000
#define UF_SERVER_TRUST_ACCOUNT         0x2000

#define UF_MACHINE_ACCOUNT_MASK ( UF_INTERDOMAIN_TRUST_ACCOUNT | \
                                  UF_WORKSTATION_TRUST_ACCOUNT | \
                                  UF_SERVER_TRUST_ACCOUNT )

#define UF_ACCOUNT_TYPE_MASK         ( \
                    UF_TEMP_DUPLICATE_ACCOUNT | \
                    UF_NORMAL_ACCOUNT | \
                    UF_INTERDOMAIN_TRUST_ACCOUNT | \
                    UF_WORKSTATION_TRUST_ACCOUNT | \
                    UF_SERVER_TRUST_ACCOUNT \
                )

#define UF_DONT_EXPIRE_PASSWD           0x10000


#define UF_SETTABLE_BITS        ( \
                    UF_SCRIPT | \
                    UF_ACCOUNTDISABLE | \
                    UF_LOCKOUT | \
                    UF_HOMEDIR_REQUIRED  | \
                    UF_PASSWD_NOTREQD | \
                    UF_PASSWD_CANT_CHANGE | \
                    UF_ACCOUNT_TYPE_MASK | \
                    UF_DONT_EXPIRE_PASSWD \
                )

/*
 *  Bit masks for field usri2_auth_flags of USER_INFO_2.
 */

#define AF_OP_PRINT             0x1
#define AF_OP_COMM              0x2
#define AF_OP_SERVER            0x4
#define AF_OP_ACCOUNTS          0x8
#define AF_SETTABLE_BITS        (AF_OP_PRINT | AF_OP_COMM | \
                                AF_OP_SERVER | AF_OP_ACCOUNTS)

#endif /* XP_WIN32 */

#define	MAX_NTUSERID_LEN	20

/* Types of privs in usri3_priv of struct USER_INFO_3  */
#define DSGW_NT_UP_GUEST	"Guest"
#define DSGW_NT_UP_USER	"User"
#define DSGW_NT_UP_ADMIN	"Admin"

/* Meaning of flags in usri3_flags of struct USER_INFO_3  */
#define DSGW_NT_UF_SCRIPT	"Logon Script Executed"
#define DSGW_NT_UF_ACCOUNT_DISABLED	"Account Disabled"
#define DSGW_NT_UF_HOMEDIR_REQD	"Home Directory Required"
#define DSGW_NT_UF_PASSWD_NOTREQD	"Password Not Required"
#define DSGW_NT_UF_PASSWD_CANT_CHANGE	"User Cannot Change Password"
#define DSGW_NT_UF_LOCKOUT	"Account Locked Out"
#define DSGW_NT_UF_DONT_EXPIRE_PASSWORD	"Password Never Expires"

#define DSGW_NT_UF_NORMAL_ACCOUNT	"Default Account Type"
#define DSGW_NT_UF_TEMP_DUPLICATE_ACCOUNT	"Temporary Account Type"
#define DSGW_NT_UF_TEMP_WRKSTN_TRUST_ACCOUNT	"Workstation Account Type"
#define DSGW_NT_UF_TEMP_SERVER_TRUST_ACCOUNT	"Server Account Type"
#define DSGW_NT_UF_TEMP_INTERDOMAIN_TRUST_ACCOUNT	"Interdomain Trust Account Type"

#define DSGW_NT_AF_OP_PRINT	"Print Operator"
#define DSGW_NT_AF_OP_COMM	"Backup Operator"
#define DSGW_NT_AF_OP_SERVER	"Server Operator"
#define DSGW_NT_AF_OP_ACCOUNTS	"Accounts Operator"

/* HTTP request methods flags */
#define DSGW_METHOD_GET		0x01
#define DSGW_METHOD_POST	0x02

/* URL prefixes specific to our gateway */
#define	DSGW_URLPREFIX_MAIN_HTTP	"lang?file="
#define	DSGW_URLPREFIX_MAIN_ADMSERV	""
/*#define	DSGW_URLPREFIX_CGI_HTTP		"../bin/"*/
#define	DSGW_URLPREFIX_CGI_HTTP		""
#define	DSGW_URLPREFIX_CGI_ADMSERV	""
#define DSGW_URLPREFIX_BIN              "/clients/dsgw/bin/"

#define DSGW_URLPREFIX_MAIN		DSGW_URLPREFIX_MAIN_HTTP

#define DSGW_CGINAME_DOSEARCH		"dosearch"
#define DSGW_CGINAME_BROWSE		"browse"
#define DSGW_CGINAME_SEARCH		"search"
#define DSGW_CGINAME_CSEARCH		"csearch"
#define DSGW_CGINAME_AUTH		"auth"
#define DSGW_CGINAME_EDIT		"edit"
#define DSGW_CGINAME_DOMODIFY		"domodify"
#define	DSGW_CGINAME_TUTOR		"tutor"
#define	DSGW_CGINAME_DNEDIT		"dnedit"
#define	DSGW_CGINAME_LANG		"lang"

/* definitions for modes - they type of operation we are performing */
/* These definitions need to match, one-for-one, the DSGW_CGINAMEs */
#define DSGW_MODE_DOSEARCH		1
#define	DSGW_CGINUM_DOSEARCH		DSGW_MODE_DOSEARCH
#define DSGW_MODE_BROWSE		2
#define DSGW_CGINUM_BROWSE		DSGW_MODE_BROWSE
#define DSGW_MODE_SEARCH		3
#define DSGW_CGINUM_SEARCH		DSGW_MODE_SEARCH
#define DSGW_MODE_CSEARCH		4
#define DSGW_CGINUM_CSEARCH		DSGW_MODE_CSEARCH
#define DSGW_MODE_AUTH			5
#define DSGW_CGINUM_AUTH		DSGW_MODE_AUTH
#define DSGW_MODE_EDIT			6
#define DSGW_CGINUM_EDIT		DSGW_MODE_EDIT
#define DSGW_MODE_DOMODIFY		7
#define DSGW_CGINUM_DOMODIFY		DSGW_MODE_DOMODIFY
#define DSGW_MODE_TUTOR			8
#define	DSGW_CGINUM_TUTOR		DSGW_MODE_TUTOR
#define	DSGW_MODE_DNEDIT		9
#define	DSGW_CGINUM_DNEDIT		DSGW_MODE_DNEDIT
#define	DSGW_MODE_LANG		        10
#define	DSGW_CGINUM_LANG		DSGW_MODE_LANG
#define	DSGW_MODE_LASTMODE		DSGW_MODE_LANG
#define	DSGW_MODE_NUMMODES		DSGW_MODE_LASTMODE
#define DSGW_MODE_UNKNOWN		99

/* error codes -- messages are in dsgw_errs[] array in error.c */
#define DSGW_ERR_BADMETHOD		1
#define DSGW_ERR_BADFORMDATA		2
#define DSGW_ERR_NOMEMORY		3
#define DSGW_ERR_MISSINGINPUT		4
#define DSGW_ERR_BADFILEPATH		5
#define DSGW_ERR_BADCONFIG		6
#define DSGW_ERR_LDAPINIT		7
#define DSGW_ERR_LDAPGENERAL		8
#define DSGW_ERR_UNKSRCHTYPE		9
#define DSGW_ERR_NOFILTERS		10
#define DSGW_ERR_OPENHTMLFILE		11
#define DSGW_ERR_SEARCHMODE		12
#define DSGW_ERR_UNKATTRLABEL		13
#define DSGW_ERR_UNKMATCHPROMPT		14
#define DSGW_ERR_LDAPURL_NODN		15
#define DSGW_ERR_LDAPURL_BADSCOPE	16
#define DSGW_ERR_LDAPURL_NOTLDAP	17
#define DSGW_ERR_LDAPURL_BAD		18
#define DSGW_ERR_INTERNAL		19
#define DSGW_ERR_OPENDIR		20
#define DSGW_ERR_WRITEINDEXFILE		21
#define DSGW_ERR_OPENINDEXFILE		22
#define DSGW_ERR_SSLINIT		23
#define DSGW_ERR_NO_MGRDN		24
/*
 * Note: do not add more error codes here!  The cookie error codes use the
 * same error code space as all the others.  Go to the end of the "more error
 * codes" section and add new error codes there.
 */

/* Cookie db routines - error codes */
#define DSGW_CKDB_KEY_NOT_PRESENT	25
#define	DSGW_CKDB_DBERROR		26
#define	DSGW_CKDB_EXPIRED		27
#define	DSGW_CKDB_RNDSTRFAIL		28
#define	DSGW_CKDB_NODN			29
#define	DSGW_CKDB_CANTOPEN		30
#define	DSGW_CKDB_CANTAPPEND		31

/* more error codes */
#define DSGW_ERR_NOSECPATH		32
#define DSGW_ERR_NOSEARCHSTRING		33
#define DSGW_ERR_CONFIGTOOMANYARGS	34
#define	DSGW_ERR_ADMSERV_CREDFAIL	35
#define	DSGW_ERR_LDAPDBURL_NODN		36
#define	DSGW_ERR_LDAPDBURL_NOTLDAPDB	37
#define	DSGW_ERR_LDAPDBURL_BAD		38
#define	DSGW_ERR_LCACHEINIT		39
#define DSGW_ERR_WSAINIT		40
#define DSGW_ERR_SERVICETYPE		41
#define	DSGW_ERR_DBCONF			42
#define DSGW_ERR_USERDB_PATH		43
#define DSGW_ERR_UPDATE_DBSWITCH	44
#define	DSGW_ERR_ENTRY_NOT_FOUND	45
#define DSGW_ERR_DB_ERASE               46
#define	DSGW_ERR_LOCALDB_PERMISSION_DENIED	47
#define DSGW_ERR_NOATTRVALUE		48
#define DSGW_ERR_USERID_REQUIRED		49
#define DSGW_ERR_DOMAINID_NOTUNIQUE		50
#define DSGW_ERR_USERID_DOMAINID_REQUIRED		51
#define DSGW_ERR_USERID_MAXLEN_EXCEEDED		52
#define DSGW_ERR_CHARSET_NOT_SUPPORTED		53

/* Return codes from dsgw_init_ldap() */
#define	DSGW_BOUND_ASUSER		1
#define	DSGW_BOUND_ANONYMOUS		2

/* NT Domain Id seperator */
#define	DSGW_NTDOMAINID_SEP		':'

/* Cookie names */
#define DSGW_BROWSESBCKNAME			"nsdsgwbrowseSB"
#define DSGW_SEARCHSBCKNAME			"nsdsgwsearchSB"
#define DSGW_AUTHCKNAME				"nsdsgwauth"
#define DSGW_CKHDR				"Set-cookie: "
#define	DSGW_EXPSTR				"expires="
#define	DSGW_UNAUTHSTR				"[unauthenticated]"

/* Name of cookie database  - context will be appended to "cookies" for multiple GW's*/
#define DSGW_COOKIEDB_FNAME		SERVER_ROOT_PATH "/bin/slapd/authck/cookies" 

/* Default lifetime of authentication cookies (in seconds) */
#define DSGW_DEF_AUTH_LIFETIME		( 60 * 60 )	/* one hour */

#define DSGW_SECS_PER_DAY		( 60 * 60 * 24 ) /* one day */

#define	DSGW_CKPURGEINTERVAL		( 60 * 10 ) /* Ten minutes */

#define	DSGW_MODIFY_GRACEPERIOD		( 60 * 5 ) /* Five minutes */

/* String used as DN in auth CGI to indicate "I want to bind as the root dn"  */
#define	MGRDNSTR	"MANAGER"

/* 
 * Enum for NT Domain checking
 */
typedef enum _LDAPDomainIdStatus {
	LDAPDomainIdStatus_Unique = 0,
	LDAPDomainIdStatus_Nonunique = -1,
	LDAPDomainIdStatus_NullAttr = -2,
	LDAPDomainIdStatus_NullId = -3
} LDAPDomainIdStatus;

/*
 * Structure used to associate LDAP objectClasses with display templates.
 * These are defined by "template" config. file lines.
 */
typedef struct dsgwtmpl {
    char			*dstmpl_name;
    char			**dstmpl_ocvals;
    struct dsgwtmpl		*dstmpl_next;
} dsgwtmpl;

/*
 * Structures used to keep track of template sets which are used to support
 * more than one way to view an entry.  These are defined by "tmplset"
 * config. file lines.
 */
typedef struct dsgwview {
    char			*dsview_caption;
    char			*dsview_template;
    char			*dsview_jscript;
    struct dsgwview		*dsview_next;
} dsgwview;

typedef struct dsgwtmplset {
    char			*dstset_name;
    dsgwview			*dstset_viewlist;
    int				dstset_viewcount;
    struct dsgwtmplset		*dstset_next;
} dsgwtmplset;

/*
 * Structure used to hold information about Attribute Value Sets that are
 * used with DS_ATTRVAL_SET entry display directives.  These sets are defined
 * by "attrvset" config. file lines.
 */
typedef struct dsgwavset {
    char			*dsavset_handle;
    int				dsavset_itemcount;
    char			**dsavset_values;
    char			**dsavset_prefixes;
    char			**dsavset_suffixes;
    struct dsgwavset		*dsavset_next;
} dsgwavset;

/*
 * Structure used to hold information about file include sets that are used
 * with INCLUDESET directives.  These sets are defined by "includeset" config.
 * file lines.
 */
typedef struct dsgwinclset {
    char			*dsiset_handle;
    int				dsiset_itemcount;
    char			**dsiset_filenames;
    struct dsgwinclset		*dsiset_next;
} dsgwinclset;

/*
 * structure used to track locations where new entries can be added
 * these are created based on the "location" config. file lines
 */
typedef struct dsgwloc {
    char		*dsloc_handle;	    /* short name */
    char		*dsloc_fullname;    /* friendly name */
    char		*dsloc_dnsuffix;    /* new entry location (a full DN) */
} dsgwloc;

/*
 * structure used to track types of new entries that can be added
 * these are created based on the "newtype" config. file lines
 */
typedef struct dsgwnewtype {
    char		*dsnt_template;	   /* name of add-XXX.html template */
    char		*dsnt_fullname;	   /* friendly name */
    char		*dsnt_rdnattr;	   /* attribute used to construct RDN */
    int			*dsnt_locations;   /* indexes into gc_locations array */
    int			dsnt_loccount;	   /* number of dsnt_locations */
    struct dsgwnewtype	*dsnt_next;
} dsgwnewtype;

/*
 * Structure used to hold mapping from LDAP attrs. to VCard properties
 */
typedef struct dsgwvcprop {
    char		*dsgwvcprop_property;	/* VCard property name */
    char		*dsgwvcprop_ldaptype;	/* LDAP attribute type */
    char		*dsgwvcprop_ldaptype2;	/* only used for "n" prop. */
    char		*dsgwvcprop_syntax;	/* cis or mls only please! */
    struct dsgwvcprop	*dsgwvcprop_next;
} dsgwvcprop;

/* substring substitution structure */
typedef struct dsgwsubst {
    char		*dsgwsubst_from;
    char		*dsgwsubst_to;
    char		**dsgwsubst_charsets; /* NULL => any charset */
    struct dsgwsubst	*dsgwsubst_next;
} dsgwsubst;

/* Configuration information structure */
typedef struct dsgwconfig_t {
    int		gc_admserv;		/* non-zero if running under admserv */
    int		gc_enduser;		/* if non-zero, running end-user CGI */
    char	*gc_baseurl;
    char	*gc_ldapserver;
    int		gc_ldapport;
    char	*gc_ldapsearchbase;
    char	*gc_rootdn;
#ifndef DSGW_NO_SSL
    int		gc_ldapssl;		/* if non-zero, do LDAP over SSL */
    char	*gc_securitypath;
#endif
    int		gc_configerr;		/* if non-zero, there were cf errs */
    char	*gc_configdir;		/* path to our config files */
    char	*gc_tmpldir;		/* path to our HTML template files */
    char        *gc_docdir;             /* path to the HTML files*/
    char        *gc_gwnametrans;        /* The nametrans for the gateway (for FT)*/
    char	*gc_urlpfxmain;		/* URL prefix for dsgw main page */
    char	*gc_urlpfxcgi;		/* URL prefix for dsgw CGIs */
    char	*gc_configerrstr;
    char	*gc_localdbconf;	/* NULL if local DB not being used */
					/* otherwise - name of localdb conf */
    char	*gc_binddn;		/* DN to bind as if user info unknown */
    char	*gc_bindpw;		/* passwd to use if user info unknown */
    float	gc_httpversion;		/* client's HTTP version */
    char	*gc_charset;		/* character set used by CGIs & HTML */
    char	*gc_NLS;		/* directory used by libnls */
    char	*gc_ClientLanguage;	/* preferred language list */
    char	*gc_AdminLanguage;	/* administrator language list */
    char	*gc_DefaultLanguage;	/* default language list for either */
    char	**gc_clientIgnoreACharset; /* browsers uses default charset 
					   instead of accept-charsets */
    char	*gc_orgcharturl;        /* http base url for orgchart*/
    char	*gc_orgchartsearchattr; /* Search attribute the orgchart uses*/
    int         gc_aimpresence;         /* enable aim presence*/
    dsgwtmpl	*gc_templates;		/* linked list */
    dsgwnewtype *gc_newentrytypes;	/* linked list */
    dsgwloc	*gc_newentrylocs;	/* array of structures */
    int		gc_newentryloccount;
    dsgwtmplset	*gc_tmplsets;		/* linked list */
    dsgwavset	*gc_avsets;		/* linked list */
    dsgwinclset	*gc_includesets;	/* linked list */
    dsgwvcprop	*gc_vcardproperties;	/* linked list */
    int		gc_httpskeysize;	/* if non-zero, HTTPS is being used */
    int		gc_sslrequired;
    time_t	gc_authlifetime;	/* lifetime of cookies, in seconds */
    int		gc_authrequired;	/* if non-zero, disallow access unless
					   authenticated */
#define DSGW_SSLREQ_NEVER		0
#define DSGW_SSLREQ_WHENAUTHENTICATED	1
#define DSGW_SSLREQ_ALWAYS		2
    dsgwsubst	*gc_changeHTML;		/* linked list */
    dsgwsubst	*gc_l10nsets;		/* linked list */
    /*
     * The following aren't strictly config file options, but are put
     * into the gc struct.
     */
    int		gc_mode;		/* Mode (CGI being executed) */
} dsgwconfig;

/*
 * Structure used to return broken-out ldapdb:// URL info
 */
typedef struct ldapdb_url_desc {
    char	*ludb_path;
    char	*ludb_dn;
} LDAPDBURLDesc;


/* template stuff */
/* The number of templates defined */
#define MAXTEMPLATE 30

/* The maximum number of variables for a given template */
#define MAXVARS 4

/* The structure of a directive is fairly simple.  You have:
 *
 * <!-- NAME var1="val" var2="val" var3="val">
 *
 * You _must_ put the values in quotes.
 */
 
/* The structure of a template.  */
typedef struct template_s {
    char *name;
    char *format;
} *tmpptr;

#define DIRECTIVE_START "<!-- "
#define GCONTEXT_DIRECTIVE "<!-- GCONTEXT -->"
#define DIRECTIVE_END '>'

/* A really big form line */
#define BIG_LINE 1024

/* struct to track saved lines */
typedef struct savedlines {
    int		svl_count;
    int		svl_current;
    char	**svl_line;
} savedlines;


typedef struct dsgwtmplinfo {
    char		*dsti_template;
    int			dsti_type;
#define DSGW_TMPLTYPE_LIST			1
#define DSGW_TMPLTYPE_DISPLAY			2
#define DSGW_TMPLTYPE_EDIT			3
#define DSGW_TMPLTYPE_ADD			4
    unsigned long	dsti_options;
#define DSGW_DISPLAY_OPT_LIST_IF_ONE		0x00000001
#define DSGW_DISPLAY_OPT_AUTH			0x00000002
#define DSGW_DISPLAY_OPT_EDITABLE		0x00000004
#define DSGW_DISPLAY_OPT_ADDING			0x00000008
#define DSGW_DISPLAY_OPT_LINK2EDIT		0x00000010
#define	DSGW_DISPLAY_OPT_DNLIST_JS		0x00000020
#define DSGW_DISPLAY_OPT_CUSTOM_SEARCHDESC	0x00000040
    char		**dsti_attrs;
    unsigned long	*dsti_attrflags;
#define DSGW_DSTI_ATTR_SEEN			0x00000001
    char		**dsti_attrsonly_attrs;
    char		*dsti_sortbyattr;
    int			dsti_entrycount;
    char		*dsti_search2s;
    char		*dsti_search3s;
    char		*dsti_search4s;
    char		*dsti_searcherror;
    char		*dsti_searchlderrtxt;
    LDAP		*dsti_ld;
    LDAPMessage		*dsti_entry;
    LDAPMessage		*dsti_attrsonly_entry;
    char		*dsti_entrydn;
    FILE		*dsti_fp;
    char		**dsti_rdncomps;	/* only set for new entries */
    savedlines		*dsti_preludelines;	/* only output once */
    savedlines		*dsti_entrylines;	/* output once for each entry */
} dsgwtmplinfo;


/*
 * HTML template directives that are specific to DSGW
 * Note that most of these supported only in entrydisplay.c
 */
#define DRCT_DS_ENTRYBEGIN		"DS_ENTRYBEGIN"
#define DRCT_DS_ENTRYEND		"DS_ENTRYEND"
#define DRCT_DS_ATTRIBUTE		"DS_ATTRIBUTE"
#define DRCT_DS_ATTRVAL_SET		"DS_ATTRVAL_SET"
#define DRCT_DS_OBJECTCLASS		"DS_OBJECTCLASS"
#define DRCT_DS_SORTENTRIES		"DS_SORTENTRIES"
#define DRCT_DS_SEARCHDESC		"DS_SEARCHDESC"
#define DRCT_DS_POSTEDVALUE		"DS_POSTEDVALUE"
#define DRCT_DS_EDITBUTTON		"DS_EDITBUTTON"
#define DRCT_DS_DELETEBUTTON		"DS_DELETEBUTTON"
#define DRCT_DS_SAVEBUTTON		"DS_SAVEBUTTON"
#define DRCT_DS_RENAMEBUTTON		"DS_RENAMEBUTTON"
#define DRCT_DS_EDITASBUTTON		"DS_EDITASBUTTON"
#define DRCT_DS_NEWPASSWORD		"DS_NEWPASSWORD"
#define DRCT_DS_CONFIRM_NEWPASSWORD	"DS_CONFIRM_NEWPASSWORD"
#define DRCT_DS_OLDPASSWORD		"DS_OLDPASSWORD"
#define DRCT_DS_HELPBUTTON		"DS_HELPBUTTON"
#define DRCT_DS_CLOSEBUTTON		"DS_CLOSEBUTTON"
#define DRCT_DS_BEGIN_ENTRYFORM		"DS_BEGIN_ENTRYFORM"
#define DRCT_DS_END_ENTRYFORM		"DS_END_ENTRYFORM"
#define	DRCT_DS_EMIT_BASE_HREF		"DS_EMIT_BASE_HREF"
#define	DRCT_DS_DNATTR			"DS_DNATTR"
#define	DRCT_DS_DNDESC			"DS_DNDESC"
#define DRCT_DS_DNEDITBUTTON		"DS_DNEDITBUTTON"
#define DRCT_DS_BEGIN_DNSEARCHFORM	"DS_BEGIN_DNSEARCHFORM"
#define DRCT_DS_END_DNSEARCHFORM	"DS_END_DNSEARCHFORM"
#define	DRCT_DS_CONFIG_INFO		"DS_CONFIG_INFO"
#define DRCT_DS_GATEWAY_VERSION		"DS_GATEWAY_VERSION"
#define DRCT_DS_VIEW_SWITCHER		"DS_VIEW_SWITCHER"
#define DRCT_DS_STD_COMPLETION_JS	"DS_STD_COMPLETION_JS"
#define DRCT_HEAD			"HEAD"
#define DRCT_DS_ALERT_NOENTRIES		"DS_ALERT_NOENTRIES"
#define DRCT_DS_ORGCHARTLINK		"DS_ORGCHARTLINK"

/*
 * directives supported inside dsgw_parse_line() itself (usable anywhere)
 * Note that these are in addition to ones in the htmlparse.c templates array
 */
#define	DRCT_DS_LAST_OP_INFO		"DS_LAST_OP_INFO"

/*
 * directives supported by genscreen
 */
#define DRCT_DS_LOCATIONPOPUP		"DS_LOCATIONPOPUP"

/*
 * these next few are supported by dsconfig
 */
#define DRCT_DS_INLINE_POST_RESULTS	"DS_INLINE_POST_RESULTS"
#define DRCT_DS_CHECKED_IF_LOCAL	"DS_CHECKED_IF_LOCAL"
#define DRCT_DS_CHECKED_IF_REMOTE	"DS_CHECKED_IF_REMOTE"
#define DRCT_DS_HOSTNAME_VALUE		"DS_HOSTNAME_VALUE"
#define DRCT_DS_PORT_VALUE		"DS_PORT_VALUE"
#define DRCT_DS_CHECKED_IF_SSL		"DS_CHECKED_IF_SSL"
#define DRCT_DS_CHECKED_IF_NOSSL	"DS_CHECKED_IF_NOSSL"
#define DRCT_DS_SSL_CONFIG_VALUE	"DS_SSL_CONFIG_VALUE"
#define DRCT_DS_BASEDN_VALUE		"DS_BASEDN_VALUE"
#define DRCT_DS_BINDDN_VALUE		"DS_BINDDN_VALUE"
#define DRCT_DS_BINDPASSWD_VALUE	"DS_BINDPASSWD_VALUE"
#define DRCT_DS_NOCERTFILE_WARNING	"DS_NOCERTFILE_WARNING"

/*
 * directives supported by dsimpldif
 */
#define DS_LDIF_FILE                    "DS_LDIF_FILE"
#define DS_CHECKED_IF_ERASE             "DS_CHECKED_IF_ERASE"
#define DS_CHECKED_IF_NOTERASE          "DS_CHECKED_IF_NOTERASE"
#define DS_CHECKED_IF_STOP              "DS_CHECKED_IF_STOP"
#define DS_CHECKED_IF_NOTSTOP           "DS_CHECKED_IF_NOTSTOP"

#define DSGW_ARG_BUTTON_LABEL		"label"
#define DSGW_ARG_BUTTON_NAME		"name"

/*
 * directives supported by dsexpldif
 */
#define DS_SUFFIX                       "DS_SUFFIX"

/* conditionals -- replaces "xxx" in  <!-- IF xxx --> directives */
#define DSGW_COND_FOUNDENTRIES		"FoundEntries"
#define DSGW_COND_ADDING		"Adding"
#define DSGW_COND_EDITING		"Editing"
#define DSGW_COND_DISPLAYING		"Displaying"
#define DSGW_COND_BOUND			"Bound"
#define DSGW_COND_BOUNDASTHISENTRY	"BoundAsThisEntry"
#define	DSGW_COND_ADMSERV		"AdminServer"
#define	DSGW_COND_LOCALDB		"DirectoryIsLocalDB"
#define	DSGW_COND_ATTRHASVALUES		"AttributeHasValues"
#define	DSGW_COND_ATTRHASTHISVALUE	"AttributeHasThisValue"
#define	DSGW_COND_POSTEDFORMVALUE	"PostedFormValue"
#define	DSGW_COND_DISPLAYORGCHART	"DisplayOrgChart"
#define	DSGW_COND_DISPLAYAIMPRESENCE    "DisplayAimPresence"

/* global variables */
extern char *progname;		/* set in dsgwutil.c:dsgw_init() */
extern char *dsgw_last_op_info;	/* set in edit.c and genscreen.c */
extern char *dsgw_dnattr;	/* set in edit.c */
extern char *dsgw_dndesc;	/* set in edit.c */
extern int http_hdr_sent;	/* set in dsgwutil.c:dsgw_send_header() */
extern char *dsgw_html_body_colors;	/* set in htmlparse.c */
extern int dsgw_NSSInitializedAlready; /* set in cookie.c:dsgw_NSSInit */

/* function prototypes */
/*
 * in cgiutil.c
 */
int dsgw_post_begin( FILE *in );
void dsgw_form_unescape( char *str );
char *dsgw_get_cgi_var( char *varname, int required );
int dsgw_get_int_var( char *varname, int required, int defval );
int dsgw_get_boolean_var( char *varname, int required, int defval );
char *dsgw_get_escaped_cgi_var( char *varname_escaped, char *varname,
	int required );
#define DSGW_CGIVAR_OPTIONAL	0
#define DSGW_CGIVAR_REQUIRED	1
char *dsgw_next_cgi_var( int *indexp, char **valuep );

/*
 * in dsgwutil.c:
 */
extern dsgwconfig *gc;
int dsgw_init( int argc, char **argv, int methods_handled );
int dsgw_simple_cond_is_true( int argc, char **argv, void *arg );
char *dsgw_file2path( char *prefix, char *filename );
char *dsgw_file2htmlpath( char *prefix, char *filename );
void *dsgw_ch_malloc( size_t n );
void *dsgw_ch_calloc( size_t nelem, size_t elsize );
void *dsgw_ch_realloc( void *p, size_t n );
char *dsgw_ch_strdup( const char *s );
char *dsgw_escape_quotes( char *in );
char *dsgw_get_translation( char *in );
void dsgw_send_header();
void dsgw_add_header( char *line );
char *dsgw_get_auth_cookie();
void dsgw_emit_helpbutton( char *topic );
void dsgw_emit_homebutton();
char *dsgw_build_urlprefix();
void dsgw_init_searchprefs( struct ldap_searchobj **solistp );
void dsgw_addtemplate( dsgwtmpl **tlpp, char *template, int count,
	char **ocvals );
dsgwtmpl *dsgw_oc2template( char **ocvals ); 
void dsgw_remove_leading_and_trailing_spaces( char **sp );
int dsgw_parse_cookie( char *cookie, char **rndstr, char **dn );
char *dsgw_getvp( int cginum );
#ifdef DSGW_DEBUG
void dsgw_log( char *fmt, ... );
void dsgw_logstringarray( char *arrayname, char **strs );
void dsgw_log_out (const char* s, size_t n);
#else
#define dsgw_log_out(s,n) ;
#endif /* DSGW_DEBUG */
void dsgw_head_begin();
void dsgw_quote_emptyFrame();
void dsgw_password_expired_alert( char *binddn );
time_t dsgw_current_time();
time_t dsgw_time_plus_sec (time_t l, long r);

/*
 * in entrydisplay.c
 */
dsgwtmplinfo *dsgw_display_init( int tmpltype, char *template,
	unsigned long options );
void dsgw_display_entry( dsgwtmplinfo *tip, LDAP *ld, LDAPMessage *entry,
	LDAPMessage *attrsonly_entry, char *dn );
void dsgw_display_done( dsgwtmplinfo *tip );
char *dsgw_mls_convertlines( char *val, char *sep, int *linesp, int emitlines,
	int quote_html_specials );
void dsgw_set_searchdesc( dsgwtmplinfo *tip, char*, char*, char*);
void dsgw_set_search_result( dsgwtmplinfo *tip, int entrycount,
	char *searcherror, char *lderrtxt );

/*
 * in error.c
 */
void dsgw_error( int errcode, char *extra, int options, int lderr,
	char *lderrtxt );
#define DSGW_ERROPT_EXIT	0x01
#define DSGW_ERROPT_IGNORE	0x02
#define DSGW_ERROPT_TERSE	0x04
#define DSGW_ERROPT_INLINE	0x08
#define DSGW_ERROPT_DURINGBIND	0x10
int dsgw_dn2passwd_error( int ckrc, int skipauthwarning );
char* dsgw_err2string( int err );
char *dsgw_ldaperr2string( int lderr );

/*
 * in htmlout.c
 */
void dsgw_html_begin( char *title, int titleinbody );
void dsgw_html_end( void );
void dsgw_html_href( char *urlprefix, char *url, char *label, char *value,
	char *extra );
void dsgw_strcat_escaped( char *s1, const char *s2 );
char *dsgw_strdup_escaped( const char *s );
void dsgw_substitute_and_output( char *s, char *tag, char *value, int escape );
void dsgw_form_begin( const char* name, const char* format, ... );
char *dsgw_strdup_with_entities( char *s, int *madecopyp );
void dsgw_HTML_emits( char * );
void dsgw_emit_cgi_var( int argc, char **argv );
void dsgw_emit_button( int argc, char **argv, const char* format, ... );
void dsgw_emit_alertForm();
void dsgw_emit_alert( const char* frame, const char* windowOptions, const char* fmt, ... );
void dsgw_emit_confirmForm();
void dsgw_emit_confirm( const char* frame, const char* yes, const char* no,
		        const char* windowOptions, int enquote, const char* fmt, ... );

/*
 * in htmlparse.c:
 */
typedef int (*condfunc)( int argc, char **argv, void *arg );
int dsgw_parse_line( char *line_input, int *argc, char ***argv, int parseonly,
	condfunc conditionalfn, void *condarg );
char *get_arg_by_name( char *name, int argc, char **argv );
int dsgw_get_arg_pos_by_name( char *name, int argc, char **argv );
FILE *dsgw_open_html_file( char *filename, int erropts );
int dsgw_next_html_line(FILE *f, char *line);
void dsgw_argv_free( char **argv );
savedlines *dsgw_savelines_alloc( void );
void dsgw_savelines_free( savedlines *svlp );
void dsgw_savelines_save( savedlines *svlp, char *line );
void dsgw_savelines_rewind( savedlines *svlp );
char *dsgw_savelines_next( savedlines *svlp );
int dsgw_directive_is(char *target, char *directive);

/*
 * in ldaputil.c
 */
int dsgw_init_ldap( LDAP **ldp, LDAPFiltDesc **lfdpp, int skipac, int skipauthwarning );
int dsgw_get_adm_identity( LDAP *ld, char **uidp, char **dnp, char **pwdp,
	int erropts );
void dsgw_ldap_error( LDAP *ld, int erropts );
struct ldap_searchobj *dsgw_type2searchobj( struct ldap_searchobj *solistp,
	char *type );
struct ldap_searchattr *dsgw_label2searchattr( struct ldap_searchobj *sop,
	char *label );
struct ldap_searchmatch *dsgw_prompt2searchmatch( struct ldap_searchobj *sop,
	char *prompt );
void dsgw_smart_search( LDAP *ld, struct ldap_searchobj *sop,
	LDAPFiltDesc *lfdp, char *base, char *value, unsigned long options );
void dsgw_pattern_search( LDAP *ld, char *listtmpl,
        char *searchdesc2, char *searchdesc3, char *searchdesc4,
        char *filtpattern, char *filtprefix, char *filtsuffix, char *attr,
        char *base, int scope, char *value, unsigned long options );
void dsgw_ldapurl_search( LDAP *ld, char *ldapurl );
void dsgw_read_entry( LDAP *ld, char *dn, char **ocvals, char *tmplname,
	char **attrs, unsigned long options );
int dsgw_ldap_entry_exists( LDAP *ld, char *dn, char **matchedp,
	unsigned long erropts );
char **dsgw_rdn_values( char *dn );
char *dsgw_get_binddn( void );
int dsgw_bound_as_dn( char *dn, int def_answer );
int dsgw_dn_cmp( char *dn1, char *dn2 );
int dsgw_is_dnparent( char *dn1, char *dn2 );
char *dsgw_dn_parent( char *dn );
void dsgw_emit_location_popup( LDAP *ld, int argc, char **argv, int erropts );

/*
 * in config.c
 */
dsgwconfig *dsgw_read_config();
int dsgw_update_dbswitch( dsgwconfig *cfgp, char *handle, int erropts );
int dsgw_valid_docname(char *filename);
char *dsgw_get_docdir(void) ;

typedef struct scriptrange {
    unsigned long sr_min;
    unsigned long sr_max;
    struct scriptrange* sr_next;
} scriptrange_t;

typedef struct scriptorder {
    unsigned so_caseIgnoreAccents;
    scriptrange_t** so_sort;
    scriptrange_t** so_display;
} scriptorder_t;

scriptorder_t* dsgw_scriptorder();


/*
 * in cookie.c
 */
char *dsgw_mkcookie();
int dsgw_ckdn2passwd( char *cookie, char *dn, char **ret_pw );
int dsgw_storecookie( char *cookie, char *dn, char *password, time_t expires );
void dsgw_traverse_db();
char *dsgw_t2gmts( time_t cktime );
int dsgw_delcookie( char *cookie );
void dsgw_closecookiedb( FILE *fp );
FILE *dsgw_opencookiedb();
time_t dsgw_getlastpurged( FILE *fp );
int dsgw_purgedatabase( char *dn );

/*
 * in emitauth.c
 */
void dsgw_emit_auth_form( char *binddn );
void dsgw_emit_auth_dest( char *binddn, char* authdesturl );

/*
 * in emitf.c
 */
int dsgw_emits (const char* s); /* like fputs(s, stdout) */
int dsgw_emitf (const char* format, ...); /* like printf */
int dsgw_emitfv (const char* format, va_list argl);
char* dsgw_emit_converts_to (char* charset);
int is_UTF_8 (const char* charset);
void*  dsgw_emitn (void*, const char* buf, size_t len);
size_t dsgw_fputn (FILE*, const char* buf, size_t len);

#define QUOTATION_JAVASCRIPT 2
#define QUOTATION_JAVASCRIPT_MULTILINE 3
void dsgw_quotation_begin (int kind);
void dsgw_quotation_end();
int dsgw_quote_emits (int kind, const char* s);
int dsgw_quote_emitf (int kind, const char* format, ...);

/*
 * in collate.c
 */
#define CASE_EXACT 0
#define CASE_INSENSITIVE 1

typedef int (*strcmp_t) (const char*, const char*);
strcmp_t dsgw_strcmp (int);

typedef int (*valcmp_t) (const char**, const char**);
valcmp_t dsgw_valcmp (int);

extern struct berval* dsgw_strkeygen (int, const char*);
extern struct berval* dsgw_key_first;
extern struct berval* dsgw_key_last;

int  LDAP_C LDAP_CALLBACK dsgw_keycmp (void*, const struct berval*, const struct berval*);
void LDAP_C LDAP_CALLBACK dsgw_keyfree(void*, const struct berval*);

/*
 * in vcard.c
 */
void dsgw_vcard_from_entry( LDAP *ld, char *dn, char *mimetype );

/*
 * utf8compare.c
 */
int dsgw_utf8casecmp(unsigned char *s0, unsigned char *s1);
int dsgw_utf8ncasecmp(unsigned char *s0, unsigned char *s1, int n);

/*
 * dsgwutil.c
 */
/******************** Accept Language List ************************/\
#if 0 /* defined in i18n.h */

#define MAX_ACCEPT_LANGUAGE 16
#define MAX_ACCEPT_LENGTH 18
typedef char ACCEPT_LANGUAGE_LIST[MAX_ACCEPT_LANGUAGE][MAX_ACCEPT_LENGTH];
#endif /* MAX_ACCEPT_LANGUAGE */

/* AcceptLangList
 *
 * Will parse an Accept-Language string of the form 
 * "en;q=1.0,fr;q=0.9..."
 * The ACCEPT_LANGUAGE_LIST array will be loaded with the ordered
 * language elements based on the priority of the languages specified.
 * The number of languages will be returned as the result of the 
 * call.
 */
size_t
AcceptLangList(
    const char * acceptLanguage,
    ACCEPT_LANGUAGE_LIST acceptLanguageList
);

/*
 * converts a buffer of characters to/from UTF8 from/to a native charset
 * the given converter will handle the native charset
 * returns 0 if not all of source was converted, 1 if all of source
 * was converted, -1 upon error
 * all of source will be converted if there is enough room in dest to contain
 * the entire conversion, or if dest is null and we are malloc'ing space for dest
 */
int
dsgw_convert(
    int direction, /* DSGW_TO_UTF8 or DSGW_FROM_UTF8 */
    UConverter *nativeConv, /* convert from/to native charset */
    char **dest, /* *dest is the destination buffer - if *dest == NULL, it will be malloced */
    size_t destSize, /* size of dest buffer (ignored if *dest == NULL) */
    size_t *nDest, /* number of chars written to dest */
    const char *source, /* source buffer to convert - either in native encoding (to) or utf8 (from) */
    size_t sourceSize, /* size of source buffer - if 0, assume source is NULL terminated */
    size_t *nSource, /* number of chars read from source buffer */
    UErrorCode *pErrorCode /* will be reset each time through */
);
#define DSGW_TO_UTF8 0
#define DSGW_FROM_UTF8 1

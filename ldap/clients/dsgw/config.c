/**
 * PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
 * license terms. Copyright © 2001 Sun Microsystems, Inc.
 * Some preexisting portions Copyright © 2001 Netscape Communications Corp.
 * All rights reserved.
 */
/*
 * config.c -- parse config file for directory server gateway
 *
 * Copyright (c) 1996 Netscape Communications Corp.
 * All rights reserved.
 */


#include <limits.h> /* ULONG_MAX */
#include <stdio.h>
#include <stdlib.h> /* strtoul */
#include <string.h>
#if !defined( XP_WIN32 )
#include <sys/param.h>
#endif

#include "dsgw.h"
#include "dbtdsgw.h"
#include "../../include/portable.h"
/* MLM - Include netsite.h to get ADMSERV_VERSION_STRING */
#ifdef	AIX
#undef	HAVE_TIME_R
#endif
#include "netsite.h"
#include "ldaputil/errors.h"
#include "ldaputil/ldaputil.h"
#include "ldaputil/dbconf.h"

extern char *get_userdb_dir(void);	/* Can't include libadmin.h, so this */
static void report_ldapu_error( int ldapu_err, int dsgw_err, int erropts );
static void adderr( dsgwconfig *gc, char *str, char *filename, int lineno );
static void fp_parse_line( char	*line, int *argcp, char	**argv );
static void fp_getline_init( int *lineno );
static char *fp_getline( FILE *fp, int *lineno );
static void add_location( int *loccountp, dsgwloc **locarrayp,
	char *locsuffix, char **argv );
static int add_newtype( dsgwnewtype **newentlistp, int loccount,
	dsgwloc *locarray, int argc, char **argv );
static void add_tmplset( dsgwtmplset **tslp, int argc, char **argv );
static void add_vcardproperty( dsgwvcprop **vcpropp, int argc, char **argv );
static void add_avset( dsgwavset **avsp, char **argv );
static void add_includeset( dsgwinclset **isp, char **argv );
static void add_l10nset( dsgwsubst **l10np, char **argv );
static void read_dsgwconfig( char *filename, char *locsuffix,
	int templatesonly, int binddnfile );
static void get_dbconf_properties( char *filename );
static int write_dbswitch_info( FILE *fp, dsgwconfig *cfgp, char *dbhandle );
static int ldapdb_url_parse( char *url, LDAPDBURLDesc **ldbudpp );
static int dsgw_valid_context();
static int browser_is_msie40();
static int browser_ignores_acceptcharset();
static char *dsgw_ch_strdup_tolower( const char *s );
static void set_dsgwcharset();
#ifdef XP_WIN32
static void dsgw_unix2dospath( char *path );
#endif


#define	MAXARGS	100
/*
 * Open and parse the dsgw config file.  If an error occurs, this function
 * does not return.
 */
dsgwconfig *
dsgw_read_config()
{
    char	*scriptname;
    char	*p, *fname;
    int         servurllen = 0;
    int		len;
    char	*path;

    /* get rid of stupid warning: */
    if (ldapu_strings != NULL);

    /* 
     * First, make sure that the context is valid. Don't want anything
     * tricky in there like dots or slashes.
     */
    if (!dsgw_valid_context ()) {
	dsgw_error( DSGW_ERR_BADFILEPATH, context, 
		    DSGW_ERROPT_EXIT, 0, NULL );
    }

    /* gc is a global */
    if (( gc = (dsgwconfig *) dsgw_ch_malloc( sizeof( dsgwconfig ))) == NULL ) {
	dsgw_error( DSGW_ERR_NOMEMORY,
		XP_GetClientStr(DBT_initializingConfigInfo_),
		DSGW_ERROPT_EXIT, 0, NULL );
    }
    memset( gc, 0, sizeof( dsgwconfig ));

    /*
     * set non-zero configuration defaults
     */
    gc->gc_ldapport = LDAP_PORT;
    gc->gc_configerrstr = dsgw_ch_strdup( "" );
    gc->gc_sslrequired = DSGW_SSLREQ_NEVER;
    gc->gc_authlifetime = DSGW_DEF_AUTH_LIFETIME;
    gc->gc_configdir = DSGW_CONFIGDIR_HTTP;	/* may be overridden below */
    gc->gc_docdir = DSGW_DOCDIR_HTTP;
    gc->gc_tmpldir = DSGW_TMPLDIR_HTTP;		/* may be overridden below */
    gc->gc_urlpfxmain = DSGW_URLPREFIX_MAIN_HTTP; /* may be overridden below */
    /*gc->gc_urlpfxcgi = DSGW_URLPREFIX_CGI_HTTP;*/
    gc->gc_urlpfxcgi = DSGW_URLPREFIX_BIN; /* may be overridden below */
    gc->gc_binddn = gc->gc_bindpw = "";
    gc->gc_charset = NULL; /* implicitly ISO-8859-1 */
    gc->gc_ClientLanguage = "";
    gc->gc_AdminLanguage = "";
    gc->gc_DefaultLanguage = "";
    gc->gc_httpversion = 0;
    gc->gc_orgchartsearchattr = "uid";
    /*
     * Figure out whether we are running under the admin server or not.  This
     * also determines where our config and html files are.  The hackage is:
     * if we're running under the admin server:
     *   configdir is  ../../../../admin-serv/config
     *   htmldir is ../html
     *   urlpfxmain is ""
     *   urlpfxcgi is ""
     *   dbswitchfile is NSHOME/userdb/dbswitch.conf
     * 
     * If we're running under any other HTTP server:
     *   configdir is ../config
     *   htmldir is ../config  (yes, that's right)
     *   urlpfxmain is "lang?context=dsgw&file="
     *   gc_urlpfxcgi is "/ds"
     *   dbswitchfile is not used
     */

    /* Get the admin server name and chop off the version number */
    /* vs = dsgw_ch_strdup( ADMSERV_VERSION_STRING );
    if (( p = strchr( vs, '/')) != NULL ) {
	*p = '\0';
    }*/

    /*ss = getenv( "SERVER_SOFTWARE" );
    if ( ss != NULL ) {
	if ( !strncasecmp( vs, ss, strlen( vs ))) {
	    char *server_names;*/
	    /* We're running under the admin server */
    /* gc->gc_admserv = 1;
	    gc->gc_configdir = DSGW_CONFIGDIR_ADMSERV;
	    gc->gc_tmpldir = DSGW_TMPLDIR_ADMSERV;
	    gc->gc_urlpfxmain = DSGW_URLPREFIX_MAIN_ADMSERV;
	    gc->gc_urlpfxcgi = DSGW_URLPREFIX_CGI_ADMSERV;*/
	    /* Check if running an end-user CGI under the admin server */
    /*	    if (( server_names = getenv( "SERVER_NAMES" )) != NULL &&
		    strlen( server_names ) >= 4 &&
		    strncmp( server_names, "user", 4 ) == 0 ) {
		gc->gc_enduser = 1;
	    }
	}
    }*/
    
    /* 
     * Get the strlen of the http://admin/port because getvp returns
     * that in the url, so we can't compare scriptname against what 
     * getvp returns. We need to skip past the server url part.
     */
    servurllen = strlen(getenv("SERVER_URL"));

    /* Set mode (based on which CGI is currently running) */
    if (( scriptname = getenv( "SCRIPT_NAME" )) == NULL ) {
	gc->gc_mode = 0;
    } else {
	if ( !strncmp( scriptname, dsgw_getvp( DSGW_CGINUM_DOSEARCH ) + servurllen,
		strlen( scriptname ))) {
	    gc->gc_mode = DSGW_MODE_DOSEARCH;
	} else if ( !strncmp( scriptname, dsgw_getvp( DSGW_CGINUM_BROWSE ) + servurllen,
		strlen( scriptname ))) {
	    gc->gc_mode = DSGW_MODE_BROWSE;
	} else if ( !strncmp( scriptname, dsgw_getvp( DSGW_CGINUM_SEARCH ) + servurllen,
		strlen( scriptname ))) {
	    gc->gc_mode = DSGW_MODE_SEARCH;
	} else if ( !strncmp( scriptname, dsgw_getvp( DSGW_CGINUM_CSEARCH )+ servurllen,
		strlen( scriptname ))) {
	    gc->gc_mode = DSGW_MODE_CSEARCH;
	} else if ( !strncmp( scriptname, dsgw_getvp( DSGW_CGINUM_AUTH )+ servurllen,
		strlen( scriptname ))) {
	    gc->gc_mode = DSGW_MODE_AUTH;
	} else if ( !strncmp( scriptname, dsgw_getvp( DSGW_CGINUM_EDIT )+ servurllen,
		strlen( scriptname ))) {
	    gc->gc_mode = DSGW_MODE_EDIT;
	} else if ( !strncmp( scriptname, dsgw_getvp( DSGW_CGINUM_DOMODIFY )+ servurllen,
		strlen( scriptname ))) {
	    gc->gc_mode = DSGW_MODE_DOMODIFY;
	} else {
	    gc->gc_mode = DSGW_MODE_UNKNOWN;
	}
    }

    if (( p = getenv( "SERVER_PROTOCOL" )) != NULL ) {
	char *pp;

	pp = strchr(p, '/');
	if (pp != NULL) {
	    gc->gc_httpversion = (float)atof(++pp);
	}
    }

    if (( p = getenv( "DefaultLanguage" )) != NULL ) {
	gc->gc_DefaultLanguage = p;
    }

    if (( p = getenv( "AdminLanguage" )) != NULL ) {
	gc->gc_AdminLanguage = p;
    }

    if (( p = getenv( "ClientLanguage" )) != NULL ) {
	gc->gc_ClientLanguage = p;
    }

    /* Accept-Language from user overrides ClientLanguage from environment */
    if (( p = getenv( "HTTP_ACCEPT_LANGUAGE" )) != NULL ) {
	gc->gc_ClientLanguage = p;
    }

    /* Set rest of config. by reading the appropriate config files */
    path = dsgw_ch_malloc( MAXPATHLEN );
    if ( gc->gc_admserv ) {
	PR_snprintf( path, MAXPATHLEN, "%s/dbswitch.conf", get_userdb_dir());
	get_dbconf_properties( path );
    }

    /* 
     * If there is no config file name (context), then use
     * DSGW_CONFIGFILE in the config directory
     */
    if (context == NULL) {
	PR_snprintf( path, MAXPATHLEN, "%s$$LANGDIR/%s",
		DSGW_CONFIGDIR_HTTP, DSGW_CONFIGFILE);
        len = strlen( DSGW_CONFIGDIR_HTTP ) + strlen( DSGW_CONFIGFILE ) + 32;
    } else {
	PR_snprintf( path, MAXPATHLEN, "%s$$LANGDIR/%s.conf",
		DSGW_CONTEXTDIR_HTTP, context);
	/* increased the length from 11 -- fix for auth crash on AIX */
        len = strlen( DSGW_CONTEXTDIR_HTTP ) + strlen( context ) + 32;
    }
    /* allocate buffers with enough extra room to fit "$$LANGDIR/" */
    if ( NULL != gc->gc_ClientLanguage ) {
        len += strlen( gc->gc_ClientLanguage );
    }
    fname = dsgw_ch_malloc( len+MAXPATHLEN );
    if ( GetFileForLanguage( path, gc->gc_ClientLanguage, fname ) < 0 ) {
        if (context == NULL) {
	    PR_snprintf( fname, len+MAXPATHLEN, "%s%s", DSGW_CONFIGDIR_HTTP,
		DSGW_CONFIGFILE);
        } else {
	    PR_snprintf( fname, len+MAXPATHLEN, "%s%s.conf",
		DSGW_CONTEXTDIR_HTTP, context);
	}
    }
    free( path );

    if (context != NULL) {
	char urlpfx[MAXPATHLEN];
	/*set the urlpfxmain to be "lang?context=CONTEXT&file="*/
	/*sprintf(urlpfx, "%slang?context=%s&file=", DSGW_URLPREFIX_CGI_HTTP, context);*/
	PR_snprintf(urlpfx, MAXPATHLEN, "%s?context=%s&file=", dsgw_getvp(DSGW_CGINUM_LANG), context);
	gc->gc_urlpfxmain = dsgw_ch_strdup( urlpfx );
    }

    read_dsgwconfig( fname, NULL, gc->gc_admserv, 0 );
    free( fname );

#if 0
    /* if necessary, try to set path to certificate database */
#ifndef DSGW_NO_SSL
    if ( gc->gc_ldapssl && gc->gc_securitypath == NULL ) {
	if ( gc->gc_admserv ) {
	    if (( p = get_nsadm_var( "CertFile" )) != NULL ) {
		gc->gc_securitypath = dsgw_ch_malloc( strlen( p ) + 4 );
		sprintf( gc->gc_securitypath, "%s.db", p );
	    }
	} else {
	    gc->gc_securitypath = DSGW_DEFSECURITYPATH;
	}
    }
#endif
#endif

    if ( browser_ignores_acceptcharset() ) {
	set_dsgwcharset();
    } else {
        /* Accept-Charset from user overrides charset from configuration */
        if (( p = getenv( "HTTP_ACCEPT_CHARSET" )) != NULL ) {
	    gc->gc_charset = p;
	    /* IE 4.0 doesn't send HTTP_ACCEPT_CHARSET, so we test for it specially -RJP */
        } else if (browser_is_msie40() ) {
	    gc->gc_charset = MSIE40_DEFAULT_CHARSET;
        } else { /* charset file overrides charset from configuration */
	    set_dsgwcharset();
        }
    }

    return( gc );
}


static void
report_ldapu_error( int ldapu_err, int dsgw_err, int erropts )
{
    char *extra = "";

    switch( ldapu_err ) {
    case LDAPU_ERR_CANNOT_OPEN_FILE:
	extra = XP_GetClientStr(DBT_cannotOpenFile_); 
	break;
    case LDAPU_ERR_DBNAME_IS_MISSING:
    case LDAPU_ERR_NOT_PROPVAL:
	extra = XP_GetClientStr(DBT_malformedDbconfFile_);
	break;
    case LDAPU_ERR_PROP_IS_MISSING:
	extra = XP_GetClientStr(DBT_missingPropertyNameInDbconfFile_);
	break;
    case LDAPU_ERR_OUT_OF_MEMORY:
	extra = XP_GetClientStr(DBT_outOfMemory_1);
	break;
    case LDAPU_ERR_DIRECTIVE_IS_MISSING:
	extra = XP_GetClientStr(DBT_missingDirectiveInDbconfFile_);
	break;
    }

    dsgw_error( dsgw_err, extra, erropts, 0, NULL );
}


/*
 * Read the gateway config file (dsgw.conf).
 */
static void
read_dsgwconfig( char *filename, char *locsuffix, int templatesonly, int binddnfile )
{
    char	buf[ MAXPATHLEN + 100 ];
    int		cargc;
    char	*cargv[ MAXARGS ];
    FILE	*fp;
    char	*line;
    int		lineno;
    int		rc;
    LDAPURLDesc	*ludp;

    if (( fp = fopen( filename, "r" )) == NULL ) {
	if ( strstr( filename, "dsgw-l10n.conf" ) != NULL ) {
	    return;	/* ignore if it's dsgw-l10n.conf */
	}
	PR_snprintf( buf, MAXPATHLEN + 100,
		XP_GetClientStr(DBT_cannotOpenConfigFileSN_), filename );
	dsgw_error( DSGW_ERR_BADCONFIG, buf, DSGW_ERROPT_EXIT, 0, NULL );
    }
    fp_getline_init( &lineno );

    while ( (line = fp_getline( fp, &lineno )) != NULL ) {
	/* skip comments and blank lines */
	if ( line[0] == '#' || line[0] == '\0' ) {
	    continue;
	}

	fp_parse_line( line, &cargc, cargv );

	if ( cargc < 1 ) {
	    continue;
	}

	if ( strcasecmp( cargv[0], "requireauth" ) == 0 ) {
	    if ( templatesonly ) continue;
	    gc->gc_authrequired = 1;
	}

	if ( strcasecmp( cargv[0], "authlifetime" ) == 0 ) {
	    if ( templatesonly ) continue;
	    if ( cargc < 2 ) {
		adderr( gc, XP_GetClientStr(DBT_missingArgumentForAuthlifetimeDi_),
			filename, lineno );
		continue;
	    }
	    gc->gc_authlifetime = (time_t) atol( cargv[ 1 ]);
	} else if ( strcasecmp( cargv[ 0 ], "changeHTML" ) == 0 ) {
	    auto dsgwsubst *sub;
	    if ( templatesonly ) continue;
	    if ( cargc < 2 || cargv[ 1 ][ 0 ] == '\0') continue;
	    sub = (dsgwsubst *)dsgw_ch_malloc( sizeof( dsgwsubst ));
	    memset( sub, 0, sizeof( dsgwsubst ));
	    sub->dsgwsubst_from = dsgw_ch_strdup( cargv[ 1 ] );
	    if ( cargc > 2 ) {
		sub->dsgwsubst_to = dsgw_ch_strdup( cargv[ 2 ] );
		if ( cargc > 3 ) {
		    auto size_t i;
		    sub->dsgwsubst_charsets = (char **)dsgw_ch_malloc
		      (sizeof(char*) * (cargc - 2));
		    for (i = 3; i < cargc; ++i) {
			sub->dsgwsubst_charsets[ i-3 ] = dsgw_ch_strdup( cargv[ i ] );
		    }
		    sub->dsgwsubst_charsets[ i-3 ] = NULL;
		}
	    }
	    { /* append sub to gc->gc_changeHTML: */
		auto dsgwsubst **s = &(gc->gc_changeHTML);
		while (*s) s = &((*s)->dsgwsubst_next);
		*s = sub;
	    }

	} else if ( strcasecmp( cargv[0], "dirmgr" ) == 0 ) {
	    if ( templatesonly ) continue;
	    if ( cargc < 2 ) {
		adderr( gc, XP_GetClientStr(DBT_missingArgumentForDirmgrDirectiv_),
			filename, lineno );
		continue;
	    }
	    gc->gc_rootdn = dsgw_ch_strdup( cargv[ 1 ]);
	} else if ( strcasecmp( cargv[0], "url-orgchart-base" ) == 0 ) {
	    if ( templatesonly ) continue;
	    if ( cargc < 2 ) {
		adderr( gc, XP_GetClientStr(DBT_missingArgumentForOrgChartURLDirectiv_),
			filename, lineno );
		continue;
	    }
	    gc->gc_orgcharturl = dsgw_ch_strdup( cargv[ 1 ]);
	} else if ( strcasecmp( cargv[0], "orgchart-attrib-farleft-rdn" ) == 0 ) {
	    if ( templatesonly ) continue;
	    if ( cargc < 2 ) {
		adderr( gc, XP_GetClientStr(DBT_missingArgumentForOrgChartSearchAttr_),
			filename, lineno );
		continue;
	    }
	    gc->gc_orgchartsearchattr = dsgw_ch_strdup( cargv[ 1 ]);
	} else if ( strcasecmp( cargv[0], "enable-aim-presence" ) == 0 ) {
	    if ( templatesonly ) continue;
	    if (cargc < 2 || strcasecmp(cargv[1], "true") == 0) {
	      gc->gc_aimpresence = 1;
	    } else {
	      gc->gc_aimpresence = 0;
	    }
	} else if ( strcasecmp( cargv[0], "baseurl" ) == 0 ) {
	    if ( templatesonly ) continue;
	    if ( cargc < 2 ) {
		adderr( gc, XP_GetClientStr(DBT_missingArgumentForBaseurlDirecti_),
			filename, lineno );
		continue;
	    }
	    gc->gc_baseurl = dsgw_ch_strdup( cargv[ 1 ]);
	    if (( rc = ldap_url_parse( gc->gc_baseurl, &ludp )) != 0 ) {
		switch ( rc ) {
		case LDAP_URL_ERR_NODN:
		    adderr( gc, XP_GetClientStr(DBT_badUrlProvidedForBaseurlDirectiv_), filename, lineno );
		    break;
		case LDAP_URL_ERR_MEM:
		    dsgw_error( DSGW_ERR_NOMEMORY,
			    XP_GetClientStr(DBT_parsingBaseurlDirective_),
			    DSGW_ERROPT_EXIT, 0, NULL );
		    break;
		case LDAP_URL_ERR_NOTLDAP:
		    adderr( gc, XP_GetClientStr(DBT_badUrlProvidedForBaseurlDirectiv_1), filename, lineno );
		    break;
		}
	    } else {
		gc->gc_ldapserver = ludp->lud_host;
		gc->gc_ldapport = ludp->lud_port;
		if ( ludp->lud_dn == NULL ) {
		    gc->gc_ldapsearchbase = dsgw_ch_strdup( "" );
		} else {
		    gc->gc_ldapsearchbase = ludp->lud_dn;
		}
		if (( ludp->lud_options & LDAP_URL_OPT_SECURE ) != 0 ) {
#ifdef DSGW_NO_SSL
		    adderr( gc, XP_GetClientStr(DBT_LdapsUrlsAreNotYetSupportedN_),
			    filename, lineno );
#else
		    gc->gc_ldapssl = 1;
#endif
		}
	    }

	} else if ( strcasecmp( cargv[0], "template" ) == 0 ) {
	    if ( cargc < 3 ) {
		adderr( gc, XP_GetClientStr(DBT_missingArgumentsForTemplateDirec_),
			filename, lineno );
		continue;
	    }
	    dsgw_addtemplate( &gc->gc_templates, cargv[1], cargc - 2,
		    &cargv[2] );

#ifndef DSGW_NO_SSL
	} else if ( strcasecmp( cargv[0], "sslrequired" ) == 0 ) {
	    if ( templatesonly ) continue;
	    if ( cargc < 2 ) {
		adderr( gc, XP_GetClientStr(DBT_missingArgumentForSslrequiredDir_),
			filename, lineno );
		continue;
	    }
	    if ( strcasecmp( cargv[1], "never" ) == 0 ) {
		gc->gc_sslrequired = DSGW_SSLREQ_NEVER;
	    } else if ( strcasecmp( cargv[1], "whenauthenticated" ) == 0 ) {
		gc->gc_sslrequired = DSGW_SSLREQ_WHENAUTHENTICATED;
	    } else if ( strcasecmp( cargv[1], "always" ) == 0 ) {
		gc->gc_sslrequired = DSGW_SSLREQ_ALWAYS;
	    } else {
		adderr( gc, XP_GetClientStr(DBT_unknownArgumentToSslrequiredDire_), filename, lineno );
	    }

	} else if ( strcasecmp( cargv[0], "securitypath" ) == 0 ) {
	    if ( templatesonly ) continue;
	    if ( cargc < 2 ) {
		adderr( gc, XP_GetClientStr(DBT_missingArgumentForSecuritypathDi_),
			filename, lineno );
		continue;
	    }
	    gc->gc_securitypath = dsgw_ch_strdup( cargv[1] );
#endif /* !DSGW_NO_SSL */

	} else if ( strcasecmp( cargv[0], "htmldir" ) == 0 ) {
	    int lenth = 0;

	    if ( cargc < 2 ) {
		adderr( gc, XP_GetClientStr(DBT_missingArgumentForHtmlpathDi_),
			filename, lineno );
		continue;
	    }
	    
	    lenth = strlen(cargv[1]);

	    /*See if the user put a slash at the end of the htmldir directive..*/
	    if (cargv[1][lenth - 1] == '/' || cargv[1][lenth - 1] == '\\') {
		gc->gc_docdir = dsgw_ch_strdup( cargv[1] );
	    } else {
		/*If not, put it there*/
		lenth ++;
		gc->gc_docdir = dsgw_ch_malloc ((lenth+MAXPATHLEN) *sizeof (char));
		PR_snprintf(gc->gc_docdir, lenth + MAXPATHLEN, "%s/", cargv[1]);
	    }
	    /* The nametrans used. For the gw, it's /dsgw/html/ */
	} else if ( strcasecmp( cargv[0], "gwnametrans" ) == 0 ) {
	    if ( cargc < 2 ) {
		adderr( gc, XP_GetClientStr(DBT_missingArgumentForNametransDi_),
			filename, lineno );
		continue;
	    }
	    
	    /* 
	     * This is needed for redirection. Can't use relative paths
	     * for Location:. If the gateway/phonebook/userDefinedGateway
	     * is running under a web server, it should be the html nametrans
	     * used to map to the html files. If it's under the admin server,
	     * it should be /dsgw/DIRECTORY_OF_HTML_FILES/ (which should be
	     * the same as the nameTrans.
	     */
	    gc->gc_gwnametrans = dsgw_ch_strdup( cargv[1] );

	} else if ( strcasecmp( cargv[0], "configdir" ) == 0 ) {
	    int lenth = 0;

	    if ( cargc < 2 ) {
		adderr( gc, XP_GetClientStr(DBT_missingArgumentForConfigpathDi_),
			filename, lineno );
		continue;
	    }

	    lenth = strlen(cargv[1]);

	    /*See if the user put a slash at the end of the htmldir directive..*/
	    if (cargv[1][lenth - 1] == '/' || cargv[1][lenth - 1] == '\\') {
		gc->gc_configdir = dsgw_ch_strdup( cargv[1] );
	    } else {
		/*If not, put it there*/
		lenth ++;
		gc->gc_configdir = dsgw_ch_malloc ((lenth+MAXPATHLEN) * sizeof (char));
		PR_snprintf(gc->gc_configdir, lenth + MAXPATHLEN, "%s/",
			cargv[1]);
	    }

	    gc->gc_tmpldir   = dsgw_ch_strdup( gc->gc_configdir );

	} else if ( strcasecmp( cargv[0], "location-suffix" ) == 0 ) {
	    if ( templatesonly ) continue;
	    if ( cargc < 2 ) {
		adderr( gc,
			XP_GetClientStr(DBT_missingArgumentForLocationSuffix_),
			filename, lineno );
		continue;
	    }
	    if ( locsuffix != NULL ) {
		free( locsuffix );
	    }
	    locsuffix = dsgw_ch_strdup( cargv[1] );

	} else if ( strcasecmp( cargv[0], "location" ) == 0 ) {
	    if ( templatesonly ) continue;
	    if ( cargc < 4 ) {
		adderr( gc,
    XP_GetClientStr(DBT_threeArgumentsAreRequiredForTheL_),
			filename, lineno );
		continue;
	    }
	    add_location( &gc->gc_newentryloccount, &gc->gc_newentrylocs,
		    locsuffix, &cargv[1] );

	} else if ( strcasecmp( cargv[0], "newtype" ) == 0 ) {
	    if ( templatesonly ) continue;
	    if ( cargc < 3 ) {
		adderr( gc,
    XP_GetClientStr(DBT_atLeastTwoArgumentsAreRequiredFo_),
			filename, lineno );
		continue;
	    }
	    if ( add_newtype( &gc->gc_newentrytypes, gc->gc_newentryloccount,
			gc->gc_newentrylocs, cargc - 1, &cargv[1] ) < 0 ) {
		adderr( gc, XP_GetClientStr(DBT_unknownLocationInNewtypeDirectiv_),
			filename, lineno );
	    }

	} else if ( strcasecmp( cargv[0], "tmplset" ) == 0 ) {
	    if ( cargc != 4 && cargc != 5 ) {
		adderr( gc,
    XP_GetClientStr(DBT_threeOrFourArgumentsAreRequiredF_),
			filename, lineno );
		continue;
	    }
	    add_tmplset( &gc->gc_tmplsets, cargc - 1, &cargv[1] );

	} else if ( strcasecmp( cargv[0], "attrvset" ) == 0 ) {
	    if ( cargc != 5 ) {
		adderr( gc,
    XP_GetClientStr(DBT_fourArgumentsAreRequiredForTheAt_),
			filename, lineno );
		continue;
	    }
	    add_avset( &gc->gc_avsets, &cargv[1] );

	} else if ( strcasecmp( cargv[0], "includeset" ) == 0 ) {
	    if ( cargc != 3 ) {
		adderr( gc,
    XP_GetClientStr(DBT_twoArgumentsAreRequiredForTheInc_),
			filename, lineno );
		continue;
	    }
	    add_includeset( &gc->gc_includesets, &cargv[1] );

	} else if ( strcasecmp( cargv[0], "charset" ) == 0 ) {
	    if ( cargc < 2 ) {
		adderr( gc, XP_GetClientStr(DBT_missingArgumentForCharsetDirecti_),
			filename, lineno );
		continue;
	    }
	    gc->gc_charset = dsgw_ch_strdup( cargv[1] );

/* the following is not needed because AdminServer */
/* puts these into environment from ns-admin.conf */
#ifdef NEED_LANG_FROM_DSGW_CONF
	} else if ( strcasecmp( cargv[0], "ClientLanguage" ) == 0 ) {
	    if ( cargc < 2 ) {
		adderr( gc,
                        XP_GetClientStr(DBT_missingArgumentForClientlanguage_),
			filename, lineno );
		continue;
	    }
	    gc->gc_ClientLanguage = dsgw_ch_strdup( cargv[1] );

	} else if ( strcasecmp( cargv[0], "AdminLanguage" ) == 0 ) {
	    if ( cargc < 2 ) {
		adderr( gc,
                        XP_GetClientStr(DBT_missingArgumentForAdminlanguageD_),
			filename, lineno );
		continue;
	    }
	    gc->gc_AdminLanguage = dsgw_ch_strdup( cargv[1] );

	} else if ( strcasecmp( cargv[0], "DefaultLanguage" ) == 0 ) {
	    if ( cargc < 2 ) {
		adderr( gc,
                        XP_GetClientStr(DBT_missingArgumentForDefaultlanguag_),
			filename, lineno );
		continue;
	    }
	    gc->gc_DefaultLanguage = dsgw_ch_strdup( cargv[1] );
#endif

	} else if ( strcasecmp( cargv[0], "NLS" ) == 0 ) {
	    if ( cargc < 2 ) {
		adderr( gc,
                        XP_GetClientStr(DBT_missingArgumentForNLS_),
			filename, lineno );
		continue;
	    }
	    gc->gc_NLS = dsgw_ch_strdup( cargv[1] );

	} else if ( strcasecmp( cargv[0], "vcard-property" ) == 0 ) {
	    if ( cargc != 4 && cargc != 5 ) {
		adderr( gc,
			XP_GetClientStr(DBT_threeOrFourArgumentsAreRequiredF_2),
			filename, lineno );
		continue;
	    }
	    if ( strcmp( cargv[2], "cis" ) != 0
		    && strcmp( cargv[2], "mls" ) != 0 ) {
		adderr( gc,
			XP_GetClientStr(DBT_vcardPropertySyntaxMustBeCisOrMl_),
			filename, lineno );
		continue;
	    }
	    add_vcardproperty( &gc->gc_vcardproperties, cargc - 1, &cargv[1] );

	} else if ( strcasecmp( cargv[0], "ignoreAcceptCharsetFrom" ) == 0 ) {
	    int i;
	    gc->gc_clientIgnoreACharset = (char **)dsgw_ch_malloc( cargc );
	    --cargc;
	    for (i = 0; i < cargc; i++)
	        gc->gc_clientIgnoreACharset[i] = dsgw_ch_strdup_tolower( cargv[i+1] );
	    gc->gc_clientIgnoreACharset[i] = NULL;

	} else if ( strcasecmp( cargv[0], "translate" ) == 0 ) {
	    if ( cargc != 3 ) {
		adderr( gc,
                        XP_GetClientStr(DBT_twoArgumentsAreRequiredForTheInc_),
			filename, lineno );
		continue;
	    }
	    add_l10nset( &gc->gc_l10nsets, &cargv[1] );

	/* include another config file */
	} else if ( strcasecmp( cargv[0], "include" ) == 0 ) {
	    char	*tmpfname = NULL;
	    char	*path = NULL;
	    char	*p;
	    int		len;

	    if ( cargc < 2 ) {
		adderr( gc, XP_GetClientStr(DBT_missingFilenameForIncludeDirecti_),
			filename, lineno );
		continue;
	    }
	    len = strlen( cargv[1] ) + 11;
	    tmpfname = dsgw_ch_malloc( len );
	    p = strrchr( cargv[1], '/' );
	    if ( p != NULL ) {
		*p++ = '\0';
	        sprintf( tmpfname, "%s/$$LANGDIR/%s", cargv[1], p);
		*(--p) = DSGW_PATHSEP_CHAR;
	    } else {
		p = cargv[1];
	        sprintf( tmpfname, "$$LANGDIR/%s", p);
	    }

            /* allocate buffers with enough extra room to fit "$$LANGDIR/" */
            if ( NULL != gc->gc_ClientLanguage ) {
                len += strlen( gc->gc_ClientLanguage );
            }
            path = dsgw_ch_malloc( len );
            if ( GetFileForLanguage( tmpfname, gc->gc_ClientLanguage, path ) < 0 )
		strcpy( path, cargv[1] );

#ifdef DSGW_DEBUG
    	    dsgw_log( "tmpfile: %s, path: %s, lang: %s\n",
		          tmpfname, path, gc->gc_ClientLanguage );
#endif
	    read_dsgwconfig( path, locsuffix, templatesonly, 0 );
	    if ( tmpfname ) free( tmpfname );
	    if ( path ) free( path );

	    /*Special file that has binddn and password*/
	} else if ( strcasecmp( cargv[0], "binddnfile" ) == 0 ) {
	    char	*tmpfname;
	    
	    if ( cargc < 2 ) {
		adderr( gc, XP_GetClientStr(DBT_missingFilenameForBinddnfileDirecti_),
			filename, lineno );
		continue;
	    }

	    /* Make sure it has at least 1 slash in it */
	    if ( strstr(cargv[1], "/") == NULL) {
		adderr( gc, XP_GetClientStr(DBT_badFilenameForBinddnfileDirecti_),
		       filename, lineno );
		continue;
	    }

	    /* ... and no ".."'s */
	    if ( strstr(cargv[1], "..") != NULL) {
		adderr( gc, XP_GetClientStr(DBT_badFilenameForBinddnfileDirecti_),
		       filename, lineno );
		continue;
	    }

	    /* And no "dsgw" in it */
	    if ( strstr(cargv[1], "/dsgw/") != NULL) {
		adderr( gc, XP_GetClientStr(DBT_badFilenameForBinddnfileDirecti_),
		       filename, lineno );
		continue;
	    }


	    tmpfname = dsgw_ch_strdup( cargv[1] );
	    read_dsgwconfig( tmpfname, locsuffix, templatesonly, 1 /*binddn file*/ );
	    free( tmpfname );
	    /* 
	     * Only consider the binddn directive if this file was
	     * included from another file with the binddnfile
	     * directive. This is to prevent the stupid user from
	     * inlining the binddn and bindpw in dsgw.conf.  This is
	     * bad because you can read dsgw.conf with a browser if
	     * you set up your web server to serve up the gateway.
	     * Just goto http://host/dsgw/context/dsgw.conf .  It is
	     * my hope that the binddn file will be outside
	     * NS-HOME/dsgw, because people can get at it if it's in
	     * there.
	     */
	} else if ( strcasecmp( cargv[0], "binddn" ) == 0 ) {
	    if (!binddnfile) {
		adderr( gc, XP_GetClientStr(DBT_wrongPlaceForBinddnDirectiv_),
			filename, lineno );
		continue;
	    }
	    if ( templatesonly ) continue;
	    if ( cargc < 2 ) {
		adderr( gc, XP_GetClientStr(DBT_missingArgumentForBinddnDirectiv_),
			filename, lineno );
		continue;
	    }
	    gc->gc_binddn = dsgw_ch_strdup( cargv[ 1 ]);

	} else if ( strcasecmp( cargv[0], "bindpw" ) == 0 ) {
	    if (!binddnfile) {
		adderr( gc, XP_GetClientStr(DBT_wrongPlaceForBinddnDirectiv_),
			filename, lineno );
		continue;
	    }

	    if ( templatesonly ) continue;
	    if ( cargc < 2 ) {
		adderr( gc, XP_GetClientStr(DBT_missingArgumentForBindpwDirectiv_),
			filename, lineno );
		continue;
	    }
	    gc->gc_bindpw = dsgw_ch_strdup( cargv[ 1 ]);

	} else {
	    adderr( gc, XP_GetClientStr(DBT_unknownDirectiveInConfigFileN_),
		    filename, lineno );
	}
    }

    if ( gc == NULL || gc->gc_configerr > 0 ) {
	dsgw_error( DSGW_ERR_BADCONFIG, ( gc->gc_configerrstr == NULL ) ?
		"" : gc->gc_configerrstr, DSGW_ERROPT_EXIT, 0, NULL );
    }
}

int
erase_db() {

    FILE    *fp;
    int     rc, lineno;
    char    *line;
    char    *cargv[ MAXARGS ];
    int     cargc;
    char    cmd[ BIG_LINE ];

    if ( (fp = fopen( gc->gc_localdbconf, "r" )) == NULL ) {
        dsgw_emitf (XP_GetClientStr(DBT_EraseDbCouldNotOpenLcacheConfFil_),
            gc->gc_localdbconf);
        return( -1 );
    }
    fp_getline_init( &lineno );

    while ( (line = fp_getline( fp, &lineno )) != NULL ) {
        fp_parse_line( line, &cargc, cargv );
        if ( strcasecmp( cargv[0], "directory" ) == 0) {
#ifdef XP_WIN32
	    dsgw_unix2dospath( cargv[1] );
#endif
            PR_snprintf (cmd, BIG_LINE, "%s %s%c* > %s 2>&1", DSGW_DELETE_CMD, cargv[1],
		    DSGW_PATHSEP_CHAR, DSGW_NULL_DEVICE);
            fflush (0);
            if (system (cmd) == 0) {
                /*
                 * success: display status message
                 */
		dsgw_emits( XP_GetClientStr(DBT_FontSize1NPTheDatabaseHasBeenDel_) );
		rc = 0;
            }
            else {
		dsgw_emits( XP_GetClientStr(DBT_FontSize1NPTheDatabaseCouldNotBe_) );
		rc = -1;
            }

            dsgw_emits( "<HR>\n" );
            fclose( fp );
	    return( rc );
        }
    }
    return -1;
}

void
app_suffix (char *ldif, char *suffix)
{
    FILE    *oldfp, *newfp;
    char    *orig_line;
    char    *p;
    char    buf[BUFSIZ];
    int     i, cargc;
    char    *cargv[ 100 ];
    char    tmpldif[ 128 ];
    char    *dns[] = { "aliasedobjectname:",
                        "aliasedobjectname:",
                        "associatedname:",
                        "dependentupon:",
                        "ditredirect:",
                        "dn:",
                        "documentauthor:",
                        "documentauthor:",
                        "documentavailable:",
                        "errorsto:",
                        "errorsto:",
                        "imagefiles:",
                        "lastmodifiedby:",
                        "manager:",
                        "member:",
                        "memberofgroup:",
                        "naminglink:",
                        "naminglink:",
                        "obsoletedbydocument:",
                        "obsoletesdocument:",
                        "owner:",
                        "proxy:",
                        "reciprocalnaminglink:",
                        "reciprocalnaminglink:",
                        "replicaroot:",
                        "replicabinddn:",
                        "requeststo:",
                        "roleoccupant:",
                        "secretary:",
                        "seealso:",
                        "uniqueMember:",
                        "updatedbydocument:",
                        "updatesdocument:",
                        NULL
                     };


    if ( (oldfp = fopen( ldif, "r" )) == NULL ) {
        dsgw_emitf (XP_GetClientStr(DBT_AppSuffixCouldNotOpenLdifFileSN_),
            ldif);
        return;
    }

    PR_snprintf( tmpldif, 128, "%s.tmp", ldif);
    if ( (newfp = fopen( tmpldif, "w" )) == NULL ) {
        dsgw_emitf (XP_GetClientStr(DBT_AppSuffixCouldNotOpenTmpFileSN_),
            ldif);
        return;
    }
    while ( fgets( buf, sizeof(buf), oldfp ) != NULL ) {
        /* skip comments and blank lines */
        if ( buf[0] == '#' || buf[0] == '\0' || buf[0] == '\n') {
            fputs( buf, newfp );
            continue;
        }
        orig_line = dsgw_ch_strdup( buf );

        fp_parse_line( buf, &cargc, cargv );
        for (i=0; dns[i]!=NULL; i++) {
            if ( strcasecmp( cargv[0], dns[i] ) == 0 ) {
                if ( (p = strchr( orig_line, '\n' )) != NULL ) {
                    *p = '\0';
                }
                fprintf ( newfp, "%s, %s\n", orig_line, suffix );
                break;
            }
        }

        if ( dns[i] == NULL ) {
            fputs( orig_line, newfp );
        }
        free (orig_line);
    }
    fclose(newfp);
    fclose(oldfp);
    unlink( ldif );
    if ( rename( tmpldif, ldif ) != 0 ) {
        dsgw_emitf (XP_GetClientStr(DBT_unableToRenameSToS_), tmpldif, ldif );
        return;
    }
}

/*
 * Running under admserv - traverse the list of property/value pairs
 * returned by dbconf_read_default_dbinfo().
 */
static void
get_dbconf_properties( char *filename )
{
    DBConfDBInfo_t *db_info;
    DBPropVal_t	*dbp;
    int		rc;
    LDAPURLDesc	*ludp;
    LDAPDBURLDesc *ldbudp;

    if (( rc = dbconf_read_default_dbinfo( filename, &db_info ))
	    != LDAPU_SUCCESS ) {
	report_ldapu_error( rc, DSGW_ERR_BADCONFIG, DSGW_ERROPT_EXIT );
    }

    if ( db_info == NULL ) {
	dsgw_error( DSGW_ERR_DBCONF,
		XP_GetClientStr(DBT_nullPointerReturnedByDbconfReadD_),
		DSGW_ERROPT_EXIT, 0, NULL );
    }

    if ( strcasecmp( db_info->dbname, DBCONF_DEFAULT_DBNAME ) != 0 ) {
	dsgw_error( DSGW_ERR_DBCONF, db_info->dbname, DSGW_ERROPT_EXIT, 0,
		NULL );
    }

#ifdef DSGW_DEBUG
    dsgw_log( "opened dbconf, dbname is %s, dburl is %s\n", db_info->dbname,
	    db_info->url );
#endif

    /* Parse the LDAPURL or LDAPDBURL */
    gc->gc_baseurl = dsgw_ch_strdup( db_info->url );
    rc = ldapdb_url_parse( gc->gc_baseurl, &ldbudp );

    if ( rc == 0 ) {
	gc->gc_localdbconf = dsgw_ch_strdup( ldbudp->ludb_path );
	gc->gc_ldapserver = NULL;
	gc->gc_ldapport = -1;
	gc->gc_ldapsearchbase = dsgw_ch_strdup( ldbudp->ludb_dn );
#ifndef DSGW_NO_SSL
	gc->gc_ldapssl = 0;
#endif

    /* If url isn't "ldapdb://", let the code below have a crack */
    } else if ( rc != DSGW_ERR_LDAPDBURL_NOTLDAPDB ) {
	switch ( rc ) {
	case DSGW_ERR_LDAPDBURL_NODN:
		adderr( gc, XP_GetClientStr(DBT_badLdapdbUrlTheBaseDnIsMissingN_), NULL, 0 );
		break;
	case DSGW_ERR_LDAPDBURL_BAD:
		adderr( gc, XP_GetClientStr(DBT_badLdapdbUrlN_), NULL, 0 );
		break;
	}
    } else {
	if (( rc = ldap_url_parse( gc->gc_baseurl, &ludp )) != 0 ) {
	    switch ( rc ) {
	    case LDAP_URL_ERR_NODN:
		adderr( gc, XP_GetClientStr(DBT_badUrlProvidedForBaseurlDirectiv_2),
			NULL, 0 );
		break;
	    case LDAP_URL_ERR_MEM:
		dsgw_error( DSGW_ERR_NOMEMORY,
			XP_GetClientStr(DBT_parsingBaseurlDirective_1),
			DSGW_ERROPT_EXIT, 0, NULL );
		break;
	    case LDAP_URL_ERR_NOTLDAP:
		adderr( gc, XP_GetClientStr(DBT_badUrlProvidedForBaseurlDirectiv_3), NULL, 0 );
		break;
	    }
	} else {
	    gc->gc_ldapserver = ludp->lud_host;
	    gc->gc_ldapport = ludp->lud_port;
	    if ( ludp->lud_dn == NULL ) {
		gc->gc_ldapsearchbase = dsgw_ch_strdup( "" );
	    } else {
		gc->gc_ldapsearchbase = ludp->lud_dn;
	    }
	    if ( ( ludp->lud_options & LDAP_URL_OPT_SECURE ) != 0 ) {
#ifdef DSGW_NO_SSL
		adderr( gc, XP_GetClientStr(DBT_LdapsUrlsAreNotYetSupportedN_1),
			NULL, 0 );
#else
		gc->gc_ldapssl = 1;
#endif
	    }
	}
    }

    /* Look through the properties for binddn and bindpw */
    for ( dbp = db_info->firstprop; dbp != NULL; dbp = dbp->next ) {

#ifdef DSGW_DEBUG
	dsgw_log( "get prop: prop = %s, val = %s\n", dbp->prop, dbp->val );
#endif

	if ( strcasecmp( dbp->prop, "binddn" ) == 0 ) {
	    if ( dbp->val == NULL || strlen( dbp->val ) == 0 ) {
		dsgw_error( DSGW_ERR_DBCONF,
			XP_GetClientStr(DBT_noValueGivenForBinddn_), 
			DSGW_ERROPT_EXIT, 0, NULL );
	    }
	    gc->gc_binddn = dsgw_ch_strdup( dbp->val );

	} else if ( strcasecmp( dbp->prop, "bindpw" ) == 0 ) {
	    if ( dbp->val == NULL || strlen( dbp->val ) == 0 ) {
		dsgw_error( DSGW_ERR_DBCONF,
			XP_GetClientStr(DBT_noValueGivenForBindpw_), 
			DSGW_ERROPT_EXIT, 0, NULL );
	    }
	    gc->gc_bindpw = dsgw_ch_strdup( dbp->val );
	}
    }

    if ( gc == NULL || gc->gc_configerr > 0 ) {
	dsgw_error( DSGW_ERR_BADCONFIG, ( gc->gc_configerrstr == NULL ) ?
		"" : gc->gc_configerrstr, DSGW_ERROPT_EXIT, 0, NULL );
    }
    if ( gc->gc_baseurl == NULL ) {
	dsgw_error( DSGW_ERR_BADCONFIG,
		XP_GetClientStr(DBT_thereIsNoDefaultDirectoryService_),
		DSGW_ERROPT_EXIT, 0, NULL );
    }
    return;
}


/*
 * Update the dbswitch.conf file (used under admin. server) to reflect
 * the local/remote directory information contained in "cfgp".  Our basic
 * strategy is to read the existing dbswitch.conf file, replacing and adding
 * lines that look like this:
 *	directory <dbhandle> ...
 *	<dbhandle>:binddn ...
 *      <dbhandle>:encoded bindpw ...
 * as necessary.  We write a new, temporary config file (copying all other
 * lines over unchanged) and then replace the old file with our new one.
 *
 * If cfgp is configured for localdb mode, we only write a directory line.
 *
 * We return zero if all goes well and non-zero if not.
 *
 * Note that all reading and writing of the dbswitch.conf file is now done
 * using the dbconf...() functions that are part of the ldaputil library, so
 * any comments, blank lines, or unrecognized config file lines will be lost.
 * Also, all "bindpw" property values will be encoded when re-written.
 *
 * Only these members of the cfgp structure are used in this function:
 *	gc_localdbconf		(NULL if using remote LDAP server)
 *	gc_ldapsearchbase
 *	gc_ldapserver
 *	gc_ldapport
 *	gc_ldapssl
 *	gc_binddn
 *	gc_bindpw
 * Actually, if gc_localdbconf is not NULL, only it and gc_ldapsearchbase are
 * used.
 */	
int
dsgw_update_dbswitch( dsgwconfig *cfgp, char *dbhandle, int erropts )
{
    char		oldfname[ MAXPATHLEN ], newfname[ MAXPATHLEN ];
    char		*userdb_path, buf[ MAXPATHLEN + 100 ];
    int			rc, wrote_dbinfo;
    FILE		*newfp;
    DBConfInfo_t	*cip;
    DBConfDBInfo_t	*dbip;
    DBPropVal_t		*pvp;

    if ( dbhandle == NULL ) {
	dbhandle = "default";
    }

    if (( userdb_path = get_userdb_dir()) == NULL ) {
	dsgw_error( DSGW_ERR_USERDB_PATH, NULL, erropts, 0, NULL );
	return( -1 );
    }

    /* read old dbswitch.conf contents */
    PR_snprintf( oldfname, MAXPATHLEN, "%s/%s", userdb_path,
		DSGW_DBSWITCH_FILE );
    if (( rc = dbconf_read_config_file( oldfname, &cip )) != LDAPU_SUCCESS ) {
	report_ldapu_error( rc, DSGW_ERR_BADCONFIG, erropts );
	return( -1 );
    }

    /* write db info to new file, replacing information for "dbhandle" */
    PR_snprintf( newfname, MAXPATHLEN, "%s/%s", userdb_path,
		DSGW_DBSWITCH_TMPFILE );
    if (( newfp = fopen( newfname, "w" )) == NULL ) {
	PR_snprintf( buf, MAXPATHLEN + 100,
	    XP_GetClientStr(DBT_cannotOpenConfigFileSForWritingN_), newfname );
	dsgw_error( DSGW_ERR_UPDATE_DBSWITCH, buf, erropts, 0, NULL );
	return( -1 );
    }

    wrote_dbinfo = 0;
    for ( dbip = cip->firstdb; dbip != NULL; dbip = dbip->next ) {
	if ( strcasecmp( dbip->dbname, dbhandle ) == 0 ) {
	    /*
	     * found db name to be replaced:  replace with updated information 
	     */
	    if (( rc = write_dbswitch_info( newfp, cfgp, dbhandle )) !=
		    LDAPU_SUCCESS ) {
		report_ldapu_error( rc, DSGW_ERR_UPDATE_DBSWITCH, erropts );
		return( -1 );
	    }

	    wrote_dbinfo = 1;

	} else {
	    /*
	     * re-write existing db conf information without changes
	     */
	    if (( rc = dbconf_output_db_directive( newfp, dbip->dbname,
		    dbip->url )) != LDAPU_SUCCESS ) {
		report_ldapu_error( rc, DSGW_ERR_UPDATE_DBSWITCH, erropts );
		return( -1 );
	    }

	    for ( pvp = dbip->firstprop; pvp != NULL; pvp = pvp->next ) {
		if (( rc = dbconf_output_propval( newfp, dbip->dbname,
			pvp->prop, pvp->val,
			strcasecmp( pvp->prop, "bindpw" ) == 0 ))
			!= LDAPU_SUCCESS ) {
		    report_ldapu_error( rc, DSGW_ERR_UPDATE_DBSWITCH, erropts );
		    return( -1 );
		}
	    }
	}
    }

    if ( !wrote_dbinfo ) {
	if (( rc = write_dbswitch_info( newfp, cfgp, dbhandle )) !=
		LDAPU_SUCCESS ) {
	    report_ldapu_error( rc, DSGW_ERR_UPDATE_DBSWITCH, erropts );
	    return( -1 );
	}
    }

    dbconf_free_confinfo( cip );
    fclose( newfp );

    /* replace old file with new one */
#ifdef _WIN32
    if ( !MoveFileEx( newfname, oldfname, MOVEFILE_REPLACE_EXISTING )) {
#else
    if ( rename( newfname, oldfname ) != 0 ) {
#endif
	PR_snprintf( buf, MAXPATHLEN + 100,
		XP_GetClientStr(DBT_unableToRenameSToS_1), newfname, oldfname );
	dsgw_error( DSGW_ERR_UPDATE_DBSWITCH, buf, erropts, 0, NULL );
	return( -1 );
    }

    return( 0 );
}


static int
write_dbswitch_info( FILE *fp, dsgwconfig *cfgp, char *dbhandle )
{
    char	*escapeddn, *url;
    int		rc;

    escapeddn = dsgw_strdup_escaped( cfgp->gc_ldapsearchbase );

    if ( cfgp->gc_localdbconf == NULL ) { /* remote server: write ldap:// URL */
	url = dsgw_ch_malloc( 21 + strlen( cfgp->gc_ldapserver )
		+ strlen( escapeddn ));	/* room for "ldaps://HOST:PORT/DN" */
	sprintf( url, "ldap%s://%s:%d/%s",
#ifdef DSGW_NO_SSL
		"",
#else
		cfgp->gc_ldapssl ? "s" : "",
#endif
		cfgp->gc_ldapserver, cfgp->gc_ldapport, escapeddn );
    } else {				  /* local db: write ldapdb:// URL */
	url = dsgw_ch_malloc( 11 + strlen( cfgp->gc_localdbconf )
		+ strlen( escapeddn ));	/* room for "ldapdb://PATH/DN" */
	sprintf( url, "ldapdb://%s/%s\n", cfgp->gc_localdbconf, escapeddn );
    }

    rc = dbconf_output_db_directive( fp, dbhandle, url );

    free( url );
    free( escapeddn );

    if ( rc != LDAPU_SUCCESS ) {
	return( rc );
    }

    if ( cfgp->gc_localdbconf == NULL ) { /* using directory server */
	if ( cfgp->gc_binddn != NULL &&
		( rc = dbconf_output_propval( fp, dbhandle, "binddn",
		cfgp->gc_binddn, 0 ) != LDAPU_SUCCESS )) {
	    return( rc );
	}

	if ( cfgp->gc_bindpw != NULL &&
		( rc = dbconf_output_propval( fp, dbhandle, "bindpw",
		cfgp->gc_bindpw, 1 ) != LDAPU_SUCCESS )) {
	    return( rc );
	}
    }

    return( LDAPU_SUCCESS );
}


/* pass 0 for lineno if it is unknown or not applicable */
static void
adderr( dsgwconfig *gc, char *str, char *filename, int lineno )
{
    char *lbuf = dsgw_ch_malloc( MAXPATHLEN + 200 );

    gc->gc_configerr++;
    if ( lineno == 0 ) {
	PR_snprintf( lbuf, MAXPATHLEN + 200,
		XP_GetClientStr(DBT_configFileS_), filename );
    } else {
	PR_snprintf( lbuf, MAXPATHLEN + 200,
		XP_GetClientStr(DBT_configFileSLineD_), filename, lineno );
    }
    gc->gc_configerrstr = dsgw_ch_realloc( gc->gc_configerrstr,
	    strlen( gc->gc_configerrstr ) + strlen( str )
	    + strlen( lbuf ) + 6 );
    strcat( gc->gc_configerrstr, lbuf );
    strcat( gc->gc_configerrstr, str );
    strcat( gc->gc_configerrstr, "<BR>\n" );
    free( lbuf );
}


static void
add_location( int *loccountp, dsgwloc **locarrayp, char *locsuffix,
	char **argv )
{
    int		len;
    dsgwloc	*locp;

    *locarrayp = (dsgwloc *)dsgw_ch_realloc( *locarrayp, 
	    ( *loccountp + 1 ) * sizeof( dsgwloc ));
    locp = &((*locarrayp)[ *loccountp ]);
    locp->dsloc_handle = dsgw_ch_strdup( argv[0] );
    locp->dsloc_fullname = dsgw_ch_strdup( argv[1] );
    len = strlen( argv[2] );

    if (  argv[2][ len - 1 ] == '#' ) {
	/* '#' implies that locsuffix is not to be appended */
	locp->dsloc_dnsuffix = dsgw_ch_strdup( argv[2] );
	locp->dsloc_dnsuffix[ len - 1 ] = '\0';

    } else if ( locsuffix != NULL && *locsuffix != '\0' ) {
	/* append suffix, preceded by ", " if location arg. is not "" */
	locp->dsloc_dnsuffix = dsgw_ch_malloc( len + strlen( locsuffix ) + 3 );
	if ( argv[2][0] != '\0' ) {
	    strcpy( locp->dsloc_dnsuffix, argv[2] );
	    strcat( locp->dsloc_dnsuffix, ", " );
	    strcat( locp->dsloc_dnsuffix, locsuffix );
	} else {
	    strcpy( locp->dsloc_dnsuffix, locsuffix );
	}

    } else {
	locp->dsloc_dnsuffix = dsgw_ch_strdup( argv[2] );
    }
    ++(*loccountp);
}


static int
add_newtype( dsgwnewtype **newentlistp, int loccount, dsgwloc *locarray,
	int argc, char **argv )
{
    int		i, j;
    dsgwnewtype	*ntp, *prevntp;

    ntp = (dsgwnewtype *)dsgw_ch_malloc( sizeof( dsgwnewtype ));
    ntp->dsnt_template = dsgw_ch_strdup( argv[0] );
    ntp->dsnt_fullname = dsgw_ch_strdup( argv[1] );
    ntp->dsnt_rdnattr = dsgw_ch_strdup( argv[2] );
    ntp->dsnt_next = NULL;
    ntp->dsnt_loccount = argc - 3;
    argv = &argv[3];

    /* fill dsnt_locations array with indexes into gc->gc_newentrylocs */
    if ( ntp->dsnt_loccount <= 0 ) {
	ntp->dsnt_locations = NULL;
    } else {
	int foundit;
	ntp->dsnt_locations = (int *)dsgw_ch_malloc( ntp->dsnt_loccount *
		sizeof( int ));
	for ( i = 0; i < ntp->dsnt_loccount; ++i ) {
	    foundit = 0;
	    for ( j = 0; j < loccount && !foundit; ++j ) {
		if ( strcasecmp( argv[ i ], locarray[ j ].dsloc_handle )
			== 0 ) {
		    ntp->dsnt_locations[ i ] = j;
		    foundit = 1;
		}
	    }
	    /* if ( j >= loccount ) { */
	    if ( !foundit ) {
		return( -1 );	/* unknown location -- error */
	    }
	}
    }

    /* append to linked list of new entry structures */
    if ( *newentlistp == NULL ) {
	*newentlistp = ntp;
    } else {
	for ( prevntp = *newentlistp; prevntp->dsnt_next != NULL;
		prevntp = prevntp->dsnt_next ) {
	    ;
	}
	prevntp->dsnt_next = ntp;
    }

    return( 0 );
}


static void
add_tmplset( dsgwtmplset **tslp, int argc, char **argv )
{
    dsgwtmplset	*prevtsp, *tsp;
    dsgwview	*prevvp, *vp;

    prevtsp = NULL;
    tsp = *tslp;
    while ( tsp != NULL ) {
	if ( strcasecmp( tsp->dstset_name, argv[0] ) == 0 ) {
	    break;
	}
	prevtsp = tsp;
	tsp = tsp->dstset_next;
    }

    if ( tsp == NULL ) {	/* new template set */
	tsp = (dsgwtmplset *)dsgw_ch_malloc( sizeof( dsgwtmplset ));
	memset( tsp, 0, sizeof( dsgwtmplset ));
	tsp->dstset_name = dsgw_ch_strdup( argv[0] );
	if ( prevtsp == NULL ) {
	    *tslp = tsp;
	} else {
	    prevtsp->dstset_next = tsp;
	}
    }

    /* add a new view to the end of this template set's view list */
    vp = (dsgwview *)dsgw_ch_malloc( sizeof( dsgwview ));
    memset( vp, 0, sizeof( dsgwview ));
    vp->dsview_caption = dsgw_ch_strdup( argv[1] );
    vp->dsview_template = dsgw_ch_strdup( argv[2] );
    if ( argc > 3 ) {
	vp->dsview_jscript = dsgw_ch_strdup( argv[3] );
    }

    if ( tsp->dstset_viewlist == NULL ) {
	tsp->dstset_viewlist = vp;
    } else {
	for ( prevvp = tsp->dstset_viewlist; prevvp->dsview_next != NULL;
		prevvp = prevvp->dsview_next ) {
	    ;
	}
	prevvp->dsview_next = vp;
    }
    ++tsp->dstset_viewcount;
}


static void
add_avset( dsgwavset **avsp, char **argv )	/* 4 args. in argv[] */
{
    dsgwavset	*prevavp, *avp;

    /* is this the first element of a set? */
    prevavp = NULL;
    for ( avp = *avsp; avp != NULL; avp = avp->dsavset_next ) {
	if ( strcasecmp( avp->dsavset_handle, argv[0] ) == 0 ) {
	    break;
	}
	prevavp = avp;
    }

    if ( avp == NULL ) {	/* first element: add a new set */
	avp = (dsgwavset *)dsgw_ch_malloc( sizeof( dsgwavset ));
	memset( avp, 0, sizeof( dsgwavset ));
	avp->dsavset_handle = dsgw_ch_strdup( argv[0] );
	if ( prevavp == NULL ) {
	    *avsp = avp;
	} else {
	    prevavp->dsavset_next = avp;
	}
    }

    ++avp->dsavset_itemcount;
    avp->dsavset_values = (char **)dsgw_ch_realloc( avp->dsavset_values,
	    avp->dsavset_itemcount * sizeof( char * ));
    avp->dsavset_values[ avp->dsavset_itemcount - 1 ] =
	    dsgw_ch_strdup( argv[1] );
    avp->dsavset_prefixes = (char **)dsgw_ch_realloc( avp->dsavset_prefixes,
	    avp->dsavset_itemcount * sizeof( char * ));
    avp->dsavset_prefixes[ avp->dsavset_itemcount - 1 ] =
	    dsgw_ch_strdup( argv[2] );
    avp->dsavset_suffixes = (char **)dsgw_ch_realloc( avp->dsavset_suffixes,
	    avp->dsavset_itemcount * sizeof( char * ));
    avp->dsavset_suffixes[ avp->dsavset_itemcount - 1 ] =
	    dsgw_ch_strdup( argv[3] );
}


static void
add_includeset( dsgwinclset **isp, char **argv )	/* 2 args. in argv[] */
{
    dsgwinclset	*previsp, *tmpisp;

    /* is this the first element of a set? */
    previsp = NULL;
    for ( tmpisp = *isp; tmpisp != NULL; tmpisp = tmpisp->dsiset_next ) {
	if ( strcasecmp( tmpisp->dsiset_handle, argv[0] ) == 0 ) {
	    break;
	}
	previsp = tmpisp;
    }

    if ( tmpisp == NULL ) {	/* first element: add a new set */
	tmpisp = (dsgwinclset *)dsgw_ch_malloc( sizeof( dsgwinclset ));
	memset( tmpisp, 0, sizeof( dsgwinclset ));
	tmpisp->dsiset_handle = dsgw_ch_strdup( argv[0] );
	if ( previsp == NULL ) {
	    *isp = tmpisp;
	} else {
	    previsp->dsiset_next = tmpisp;
	}
    }

    ++tmpisp->dsiset_itemcount;
    tmpisp->dsiset_filenames =
	    (char **)dsgw_ch_realloc( tmpisp->dsiset_filenames,
	    tmpisp->dsiset_itemcount * sizeof( char * ));
    tmpisp->dsiset_filenames[ tmpisp->dsiset_itemcount - 1 ] =
	    dsgw_ch_strdup( argv[1] );
}

static void
add_l10nset( dsgwsubst **l10np, char **argv )		/* 2 args, in argv[] */
{
    dsgwsubst *tmpsp;

    tmpsp = (dsgwsubst *)dsgw_ch_malloc( sizeof( dsgwsubst ));
    tmpsp->dsgwsubst_from = dsgw_ch_strdup( argv[0] );
    tmpsp->dsgwsubst_to = dsgw_ch_strdup( argv[1] );
    tmpsp->dsgwsubst_next = *l10np;
    *l10np = tmpsp;
}

static void
add_vcardproperty( dsgwvcprop **vcpropp, int argc, char **argv )
{
    dsgwvcprop	*prevvcp, *newvcp, *vcp;

    newvcp = (dsgwvcprop *)dsgw_ch_malloc( sizeof( dsgwvcprop ));
    newvcp->dsgwvcprop_next = NULL;
    newvcp->dsgwvcprop_property = dsgw_ch_strdup( argv[0] );
    newvcp->dsgwvcprop_syntax = dsgw_ch_strdup( argv[1] );
    newvcp->dsgwvcprop_ldaptype = dsgw_ch_strdup( argv[2] );
    if ( argc == 3 ) {
	newvcp->dsgwvcprop_ldaptype2 = NULL;
    } else {
	newvcp->dsgwvcprop_ldaptype2 = dsgw_ch_strdup( argv[3] );
    }

    prevvcp = NULL;
    for ( vcp = *vcpropp; vcp != NULL; vcp = vcp->dsgwvcprop_next ) {
	prevvcp = vcp;
    }

    if ( prevvcp == NULL ) {
	*vcpropp = newvcp;
    } else {
	prevvcp->dsgwvcprop_next = newvcp;
    }
}


static char *
strtok_quote( char *line, char *sep )
     /* This implementation can't handle characters > 127 in sep.
	But it works fine for sep == " \t".
      */
{
	int		inquote;
	char		*tmp;
	static char	*next;

	if ( line != NULL ) {
		next = line;
	}
	while ( *next && strchr( sep, *next ) ) {
		next++;
	}

	if ( *next == '\0' ) {
		next = NULL;
		return( NULL );
	}
	tmp = next;

	for ( inquote = 0; *next; ) {
		switch ( *next ) {
		case '"':
			if ( inquote ) {
				inquote = 0;
			} else {
				inquote = 1;
			}
			strcpy( next, next + 1 );
			break;

#ifndef _WIN32
		case '\\':
			strcpy( next, next + 1 );
			break;
#endif

		default:
			if ( ! inquote ) {
				if ( strchr( sep, *next ) != NULL ) {
					*next++ = '\0';
					return( tmp );
				}
			}
			next++;
			break;
		}
	}

	return( tmp );
}

static char	buf[BUFSIZ];
static char	*line;
static int	lmax, lcur;

#define CATLINE( buf )	{ \
	int	len; \
	len = strlen( buf ); \
	while ( lcur + len + 1 > lmax ) { \
		lmax += BUFSIZ; \
		line = (char *) dsgw_ch_realloc( line, lmax ); \
	} \
	strcpy( line + lcur, buf ); \
	lcur += len; \
}



static void
fp_parse_line(
    char	*line,
    int		*argcp,
    char	**argv
)
{
	char *	token, buf[ 20 ];

	*argcp = 0;
	for ( token = strtok_quote( line, " \t" ); token != NULL;
	    token = strtok_quote( NULL, " \t" ) ) {
		if ( *argcp == MAXARGS ) {
			PR_snprintf( buf, 20,
				XP_GetClientStr(DBT_maxD_), MAXARGS );
			dsgw_error( DSGW_ERR_CONFIGTOOMANYARGS, buf,
				DSGW_ERROPT_EXIT, 0, NULL );
		}
		argv[(*argcp)++] = token;
	}
	argv[*argcp] = NULL;
}



static char *
fp_getline( FILE *fp, int *lineno )
{
	char		*p;

	lcur = 0;

	while ( fgets( buf, sizeof(buf), fp ) != NULL ) {
		if ( (p = strchr( buf, '\n' )) != NULL ) {
			*p = '\0';
		}
		if ( lcur > 0 && ! ldap_utf8isspace( buf ) ) {
			return( line );	/* return previously saved line */
		}
		CATLINE( buf );
		(*lineno)++;
		if ( ! ldap_utf8isspace( buf )) {
			return( line );	/* return this line */
		}
	}
	buf[0] = '\0';

	return( lcur > 0 ? line : NULL );
}

static void
fp_getline_init( int *lineno )
{
	*lineno = 0;
	buf[0] = '\0';
}


static int
ldapdb_url_parse( char *url, LDAPDBURLDesc **ldbudpp )
{
/*
 * Pick apart the pieces of an ldapdb:// quasi-URL
 */
    LDAPDBURLDesc	*ldbudp;
    char		*basedn;

    *ldbudpp = NULL;

    if ( strncasecmp( url, LDAPDB_URL_PREFIX, LDAPDB_URL_PREFIX_LEN )) {
	return( DSGW_ERR_LDAPDBURL_NOTLDAPDB );
    }

    /* allocate return struct */
    ldbudp = (LDAPDBURLDesc *) dsgw_ch_malloc( sizeof( LDAPDBURLDesc ));

    /* Make a copy */
    url = dsgw_ch_strdup( url );
    ldbudp->ludb_path = url + LDAPDB_URL_PREFIX_LEN;

    /* Must start with a "/" (or "x:" on NT) */
    if ( ldbudp->ludb_path[ 0 ] != '/'
#ifdef _WIN32
	    && ( !ldap_utf8isalpha( ldbudp->ludb_path )
	    || ldbudp->ludb_path[ 1 ] != ':' )
#endif
    ) {
	free( url );
	free( ldbudp );
	return( DSGW_ERR_LDAPDBURL_BAD );
    }

    /* Find base DN */
    if (( basedn = strrchr( ldbudp->ludb_path, '/' )) == NULL ) {
	free( url );
	free( ldbudp );
	return( DSGW_ERR_LDAPDBURL_BAD );
    }

    *basedn++ = '\0';
    ldbudp->ludb_dn = basedn;
    dsgw_form_unescape( ldbudp->ludb_dn );

    *ldbudpp = ldbudp;
    return( 0 );
}

#ifdef XP_WIN32
/* convert forward slashes to backwards ones */
static void
dsgw_unix2dospath( char *path )
{
    if( path ) {
	while( *path ) {
	    if( *path == '/' ) {
		 *path = '\\';
	    }
	    path++;
	}
    }
}
#endif

/*
 * Function: dsgw_valid_context
 *
 * Returns: 1 if context doesn't have / . \ ,etc, 0 else
 *
 * Description: context is the name of the config file
 *              that is passed into the CGI.
 *              Let's say context = pb
 *              then it gets translated into: ../context/pb.conf
 *              so we have to make sure that context
 *              only contains numbers or letters, and nothing else
 *
 * Author: RJP
 *
 */
static int 
dsgw_valid_context()
{
    char *local_context = NULL;

    /*Get a local pointer to the global context*/
    local_context = context;

    if (local_context == NULL) {
	return(1);
    }

    for ( ; *local_context; LDAP_UTF8INC(local_context)) {
	
	if (!ldap_utf8isalnum(local_context)) {
	
	    /*Allow dashes and underscores*/
	    if (*local_context == '-' || *local_context == '_') {
		continue;
	    }
	    return(0);
	}
    }
    return(1);

}

/*
 * Function: dsgw_valid_docname
 *
 * Returns: 1 if context doesn't have / . \ ,etc, 0 else
 *
 * Description: Checks to make sure that filename contains
 *              only alphanumeric values and one dot
 *
 * Author: RJP
 *
 */
int 
dsgw_valid_docname(char *filename)
{
    int dots = 0;
    char *local_filename = NULL;

    local_filename = filename;

    if (local_filename == NULL) {
	return(1);
    }

    for ( ; *local_filename; LDAP_UTF8INC(local_filename)) {

	/*If it's not a number or a letter...*/
	if (!ldap_utf8isalnum(local_filename)) {

	    /*If it's a dot, and there haven't been any other dots...*/
	    if (*local_filename == '.' && dots == 0) {
		/*Then increment the dot count and continue...*/
		dots ++;
		continue;
	    }

	    /*Allow dashes and underscores*/
	    if (*local_filename == '-' || *local_filename == '_') {
		continue;
	    }

	    return (0);	    
	}
    }
   
    return(1);
}

/*
 * Function: dsgw_get_docdir
 *
 * Returns: a pointer to the html directory
 *
 * Description: Just returns gc->gc_docdir
 *
 * Author: RJP
 *
 */
char *
dsgw_get_docdir(void) 
{
    return(gc->gc_docdir);
}

/*
 * Function: browser_is_msie40
 *
 * Returns: 1 if HTTP_USER_AGENT is MSIE 4.0 or greater, 0 else
 *
 * Description: MSIE 4.0 doesn't return HTTP_ACCEPT_CHARSET,
 *              but it does understand utf-8, so we need to
 *              make a special case for it. If the browser
 *              being used is MSIE 4.0 or greater, this function
 *              returns 1.
 *
 * Author: RJP
 *
 */
static int
browser_is_msie40()
{
  char *p = NULL;
  char *browzer = NULL;
  char version[6];
  int i;

  /* Get the browser name */
  if (( p = getenv( "HTTP_USER_AGENT" )) == NULL ) {
    return(0);
  }

  /* Try to find MSIE in there */
  browzer = strstr (p, "MSIE ");

  /* If nothing, then we're done */
  if (browzer == NULL) {
    return (0);
  }
  
  /* Skip to the version */
  browzer += 5;
  
  /* Accumulate the version */
  for (i=0; i < 5 && *browzer != '.' ; i++, browzer++) {
    version[i] = *browzer;
  }
  
  /* Null terminate */
  version[i] = '\0';

  if (atoi(version) > 3) {
    return(1);
  }
  
  return(0);

}

/*
 * Function: browser_ignores_acceptcharset
 *
 * Returns: 1 if ignoreAcceptCharsetFrom contains the current HTTP_USER_AGENT,
 *	    0 else
 *
 * Description: bug fix for #97908:
 *   The dsgw doesn't respect the "charset" variable in the dsgw.conf file.
 *   E.g., ignoreAcceptCharsetFrom Mozilla/4.01x-NSCP  Mozilla/4.03C-NSCP
 *
 */
static int
browser_ignores_acceptcharset()
{
  char *p = NULL;
  char *browzer = NULL;
  int i;

  if ( gc->gc_clientIgnoreACharset == NULL ||
       gc->gc_clientIgnoreACharset[0] == NULL )
    return 0;

  /* Get the browser name */
  if (( p = getenv( "HTTP_USER_AGENT" )) == NULL ) {
    return 0;
  }
  browzer = dsgw_ch_strdup_tolower( p );

  for ( i = 0; gc->gc_clientIgnoreACharset[i]; i++ ) {
    if ( strstr( browzer, gc->gc_clientIgnoreACharset[i] ) != NULL )
      return 1;
  }
  free( browzer );
  return 0;
}

static void
set_dsgwcharset()
{
  auto char* fname = dsgw_file2path (gc->gc_configdir, "dsgwcharset.conf");
  auto FILE* f = fopen (fname, "r");
  if (f != NULL) {
    auto char buf[BUFSIZ];
    if (fgets (buf, sizeof(buf), f)) {
      auto const size_t buflen = strlen (buf);
      if (buf[buflen-1] == '\n') {
        buf[buflen-1] = '\0';
      }
      gc->gc_charset = dsgw_ch_strdup (buf);
    }
    fclose (f);
  }
  free (fname);
}

static char *
dsgw_ch_strdup_tolower( const char *s )
{
  int         len, i;
  char        *p, *sp, *dp;

  len = strlen( s ) + 1;
  dp = p = dsgw_ch_malloc( len );
  sp = (char *)s;
  for (i = 0; i < len; i++, dp++, sp++)
      *dp = tolower(*sp);
  return( p );
}

static scriptrange_t**
parse_scriptranges (char** cargv, size_t cargc)
{
    auto scriptrange_t** result = (scriptrange_t**)
        dsgw_ch_calloc (cargc + 1, sizeof(scriptrange_t*));
    auto size_t i;
    for (i = 0; i < cargc; ++i) {
	auto scriptrange_t** last = result+i;
	auto char* token;
	auto char* cursor = NULL;
	for (token = ldap_utf8strtok_r (cargv[i], ",;", &cursor); token;
	     token = ldap_utf8strtok_r (NULL,     ",;", &cursor)) {
#ifdef DSGW_DEBUG
	    dsgw_log ("parse_scriptranges %s\n", token);
#endif
	    *last = dsgw_ch_malloc (sizeof(scriptrange_t));
	    (*last)->sr_min = (*token == '-') ? 0 : strtoul (token, &token, 16);
	    (*last)->sr_max = (*token != '-') ? (*last)->sr_min
	      : ((*++token == '\0') ? ULONG_MAX : strtoul (token, &token, 16));
	    last = &((*last)->sr_next);
	}
	*last = NULL;
    }
    result[cargc] = NULL;
    return result;
}

scriptorder_t*
dsgw_scriptorder()
{
    static scriptorder_t* result = NULL;
    if (result == NULL) {
    	auto char* simplename = "dsgwcollate.conf";
	auto char* fname = dsgw_file2path (gc->gc_configdir, simplename);
	auto FILE* fp;
	result = (scriptorder_t*) dsgw_ch_calloc (1, sizeof(scriptorder_t));
	if (NULL == fname) {
#ifdef DSGW_DEBUG
	    dsgw_log ("dsgw_scriptorder can't find %s\n", simplename);
#endif
	} else if (NULL == (fp = fopen (fname, "r"))) {
#ifdef DSGW_DEBUG
	    dsgw_log ("dsgw_scriptorder can't open %s\n", fname);
#endif
	} else {
	    auto char* line;
	    auto int   lineno;
	    fp_getline_init( &lineno );
	    while ( (line = fp_getline( fp, &lineno )) != NULL ) {
		auto int   cargc;
		auto char* cargv[ MAXARGS ];
		/* skip comments and blank lines */
		if ( line[0] == '#' || line[0] == '\0' ) {
		    continue;
		}
		fp_parse_line( line, &cargc, cargv );
		if ( !strcasecmp (cargv[0], "caseIgnoreAccents")) {
		    result->so_caseIgnoreAccents = 1;
		} else if ( !strcasecmp (cargv[0], "sort")) {
		    result->so_sort = parse_scriptranges (cargv+1, cargc-1);
		} else if ( !strcasecmp (cargv[0], "display")) {
		    result->so_display = parse_scriptranges (cargv+1, cargc-1);
		} else {
#ifdef DSGW_DEBUG
		    dsgw_log ("%s/%i: unknown keyword %s\n", fname, lineno, cargv[0]);
#endif
		}
	    }
	    fclose (fp);
#ifdef DSGW_DEBUG
	    dsgw_log ("dsgw_scriptorder %s line %i\n", fname, lineno);
#endif
	}
	if (fname) free (fname);
    }
    return result;
}

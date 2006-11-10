/** --- BEGIN COPYRIGHT BLOCK ---
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
  --- END COPYRIGHT BLOCK ---  */
/*
 * dsgwutil.c -- misc. utility functions -- HTTP gateway
 */

#include <limits.h> /* PATH_MAX */
#include "dsgw.h"
#include "dbtdsgw.h"
#include "../lib/libsi18n/gsslapd.h"

#ifdef DSGW_DEBUG
#include <time.h>
#include <stdarg.h>
#endif /* DSGW_DEBUG */

static char **vpmap = NULL;

extern char *Versionstr;	/* from Versiongw.c */

char		*progname;		/* set by dsgw_init() */
dsgwconfig	*gc;			/* set by dsgw_init() */
int		http_hdr_sent = 0;	/* non-zero if header has been sent */
char		**header_lines = NULL;	/* null-terminated array of hdr lines */
char *dsgw_html_body_colors = "";	/* reset by dsgw_init() */

/*Global context variable, telling the CGI's where to look for the config file*/
char            *context = NULL;        /* Gotten from the QUERY_STRING */
char            *langwich = NULL;       /* The language that libsi18n 
					   picks from acceptlang*/
char            *countri = NULL;       /* The country that libsi18n 
					   picks from acceptlang*/


static void figure_out_langwich(void);

/*
 * dsgw_init -- initialize a dsgw CGI program:
 *	set "progname" global based on "progpath" (normally argv[0])
 *	check that REQUEST_METHOD is in "methods_handled" mask
 *	if request method is "POST", read HTML form variables from stdin
 *      handles the context variable if the CGI was called with a post.
 *      The context variable tells dsgw_read_config what config file
 *      to read.
 *
 * If an fatal error occurs, -1 is returned.
 * If all goes well, returns either DSGW_METHOD_GET or DSGW_METHOD_POST
 */
int
dsgw_init( int argc, char **argv, int methods_handled )
{
    char	*m, *s;
    int		method;
    int		c, err;

    (void)ADM_Init();

    /* initialize the string database */
    XP_InitStringDatabase(
	  SERVER_ROOT_PATH "/bin/slapd/property" /* Directory Server Gateway */
	  , DATABASE_NAME);
    /* set default default languages for string database */
    SetLanguage(CLIENT_LANGUAGE, "");
    SetLanguage(ADMIN_LANGUAGE, "");
    SetLanguage(DEFAULT_LANGUAGE, "");

    if (( progname = strchr( argv[0], '/' )) == NULL ) {
	progname = dsgw_ch_strdup( argv[0] );
#ifdef _WIN32
	if (( s = strrchr( progname, '.' )) != NULL
		&& strcasecmp( s, ".EXE" ) == 0 ) {
	    *s = '\0';
	}
#endif /* _WIN32 */
    } else {
	++progname;
    }

    while (( c = getopt( argc, argv, "v" )) != EOF ) {
	if ( c == 'v' ) {
	    printf( "%s\n", Versionstr );
	}
	exit( 0 );
    }

#ifdef DSGW_DEBUG
    dsgw_log( "%s started\n", Versionstr );
#endif
    err = method = 0;

    /*Have to get the context before we read the config file.*/
    if (( m = getenv( "REQUEST_METHOD" )) != NULL ) {
	if ( strcasecmp( m, "GET" ) == 0 || strcasecmp( m, "HEAD" ) == 0 ) {
	    method = DSGW_METHOD_GET;
	} else if ( strcasecmp( m, "POST" ) == 0 ) {
	    method = DSGW_METHOD_POST;
	    if (( err = dsgw_post_begin( stdin )) == 0 ) {
		 context = dsgw_get_cgi_var( "context", DSGW_CGIVAR_OPTIONAL );
	    }
	}
    }

    if ( method == 0 || ( methods_handled & method ) == 0 ) {
	dsgw_error( DSGW_ERR_BADMETHOD, NULL, DSGW_ERROPT_EXIT, 0, NULL );
   }
    
    /*If no context was given, try default.conf.*/
    if (context == NULL) { 
	context = dsgw_ch_strdup("default");
    }

    gc = dsgw_read_config();

    gc->gc_charset = dsgw_emit_converts_to (gc->gc_charset);
    {
	/* eliminate elements of gc_changeHTML that don't apply to gc_charset: */
	auto dsgwsubst **s = &(gc->gc_changeHTML);
	auto char *charset = gc->gc_charset;
	if ( charset == NULL ) charset = ""; /* Latin-1, implicitly */
	while ( *s ) {
	    auto char **c = (*s)->dsgwsubst_charsets;
	    if ( c && *c ) {
		for ( ; *c; ++c ) {
		    if ( strcasecmp( *c, charset ) == 0 ) {
			break;
		    }
		}
		if ( *c == NULL ) {
		    *s = (*s)->dsgwsubst_next; /* eliminate **s */
		    /* This is quick and dirty: we just created garbage. */
		    continue;
		}
	    }
	    s = &((*s)->dsgwsubst_next);
	}
    }

    /* set languages for string database */
    SetLanguage(CLIENT_LANGUAGE,gc->gc_ClientLanguage);
    SetLanguage(ADMIN_LANGUAGE,gc->gc_AdminLanguage);
    SetLanguage(DEFAULT_LANGUAGE,gc->gc_DefaultLanguage);

    /* Figure out the language that libsi18n is using */
    figure_out_langwich();

    /* Get the port and servername */
    if (method == DSGW_METHOD_POST) {
	 if (( s = dsgw_get_cgi_var( "ldapport", DSGW_CGIVAR_OPTIONAL )) != NULL ) {
	      gc->gc_ldapport = atoi( s );
	      free( s );
	 }
	 if (( s = dsgw_get_cgi_var( "ldapserver", DSGW_CGIVAR_OPTIONAL )) != NULL ) {
	      gc->gc_ldapserver = s;
	 }
	 
    }
    
    if (( s = getenv( "HTTPS" )) == NULL || strcasecmp( s, "on" ) == 0 ||
	    ( s = getenv( "HTTPS_KEYSIZE" )) == NULL ) {
	gc->gc_httpskeysize = 0;
    } else {
	gc->gc_httpskeysize = atoi( s );
    }

    /* set default color scheme */
    if ( method == DSGW_METHOD_POST && ( s = dsgw_get_cgi_var( "colors",
	    DSGW_CGIVAR_OPTIONAL )) != NULL ) {
	dsgw_html_body_colors = s;
    } else if ( gc->gc_admserv ) {	/* use same color scheme as libadmin */
	dsgw_html_body_colors = "BGCOLOR=\"#C0C0C0\" LINK=\"#0000EE\" "
		"VLINK=\"#551A8B\" ALINK=\"#FF0000\"";
    } else {
	dsgw_html_body_colors = "BGCOLOR=\"white\"";
    }

    return( method );
}


/*
 * function called back by dsgw_parse_line() to evaluate IF directives.
 * return non-zero for true, zero for false.
 */
int
dsgw_simple_cond_is_true( int argc, char **argv, void *arg /* UNUSED */ )
{
    if ( strcasecmp( argv[0], DSGW_COND_ADMSERV ) == 0 ) {
	return( gc->gc_admserv );
    }
 
    if ( strcasecmp( argv[0], DSGW_COND_LOCALDB ) == 0 ) {
	return( gc->gc_localdbconf != NULL );
    }

    if ( strcasecmp( argv[0], DSGW_COND_POSTEDFORMVALUE ) == 0 ) {
	/*
	 * format of IF statment is:
	 *    <-- IF "PostedFormValue" "VARNAME" "VALUE" -->
	 * where VARNAME is the name of a POSTed CGI variable to look for and
	 * VALUE is an optional value to test it against.  If VALUE is omitted,
	 * the test is just for the presence of a variable named VARNAME.
	 */
	char	*postedvalue;

	if ( argc < 2 || ( postedvalue = dsgw_get_cgi_var( argv[1],
		DSGW_CGIVAR_OPTIONAL )) == NULL ) {
	    return( 0 );	/* VARNAME is missing or not posted */
	} else if ( argc < 3 ) {
	    return( 1 );	/* VALUE is missing, so return true */
	} else {
	    return( strcasecmp( postedvalue, argv[ 2 ] ) == 0 );
	}
    } 

    return( 0 );
}


/*
 * return a pointer to a malloc'd string containing the path to
 * config. file "filename", based on the DSGW_CONFIGDIR define.
 * If "filename" contains "..", or "//" this is treated as a fatal
 * error.  If "prefix" is not NULL, it is pre-pended to "filename"
 */
char *
dsgw_file2path( char *prefix, char *filename )
{
    char	*path, *pattern;
    int		len;

    if ( strstr( filename, "//" ) != NULL ||
	    strstr( filename, ".." ) != NULL ) {
	dsgw_error( DSGW_ERR_BADFILEPATH, filename, DSGW_ERROPT_EXIT, 0, NULL );
    }

    if ( prefix == NULL ) {
	prefix = "";
    }

    /* allocate buffers with enough extra room to fit "$$LANGDIR/" */
    len =  strlen( prefix ) + strlen( filename ) + 11;
    if ( NULL != gc->gc_ClientLanguage ) {
        len += strlen( gc->gc_ClientLanguage );
    }
    path = dsgw_ch_malloc( len );
    pattern = dsgw_ch_malloc( len );

    /* call GetFileForLanguage() to do its I18n magic */
    sprintf( pattern, "%s$$LANGDIR/%s", prefix, filename );
    if ( GetFileForLanguage( pattern, gc->gc_ClientLanguage, path ) < 0 ) {
	sprintf( path, "%s%s", prefix, filename );	/* fallback */
    }
    free( pattern );

    return( path );
}



/*
 * return a pointer to a malloc'd string containing the path to
 * config. file "filename", based on the DSGW_HTMLDIR define.
 * If "filename" contains "..", or "//" this is treated as a fatal
 * error.  If "prefix" is not NULL, it is pre-pended to "filename"
 */
char *
dsgw_file2htmlpath( char *prefix, char *filename )
{
    char	*path, *pattern;
    int		len;

    if ( strstr( filename, "//" ) != NULL ||
	    strstr( filename, ".." ) != NULL ) {
	dsgw_error( DSGW_ERR_BADFILEPATH, filename, DSGW_ERROPT_EXIT, 0, NULL );
    }

    if ( prefix == NULL ) {
	prefix = "";
    }

    /* allocate buffers with enough extra room to fit "$$LANGDIR/" */
    /*len = strlen( DSGW_HTMLDIR ) + strlen( prefix ) + strlen( filename ) + 11;*/
    len = strlen( gc->gc_docdir ) + strlen( prefix ) + strlen( filename ) + 11;
    if ( NULL != gc->gc_ClientLanguage ) {
        len += strlen( gc->gc_ClientLanguage );
    }

    path = dsgw_ch_malloc( len );
    pattern = dsgw_ch_malloc( len );

    /* call GetFileForLanguage() to do its I18n magic */
    sprintf( pattern, "%s%s$$LANGDIR/%s", gc->gc_docdir, prefix, filename );
    if ( GetFileForLanguage( pattern, gc->gc_ClientLanguage, path ) < 0 ) {
	/*  use fallback */
	sprintf( path, "%s/%s%s", gc->gc_docdir, prefix, filename );
    }
    free( pattern );

    return( path );
}


/*
 * malloc that checks for NULL return value and exits upon failure
 */
void *
dsgw_ch_malloc( size_t n )
{
    void *p;

    if (( p = malloc( n )) == NULL ) {
	dsgw_error( DSGW_ERR_NOMEMORY, NULL, DSGW_ERROPT_EXIT, 0, NULL );
    }

    return( p );
}

void *
dsgw_ch_calloc( size_t nelem, size_t elsize )
{
    register void *p = calloc( nelem, elsize );
    if ( p == NULL ) {
	dsgw_error( DSGW_ERR_NOMEMORY, NULL, DSGW_ERROPT_EXIT, 0, NULL );
    }
    return( p );
}

/*
 * realloc that checks for NULL return value and exits upon failure
 * we also handle p == NULL by doing a malloc
 */
void *
dsgw_ch_realloc( void *p, size_t n )
{
    if ( p == NULL ) {
	p = malloc( n );
    } else {
	p = realloc( p, n );
    }

    if ( p == NULL ) {
	dsgw_error( DSGW_ERR_NOMEMORY, NULL, DSGW_ERROPT_EXIT, 0, NULL );
    }

    return( p );
}


/*
 * strdup that checks for NULL return value and exits upon failure
 */
char *
dsgw_ch_strdup( const char *s )
{
    int		len;
    char	*p;

    len = strlen( s ) + 1;
    p = dsgw_ch_malloc( len );
    memcpy( p, s, len );
    return( p );
}



/*
 * Escape any single- or double-quotes with a '\'.  Used when generating
 * JavaScript code.  Returns a malloc'd string which the caller is
 * responsible for freeing.
 */
char *
dsgw_escape_quotes( char *in )
{
    char *out;
    char *p, *t;
    int nq = 0;


    if ( in == NULL ) {
	return NULL;
    }
    /* count number of quotes */
    for ( p = in; *p != '\0'; p++ ) {
	if ( *p == '\'' || *p == '"' ) {
	    nq++;
	}
    }
    out = dsgw_ch_malloc(( p - in ) + nq + 1 );
    for ( p = in, t = out; *p != '\0'; p++ ) {
	if ( *p == '\'' || *p == '"' ) {
	    *t++ = '\\';
	}
	*t++ = *p;
    }
    *t = '\0';
    return out;
}

char *
dsgw_get_translation( char *in )
{
    dsgwsubst *p;

#ifdef DSGW_DEBUG
    dsgw_log( "L10n map table:\n" );
    for ( p = gc->gc_l10nsets; p ; p = p->dsgwsubst_next ) {
        dsgw_log( "%s -> %s\n", p->dsgwsubst_from, p->dsgwsubst_to );
    }
#endif

    for ( p = gc->gc_l10nsets; p ; p = p->dsgwsubst_next ) {
	if ( !strcasecmp( in, p->dsgwsubst_from ))
	    return p->dsgwsubst_to;
    }
    return in;
}

static void
dsgw_puts (const char* s)
{
    dsgw_fputn (stdout, s, strlen(s));
}

#define CONTENT_TYPE "Content-type"
#define TYPE_HTML "text/html"
#define VARY "Vary"
#define VARYLIST "Accept-Language,Accept-Charset,User-Agent"

static const char* ct_prefix = CONTENT_TYPE ": " TYPE_HTML;
static const char* cs_prefix = ";charset=";
static const char* vr_prefix = VARY ": ";

/*
 * Send the headers we've accumulated.
 */
void
dsgw_send_header()
{
    int i;

    if ( http_hdr_sent ) {
	return;
    }
    if ( header_lines == NULL ) {
	dsgw_puts (ct_prefix);
	if ( gc != NULL && gc->gc_charset != NULL && *gc->gc_charset != '\0' ) {
	    dsgw_puts (cs_prefix); dsgw_puts (gc->gc_charset );
	}
	dsgw_puts ("\n");
	/* send Vary tag if HTTP/1.1 or greater */
	if ( NULL != gc && gc->gc_httpversion >= 1.1 ) {
	    dsgw_puts (vr_prefix); dsgw_puts (VARYLIST); dsgw_puts ("\n");
	}
    } else for ( i = 0; header_lines[ i ] != NULL; i++ ) {
	dsgw_puts (header_lines[ i ]);
	dsgw_puts ("\n");
    }
    dsgw_puts ("\n");
    http_hdr_sent = 1;
}
	

/*
 * Add a line to the array of header lines.
 */
void
dsgw_add_header( char *line )
{
    int i;

    if ( header_lines == NULL ) {
	header_lines = ( char ** ) dsgw_ch_malloc( 3 * sizeof( char * ));
	if ( gc != NULL && gc->gc_charset != NULL && *gc->gc_charset != '\0' ) {
	    header_lines[ 0 ] = dsgw_ch_malloc( strlen( ct_prefix ) +
		    strlen( cs_prefix ) + strlen( gc->gc_charset ) + 1 );
	    sprintf( header_lines[ 0 ], "%s%s%s", ct_prefix, cs_prefix,
		    gc->gc_charset );
	} else {
	    header_lines[ 0 ] = dsgw_ch_strdup( ct_prefix );
	}
	/* send Vary tag if HTTP/1.1 or greater */
	if ( gc->gc_httpversion >= 1.1 ) {
            header_lines[ 1 ] =
              dsgw_ch_malloc( strlen( vr_prefix ) + sizeof( VARYLIST ) );
                                    /* (char *) */    /* string literal */
	    sprintf( header_lines[ 1 ], "%s%s", vr_prefix, VARYLIST );
	    header_lines[ 2 ] = NULL;
	} else {
	    header_lines[ 1 ] = NULL;
	}
    }
    for ( i = 0; header_lines[ i ] != NULL; i++ );
    header_lines = (char **) dsgw_ch_realloc( header_lines,
	   ( i + 2 ) * sizeof( char * ));
    header_lines[ i ] = dsgw_ch_strdup( line );
    header_lines[ i + 1 ] = NULL;
}


/*
 * Check the environment for an authentication cookie.  Returns the
 * entire auth cookie if present, or returns NULL if no such cookie
 * exists.  The returned string must be freed by the caller.
 */
char *
dsgw_get_auth_cookie()
{
    char *p, *e, *ckhdr;
 
    ckhdr = getenv( "HTTP_COOKIE" );

    if ( ckhdr == NULL ) {
        return NULL;
    } else {
	ckhdr = strdup( ckhdr );
    }
 
    if (( p = strstr( ckhdr, DSGW_AUTHCKNAME )) == NULL ) {
	free( ckhdr );
        return NULL;
    }
 
    if (( e = strchr( p, ';' )) != NULL ) {
        *e = '\0';
    }
 
    p = strdup( p );
    free( ckhdr );
    return p;
}



/* 
 * Break a cookie into its random string and DN parts.  The DN is returned
 * unescaped.  The caller is responsible for freeing the returned DN
 * and random string.  Returns 0 on success, -1 on error.  If the
 * cookie has the value "[unauthenticated]", then 0 is returned and
 * dn is set to NULL;
 */
int
dsgw_parse_cookie( char *cookie, char **rndstr, char **dn )
{
    char 	*p,  *r;
    int		rlen;

    if ( cookie == NULL ) {
	*rndstr = *dn = NULL;
	return -1;
    }

    /* Make sure cookie starts with "nsdsgwauth" */
    if ( strncmp( cookie, DSGW_AUTHCKNAME, strlen( DSGW_AUTHCKNAME ))) {
	/* Cookie didn't start with "nsdsgwauth" */
	*rndstr = *dn = NULL;
	return -1;
    }
    
    r = cookie + strlen( DSGW_AUTHCKNAME );
    if ( *r == '=' ) {
	r++;
    }

    /* Is cookie value "[unauthenticated]" ? */
    if ( !strncmp( r, DSGW_UNAUTHSTR, strlen( DSGW_UNAUTHSTR ))) {
	*rndstr = strdup( DSGW_UNAUTHSTR );
	*dn = NULL;
	return 0;
    }

    /* find start of DN */
    if (( p = strrchr( cookie, ':' )) == NULL ) {
	*rndstr = *dn = NULL;
	return -1;
    }
    
    rlen = p - r + 1;
    *(rndstr) = dsgw_ch_malloc( rlen );
    *(rndstr)[ 0 ] = '\0';
    strncat( *rndstr, r, rlen-1 );
    (*rndstr)[ rlen - 1 ] = '\0'; 

    p++;
    *dn = strdup( p );
    dsgw_form_unescape( *dn );

    return 0;
}
    
/*
 * Generate a "go home" button with a link to the main entry point for
 * the gateway.  The caller is responsible for any surrounding
 * HTML, e.g. <FORM> and <TABLE> tags.
 */
void
dsgw_emit_homebutton()
{
    dsgw_emitf( "<INPUT TYPE=\"button\" VALUE=\"%s\" "
	"onClick=\"top.location.href='%s'\">", XP_GetClientStr(DBT_returnToMain_), gc->gc_urlpfxmain /*DSGW_URLPREFIX_MAIN*/ );
}
   

/*
 * Generate a help button with a link to the tutor program for
 * the given help topic.   The caller is responsible for any surrounding
 * HTML, e.g. <FORM> and <TABLE> tags.
 */
void
dsgw_emit_helpbutton( char *topic )
{
    if ( topic == NULL ) {
	return;
    }

    if ( gc->gc_admserv ) {
	char	*jscript;

	if (( jscript = helpJavaScriptForTopic( topic )) == NULL ) {
	    return;
	}

	dsgw_emitf( "<INPUT TYPE=\"button\" VALUE=\"%s\" onClick=\"%s\">",
#define LABEL_HELP "ヘルプ"
/*LABEL_HELP*/ XP_GetClientStr(DBT_help_), jscript );
    } else {
	char	*tutorvp;

	tutorvp = dsgw_getvp( DSGW_CGINUM_TUTOR );

	/*
	 * the following is based on code that was found in
	 * ldapserver/lib/libadmin/template.c inside the
	 * helpJavaScriptForTopic() function.  We need our own copy because
	 * we use a different tutor CGI.  Sigh.
	 */
	dsgw_emitf( "<INPUT TYPE=\"button\" VALUE=\"%s\" onClick=\""
		    "if ( top.helpwin ) {"
		    "  top.helpwin.focus();"
		    "  top.helpwin.infotopic.location='%s?!%s&context=%s';"
		    "} else {"
		    "  window.open('%s?%s&context=%s', 'infowin_dsgw', "
		    "    'resizable=1,width=400,height=500');"
		    "}\">\n",
		    XP_GetClientStr(DBT_help_1),tutorvp, topic, context, 
		    tutorvp, topic, context );
    }
}


/*
 * Return malloc'd URL prefix that consists of:
 *	prefix + '/' + HOST:PORT + '/' (not anymore - RJP)
 *      prefix + ? + context=CONTEXT&hp=HOST:PORT&dn=
 */
char *
dsgw_build_urlprefix()
{
    char	*prefix = dsgw_getvp( DSGW_CGINUM_DOSEARCH );
    char	*p, *urlprefix;

    p = ( gc->gc_ldapserver == NULL ? "" : gc->gc_ldapserver );
    urlprefix = dsgw_ch_malloc( 16 /* room for "?:port#&dn=" + zero-term. */
            + strlen( prefix ) + strlen( p ) +strlen(context) + 9);
    sprintf( urlprefix, "%s?context=%s&hp=%s", prefix, context, p );
    if ( gc->gc_ldapport != 0 && gc->gc_ldapport != LDAP_PORT ) {
        sprintf( urlprefix + strlen( urlprefix ), ":%d", gc->gc_ldapport );
    }
    strcat( urlprefix,"&dn=" );
    return( urlprefix );
}


void
dsgw_addtemplate( dsgwtmpl **tlpp, char *template, int count, char **ocvals )
{
    int		i;
    dsgwtmpl	*prevtp, *tp;

    tp = (dsgwtmpl *)dsgw_ch_malloc( sizeof( dsgwtmpl ));
    memset( tp, 0, sizeof( dsgwtmpl ));
    tp->dstmpl_name = dsgw_ch_strdup( template );

    /* each argument is one objectClass */
    tp->dstmpl_ocvals = dsgw_ch_malloc(( count + 1 ) * sizeof( char * ));
    for ( i = 0; i < count; ++i ) {
	tp->dstmpl_ocvals[ i ] = dsgw_ch_strdup( ocvals[ i ] );
    }
    tp->dstmpl_ocvals[ count ] = NULL;

    if ( *tlpp == NULL ) {
	*tlpp = tp;
    } else {
	for ( prevtp = *tlpp; prevtp->dstmpl_next != NULL;
		prevtp = prevtp->dstmpl_next ) {
	    ;
	}
	prevtp->dstmpl_next = tp;
    } 
}


dsgwtmpl *
dsgw_oc2template( char **ocvals )
{
    int			i, j, needcnt, matchcnt;
    dsgwtmpl		*tp;

    for ( tp = gc->gc_templates; tp != NULL; tp = tp->dstmpl_next ) {
	needcnt = matchcnt = 0;
	for ( i = 0; tp->dstmpl_ocvals[ i ] != NULL; ++i ) {
	    for ( j = 0; ocvals[ j ] != NULL; ++j ) {
		if ( strcasecmp( ocvals[ j ], tp->dstmpl_ocvals[ i ] ) == 0 ) {
		    ++matchcnt;
		}
	    }
	    ++needcnt;
	}

	if ( matchcnt == needcnt ) {
	    return( tp );
	}
    }

    return( NULL );
}



void
dsgw_init_searchprefs( struct ldap_searchobj **solistp )
{
    char	*path;

    path = dsgw_file2path( gc->gc_configdir, DSGW_SEARCHPREFSFILE );
    if ( ldap_init_searchprefs( path, solistp ) != 0 ) {
	dsgw_error( DSGW_ERR_BADCONFIG, path, DSGW_ERROPT_EXIT, 0, NULL );
    }
    free( path );
}


void
dsgw_remove_leading_and_trailing_spaces( char **sp )
{
    auto char *s, *p;

    if ( sp == NULL || *sp == NULL ) {
        return;
    }

    s = *sp;

    /* skip past any leading spaces */
    while ( ldap_utf8isspace( s )) {
	LDAP_UTF8INC (s);
    }

    /* truncate to remove any trailing spaces */
    if ( *s != '\0' ) {
	p = s + strlen( s );
	LDAP_UTF8DEC (p);
	while (ldap_utf8isspace( p )) {
	    LDAP_UTF8DEC (p);
	}
	*LDAP_UTF8INC(p) = '\0';
    }
    *sp = s;
}


/*
 * Return the virtual path prefix for the CGI program specified by
 * cginum.
 */
char *
dsgw_getvp( int cginum )
{
    char	*cginame;
    char	*surl;
    /*char	*extpath;*/
    int		i;

    if ( cginum < 1 || cginum > DSGW_MODE_NUMMODES ) {
	return "";
    }
    if ( vpmap == NULL ) {
	/* note: slot zero of vpmap isn't used */
	vpmap = dsgw_ch_malloc(( DSGW_MODE_NUMMODES + 1 ) * sizeof( char * ));
	for ( i = 0; i <= DSGW_MODE_NUMMODES; i++ ) {
	    vpmap[ i ] = NULL;
	}
    }

    if ( vpmap[ cginum ] == NULL ) {
	switch ( cginum ) {
	case DSGW_CGINUM_DOSEARCH:
	    cginame = DSGW_CGINAME_DOSEARCH;
	    break;
	case DSGW_CGINUM_BROWSE:
	    cginame = DSGW_CGINAME_BROWSE;
	    break;
	case DSGW_CGINUM_SEARCH:
	    cginame = DSGW_CGINAME_SEARCH;
	    break;
	case DSGW_CGINUM_CSEARCH:
	    cginame = DSGW_CGINAME_CSEARCH;
	    break;
	case DSGW_CGINUM_AUTH:
	    cginame = DSGW_CGINAME_AUTH;
	    break;
	case DSGW_CGINUM_EDIT:
	    cginame = DSGW_CGINAME_EDIT;
	    break;
	case DSGW_CGINUM_DOMODIFY:
	    cginame = DSGW_CGINAME_DOMODIFY;
	    break;
	case DSGW_CGINUM_DNEDIT:
	    cginame = DSGW_CGINAME_DNEDIT;
	    break;
	case DSGW_CGINUM_TUTOR:
	    cginame = DSGW_CGINAME_TUTOR;
	    break;
	case DSGW_CGINUM_LANG:
	    cginame = DSGW_CGINAME_LANG;
	    break;
	default:
	    return "";
	}
	
	if (( surl = getenv( "SERVER_URL" )) == NULL ) { 
	  surl = ""; 
	} 
	 
	/*if ( gc->gc_admserv ) {
	 *
	 * include "/admin-serv/" or "/user-environment/" if appropriate
	 * 
	 *  if ( gc->gc_enduser ) {
	 *     extpath = DSGW_USER_ADM_BINDIR;
	 *  } else {
	 *     extpath = DSGW_ADMSERV_BINDIR;
	 *  }
	 * } else {
	 *  extpath = "";
	 * }
	 */
	vpmap[ cginum ] = dsgw_ch_malloc( strlen( gc->gc_urlpfxcgi ) + strlen( surl )
					  /*+ strlen( extpath ) */
					  + strlen( cginame ) + 2 );
	
	sprintf( vpmap[ cginum ], "%s%s%s", surl, 
		 /*extpath, */
		 gc->gc_urlpfxcgi, cginame );
	 
	/*sprintf( vpmap[ cginum ], "%s%s%s", extpath, gc->gc_urlpfxcgi, cginame );*/
    }
    return( vpmap[ cginum ]);
}


#ifdef DSGW_DEBUG
#include <stdio.h> /* FILE */

/* Returns a directory path used for tmp log files. */
char * 
dsgw_get_tmp_log_dir()
{	
	static char	tmp_log[MAXPATHLEN];
	char *install_dir = NULL;

#if defined( XP_WIN32 )
	int ilen;
	char *pch;
	char tmp_dir[_MAX_PATH];
#endif
	install_dir = getenv("NETSITE_ROOT");
	if (install_dir != NULL) {
		 PR_snprintf(tmp_log, sizeof(tmp_log), "%s/tmp/dsgw", install_dir);
#if defined( XP_WIN32 )
		 for(ilen=0; ilen < strlen(tmp_log); ilen++)
		 {
			 if(tmp_log[ilen]=='/')
				 tmp_log[ilen]='\\';
		 }
#endif /* XP_WIN32 */
	} else {
#if defined( XP_WIN32 )
		GetTempPath( ilen+1, tmp_dir ); 
		ilen = strlen(tmp_dir); 
		/* Remove trailing slash. */ 
		pch = tmp_dir[ilen-1]; 
		if( pch == '\\' || pch == '/' ) 
			tmp_dir[ilen-1] = '\0';
		PR_snprintf(tmp_log, sizeof(tmp_log), "%s\\DSGW", tmp_dir);
#else
		PR_snprintf(tmp_log, sizeof(tmp_log), "/tmp/dsgw");		
#endif
	}
	return tmp_log;
}

static FILE* log_out_fp = NULL;

void
dsgw_log_out (const char* s, size_t n)
{
    if ( log_out_fp == NULL ) {
	char	fname[ 256 ];
	char*	format =
#if defined( XP_WIN32 )
	  "%s\\log%.50s.out";
#else
	  "%s/%.50s.out";
#endif
	PR_snprintf( fname, sizeof(fname), format, dsgw_get_tmp_log_dir(), progname );
	log_out_fp = fopen( fname, "w" );
    }
    if (log_out_fp != NULL) {
	fwrite (s, sizeof(char), n, log_out_fp);
	fflush (log_out_fp);
    }
}


/*
 * logging function -- called like printf(); syslog-like output is written
 *	to a file called /tmp/progname where progname is derived from argv[0]
 */
static FILE* logfp = NULL;
void
dsgw_log( char *fmt, ... )
{
    time_t	t;
    char	timebuf[ 20 ];
    va_list	ap;

    t = time( NULL );

    if ( logfp == NULL ) {
	char	fname[ 256 ];
	char*	format =
#if defined( XP_WIN32 )
	  "%s\\log%.50s";
#else
	  "%s/%.50s";
#endif
	PR_snprintf( fname, sizeof(fname), format, dsgw_get_tmp_log_dir(), progname );
	if (( logfp = fopen( fname, "a+" )) == NULL ) {
	    return;
	}
    }

    memcpy( timebuf, ctime( &t ), sizeof(timebuf)-1 );
    timebuf[ sizeof(timebuf)-1 ] = '\0';
    fprintf( logfp, "%s %s: ", timebuf, progname );

    va_start( ap, fmt );
    (void)vfprintf( logfp, fmt, ap );
    va_end( ap );
    fflush( logfp );
}


/*
 * log the contents of a NULL-terminated array of character strings
 */
void
dsgw_logstringarray( char *arrayname, char **strs )
{
    int		i;

    if ( strs == NULL || strs[ 0 ] == NULL ) {
	dsgw_log( "Array %s: empty\n", arrayname );
    } else {
	dsgw_log( "Array %s:\n", arrayname );

	for ( i = 0; strs[ i ] != NULL; ++i ) {
	    dsgw_log( "\t%2d: \"%s\"\n", i, strs[ i ] );
	}
    }
}
#endif /* DSGW_DEBUG */

void
dsgw_head_begin()
{
    dsgw_emits ("<HEAD>");
    if ( gc != NULL && gc->gc_charset != NULL && *gc->gc_charset != '\0' ) {
	dsgw_emitf ("<META HTTP-EQUIV=\"%s\" CONTENT=\"%s%s%s\">",
		    CONTENT_TYPE, TYPE_HTML, cs_prefix, gc->gc_charset);
    }
}

void
dsgw_quote_emptyFrame()
{
    dsgw_quotation_begin( QUOTATION_JAVASCRIPT_MULTILINE );
    dsgw_emits( "<HTML>" );
    dsgw_emitf( "<BODY %s></BODY></HTML>", dsgw_html_body_colors );
    dsgw_quotation_end();
}

/* This function contains code to alert the user that their password has
   already expired. It gives them an opportunity to change it. */
void 
dsgw_password_expired_alert( char *dn )
{
#ifdef NOTFORNOW
    char *ufn;
#endif
    char *encodeddn = dsgw_strdup_escaped( dn );

	dsgw_send_header();
	dsgw_emits( "<HTML>" );
	dsgw_head_begin();

	dsgw_emits( "\n" 
		"<TITLE>Password Expired</TITLE>\n"
		"<SCRIPT LANGUAGE=\"JavaScript\">\n"
		"<!-- Hide from non-JavaScript browsers\n" );

	if ( encodeddn != NULL && strlen( encodeddn ) > 0 ) {
	    dsgw_emitf( "var editdesturl = '%s?passwd&dn=%s&context=%s';\n",
		    dsgw_getvp( DSGW_CGINUM_EDIT ), encodeddn, context );
	} else {
	    dsgw_emitf( "var editdesturl=null;\n" );
	}

	dsgw_emits( "function EditPassword()\n"
	    "{\n"
	    "    if ( editdesturl != null ) {\n"
	    "        top.location.href = editdesturl;\n"
	    "    } else {\n"
	    "        top.close();\n"
	    "    }\n"
	    "}\n"
	    "var contButtons = ");

	dsgw_quotation_begin (QUOTATION_JAVASCRIPT_MULTILINE);
	dsgw_form_begin ("bForm", NULL);
	dsgw_emits(
	    "\n<TABLE BORDER=2 WIDTH=100%>\n"
	    "<TD ALIGN=CENTER WIDTH=50%>\n"
	    "<INPUT TYPE=BUTTON NAME=\"contButton\""
	    "VALUE=\"");
	dsgw_emits( XP_GetClientStr( DBT_EditPassword_ ));
	dsgw_emits(
	    "\" onClick=\"EditPassword();\">\n"
	    "<TD ALIGN=CENTER WIDTH=50%%>" );
	dsgw_emit_helpbutton( "AUTHSUCCESS" );
	dsgw_emits(
	    "\n</TABLE></FORM>");
	dsgw_quotation_end(); dsgw_emits(";\n");

	dsgw_emits(
	    "var noContButtons = ");
	dsgw_quotation_begin (QUOTATION_JAVASCRIPT_MULTILINE);
	dsgw_emits( XP_GetClientStr( DBT_ToContinue_ ));
	dsgw_form_begin( "bForm", NULL );
	dsgw_emits(
	    "\n<TABLE BORDER=2 WIDTH=100%>"
	    "\n<TD ALIGN=CENTER WIDTH=50%>" );
	dsgw_emit_homebutton();
	dsgw_emits( "\n<TD ALIGN=CENTER WIDTH=50%%>" );
	dsgw_emit_helpbutton( "AUTHPROBLEM" );
	dsgw_emits(
	    "\n</TABLE></FORM>\n");
	dsgw_quotation_end(); dsgw_emits(";\n");

#ifdef NOTFORNOW
	/* ldap_dn2ufn currently gobbles up 'dc' so don't use it for */
	/* now */
	ufn = ldap_dn2ufn( dn );
#endif

	dsgw_emitf(
		"// End hiding -->\n"
		"</SCRIPT>\n"
		"</HEAD>\n<BODY %s>\n"
		"<CENTER>\n",
		dsgw_html_body_colors );
	dsgw_emitf( XP_GetClientStr( DBT_PasswordExpiredFor_ ), dn );
	dsgw_emits( "</CENTER>\n" );
	dsgw_emits( XP_GetClientStr( DBT_YourPasswordHasExpired_ ));
	dsgw_emits( XP_GetClientStr( DBT_YouMustChangeYourPasswd_ ));
	dsgw_emits( "<P>\n"
	    "<TR>\n"
	    "<SCRIPT LANGUAGE=\"JavaScript\">\n"
	    "<!-- Hide from non-JavaScript browsers\n"
	    "if ( editdesturl != null ) {\n"
	    "    document.write( contButtons );\n"
	    "} else {\n"
	    "    document.write( noContButtons );\n"
	    "}\n"
	    "// End hiding -->\n"
	    "</SCRIPT>\n"
	    "</BODY>\n</HTML>\n" );
}

/* Pulled from ldapserver/ldap/servers/slapd/time.c */

time_t
dsgw_current_time()
{
	return( time( (time_t *)0 ));
}

#define mktime_r(from) mktime (from)

time_t
dsgw_time_plus_sec (time_t l, long r)
    /* return the point in time 'r' seconds after 'l'. */
{
    /* On many (but not all) platforms this is simply l + r;
       perhaps it would be better to implement it that way. */
    struct tm t;
    if (r == 0) return l; /* performance optimization */
#ifdef _WIN32
    {
        struct tm *pt = localtime( &l );
        memcpy(&t, pt, sizeof(struct tm) );
    }
#else
    localtime_r (&l, &t);
#endif
    /* Conceptually, we want to do: t.tm_sec += r;
       but to avoid overflowing fields: */
    r += t.tm_sec;  t.tm_sec  = r % 60; r /= 60;
    r += t.tm_min;  t.tm_min  = r % 60; r /= 60;
    r += t.tm_hour; t.tm_hour = r % 24; r /= 24;
    t.tm_mday += r; /* may be > 31; mktime_r() must handle this */

    /* These constants are chosen to work when the maximum
       field values are 127 (the worst case) or more.
       Perhaps this is excessively conservative. */
    return mktime_r (&t);
}

/*
 * Function: figure_out_langwich
 *
 * Returns: nothing
 *
 * Description: figures out the language/locale that libsi18n will
 *              use. This is so that non libsi18n functions can display
 *              stuff in the same language.
 *
 * Author: RJP
 *
 */
static void
figure_out_langwich(void)
{
  char *path   = NULL;
  char *iter   = NULL;
  char *p      = NULL;
  char *before = NULL;

  /* Get a path to the html directory */
  path = dsgw_file2path( gc->gc_configdir, "dsgwfilter.conf");

  before = path;

  /* Find the lang subdirectory part */
  for ( p = ldap_utf8strtok_r( path, DSGW_PATHSEP_STR, &iter );
       p != NULL && *p != '\0' && strcmp(p, "dsgwfilter.conf") != 0;
       p = ldap_utf8strtok_r( NULL, DSGW_PATHSEP_STR, &iter )){
    before = p;
  }
  
  /* If there is one, copy it. */
  if (before != NULL && *before != '\0') {
    langwich = dsgw_ch_strdup(before);
  }
  
  iter = NULL;

  /* split off any country specification */
  ldap_utf8strtok_r( langwich, "-", &iter );
  countri = iter;

  free (path);

}

/*
 *      Accept-Language = "Accept-Language" ":"
 *                        1#( language-range [ ";" "q" "=" qvalue ] )
 *      language-range  = ( ( 1*8ALPHA *( "-" 1*8ALPHA ) ) | "*" )
 *
 *      NLS_AccLangList() assumes that "Accept-Language:" has already
 *      been stripped off. It takes as input
 * 
 *      1#( ( ( 1*8ALPHA *( "-" 1*8ALPHA ) ) | "*" ) [ ";" "q" "=" qvalue ] )
 *
 *      and returns a list of languages, ordered by qvalues, in
 *      the array NLS_ACCEPT_LANGUAGE_LIST. 
 *      
 *      If there are to many languages (>NLS_MAX_ACCEPT_LANGUAGE) the excess
 *      is ignored. If the language-range is too long (>NLS_MAX_ACCEPT_LENGTH),
 *      the language-range is ignored. In these cases, NLS_AccLangList()
 *      will quietly return, perhaps with numLang = 0. numLang is
 *      returned by the function.  
 */


size_t
AcceptLangList(const char* AcceptLanguage,
               ACCEPT_LANGUAGE_LIST AcceptLanguageList)
{
  char* input;
  char* cPtr;
  char* cPtr1;
  char* cPtr2;
  int i;
  int j;
  int countLang = 0;
  
  input = dsgw_ch_strdup(AcceptLanguage);
  if (input == (char*)NULL){
	  return 0;
  }

  cPtr1 = input-1;
  cPtr2 = input;

  /* put in standard form */
  while (*(++cPtr1)) {
    if      (isalpha(*cPtr1))  *cPtr2++ = tolower(*cPtr1); /* force lower case */
    else if (isspace(*cPtr1));                             /* ignore any space */
    else if (*cPtr1=='-')      *cPtr2++ = '_';             /* "-" -> "_"       */
    else if (*cPtr1=='*');                                 /* ignore "*"       */
    else                       *cPtr2++ = *cPtr1;          /* else unchanged   */
  }
  *cPtr2 = '\0';

  countLang = 0;

  if (strchr(input,';')) {
    /* deal with the quality values */

    float qvalue[MAX_ACCEPT_LANGUAGE];
    float qSwap;
    float bias = 0.0f;
    char* ptrLanguage[MAX_ACCEPT_LANGUAGE];
    char* ptrSwap;

    cPtr = strtok(input,",");
    while (cPtr) {
      qvalue[countLang] = 1.0f;
      if ((cPtr1 = strchr(cPtr,';'))) {
        sscanf(cPtr1,";q=%f",&qvalue[countLang]);
        *cPtr1 = '\0';
      }
      if (strlen(cPtr)<MAX_ACCEPT_LENGTH) {     /* ignore if too long */
        qvalue[countLang] -= (bias += 0.0001f); /* to insure original order */
        ptrLanguage[countLang++] = cPtr;
        if (countLang>=MAX_ACCEPT_LANGUAGE) break; /* quit if too many */
      }
      cPtr = strtok(NULL,",");
    }

    /* sort according to decending qvalue */
    /* not a very good algorithm, but count is not likely large */
    for ( i=0 ; i<countLang-1 ; i++ ) {
      for ( j=i+1 ; j<countLang ; j++ ) {
        if (qvalue[i]<qvalue[j]) {
          qSwap     = qvalue[i];
          qvalue[i] = qvalue[j];
          qvalue[j] = qSwap;
          ptrSwap        = ptrLanguage[i];
          ptrLanguage[i] = ptrLanguage[j];
          ptrLanguage[j] = ptrSwap;
        }
      }
    }
    for ( i=0 ; i<countLang ; i++ ) {
      PL_strncpyz(AcceptLanguageList[i],ptrLanguage[i],sizeof(AcceptLanguageList[i]));
    }

  } else {
    /* simple case: no quality values */

    cPtr = strtok(input,",");
    while (cPtr) {
      if (strlen(cPtr)<MAX_ACCEPT_LENGTH) {        /* ignore if too long */
        PL_strncpyz(AcceptLanguageList[countLang++],cPtr,sizeof(AcceptLanguageList[i]));
        if (countLang>=MAX_ACCEPT_LANGUAGE) break; /* quit if too many */
      }
      cPtr = strtok(NULL,",");
    }
  }

  free(input);

  return countLang;
}

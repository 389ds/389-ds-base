/** --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
  --- END COPYRIGHT BLOCK ---  */
/*
 * htmlout.c -- routines to output HTML elements -- HTTP gateway 
 */

#include "dsgw.h"
#include "dbtdsgw.h"

#define DSGW_POSTEDVARARG_NAME		"name"

static char*
dsgw_change( char *s, dsgwsubst *changes )
{
    auto dsgwsubst *ch;
    if ( changes == NULL ) return s;
    for ( ch = changes; ch; ch = ch->dsgwsubst_next ) {
	if ( strstr( s, ch->dsgwsubst_from ) ) {
	    break;
	}
    }
    if ( ch != NULL ) {
	auto char *cs = dsgw_ch_strdup( s );
	for ( ch = changes; ch; ch = ch->dsgwsubst_next ) {
	    auto const size_t from_len = strlen( ch->dsgwsubst_from );
	    auto const size_t   to_len = strlen( ch->dsgwsubst_to );
	    auto const long change_len = to_len - from_len;
	    auto char *p;
	    for ( p = cs; (p = strstr( p, ch->dsgwsubst_from )) != NULL; p += to_len ) {
		if ( change_len ) {
		    if ( change_len > 0 ) { /* allocate more space: */
			auto const size_t offset = p - cs;
			cs = dsgw_ch_realloc( cs, strlen( cs ) + change_len + 1 );
			p = cs + offset;
		    }
		    memmove( p + to_len, p + from_len, strlen( p + from_len ) + 1 );
		}
		if ( to_len != 0 ) {
		    memcpy( p, ch->dsgwsubst_to, to_len );
		}
	    }
	}
	return cs;
    }
    return s;
}

void
dsgw_HTML_emits( char *s )
{
    auto char *sc = dsgw_change( s, gc->gc_changeHTML );
    dsgw_emits( sc );
    if ( sc != s ) free( sc );
}

void
dsgw_html_begin( char *title, int titleinbody )
{
    static int	header_done = 0;

    if ( !header_done ) {
	dsgw_emits( "<HTML>" );
	dsgw_head_begin();
	dsgw_emits( "\n" );
	if ( title != NULL ) {
	    dsgw_emitf( "<TITLE>%s</TITLE>\n", title );
	}
	dsgw_emitf( "</HEAD>\n<BODY %s>\n", dsgw_html_body_colors );
    } else {
	dsgw_emits( "\n<HR>\n" );
    }

    if (( title != NULL ) && ( header_done || titleinbody )) {
	dsgw_emitf( "<CENTER><TABLE BORDER=\"2\" CELLPADDING=\"10\" WIDTH=100%%>\n"
		"<TR><TD ALIGN=\"center\" WIDTH=\"100%%\">\n<FONT SIZE=\"+2\">"
		"<B>%s</B></FONT></TD></TR></TABLE></CENTER>\n<P>\n", title );
    }

    header_done = 1;
}


void
dsgw_html_end()
{
    dsgw_emits( "</BODY></HTML>\n" );

    /* make sure everything has been written to the server before we exit */
    fflush( stdout );
    fflush( stderr );
}


/*
 * output a hypertext reference/URL:
 * if "urlprefix" != NULL, it is prepended to "url" and "url" is hex-escaped.
 * if "urlprefix" == NULL, "url" is assumed to be already escaped as needed.
 *
 * if "value" != NULL, any occurrence of "--value--" in "url" is replaced by
 *    a URL-escaped version of the actual value.
 * if "value" == NULL, no substitution is done.
 *
 * if "label" == NULL or is of zero length, the closing ">LABEL</A>" is omitted.
 */
void
dsgw_html_href( char *urlprefix, char *url, char *label, char *value,
	char *extra )
{
    char	*escaped_url, *tag = "--value--";
    char	*newlabel = NULL;
    int	freenewlabel;

    if ( urlprefix == NULL ) {
	dsgw_emits( "<A HREF=" );
	escaped_url = NULL;
    } else {
	dsgw_emitf( "<A HREF=%s", urlprefix );
	escaped_url = dsgw_ch_malloc( 3 * strlen( url ) + 1 );
	*escaped_url = '\0';
	dsgw_strcat_escaped( escaped_url, url );
	url = escaped_url;
    }

    if ( value != NULL ) {
	dsgw_substitute_and_output( url, tag, value, 1 );
    } else {
	dsgw_emits( url );
    }

    if ( extra != NULL ) {
	dsgw_emits( " " );
	if ( value != NULL ) {
	    dsgw_substitute_and_output( extra, tag, value, 1 );
	} else {
	    dsgw_emits( extra );
	}
    }

    newlabel = dsgw_strdup_with_entities( label, &freenewlabel );
    if ( newlabel != NULL && *newlabel != '\0' ) {
	dsgw_emitf( ">%s</A>\n", newlabel );
	if ( freenewlabel ) {
	    free( newlabel );
	}
    } else {
	dsgw_emits( "></A>\n" );
    }

    if ( escaped_url != NULL ) {
	free( escaped_url );
    }
}


void
dsgw_substitute_and_output( char *s, char *tag, char *value, int escape )
{
    char	*p, *escval;

    escval = NULL;

    while ( ( p = strstr( s, tag )) != NULL ) {
	if ( p > s ) {
	    dsgw_emitn( stdout, s, p - s );
	}
	if ( escape ) {
	    if ( escval == NULL ) {
		escval = dsgw_strdup_escaped( value );
	    }
	    dsgw_emits( escval );
	} else {
	    dsgw_emits( value );
	}
	
	s = p + strlen( tag );
    }

    if ( escval != NULL ) {
	free( escval );
    }

    if ( *s != '\0' ) {
	dsgw_emits( s );
    }
}


char *
dsgw_strdup_escaped( const char *s )
{
    char	*p;

    p = dsgw_ch_malloc( 3 * strlen( s ) + 1 );
    *p = '\0';
    dsgw_strcat_escaped( p, s );
    return( p );
}


/* this macro was copied from libldap/tmplout.c */

#define HREF_CHAR_ACCEPTABLE( c )	(( c >= '-' && c <= '9' ) ||	\
					 ( c >= '@' && c <= 'Z' ) ||	\
					 ( c == '_' ) ||		\
					 ( c >= 'a' && c <= 'z' ))

/* this function is copied from libldap/tmplout.c:strcat_escaped */
void
dsgw_strcat_escaped( char *s1, const char *s2 )
{
    unsigned char	*q;
    char		*p, *hexdig = "0123456789ABCDEF";

    p = s1 + strlen( s1 );
    for ( q = (unsigned char *)s2; *q != '\0'; ++q ) {
	if ( HREF_CHAR_ACCEPTABLE( *q )) {
	    *p++ = *q;
	} else {
	    *p++ = '%';
	    *p++ = hexdig[ 0x0F & ((*(unsigned char*)q) >> 4) ];
	    *p++ = hexdig[ 0x0F & *q ];
	}
    }

    *p = '\0';
}


#define DSGW_MAX_ENTITY_LEN			6	/* &quot; */
static char	*specials = "&\"<>";
static char	*entities[] = { "&amp;", "&quot;", "&lt;", "&gt;" };
static int	entitylen[] = { 5, 6, 4, 4 };

char *
dsgw_strdup_with_entities( char *s, int *madecopyp )
{
/*
 * If the UTF8 string "s" contains any HTML special characters, make a
 * duplicate where the appropriate HTML "entities" have been substituted
 * for the special chars.  For example, "<mcs@ace.com>" will be translated
 * to "&lt;mcs@ace.com&gt;".
 * 
 * If "s" does not contain any special characters, it is returned and
 *	*madecopyp is set to 0.
 * Otherwise a malloc'd string is returned and *madecopyp is set to 1.
 */
    int		spcount, idx;
    char	*p, *q, *r, *d;

    spcount = 0;
    for ( p = s; *p != '\0'; LDAP_UTF8INC( p )) {
	if ( ((*p) & 0x80) == 0 && strchr( specials, *p ) != NULL ) {
	    ++spcount;
	}
    }

    if ( spcount == 0 ) {
	*madecopyp = 0;
	return( s );
    }

    d = r = dsgw_ch_malloc( strlen( s ) + 1 + spcount * DSGW_MAX_ENTITY_LEN );
    for ( p = s; *p != '\0'; LDAP_UTF8INC( p )) {
	if ( ((*p) & 0x80) == 0 && ( q = strchr( specials, *p )) != NULL ) {
	    idx = ( q - specials );
	    memcpy( r, entities[ idx ], entitylen[ idx ] );
	    r += entitylen[ idx ];
	} else {
	    r += LDAP_UTF8COPY( r, p );
	}
    }
    *r = '\0';

    *madecopyp = 1;
    return( d );
}


void
dsgw_form_begin( const char* name, const char* format, ... )
{
    dsgw_emits ("<FORM method=POST");
    if (name) {
	dsgw_emitf (" name=\"%s\"", name);
    }
    if (format) {
	va_list argl;
	va_start (argl, format);
	dsgw_emits (" ");
	dsgw_emitfv (format, argl);
	va_end (argl);
    }
    dsgw_emits (">");
    dsgw_emitf("<INPUT type=hidden name=context value=\"%s\">", context);
    dsgw_emitf ("<INPUT type=hidden name=charset value=\"%s\">",
		(gc->gc_charset && *(gc->gc_charset)) ? gc->gc_charset : ISO_8859_1_ENCODING );
}

void
dsgw_emit_cgi_var( int argc, char **argv )
{
    char	*name, *postedvalue;

    if (( name = get_arg_by_name( DSGW_POSTEDVARARG_NAME, argc, argv ))
	    == NULL ) {
	dsgw_emitf( XP_GetClientStr(DBT_missingS_1), DSGW_POSTEDVARARG_NAME );
    } else if (( postedvalue = dsgw_get_cgi_var( name, DSGW_CGIVAR_OPTIONAL ))
	    != NULL ) {
	dsgw_emits( postedvalue );
    }
}

void
dsgw_emit_button( int argc, char **argv, const char* format, ... )
{
    auto char *name = get_arg_by_name( DSGW_ARG_BUTTON_NAME, argc, argv );
    auto char *label = get_arg_by_name( DSGW_ARG_BUTTON_LABEL, argc, argv );

    if ( !label ) label = XP_GetClientStr( DBT_closeWindow_3 );

    dsgw_emitf( "<INPUT TYPE=\"button\" VALUE=\"%s\"", label );
    if ( name ) dsgw_emitf( " NAME=\"%s\"", name );
    if ( format ) {
	va_list argl;
	va_start( argl, format );
	dsgw_emits( " " );
	dsgw_emitfv( format, argl );
	va_end( argl );
    }
    dsgw_emits( ">" );
}

void
dsgw_emit_alertForm ()
{
    dsgw_form_begin ("alertForm", "action=\"%s\" target=alertWindow",
		     DSGW_URLPREFIX_MAIN_HTTP "alert.html");
    dsgw_emitf ("<INPUT TYPE=hidden NAME=TITLE VALUE=\"%s\">", XP_GetClientStr(DBT_alertTitle_));
    dsgw_emits ("<INPUT TYPE=hidden NAME=MSG VALUE=\"\">"
		"</FORM>\n");
}

void
dsgw_emit_confirmForm ()
{
    dsgw_form_begin ("confirmForm", "action=\"%s\" target=confirmWindow",
		     DSGW_URLPREFIX_MAIN_HTTP "confirm.html");
    dsgw_emitf ("<INPUT TYPE=hidden NAME=TITLE VALUE=\"%s\">", XP_GetClientStr(DBT_confirmTitle_));
    dsgw_emits ("<INPUT TYPE=hidden NAME=MSG VALUE=\"\">"
		"<INPUT TYPE=hidden NAME=YES VALUE=\"\">"
		"<INPUT TYPE=hidden NAME=NO VALUE=\"\">"
		"</FORM>\n");
}

static const char*
defaultWindowOptions = "width=350,height=130,resizable";

void
dsgw_emit_alert (const char* frame, const char* windowOptions, const char* format, ...)
{
    if (!windowOptions) windowOptions = defaultWindowOptions;
    dsgw_emits ("	var aw = window.open(");
    dsgw_emits ("''");
/*  dsgw_quote_emits (QUOTATION_JAVASCRIPT, DSGW_URLPREFIX_MAIN_HTTP "emptyFrame.html"); */
    dsgw_emits (               ", 'alertWindow', ");
    dsgw_quote_emits (QUOTATION_JAVASCRIPT, windowOptions);
    dsgw_emits (               ");\n"
		"	aw.focus();\n"
		"	window."); /* Navigator 3 needs this */
    if (frame) dsgw_emitf ("%s.", frame);
    dsgw_emits (           "document.alertForm.MSG.value =");
    dsgw_quotation_begin (QUOTATION_JAVASCRIPT);
    if (format) {
	va_list argl;
	va_start (argl, format);
	dsgw_emitfv (format, argl);
	va_end (argl);
    }
    dsgw_quotation_end();
    dsgw_emits (               ";\n"
		"	window.");
    if (frame) dsgw_emitf ("%s.", frame);
    dsgw_emits ("document.alertForm.submit();\n");
}

void
dsgw_emit_confirm (const char* frame, const char* yes, const char* no,
		   const char* windowOptions, int enquote, const char* format, ...)
{
    if (!windowOptions) windowOptions = defaultWindowOptions;
    dsgw_emits ("	cw = window.open ('', 'confirmWindow', ");
    dsgw_quote_emits (QUOTATION_JAVASCRIPT, windowOptions);
    dsgw_emits (               ");\n"
		"	cw.focus();\n"
		"	if (cw.opener == null) cw.opener = self;\n" /* Navigator 2 needs this */
		"	window."); /* Navigator 3 needs this */
    if (frame) dsgw_emitf ("%s.", frame);
    dsgw_emits (           "document.confirmForm.MSG.value = ");
    if (enquote) dsgw_quotation_begin (QUOTATION_JAVASCRIPT);
    if (format) {
	va_list argl;
	va_start (argl, format);
	dsgw_emitfv (format, argl);
	va_end (argl);
    }
    if (enquote) dsgw_quotation_end();
    dsgw_emits (           ";\n");

    dsgw_emits ("	window.");
    if (frame) dsgw_emitf ("%s.", frame);
    dsgw_emits (           "document.confirmForm.YES.value = ");
    dsgw_quote_emits (QUOTATION_JAVASCRIPT, yes ? yes : "");
    dsgw_emits (           ";\n");

    dsgw_emits ("	window.");
    if (frame) dsgw_emitf ("%s.", frame);
    dsgw_emits (           "document.confirmForm.NO.value = ");
    dsgw_quote_emits (QUOTATION_JAVASCRIPT, no ? no : "");
    dsgw_emits (           ";\n");

    dsgw_emits ("	window.");
    if (frame) dsgw_emitf ("%s.", frame);
    dsgw_emits (           "document.confirmForm.submit();\n");
}

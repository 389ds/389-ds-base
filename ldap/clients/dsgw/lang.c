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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
  --- END COPYRIGHT BLOCK ---  */
/* 
 * Convert a document from ../html, or redirect the server to it.
 */

#include "dsgw.h"
#include "dbtdsgw.h"

#ifdef XP_WIN
#define PATH_SLASH "\\"
#else
#define PATH_SLASH "/"
#endif

static int
doc_is_UTF_8 (const char* docname)
{
    static const char* suffixes [] = {".html", ".htm", NULL};
    const size_t doclen = strlen (docname);
    const char** suf = suffixes;
    for (suf = suffixes; *suf; ++suf) {
	const size_t suflen = strlen (*suf);
	if (doclen >= suflen && !strcasecmp (*suf, docname + doclen - suflen)) {
	    return 1;
	}
    }
    return 0;
}

static const char*
skip_prefix (const char* prefix, const char* s)
{
    const size_t prelen = strlen (prefix);
    if (!strncmp (prefix, s, prelen)) return s + prelen;
    return s;
}

static int
doc_convert( FILE** fpp, char* stop_at_directive, int erropts )
{
    char	**argv, line[ BIG_LINE ];
    int		argc;
 
    while ( dsgw_next_html_line( *fpp, line ))  {
	if ( dsgw_parse_line( line, &argc, &argv, 0, dsgw_simple_cond_is_true,
		NULL )) {
	    if ( stop_at_directive != NULL &&
		    dsgw_directive_is( line, stop_at_directive )) {
		return( 0 );

	    } else if ( dsgw_directive_is( line, DRCT_HEAD )) {
		dsgw_head_begin();
		dsgw_emits ("\n");

	    } else if ( dsgw_directive_is( line, DRCT_DS_POSTEDVALUE )) {
		dsgw_emit_cgi_var (argc, argv);

	    } else if ( dsgw_directive_is( line, DRCT_DS_CLOSEBUTTON )) {
		dsgw_emit_button (argc, argv, "onClick=\"top.close()\"");

	    } else if ( dsgw_directive_is( line, "DS_CONFIRM_SCRIPT" )) {
		{
		    auto char* yes = dsgw_get_cgi_var ("YES", DSGW_CGIVAR_OPTIONAL);
		    auto char* no  = dsgw_get_cgi_var ("NO",  DSGW_CGIVAR_OPTIONAL);
		    dsgw_emitf ("<SCRIPT LANGUAGE=JavaScript><!--\n"
				"function OK() {\n");
		    if (yes) dsgw_emitf ("    %s\n", yes);
		    dsgw_emits ("    top.close();\n"
				"}\n"
				"\n"
				"function Cancel() {\n");
		    if (no) dsgw_emitf ("    %s\n", no);
		    dsgw_emits ("    top.close();\n"
				"}\n"
				"// -->\n"
				"</SCRIPT>\n");
		}

	    } else if ( dsgw_directive_is( line, "DS_CONFIRM_BUTTON_OK" )) {
		dsgw_emitf ("<INPUT TYPE=BUTTON VALUE=\"%s\" onClick=\"parent.OK()\">\n",
			    XP_GetClientStr(DBT_ok_2));

	    } else if ( dsgw_directive_is( line, "DS_CONFIRM_BUTTON_CANCEL" )) {
		dsgw_emitf ("<INPUT TYPE=BUTTON VALUE=\"%s\" onClick=\"parent.Cancel()\">\n",
			    XP_GetClientStr(DBT_cancel_2));

	    } else {
		dsgw_emits (line);
	    }
	}
    }
    fclose( *fpp );
    *fpp = NULL;
    return( 0 );
}

int
main( int argc, char *argv[]
#ifdef DSGW_DEBUG
     , char *env[]
#endif
     )
{
    /*static char* docdir = ".." PATH_SLASH "html" PATH_SLASH;*/
    static char* docdir = NULL;
    static char* helpdir = NULL;
    char* docname = NULL;
    char* tfname;
    int result = 0;
    char  *qs = NULL; 
    int  manual_file = 0; /* Flag: is the file a documentation file? */

    /* Parse out the file=blah.html */
    if (( qs = getenv( "QUERY_STRING" )) != NULL && *qs != '\0' ) {
	/* parse the query string: */
	auto char *p, *iter = NULL;
	qs = dsgw_ch_strdup( qs );
	
	for ( p = ldap_utf8strtok_r( qs,   "&", &iter ); p != NULL;
	      p = ldap_utf8strtok_r( NULL, "&", &iter )) {
	    
	    /*
	     * Get the conf file name. It'll be translated
	     * into /dsgw/context/CONTEXT.conf if
	     * CONTEXT is all alphanumeric (no slahes,
	     * or dots). CONTEXT is passed into the cgi.
	     * if context=CONTEXT is not there, or PATH_INFO
	     * was used, then use dsgw.conf
	     */
	    if ( !strncasecmp( p, "context=", 8 )) {
		context = dsgw_ch_strdup( p + 8 );
		dsgw_form_unescape( context );
		continue;
	    }
	    
	    
	    /*Get the filename and check it for naughtiness -RJP*/
	    if ( !strncasecmp( p, "file=", 5 )) {
		
		/*If there is no file specified, go with index.html*/
		if (strlen(p) == 5) {
		    docname = dsgw_ch_strdup("index.html");
		} else {
		    docname = dsgw_ch_strdup( p + 5 );
		    dsgw_form_unescape( docname );
		}
		
		
		/*If we're handling a help page, forgo the filename check*/
		if ( strlen( docname ) > DSGW_MANUALSHORTCUT_LEN && 
		     strncmp( docname, DSGW_MANUALSHORTCUT, 
			      DSGW_MANUALSHORTCUT_LEN ) == 0 ) {
		    manual_file = 1;
		}
		
		/*
		 * Make sure the person isn't trying to get 
		 * some file not in the gateway.
		 */
		if (manual_file == 0 && !dsgw_valid_docname(docname)) {
		    dsgw_error( DSGW_ERR_BADFILEPATH, docname, 
				DSGW_ERROPT_EXIT, 0, NULL );
		}
		continue;
	    }
	    
	    
	}
	
	free( qs ); qs = NULL;
    }
    
    (void)dsgw_init( argc, argv, DSGW_METHOD_GET | DSGW_METHOD_POST );
    docdir = dsgw_get_docdir();
    
    /*If there is no docname, default to index.html*/
    if (docname == NULL) {
      docname = dsgw_ch_strdup("index.html");
    }

    if (!strcmp (docname, "/")) {
	printf( "Location: %s?context=%s\n\n", 
		dsgw_getvp( DSGW_CGINUM_SEARCH ), context );
	return( result );
    } else {
	char* p;
	if (*docname == '/') ++docname;
	docname = dsgw_ch_strdup( docname );
	if (( p = strrchr( docname, '&' )) != NULL ) {
	    *p++ = '\0';
	    if ( strncasecmp( p, "info=", 5 ) == 0 ) {
		dsgw_last_op_info = dsgw_ch_strdup( p + 5 );
		dsgw_form_unescape( dsgw_last_op_info );
	    }
	}
    }
    
    if (manual_file) {
	helpdir = dsgw_file2path ( DSGW_MANROOT, "slapd/gw/manual/" );
	tfname = (char *)dsgw_ch_malloc( strlen( helpdir ) +
				strlen( docname + DSGW_MANUALSHORTCUT_LEN ) +
				1 );
        sprintf( tfname, "%s%s",
			 helpdir, docname + DSGW_MANUALSHORTCUT_LEN);
	free( helpdir );

    } else {
	tfname = dsgw_file2path (docdir, docname);
    }

    if ( ! doc_is_UTF_8 (tfname)) { /* Redirect the Web server: */
	printf ("Location: %s%s%s\n\n", 
		getenv("SERVER_URL"), gc->gc_gwnametrans, skip_prefix (docdir, tfname));
	/* It's tempting to also redirect if is_UTF_8(gc->gc_charset).
	   But it would be wrong: the Web server would transmit an
	   HTTP Content-type with no charset parameter.  The header
	   must include ";charset=UTF-8".  So we transmit it:
	*/
    } else { /* Transmit the document: */
	const int erropts = DSGW_ERROPT_EXIT;
	auto FILE* docfile;

	dsgw_send_header();
#ifdef DSGW_DEBUG
	dsgw_logstringarray( "env", env ); 
#endif
	if ((docfile = fopen(tfname, "r")) == NULL) {
	    dsgw_error( DSGW_ERR_OPENHTMLFILE, tfname, erropts, 0, NULL );
	    return( -1 );
	}
	result = doc_convert( &docfile, NULL, erropts );
    }
/*
 * XXXmcs: the following free() causes a crash on NT... so don't do it!
 */
#if 0
	free( tfname );
#endif

    return result;
}

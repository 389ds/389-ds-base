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
 * search.c -- CGI program to generate smart search form -- HTTP gateway
 */

#include "dsgw.h"
#include "dbtdsgw.h"
static void get_request(char *docname);
static void do_searchtype_popup( struct ldap_searchobj *sop );


int main( argc, argv, env )
    int		argc;
    char	*argv[];
#ifdef DSGW_DEBUG
    char	*env[];
#endif
{
    auto int reqmethod; 
    char *docname = NULL;
    char *qs = NULL;
    
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
		docname = dsgw_ch_strdup( p + 5 );
		dsgw_form_unescape( docname );
		
		/*
		 * Make sure the person isn't trying to get 
		 * some file not in the gateway.
		 */
		if (! dsgw_valid_docname(docname)) {
		    dsgw_error( DSGW_ERR_BADFILEPATH, docname, 
				DSGW_ERROPT_EXIT, 0, NULL );
		}
		continue;
	    }
	    
	    
	}
	
	free( qs ); qs = NULL;
    }
    
    
    reqmethod = dsgw_init( argc, argv, DSGW_METHOD_GET );
    dsgw_send_header();

#ifdef DSGW_DEBUG
    dsgw_logstringarray( "env", env ); 
{
    char	buf[ 1024 ];
    getcwd( buf, sizeof(buf));
    dsgw_log( "cwd: \"%s\"\n", buf );
}
#endif

    if ( reqmethod == DSGW_METHOD_GET ) {
	get_request(docname);
    }
    exit( 0 );
}


static void
get_request(char *docname)
{

    auto char* filename = NULL;
    auto struct ldap_searchobj* sop = NULL;

    if (docname != NULL && *docname == '/') {
	docname++;
    }

    if ( docname == NULL || *docname == '\0' ) {
	filename = "search.html";
    } else if ( !strcmp( docname, "string" )) {
	filename = "searchString.html";
	dsgw_init_searchprefs( &sop );
    }
    if (filename) {
	auto FILE* html = dsgw_open_html_file( filename, DSGW_ERROPT_EXIT );
	auto char line[ BIG_LINE ];
	auto int argc;
	auto char **argv;

	while ( dsgw_next_html_line( html, line )) {
	    if ( dsgw_parse_line( line, &argc, &argv, 0, dsgw_simple_cond_is_true, NULL )) {
		if ( dsgw_directive_is( line, "HEAD" )) {
		    dsgw_head_begin();
		    dsgw_emits ("\n");
		} else if ( dsgw_directive_is( line, "DS_SEARCH_SCRIPT" )) {
		    dsgw_emits ("<SCRIPT LANGUAGE=\"JavaScript\">\n"
				"<!-- Hide from non-JavaScript-capable browsers\n"
				"\n"
				"function validate(sform)\n"
				"{\n"
				"    if (sform.searchstring.value == '') {\n");
/* 
 * It would have been nice to detect when the user pressed return without
 * typing anything into the searchstring area, but on Navigator 2.x, the
 * form variable's value seems to get set *after* the onSubmit handler
 * executes, which is unfortunate.
 */
		    dsgw_emit_alert ("searchFrame", NULL, /* "%s<br>(search base %s)", */
				     XP_GetClientStr (DBT_youDidNotSupplyASearchString_),
				     gc->gc_ldapsearchbase);
		    dsgw_emits ("	return false;\n"
				"    }\n"
				"    sform.searchstring.select();\n"
				"    sform.searchstring.focus();\n"
				"    return true;\n"
				"}\n"
				"\n"
				"function init()\n"
				"{}\n"
				"// End hiding -->\n"
				"</SCRIPT>\n");

		} else if ( dsgw_directive_is( line, "DS_SEARCH_BODY" )) {
		    dsgw_emitf ("<BODY onLoad=\""
			    "document.searchForm.searchstring.select();"
			    "document.searchForm.searchstring.focus();\" %s>\n",
			    dsgw_html_body_colors );
		    dsgw_emit_alertForm();

		} else if ( dsgw_directive_is( line, "DS_SEARCH_FORM" )) {
		    dsgw_form_begin ("searchForm", "action=\"%s\" %s %s",
				     dsgw_getvp( DSGW_CGINUM_DOSEARCH ),
				     "onSubmit=\"return top.validate(this)\"",
				     argc > 0 ? argv[0] : "");
		    dsgw_emitf ("\n"
				"<INPUT TYPE=hidden NAME=\"mode\" VALUE=\"smart\">\n"
				"<INPUT TYPE=hidden NAME=\"base\" VALUE=\"%s\">\n"
				"<INPUT TYPE=hidden NAME=\"ldapserver\" VALUE=\"%s\">\n"
				"<INPUT TYPE=hidden NAME=\"ldapport\" VALUE=\"%d\">\n",
				gc->gc_ldapsearchbase, gc->gc_ldapserver, gc->gc_ldapport );
		} else if ( dsgw_directive_is( line, "DS_SEARCH_BASE" )) {
#ifdef NOTFORNOW
		/* ldap_dn2ufn currently gobbles up 'dc' so don't use */
		/* it for now */
		    auto char* ufn = ldap_dn2ufn( gc->gc_ldapsearchbase );
		    dsgw_emits( ufn );
		    free( ufn );
#else
	            dsgw_emits( gc->gc_ldapsearchbase );
#endif
		} else if ( dsgw_directive_is( line, "DS_SEARCH_TYPE" )) {
		    do_searchtype_popup( sop );
		} else if ( dsgw_directive_is( line, "DS_HELP_BUTTON" )) {
		    dsgw_emit_helpbutton (argc > 0 ? argv[0] : "");
		} else {
		    dsgw_emits (line);
		}
		dsgw_argv_free( argv );
	    }
	}
	fclose (html);
    }
}


static void
do_searchtype_popup(
struct ldap_searchobj *sop
)
{
    int first = 1;
    struct ldap_searchobj *so;

    dsgw_emits( "<SELECT NAME=\"type\">\n" );
    for ( so = ldap_first_searchobj( sop ); so != NULL;
	  so = ldap_next_searchobj( sop, so ), first = 0) {
	/* Skip any marked "internal-only" */
	if ( LDAP_IS_SEARCHOBJ_OPTION_SET( so, LDAP_SEARCHOBJ_OPT_INTERNAL )) {
	    continue;
	}
	dsgw_emitf( "<OPTION%s value=\"%s\">%s</OPTION>\n",
		   first ? " selected" : "",
		   so->so_objtypeprompt,
		   dsgw_get_translation( so->so_objtypeprompt ));
    }
    dsgw_emits( "</SELECT>\n" );
}

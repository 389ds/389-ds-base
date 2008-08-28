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
 * newentry.c -- CGI program to generate newentry form -- HTTP gateway
 */
#include "dsgw.h"
#include "dbtdsgw.h"

static void emit_file(char* filename, dsgwnewtype* entType);

#if 0   /* unused */
static void
unquote_emits(char* s)
{
    dsgw_quotation_end();
    dsgw_emits (s);
    dsgw_quotation_begin (QUOTATION_JAVASCRIPT_MULTILINE);
}

static void
quote_emit_file(char* filename)
{
    dsgw_quotation_begin (QUOTATION_JAVASCRIPT_MULTILINE);
    emit_file (filename, NULL);
    dsgw_quotation_end();
}
#endif

static void
emit_file (char* filename, dsgwnewtype* entType)
{
    auto FILE* html = dsgw_open_html_file( filename, DSGW_ERROPT_EXIT );
    auto char line[ BIG_LINE ];
    auto int argc;
    auto char **argv;
    char *deleteme = NULL;

    while ( dsgw_next_html_line( html, line )) {
	if ( dsgw_parse_line( line, &argc, &argv, 0, dsgw_simple_cond_is_true, NULL )) {
	    if ( dsgw_directive_is( line, DRCT_HEAD )) {
		dsgw_head_begin();
		dsgw_emits ("\n");

	    } else if ( dsgw_directive_is( line, "DS_NEWENTRY_SCRIPT" )) {
		dsgw_emits ("<SCRIPT LANGUAGE=\"JavaScript\">\n"
			    "<!-- Hide from non-JavaScript-capable browsers\n"
			    "var selectedType = -1;\n"
			    "\n"
			    "function typeChange(selectType)\n"
			    "{\n"
			    "    var newType = selectType.selectedIndex;\n"
			    "    if ( newType != selectedType ) {\n"
			    "	selectedType = newType;\n"
			    "	newentryNameFrame.location.href = '"
		            DSGW_URLPREFIX_CGI_HTTP
			    "newentry?context=");
		dsgw_emits(context);
		dsgw_emits( "&file=name&etype=' +\n"
			    "	    escape (selectType.options[newType].value);\n"
			    "    }\n"
			    "}\n"
			    "\n"
			    "var previousLocation = '';\n"
			    "var locationChangedRecently = false;\n"
			    "\n"
			    "function locationChange(nameForm)\n"
			    "{\n"
			    "    var location = nameForm.selectLocation.options[nameForm.selectLocation.selectedIndex].value;\n"
			    "    if ( location != previousLocation ) {\n"
			    "	if ( nameForm.dnsuffix != null ) {\n"
			    "	    if ( location != '' ) {\n"
			    "		nameForm.dnsuffix.blur();\n"
			    "		nameForm.dnsuffix.value = '';\n"
			    "		// In Navigator for Macintosh, the preceding code\n"
			    "		// causes a subsequent focus event in dnsuffix.\n"
			    "		// Prevent dnsuffixFocus from acting on it:\n"
			    "		locationChangedRecently = true;\n"
			    "		setTimeout ('locationChangedRecently = false', 100);\n"
			    "	    } else {\n"
			    "		nameForm.dnsuffix.value = previousLocation;\n"
			    "		nameForm.dnsuffix.focus();\n"
			    "		nameForm.dnsuffix.select();\n"
			    "	    }\n"
			    "	}\n"
			    "	previousLocation = location;\n"
			    "    }\n"
			    "}\n"
			    "\n"
			    "function dnsuffixFocus(nameForm)\n"
			    "{\n"
			    "    var location = nameForm.selectLocation.options[nameForm.selectLocation.selectedIndex].value;\n"
			    "    if ( location != '' && ( ! locationChangedRecently )) {\n"
			    "	if ( nameForm.dnsuffix.value == '' ) {\n"
			    "	    nameForm.dnsuffix.value = location;\n"
			    "	    setTimeout ('newentryNameFrame.document.nameForm.dnsuffix.select()', 75);\n"
			    "	    // This is not done immediately, to avoid interference from mouse-up.\n"
			    "	}\n"
			    "	for ( i = 0; i < nameForm.selectLocation.length; i++ ) {\n"
			    "	    if ( nameForm.selectLocation.options[i].value == '' ) {\n"
			    "		previousLocation = '';\n"
			    "		nameForm.selectLocation.selectedIndex = i;\n"
			    "		break;\n"
			    "	    }\n"
			    "	}\n"
			    "    }\n"
			    "}\n"
			    "\n"
			    "function submitNameForm(nameForm)\n"
			    "{\n"
			    "    if ( nameForm.entryname.value == '' ) {\n");
		deleteme =  XP_GetClientStr (DBT_enterNameForNewEntry_);

		dsgw_emit_alert ("newentryNameFrame", "width=400,height=130,resizable",
				 XP_GetClientStr (DBT_enterNameForNewEntry_));
		dsgw_emits ("	return false;\n"
			    "    } else if ( nameForm.selectLocation.options[nameForm.selectLocation.selectedIndex].value == '' &&\n"
			    "               ( nameForm.dnsuffix == null ||\n"
			    "                 nameForm.dnsuffix.value == '' )) {\n");
		dsgw_emit_alert ("newentryNameFrame", "width=400,height=130,resizable",
				 XP_GetClientStr (DBT_enterLocationForNewEntry_));
		dsgw_emits ("	return false;\n"
			    "    } else {\n"
			    "     open('', 'NewEntryWindow');\n"			    
			    "    }\n"
			    "    return true;\n"
			    "}\n"
			    "\n"
			    "function init()\n"
			    "{\n"
			    "}\n"
			    "\n"
			    "// end hiding -->\n"
			    "</SCRIPT>\n");

	    } else if ( dsgw_directive_is( line, "DS_NEWENTRY_TYPE_BODY" )) {
		dsgw_emitf ("<BODY %s>\n",
			    dsgw_html_body_colors );

	    } else if ( dsgw_directive_is( line, "DS_NEWENTRY_TYPE_FORM" )) {
		dsgw_form_begin ("typeForm", NULL);
		dsgw_emits ("\n");

	    } else if ( dsgw_directive_is( line, "DS_NEWENTRY_TYPE_SELECT" )) {
		auto dsgwnewtype* ntp;
		dsgw_emits ("<SELECT NAME=\"selectType\" onChange=\"parent.typeChange(this)\">\n");
		for (ntp = gc->gc_newentrytypes; ntp; ntp = ntp->dsnt_next) {
		    dsgw_emitf ("<OPTION VALUE=\"%s\">%s</OPTION>\n",
				ntp->dsnt_template ? ntp->dsnt_template : "",
				ntp->dsnt_fullname ? ntp->dsnt_fullname : "");
		}
		dsgw_emits ("</SELECT>\n" );

	    } else if ( dsgw_directive_is( line, "DS_NEWENTRY_NAME_BODY" )) {
		dsgw_emits ("<BODY onLoad=\"");
		if (entType && entType->dsnt_loccount) {
		    dsgw_emits ("parent.locationChange(document.nameForm);");
		}
		dsgw_emitf ("document.nameForm.entryname.focus()\" %s>\n",
			    dsgw_html_body_colors );
		dsgw_emit_alertForm();

	    } else if ( dsgw_directive_is( line, "DS_NEWENTRY_NAME_FORM" )) {
		dsgw_form_begin ("nameForm", "action=\"" DSGW_URLPREFIX_CGI_HTTP "newentry\""
				 " target=NewEntryWindow"
				 " onSubmit=\"return parent.submitNameForm(this)\"");
		dsgw_emits ("\n");

		if (entType) {
		    if (entType->dsnt_rdnattr) {
			dsgw_emitf ("<INPUT TYPE=\"hidden\" NAME=\"rdntag\" VALUE=\"%s\">\n",
				    entType->dsnt_rdnattr);
		    }
		    if (entType->dsnt_template) {
			dsgw_emitf ("<INPUT TYPE=\"hidden\" NAME=\"entrytype\" VALUE=\"%s\">\n",
				    entType->dsnt_template);
		    }
		}

	    } else if ( dsgw_directive_is( line, "DS_NEWENTRY_LOCATION_BEGIN" )) {
		if ( ! (entType && entType->dsnt_loccount)) {
		    while ( dsgw_next_html_line( html, line )) {
			if ( dsgw_parse_line( line, &argc, &argv, 1, dsgw_simple_cond_is_true, NULL )) {
			    if ( dsgw_directive_is( line, "DS_NEWENTRY_LOCATION_END" )) {
				break;
			    }
			}
		    }
		}

	    } else if ( dsgw_directive_is( line, "DS_NEWENTRY_LOCATION_SELECT" )) {
		dsgw_emits ("<SELECT NAME=\"selectLocation\""
			    " onChange=\"parent.locationChange(this.form)\">\n");
		if (entType) {
		    auto dsgwloc* locarray = gc->gc_newentrylocs;
		    auto const int loccount = gc->gc_newentryloccount;
		    auto int j;
		    for ( j = 0; j < entType->dsnt_loccount; ++j ) {
			auto const int i = entType->dsnt_locations[j];
			if (i < loccount) {
			    dsgw_emits ("<OPTION VALUE=");
			    dsgw_emitf ("\"%s\"", locarray[i].dsloc_dnsuffix); /* XXX should escape '"' in dnsuffix */
			    dsgw_emitf (">%s</OPTION>\n", locarray[i].dsloc_fullname);
			}
		    }
		}

	    } else if ( dsgw_directive_is( line, "DS_NEWENTRY_LOCATION_END" )) {

	    } else if ( dsgw_directive_is( line, "EVALUATE" )) {
		if (entType) {
		    auto int i;
		    for (i = 0; i < argc; ++i) {
			if (!strcmp (argv[i], "entType.fullname")) {
			    if (entType->dsnt_fullname) dsgw_emits (entType->dsnt_fullname);
			} else if (!strcmp (argv[i], "entType.rdnattr")) {
			    if (entType->dsnt_rdnattr) dsgw_emits (entType->dsnt_rdnattr);
			} else if (!strcmp (argv[i], "entType.template")) {
			    if (entType->dsnt_template) dsgw_emits (entType->dsnt_template);
			}
		    }
		}

	    } else if ( dsgw_directive_is( line, "DS_HELP_BUTTON" ) && argc > 0) {
		dsgw_emit_helpbutton (argv[0]);
	    } else {
		dsgw_emits (line);
	    }
	    dsgw_argv_free( argv );
	}
    }
    fclose (html);
}

static char*
compute_newurl()
{
    auto char* entryType = dsgw_get_cgi_var( "entrytype", DSGW_CGIVAR_REQUIRED );
    auto char* entryName = dsgw_get_cgi_var( "entryname", DSGW_CGIVAR_REQUIRED );
    auto char* rdnTag    = dsgw_get_cgi_var( "rdntag",    DSGW_CGIVAR_REQUIRED );
    auto char* dnSuffix  = dsgw_get_cgi_var( "selectLocation", DSGW_CGIVAR_OPTIONAL );
    auto size_t entryTypeLen = strlen (entryType);
    auto size_t entryNameLen = strlen (entryName);
    auto size_t rdnTagLen    = strlen (rdnTag);
    auto size_t dnSuffixLen;
    auto char* dn;
    auto char* newurl = NULL;

    if (!dnSuffix || !*dnSuffix) {
	dnSuffix = dsgw_get_cgi_var( "dnsuffix",  DSGW_CGIVAR_REQUIRED );
    }
    dnSuffixLen = strlen (dnSuffix);
    dn = dsgw_ch_malloc (rdnTagLen + 1 + entryNameLen + 2 + 1 + dnSuffixLen + 1);
    memcpy (dn, rdnTag, rdnTagLen + 1);
    strcat (dn, "=");
    if ( strchr (entryName, ',') || strchr (entryName, ';') ) {
	strcat (dn, "\"");
	strcat (dn, entryName);
	strcat (dn, "\"");
    } else {
	strcat (dn, entryName);
    } 
    strcat (dn, ",");
    strcat (dn, dnSuffix);
    {
	auto char* edn = dsgw_strdup_escaped (dn);
	auto const char* const prefix = DSGW_URLPREFIX_CGI_HTTP "edit?";
	auto const char* const suffix = "&ADD";
	auto const size_t ednLen = strlen (edn);
	auto const size_t prefixLen = strlen (prefix);
	auto const size_t suffixLen = strlen (suffix);
	auto const size_t contextLen = strlen (context) + 9;

	newurl = dsgw_ch_malloc (prefixLen + entryTypeLen + contextLen + suffixLen + 4 + ednLen + 1);

	memcpy (newurl, prefix, prefixLen + 1);
	strcat (newurl, entryType);
	strcat (newurl, "&context=");
	strcat (newurl, context);
	strcat (newurl, suffix);
	strcat (newurl, "&dn=");
	strcat (newurl, edn);
	free (edn);
    }
    free (dn);
    return newurl;
}

static int
client_is_authenticated()
{
    auto char* cookie = dsgw_get_auth_cookie();
    auto char* rndstr = NULL;
    auto char* dn = NULL;
    auto int answer = 0;
    if (cookie == NULL) return 0;
    if (dsgw_parse_cookie (cookie, &rndstr, &dn) == 0) {
	if (dn) {
	    answer = 1;
	    free (dn);
	}
	if (rndstr) free (rndstr);
    }
    free (cookie);
    return answer;
}

static dsgwnewtype*
find_entryType (char* query)
{
    auto dsgwnewtype* ntp = gc->gc_newentrytypes;
    if (query && *query) {
	auto char* template = dsgw_ch_strdup (query);
	dsgw_form_unescape (template);
	for ( ; ntp; ntp = ntp->dsnt_next) {
	    if (ntp->dsnt_template && !strcmp (ntp->dsnt_template, template)) {
		break;
	    }
	}
	free (template);
    }
    return ntp;
}

static void
get_request(char *docname, char *etype)
{
    if ( docname == NULL || *docname == '\0' ) {
	emit_file ("newentry.html", NULL);
    } else if ( !strcmp( docname, "type" )) {
	emit_file ("newentryType.html", NULL);
    } else if ( !strcmp( docname, "name" )) {
	/*emit_file ("newentryName.html", find_entryType (getenv ("QUERY_STRING")));*/
	emit_file ("newentryName.html", find_entryType (etype));
    }
}

static void
post_request()
{
    auto char* newurl = compute_newurl();
    if (client_is_authenticated()) {
	/* Direct the client to GET newurl */
	dsgw_emits ("<HTML>" );
	dsgw_head_begin();
	dsgw_emitf ("\n<TITLE>%s</TITLE>\n", XP_GetClientStr (DBT_titleNewEntry_));
	dsgw_emits ("</HEAD>\n"
		    "<FRAMESET ROWS=*,1>\n");
	dsgw_emitf ("    <FRAME SRC=\"%s\" NORESIZE>\n", newurl);
	dsgw_emits ("</FRAMESET>\n"
		    "</HTML>\n");
	/* It's tempting to use server redirection, like this:
	   printf ("Location: %s\n\n", newurl);
	   ... but it won't work, because we're handling a POST,
	   and the client should GET newurl.
	*/
    } else {
#ifdef DSGW_DEBUG
	dsgw_log ("dsgw_emit_auth_dest (NULL, %s)\n",
		  newurl ? newurl : "NULL");
#endif
	dsgw_emit_auth_dest (NULL, newurl);
    }
    if (newurl) free (newurl);
}

int
main( argc, argv, env )
    int		argc;
    char	*argv[];
#ifdef DSGW_DEBUG
    char	*env[];
#endif
{
    auto int         reqmethod;
    char            *qs = NULL;
    char            *docname = NULL;
    char            *etype = NULL;

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
	  	context = dsgw_form_unescape_url_escape_html( p + 8 );
		continue;
	    }
	    
	    /*
	     * file will be either "name", "type", or nothing.
	     * It'll be mapped into an html file in get_request
	     */
	    if ( !strncasecmp( p, "file=", 5 )) {
	  	docname = dsgw_form_unescape_url_escape_html( p + 5 );
		
		continue;
	    }
	    
	    /* etype will be ntgroup, or person, etc */
	    if ( !strncasecmp( p, "etype=", 6 )) {
	  	etype = dsgw_form_unescape_url_escape_html( p + 6 );
		
		continue;
	    }
	}
	free( qs ); qs = NULL;
    }
    
    if (docname != NULL && *docname == '/') {
      docname++;
    }
    
    reqmethod = dsgw_init( argc, argv, DSGW_METHOD_POST | DSGW_METHOD_GET);
    dsgw_send_header();
#ifdef DSGW_DEBUG
    dsgw_logstringarray( "env", env ); 
#endif

    if ( reqmethod == DSGW_METHOD_GET ) {
	get_request(docname, etype);
    } else {
	post_request();
    }
    exit( 0 );
}

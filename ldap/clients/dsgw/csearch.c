/** --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
  --- END COPYRIGHT BLOCK ---  */
/*
 * csearch.c -- CGI program to generate complex search form -- HTTP gateway
 */

#include "dsgw.h"
#include "dbtdsgw.h"

static void get_request(char *fname);
static void emit_file(char* filename, struct ldap_searchobj* sop);


int main( argc, argv, env )
    int		argc;
    char	*argv[];
#ifdef DSGW_DEBUG
    char	*env[];
#endif
{
    int   reqmethod;
    char *qs = NULL;
    char *fname = NULL;

    /* Parse out the file=blah.html from the query string*/
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
	
	if ( !strncasecmp( p, "file=", 5 )) {
	  fname = dsgw_ch_strdup( p + 5 );
	  dsgw_form_unescape( fname );
	  continue;
	}
      }
      free( qs ); qs = NULL;
    } 


    reqmethod = dsgw_init( argc, argv, DSGW_METHOD_POST | DSGW_METHOD_GET );
    dsgw_send_header();

#ifdef DSGW_DEBUG
   dsgw_logstringarray( "env", env ); 
{
    char	buf[ 1024 ];
    getcwd( buf, sizeof(buf));
    dsgw_log( "cwd: \"%s\"\n", buf );
}
#endif

    if ( reqmethod == DSGW_METHOD_POST || reqmethod == DSGW_METHOD_GET ) {
	get_request(fname);
    }

    exit( 0 );
}


static void
get_request(char *fname)
{
    auto char* filename = NULL;
    struct ldap_searchobj* sop = NULL;

    if ( fname == NULL || *fname == '\0' ) {
	filename = "csearch.html";
	dsgw_init_searchprefs( &sop );
    } else if ( !strcmp( fname, "type" )) {
	filename = "csearchType.html";
    } else if ( !strcmp( fname, "attr" )) {
	filename = "csearchAttr.html";
    } else if ( !strcmp( fname, "match" )) {
	filename = "csearchMatch.html";
    } else if ( !strcmp( fname, "string" )) {
	filename = "csearchString.html";
    } else if ( !strcmp( fname, "base" )) {
	filename = "csearchBase.html";
    }
    if (filename) {
	emit_file (filename, sop);
    }
    fflush(stdout);
}


static void
dsgw_emit_options (struct ldap_searchobj** sop, char* searchType, char* searchAttr)
     /* Emit HTML <OPTION> tags corresponding to search preferences.
	If searchType==NULL, emit searchType options; otherwise
	if searchAttr==NULL, emit searchAttr options for the given searchType;
	otherwise emit searchMatch options for the given searchType and searchAttr.
     */
{
    auto struct ldap_searchobj *so;
    if (!*sop) dsgw_init_searchprefs (sop);
    for (so = ldap_first_searchobj(*sop); so != NULLSEARCHOBJ;
	 so = ldap_next_searchobj (*sop, so)) {
	if (LDAP_IS_SEARCHOBJ_OPTION_SET (so, LDAP_SEARCHOBJ_OPT_INTERNAL)) {
	    continue; /* Skip any marked "internal-only" */
	}
	if (!searchType) { /* emit searchType option */
	    dsgw_emitf ("<OPTION VALUE=\"%s\">%s</OPTION>\n",
			so->so_objtypeprompt,
			dsgw_get_translation( so->so_objtypeprompt ));
	} else if (!*searchType || !strcmp (searchType, so->so_objtypeprompt)) {
	    auto struct ldap_searchattr *sa;
	    for (sa = so->so_salist; sa != NULL;
		 sa = sa->sa_next) {
		if (!searchAttr) { /* emit searchAttr option */
		    dsgw_emitf ("<OPTION VALUE=\"%1$s\">%1$s</OPTION>\n",
				sa->sa_attrlabel);
		} else if (!*searchAttr || !strcmp (searchAttr, sa->sa_attrlabel)) {
		    auto int mi;
		    auto struct ldap_searchmatch *sm;
		    for (mi=0, sm = so->so_smlist; sm != NULL;
			 ++mi, sm = sm->sm_next) { /* emit searchMatch option */
			if (sa->sa_matchtypebitmap & (1L << mi)) {
			    dsgw_emitf ("<OPTION VALUE=\"%1$s\">%1$s</OPTION>\n",
					sm->sm_matchprompt);
			}
		    }
		    break;
		}
	    }
	    break;
	}
    }
}


static void
emit_file (char* filename, struct ldap_searchobj* sop)
{
    auto FILE* html = dsgw_open_html_file( filename, DSGW_ERROPT_EXIT );
    auto char line[ BIG_LINE ];
    auto int argc;
    auto char **argv;
    
    while ( dsgw_next_html_line( html, line )) {
	if ( dsgw_parse_line( line, &argc, &argv, 0, dsgw_simple_cond_is_true, NULL )) {
	    if ( dsgw_directive_is( line, "HEAD" )) {
		dsgw_head_begin();
		dsgw_emits ("\n");

	    } else if ( dsgw_directive_is( line, "DS_CSEARCH_SCRIPT" )) {
		dsgw_emits("<SCRIPT LANGUAGE=\"JavaScript\">\n"
			   "<!-- Hide from non-JavaScript-capable browsers\n"
			   "var searchType = '';\n"
			   "var searchAttr = '';\n"
			   "var searchMatch = '';\n" );
		dsgw_emits ("\n"
			    "function searchTypeSet(val)\n"
			    "{\n"
			    "    searchType = val + '';\n"
			    "}\n"
			    "\n"
			    "function searchAttrSet(val)\n"
			    "{\n"
			    "    searchAttr = val + '';\n"
			    "}\n"
			    "\n"
			    "function searchMatchSet(val)\n"
			    "{\n"
			    "    searchMatch = val + '';\n"
			    "}\n"
			    "\n"
			    "function setHiddenFields(sform)\n"
			    "{\n"
/*
 * On Navigator 2.x, the form variable's value seems to get set
 * *after* the onSumbit handler executes, which is unfortunate.
 */
			    "    if (sform.searchstring.value == '') {\n");
		dsgw_emit_alert ("searchStringFrame", NULL,
				 XP_GetClientStr (DBT_youDidNotSupplyASearchString_));
		dsgw_emits ("         return false;\n"
			    "    }\n"
			    "    sform.type.value = searchType;\n"
			    "    sform.attr.value = searchAttr;\n"
			    "    sform.match.value = searchMatch;\n"
			    "    sform.searchstring.select();\n"
			    "    sform.searchstring.focus();\n"
			    "    return true;\n"
			    "}\n"
			    "\n"
			    "function init()\n"
			    "{}\n"
			    "// End hiding -->\n"
			    "</SCRIPT>\n" );

	    } else if ( dsgw_directive_is( line, "EVALUATE" )) {
		auto int i;
		for (i = 0; i < argc; ++i) {
		    if        (!strcmp (argv[i], "parent.searchBase")) {
			dsgw_emits (gc->gc_ldapsearchbase);
		    } else if (!strcmp (argv[i], "parent.UFNsearchBase")) {
#ifdef NOTFORNOW
			/* ldap_dn2ufn currently gobbles up 'dc' so don't use */
			/* it for now */
			auto char* ufn = ldap_dn2ufn (gc->gc_ldapsearchbase);
			dsgw_emits (ufn);
			free( ufn );
#else
			dsgw_emits (gc->gc_ldapsearchbase);
#endif
		    }
		}

	    } else if ( dsgw_directive_is( line, "DS_CSEARCH_TYPE_BODY" )) {
		dsgw_emitf ("<BODY %s %s>\n", dsgw_html_body_colors,
			    "onLoad=\"parent.searchTypeSet(document.searchTypeForm.searchType.options"
			             "[document.searchTypeForm.searchType.selectedIndex].value);\"");

	    } else if ( dsgw_directive_is( line, "DS_CSEARCH_ATTR_BODY" )) {
		dsgw_emitf ("<BODY %s %s>\n", dsgw_html_body_colors,
			    "onLoad=\"parent.searchAttrSet(document.searchAttrForm.searchAttr.options"
			             "[document.searchAttrForm.searchAttr.selectedIndex].value);\"");

	    } else if ( dsgw_directive_is( line, "DS_CSEARCH_MATCH_BODY" )) {
		dsgw_emitf ("<BODY %s %s>\n", dsgw_html_body_colors,
			    "onLoad=\"parent.searchMatchSet(document.searchMatchForm.searchMatch.options"
			             "[document.searchMatchForm.searchMatch.selectedIndex].value);\"");

	    } else if ( dsgw_directive_is( line, "DS_CSEARCH_STRING_BODY" )) {
		dsgw_emitf ("<BODY %s %s>\n", dsgw_html_body_colors,
			    "onLoad=\"document.searchStringForm.searchstring.select();"
			             "document.searchStringForm.searchstring.focus();\"");
		dsgw_emit_alertForm();

	    } else if ( dsgw_directive_is( line, "DS_CSEARCH_BASE_BODY" )) {
		dsgw_emitf ("<BODY %s>\n", dsgw_html_body_colors);

	    } else if ( dsgw_directive_is( line, "DS_CSEARCH_TYPE_FORM" )) {
		dsgw_form_begin ("searchTypeForm",
				 "action=\"%s?file=attr\" target=searchAttrFrame", 
				 dsgw_getvp( DSGW_CGINUM_CSEARCH));
		dsgw_emits("\n");

	    } else if ( dsgw_directive_is( line, "DS_CSEARCH_ATTR_FORM" )) {
		dsgw_form_begin ("searchAttrForm",
				 "action=\"%s?file=match\" target=searchMatchFrame", 
				 dsgw_getvp( DSGW_CGINUM_CSEARCH));
		dsgw_emits("\n");
		{
		    auto char* searchType = dsgw_get_cgi_var ("searchType", DSGW_CGIVAR_OPTIONAL);
		    if (searchType && *searchType) {
			dsgw_emitf ("<INPUT TYPE=hidden NAME=searchType VALUE=\"%s\">\n", 
				    searchType);
		    }
		}

	    } else if ( dsgw_directive_is( line, "DS_CSEARCH_MATCH_FORM" )) {
		dsgw_form_begin ("searchMatchForm", NULL);
		dsgw_emits("\n");

	    } else if ( dsgw_directive_is( line, "DS_CSEARCH_STRING_FORM" )) {
		dsgw_form_begin ("searchStringForm", "action=\"%s\" %s %s",
				 dsgw_getvp( DSGW_CGINUM_DOSEARCH ),
				 "onSubmit=\"return parent.setHiddenFields(this)\"",
				 argc > 0 ? argv[0] : "");
		dsgw_emitf ("\n"
			    "<INPUT TYPE=hidden NAME=mode VALUE=\"complex\">\n"
			    "<INPUT TYPE=hidden NAME=base VALUE=\"%s\">\n"
			    "<INPUT TYPE=hidden NAME=ldapserver VALUE=\"%s\">\n"
			    "<INPUT TYPE=hidden NAME=ldapport VALUE=\"%d\">\n"
			    "<INPUT TYPE=hidden NAME=type>\n"
			    "<INPUT TYPE=hidden NAME=attr>\n"
			    "<INPUT TYPE=hidden NAME=match>\n"
			    "<INPUT TYPE=hidden NAME=context VALUE=\"%s\">\n",
			    gc->gc_ldapsearchbase, gc->gc_ldapserver, gc->gc_ldapport, context);

	    } else if ( dsgw_directive_is( line, "DS_CSEARCH_TYPE_SELECT" )) {
		dsgw_emitf ("<SELECT NAME=searchType "
			    "onChange=\"parent.searchTypeSet(this.options[this.selectedIndex].value);"
			    "this.form.submit();"
			    "parent.searchMatchFrame.location='%s?context=%s&file=match&'+this.name+'='"
			    "+escape(this.options[this.selectedIndex].value);\">\n", 
			    dsgw_getvp( DSGW_CGINUM_CSEARCH), context);
		dsgw_emit_options (&sop, NULL, NULL);
		dsgw_emits ("</SELECT>\n");

	    } else if ( dsgw_directive_is( line, "DS_CSEARCH_ATTR_SELECT" )) {
		dsgw_emits ("<SELECT NAME=searchAttr"
			    " onChange=\"parent.searchAttrSet(this.options[this.selectedIndex].value);"
			                "this.form.submit();\">\n");
		{
		    auto char* searchType = dsgw_get_cgi_var ("searchType", DSGW_CGIVAR_OPTIONAL);
		    dsgw_emit_options (&sop, searchType ? searchType : "", NULL);
		}
		dsgw_emits ("</SELECT>\n");

	    } else if ( dsgw_directive_is( line, "DS_CSEARCH_MATCH_SELECT" )) {
		dsgw_emits ("<SELECT NAME=searchMatch"
			    " onChange=\"parent.searchMatchSet(this.options[this.selectedIndex].value);\">\n");
		{
		    auto char* searchType = dsgw_get_cgi_var ("searchType", DSGW_CGIVAR_OPTIONAL);
		    auto char* searchAttr = dsgw_get_cgi_var ("searchAttr", DSGW_CGIVAR_OPTIONAL);
		    dsgw_emit_options (&sop, searchType ? searchType : "", searchAttr ? searchAttr : "");
		}
		dsgw_emits ("</SELECT>\n");

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

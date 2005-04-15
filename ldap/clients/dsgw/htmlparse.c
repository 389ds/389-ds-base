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
 * htmlparse.c -- routines to parse HTML templates -- HTTP gateway.
 * Stolen from libadmin/template.c and libadmin/form_get.c, originally
 *	by Mike McCool.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "dsgw.h"
#include "dbtdsgw.h"

extern char *Versionstr;	/* from Versiongw.c */

/* global variables */
char *dsgw_last_op_info;	/* set in edit.c and genscreen.c */
char *dsgw_dnattr;		/* set in edit.c */
char *dsgw_dndesc;		/* set in edit.c */

/*
 * Save yourself a lot of grief and put a space after the name.
 */

static struct template_s templates[] = {
  {"IF ", "FUNC conditional"},
  {"ELSE ", "FUNC conditional"},
  {"ELIF ", "FUNC conditional"},
  {"ENDIF ", "FUNC conditional"},
  {"TITLE ", "FUNC title"},
  {"BODY ", "FUNC body"},
  {"COLORS ", "FUNC colors"},
  {"PAGEHEADER ", "FUNC pageheader"},
  {"BEGININFO ", "<table border=2 width=100%% cellpadding=2>\n"
                 "<tr><td align=center colspan=2>"
                 "<b><FONT size=+1>%s</FONT></b></td></tr>"
                 "<td colspan=2>\n"},
  {"ADDINFO ", "</td></tr><tr><td colspan=2>"},
  {"ENDINFO ", "</td></tr></table>\n<hr width=10%%>\n"},
  {"SUBMIT ", "FUNC submit\n"},
  {"BEGINELEM ", "<pre>"},
  {"ELEM ", "\n<b>%s</b>"},
  {"ENDELEM ", "</pre>\n"},
  {"ELEMADD ", "<b>%s</b>"},
  {"ELEMDIV ", "\n"},
  {"INDEX ", "<a href=\"index\">%s</a>\n"},
  {"HELPBUTTON", "FUNC helpbutton"},
  {"DIALOGSUBMIT", "FUNC dialogsubmit"},
  {DRCT_DS_LAST_OP_INFO, "FUNC emit_last_op_info"},
  {DRCT_DS_GATEWAY_VERSION, "FUNC emit_version_str"},
  {DRCT_DS_ALERT_NOENTRIES " ", "FUNC emit_alert_noentries"},
  {"ENDHTML", "</BODY></HTML>"},
  {"GCONTEXT ", "context=%s"},
  {"PCONTEXT ", "<INPUT TYPE=\"hidden\" NAME=\"context\" VALUE=\"%s\">\n"},
  { NULL, NULL }
};

/* global to track output status */
#define DSGW_PARSE_STATUS_NO_IF_SEEN	-1
#define DSGW_PARSE_STATUS_NO_OUTPUT	0
#define DSGW_PARSE_STATUS_OUTPUT	1
static int parse_status = DSGW_PARSE_STATUS_NO_IF_SEEN;

static int dsgw_get_directive(char *string);
static char **dsgw_get_vars(char *string, int *argc);
static void dsgw_pageheader(int argc, char **argv);
static void dsgw_title(int argc, char **argv);
static void dsgw_body(int argc, char **argv);
static void dsgw_colors(int argc, char **argv);
static void dsgw_submit(int verify, char **vars);
static void dsgw_dialogsubmit(void);
static void dsgw_conditional(char *name, int argc, char **argv,
	condfunc conditionalfn, void *condarg);
static int dsgw_condition_true( int argc, char **argv,
	condfunc conditionalfn, void *condarg );
static void emit_last_op_info(int argc, char **argv);
static void emit_version_str( void );
static void emit_alert_noentries( void );
static void template_error( char *msg );

/* Filter a page.  Takes the page to filter as an argument.  Uses above 
 * filters to process.  If we encounter a directive we don't know about,
 * we set argc and argv, and return -1.  The caller is responsible for
 * figuring out what to do with the directive and arg vector.
 *
 * If parseonly is non-zero, this routine will just parse lines that contain
 * directives -- nothing will be written to stdout.
 */
int
dsgw_parse_line(
char *line_input,
int  *argc,
char ***argv,
int parseonly,
condfunc conditionalfn,
void *condarg
)
{
     register int index;
     char *position;
     int dirlen = strlen(DIRECTIVE_START);
     char **vars;
     int func_flag = 0;


     *argc = 0;
     *argv = NULL;
     if ( !strncmp( line_input, DIRECTIVE_START, dirlen )) {
         position = (char *) ( line_input + dirlen );
	 if ( parseonly ) {
	     index = -1;	/* treat all directives as "unknown" */
	 } else {
	     index = dsgw_get_directive( position );
	 }

         /* did we get one? */
         if ( index != -1 )  {
             /* if so, get the vars. */
             position += strlen( templates[index].name );
             vars = dsgw_get_vars( position, argc );
             /* Dispatch the correct function (done for readability) */
             if ( !strncmp(templates[ index ].format, "FUNC ", 5 ))  {
		 func_flag = 1;
	     }

	     /* Don't check the parse_status for conditionals -RJP */
	     if (func_flag == 1 && 
		 !strncmp( templates[index].format+5, "conditional", 11 )) {
		 dsgw_conditional( templates[index].name, *argc, vars,
				   conditionalfn, condarg );
		 /* But do so for the other directives */
	     } else if (func_flag == 1 && parse_status != DSGW_PARSE_STATUS_NO_OUTPUT) {
		 if ( !strncmp( templates[ index ].format+5, "pageheader",10 ))
                     dsgw_pageheader( *argc, vars );
		 else if ( !strncmp( templates[index].format+5,"title",5))
		     dsgw_title( *argc, vars );
		 else if ( !strncmp( templates[index].format+5,"body",4))
		     dsgw_body( *argc, vars );
		 else if ( !strncmp( templates[index].format+5,"colors",6))
		     dsgw_colors( *argc, vars );
                 else if ( !strncmp( templates[ index ].format+5, "submit",6 ))
                     dsgw_submit( 0, vars );
                 else if ( !strncmp( templates[ index ].format+5, "verify",6 ))
                     dsgw_submit( 1, vars );
                 else if ( !strncmp( templates[index].format+5,
			"dialogsubmit",12 ))
                     dsgw_dialogsubmit();
		 else if ( !strncmp( templates[index].format+5, "helpbutton", 10 ) && ( *argc > 0 ))
		     dsgw_emit_helpbutton( vars[ 0 ] );
                 else if ( !strncmp( templates[index].format+5,"emit_last_op_info", 17 ))
		     emit_last_op_info( *argc, vars );
                 else if ( !strncmp( templates[index].format+5,	"emit_version_str", 16 ))
		     emit_version_str();
                 else if ( !strncmp( templates[index].format+5, "emit_alert_noentries", 20 ))
		     emit_alert_noentries();
                 else { /* We don't know what this template is.  Send it back. */
		    *argv = vars;
		    return -1;
		 }
		 /*
		  * Handle the context case specially, because there is no
		  * vars generated, yet the format has a %s in it. Handle
		  * both the GCONTEXT and the PCONTEXT case (GET AND POST)
		  */
             } else if ( parse_status != DSGW_PARSE_STATUS_NO_OUTPUT && 
			 !strcmp(templates[ index ].name + 1, "CONTEXT ")) {
		 char line[ BIG_LINE ];
                 PR_snprintf( line, BIG_LINE, templates[ index ].format, context);
                 dsgw_emits( line );

	     } else if ( parse_status != DSGW_PARSE_STATUS_NO_OUTPUT ) { 
                 /* I just can't believe there's no easy way to create 
                  * a va_list. */
                 char line[ BIG_LINE ];
                 PR_snprintf( line, BIG_LINE, templates[ index ].format, 
                         ( *argc > 0 && vars[ 0 ] != NULL ) ? vars[ 0 ]: "",
                         ( *argc > 1 && vars[ 1 ] != NULL ) ? vars[ 1 ]: "",
                         ( *argc > 2 && vars[ 2 ] != NULL ) ? vars[ 2 ]: "",
                         ( *argc > 3 && vars[ 3 ] != NULL ) ? vars[ 3 ]: "");
                 dsgw_emits( line );
             }
         } else if ( parse_status != DSGW_PARSE_STATUS_NO_OUTPUT ) { 
	    /* We found a directive, but we can't identify it. Return non-zero
	     * value so caller knows to deal with it.
	     */
	     vars = dsgw_get_vars( position, argc );
	     *argv = vars;
	     return -1;
         }
     } else if ( !parseonly && parse_status != DSGW_PARSE_STATUS_NO_OUTPUT ) {
	auto char *gcontext = NULL;
	auto char *start_of_newline = (char *) dsgw_ch_strdup(line_input);
	auto char *new_line_input = start_of_newline; 

         /* We found no directive at the beginning. Look for GCONTEXT
	  * It could be anywhere in the line. Sorry, but that's the way
	  * It has to be. - RJP
	  */
	 for (gcontext = strstr(new_line_input, GCONTEXT_DIRECTIVE); 
	      gcontext != NULL;
	      gcontext = strstr(new_line_input, GCONTEXT_DIRECTIVE)){
	     
	     *gcontext = '\0';
	     /* 
	      * Print the new_line_input (everything up to the first
              * GCONTEXT_DIRECTIVE 
	      */
	     dsgw_HTML_emits( new_line_input );

	     
	     /*Now print "context=whatever"*/
	     dsgw_emitf("context=%s", context);

	     /* Now skip past the directive */
	     new_line_input = gcontext + strlen(GCONTEXT_DIRECTIVE);
	 }

         /* If there's anything left, output it*/
	 if (*new_line_input) {
	     dsgw_HTML_emits( new_line_input );
	 }

	 free ((void*)start_of_newline);
    }

    /* If we're here, we either handled it correctly or the line was benign.*/
    return 0;
}


FILE *
dsgw_open_html_file(char *filename, int erropts)
{
    FILE *f;
    char *tfname = NULL;

    tfname = dsgw_file2path( gc->gc_tmpldir, filename);
    if (!(f = fopen(tfname, "r"))) {
	/* punt */
	dsgw_error(DSGW_ERR_OPENHTMLFILE, tfname, erropts, 0, NULL );
    }

    free( tfname );

    return f;
}


#define DSGW_INCLUDE_DRCT	"<!-- INCLUDE "
#define DSGW_INCLUDE_DRCT_LEN	13
#define DSGW_INCLSET_DRCT	"<!-- INCLUDESET "
#define DSGW_INCLSET_DRCT_LEN	16

int
dsgw_next_html_line(FILE *f, char *line)
{
    char		*p, *incfile;
    int			linelen;
    static FILE		*incfp = NULL;
    static FILE		*parentfp = NULL;
    static int		incset_index = 0;
    static dsgwinclset	*incsetp = NULL;

    if ( incfp != NULL && parentfp == f ) {
	/* we're in the midst of an include -- read from include file */
	if ( fgets(line, BIG_LINE, incfp ) != 0 ) {
	    return 1;	/* success */
	}

	/* end of include file */
	fclose( incfp );

	/* if in middle of an include set, open and use next file in set */
	if ( incsetp != NULL && ++incset_index < incsetp->dsiset_itemcount ) {
	    incfp = dsgw_open_html_file(
		    incsetp->dsiset_filenames[ incset_index ],
		    DSGW_ERROPT_EXIT );
	    return( dsgw_next_html_line( f, line ));
	}
	incfp = NULL;
	incsetp = NULL;
    }

    if(!(fgets(line, BIG_LINE, f))) {
	return 0;	/* end of file */
    }

    if ( incfp != NULL ) {
	return 1;	/* ignore nested includes */
    }

    /* check for start of a simple or an include set based include */
    incfile = NULL;
    linelen = strlen( line );
    if ( linelen > DSGW_INCLUDE_DRCT_LEN && strncasecmp( line,
	    DSGW_INCLUDE_DRCT, DSGW_INCLUDE_DRCT_LEN ) == 0 ) {
	incfile = line + DSGW_INCLUDE_DRCT_LEN;
	if (( p = strchr( incfile, ' ' )) != NULL ) {
	    *p = '\0';
	}
    } else if ( linelen > DSGW_INCLSET_DRCT_LEN && strncasecmp( line,
	    DSGW_INCLSET_DRCT, DSGW_INCLSET_DRCT_LEN ) == 0 ) {
	char		*sethandle;

	sethandle = line + DSGW_INCLSET_DRCT_LEN;
	if (( p = strchr( sethandle, ' ' )) != NULL ) {
	    *p = '\0';
	}

	for ( incsetp = gc->gc_includesets; incsetp != NULL;
		incsetp = incsetp->dsiset_next ) {
	    if ( strcasecmp( sethandle, incsetp->dsiset_handle ) == 0 ) {
		break;
	    }
	}
	if ( incsetp == NULL ) {	/* set not found -- ignore it */
	    if ( p != NULL ) {
		*p = ' ';
	    }
	    return( 1 );
	}
	incset_index = 0;
	incfile = incsetp->dsiset_filenames[ 0 ];
    }

    if ( incfile != NULL ) {
	incfp = dsgw_open_html_file( incfile, DSGW_ERROPT_EXIT );
	parentfp = f;
	return( dsgw_next_html_line( f, line ));
    }

    return 1;
}


static void
dsgw_pageheader(int argc, char **argv)
{
    char line[BIG_LINE];

    dsgw_emits("<center><table border=2 width=100%%>\n");

    util_snprintf(line, BIG_LINE, "<tr>");
    dsgw_emits(line);

    util_snprintf(line, BIG_LINE, "<td align=center width=100%%>");
    dsgw_emits(line);
    util_snprintf(line, BIG_LINE, "<hr size=0 width=0>");
    dsgw_emits(line);
    util_snprintf(line, BIG_LINE, "<FONT size=+2><b>%s</b></FONT>"
                                  "<hr size=0 width=0>"
                                  "</th>", ( argc > 0 ) ? argv[0] : "" );
    dsgw_emits(line);
    
    dsgw_emits("</tr></table></center>\n");
}


static void
dsgw_title( int argc, char **argv)
{
    char line[BIG_LINE];
    dsgw_emits("<HTML>");
    dsgw_head_begin();
    util_snprintf(line, BIG_LINE, "\n<TITLE>%s</TITLE></HEAD>\n"
	    "<BODY %s>\n", ( argc > 0 ) ? argv[0] : "", dsgw_html_body_colors );
    dsgw_emits(line);
}


static void
dsgw_body( int argc, char **argv)
{
    char line[BIG_LINE];

    if ( argc > 0 ) {
	util_snprintf(line, BIG_LINE, "<BODY %s %s>\n", dsgw_html_body_colors,
		( argc > 0 ) ? argv[0] : "" );
    } else {
	util_snprintf(line, BIG_LINE, "<BODY %s>\n", dsgw_html_body_colors );
    }

    dsgw_emits(line);
}


static void
dsgw_colors( int argc, char **argv)
{
    if ( argc > 0 ) {
	dsgw_html_body_colors = dsgw_ch_strdup( argv[0] );
    } else {
	dsgw_html_body_colors = "";
    }
}


static void
dsgw_submit(int verify, char **vars)
{
    if(verify)  {
        dsgw_emits ("<SCRIPT language=JavaScript><!--\n"
		    "function verify(form)\n{\n"
		    "    window.confirmedForm = form;\n");
	dsgw_emit_confirm (NULL, "opener.confirmedForm.submit();", NULL /* no */,
			   XP_GetClientStr(DBT_doYouReallyWantToWindow_), 1,
			   XP_GetClientStr(DBT_doYouReallyWantTo_), vars[0]);
        dsgw_emits ("}\n"
		    "// -->\n"
		    "</SCRIPT>\n");
    }

    dsgw_emits("<center><table border=2 width=100%%><tr>");

    if(!verify)  {
        char outstr[256];
        PR_snprintf(outstr, 256, "<td width=50%% align=center>"
               "<input type=submit value=\"%s\">"
               "</td>\n",
               XP_GetClientStr(DBT_ok_1));
        dsgw_emits(outstr);
    }  else  {
        char outstr[256];
        PR_snprintf(outstr, 256, "<td width=50%% align=center>"
               "<input type=button value=\"%s\" "
               "onclick=\"verify(this.form)\">"
               "</td>\n",
               XP_GetClientStr(DBT_ok_2));
        dsgw_emits(outstr);
    }
    {
        char outstr[256];
        PR_snprintf(outstr, 256, "<td width=50%% align=center>"
               "<input type=reset value=\"%s\"></td>\n",
               XP_GetClientStr(DBT_reset_));
        dsgw_emits(outstr);
    }       

    dsgw_emits("</tr></table></center>\n");

    dsgw_emits("</form>\n");

    dsgw_emits("<SCRIPT language=JavaScript>\n");
    dsgw_emits("</SCRIPT>\n");
}


static void
dsgw_dialogsubmit(void)
{
    char outstr[256];

    dsgw_emits("<center><table border=2 width=100%%><tr>");

    PR_snprintf(outstr, 256, "<td width=50%% align=center>"
           "<input type=submit value=\"%s\">"
           "</td>\n",
           XP_GetClientStr(DBT_done_));
    dsgw_emits(outstr);
    PR_snprintf(outstr, 256, "<td width=50%% align=center>"
           "<input type=button value=\"%s\" "
           "onClick=\"top.close()\"></td>\n",
           XP_GetClientStr(DBT_cancel_2));
    dsgw_emits(outstr);

    dsgw_emits("</tr></table></center>\n");

    dsgw_emits("</form>\n");

    dsgw_emits("<SCRIPT language=JavaScript>\n");
    dsgw_emits("</SCRIPT>\n");
}


static void
dsgw_conditional( char *name, int argc, char **argv, condfunc conditionalfn,
	void *condarg )
{
#define DSGW_COND_STATUS_NO_COND_SEEN	0
#define DSGW_COND_STATUS_IN_IF		1
#define DSGW_COND_STATUS_IN_ELSE	2
#define DSGW_COND_STATUS_IN_ELIF	3

    static int	cond_status = DSGW_COND_STATUS_NO_COND_SEEN;
    static int	cond_was_true = 0;

    if ( strncmp( name, "IF", 2 ) == 0 ) {
	if ( cond_status != DSGW_COND_STATUS_NO_COND_SEEN ) {
	    template_error( XP_GetClientStr(DBT_foundAnotherIfNestedIfsAreNotSup_) );
	    return;
	}
	cond_was_true = dsgw_condition_true( argc, argv, conditionalfn,
		condarg );
	parse_status = cond_was_true ? DSGW_PARSE_STATUS_OUTPUT
		: DSGW_PARSE_STATUS_NO_OUTPUT;
	cond_status = DSGW_COND_STATUS_IN_IF;

    } else if ( strncmp( name, "ELSE", 4 ) == 0 ) {
	if ( cond_status == DSGW_COND_STATUS_NO_COND_SEEN ) {
	    template_error( XP_GetClientStr(DBT_foundElseButDidnTSeeAnIf_) );
	    return;
	}
	if ( cond_status == DSGW_COND_STATUS_IN_ELSE ) {
	    template_error( XP_GetClientStr(DBT_foundElseAfterElseExpectingEndif_) );
	    return;
	}
	parse_status = cond_was_true ? DSGW_PARSE_STATUS_NO_OUTPUT
		: DSGW_PARSE_STATUS_OUTPUT;
	cond_status = DSGW_COND_STATUS_IN_ELSE;

    } else if ( strncmp( name, "ELIF", 4 ) == 0 ) {
	if ( cond_status == DSGW_COND_STATUS_NO_COND_SEEN ) {
	    template_error( XP_GetClientStr(DBT_foundElifButDidnTSeeAnIf_) );
	    return;
	}
	if ( cond_status == DSGW_COND_STATUS_IN_ELSE ) {
	    template_error( XP_GetClientStr(DBT_foundElifAfterElseExpectingEndif_) );
	    return;
	}

	if ( cond_was_true ) {
	    parse_status = DSGW_PARSE_STATUS_NO_OUTPUT;
	} else {
	    cond_was_true = dsgw_condition_true( argc, argv, conditionalfn,
		    condarg );
	    parse_status = cond_was_true ? DSGW_PARSE_STATUS_OUTPUT
		    : DSGW_PARSE_STATUS_NO_OUTPUT;
	}
	cond_status = DSGW_COND_STATUS_IN_ELIF;

    } else if ( strncmp( name, "ENDIF", 5 ) == 0 ) {
	if ( cond_status == DSGW_COND_STATUS_NO_COND_SEEN ) {
	    template_error( XP_GetClientStr(DBT_foundEndifButDidnTSeeAnIf_) );
	}
	parse_status = DSGW_PARSE_STATUS_NO_IF_SEEN;
	cond_status = DSGW_COND_STATUS_NO_COND_SEEN;
    }
}


static void
emit_last_op_info( int argc, char **argv )
{
    char	*s;

    if ( dsgw_last_op_info != NULL ) {
	if (( s = get_arg_by_name( "prefix", argc, argv )) != NULL ) {
	    dsgw_emits( s );
	}

	dsgw_emits( dsgw_last_op_info );

	if (( s = get_arg_by_name( "suffix", argc, argv )) != NULL ) {
	    dsgw_emits( s );
	}
    }
}


static void
emit_version_str()
{
    dsgw_emits( Versionstr );
}


static void
emit_alert_noentries()
{
    dsgw_emit_alertForm();
    dsgw_emits( "<SCRIPT LANGUAGE=JavaScript><!--\n" );
    dsgw_emit_alert (NULL, NULL, XP_GetClientStr(DBT_SearchFound0Entries_),
		     0L, "", "", "");
    dsgw_emits( "// -->\n</SCRIPT>\n");
}


static void
template_error( char *msg )
{
    dsgw_emitf( XP_GetClientStr(DBT_BrBTemplateErrorBSBrN_), msg );
}


static int
dsgw_condition_true( int argc, char **argv, condfunc conditionalfn,
	void *condarg )
{
    char	*save_argv0;
    int		rc;

    if ( argc < 1 || conditionalfn == NULL ) {
	return( 1 );	/* unknown, but we default to true */
    }

    if ( argv[0][0] == '!' ) {	/* NOT */
	save_argv0 = argv[0];
	argv[0] = save_argv0 + 1;
    } else {
	save_argv0 = NULL;
    }

    rc = (*conditionalfn)( argc, argv, condarg );

    if ( save_argv0 != NULL ) {
	argv[0] = save_argv0;
	rc = !rc;	/* '!' was seen -- reverse the result */
    }

    return( rc );
}

static int
dsgw_get_directive(
char *string
)  
{
    int index = -1;
    register int x;

    for ( x = 0; templates[ x ].name != NULL; x++ )  {
        if ( !strncmp( string, templates[ x ].name,
                    strlen( templates[ x ].name ))) {
            index = x;
            break;
        }
    }
    return index;
}

int
dsgw_directive_is(char *target, char *directive)
{
    char *position = (target + strlen(DIRECTIVE_START));
    return(!(strncmp(directive, position, strlen(directive))));
}

static char **
dsgw_get_vars(
char *string,
int *argc
)
{
    char **vars = (char **) NULL;
    register int x;
    int isvar;
    char scratch[BIG_LINE];
    char lastchar, *p;
    int numvars = 0;

    isvar = -1;
    x = 0;
    scratch[0] = '\0';
    lastchar = ' ';
 
    while ( *string != '\0' ) {
        if (( *string == '\"' ) && ( lastchar != '\\' )) {
            if ( isvar != -1 )  {
		numvars++;
		vars = (char **)dsgw_ch_realloc( vars,
			( numvars + 1 ) * sizeof ( char * ));
                vars[ numvars - 1 ] = (char *) dsgw_ch_strdup( scratch );
		if (( p = strchr( vars[ numvars - 1 ], '=' )) != NULL ) {
		    dsgw_form_unescape( p + 1 );
		}
		vars[ numvars ] = NULL;
                isvar = -1;
            }  else {
                isvar = 0;
	    }
        } else {
            if ( isvar != -1 )  {
		isvar += LDAP_UTF8COPY(scratch + isvar, string);
                scratch[ isvar ] = '\0';
            } else {
                if ( *string == DIRECTIVE_END ) {
                    break;
		}
	    }
	}
        lastchar = *string;
        LDAP_UTF8INC(string);
    }
    *argc = numvars;
    return vars;
}




/*
 * Search the given arg vector for a "tag=value" string where "tag" is
 * the same string as "name".  If found, return a pointer to the beginning
 * of the "value" string.  If the value string is missing (e.g. "tag="
 * was given), return a zero-length string.  If no matching tag was found,
 * return NULL.
 */
char *
get_arg_by_name( char *name, int argc, char **argv )
{
    int		i;

    if (( i = dsgw_get_arg_pos_by_name( name, argc, argv )) >= 0 ) {
	return( &argv[ i ][ strlen( name ) + 1 ] );
    } else {
	return( NULL );
    }
}


int
dsgw_get_arg_pos_by_name( char *name, int argc, char **argv )
{
    int i;
    int nl = strlen( name );

    for ( i = 0; i < argc; i++ ) {
	if ( argv[ i ] != NULL ) {
	    if ( !strncasecmp( name, argv[ i ], nl )) {
		if (( argv[ i ][ nl ] == '=' )) {
		    return( i );
		}
	    }
	}
    }
    return( -1 );
}


void
dsgw_argv_free( char **argv )
{
    char	**p;

    if ( argv != NULL ) {
	for ( p = argv; *p != NULL; ++p ) {
	    free( *p );
	}
	free( argv );
    } 
}


savedlines *
dsgw_savelines_alloc()
{
    savedlines	*slp;

    slp = dsgw_ch_malloc( sizeof( savedlines ));
    memset( slp, 0, sizeof( savedlines ));
    return( slp );
}


void
dsgw_savelines_free( savedlines *svlp )
{
    int		i;

    for ( i = 0; i < svlp->svl_count; ++i ) {
	free( svlp->svl_line[ i ] );
    }
    free( svlp );
}


void
dsgw_savelines_rewind( savedlines *svlp )
{
    svlp->svl_current = 0;
}


void
dsgw_savelines_save( savedlines *svlp, char *line )
{
    svlp->svl_line = (char **)dsgw_ch_realloc( svlp->svl_line,
	    (1 + svlp->svl_count ) * sizeof( char * ));
    svlp->svl_line[ svlp->svl_count++ ] = dsgw_ch_strdup( line );
}


char *
dsgw_savelines_next( savedlines *svlp )
{
    char	*p;

    if ( svlp->svl_current >= svlp->svl_count ) {
	return( NULL );
    }

    p = svlp->svl_line[ svlp->svl_current ];
    ++svlp->svl_current;

    return( p );
}

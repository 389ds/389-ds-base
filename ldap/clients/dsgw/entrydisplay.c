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
 * entrydisplay.c --  output entries one at a time or in a list -- HTTP gateway 
 */

#include "dsgw.h"
#include "dbtdsgw.h"
#include <ldap.h> /* ldap_utf8* */
#include <unicode/udat.h>
#include <unicode/utypes.h>
#include <unicode/unum.h>
#include <unicode/ucal.h>

/*
 * Note: the value of the following DSGW_ATTRHTML_XXX #defines must match
 * their position in the attrhtmltypes[] and attrhtmlvals[] arrays.
 */
#define DSGW_ATTRHTML_HIDDEN		0
#define DSGW_ATTRHTML_TEXT		1
#define DSGW_ATTRHTML_TEXTAREA		2
#define DSGW_ATTRHTML_RADIO		3
#define DSGW_ATTRHTML_CHECKBOX		4
#define DSGW_ATTRHTML_PASSWORD		5
static char *attrhtmltypes[] = {
	"hidden",
	"text",
	"textarea",
	"radio",
	"checkbox",
	"password",
	NULL
};
static int attrhtmlvals[] = {
	DSGW_ATTRHTML_HIDDEN,
	DSGW_ATTRHTML_TEXT,
	DSGW_ATTRHTML_TEXTAREA,
	DSGW_ATTRHTML_RADIO,
	DSGW_ATTRHTML_CHECKBOX,
	DSGW_ATTRHTML_PASSWORD,
};

#define DSGW_ATTROPT_SORT	0x00000001
#define DSGW_ATTROPT_NOLINK	0x00000002
#define DSGW_ATTROPT_DNTAGS	0x00000004
#define DSGW_ATTROPT_DATEONLY	0x00000008	/* only for syntax=time */
#define DSGW_ATTROPT_READONLY	0x00000010	/* over-rides ..._EDITABLE */
#define DSGW_ATTROPT_DNPICKER	0x00000020	/* display dns for find-n-add */
#define DSGW_ATTROPT_UNIQUE	0x00000040	/* attr values must be unique */
#define DSGW_ATTROPT_LINK	0x00000080	/* link to attribute value */
#define DSGW_ATTROPT_TYPEONLY	0x00000100	/* retrieve attr. type only */
#define DSGW_ATTROPT_NO_ENTITIES  0x00000200	/* don't use entities */
#define DSGW_ATTROPT_HEX		0x00000400	/* display as hex value */
#define DSGW_ATTROPT_DECIMAL	0x00000800	/* display as decimal value */
#define DSGW_ATTROPT_QUOTED	0x00001000	/* quote the result */
#define DSGW_ATTROPT_EDITABLE	0x10000000	/* not exposed in HTML */
#define DSGW_ATTROPT_ADDING	0x20000000	/* not exposed in HTML */
#define DSGW_ATTROPT_LINK2EDIT	0x40000000	/* not exposed in HTML */
static char *attroptions[] = {
	"sort",
	"nolink",
	"dntags",
	"dateonly",
	"readonly",
	"dnpicker",
	"unique",
	"link",
	"typeonly",
	"noentities",
	"hex",
	"decimal",
	"quoted",
	NULL
};

static unsigned long attroptvals[] = {
	DSGW_ATTROPT_SORT,
	DSGW_ATTROPT_NOLINK,
	DSGW_ATTROPT_DNTAGS,
	DSGW_ATTROPT_DATEONLY,
	DSGW_ATTROPT_READONLY,
	DSGW_ATTROPT_DNPICKER,
	DSGW_ATTROPT_UNIQUE,
	DSGW_ATTROPT_LINK,
	DSGW_ATTROPT_TYPEONLY,
	DSGW_ATTROPT_NO_ENTITIES,
	DSGW_ATTROPT_HEX,
	DSGW_ATTROPT_DECIMAL,
	DSGW_ATTROPT_QUOTED,
};


#define DSGW_ATTRARG_ATTR	"attr"
#define DSGW_ATTRARG_SYNTAX	"syntax"
#define DSGW_ATTRARG_HTMLTYPE	"type"
#define DSGW_ATTRARG_OPTIONS	"options"
#define DSGW_ATTRARG_DEFAULT	"defaultvalue"
#define DSGW_ATTRARG_WITHIN	"within"	/* overrides href & hrefextra */
#define DSGW_ATTRARG_HREF	"href"
#define DSGW_ATTRARG_HREFEXTRA	"hrefextra"
#define DSGW_ATTRARG_LABEL	"label"		/* only used with syntax=dn */
#define DSGW_ATTRARG_DNCOMP	"dncomponents"	/* only used with syntax=dn */
#define DSGW_ATTRARG_TRUESTR	"true"		/* only used with syntax=bool */
#define DSGW_ATTRARG_FALSESTR	"false"		/* only used with syntax=bool */
#define DSGW_ATTRARGS_SIZE	"size"
#define DSGW_ATTRARGS_ROWS	"rows"
#define DSGW_ATTRARGS_COLS	"cols"
#define DSGW_ATTRARGS_NUMFIELDS	"numfields"
#define DSGW_ATTRARGS_VALUE	"value"
#define DSGW_ATTRARG_MIMETYPE	"mimetype"
#define	DSGW_ATTRARG_SCRIPT	"script"

#define DSGW_ARG_BUTTON_PROMPT		"prompt"
#define DSGW_ARG_BUTTON_TEMPLATE	"template"
#define DSGW_ARG_BUTTON_CHECKSUBMIT	"checksubmit"
#define DSGW_ARG_BUTTON_TOPIC		"topic"
#define DSGW_ARG_DNEDIT_LABEL		"label"
#define DSGW_ARG_DNEDIT_TEMPLATE	"template"
#define DSGW_ARG_DNEDIT_ATTR		"attr"
#define DSGW_ARG_DNEDIT_DESC		"desc"

#define	DSGW_ARG_FABUTTON_LABEL		"label"
#define	DSGW_ARG_FABUTTON_ATTRNAME	"attr"
#define	DSGW_ARG_FABUTTON_ATTRDESC	"attrdesc"

#define DSGW_ARG_AVSET_SET		"set"

/*
 * structure used simply to avoid passing a lot of parameters in call to
 * the attribute syntax handlers
 */
struct dsgw_attrdispinfo {
    struct attr_handler	*adi_handlerp;
    char		*adi_attr;
    int			adi_argc;
    char		**adi_argv;
    char		**adi_vals;
    char		*adi_rdn; /* a copy of adi_vals[i] (possibly NULL) */
    int			adi_htmltype;
    unsigned long	adi_opts;
};
/* adi_rdn should be generalized, to support an RDN
   that contains several values of one attribute type.
*/

typedef void (*attrdisplay)( struct dsgw_attrdispinfo *adip );
typedef void (*attredit)( struct dsgw_attrdispinfo *adip );

struct attr_handler {
    char		*ath_syntax;	/* dn, tel, cis, etc. */
    attrdisplay		ath_display;	/* function to display values */
    attredit		ath_edit;	/* function to display for editing */
    int			ath_compare;	/* compare function */
};

/* functions local to this file */
static void append_to_array( char ***ap, int *countp, char *s );
static unsigned long get_attr_options( int argc, char **argv );
static void output_prelude( dsgwtmplinfo *tip );
static void output_nonentry_line( dsgwtmplinfo *tip, char *line );
static struct attr_handler *syntax2attrhandler( char *syntax );
static int numfields( int argc, char **argv, int valcount );
static void element_sizes( int argc, char **argv, char **vals, int valcount,
	int *rowsp, int *colsp );
#define DSGW_TEXTOPT_FOCUSHANDLERS	0x0001
#define DSGW_TEXTOPT_CHANGEHANDLERS	0x0002
static void output_text_elements( int argc, char **argv, char *attr,
	char **vals, const char* rdn, char *prefix, int htmltype, unsigned long opts );
static void output_textarea( int argc, char **argv, char *attr,
	char **vals, int valcount, char *prefix, unsigned long opts );
static void emit_value( char *val, int quote_html_specials );
static void output_text_checkbox_or_radio( struct dsgw_attrdispinfo *adip,
	char *prefix, int htmltype );
static void do_attribute( dsgwtmplinfo *tip, char *dn, unsigned long dispopts,
	int argc, char **argv );
static void do_orgchartlink( dsgwtmplinfo *tip, char *dn, unsigned long dispopts,
	int argc, char **argv );
static void do_attrvalset( dsgwtmplinfo *tip, char *dn, unsigned long dispopts,
	int argc, char **argv );
static void do_editbutton( char *dn, char *encodeddn, int argc, char **argv );
static void do_savebutton( unsigned long dispopts, int argc, char **argv );
static void do_deletebutton( int argc, char **argv );
#if 0
static void do_renamebutton( char *dn, int argc, char **argv );
#endif
static void do_editasbutton( int argc, char **argv );
static void do_dneditbutton( unsigned long dispopts, int argc, char **argv );
static void do_searchdesc( dsgwtmplinfo *tip, int argc, char **argv );
static void do_passwordfield( unsigned long dispopts, int argc, char **argv,
	char *fieldname );
static void do_helpbutton( unsigned long dispopts, int argc, char **argv );
static void do_closebutton( unsigned long dispopts, int argc, char **argv );
static void do_viewswitcher( char *template, char *dn, int argc, char **argv );
static int did_output_as_special( int argc, char **argv, char *label,
	char *val );
static char *time2text( char *ldtimestr, int dateonly );
static long gtime( struct tm *tm );
static int looks_like_dn( char *s );
static void do_std_completion_js( char *template, int argc, char **argv );
static int condition_is_true( int argc, char **argv, void *arg );
static char ** dsgw_get_values( LDAP *ld, LDAPMessage *entry, 
				const char *target, int binary_value );
static void dsgw_value_free( void **ldvals, int binary ) ;
static char *dsgw_time(time_t secs_since_1970);

/* attribute syntax handler routines */
#if NEEDED_FOR_DEBUGGING
static void ntdomain_display( struct dsgw_attrdispinfo *adip );
#endif
static void ntuserid_display( struct dsgw_attrdispinfo *adip );
static void str_display( struct dsgw_attrdispinfo *adip );
static void str_edit( struct dsgw_attrdispinfo *adip );
static void dn_display( struct dsgw_attrdispinfo *adip );
static void dn_edit( struct dsgw_attrdispinfo *adip );
static void mail_display( struct dsgw_attrdispinfo *adip );
static void mls_display( struct dsgw_attrdispinfo *adip );
static void mls_edit( struct dsgw_attrdispinfo *adip );
static void binvalue_display( struct dsgw_attrdispinfo *adip );
static void url_display( struct dsgw_attrdispinfo *adip );
static void bool_display( struct dsgw_attrdispinfo *adip );
static void bool_edit( struct dsgw_attrdispinfo *adip );
static void time_display( struct dsgw_attrdispinfo *adip );


/* static variables */
#define DSGW_MOD_PREFIX_NORMAL	0
#define DSGW_MOD_PREFIX_UNIQUE	1
static char *replace_prefixes[] = { "replace_", "replace_unique_" };
static char *replace_mls_prefixes[] = { "replace_mls_", "replace_mls_unique_" };
static char *add_prefixes[] = { "add_", "add_unique_" };
static char *add_mls_prefixes[] = { "add_mls_", "add_mls_unique_" };

struct attr_handler attrhandlers[] = {
    { "cis",	str_display,	str_edit,	CASE_INSENSITIVE	},
    { "dn",	dn_display,	dn_edit,	CASE_INSENSITIVE	},
    { "mail",	mail_display,	str_edit,	CASE_INSENSITIVE	},
    { "mls",	mls_display,	mls_edit,	CASE_INSENSITIVE	},
    { "tel",	str_display,	str_edit,	CASE_INSENSITIVE	},
    { "url",	url_display,	str_edit,	CASE_EXACT       	},
    { "ces",	str_display,	str_edit,	CASE_EXACT       	},
    { "bool",	bool_display,	bool_edit,	CASE_INSENSITIVE	},
    { "time",	time_display,	str_edit,	CASE_INSENSITIVE	},
    { "ntuserid", ntuserid_display, str_edit,	CASE_INSENSITIVE	},
    { "ntgroupname", ntuserid_display, str_edit,	CASE_INSENSITIVE	},
    { "binvalue", binvalue_display, str_edit, CASE_INSENSITIVE	},
};
#define DSGW_AH_COUNT ( sizeof( attrhandlers ) / sizeof( struct attr_handler ))


static char *
template_filename( int tmpltype, char *template )
{
    char	*fn, *prefix, *suffix = ".html";

    if ( tmpltype == DSGW_TMPLTYPE_LIST ) {
	prefix = DSGW_CONFIG_LISTPREFIX;
    } else if ( tmpltype == DSGW_TMPLTYPE_EDIT ) {
	prefix = DSGW_CONFIG_EDITPREFIX;
    } else if ( tmpltype == DSGW_TMPLTYPE_ADD ) {
	prefix = DSGW_CONFIG_ADDPREFIX;
    } else {
	prefix = DSGW_CONFIG_DISPLAYPREFIX;
    }

    fn = dsgw_ch_malloc( strlen( prefix ) + strlen( template )
	    + strlen( suffix ) + 1 );
    sprintf( fn, "%s%s%s", prefix, template, suffix );

    return( fn );
}

static void
do_postedvalue( int argc, char **argv )
{
    dsgw_emits( "VALUE=\"" );
    dsgw_emit_cgi_var( argc, argv );
    dsgw_emits( "\"\n" );
}

static int
dsgw_display_line( dsgwtmplinfo *tip, char *line, int argc, char **argv )
{
    if ( dsgw_directive_is( line, DRCT_DS_POSTEDVALUE )) {
	do_postedvalue( argc, argv );
    } else if ( dsgw_directive_is( line, DRCT_DS_HELPBUTTON )) {
	do_helpbutton( tip->dsti_options, argc, argv );
    } else if ( dsgw_directive_is( line, DRCT_DS_CLOSEBUTTON )) {
	do_closebutton( tip->dsti_options, argc, argv );
    } else if ( dsgw_directive_is( line, DRCT_DS_OBJECTCLASS )) {
	/* omit objectClass lines */
    } else if ( dsgw_directive_is( line, DRCT_HEAD )) {
	dsgw_head_begin();
	dsgw_emits ("\n");
    } else {
	return 0;
    }
    return 1;
}

dsgwtmplinfo *
dsgw_display_init( int tmpltype, char *template, unsigned long options )
{
    dsgwtmplinfo	*tip;
    int			argc, attrcount, attrsonlycount, skip_line, in_entry;
    char		**argv, *attr, *filename, line[ BIG_LINE ];
    unsigned long	aopts;

    tip = (dsgwtmplinfo *)dsgw_ch_malloc( sizeof( dsgwtmplinfo ));
    memset( tip, 0, sizeof( dsgwtmplinfo ));
    tip->dsti_type = tmpltype;
    tip->dsti_options = options;
    tip->dsti_template = dsgw_ch_strdup( template );

    if (( options & DSGW_DISPLAY_OPT_ADDING ) != 0 ) {
	options |= DSGW_DISPLAY_OPT_EDITABLE;	/* add implies editable */

	if ( tmpltype != DSGW_TMPLTYPE_ADD ) {
	    /*
	     * if we are going to display an "add" view of an entry and
	     * an add template has not been explicitly requested, first look
	     * for a file called "add-TEMPLATE.html" and fall back on using
	     * whatever we would use if just editing an existing entry.
	     */
	    filename = template_filename( DSGW_TMPLTYPE_ADD, template );
	    tip->dsti_fp = dsgw_open_html_file( filename, DSGW_ERROPT_IGNORE );
	    free( filename );
	}
    }

    if ( tip->dsti_fp == NULL && ( options & DSGW_DISPLAY_OPT_EDITABLE ) != 0
	    && tmpltype != DSGW_TMPLTYPE_EDIT ) {
	/*
	 * if we are going to display an editable view of an entry and
	 * an edit template has not been explicitly requested, first look
	 * for a file called "edit-TEMPLATE.html" and fall back on using
	 * "list-TEMPLATE.html" or "display-TEMPLATE.html", as indicated by
	 * the value of tmpltype.
	 */
	filename = template_filename( DSGW_TMPLTYPE_EDIT, template );
	tip->dsti_fp = dsgw_open_html_file( filename, DSGW_ERROPT_IGNORE );
	free( filename );
    }

    if ( tip->dsti_fp == NULL ) {
	filename = template_filename( tmpltype, template );
	tip->dsti_fp = dsgw_open_html_file( filename, DSGW_ERROPT_EXIT );
	free( filename );
    }

    tip->dsti_preludelines = dsgw_savelines_alloc();
    tip->dsti_entrylines = dsgw_savelines_alloc();
    in_entry = 0;

    /* prime attrs array so we always retrieve objectClass values */
    attrcount = 1;
    tip->dsti_attrs = (char **)dsgw_ch_realloc( tip->dsti_attrs,
	    2 * sizeof( char * ));
    tip->dsti_attrs[ 0 ] = dsgw_ch_strdup( DSGW_ATTRTYPE_OBJECTCLASS );
    tip->dsti_attrs[ 1 ] = NULL;
    attrsonlycount = 0;
    tip->dsti_attrsonly_attrs = NULL;

    while ( dsgw_next_html_line( tip->dsti_fp, line )) {
	skip_line = 0;
	if ( dsgw_parse_line( line, &argc, &argv, 1, condition_is_true, tip )) {
	    if ( in_entry && dsgw_directive_is( line, DRCT_DS_ENTRYEND )) {
		dsgw_argv_free( argv );
		break;	/* the rest is read inside dsgw_display_done */
	    }
	    if ( dsgw_directive_is( line, DRCT_DS_ENTRYBEGIN )) {
		in_entry = skip_line = 1;
	    } else if ( dsgw_directive_is( line, DRCT_DS_ATTRIBUTE ) ||
		    dsgw_directive_is( line, DRCT_DS_ATTRVAL_SET )) {
		aopts = get_attr_options( argc, argv );
		if (( attr = get_arg_by_name( DSGW_ATTRARG_ATTR, argc,
			argv )) != NULL && strcasecmp( attr, "dn" ) != 0 &&
		    (strcasecmp(attr,DSGW_ATTRTYPE_AIMSTATUSTEXT) != 0 || gc->gc_aimpresence == 1) &&
			( aopts & DSGW_ATTROPT_LINK ) == 0 ) {
		    if (( aopts & DSGW_ATTROPT_TYPEONLY ) == 0 ) {
			append_to_array( &tip->dsti_attrs, &attrcount, attr );
		    } else {
			append_to_array( &tip->dsti_attrsonly_attrs,
				&attrsonlycount, attr );
		    }
		}
	    } else if ( dsgw_directive_is( line, DRCT_DS_ORGCHARTLINK )) {
		aopts = get_attr_options( argc, argv );
		if (( aopts & DSGW_ATTROPT_TYPEONLY ) == 0 ) {
		    append_to_array( &tip->dsti_attrs, &attrcount, gc->gc_orgchartsearchattr );
		} else {
		    append_to_array( &tip->dsti_attrsonly_attrs,
				     &attrsonlycount, gc->gc_orgchartsearchattr);
		}
	    } else if ( dsgw_directive_is( line, DRCT_DS_SORTENTRIES )) {
		if (( attr = get_arg_by_name( DSGW_ATTRARG_ATTR, argc,
			argv )) == NULL ) {
		    tip->dsti_sortbyattr = NULL;  /* no attr=, so sort by DN */
		} else {
		    tip->dsti_sortbyattr = dsgw_ch_strdup( attr );
		} 
		skip_line = 1;	/* completely done with directive */
	    }
	    dsgw_argv_free( argv );
	}

	if ( !skip_line ) {
	    if ( in_entry ) {	/* in entry */
		dsgw_savelines_save( tip->dsti_entrylines, line );
	    } else {		/* in prelude */
		dsgw_savelines_save( tip->dsti_preludelines, line );
	    }
	}
    }

    if ( attrcount > 0 ) {
	tip->dsti_attrflags = (unsigned long *)dsgw_ch_malloc( attrcount
		* sizeof( unsigned long ));
	memset( tip->dsti_attrflags, 0, attrcount * sizeof( unsigned long ));
    }

    /*
     * Add the sortattr to the list of attrs retrieved, if it's not
     * already in the list.
     */
    if ( tip->dsti_sortbyattr != NULL ) {
	int i, found = 0;
	for ( i = 0; i < attrcount; i++ ) {
	    if ( !strcasecmp( tip->dsti_sortbyattr, tip->dsti_attrs[ i ])) {
		found = 1;
		break;
	    }
	}
	if ( !found ) {
	    append_to_array( &tip->dsti_attrs, &attrcount,
		    tip->dsti_sortbyattr );
	}
    }

    return( tip );
}


void
dsgw_display_entry( dsgwtmplinfo *tip, LDAP *ld, LDAPMessage *entry,
	LDAPMessage *attrsonly_entry, char *dn )
{
    int		argc, editable, adding;
    char        **argv, *encodeddn, *line;

    editable = (( tip->dsti_options & DSGW_DISPLAY_OPT_EDITABLE ) != 0 );
    adding = (( tip->dsti_options & DSGW_DISPLAY_OPT_ADDING ) != 0 );

    if ( entry == NULL && !adding ) {
	dsgw_error( DSGW_ERR_MISSINGINPUT, NULL, DSGW_ERROPT_EXIT, 0, NULL );
    }

    tip->dsti_ld = ld;
    tip->dsti_entry = entry;
    tip->dsti_attrsonly_entry = attrsonly_entry;

    if ( dn == NULL ) {
	if ( entry == NULL ) {
	    dn = "dn=unknown";
	} else if (( dn = ldap_get_dn( ld, entry )) == NULL ) {
	    dsgw_ldap_error( ld, DSGW_ERROPT_EXIT );
	}
    }
    tip->dsti_entrydn = dsgw_ch_strdup( dn );
    encodeddn = dsgw_strdup_escaped( dn );

    if ( adding ) {
	tip->dsti_rdncomps = dsgw_rdn_values( dn );
    }

    if ( tip->dsti_preludelines != NULL ) {
	output_prelude( tip );
    }


    dsgw_savelines_rewind( tip->dsti_entrylines );
    while (( line = dsgw_savelines_next( tip->dsti_entrylines )) != NULL ) {
	if ( dsgw_parse_line( line, &argc, &argv, 0, condition_is_true, tip )) {
	    if ( dsgw_directive_is( line, DRCT_DS_ATTRIBUTE )) {
		do_attribute( tip, dn, tip->dsti_options, argc, argv );

	    } else if ( dsgw_directive_is( line, DRCT_DS_ATTRVAL_SET )) {
		do_attrvalset( tip, dn, tip->dsti_options, argc, argv );

	    } else if ( dsgw_directive_is( line, DRCT_DS_ORGCHARTLINK )) {
		do_orgchartlink( tip, dn, tip->dsti_options, argc, argv );

	    } else if ( dsgw_directive_is( line, DRCT_DS_EMIT_BASE_HREF )) {
		char *p;
		char *sname = dsgw_ch_strdup( getenv( "SCRIPT_NAME" ));
		if (( p = strrchr( sname, '/' )) != NULL ) {
		    *p = '\0';
		}
		dsgw_emitf( "<BASE HREF=\"%s%s/\">\n",
			getenv( "SERVER_URL" ), sname );

	    } else if ( dsgw_directive_is( line, DRCT_DS_BEGIN_DNSEARCHFORM )) {
		dsgw_form_begin ( "searchForm", "action=\"%s\" %s %s",
				 dsgw_getvp( DSGW_CGINUM_DOSEARCH ),
				 "target=stagingFrame",
				 "onSubmit=\"return parent.processSearch(searchForm);\"" );
		dsgw_emitf( "\n<INPUT TYPE=\"hidden\" NAME=\"dn\" VALUE=\"%s\";>\n", encodeddn );

	    } else if ( dsgw_directive_is( line, DRCT_DS_BEGIN_ENTRYFORM )) {
		if ( editable ) {
		    dsgw_form_begin("modifyEntryForm","ACTION=\"%s\"",
				    dsgw_getvp( DSGW_CGINUM_DOMODIFY ));
		    dsgw_emits( "\n<INPUT TYPE=hidden NAME=\"changetype\">\n" );
		    dsgw_emitf( "<INPUT TYPE=hidden NAME=\"dn\" VALUE=\"%s\">\n",
				encodeddn );
		    dsgw_emits( "<INPUT TYPE=hidden NAME=\"changed_DN\" VALUE=false>\n");
		    dsgw_emits( "<INPUT TYPE=hidden NAME=\"deleteoldrdn\" VALUE=true>\n");

		} else {
		    dsgw_form_begin("editEntryForm", "action=\"%s\" %s",
				    dsgw_getvp( DSGW_CGINUM_AUTH ),
				    "target=\"_blank\"" );
		    dsgw_emits( "\n" );
		}

	    } else if ( dsgw_directive_is( line, DRCT_DS_END_ENTRYFORM )) {
		dsgw_emitf( "</FORM>\n" );
		dsgw_emit_confirmForm();

	    } else if ( dsgw_directive_is( line, DRCT_DS_END_DNSEARCHFORM )) {
		dsgw_emitf( "</FORM>\n" );
		dsgw_emit_alertForm();
		dsgw_emit_confirmForm();

	    } else if ( dsgw_directive_is( line, DRCT_DS_EDITBUTTON )) {
		if ( !editable ) do_editbutton( dn, encodeddn, argc, argv );

	    } else if ( dsgw_directive_is( line, DRCT_DS_DELETEBUTTON )) {
		if ( editable && !adding ) do_deletebutton( argc, argv );

	    } else if ( dsgw_directive_is( line, DRCT_DS_RENAMEBUTTON )) {
		/* if ( editable && !adding ) do_renamebutton( dn, argc, argv ); */

	    } else if ( dsgw_directive_is( line, DRCT_DS_EDITASBUTTON )) {
		if ( editable ) do_editasbutton( argc, argv );

	    } else if ( dsgw_directive_is( line, DRCT_DS_SAVEBUTTON )) {
		if ( editable ) do_savebutton( tip->dsti_options, argc, argv );

	    } else if ( dsgw_display_line( tip, line, argc, argv )) {

	    } else if ( dsgw_directive_is( line, DRCT_DS_NEWPASSWORD )) {
		if ( editable ) do_passwordfield( tip->dsti_options, argc,
			argv, "newpasswd" );

	    } else if ( dsgw_directive_is( line, DRCT_DS_CONFIRM_NEWPASSWORD )) {
		if ( editable ) do_passwordfield( tip->dsti_options, argc,
			argv, "newpasswdconfirm" );

	    } else if ( dsgw_directive_is( line, DRCT_DS_OLDPASSWORD )) {
		if ( editable ) do_passwordfield( tip->dsti_options, argc,
			argv, "passwd" );

	    } else if ( dsgw_directive_is( line, DRCT_DS_DNATTR )) {
		if ( dsgw_dnattr != NULL ) dsgw_emits( dsgw_dnattr );

	    } else if ( dsgw_directive_is( line, DRCT_DS_DNDESC )) {
		if ( dsgw_dndesc != NULL ) dsgw_emits( dsgw_dndesc );
	    
	    } else if ( dsgw_directive_is( line, DRCT_DS_DNEDITBUTTON )) {
		if ( editable ) {
		    do_dneditbutton( tip->dsti_options, argc, argv );
		}

	    } else if ( dsgw_directive_is( line, "DS_DNADDBUTTON" )) {
		dsgw_emits ("<INPUT TYPE=SUBMIT");
		{
		    auto char* v = get_arg_by_name (DSGW_ATTRARGS_VALUE, argc, argv);
		    if (v) dsgw_emitf (" VALUE=\"%s\"", v);
		}
		dsgw_emits (">\n");

	    } else if ( dsgw_directive_is( line, "DS_DNREMOVEBUTTON" )) {
		dsgw_emits ("<INPUT TYPE=BUTTON");
		{
		    auto char* v = get_arg_by_name (DSGW_ATTRARGS_VALUE, argc, argv);
		    if (v) dsgw_emitf (" VALUE=\"%s\"", v);
		}
		dsgw_emits (" onClick=\"if (parent.processSearch(searchForm)) {"
			               "searchForm.faMode.value='remove';"
			               "searchForm.submit();"
			               "searchForm.searchstring.select();"
			               "searchForm.searchstring.focus();"
			    "}\">\n");

	    } else if ( dsgw_directive_is( line, DRCT_DS_VIEW_SWITCHER ) &&
		    tip->dsti_entry != NULL ) {
		do_viewswitcher( tip->dsti_template, tip->dsti_entrydn,
			argc, argv );

	    } else if ( dsgw_directive_is( line, DRCT_DS_STD_COMPLETION_JS )) {
		do_std_completion_js( tip->dsti_template, argc, argv );

	    } else {
		dsgw_emits( line );
	    }

	    dsgw_argv_free( argv );
	}
    }

    free( encodeddn );
}

static void
dsgw_setstr (char** into, const char* from)
{
    if (from) {
	auto const size_t len = strlen (from) + 1;
	*into = dsgw_ch_realloc (*into, len);
	memmove (*into, from, len);
    } else if (*into) {
	free (*into);
	*into = NULL;
    }
}

void
dsgw_set_searchdesc( dsgwtmplinfo *tip, char *s2, char *s3, char *s4 )
{
    dsgw_setstr( &(tip->dsti_search2s), s2 );
    dsgw_setstr( &(tip->dsti_search3s), s3 );
    dsgw_setstr( &(tip->dsti_search4s), s4 );
}

void
dsgw_set_search_result( dsgwtmplinfo *tip, int entrycount, char *searcherror,
	char *lderrtxt )
{
    tip->dsti_entrycount = entrycount;
    dsgw_setstr( &(tip->dsti_searcherror), searcherror );
    dsgw_setstr( &(tip->dsti_searchlderrtxt), lderrtxt );
}


void
dsgw_display_done( dsgwtmplinfo *tip )
{
    char	line[ BIG_LINE ], *jscomp;

    if ( tip->dsti_preludelines != NULL ) {
	output_prelude( tip );
    }

    while ( dsgw_next_html_line( tip->dsti_fp, line )) {
	output_nonentry_line( tip, line );
    }

    /*
     * check for "completion_javascript" form var and
     * execute it if present.
     */ 
    jscomp = dsgw_get_cgi_var( "completion_javascript",
	    DSGW_CGIVAR_OPTIONAL );
    if ( jscomp != NULL ) {
	dsgw_emits( "<SCRIPT LANGUAGE=\"JavaScript\">\n" );
	dsgw_emitf( "eval('%s');\n", jscomp );
	dsgw_emits( "</SCRIPT>\n" );
    }

    fflush( stdout );
    fflush( stdout );

    dsgw_savelines_free( tip->dsti_entrylines );
    fclose( tip->dsti_fp );
    if ( tip->dsti_attrs != NULL ) {
	ldap_value_free( tip->dsti_attrs );
    }
    if ( tip->dsti_attrflags != NULL ) {
	free( tip->dsti_attrflags );
    }
    if ( tip->dsti_rdncomps != NULL ) {
	ldap_value_free( tip->dsti_rdncomps );
    }
    free( tip );
}


static void
output_prelude( dsgwtmplinfo *tip )
{
    int		editable, adding;
    char	*line, *encodeddn;

    if ( tip->dsti_preludelines != NULL ) {	/* output the prelude */
	dsgw_savelines_rewind( tip->dsti_preludelines );
	while (( line = dsgw_savelines_next( tip->dsti_preludelines ))
		!= NULL ) {
	    output_nonentry_line( tip, line );
	}
	dsgw_savelines_free( tip->dsti_preludelines );
	tip->dsti_preludelines = NULL;
    }

    /* output any JavaScript functions we want to include before the entry */
    dsgw_emits( "<SCRIPT LANGUAGE=\"JavaScript\">\n" );
    dsgw_emits( "<!-- Hide from non-JavaScript-capable browsers\n" );
    dsgw_emits( "var emptyFrame = '';\n" );
    editable = ( tip->dsti_options & DSGW_DISPLAY_OPT_EDITABLE ) != 0;
    adding = ( tip->dsti_options & DSGW_DISPLAY_OPT_ADDING ) != 0;

    if ( !editable ) {
	char *urlprefix = dsgw_ch_malloc( strlen(gc->gc_urlpfxmain) + 128);
	
	sprintf(urlprefix, "%semptyFrame.html", gc->gc_urlpfxmain);

	/* include the functions used to support "Edit" buttons */
	/* function haveAuthCookie() */
	dsgw_emits( "function haveAuthCookie()\n{\n" );
	dsgw_emitf( "    return ( document.cookie.indexOf( '%s=' ) >= 0 "
		"&& document.cookie.indexOf( '%s=%s' ) < 0 );\n}\n\n",
		DSGW_AUTHCKNAME, DSGW_AUTHCKNAME, DSGW_UNAUTHSTR );

	/* function authOrEdit() -- calls haveAuthCookie() */
	dsgw_emits( "function authOrEdit(encodeddn)\n{\n" );
	dsgw_emitf( "    editURL = '%s?context=%s&dn=' + encodeddn;\n",
		dsgw_getvp( DSGW_CGINUM_EDIT ), context);
	dsgw_emits( "    if ( haveAuthCookie()) {\n" );
	dsgw_emits( "\tnw = open(editURL, \"_blank\");\n" );
	dsgw_emits( "\twindow.location.href = " );
	dsgw_quote_emits (QUOTATION_JAVASCRIPT, urlprefix);
	dsgw_emits( ";\n"
		"    } else {\n"
		"\tdocument.editEntryForm.authdesturl.value = editURL;\n"
		"\ta = open(");
	dsgw_quote_emits (QUOTATION_JAVASCRIPT, urlprefix);

	free(urlprefix);
	urlprefix = NULL;
	dsgw_emits(", 'AuthWin');\n"
		"\ta.opener = self;\n"
		"\ta.closewin = true;\n"
		"\tdocument.editEntryForm.target = 'AuthWin';\n"
		"\tdocument.editEntryForm.submit();\n"
		"    }\n}\n" );

    } else {
	/* include variables and functions used to support edit mode */
	dsgw_emits( "var changesHaveBeenMade = 0;\n\n" );
	dsgw_emits( "var possiblyChangedAttr = null;\n\n" );

	/* function aChg() -- called from onChange and onClick handlers */
	dsgw_emits( "function aChg(attr)\n{\n" );
	if ( !adding ) {
	    dsgw_emits( "    cmd = 'document.modifyEntryForm.changed_' + "
		    "attr + '.value = \"true\"';\n" );
	    dsgw_emits( "    eval( cmd );\n    possiblyChangedAttr = null;\n" );
	}
	dsgw_emits( "    changesHaveBeenMade = 1;\n}\n\n" );


	if ( !adding ) {
	    /* function aFoc() -- called when text area gets focus. */
	    dsgw_emits( "function aFoc(attr)\n{\n"
		    "   possiblyChangedAttr = attr;\n}\n\n" );
	}

	/* function submitModify() */
	dsgw_emits( "function submitModify(changetype)\n{\n" );
	if ( !adding ) {
	    dsgw_emits( "\tif ( possiblyChangedAttr != null ) "
		    "aChg(possiblyChangedAttr);\n" );
	}
	dsgw_emits( "\tdocument.modifyEntryForm.changetype.value = changetype;\n" );
	dsgw_emits( "\tdocument.modifyEntryForm.submit();\n}\n" );

	/* function confirmModify() */
	dsgw_emits( "var changetype = '';\n\n" );
	dsgw_emits( "function confirmModify(ctype, prompt)\n{\n" );
	dsgw_emits( "	changetype = ctype;\n" );
	dsgw_emit_confirm (NULL, "opener.submitModify(opener.changetype);", NULL/*no*/,
			   NULL /* options */, 0, "prompt");
	dsgw_emits( "}\n" );

	/* function EditEntryAs() */
/*
	dsgw_emits( "function EditEntryAs(template)\n{\n" );
	dsgw_emits( "    newurl = window.location.protocol + '//' +\n"
		"\twindow.location.host +\n"
		"\twindow.location.pathname + '?' + template;\n" );
	dsgw_emits( "\twindow.location.href = newurl;\n}\n" );
*/

	if ( tip->dsti_entrydn != NULL ) {
	    encodeddn = dsgw_strdup_escaped( tip->dsti_entrydn );
	    dsgw_emits( "function EditEntryAs(template)\n{\n" );
	    dsgw_emitf( "    newurl = '%s?' + template + '&context=%s&dn=%s';\n",
		    dsgw_getvp( DSGW_CGINUM_EDIT ), context, encodeddn );
	    dsgw_emits( "\twindow.location.href = newurl;\n}\n" );
	}

	/* function DNEdit() */
	if ( tip->dsti_entrydn != NULL ) {
	    encodeddn = dsgw_strdup_escaped( tip->dsti_entrydn );
	    dsgw_emits( "var DNEditURL;\n" );
	    dsgw_emits( "function DNEdit(template, attr, desc)\n{\n" );
	    dsgw_emitf( "    DNEditURL = '%s?template=' + template + "
	                               "'&dn=%s&context=%s&ATTR=' + attr + '&DESC=' + escape(desc);\n", 
	                               dsgw_getvp( DSGW_CGINUM_DNEDIT ), encodeddn, context );
	    dsgw_emits( "    if( !changesMade() ) window.location.href = DNEditURL;\n"
		        "    else {\n");
	    dsgw_emit_confirm( NULL, "opener.location.href = opener.DNEditURL;", NULL/*no*/,
			       XP_GetClientStr(DBT_continueWithoutSavingWindow_), 1,
			       XP_GetClientStr(DBT_continueWithoutSaving_));
	    dsgw_emits( "    }\n");
	    dsgw_emits( "}\n" );
	}

	/* function changesMade() */
	dsgw_emits( "function changesMade()\n{\n" );
	if ( !adding ) {
	    dsgw_emits( "\tif ( possiblyChangedAttr != null ) "
		    "aChg(possiblyChangedAttr);\n" );
	}
	dsgw_emits( "    return( changesHaveBeenMade );\n}\n" );

	/* function closeIfOK() */
	dsgw_emits( "function closeIfOK()\n{\n"
		    "    if ( !changesMade() ) top.close();\n"
		    "    else {\n" );
	dsgw_emit_confirm( NULL, "opener.top.close();", NULL/*no*/,
			   XP_GetClientStr(DBT_discardChangesWindow_), 1,
			   XP_GetClientStr(DBT_discardChanges_));
	dsgw_emits( "    }\n}\n" );

	/* set unload handler to catch unsaved changes */
	dsgw_emits( "document.onUnload = \""
		"return ( !changesMade() || prompt( 'Discard Changes?' ));\"\n" );
    }

    dsgw_emits( "// End hiding -->\n</SCRIPT>\n" );
}


static void
output_nonentry_line( dsgwtmplinfo *tip, char *line )
{
    int		argc;
    char	**argv;

    if ( dsgw_parse_line( line, &argc, &argv, 0, condition_is_true, tip )) {
	if ( dsgw_directive_is( line, DRCT_DS_SEARCHDESC )) {
	    do_searchdesc( tip, argc, argv );
	} else if ( dsgw_display_line ( tip, line, argc, argv )) {
	} else {
	    dsgw_emits( line );
	}
	dsgw_argv_free( argv );
    }
}

static char*
find_RDN (char* DN, char* attr, char** vals)
     /* Return a copy of the vals[i] that is
	part of the RDN of the given DN.
     */
{
    if (DN && *DN && vals && *vals) {
	auto char** RDNs = ldap_explode_dn (DN, 0);
	auto char** AVAs = ldap_explode_rdn (RDNs[0], 0);
	ldap_value_free (RDNs);
	if (AVAs) {
	    auto char** val = NULL;
	    auto char** AVA;
	    for (AVA = AVAs; *AVA; ++AVA) {
		auto char* RDN = strchr (*AVA, '=');
		if (RDN) {
		    *RDN++ = '\0';
		    if (!strcasecmp (*AVA, attr)) {
			for (val = vals; *val; ++val) {
			    if (!strcmp (RDN, *val)) {
				break;
			    }
			}
			if (*val) break;
			/* bug: what if there are other AVAs
			   that also match attr and one of vals?
			   Even if this algorithm could find them,
			   it couldn't return them (the function
			   return value can't express multiple
			   values).
			*/
		    }
		}
	    }
	    ldap_value_free (AVAs);
	    if (val) return *val;
	}
    }
    return NULL;
}

/*static int
 *is_aim_online(dsgwtmplinfo *tip) 
 *{
 * char **ldvals = (char **) dsgw_get_values(tip->dsti_ld, tip->dsti_entry, DSGW_ATTRTYPE_AIMSTATUSTEXT, 0);
 *
 * if (ldvals == NULL || *ldvals == NULL || strcmp(*ldvals, "") == 0 ) {
 *   return(0);
 * }
 * return(1);
 *
 *}
 */
static void
do_orgchartlink( dsgwtmplinfo *tip, char *dn, unsigned long dispopts,
	int argc, char **argv )
{
  char **ldvals = (char **) dsgw_get_values(tip->dsti_ld, tip->dsti_entry, gc->gc_orgchartsearchattr, 0);
  char *escaped_value;

  if (gc->gc_orgcharturl == NULL || ldvals == NULL || *ldvals == NULL || strcmp(*ldvals,"") == 0) {
    dsgw_emits("\"javascript:void(0)\"");
    return;
  }
  dsgw_emits("\"");
  dsgw_emits(gc->gc_orgcharturl);
  escaped_value = dsgw_ch_malloc( 3 * strlen( ldvals[0] ) + 1 );
  *escaped_value = '\0';
  dsgw_strcat_escaped( escaped_value, ldvals[0]);
  dsgw_emits(escaped_value);
  dsgw_emits("\"\n");

  return;
}

static void
do_attribute( dsgwtmplinfo *tip, char *dn, unsigned long dispopts,
	int argc, char **argv )
{
    char			*attr, *syntax, *defval, *tmpvals[ 2 ], *s;
    char			**ldvals, **vals;
    unsigned long		options;
    int				i, len, attrindex, htmltype;
    struct dsgw_attrdispinfo	adi;
    int                         editable = 0;
    int                         tagged_attrs = 0;
    int                         binary_value = 0;
    
    if (( attr = get_arg_by_name( DSGW_ATTRARG_ATTR, argc, argv )) == NULL ) {
	dsgw_emitf( XP_GetClientStr(DBT_missingS_), DSGW_ATTRARG_ATTR );
	return;
    }
    if (( syntax = get_arg_by_name( DSGW_ATTRARG_SYNTAX, argc, argv ))
	    == NULL ) {
	syntax = "cis";
    }

    if (( s = get_arg_by_name( DSGW_ATTRARG_HTMLTYPE, argc, argv )) == NULL ) {
	htmltype = DSGW_ATTRHTML_TEXT;
    } else {
	for ( i = 0; attrhtmltypes[ i ] != NULL; ++i ) {
	    if ( strcasecmp( s, attrhtmltypes[ i ] ) == 0 ) {
		htmltype = attrhtmlvals[ i ];
		break;
	    }
	}
	if ( attrhtmltypes[ i ] == NULL ) {
	    dsgw_emitf( XP_GetClientStr(DBT_unknownSS_), DSGW_ATTRARG_HTMLTYPE, s );
	    return;
	}
    }

    options = get_attr_options( argc, argv );

    if (( options & DSGW_ATTROPT_TYPEONLY ) != 0 ) {
	return;	/* don't actually display attr. if we only retrieved types */
    }

    if (( options & DSGW_ATTROPT_LINK ) != 0 ) {
	/*
	 * Output a "dosearch" URL that will retrieve this attribute.
	 * These used to look like:
	 *   .../dosearch/<host>:<port>?dn=<encodeddn>&<attr>&<mimetype>&<valindex>
	 *
	 * Now, thanks to me, they look like:
	 *   .../dosearch?context=<blah>&hp=<host>:<port>&dn=<encodeddn>&ldq=<the rest>
	 *     - RJP
	 */
	char    *urlprefix, *escapeddn, *mimetype, *prefix, *suffix;

	urlprefix = dsgw_build_urlprefix();
	escapeddn = dsgw_strdup_escaped( dn );
	mimetype = get_arg_by_name( DSGW_ATTRARG_MIMETYPE, argc, argv );
	if (( prefix = get_arg_by_name( "prefix", argc, argv )) == NULL ) {
	    prefix = "";
	}
	if (( suffix = get_arg_by_name( "suffix", argc, argv )) == NULL ) {
	    suffix = "";
	}

	/* XXXmcs
	 * always reference first value for now ( "&0" ) unless returning
	 * link to a vCard (in which case we leave the &0 off)
	 */
	dsgw_emitf("%s\"%s%s&ldq=%s&%s%s\"%s\n", prefix, urlprefix, escapeddn, attr,
		   ( mimetype == NULL ) ? "" : mimetype,
		    ( strcasecmp( "_vcard", attr ) == 0 ) ? "" : "&0", suffix );
	free( urlprefix );
	free( escapeddn );
	return;
    }

    if (( dispopts & DSGW_DISPLAY_OPT_EDITABLE ) != 0
	    && ( options & DSGW_ATTROPT_READONLY ) == 0 ) {
	options |= DSGW_ATTROPT_EDITABLE;
	editable = 1;
	if ((  dispopts & DSGW_DISPLAY_OPT_ADDING ) != 0 ) {
	    options |= DSGW_ATTROPT_ADDING;
	}
    }

    if (( dispopts & DSGW_DISPLAY_OPT_LINK2EDIT ) != 0 ) {
	options |= DSGW_ATTROPT_LINK2EDIT;
    }
    if ((options & DSGW_ATTROPT_QUOTED ) != 0 ) {
      options &= ~DSGW_ATTROPT_EDITABLE;/* always read-only */
      options &= ~DSGW_ATTROPT_ADDING;  /* always read-only */
      options |= DSGW_ATTROPT_READONLY;
    }

    ldvals = vals = NULL;

    if ( strcasecmp( attr, "dn" ) == 0 ) {	/* dn pseudo-attribute */
	tmpvals[ 0 ] = dn;
	tmpvals[ 1 ] = NULL;
	vals = tmpvals;
	options &= ~DSGW_ATTROPT_EDITABLE;	/* always read-only */
	options &= ~DSGW_ATTROPT_ADDING;	/* always read-only */
	options |= DSGW_ATTROPT_READONLY;
	} else if( strcasecmp( syntax, "binvalue" ) == 0) {

	    binary_value = 1;
	    /* Only display tagged stuff on searches */
	    if (editable){
		ldvals = (char **) ldap_get_values_len(tip->dsti_ld, tip->dsti_entry, attr);
		tagged_attrs = 0;
	    } else {
		ldvals = (char **) dsgw_get_values(tip->dsti_ld, tip->dsti_entry, attr, 1 /*binary value*/);
		tagged_attrs = 1;
	    }

	    if (ldvals != NULL) {
		vals = ldvals;
	    }
	} else if ( tip->dsti_entry != NULL) {

	    /* Only display tagged stuff on searches */
	    if ( editable){
		ldvals = (char **) ldap_get_values( tip->dsti_ld, tip->dsti_entry, attr);
		tagged_attrs = 0;
	    } else {
		ldvals = (char **) dsgw_get_values( tip->dsti_ld, tip->dsti_entry, attr, 0 );
		tagged_attrs = 1;
	    }
	    if (ldvals != NULL) {
		vals = ldvals;
	    }
	}

    if (vals == NULL && (options & DSGW_ATTROPT_QUOTED ) != 0 ) {
      dsgw_emits( "\"\"" );
      return;
    }

    if ( vals == NULL && tip->dsti_rdncomps != NULL
	    && ( options & DSGW_ATTROPT_ADDING ) != 0 ) {
	/*
	 * include values from the DN of new entry being added
	 */
	len = strlen( attr );
	ldvals = NULL;

	for ( i = 0; tip->dsti_rdncomps[ i ] != NULL; ++i ) {
	    if (( s = strchr( tip->dsti_rdncomps[ i ], '=' )) != NULL &&
		    s - tip->dsti_rdncomps[ i ] == len &&
		    strncasecmp( attr, tip->dsti_rdncomps[ i ], len ) == 0 ) {
		tmpvals[ 0 ] = ++s;
		tmpvals[ 1 ] = NULL;
		vals = tmpvals;
		break;
	    }
	}
    }

    if ( vals == NULL && ( defval = get_arg_by_name( DSGW_ATTRARG_DEFAULT,
	    argc, argv )) != NULL ) {
	tmpvals[ 0 ] = defval;
	tmpvals[ 1 ] = NULL;
	vals = tmpvals;
    }

    if ( vals == NULL && ( options & DSGW_ATTROPT_EDITABLE ) == 0 ) {
	if ( htmltype != DSGW_ATTRHTML_HIDDEN ) {
	    dsgw_HTML_emits( DSGW_UTF8_NBSP );
	}
    } else {
	if (( adi.adi_handlerp = syntax2attrhandler( syntax )) == NULL ) {
	    dsgw_emitf( XP_GetClientStr(DBT_unknownSyntaxSN_), syntax );
	} else {
	    if ( vals != NULL && vals[1] != NULL
			&& ( options & DSGW_ATTROPT_SORT ) != 0 ) {
		ldap_sort_values( tip->dsti_ld, vals,
				 dsgw_valcmp (adi.adi_handlerp->ath_compare));
	    }
	    adi.adi_attr = attr;
	    adi.adi_argc = argc;
	    adi.adi_argv = argv;
	    adi.adi_vals = vals;
	    adi.adi_rdn = NULL;
	    adi.adi_htmltype = htmltype;
	    adi.adi_opts = options;

	    if (( options & DSGW_ATTROPT_EDITABLE ) == 0 ) {
		(*adi.adi_handlerp->ath_display)( &adi );
	    } else {
		if (( options & DSGW_ATTROPT_ADDING ) == 0 ) {
		    /* set flag to track attrs. we have seen */
		    for ( attrindex = 0; tip->dsti_attrs[ attrindex ] != NULL;
			    ++attrindex ) {
			if ( strcasecmp( attr, tip->dsti_attrs[ attrindex ] )
				== 0 ) {
			    break;
			}
		    }
		    if ( tip->dsti_attrs[ attrindex ] != NULL ) {
			if ( ! (tip->dsti_attrflags[ attrindex ] & DSGW_DSTI_ATTR_SEEN)) {
			    tip->dsti_attrflags[ attrindex ] |= DSGW_DSTI_ATTR_SEEN;
			    dsgw_emitf( "<INPUT TYPE=hidden NAME=\"changed_%s\" VALUE=false>\n",
				        attr );
			}
			adi.adi_rdn = find_RDN( dn, attr, vals );
		    }
		}

		/* display for editing */
		(*adi.adi_handlerp->ath_edit)( &adi );
	    }
	}
    }

    if ( ldvals != NULL ) {
	if (tagged_attrs) {
	    dsgw_value_free( (void **) ldvals, binary_value );
	} else {
	    if (binary_value) {
		ldap_value_free_len( (struct berval **) ldvals );
	    } else {
		ldap_value_free( ldvals );
	    }
	}
    }
}



static void
append_to_array( char ***ap, int *countp, char *s )
{
    char	**a;
    int		count;

    a = *ap;
    count = *countp;

    a = (char **)dsgw_ch_realloc( a, ( count + 2 ) * sizeof( char * ));
    a[ count++ ] = dsgw_ch_strdup( s );
    a[ count ] = NULL;

    *ap = a;
    *countp = count;
}


static unsigned long
get_attr_options( int argc, char **argv )
{
    int			i;
    unsigned long	opts;
    char		*s;

    opts = 0;

    if (( s = get_arg_by_name( DSGW_ATTRARG_OPTIONS, argc, argv )) != NULL ) {
	char	*p, *q;

	for ( p = dsgw_ch_strdup( s ); p != NULL; p = q ) {
	    if (( q = strchr( p, ',' )) != NULL ) {
		*q++ = '\0';
	    }
	    for ( i = 0; attroptions[ i ] != NULL; ++i ) {
		if ( strcasecmp( p, attroptions[ i ] ) == 0 ) {
		    opts |= attroptvals[ i ];
		    break;
		}
	    }
	    if ( attroptions[ i ] == NULL ) {
		dsgw_emitf( XP_GetClientStr(DBT_unknownOptionS_), p );
		break;
	    }
	}
	free( p );
    }

    return( opts );
}


static struct attr_handler *
syntax2attrhandler( char *syntax )
{
    int		i;

    for ( i = 0; i < DSGW_AH_COUNT; ++i ) {
	if ( strcasecmp( syntax, attrhandlers[ i ].ath_syntax ) == 0 ) {
	    return( &attrhandlers[ i ] );
	}
    }

    return( NULL );
}


static int
numfields( int argc, char **argv, int valcount )
{
    char	*s;
    int		fields;

    if (( s = get_arg_by_name( DSGW_ATTRARGS_NUMFIELDS, argc,
	    argv )) == NULL ) {
	fields = 1;
    } else {
	if ( *s == '+' || *s == ' ') {
	    /* "numfields=+N" means show N more than number of values */
	    fields = valcount + atoi( s + 1 );
	} else {
	    if ( *s == '>' ) ++s;
	    /* "numfields=N" or "=>N" means show at least N fields */
	    fields = atoi( s );
	}
    }

    if ( fields < 1 ) {
	fields = 1;
    } else if ( fields < valcount ) {
	fields = valcount;
    }

    return( fields );
}

/*
 * calculate size of TEXT or TEXTAREA elements based on arguments,
 * the number of values, and the length of longest value.
 */
static void
element_sizes( int argc, char **argv, char **vals, int valcount,
	int *rowsp, int *colsp )
{
    int		i, len, maxlen;
    char	*s;

    /* set *colsp (number of columns in each input item) */
    if ( colsp != NULL ) {
	/*
	 * columns are set using the "cols=N" or "size=N" argument
	 * "cols=>N" can be used to indicate at least N columns should be shown
	 * "cols=+N" can be used to size to N more than longest value
	 * in the absence of any of these, we set columns to one more than
	 * the longest value in the "vals" array
	 */
	if (( s = get_arg_by_name( DSGW_ATTRARGS_COLS, argc, argv )) == NULL ) {
	    s = get_arg_by_name( DSGW_ATTRARGS_SIZE, argc, argv );
	}

	if ( s != NULL && *s != '+' && *s != ' ' && *s != '>' ) {
	    *colsp = atoi( s );	/* extact width specified */
	} else if ( valcount == 0 ) {
	    if ( s != NULL && *s == '>' ) {
		*colsp = atoi( s + 1 );
	    } else {
		*colsp = 0;	/* use default width */
	    }
	} else {
	    /* determine ( length of longest value ) + 1  */
	    maxlen = 0;
	    for ( i = 0; i < valcount; ++i ) {
		if (( len = strlen( vals[ i ] )) > maxlen ) {
		    maxlen = len;
		}
	    }
	    ++maxlen;

	    if ( s != NULL ) {
		i = atoi( s + 1 );
		if ( *s == ' ' || *s == '+' ) {
		    maxlen += i;
		} else {	/* '>' */
		    if ( maxlen < i ) {
			maxlen = i;
		    }
		}
	    }
	    *colsp = maxlen;
	}
    }

    /* set *rowsp (number of rows in each input item) */
    if ( rowsp != NULL ) {
	/*
	 * rows are set using "rows=M" ("=>M" and "=+M" are supported also)
	 * in the absense of this, we set it to the number of values in the
         * "vals" array
	 */
	if (( s = get_arg_by_name( DSGW_ATTRARGS_ROWS, argc, argv )) == NULL ) {
	    *rowsp = valcount;
	} else if ( *s == ' ' || *s == '+' ) {
	    *rowsp = valcount + atoi( s + 1 );
	} else if ( *s == '>' ) {
	    if (( *rowsp = atoi( s + 1 )) < valcount ) {
		*rowsp = valcount;
	    }
	} else {
	    *rowsp = atoi( s );
	}
    }
}


static void
output_text_elements( int argc, char **argv, char *attr, char **vals,
	const char* rdn, char *prefix, int htmltype, unsigned long opts )
{
    int		i, valcount, fields, cols;

    if ( vals == NULL ) {
	valcount = 0;
    } else {
	for ( valcount = 0; vals[ valcount ] != NULL; ++valcount ) {
		/* just count vals  */
	}
	}

    fields = numfields( argc, argv, valcount );
    element_sizes( argc, argv, vals, valcount, NULL, &cols );

    for ( i = 0; i < fields; ++i ) {
	auto const int is_rdn = (i < valcount && vals[ i ] == rdn);

	dsgw_emitf( "<INPUT TYPE=\"%s\"", attrhtmltypes[ htmltype ] );

	dsgw_emitf( " NAME=\"%s%s%s\"", prefix, is_rdn ? "DN_" : "", attr );
	if ( cols > 0 ) {
	    dsgw_emitf( " SIZE=%d", cols );
	}
	
	if ( i < valcount ) {
	    dsgw_emitf( " VALUE=\"%s\"", vals[ i ] );
	}
	
	if (( opts & DSGW_TEXTOPT_CHANGEHANDLERS ) != 0 ) {
	    dsgw_emitf( " onChange=\"aChg('%s')\"", is_rdn ? "DN" : attr );
	}
	if (( opts & DSGW_TEXTOPT_FOCUSHANDLERS ) != 0 ) {
	    dsgw_emitf( " onFocus=\"aFoc('%s')\"", is_rdn ? "DN" : attr );
	}

	dsgw_emitf( ">%s\n%s",
		is_rdn ? " DN" : "",
		( i < fields - 1 &&
		htmltype != DSGW_ATTRHTML_HIDDEN ) ? "<BR>\n" : "" );
    }
}


static void
output_textarea( int argc, char **argv, char *attr, char **vals,
	int valcount, char *prefix, unsigned long opts )
{
    int		i, rows, cols;

    element_sizes( argc, argv, vals, valcount, &rows, &cols );

    dsgw_emits( "<TEXTAREA" );
    dsgw_emitf( " NAME=\"%s%s\"", prefix, attr );
    if ( rows > 0 ) {
	if ( rows == 1 ) {
	    rows = 2;	/* one line TEXTAREAs are ugly! */
	}
	dsgw_emitf( " ROWS=%d", rows );
    }

    if ( cols > 0 ) {
	dsgw_emitf( " COLS=%d", cols );
    }

    if (( opts & DSGW_TEXTOPT_CHANGEHANDLERS ) != 0 ) {
	dsgw_emitf( " onChange=\"aChg('%s')\"", attr );
    }
    if (( opts & DSGW_TEXTOPT_FOCUSHANDLERS ) != 0 ) {
	dsgw_emitf( " onFocus=\"aFoc('%s')\"", attr );
    }

    dsgw_emits( ">\n" );

    for ( i = 0; i < valcount; ++i ) {
	dsgw_emits( vals[ i ] );
	dsgw_emits( "\n" );
    }

    dsgw_emits( "</TEXTAREA>\n" );
}


static void
output_text_checkbox_or_radio( struct dsgw_attrdispinfo *adip, char *prefix,
	int htmltype )
{
    int		i, checked;
    char	*value;

    /*
     * for checkboxes or radio buttons that are associated with string values,
     * we "check the box" if the value found in the "value=XXX" parameter is
     * present.
     */
    checked = 0;
    if (( value = get_arg_by_name( DSGW_ATTRARGS_VALUE, adip->adi_argc,
	    adip->adi_argv )) == NULL ) {
	value = "TRUE";	/* assume LDAP Boolean value */
    }
    if ( adip->adi_vals == NULL ) {
	if ( *value == '\0' ) {
	    /*
	     * There are no existing values in the entry and this checkbox or
	     * radio button has a zero-length value associated with it.  We
	     * check this box/enable this radio button as a special case to
	     * support an "off" or "none of the rest" scenario. 
	     */
	    checked = 1;
	}

    } else {
	for ( i = 0; adip->adi_vals[ i ] != NULL; ++i ) {
	    if ( dsgw_valcmp(adip->adi_handlerp->ath_compare)( (const char **)&value,
		    (const char **)&(adip->adi_vals[ i ]) ) == 0 ) {
		checked = 1;
		break;
	    }
	}
    }
    dsgw_emitf( "<INPUT TYPE=\"%s\" NAME=\"%s%s\" "
	    "VALUE=\"%s\"%s onClick=\"aChg('%s')\">\n",
	    ( htmltype == DSGW_ATTRHTML_RADIO ) ? "radio" : "checkbox",
	    prefix, adip->adi_attr, value, checked ? " CHECKED" : "",
	    adip->adi_attr );
}


static void
emit_value( char *val, int quote_html_specials )
{
    int		freeit;

    if ( quote_html_specials ) {
	val = dsgw_strdup_with_entities( val, &freeit );
    } else {
	freeit = 0;
    }

    dsgw_emits( val );

    if ( freeit ) {
	free( val );
    }
}


/*
 * Default display handler for binary values
 */
static void
binvalue_display( struct dsgw_attrdispinfo *adip )
{
    int		i;
    struct berval **list_of_binvals;
	char *checked = " CHECKED";
	char *selected = " SELECTED";
	int		iValue;

	list_of_binvals = (struct berval **)adip->adi_vals;

	for ( i = 0; list_of_binvals[ i ] != NULL; ++i ) 
	{
		char szFlags[512], szFormat[512];
		struct berval bin_data = *list_of_binvals[i];

		if( !bin_data.bv_val  || !bin_data.bv_len )
			continue;

		/* Now interpret the binary value if it has NT semantics */
		if( !strcasecmp( adip->adi_attr, "ntuserpriv") )
		{

			memcpy( &iValue, bin_data.bv_val, sizeof( iValue ) );
			fprintf( stdout, "<INPUT TYPE=\"radio\" NAME=\"%s\" "
			"VALUE=\"TRUE\"%s>%s<BR>\n", adip->adi_attr,
			(iValue == USER_PRIV_GUEST) ? checked : "", DSGW_NT_UP_GUEST);
			fprintf( stdout, "<INPUT TYPE=\"radio\" NAME=\"%s\" "
			"VALUE=\"TRUE\"%s>%s<BR>\n", adip->adi_attr,
			(iValue == USER_PRIV_USER) ? checked : "", DSGW_NT_UP_USER);
			fprintf( stdout, "<INPUT TYPE=\"radio\" NAME=\"%s\" "
			"VALUE=\"TRUE\"%s>%s<BR>\n", adip->adi_attr,
			(iValue == USER_PRIV_ADMIN) ? checked : "", DSGW_NT_UP_ADMIN);
		}
		else if ( strcasecmp( adip->adi_attr, "ntuserflags" ) == 0 ) 
		{
			memcpy( &iValue, bin_data.bv_val, sizeof( iValue ) );
			fprintf( stdout, "<FONT size=-1><SELECT MULTIPLE name=\"%s\" size=5>\n", adip->adi_attr);

			fprintf( stdout, "<OPTION value=\"%s\" %s>%s\n", DSGW_NT_UF_SCRIPT,
				(iValue & UF_SCRIPT) ? selected : "", DSGW_NT_UF_SCRIPT );
			fprintf( stdout, "<OPTION value=\"%s\" %s>%s\n", DSGW_NT_UF_ACCOUNT_DISABLED,
				(iValue & UF_ACCOUNTDISABLE) ? selected : "", 
							DSGW_NT_UF_ACCOUNT_DISABLED);
			fprintf( stdout, "<OPTION value=\"%s\" %s>%s\n", DSGW_NT_UF_HOMEDIR_REQD,
				(iValue & UF_HOMEDIR_REQUIRED) ? selected : "", 
							DSGW_NT_UF_HOMEDIR_REQD);
			fprintf( stdout, "<OPTION value=\"%s\" %s>%s\n", DSGW_NT_UF_PASSWD_NOTREQD,
				(iValue & UF_PASSWD_NOTREQD) ? selected : "", 
							DSGW_NT_UF_PASSWD_NOTREQD);
			fprintf( stdout, "<OPTION value=\"%s\" %s>%s\n", DSGW_NT_UF_PASSWD_CANT_CHANGE,
				(iValue & UF_PASSWD_CANT_CHANGE) ? selected : "", 
							DSGW_NT_UF_PASSWD_CANT_CHANGE);
			fprintf( stdout, "<OPTION value=\"%s\" %s>%s\n", DSGW_NT_UF_LOCKOUT,
				(iValue & UF_LOCKOUT) ? selected : "", DSGW_NT_UF_LOCKOUT);
			fprintf( stdout, "<OPTION value=\"%s\" %s>%s\n", DSGW_NT_UF_DONT_EXPIRE_PASSWORD,
				(iValue & UF_DONT_EXPIRE_PASSWD) ? selected : "", 
							DSGW_NT_UF_DONT_EXPIRE_PASSWORD);

			fprintf( stdout, "<OPTION value=\"%s\" %s>%s\n", DSGW_NT_UF_NORMAL_ACCOUNT,
				(iValue & UF_NORMAL_ACCOUNT) ? selected : "", 
							DSGW_NT_UF_NORMAL_ACCOUNT);
			fprintf( stdout, "<OPTION value=\"%s\" %s>%s\n", DSGW_NT_UF_TEMP_DUPLICATE_ACCOUNT,
				(iValue & UF_TEMP_DUPLICATE_ACCOUNT) ? selected : "", 
							DSGW_NT_UF_TEMP_DUPLICATE_ACCOUNT);
			fprintf( stdout, "<OPTION value=\"%s\" %s>%s\n", DSGW_NT_UF_TEMP_WRKSTN_TRUST_ACCOUNT,
				(iValue & UF_WORKSTATION_TRUST_ACCOUNT) ? selected : "", 
							DSGW_NT_UF_TEMP_WRKSTN_TRUST_ACCOUNT);
			fprintf( stdout, "<OPTION value=\"%s\" %s>%s\n", DSGW_NT_UF_TEMP_SERVER_TRUST_ACCOUNT,
				(iValue & UF_SERVER_TRUST_ACCOUNT) ? selected : "", 
							DSGW_NT_UF_TEMP_SERVER_TRUST_ACCOUNT);
			fprintf( stdout, "<OPTION value=\"%s\" %s>%s\n", DSGW_NT_UF_TEMP_INTERDOMAIN_TRUST_ACCOUNT,
				(iValue & UF_INTERDOMAIN_TRUST_ACCOUNT) ? selected : "", 
							DSGW_NT_UF_TEMP_INTERDOMAIN_TRUST_ACCOUNT);

			fprintf( stdout, "</SELECT><FONT size=+1>\n" );
		}
		else if ( strcasecmp( adip->adi_attr, "ntuserauthflags" ) == 0 ) 
		{
			memcpy( &iValue, bin_data.bv_val, sizeof( iValue ) );
			fprintf( stdout, "<FONT size=-1><SELECT MULTIPLE name=\"%s\" "
				"size=4>\n", adip->adi_attr);

			fprintf( stdout, "<OPTION value=\"%s\" %s>%s\n", DSGW_NT_AF_OP_PRINT,
				(iValue & AF_OP_PRINT) ? selected : "", DSGW_NT_AF_OP_PRINT);
			fprintf( stdout, "<OPTION value=\"%s\" %s>%s\n", DSGW_NT_AF_OP_COMM,
				(iValue & AF_OP_COMM) ? selected : "", DSGW_NT_AF_OP_COMM);
			fprintf( stdout, "<OPTION value=\"%s\" %s>%s\n", DSGW_NT_AF_OP_SERVER,
				(iValue & AF_OP_SERVER) ? selected : "", DSGW_NT_AF_OP_SERVER);
			fprintf( stdout, "<OPTION value=\"%s\" %s>%s\n", DSGW_NT_AF_OP_ACCOUNTS,
				(iValue & AF_OP_ACCOUNTS) ? selected : "", DSGW_NT_AF_OP_ACCOUNTS);

			fprintf( stdout, "</SELECT><FONT size=+1>\n" );			
		}
		else if ( bin_data.bv_val  && ( bin_data.bv_len != 0 ))
		{
			if( bin_data.bv_len == 4 )
			{
				memcpy( &iValue, bin_data.bv_val, sizeof( iValue ) );

				if(( adip->adi_opts & DSGW_ATTROPT_DECIMAL ) != 0 ) 
					PR_snprintf( szFormat, sizeof(szFormat), "%%lu" );
				else
					PR_snprintf( szFormat, sizeof(szFormat), "%%#0%lu.%lux", bin_data.bv_len*2, bin_data.bv_len*2 );
				PR_snprintf( szFlags, sizeof(szFlags), szFormat, iValue );

				fputs( szFlags, stdout );

				if ( list_of_binvals[ i + 1 ] != NULL ) 
				{
					fputs( "<BR>\n", stdout );
				}
			}
		}
	}
}

#if NEEDED_FOR_DEBUGGING
/*
 * display handler for NT Domain Identifier string
 */
static void
ntdomain_display( struct dsgw_attrdispinfo *adip )
{
    int		i;

    /* Write values with a break (<BR>) separating them, 
	removing all after ":" */
    for ( i = 0; adip->adi_vals[ i ] != NULL; ++i ) {
	if ( !did_output_as_special( adip->adi_argc, adip->adi_argv, 
		adip->adi_vals[ i ], adip->adi_vals[ i ] )) {
            char *pch = strchr( adip->adi_vals[ i ], DSGW_NTDOMAINID_SEP );
            if( pch )
               *pch = (char )NULL;
	    if ((adip->adi_opts & DSGW_ATTROPT_QUOTED ) != 0 ) {
		dsgw_emits( "\"" );
	    }
	    
	    fputs( adip->adi_vals[ i ], stdout );
	    if ((adip->adi_opts & DSGW_ATTROPT_QUOTED ) != 0 ) {
		dsgw_emits( "\"" );
	    }
	}

	if ( adip->adi_vals[ i + 1 ] != NULL ) {
	    fputs( "<BR>\n", stdout );
	}
    }

}
#endif


/*
 * display handler for simple strings
 */
static void
str_display( struct dsgw_attrdispinfo *adip )
{
    int		i;

    if ( adip->adi_htmltype == DSGW_ATTRHTML_CHECKBOX ||
	    adip->adi_htmltype == DSGW_ATTRHTML_RADIO ) {
	output_text_checkbox_or_radio( adip, "", adip->adi_htmltype );
	return;
    }

    /* just write values with a break (<BR>) separating them */
    for ( i = 0; adip->adi_vals[ i ] != NULL; ++i ) {

	if ( !did_output_as_special( adip->adi_argc, adip->adi_argv,
		adip->adi_vals[ i ], adip->adi_vals[ i ] ) &&
		adip->adi_htmltype != DSGW_ATTRHTML_HIDDEN ) {
	  if ((adip->adi_opts & DSGW_ATTROPT_QUOTED ) != 0 ) {
	    dsgw_emits( "\"" );
	  }
	  emit_value( adip->adi_vals[ i ], 
		    (( adip->adi_opts & DSGW_ATTROPT_NO_ENTITIES ) == 0 ));
	  if ((adip->adi_opts & DSGW_ATTROPT_QUOTED ) != 0 ) {
	    dsgw_emits( "\"" );
	  }
	}

	if ( adip->adi_htmltype != DSGW_ATTRHTML_HIDDEN &&
		adip->adi_vals[ i + 1 ] != NULL ) {
	    dsgw_emits( "<BR>\n" );
	}
    }

}


static void
ntuserid_display( struct dsgw_attrdispinfo *adip )
{
    int		i;

    for ( i = 0; adip->adi_vals[ i ] != NULL; ++i ) {
	if ( !did_output_as_special( adip->adi_argc, adip->adi_argv, 
		  adip->adi_vals[ i ], adip->adi_vals[ i ] )) {
            char *pch = adip->adi_vals[ i ];
            if( pch ) {

		if ((adip->adi_opts & DSGW_ATTROPT_QUOTED ) != 0 ) {
		    dsgw_emits( "\"" );
		}
		
		fputs( pch, stdout );
		if ((adip->adi_opts & DSGW_ATTROPT_QUOTED ) != 0 ) {
		    dsgw_emits( "\"" );
		}
	    }
	}

	if ( adip->adi_vals[ i + 1 ] != NULL ) {
	    fputs( "<BR>\n", stdout );
	}
    }

}



/*
 * edit handler for simple strings
 */
static void
str_edit( struct dsgw_attrdispinfo *adip )
{
    int			valcount, adding, pre_idx;
    char		*prefix;
    unsigned long	textopts;

    adding = (( adip->adi_opts & DSGW_ATTROPT_ADDING ) != 0 );
    if (( adip->adi_opts & DSGW_ATTROPT_UNIQUE ) == 0 ) {
	pre_idx = DSGW_MOD_PREFIX_NORMAL;
    } else {
	pre_idx = DSGW_MOD_PREFIX_UNIQUE;
    }
    prefix = adding ? add_prefixes[ pre_idx ] : replace_prefixes[ pre_idx ];

    textopts = DSGW_TEXTOPT_CHANGEHANDLERS;
    if ( !adding ) {
	textopts |= DSGW_TEXTOPT_FOCUSHANDLERS;
    }

    switch( adip->adi_htmltype ) {
    case DSGW_ATTRHTML_TEXTAREA:
	if ( adip->adi_vals == NULL ) {
	    valcount = 0;
	} else {
	    for ( valcount = 0; adip->adi_vals[ valcount ] != NULL;
		    ++valcount ) {
		;
	    }
	}
	output_textarea( adip->adi_argc, adip->adi_argv, adip->adi_attr,
		adip->adi_vals, valcount, prefix, textopts );
	break;

    case DSGW_ATTRHTML_TEXT:
    case DSGW_ATTRHTML_HIDDEN:
	output_text_elements( adip->adi_argc, adip->adi_argv, adip->adi_attr,
		adip->adi_vals, adip->adi_rdn, prefix, adip->adi_htmltype, textopts );
	break;

    case DSGW_ATTRHTML_CHECKBOX:
    case DSGW_ATTRHTML_RADIO:
	output_text_checkbox_or_radio( adip, prefix, adip->adi_htmltype );
	break;

    default:
	dsgw_emitf( XP_GetClientStr(DBT_HtmlTypeSNotSupportedBrN_),
		attrhtmltypes[ adip->adi_htmltype ] );
    }
}


/*
 * display handler for multi-line strings, e.g. postalAddress
 * these are funny in that over LDAP, lines are separated by " $ "
 * this only support "htmltype=text"
 */
static void
mls_display( struct dsgw_attrdispinfo *adip )
{
    int		i;

    for ( i = 0; adip->adi_vals[ i ] != NULL; ++i ) {
	if ( !did_output_as_special( adip->adi_argc, adip->adi_argv,
		adip->adi_vals[ i ], adip->adi_vals[ i ] )) {
	    (void)dsgw_mls_convertlines( adip->adi_vals[ i ], "<BR>\n", NULL,
		    1, ( adip->adi_opts & DSGW_ATTROPT_NO_ENTITIES ) == 0 );
	}

	if ( adip->adi_vals[ i + 1 ] != NULL ) {
	    dsgw_emits( "<BR><BR>\n" );
	}
    }
}


/*
 * edit handler for multi-line strings
 */
static void
mls_edit( struct dsgw_attrdispinfo *adip )
{
    char		*prefix, **valscopy, *tval[ 2 ];
    int			i, valcount, adding, pre_idx, *lines;
    unsigned long	textopts;

    adding = (( adip->adi_opts & DSGW_ATTROPT_ADDING ) != 0 );
    textopts = DSGW_TEXTOPT_CHANGEHANDLERS;
    if ( !adding ) {
	textopts |= DSGW_TEXTOPT_FOCUSHANDLERS;
    }

    if (( adip->adi_opts & DSGW_ATTROPT_UNIQUE ) == 0 ) {
	pre_idx = DSGW_MOD_PREFIX_NORMAL;
    } else {
	pre_idx = DSGW_MOD_PREFIX_UNIQUE;
    }
    prefix = adding ? add_mls_prefixes[ pre_idx ] :
	    replace_mls_prefixes[ pre_idx ];

    if ( adip->adi_vals == NULL ) {
	valscopy = NULL;
    } else {
	for ( valcount = 0; adip->adi_vals[ valcount ] != NULL; ++valcount ) {
	    ;
	}
	valscopy = (char **)dsgw_ch_malloc( (valcount + 1) * sizeof( char * ));
	lines = (int *)dsgw_ch_malloc( valcount * sizeof( int ));
	for ( i = 0; i < valcount; ++i ) {
	    valscopy[ i ] = dsgw_mls_convertlines( adip->adi_vals[ i ], "\n",
		    &lines[ i ], 0, 0 );
	}
	valscopy[ valcount ] = NULL;
    }

    if ( adip->adi_htmltype == DSGW_ATTRHTML_TEXTAREA ) {
	if ( adip->adi_vals == NULL ) {
	    output_textarea( adip->adi_argc, adip->adi_argv, adip->adi_attr,
		    NULL, 0, prefix, textopts );
	} else {
	    tval[ 1 ] = NULL;
	    for ( i = 0; i < valcount; ++i ) {
		tval[ 0 ] = valscopy[ i ];
		output_textarea( adip->adi_argc, adip->adi_argv,
			adip->adi_attr, tval, 1, prefix, textopts );
		if ( i < valcount - 1 ) {
		    dsgw_emits( "<BR>\n" );
		}
	    }
	}
    } else {
	output_text_elements( adip->adi_argc, adip->adi_argv, adip->adi_attr,
		valscopy, NULL, prefix, adip->adi_htmltype, textopts );
	/* Bug: what if adip->adi_rdn != NULL?  In this case,
	   the element of valscopy that is a copy of adi_rdn
	   should be passed to output_text_elements (as the rdn).
	*/
    }

    if ( valscopy != NULL ) {
	ldap_value_free( valscopy );
	free( lines );
    }
}


/*
 * convert all occurrences of "$" in val to sep
 * un-escape any \HH sequences
 * if linesp != NULL, set *linesp equal to number of lines in val
 * if emitlines is zero, a malloc'd string is returned. 
 * if emitlines is non-zero, values are written to stdout (respecting the
 *    quote_html_specials flag) and NULL is returned.
 */
char *
dsgw_mls_convertlines( char *val, char *sep, int *linesp, int emitlines,
	int quote_html_specials )
{
    char	*valcopy, *p, *q, *curline;
    int		i, c, lines, seplen;

    if ( sep == NULL ) {
	sep = "";
	seplen = 0;
    } else {
	seplen = strlen( sep );
    }

    lines = 0;
    for ( q = val; *q != '\0'; ++q ) {
	if ( *q == '$' ) {
	    ++lines;
	}
    }

    if ( linesp != NULL ) {
	*linesp = lines;
    }

    valcopy = dsgw_ch_malloc( strlen( val ) + lines * seplen + 1 );

    /*
     * p points to the place we are copying to
     * q points to the place within the original value that we are examining
     * curline points to the start of the current line
     */
    p = curline = valcopy;
    for ( q = val; *q != '\0'; ++q ) {
	if ( *q == '$' ) {		/* line separator */
	    if ( emitlines ) {
		*p = '\0';
		emit_value( curline, quote_html_specials );
		emit_value( sep, 0 );
	    }
	    strcpy( p, sep );
	    p += seplen;
	    curline = p;
	} else if ( *q == '\\' ) {	/* undo hex escapes */
	    if ( *++q == '\0' ) {
		break;
	    }
	    c = toupper( *q );
	    i = ( c >= 'A' ? ( c - 'A' + 10 ) : c - '0' );
	    i <<= 4;
	    if ( *++q == '\0' ) {
		break;
	    }
	    c = toupper( *q );
	    i += ( c >= 'A' ? ( c - 'A' + 10 ) : c - '0' );
	    *p++ = i;
	} else {
	    *p++ = *q;
	}
    }

    *p = '\0';

    if ( emitlines ) {
	if ( p > curline ) {
	    emit_value( curline, quote_html_specials );
	}
	free( valcopy );
	valcopy = NULL;
    }

    return( valcopy );
}


static void
dn_edit( struct dsgw_attrdispinfo *adip )
{
    if (( adip->adi_opts & DSGW_ATTROPT_DNPICKER ) != 0 ) {
	dn_display( adip );
    } else {
	str_edit( adip );
    }
    return;
}
    

static void
dn_display( struct dsgw_attrdispinfo *adip )
{
    int		i, j, len, dncomps;
    char	*p, *staticlabel, *tmps = NULL, *label, *urlprefix, **rdns = NULL;

    staticlabel = get_arg_by_name( DSGW_ATTRARG_LABEL, adip->adi_argc,
	    adip->adi_argv );

    if (( p = get_arg_by_name( DSGW_ATTRARG_DNCOMP, adip->adi_argc,
		adip->adi_argv )) == NULL ) { 
	dncomps = 1;
    } else {
	dncomps = atoi( p );	/* 0 or "all" means show all components */
    }

    if (( adip->adi_opts & DSGW_ATTROPT_LINK2EDIT ) != 0 ) {
	auto const char* vp = dsgw_getvp( DSGW_CGINUM_EDIT );
	/* urlprefix = vp + "?&context=CONTEXT&dn=": */
	auto const size_t vplen = strlen (vp);
	urlprefix = dsgw_ch_malloc (vplen + 6 + strlen(context) + 9);
	memcpy( urlprefix, vp, vplen );
	strcat( urlprefix, "?&context=");
	strcat( urlprefix, context);
	strcat( urlprefix, "&dn=");
    } else {
	urlprefix = dsgw_build_urlprefix();
    }
#ifdef DSGW_DEBUG
    dsgw_log( "dn_display: urlprefix is %s\n", urlprefix );
#endif

    for ( i = 0; adip->adi_vals != NULL && adip->adi_vals[ i ] != NULL; ++i ) {
	if ( staticlabel != NULL ) {
	    label = staticlabel;
	} else if ( !looks_like_dn( adip->adi_vals[ i ]) || 
		( rdns = ldap_explode_dn( adip->adi_vals[ i ],
		( adip->adi_opts & DSGW_ATTROPT_DNTAGS ) == 0 )) == NULL ) {
	    /* explode DN failed -- show entire DN */
	    label = adip->adi_vals[ i ];
	    tmps = NULL;
	} else {
	    len = 1;	/* room for zero-termination */
	    for ( j = 0; rdns[ j ] != NULL && ( dncomps == 0 || j < dncomps );
		    ++ j ) {
		len += ( 2 + strlen( rdns[ j ] ));	/* rdn + ", " */
	    }
	    label = p = tmps = dsgw_ch_malloc( len );
	    for ( j = 0; rdns[ j ] != NULL && ( dncomps == 0 || j < dncomps );
		    ++ j ) {
		if ( j > 0 ) {
		    strcpy( p, ", " );
		    p += 2;
		}
		strcpy( p, rdns[ j ] );
		p += strlen( p );
	    }
	}

	if ( !did_output_as_special( adip->adi_argc, adip->adi_argv, label,
		adip->adi_vals[ i ] )) {
	    if (( adip->adi_opts & DSGW_ATTROPT_NOLINK ) == 0 &&
		    looks_like_dn( adip->adi_vals[ i ] )) {
		if (( adip->adi_opts & DSGW_ATTROPT_DNPICKER ) != 0 ) {
		    dsgw_emits( "<TR><TD>" );
		}
		/* Don't display a link for the rootdn */
		if ( gc->gc_rootdn && dsgw_dn_cmp(adip->adi_vals[i], gc->gc_rootdn)) {
		  if ((adip->adi_opts & DSGW_ATTROPT_QUOTED ) != 0 ) {
		    dsgw_emits( "\"" );
		  }
		  dsgw_emits( label );
		  if ((adip->adi_opts & DSGW_ATTROPT_QUOTED ) != 0 ) {
		    dsgw_emits( "\"" );
		  }
		} else {
		  dsgw_html_href( urlprefix, adip->adi_vals[ i ], label,
				 adip->adi_vals[ i ],
				 get_arg_by_name( DSGW_ATTRARG_HREFEXTRA,
						 adip->adi_argc, adip->adi_argv ));
		}
		if (( adip->adi_opts & DSGW_ATTROPT_DNPICKER ) != 0 ) {
		    dsgw_emits( "</TD>\n<TD ALIGN=CENTER><INPUT TYPE=CHECKBOX " );
		    dsgw_emitf( "VALUE=\"%s\" NAME=delete_%s ",
			   adip->adi_vals[ i ], adip->adi_attr );
		    dsgw_emitf( "onClick=\"aChg('%s');\"</TD>\n</TR>\n",
			    adip->adi_attr );
		}
	    } else {
	      if ((adip->adi_opts & DSGW_ATTROPT_QUOTED ) != 0 ) {
		dsgw_emits( "\"" );
	      }
    
	      emit_value( label,
			(( adip->adi_opts & DSGW_ATTROPT_NO_ENTITIES ) == 0 ));
	      if ((adip->adi_opts & DSGW_ATTROPT_QUOTED ) != 0 ) {
		dsgw_emits( "\"" );
	      }
	    }
	}

	if ( !( adip->adi_opts & DSGW_ATTROPT_DNPICKER ) &&
		adip->adi_vals[ i + 1 ] != NULL ) {
	    dsgw_emits( "<BR>\n" );
	}

	if ( tmps != NULL ) {
	    free( tmps );
	}

	if ( rdns != NULL ) {
	    ldap_value_free( rdns );
	}
    }


    /* Output a javascript array of values for this attribute */
    if (( adip->adi_opts & DSGW_ATTROPT_DNPICKER ) != 0 ) {
	dsgw_emits( "<SCRIPT LANGUAGE=\"JavaScript\">\n" );
	dsgw_emits( "<!-- Hide from non-JavaScript-capable browsers\n" );
	dsgw_emitf( "var %s_values = new Object;\n", adip->adi_attr );
	for ( i = 0; adip->adi_vals != NULL && adip->adi_vals[ i ] != NULL; ++i ) {
	    char *edn;
	    edn = dsgw_strdup_escaped( adip->adi_vals[ i ]);
	    dsgw_emitf( "%s_values[%d] = \"%s\";\n", adip->adi_attr, i,
		    edn );
	    free( edn );
	}
	dsgw_emitf( "%s_values.count = %d;\n", adip->adi_attr, i );
	dsgw_emits( "// End hiding -->\n" );
	dsgw_emits( "</SCRIPT>\n" );
    }

    free( urlprefix );
}


static void
mail_display( struct dsgw_attrdispinfo *adip )
{
    int		i;

    for ( i = 0; adip->adi_vals[ i ] != NULL; ++i ) {
	if ( !did_output_as_special( adip->adi_argc, adip->adi_argv,
		    adip->adi_vals[ i ], adip->adi_vals[ i ] )) {
	    if (( adip->adi_opts & DSGW_ATTROPT_NOLINK ) == 0 ) {
		dsgw_html_href( "mailto:", adip->adi_vals[ i ], adip->adi_vals[ i ], NULL,
			get_arg_by_name( DSGW_ATTRARG_HREFEXTRA,
				adip->adi_argc, adip->adi_argv ));
	    } else {
	      if ((adip->adi_opts & DSGW_ATTROPT_QUOTED ) != 0 ) {
		dsgw_emits( "\"" );
	      }
	      
	      emit_value( adip->adi_vals[ i ],
			  (( adip->adi_opts & DSGW_ATTROPT_NO_ENTITIES ) == 0 ));
	      if ((adip->adi_opts & DSGW_ATTROPT_QUOTED ) != 0 ) {
		dsgw_emits( "\"" );
	      }

	    }
	}

	if ( adip->adi_vals[ i + 1 ] != NULL ) {
	    dsgw_emits( "<BR>\n" );
	}
    }

}


static void
url_display( struct dsgw_attrdispinfo *adip )
{
    int		i;
    char	*savep, *label;

    for ( i = 0; adip->adi_vals[ i ] != NULL; ++i ) {
	if (( label = strchr( adip->adi_vals[ i ], ' ' )) == NULL ) {
	    label = adip->adi_vals[ i ];
	    savep = NULL;
	} else {
	    savep = label;
	    *label++ = '\0';
	}

	if ( !did_output_as_special( adip->adi_argc, adip->adi_argv, label,
		adip->adi_vals[ i ] )) {
	    if (( adip->adi_opts & DSGW_ATTROPT_NOLINK ) == 0 ) {
		dsgw_html_href( NULL, adip->adi_vals[ i ], label, NULL,
			get_arg_by_name( DSGW_ATTRARG_HREFEXTRA,
				adip->adi_argc, adip->adi_argv ));
	    } else {
		if ((adip->adi_opts & DSGW_ATTROPT_QUOTED ) != 0 ) {
		    dsgw_emits( "\"" );
		}
		
		emit_value( adip->adi_vals[ i ],
			(( adip->adi_opts & DSGW_ATTROPT_NO_ENTITIES ) == 0 ));
		if ((adip->adi_opts & DSGW_ATTROPT_QUOTED ) != 0 ) {
		    dsgw_emits( "\"" );
		}

	    }
	}

	if ( savep != NULL ) {
	    *savep = ' ';
	}

	if ( adip->adi_vals[ i + 1 ] != NULL ) {
	    dsgw_emits( "<BR>\n" );
	}
    }

}


static void
bool_display( struct dsgw_attrdispinfo *adip )
{
    int		boolval, free_onclick, pre_idx;
    char	*usestr, *truestr, *falsestr, *checked;
    char	*nameprefix, *onclick;

    if ( adip->adi_vals == NULL || adip->adi_vals[ 0 ] == NULL ) {
      return;
    }

    checked = " CHECKED";

    if (( adip->adi_opts & DSGW_ATTROPT_EDITABLE ) == 0 ) {
	nameprefix = onclick = "";
	free_onclick = 0;
    } else {
	char *onclickfmt = " onClick=\"aChg('%s')\"";

	if (( adip->adi_opts & DSGW_ATTROPT_UNIQUE ) == 0 ) {
	    pre_idx = DSGW_MOD_PREFIX_NORMAL;
	} else {
	    pre_idx = DSGW_MOD_PREFIX_UNIQUE;
	}
	nameprefix = (( adip->adi_opts & DSGW_ATTROPT_ADDING ) == 0 ) ?
		replace_prefixes[ pre_idx ] : add_prefixes[ pre_idx ];
	onclick = dsgw_ch_malloc( strlen( onclickfmt ) +
		strlen( adip->adi_attr ) + 1 );
	sprintf( onclick, onclickfmt, adip->adi_attr );
	free_onclick = 1;
    }

    if (( truestr = get_arg_by_name( DSGW_ATTRARG_TRUESTR, adip->adi_argc,
	    adip->adi_argv )) == NULL ) {
	truestr = DSGW_ATTRARG_TRUESTR;
    }
    if (( falsestr = get_arg_by_name( DSGW_ATTRARG_FALSESTR, adip->adi_argc,
	    adip->adi_argv )) == NULL ) {
	falsestr = DSGW_ATTRARG_FALSESTR;
    }

    boolval = ( toupper( adip->adi_vals[ 0 ][ 0 ] ) == 'T' );

    if ( adip->adi_htmltype == DSGW_ATTRHTML_RADIO ) {
	dsgw_emitf( "<INPUT TYPE=\"radio\" NAME=\"%s%s\" "
		"VALUE=\"TRUE\"%s%s>%s<BR>\n", nameprefix, adip->adi_attr,
		boolval ? checked : "", onclick, truestr );
	dsgw_emitf( "<INPUT TYPE=\"radio\" NAME=\"%s%s\" "
		"VALUE=\"FALSE\"%s%s>%s<BR>\n", nameprefix, adip->adi_attr,
		boolval ? "" : checked, onclick, falsestr );
    } else if ( adip->adi_htmltype == DSGW_ATTRHTML_CHECKBOX ) {
	dsgw_emitf( "<INPUT TYPE=\"checkbox\" NAME=\"%s%s\" "
		"VALUE=\"TRUE\"%s%s\">%s\n", nameprefix, adip->adi_attr,
		boolval ? checked : "", onclick, truestr );
    } else {
	usestr =  boolval ? truestr : falsestr;
	if ( !did_output_as_special( adip->adi_argc, adip->adi_argv, usestr,
		adip->adi_vals[ 0 ] )) {
	    if ((adip->adi_opts & DSGW_ATTROPT_QUOTED ) != 0 ) {
		dsgw_emits( "\"" );
	    }
	    
	    dsgw_emits( boolval ? truestr : falsestr );
	    if ((adip->adi_opts & DSGW_ATTROPT_QUOTED ) != 0 ) {
		dsgw_emits( "\"" );
	    }
	}
    }
}


static void
bool_edit( struct dsgw_attrdispinfo *adip )
{
    if ( adip->adi_htmltype == DSGW_ATTRHTML_RADIO ||
	    adip->adi_htmltype == DSGW_ATTRHTML_CHECKBOX ) {
	bool_display( adip );
    } else {
	str_edit( adip );
    }
}


static void
time_display( struct dsgw_attrdispinfo *adip )
{
    int		i;

    for ( i = 0; adip->adi_vals[ i ] != NULL; ++i ) {
	if ( !did_output_as_special( adip->adi_argc, adip->adi_argv,
		adip->adi_vals[ i ], adip->adi_vals[ i ] )) {
	    if ((adip->adi_opts & DSGW_ATTROPT_QUOTED ) != 0 ) {
		dsgw_emits( "\"" );
	    }
	    dsgw_emits( time2text( adip->adi_vals[ i ],
		( adip->adi_opts & DSGW_ATTROPT_DATEONLY ) != 0 ) );
	    if ((adip->adi_opts & DSGW_ATTROPT_QUOTED ) != 0 ) {
		dsgw_emits( "\"" );
	    }
	}

	if ( adip->adi_vals[ i + 1 ] != NULL ) {
	    dsgw_emits( "<BR>\n" );
	}
    }

}


/*
 * handle special "within=", "href=", and "script=" options
 * return 0 if nothing was output or 1 if something was.
 */
static int
did_output_as_special( int argc, char **argv, char *label, char *val )
{
    char	*href = NULL;
    char	*within = NULL;
    char	*script = NULL;
    char	*newval = NULL;

    if (( href = get_arg_by_name( DSGW_ATTRARG_HREF, argc, argv )) == NULL &&
	    ( within = get_arg_by_name( DSGW_ATTRARG_WITHIN, argc,
	    argv )) == NULL &&
	    ( script = get_arg_by_name( DSGW_ATTRARG_SCRIPT, argc,
	    argv )) == NULL ) {
	return( 0 );
    }

    if ( within != NULL ) {
	dsgw_substitute_and_output( within, "--value--", val, 1 );
    } else if (href != NULL) {
	dsgw_html_href( NULL, href, label, val,
		get_arg_by_name( DSGW_ATTRARG_HREFEXTRA, argc, argv ));
    } else if (script != NULL) {
        newval = dsgw_strdup_escaped ( val );
        if (newval != NULL && *newval != '\0') {
		fputs( newval, stdout );
                free( newval );
	}
    }

    return( 1 );
}


/*
 * The GET2BYTENUM() macro, time2text(), and gtime() functions are taken
 * with slight changes (to handle 4-digit years) from libldap/tmplout.c
 */
#define GET2BYTENUM( p )	(( *p - '0' ) * 10 + ( *(p+1) - '0' ))
#define BSIZ 1024

static char *
time2text( char *ldtimestr, int dateonly )
{
    int			len;
    struct tm		t;
    char		*p, zone;
    time_t		gmttime;
    char                *timestr = NULL;

    memset( (char *)&t, 0, sizeof( struct tm ));
    if (( len = strlen( ldtimestr )) < 13 ) {
	return( ldtimestr );
    }
    if ( len > 15 ) {	/* throw away excess from 4-digit year time string */
	len = 15;
    } else if ( len == 14 ) {
	len = 13;	/* assume we have a time w/2-digit year (len=13) */
    }

    for ( p = ldtimestr; p - ldtimestr + 1 < len; ++p ) {
	if ( !ldap_utf8isdigit( p )) {
	    return( ldtimestr );
	}
    }

    p = ldtimestr;
    t.tm_year = GET2BYTENUM( p ); p += 2;
    if ( len == 15 ) {
	t.tm_year = 100 * (t.tm_year - 19);
	t.tm_year += GET2BYTENUM( p ); p += 2;
    }
    else {
        /* 2 digit years...assumed to be in the range (19)70 through
           (20)69 ...less than 70 (for now, 38) means 20xx */
        if(t.tm_year < 70) {
            t.tm_year += 100;
        }
    }
 
    t.tm_mon = GET2BYTENUM( p ) - 1; p += 2;
    t.tm_mday = GET2BYTENUM( p ); p += 2;
    t.tm_hour = GET2BYTENUM( p ); p += 2;
    t.tm_min = GET2BYTENUM( p ); p += 2;
    t.tm_sec = GET2BYTENUM( p ); p += 2;

    if (( zone = *p ) == 'Z' ) {	/* GMT */
	zone = '\0';	/* no need to indicate on screen, so we make it null */
    }

    gmttime = gtime( &t );

    /* Try to get the localized string */
    timestr = dsgw_time(gmttime);

    /* Localized time string getter failed, try ctime()*/
    if (timestr == NULL){
      timestr = ctime( &gmttime );

      /* replace trailing newline */
      timestr[ strlen( timestr ) - 1 ] = zone;	
      if ( dateonly ) {
	strcpy( timestr + 11, timestr + 20 );
      }
    }

    return(timestr);
}





/* gtime.c - inverse gmtime */

#if !defined( MACOS ) && !defined( _WINDOWS ) && !defined( DOS )
#include <sys/time.h>
#endif /* !MACOS */

/* gtime(): the inverse of localtime().
	This routine was supplied by Mike Accetta at CMU many years ago.
 */

static int	dmsize[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

#define	dysize(y)	\
	(((y) % 4) ? 365 : (((y) % 100) ? 366 : (((y) % 400) ? 365 : 366)))

/*
#define	YEAR(y)		((y) >= 100 ? (y) : (y) + 1900)
*/
#define YEAR(y)         (((y) < 1900) ? ((y) + 1900) : (y))


/*  */

static long	gtime ( struct tm *tm )
{
    register int    i,
                    sec,
                    mins,
                    hour,
                    mday,
                    mon,
                    year;
    register long   result;

    if ((sec = tm -> tm_sec) < 0 || sec > 59
	    || (mins = tm -> tm_min) < 0 || mins > 59
	    || (hour = tm -> tm_hour) < 0 || hour > 24
	    || (mday = tm -> tm_mday) < 1 || mday > 31
	    || (mon = tm -> tm_mon + 1) < 1 || mon > 12)
	return ((long) -1);
    if (hour == 24) {
	hour = 0;
	mday++;
    }
    year = YEAR (tm -> tm_year);

    result = 0L;
    for (i = 1970; i < year; i++)
	result += dysize (i);
    if (dysize (year) == 366 && mon >= 3)
	result++;
    while (--mon)
	result += dmsize[mon - 1];
    result += mday - 1;
    result = 24 * result + hour;
    result = 60 * result + mins;
    result = 60 * result + sec;

    return result;
}


static int
looks_like_dn( char *s )
{
    return( strchr( s, '=' ) != NULL );
}


static void
do_searchdesc( dsgwtmplinfo *tip, int argc, char** argv)
{
    auto unsigned fmt = 0;
    auto unsigned opt = 0;
    {
	auto int i;
	for (i = 0; i < argc; ++i) {
	    if (!strcasecmp (argv[i], "VERBOSE")) {
		opt |= 1;
	    }
	}
    }
    switch ( tip->dsti_entrycount ) {
      case 0:
	fmt = opt & 1
	    ? ((tip->dsti_options & DSGW_DISPLAY_OPT_CUSTOM_SEARCHDESC)
	       ? DBT_SearchFound0Entries_
	       : DBT_SearchFound0EntriesWhere_)
	    : ((tip->dsti_options & DSGW_DISPLAY_OPT_CUSTOM_SEARCHDESC)
	       ? DBT_Found0Entries_
	       : DBT_Found0EntriesWhere_);
      case 1:
	fmt = opt & 1
	    ? ((tip->dsti_options & DSGW_DISPLAY_OPT_CUSTOM_SEARCHDESC)
	       ? DBT_SearchFound1Entry_
	       : DBT_SearchFound1EntryWhere_)
	    : ((tip->dsti_options & DSGW_DISPLAY_OPT_CUSTOM_SEARCHDESC)
	       ? DBT_Found1Entry_
	       : DBT_Found1EntryWhere_);
      default:
	fmt = opt & 1
	    ? ((tip->dsti_options & DSGW_DISPLAY_OPT_CUSTOM_SEARCHDESC)
	       ? DBT_SearchFoundEntries_
	       : DBT_SearchFoundEntriesWhere_)
	    : ((tip->dsti_options & DSGW_DISPLAY_OPT_CUSTOM_SEARCHDESC)
	       ? DBT_FoundEntries_
	       : DBT_FoundEntriesWhere_);
    }
    {
	auto char* format = XP_GetClientStr (fmt);
	if (format == NULL || *format == '\0') {
	    format = "Found %1$li entries where the %2$s %3$s '%4$s'.\n";
	}
	dsgw_emitf (format, (long)tip->dsti_entrycount, /* %1$li */
		    tip->dsti_search2s ? tip->dsti_search2s : "", /* %2$s */
		    tip->dsti_search3s ? tip->dsti_search3s : "", /* %3$s */
		    tip->dsti_search4s ? tip->dsti_search4s : "");/* %4$s */
    }
    if ( tip->dsti_searcherror != NULL && *tip->dsti_searcherror != '\0' ) {
	dsgw_emitf( "<BR>%s\n", tip->dsti_searcherror );
    }
    if ( tip->dsti_searchlderrtxt != NULL &&
	    *tip->dsti_searchlderrtxt != '\0' ) {
	dsgw_emitf( "<BR>(%s)\n", tip->dsti_searchlderrtxt );
    }
}


static void
do_editbutton( char *dn, char *encodeddn, int argc, char **argv )
{
    char        *buttonlabel, **rdns;

    if (( buttonlabel = get_arg_by_name( DSGW_ARG_BUTTON_LABEL, argc,
	    argv )) == NULL ) {
	buttonlabel = XP_GetClientStr(DBT_edit_);
    }

    if (( rdns = ldap_explode_dn( dn, 1 )) != NULL ) {
	dsgw_emitf(
		"<INPUT TYPE=\"hidden\" NAME=\"authhint\" VALUE=\"%s\">\n",
		rdns[ 0 ] );
	ldap_value_free( rdns );
    }

    dsgw_emitf( "<INPUT TYPE=\"hidden\" NAME=\"authdesturl\">\n"
	    "<INPUT TYPE=\"button\" VALUE=\"%s\" "
	    "onClick=\"authOrEdit('%s')\">\n", buttonlabel, encodeddn );
}


static void
do_savebutton( unsigned long dispopts, int argc, char **argv )
{
    char	*buttonlabel, *checksubmit;

    if (( buttonlabel = get_arg_by_name( DSGW_ARG_BUTTON_LABEL, argc,
	    argv )) == NULL ) {
	buttonlabel = XP_GetClientStr(DBT_saveChanges_);
    }

    dsgw_emitf( "<INPUT TYPE=\"button\" VALUE=\"%s\" onClick=\"",
	    buttonlabel );
    if (( checksubmit = get_arg_by_name( DSGW_ARG_BUTTON_CHECKSUBMIT, argc,
	    argv )) != NULL ) {
	dsgw_emitf( "if (%s) ", checksubmit );
    }
    dsgw_emitf( "submitModify('%s')\">\n",
	    ( dispopts & DSGW_DISPLAY_OPT_ADDING ) == 0
	      ? "modify" : "add" );
}


static void
do_deletebutton( int argc, char **argv )
{
    char	*buttonlabel, *prompt;

    if (( buttonlabel = get_arg_by_name( DSGW_ARG_BUTTON_LABEL, argc,
	    argv )) == NULL ) {
	buttonlabel = XP_GetClientStr(DBT_delete_);
    }

    if (( prompt = get_arg_by_name( DSGW_ARG_BUTTON_PROMPT, argc,
	    argv )) == NULL ) {
	prompt = XP_GetClientStr(DBT_deleteThisEntry_);
    }

    dsgw_emitf("<INPUT TYPE=BUTTON VALUE=\"%s\"", buttonlabel);
    dsgw_emits(" onClick=\"confirmModify('delete', ");
    dsgw_quote_emits(QUOTATION_JAVASCRIPT, prompt);
    dsgw_emits(")\">\n");
}


#if 0
static void
do_renamebutton( char *dn, int argc, char **argv )
{
    char	*buttonlabel, *prompt, *oldname, **rdns, *tag;
    int		len;

    if (( buttonlabel = get_arg_by_name( DSGW_ARG_BUTTON_LABEL, argc,
	    argv )) == NULL ) {
	buttonlabel = XP_GetClientStr(DBT_rename_);
    }

    if (( prompt = get_arg_by_name( DSGW_ARG_BUTTON_PROMPT, argc,
	    argv )) == NULL ) {
	prompt = XP_GetClientStr(DBT_enterANewNameForThisEntry_);
    }

    if (( rdns = ldap_explode_dn( dn, 0 )) != NULL &&
	    ( oldname = strchr( rdns[ 0 ], '=' )) != NULL ) {
	*oldname++ = '\0';
	tag = rdns[ 0 ];
	if ( *oldname == '"' ) {
	    ++oldname;
	    if (( len = strlen( oldname )) > 0
		    && oldname[ len - 1 ] == '"' ) {
		oldname[ len - 1 ] = '\0';
	    }
	}
    } else {
	oldname = dn;
	tag = "";
    }

    dsgw_emitf( "<INPUT TYPE=\"button\" VALUE=\"%s\""
	    " onClick=\"renameEntry('%s','%s',", buttonlabel, tag, prompt );
    dsgw_quote_emits( QUOTATION_JAVASCRIPT, oldname );
    dsgw_emits( ")\">\n" );

    if ( rdns != NULL ) {
	ldap_value_free( rdns );
    }
}
#endif


static void
do_editasbutton( int argc, char **argv )
{
    char	*template, *buttonlabel;

    if (( template = get_arg_by_name( DSGW_ARG_BUTTON_TEMPLATE, argc,
	    argv )) == NULL ) {
	template = "";
    }

    if (( buttonlabel = get_arg_by_name( DSGW_ARG_BUTTON_LABEL, argc,
	    argv )) == NULL ) {
	buttonlabel = XP_GetClientStr(DBT_editAs_);
    }

    dsgw_emitf( "<INPUT TYPE=\"button\" VALUE=\"%s\""
	    " onClick=\"EditEntryAs('%s')\">\n", buttonlabel, template );
}


static void
do_passwordfield( unsigned long dispopts, int argc, char **argv,
	char *fieldname )
{
    output_text_elements( argc, argv, fieldname, NULL, NULL, "",
	    DSGW_ATTRHTML_PASSWORD, dispopts );
}


static void
do_helpbutton( unsigned long dispopts, int argc, char **argv )
{
    char        *topic;

    if (( topic = get_arg_by_name( DSGW_ARG_BUTTON_TOPIC, argc,
	    argv )) == NULL ) {
	topic = "";
    }

    dsgw_emit_helpbutton( topic );
}


static void
do_closebutton( unsigned long dispopts, int argc, char **argv )
{
    dsgw_emit_button( argc, argv, "onClick=\"%s\"",
		     ( dispopts & DSGW_DISPLAY_OPT_EDITABLE ) == 0
		     ? "top.close()" : "closeIfOK()" );
}


static void
do_dneditbutton( unsigned long dispopts, int argc, char **argv )
{
    char *label, *template, *attr, *desc;

    if (( label = get_arg_by_name( DSGW_ARG_DNEDIT_LABEL, argc,
	    argv )) == NULL ) {
	label = XP_GetClientStr(DBT_edit_1);
    }
    if (( template = get_arg_by_name( DSGW_ARG_DNEDIT_TEMPLATE, argc,
	    argv )) == NULL ) {
	template = "dnedit";
    }
    if (( attr = get_arg_by_name( DSGW_ARG_DNEDIT_ATTR, argc,
	    argv )) == NULL ) {
	dsgw_emits( "<!-- Error: missing attr= argument in DS_DNEDITBUTTON "
		"directive -->\n" );
	return;
    }
    if (( desc = get_arg_by_name( DSGW_ARG_DNEDIT_DESC, argc,
	    argv )) == NULL ) {
	desc = attr;
    }

    dsgw_emitf( "<INPUT TYPE=\"button\" VALUE=\"%s\""
	     " onClick=\"DNEdit('%s', '%s', '%s')\">\n", label, template,
	     attr, desc );
}


static void
do_viewswitcher( char *template, char *dn, int argc, char **argv )
{
    dsgwtmplset	*tsp;
    dsgwview	*vp;
    char	*s, *altprefix, *altsuffix, *curprefix, *cursuffix;

    /* first we see if this template is part of a template set */
    for ( tsp = gc->gc_tmplsets; tsp != NULL; tsp = tsp->dstset_next ) {
	for ( vp = tsp->dstset_viewlist; vp != NULL; vp = vp->dsview_next ) {
	    if ( strcasecmp( vp->dsview_template, template ) == 0 ) {
		break;
	    }
	}
	if ( vp != NULL ) {
	    break;
	}
    }

    if ( tsp == NULL || tsp->dstset_viewcount == 1 ) {
	return;	/* not part of a set at all or only one view in the set */
    }

    /* emit view switcher prefix */
    if (( s = get_arg_by_name( "prefix", argc, argv )) == NULL ) {
	s = "<TABLE CELLPADDING=6 BORDER=0><TR VALIGN=center>\n";
    }
    dsgw_emits( s );

    /* retrieve view item prefix and suffix arguments */
    if (( altprefix = get_arg_by_name( "altprefix", argc, argv )) == NULL ) {
	altprefix = "<TD BGCOLOR=#B0B0B0>\n";
    }
    if (( altsuffix = get_arg_by_name( "altsuffix", argc, argv )) == NULL ) {
	altsuffix = "</TD>\n";
    }
    if (( curprefix = get_arg_by_name( "curprefix", argc, argv )) ==
	    NULL ) {
	curprefix = "<TD BGCOLOR=#808080><FONT COLOR=#000000><B>\n";
    }
    if (( cursuffix = get_arg_by_name( "currentsuffix", argc, argv )) ==
	    NULL ) {
	cursuffix = "</B></FONT></TD>\n";
    }

    /* emit one table cell item (or similar) for each available view */
    for ( vp = tsp->dstset_viewlist; vp != NULL; vp = vp->dsview_next ) {
	if ( strcasecmp( vp->dsview_template, template ) == 0 ) {
	    dsgw_emitf( "%s%s%s", curprefix, vp->dsview_caption,
		    cursuffix );
	} else {
	    dsgw_emitf( "%s\n<A HREF=\"", altprefix );
	    if ( vp->dsview_jscript == NULL ) {
		dsgw_emitf( "javascript:EditEntryAs('%s')",
			vp->dsview_template );
	    } else {
		dsgw_substitute_and_output( vp->dsview_jscript, "--dn--",
			dn, 1 );
	    }
	    dsgw_emitf( "\">%s</A>\n%s", vp->dsview_caption, altsuffix );
	}
    }

    /* emit view switcher suffix */
    if (( s = get_arg_by_name( "suffix", argc, argv )) == NULL ) {
	s = "</TR></TABLE>\n";
    }
    dsgw_emits( s );
}


static void
do_attrvalset( dsgwtmplinfo *tip, char *dn, unsigned long dispopts,
	int argc, char **argv )
{
    dsgwavset	*avp;
    char	*s, *valuearg, *prefix, *suffix;
    int		i, setpos, len, maxvallen;

    /*
     * locate "set" element in argv array so we can replace it later
     * with "value="
     */
    if (( setpos = dsgw_get_arg_pos_by_name( DSGW_ARG_AVSET_SET, argc,
	    argv )) < 0 ) {
        dsgw_emitf( XP_GetClientStr(DBT_missingSN_), DSGW_ARG_AVSET_SET );
        return;
    }
    s = &argv[ setpos ][ 4 ];

    for ( avp = gc->gc_avsets; avp != NULL; avp = avp->dsavset_next ) {
	if ( strcasecmp( s, avp->dsavset_handle ) == 0 ) {
	    break;
	}
    }
    if ( avp == NULL ) {
	dsgw_emitf( XP_GetClientStr(DBT_unknownSetSN_), s );
	return;
    }

    prefix = get_arg_by_name( "prefix", argc, argv );
    suffix = get_arg_by_name( "suffix", argc, argv );

    /* repeatedly call on do_attribute() to perform all the difficult work */
    maxvallen = 0;
    valuearg = NULL;
    for ( i = 0; i < avp->dsavset_itemcount; ++i ) {
	if ( prefix != NULL ) {
	    dsgw_emits( prefix );
	}
	dsgw_emits( avp->dsavset_prefixes[ i ] );

	/* construct "value=XXX" arg. and place in argv array */
	if (( len = strlen( avp->dsavset_values[ i ] )) > maxvallen ||
		valuearg == NULL ) {
	    maxvallen = len;
	    valuearg = dsgw_ch_realloc( valuearg, maxvallen + 7 );
	}
	PR_snprintf( valuearg, maxvallen + 7, "value=%s", avp->dsavset_values[ i ] );
	argv[ setpos ] = valuearg;

	do_attribute( tip, dn, dispopts, argc, argv );

	dsgw_emits( avp->dsavset_suffixes[ i ] );
	if ( suffix != NULL ) {
	    dsgw_emitf( "%s\n", suffix );
	}
    }
}


static void
do_std_completion_js( char *template, int argc, char **argv )
{
    if ( template != NULL ) {
	dsgw_emitf(
		"<INPUT TYPE=\"hidden\" NAME=\"completion_javascript\" VALUE=\""
		"if (dsmodify_dn.length == 0) "
		    "document.writeln( \\'<FONT SIZE=+1>\\' + dsmodify_info +"
		    " \\'</FONT>\\' );"
		" else "
		    "parent.document.location.href=\\'%s?%s"
		    "&context=%s&dn=\\' + dsmodify_dn + \\'&info=\\' + escape(dsmodify_info)\">\n",
		dsgw_getvp( DSGW_CGINUM_EDIT ), template, context );
    }
}


/*
 * function called back by dsgw_parse_line() to evaluate IF directives.
 * return non-zero for true, zero for false.
 */
static int
condition_is_true( int argc, char **argv, void *arg )
{
    dsgwtmplinfo	*tip;

    if ( argc < 1 ) {
	return( 0 );
    }

    tip = (dsgwtmplinfo *)arg; 

    if ( strcasecmp( argv[0], DSGW_COND_FOUNDENTRIES ) == 0 ) {
	return( tip->dsti_entrycount > 0 );
    }

    if ( strcasecmp( argv[0], DSGW_COND_ADDING ) == 0 ) {
	return(( tip->dsti_options & DSGW_DISPLAY_OPT_ADDING ) != 0 );
    }
 
    if ( strcasecmp( argv[0], DSGW_COND_EDITING ) == 0 ) {
	return(( tip->dsti_options & DSGW_DISPLAY_OPT_EDITABLE ) != 0 &&
		( tip->dsti_options & DSGW_DISPLAY_OPT_ADDING ) == 0 );
    }
 
    if ( strcasecmp( argv[0], DSGW_COND_DISPLAYING ) == 0 ) {
	return(( tip->dsti_options & DSGW_DISPLAY_OPT_EDITABLE ) == 0 );
    }

    if ( strcasecmp( argv[0], DSGW_COND_BOUND ) == 0 ) {
	return( dsgw_get_binddn() != NULL );
    }

    if ( strcasecmp( argv[0], DSGW_COND_BOUNDASTHISENTRY ) == 0 ) {
	return( dsgw_bound_as_dn( tip->dsti_entrydn, 0 ));
    }

    if ( strcasecmp( argv[0], DSGW_COND_DISPLAYORGCHART ) == 0 ) {
      return(gc->gc_orgcharturl != NULL && ((tip->dsti_options & DSGW_DISPLAY_OPT_ADDING ) == 0));
    }

    if ( strcasecmp( argv[0], DSGW_COND_DISPLAYAIMPRESENCE ) == 0 ) {
      return((gc->gc_aimpresence == 1) && ((tip->dsti_options & DSGW_DISPLAY_OPT_ADDING ) == 0));
    }

    if ( strcasecmp( argv[0], DSGW_COND_ATTRHASVALUES ) == 0 ) {
	/*
	 * format of IF statment is:
	 *    <-- IF "AttributeHasValues" "ATTRIBUTE" "MINIMUM_COUNT" -->
	 * MINIMUM_COUNT is an optional number.
	 */
	char	**vals;
	int	rc, minimum;

	if ( argc < 2 || tip->dsti_entry == NULL ||
		( vals = (char **) ldap_get_values( tip->dsti_ld, tip->dsti_entry,
		argv[1])) == NULL ) {
	    /* check "attrsonly" information if applicable */
	    if ( argc < 3 && tip->dsti_attrsonly_entry != NULL ) {
		(void)ldap_get_values( tip->dsti_ld, tip->dsti_attrsonly_entry,	argv[1]);
		if ( ldap_get_lderrno( tip->dsti_ld, NULL, NULL )
			== LDAP_SUCCESS ) {
		    return( 1 );
		}
	    }
	    return( 0 );
	}
	minimum = ( argc < 3 ) ? 1 : atoi( argv[ 2 ] );
	rc = ( minimum <= 1 || ldap_count_values( vals ) >= minimum );
	ldap_value_free( vals );
	return( rc );
    }

    if ( strcasecmp( argv[0], DSGW_COND_ATTRHASTHISVALUE ) == 0 ) {
	/*
	 * format of IF statment is:
	 *    <-- IF "AttributeHasThisValue" "ATTRIBUTE" "SYNTAX" "VALUE" -->
	 */
	char			**vals;
	int			i, rc;
	struct attr_handler	*ahp;

	if ( argc < 4 ||  tip->dsti_entry == NULL ||
	     ( vals = (char **) ldap_get_values( tip->dsti_ld, tip->dsti_entry,
				       argv[1])) == NULL ) {
	    return( 0 );
	}
	if (( ahp = syntax2attrhandler( argv[2] )) == NULL ) {
	    dsgw_emitf( XP_GetClientStr(DBT_unknownSyntaxSN_1), argv[2] );
	    return( 0 );
	}

	rc = 0;
	for ( i = 0; vals[ i ] != NULL; ++i ) {
	    if ( dsgw_valcmp(ahp->ath_compare)( (const char **)&vals[i],
		    (const char **)&argv[3] ) == 0 ) {
		rc = 1;
		break;
	    }
	}
	ldap_value_free( vals );
	return( rc );
    }

    /* pass unrecognized conditionals to simple conditional handler */
    return( dsgw_simple_cond_is_true( argc, argv, NULL ));
}

/*
 * Function: dsgw_get_values
 *
 * Returns: an array of values
 *
 * Description: This function returns the values of 
 *              an attribute, taking into account any
 *              possible language or phonetic tags.
 *              pass in something like "cn" and this function
 *              will return all cn's, tagged or not.
 *              If binary_value is 1, then it'll handle
 *              everything as binary values.
 *
 * Author: RJP
 *
 */
static char ** 
dsgw_get_values( LDAP *ld, LDAPMessage *entry, 
		 const char *target, int binary_value ) 
{
    BerElement  *ber        = NULL;
    char        *attr       = NULL;
    char        *new_target = NULL;
    int          new_target_size = 0;
    char       **val_youse  = NULL;
    char       **temp_vals  = NULL;
    int          i          = 0;
    int          j          = 0;
    int          temp_val_count = 0;

    /* Allocate a new target that is the original plus a semicolon*/ 
    new_target = (char *) dsgw_ch_malloc (sizeof(char) * (strlen(target) + 2) );
    sprintf (new_target, "%s;", target);
    
    new_target_size = strlen(new_target);

    /* 
     * Go through the attributes and 
     * compare the new_target with the attr name
     */
    for ( attr = ldap_first_attribute( ld, entry, &ber ); attr != NULL; 
	  attr = ldap_next_attribute( ld, entry, ber ) ) {
	
	/* If the "target;" matches the attribute name, get the values*/
	if ( strcasecmp(attr, target) == 0 ||
	     strncasecmp (attr, new_target, new_target_size) == 0) {
	    if (binary_value) {
		temp_vals = (char **) ldap_get_values_len( ld, entry, attr );
	    } else {
		temp_vals = (char **) ldap_get_values( ld, entry, attr );
	    }
	    
	    if (temp_vals == NULL) {
		continue;
	    }
	    
	    /* Find the next open spot in val_youse*/
	    if (val_youse) {
		for (; val_youse[i] != NULL; i++) ;
	    }

	    /* Count the number of values in temp_vals */
	    for (temp_val_count = 0; temp_vals[temp_val_count] != NULL;
		 temp_val_count++);
	    
	    /* Realloc */
	    val_youse = (char **) dsgw_ch_realloc (val_youse, sizeof(char *) * (temp_val_count + i + 1) );
	    
	    /* Start there and copy over the pointers from temp_vals */
	    for (j = 0; j < temp_val_count; j++, i++) {
		val_youse[i] = temp_vals[j];
	    }
	    
	    val_youse[i] = NULL;
	    
	    ldap_memfree(temp_vals);
	    
	}
    }
    
    /* Free the BerElement from memory when done */
    
    if ( ber != NULL ) {
	
	ldap_ber_free( ber, 0 );
	
    }
    
    free (new_target);
    
    return(val_youse);
}

/*
 * Function: dsgw_value_free
 *
 * Returns: nothing
 *
 * Description: frees a half libldap and half dsge malloc'd array.
 *              Sorry. This really sucks, I know, but I didn't
 *              want to copy all that data around.
 *
 * Author: RJP
 *
 */
static void
dsgw_value_free( void **ldvals, int binary ) 
{
    int i;

    for (i = 0; ldvals[i] != NULL; i ++) {
	if (binary) {
	    struct berval *delete_me = NULL;

	    delete_me = (struct berval *) ldvals[i];
	    
	    ldap_memfree(delete_me->bv_val);
	    ldap_memfree(delete_me);
	} else {
	    ldap_memfree (ldvals[i]);
	}
    }
    
    free(ldvals);
	
    
}
/*
 * Function: dsgw_time
 *
 * Returns: a string not unlike the string returned from ctime()
 *          except it's localized
 *
 * Description: this function takes the number of seconds since 1970
 *              and converts it to a localized string version of that.
 *              First it tries to use the clientLanguage, if that fails,
 *              It tries the default language. if that fails, it returns
 *              NULL
 *
 * Author: RJP
 *
 */
static char *
dsgw_time(time_t secs_since_1970) 
{
  UDateFormat *edatefmt;
  UErrorCode err = U_ZERO_ERROR;
  UChar *dstr0;
  static char obuf[BSIZ];
  UDate tmp_dat;
  char *locale = NULL;
  int32_t myStrlen = 0;

  /* Create a Date/Time Format using the locale */
  if (countri) {
	  locale = PR_smprintf("%s_%s", langwich, countri);
  } else {
	  locale = PR_smprintf("%s", langwich);
  }

  edatefmt = udat_open(
	  UDAT_DEFAULT, /* default date style for locale */
	  UDAT_DEFAULT, /* default time style for locale */
	  locale,
	  NULL, 0, /* use default timezone */
	  NULL, 0, /* no pattern */
	  &err);

  PR_smprintf_free(locale);
  locale = NULL;

  if (!edatefmt || (err != U_ZERO_ERROR)) {
	  if (edatefmt) {
		  udat_close(edatefmt);
	  }
	  err = U_ZERO_ERROR;
	  edatefmt = udat_open(
		  UDAT_DEFAULT, /* default date style for locale */
		  UDAT_DEFAULT, /* default time style for locale */
		  gc->gc_DefaultLanguage, /* default language */
		  NULL, 0, /* use default timezone */
		  NULL, 0, /* no pattern */
		  &err);
  }

  if (!edatefmt || (err != U_ZERO_ERROR)) {
    dsgw_error( DSGW_ERR_LDAPGENERAL, NULL, DSGW_ERROPT_EXIT, err, NULL );
    /*fprintf(stderr, "ERROR: NLS_NewDateTimeFormat(0): %d\n", err);*/
  }

  /* Get Current Date/Time */
  tmp_dat = (UDate) secs_since_1970;
  tmp_dat *= 1000.00;

  /* Format using the first Date/Time format */
  myStrlen = udat_format(edatefmt, tmp_dat, NULL, myStrlen, NULL, &err);
  if(err == U_BUFFER_OVERFLOW_ERROR){
    err = U_ZERO_ERROR;
    dstr0 = (UChar*)dsgw_ch_malloc(sizeof(UChar) * (myStrlen+1) );
    myStrlen = udat_format(edatefmt, tmp_dat, dstr0, myStrlen+1, NULL, &err);
  }

  if (err != U_ZERO_ERROR) {
    dsgw_error( DSGW_ERR_LDAPGENERAL, NULL, DSGW_ERROPT_EXIT, err, NULL );
    /*fprintf(stderr, "ERROR: NLS_FormatDate(1): %d\n", err);*/
  }

  /* convert to utf8 */
  u_strToUTF8(obuf, sizeof(obuf), NULL, dstr0, myStrlen, &err);

  if (err != U_ZERO_ERROR) {
    dsgw_error( DSGW_ERR_LDAPGENERAL, NULL, DSGW_ERROPT_EXIT, err, NULL );
    /*fprintf(stderr, "ERROR: NLS_NewEncodingConverter(0): %d\n", err);*/
  }
  /*fprintf(stdout, "Date(0): %s\n", obuf);*/

  /* Clean up -- but may not be enough... :) */
  free(dstr0);
  
  udat_close(edatefmt);
  edatefmt = NULL;
  
  return( (char *) obuf);
}

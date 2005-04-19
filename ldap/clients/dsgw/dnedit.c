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
 * Generate a DN edit screen.
 */

#include "dsgw.h"
#include "dbtdsgw.h"

#ifdef DSGW_DEBUG
int main(int argc, char *argv[], char *env[] )
#else /* DSGW_DEBUG */
int main(int argc, char *argv[] )
#endif /* DSGW_DEBUG */
{
    char	*tmplname, *attrname, *attrdesc, *qs, *dn, *edn;
    char	*attrs[ 2 ], **attrvals, **xdn, *avedn, *js0, *js1;
    LDAP	*ld;
    LDAPMessage	*msgp;
    int		i;

    /*
     * The URL used to invoke this CGI looks like:
     *  http://host/dnedit?CONTEXT=context&TEMPLATE=tmplname&DN=dn&ATTR=attrname&DESC=description
     *
     * where:
     *   "tmplname" is the name of the HTML template to render
     *   "attrname" is the name of a dn-valued attribute to display
     *   "description" is a textual description of the attribute
     *
     * Note: original form http://host/dnedit/dn?... is supported
     *       for keeping backward compatibility.
     */
    tmplname = attrname = attrdesc = dn = edn = NULL;
    if (( qs = getenv( "QUERY_STRING" )) != NULL && *qs != '\0' ) {
	char *p, *q;
	q = qs + strlen( qs );
	while ((( p = strrchr( qs, '&' )) != NULL ) || ( q - qs > 1 )) {
	    if ( p )
	        *p++ = '\0';
	    else
		p = qs;
	    q = p;

	    if ( p != NULL && strncasecmp( p, "dn=", 3 ) == 0 ) {
		edn = dsgw_ch_strdup( p + 3 );
		dn = dsgw_ch_strdup( p + 3 );
		dsgw_form_unescape( dn );
	    } else if ( p != NULL && strncasecmp( p, "template=", 9 ) == 0 ) {
		tmplname = dsgw_ch_strdup( p + 9 );
		dsgw_form_unescape( tmplname );
	    } else if ( p != NULL && strncasecmp( p, "attr=", 5 ) == 0 ) {
		attrname = dsgw_ch_strdup( p + 5 );
		dsgw_form_unescape( attrname );
	    } else if ( p != NULL && strncasecmp( p, "desc=", 5 ) == 0 ) {
		attrdesc = dsgw_ch_strdup( p + 5 );
		/* Don't bother unescaping it;
		   we're only going to put it back in another URL. */
	    } else if ( p != NULL && strncasecmp( p, "context=", 8 ) == 0) {
		context = dsgw_ch_strdup( p + 8 );
		dsgw_form_unescape( context );
	    }
	    
	}

	if ( !tmplname )
	    dsgw_error( DSGW_ERR_MISSINGINPUT, "template", DSGW_ERROPT_EXIT,
		    0, NULL );
	if ( !attrname )
	    dsgw_error( DSGW_ERR_MISSINGINPUT, "attr", DSGW_ERROPT_EXIT,
		    0, NULL );
	if ( !attrdesc )
	    dsgw_error( DSGW_ERR_MISSINGINPUT, "desc", DSGW_ERROPT_EXIT,
		    0, NULL );
    } else {
	dsgw_error( DSGW_ERR_MISSINGINPUT, NULL, DSGW_ERROPT_EXIT, 0, NULL );
    }

    if ( dn == NULL ) {
	dsgw_error( DSGW_ERR_MISSINGINPUT, "dn", DSGW_ERROPT_EXIT, 0, NULL );
    }

    (void)dsgw_init( argc, argv, DSGW_METHOD_GET );

#ifdef DSGW_DEBUG
    dsgw_logstringarray( "env", env );
#endif

    dsgw_send_header();


    /* Get the current attribute values */
    (void) dsgw_init_ldap( &ld, NULL, 0, 0);
    attrs[ 0 ] = attrname;
    attrs[ 1 ] = NULL;
    if (ldap_search_s( ld, dn, LDAP_SCOPE_BASE, "(objectclass=*)", attrs, 0,
	    &msgp ) != LDAP_SUCCESS ) {
	dsgw_error( DSGW_ERR_ENTRY_NOT_FOUND, dn, DSGW_ERROPT_EXIT, 0, NULL );
    }
    attrvals = ldap_get_values( ld, msgp, attrname );
	

    /* Send the top-level document HTML */
    dsgw_emits( "<HTML>\n"
		"<SCRIPT LANGUAGE=\"JavaScript\">\n" );
    dsgw_emitf( "var emptyFrame = '';\n" );
    dsgw_emitf( "var attrname = '%s';\n", attrname );
    /*
     * fix for 333110: dn should be escaped to be used in saveChanges/domodify
     */
    dsgw_emitf( "var dn = '%s';\n", edn );
    dsgw_emitf( "var needToSaveChanges = false;\n" );
    dsgw_emitf( "var completion_url = '%s?dn=%s&context=%s';\n", 
	    dsgw_getvp( DSGW_CGINUM_EDIT ), edn, context);
    dsgw_emitf(
    /*
     * This needs to output \\\' so that when JavaScript writeln's
     * this string, it writes \' to the output document.
     *
     * I'm really, really sorry about this - ggood.
     * 
     * Moral of the story - next time someone asks you to write C code which
     * writes JavaScript code which writes JavaScript code... just say "no".
     */
    "var comp_js = 'var cu=\\\\\\\'%s?context=%s&dn=%s\\\\\\\'; this.document.location.href=cu;'\n",
	dsgw_getvp( DSGW_CGINUM_EDIT ), context, edn ); 
    dsgw_emits("var dnlist = new Array;\n" );
    for ( i = 0; attrvals && attrvals[ i ] != NULL; i++ ) {
	xdn = ldap_explode_dn( attrvals[ i ], 1 );
	avedn = dsgw_strdup_escaped( attrvals[ i ]);
	dsgw_emitf( "dnlist[%d] = new Object\n", i );
	dsgw_emitf( "dnlist[%d].edn = '%s';\n", i, avedn );
	js0 = dsgw_escape_quotes( xdn[ 0 ] );
	if ( xdn[1] != NULL ) {
	    js1 = dsgw_escape_quotes( xdn[ 1 ] );
	    dsgw_emitf( "dnlist[%d].rdn = '%s, %s';\n", i, js0, js1 );
	    free( js1 );
	} else {
	    dsgw_emitf( "dnlist[%d].rdn = '%s';\n", i, js0 );
	}
	free( js0 );
	dsgw_emitf( "dnlist[%d].selected = false;\n", i );
	free( avedn );
	ldap_value_free( xdn );
    }
    dsgw_emitf( "dnlist.count = %d;\n", i );
    dsgw_emits( 
	"var changesMade = 0;\n"
	"\n"

	/*
	 * JavaScript function processSearch
	 */

	"function processSearch(f)\n"
	"{\n"
	"    var sel = f.type;\n"
	"    var selvalue = sel.options[sel.selectedIndex].value;\n"
	"    var lt = f.listtemplate;\n"
	"    if ( f.searchstring.value.length == 0 ) {\n");
    dsgw_emit_alert( "controlFrame", NULL, XP_GetClientStr( DBT_noSearchStringWasProvidedPleaseT_ ));
    dsgw_emits(
	"	return false;\n"
	"    }\n"
	"    lt.value = 'fa-' + selvalue;\n");
    dsgw_emitf(
	"    f.action = ");
    dsgw_quote_emitf( QUOTATION_JAVASCRIPT, "%s?context=%s",
	     dsgw_getvp( DSGW_CGINUM_DOSEARCH ), context);
    dsgw_emits( ";\n"
	"    f.searchstring.select();\n"
	"    f.searchstring.focus();\n"
	"    return true;\n"
	"}\n"
	"\n"

	/*
	 * JavaScript function removeItem
	 */

	"function removeItem(itemno, refresh)\n"
	"{\n"
	"    var extantDNs = dnlist;\n"
	"    var extantDNsCount = dnlist.count;\n"
	" \n"
	"    // Get rid of element in slot dup\n"
	"    for (k = itemno; k < extantDNsCount - 1; k++) {\n"
	"	extantDNs[k] = extantDNs[k+1];\n"
	"    }\n"
	"    dnlist.count--;\n"
	"    if ( refresh ) genOutputFrame(outputFrame, dnlist);\n"
	"    this.changesMade = 1;\n"
	"}\n"
	"\n"

	/*
	 * JavaScript function dnarrcomp
	 */

	"function dnarrcomp(a,b)\n"
	"{\n"
	"    return(a.edn.toLowerCase() > b.edn.toLowerCase());\n"
	"}\n"
	" \n"
#ifdef NAV30_SORT_NO_LONGER_COREDUMPS
	/*
	 * JavaScript function sortEntries
	 */

	"function sortEntries()\n"
	"{\n"
	"    var extantDNs = dnlist;\n"
	"    extantDNs.sort(dnarrcomp);\n"
	"    genOutputFrame(outputFrame, dnlist);\n"
	"}\n"
	"\n"
#endif /* NAV30_SORT_NO_LONGER_COREDUMPS */

	/*
	 * JavaScript function genOutputFrame
	 */

	"function genOutputFrame(oframe, dnl)\n"
	"{\n"
	"    var d = oframe.document;\n"
	"\n"
	"    d.open('text/html');\n"
	"    d.writeln('<HTML>');\n" );

    dsgw_emitf(
	"    d.writeln('<BODY %s>');\n", dsgw_html_body_colors );
    dsgw_emits(
        "    d.writeln(");
    dsgw_quotation_begin (QUOTATION_JAVASCRIPT);
    dsgw_form_begin (NULL, NULL);
    dsgw_quotation_end();
    dsgw_emits(      ");\n");
    dsgw_emits( 
	"    d.writeln('<CENTER>');\n"
	"    if (dnl.count == 0) {\n" );

    dsgw_emits( "       d.write(" );
    dsgw_quote_emits (QUOTATION_JAVASCRIPT, XP_GetClientStr (DBT_noNameInTheList_));
    dsgw_emits( ");\n" );

    dsgw_emits( "    } else if (dnl.count == 1) {\n" );
    dsgw_emits( "       d.write(" );
    dsgw_quote_emits (QUOTATION_JAVASCRIPT, XP_GetClientStr (DBT_oneNameInTheList_));
    dsgw_emits( ");\n" );
    dsgw_emits( "    } else {\n" );
    dsgw_emits( "       d.write('");
    dsgw_emitf( XP_GetClientStr( DBT_someNamesInTheList_ ), "' + dnl.count + '" );
    dsgw_emits( "');\n" );

    dsgw_emits( 
	"    }\n"
#ifdef NAV30_SORT_NO_LONGER_COREDUMPS
	"    d.writeln('</FONT>\\n')\n"
	"    d.writeln('<INPUT TYPE=\"button\" VALUE=\"Sort\" onClick=\"parent.sortEntries();\"></CENTER>\\n');\n"
#else
	"    d.writeln('</FONT></CENTER>\\n');\n"
#endif
	"    if (dnl.count > 0) {\n"
	"	d.write('<PRE><B>');\n" );

    dsgw_emitf(
	"	d.write('%s</B><HR>');\n",
		XP_GetClientStr( DBT_RemoveFromList_ ));

    dsgw_emits( 
	"	for (i = 0; i < dnl.count; i++) {\n"
	"	    d.write('<INPUT TYPE=CHECKBOX onClick=\"parent."
	"removeItem(' + i + ', true);\">');\n"
	"	    d.write('           ');\n"
	"	    d.write(dnl[i].rdn + '\\n');\n"
	"	}\n"
	"	d.writeln('</PRE></FORM><HR>');\n"
	"    }\n"
	"    d.writeln('</BODY>');\n"
	"    d.close();\n"
	"}\n"
	"\n"

	/*
	 * JavaScript function mergeLists
	 */

	"function mergeLists(mode, old, newl)\n"
	"{\n"
	"    var dup = -1;\n"
	"    var i, j, k;\n"
	" \n"
	"    for (i = 0; i < newl.count; i++) {\n"
	"	// Check for a duplicate\n"
	"	for (j = 0; j < old.count; j++) {\n"
	"	    dup = -1;\n"
	"	    if (newl[i].edn.toLowerCase() == "
	"old[j].edn.toLowerCase()) {\n"
	"		// Duplicate - skip\n"
	"		dup = j;\n"
	"		break;\n"
	"	    }\n"
	"	}\n"
	"	if ((dup == -1) && (mode == \"add\")) {\n"
	"	    // add new dn at end of array\n"
	"	    old[old.count] = new Array;\n"
	"	    old[old.count].edn = newl[i].edn;\n"
	"	    old[old.count].rdn = newl[i].rdn;\n"
	"	    old[old.count].sn = newl[i].sn;\n"
	"	    old[old.count].selected = false;\n"
	"	    old.count++;\n"
	"	} else if (dup != -1 && mode == \"remove\") {\n"
	"           removeItem(dup,false);\n"
	"	}\n"
	"    }\n"
	"}\n"
	"\n"

	/*
	 * JavaScript function updateList
	 */

	"function updateList(mode, old_list, new_list, outframe)\n"
	"{\n"
	"    mergeLists(mode, old_list, new_list);\n"
	"    genOutputFrame(outframe, old_list);\n"
	"    this.changesMade = 1;\n"
	"}\n"
	"\n"

	/*
	 * JavaScript function cancel
	 */
        "function cancel ()\n"
        "{\n"
	"    if (changesMade == 0) {\n"
	"	document.location = completion_url;\n"
	"    } else {\n");
    dsgw_emit_confirm ("controlFrame",
		       "opener.document.location.href = opener.completion_url;",
		       NULL /* no */,
		       XP_GetClientStr(DBT_discardChangesWindow_), 1,
		       XP_GetClientStr(DBT_discardChanges_));
    dsgw_emits (
	"    }\n"
	"}\n"
	"\n"

	/*
	 * JavaScript function saveChanges
	 */

	"function saveChanges()\n"
	"{\n"
	"    var i, j;\n"
	"    needToSaveChanges = true;\n"
	"    of = self.stagingFrame.document;\n"
	"    of.open('text/html');\n" );
    dsgw_emitf(
	"    of.write('<BODY onLoad=\"if ( parent.needToSaveChanges ) { parent.needToSaveChanges = false; document.stagingForm.submit() }\">');\n" );
    dsgw_emits(
	"    of.write('");
    dsgw_form_begin ("stagingForm",
	"action=\"%s\" METHOD=\"POST\" TARGET=\"_parent\"",
	 dsgw_getvp( DSGW_CGINUM_DOMODIFY ));
    dsgw_emits("\\n');\n");
    dsgw_emits(
	"    if (self.dnlist.count < 1) {\n"
	"       of.write('<INPUT TYPE=\"hidden\" NAME=\"replace_');\n"
	"       of.write(self.attrname);\n"
	"       of.write('\" VALUE=\"\">\\n');\n"
	"    } else {\n"
	"       for (j = 0; j < self.dnlist.count; j++) {\n"
	"	    of.write('<INPUT TYPE=\"hidden\" NAME=\"replace_');\n"
	"	    of.write(self.attrname);\n"
	"	    of.write('\" VALUE=\"');\n"
	"	    of.write(unescape(self.dnlist[j].edn));\n"
	"	    of.write('\">\\n');\n"
	"	}\n"
	"    }\n"
	"    of.writeln('<INPUT TYPE=\"hidden\" NAME=\"changetype\" "
	"VALUE=\"modify\">\\n');\n"
	"    of.writeln('<INPUT TYPE=\"hidden\" NAME=\"completion_javascript\" "
	"VALUE=\"' + comp_js + '\">');\n"
	"    of.writeln('<INPUT TYPE=\"hidden\" NAME=\"dn\" VALUE=\"' "
	"+ self.dn + '\"\\n');\n"
	"    of.writeln('<INPUT TYPE=\"hidden\" NAME=\"context\" "
	"VALUE=\"");
    dsgw_emits(context);
    dsgw_emits("\">\\n');\n"
	"    of.writeln('</FORM>\\n');\n"
	"    of.close();\n"
	"}\n"

	"</SCRIPT>\n"
	"\n"
	"<FRAMESET BORDER=1 FRAMEBORDER=1 ROWS=230,*,0,0 "
	"SCROLLING=\"NO\" NORESIZE onLoad=\"genOutputFrame"
	"(this.outputFrame, this.dnlist);\">\n" );
    dsgw_emitf( "   <FRAME SRC=\"%s?%s&dn=%s&context=%s&DNATTR=%s&"
	     "DNDESC=%s\" NAME=\"controlFrame\" SCROLLING=\"no\">\n",
	     dsgw_getvp( DSGW_CGINUM_EDIT ), tmplname, edn, context, attrname,
	     attrdesc );
    dsgw_emitf( "   <FRAME SRC=\"javascript:parent.emptyFrame\" "
	   "NAME=\"outputFrame\">\n"
	    "   <FRAME SRC=\"javascript:parent.emptyFrame\" "
	    "NAME=\"stagingFrame\">\n"
	   "</FRAMESET>\n"
           "</HTML>\n" );
    return 0;
}

/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* 
 * template.c:  The actual HTML templates in a static variable 
 *            
 * All blame to Mike McCool
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "template.h"
#include "libadmin/libadmin.h"
#include "libadmin/dbtlibadmin.h"
#include "base/util.h"

/* If you add something to this structure, don't forget to document it 
 * in templates.h, and increase MAXTEMPLATE!
 * 
 * Also, save yourself a lot of grief and put a space after the name.
 */

static struct template_s templates[MAXTEMPLATE] = {
  {"IF ", "FUNC conditional"},
  {"ELSE ", "FUNC conditional"},
  {"ENDIF ", "FUNC conditional"},
  {"TITLE ", "<HTML><HEAD><TITLE>%s</TITLE></HEAD>\n"
             "<BODY bgcolor=\"#C0C0C0\" link=\"#0000EE\" "
                         "vlink=\"#551A8B\" alink=\"#FF0000\" %s>\n"},
  {"PAGEHEADER ", "FUNC pageheader"},
  {"DOCSWITCHER ", ""},
  {"COPYRIGHT ", ""},
  {"RESOURCEPICKER ", "FUNC respicker"},
  {"BOOKTRACK ", "FUNC booktrack"},
  {"BEGININFO ", "<table border=2 width=100%% cellpadding=2>\n"
                 "<tr><td align=center colspan=2>"
                 "<b><FONT size=+1>%s</FONT></b></td></tr>"
#if 0
                 "<tr><td>"
                 "<IMG src=\"../icons/b-open.gif\" hspace=8 alt=\"*\""
                 "height=26 width=55></td>"
                 "<td>\n"},
#endif
                 "<td colspan=2>\n"},
  {"ADDINFO ", "</td></tr><tr><td colspan=2>"},
  {"ENDINFO ", "</td></tr></table>\n<hr width=10%%>\n"},
  {"SUBMIT ", "FUNC submit\n"},
  {"DOCUMENTROOT ", "FUNC docroot"},
  {"BEGINELEM ", "<pre>"},
/*  {"ELEM ", "<hr width=100%%><b>%s</b>"}, */
  {"ELEM ", "\n<b>%s</b>"},
/*  {"ENDELEM ", "<hr width=100%%></pre>\n"}, */
  {"ENDELEM ", "</pre>\n"},
  {"ELEMADD ", "<b>%s</b>"},
/*  {"ELEMDIV ", "<hr width=100%%>"}, */
  {"ELEMDIV ", "\n"},
  {"REFERER ", "FUNC link_referer"},
  {"INDEX ", "<a href=\"index\">%s</a>\n"},
  {"SERVERROOT ", "FUNC serverroot"},
  {"RESTART ", "<a href=\"pcontrol\">%s</a>\n"},
  {"ACCESS ", "FUNC makeurl"},
  {"COMMIT ", "<a href=\"commit?commit\">%s</a>\n"},
  {"BACKOUT ", "<center>If you don't want to %s, you can <a href=index>"
              "return to the server manager.</a></center>\n"},
  {"CURSERVNAME", "FUNC curservname"},
  {"VERIFY ", "FUNC verify"},
  {"HELPBUTTON", "FUNC helpbutton"},
  {"DIALOGSUBMIT", "FUNC dialogsubmit"},
  {"HELPJSFN", "FUNC helpjsfn"}
};

int get_directive(char *string);
void conditional(char *input, char **vars, int index);
void respicker(char **config);
void currentres(char **config);
void prevres(char **config);
void booktrack(char *input, char **vars);
void docswitcher(char *input);
void docroot(char **vars);
void link_referer(char **input, char **vars);
void serverroot(char **vars);
char **get_vars(char *string);
static void output(char *string);
void makeurl(char **vars);
void curservname(void);
void pageheader(char **vars, char **config);
void submit(int verify, char **vars);
void helpbutton(char *topic);
void dialogsubmit(char *topic);

static int status = -1;

/* Filter a page.  Takes the page to filter as an argument.  Uses above 
 * filters to process. 
 */
NSAPI_PUBLIC int parse_line(char *line_input, char **input)
{
     register int index;
     char *position;
     int dirlen = strlen(DIRECTIVE_START);
     char **vars;


     if(!strncmp(line_input, DIRECTIVE_START, dirlen)) {
         position = (char *) (line_input + dirlen);
         index = get_directive(position);

         /* did we get one? */
         if(index != -1)  {
             /* if so, get the vars. */
             position += strlen(templates[index].name);
             vars = get_vars(position);
             /* Dispatch the correct function (done for readability,
              * although I'm starting to wonder if I should bother)
              */
             if(!strncmp(templates[index].format, "FUNC ", 5))  {

                 if(!strncmp(templates[index].format+5, "conditional", 11))
                     conditional(input[0], vars, index);
                 else if(!strncmp(templates[index].format+5, "respicker", 9))
                     respicker(input);
                 else if(!strncmp(templates[index].format+5, "booktrack", 9))
                     booktrack(input[0], vars);
                 else if(!strncmp(templates[index].format+5, "docswitcher", 11))
                     docswitcher(input[0]);
                 else if(!strncmp(templates[index].format+5, "docroot", 7))
                     docroot(vars);
                 else if(!strncmp(templates[index].format+5, "link_referer",12))
                     link_referer(input, vars);
                 else if(!strncmp(templates[index].format+5, "serverroot",10))
                     serverroot(vars);
                 else if(!strncmp(templates[index].format+5, "makeurl",7))
                     makeurl(vars);
                 else if(!strncmp(templates[index].format+5, "curservname",11))
                     curservname();
                 else if(!strncmp(templates[index].format+5, "pageheader",10))
                     pageheader(vars, input);
                 else if(!strncmp(templates[index].format+5, "submit",6))
                     submit(0, vars);
                 else if(!strncmp(templates[index].format+5, "verify",6))
                     submit(1, vars);
                 else if(!strncmp(templates[index].format+5, "helpbutton",10))
                     helpbutton(vars[0]);
                 else if(!strncmp(templates[index].format+5, "dialogsubmit",12))
                     dialogsubmit(vars[0]);
                 /* We don't know what this template is.  Send it back. */
                 else return -1;
             }  else { 
                 /* I just can't believe there's no easy way to create 
                  * a va_list. */
                 char line[BIG_LINE];
                 sprintf(line, templates[index].format, 
                         (vars[0] != NULL) ? vars[0]: "",
                         (vars[1] != NULL) ? vars[1]: "",
                         (vars[2] != NULL) ? vars[2]: "",
                         (vars[3] != NULL) ? vars[3]: "");
                 output(line);
             }
         }  else  {
         /* We found a directive, but we can't identify it. Send it back.*/
         /* Check status first; if we're not supposed to be outputing */
         /* because of an "IF" block, don't tell the program to */
         /* try and cope with it. */
             if(status)
                 return -1;
             else
                 return 0;
         }
     }  else 
         /* We found no directive.  The line is normal. */
         output(line_input);

    /* If we're here, we either handled it correctly or the line was benign.*/
    return 0;
}

void conditional(char *input, char **vars, int index)
{    
     if((!strncmp(templates[index].name, "IF", 2)) &&
        (vars[0] != NULL))  {
         status = input[atoi(vars[0])] - '0';
     }  else
     if((!strncmp(templates[index].name, "ELSE", 4)) &&
        (status != -1))  {
         status ^= 1;
     }  else
     if(!strncmp(templates[index].name, "ENDIF", 5))
         status = -1;
}

void respicker(char **config)
{
    output("<FORM action=rsrcpckr method=GET>\n");
    output("<hr size=4><center>\n");
    prevres(config);
    output("</center><hr size=4>\n");
    output("</FORM>\n");
}

void currentres(char **config)
{
    int l;
    char line[BIG_LINE];
    char *resname, *restype;

    resname = get_current_resource(config);
    restype = get_current_typestr(config);
    if(!strcmp(restype, NAME))  {
        if(!strcmp(resname, "default"))
            sprintf(line, "<font size=+1>Modifying: "
                          "<b>the entire server</b>.</font>");
        else
            sprintf(line, "<font size=+1>Modifying: "
                          "<b>the object named %s</b>.</font>", resname);
    } 
    else if(!strcmp(restype, FILE_OR_DIR))  {
        l = strlen(resname) - 1;
        if(resname[l] == '*')  {
            sprintf(line, "<font size=+1>Modifying: <b>the directory "
                          "%s</b></font>",
                    resname);
        } else {
            sprintf(line, "<font size=+1>Modifying: <b>%s %s</b></font>",
                    (strchr(resname, '*')) ? "files matching" : "the file",
                    resname);
        }    
    }
    else if(!strcmp(restype, TEMPLATE))  {
        sprintf(line, "<font size=+1>Modifying: <b>the template %s</b></font>",
                      resname);
    }
    else if(!strcmp(restype, WILDCARD))  {
#ifdef MCC_PROXY
        sprintf(line, "<font size=+1>Modifying: <b>URLs matching RE %s"
#else
        sprintf(line, "<font size=+1>Modifying: <b>files matching %s"
#endif
                      "</b></font>",
                      resname);
    }
    output(line);
}

void prevres(char **config)
{
#ifndef MCC_NEWS
    char *res = get_current_resource(config);
    int rtype = get_current_restype(config);

    if(status)  {
        char **options = NULL;
        register int x=0;
        int found=0;
	int option_cnt = total_object_count();

        fprintf(stdout, "<SCRIPT language=JavaScript>\n");
        fprintf(stdout, "function checkForClick()  {\n");
        fprintf(stdout, "    document.forms[0].resource.blur();\n");
        fprintf(stdout, "    var idx=document.forms[0]."
                                    "resource.options.selectedIndex;\n");
        fprintf(stdout, "    if(document.forms[0].resource."
                                "options[idx].defaultSelected == 0)  {\n");
        fprintf(stdout, "        document.forms[0].submit();\n");
        fprintf(stdout, "        return 1;\n");
        fprintf(stdout, "    } else return 0;\n");
        fprintf(stdout, "}\n");
        fprintf(stdout, "</SCRIPT>\n");

#ifdef MCC_PROXY
	fprintf(stdout, "<TABLE BORDER=0>\n");
	fprintf(stdout, "<TR><TD><font size=+1>Editing:</font></TD>\n"); 
        fprintf(stdout,
		"<TD><SELECT name=\"resource\" onChange=\"checkForClick()\" SIZE=\"%d\">\n",
		option_cnt <= 20 ? 1 : 5);
#else
        output("<nobr>");
        fputs("<font size=+1>Editing:</font>\n", stdout); 
        fprintf(stdout, "<SELECT name=\"resource\" "
                "onChange=\"checkForClick()\" %s>\n", 
                option_cnt <=20 ? "" : "size=5");
#endif

#ifdef MCC_HTTPD /* template->styles nightmare */
        if((rtype==PB_NAME) && (strcmp(res, "default")))  {
        /* enter: STYLES MODE */
            fprintf(stdout, "<OPTION value=ndefault>Exit styles mode\n");
        }  else  {
            fprintf(stdout, "<OPTION value=ndefault %s>The entire server\n",
                            (!strcmp(res, "default")) ? "SELECTED" : "");
        }
#else
        fprintf(stdout, "<OPTION value=ndefault %s>The entire server\n",
                        (!strcmp(res, "default")) ? "SELECTED" : "");
#endif
        if(!strcmp(res, "default")) found=1;
        options = list_objects(PB_PATH);
#ifdef MCC_HTTPD /* template->styles nightmare */
        if((options) && !((rtype==PB_NAME) && (strcmp(res, "default"))) )  {
#else
        if(options)  {
#endif
            for(x=0; options[x]; x++)  {
                fprintf(stdout, "<OPTION value=f%s %s>%s\n", 
                                options[x],
                                (!strcmp(options[x], res)) ? "SELECTED" : "",
                                options[x]);
                if(!strcmp(options[x], res)) found=1;
            }
        }
        options=list_objects(PB_NAME);
#ifdef MCC_HTTPD /* template->styles nightmare */
        if((options) && ((rtype==PB_NAME) && (strcmp(res, "default"))) )  {
#else
        if(options)  {
#endif
            for(x=0; options[x]; x++)  {
                if(!strcmp(options[x], "default") ||
                   !strcmp(options[x], "cgi"))
                    continue;
#ifdef MCC_HTTPD /* template->style usability */
                fprintf(stdout, "<OPTION value=n%s %s>The style '%s'\n", 
                                options[x],
                                (!strcmp(options[x], res)) ? "SELECTED":"",
                                options[x]);
#else
                fprintf(stdout, "<OPTION value=n%s %s>The template '%s'\n", 
                                options[x],
                                (!strcmp(options[x], res)) ? "SELECTED":"",
                                options[x]);
#endif
                if(!strcmp(options[x], res)) found=1;
            }
        }
        if(!found)  {
            if(rtype==PB_NAME)  {
                fprintf(stdout, "<OPTION value=n%s SELECTED>The template %s\n",
                        res, res);
            }  else  {
                fprintf(stdout, "<OPTION value=f%s SELECTED>%s\n",
                        res, res);
            }
        }
        fputs("</SELECT></nobr>\n", stdout);
        fputs("<nobr>", stdout);
#ifndef MCC_PROXY
        fprintf(stdout, "<INPUT type=button value=\"Browse...\" "
                        "onClick=\"window.location='rsrcpckr?b'\"> ");
#endif
#ifdef MCC_PROXY
	fprintf(stdout, "</TD>\n<TD>");
        fprintf(stdout, "<INPUT type=button value=\"Regular Expression...\" "
                        "onClick=\"var pat="
                        "prompt('Enter the regular expression to edit:', ''); "
#else
        fprintf(stdout, "<INPUT type=button value=\"Wildcard...\" "
                        "onClick=\"var pat="
                        "prompt('Enter the wildcard pattern to edit:', ''); "
#endif
                        "if(pat!=null) window.location='rsrcpckr?"
                        "type="WILDCARD"&resource='+escape(pat);\">");
#ifdef MCC_PROXY
	fprintf(stdout, "</TD>\n</TR>\n</TABLE>\n");
#endif
        fputs("</nobr>", stdout);
        /* output("</td></tr>\n"); */
    }  
#endif
}

void booktrack(char *input, char **vars)  
{
    char line[BIG_LINE];

    if((vars[0] != NULL) && (vars[1] != NULL))  {
        sprintf(line, "<a href=index?0>"
                      "<img src=\"%s\" hspace=8 align=%s alt=\"\"></a>", 
                      (input[0] - '0') ? vars[0] : vars[1], 
                      (vars[2] != NULL) ? vars[2] : "none");
        output(line);
    }
}

void docswitcher(char *input)
{
    char line[BIG_LINE];
    char *whichimg, *whatmode;
#ifdef USE_ADMSERV
    char *qs = getenv("QUERY_STRING");
    char *sname = getenv("SCRIPT_NAME");
    char *mtmp;

    char *tmp = getenv("SERVER_NAMES");
    char *servers = NULL;
    if(tmp) servers = STRDUP(tmp);
#endif

    if(!(input[0] - '0'))  {
        whichimg = "b-clsd.gif";
        whatmode = "Express mode";
    } else {
        whichimg = "b-open.gif";
        whatmode = "Full docs";
    }

    mtmp = (char *) MALLOC( (sname? strlen(sname) : 0) +
                            (qs? strlen(qs) : 0) +
                            (strlen(whichimg) + strlen(whatmode)) +
                            1024);
    sprintf(mtmp, "<center><table border=2 width=95%%>\n"
                  "<tr><td rowspan=2><a href=index%s>"
                  "<img src=\"../icons/%s\" "
                  "alt=\"[%s]   \" border=2>"
                  "</td>\n",
                  (qs ? "?0" : sname),
                  whichimg, whatmode);
    output(mtmp);

#ifdef USE_ADMSERV
    if(!servers)  {
        sprintf(line, "<td width=100%% align=center rowspan=2><b>%s</b></td>\n",
                      whatmode);
        output(line);
    }  else
    if(servers[0] == '(')  {

        sprintf(line, "<td width=100%% align=center>Current servers:<br>\n");
        output(line);
        output("<b>");

        tmp=strtok(++servers, "|)");
        while(tmp)  {
            char *tmp2;
            output("<nobr>");
            tmp2=strchr(tmp, '-');
            tmp2++;
            output(tmp2);
            tmp=strtok(NULL, "|)");
            if(tmp)
                output(",");
            output("</nobr>\n");
        }
        output("</b></td>\n");
    }  else  {

        sprintf(line, "<td width=100%% align=center>Current server: ");
        output(line);
        output("<b>");
        tmp = strchr(servers, '-');
        *tmp++ = '\0';
        output(tmp);
        output("</b>");
        output("</td>\n");
    }
#endif
    sprintf(mtmp, "<td rowspan=2><a href=index%s>"
                  "<img src=\"../icons/%s\" "
                  "alt=\"\" border=2></a></td></tr>\n", 
                  (qs? "?0" : sname), 
                  whichimg);
    output(mtmp);
#ifdef USE_ADMSERV
    if(servers)  {
        sprintf(line, "<tr><td align=center>"
                      "<a href=\"/admin-serv/bin/chooser\">"
                      "Choose</a> a new server or set of servers</a></td>\n");
        output(line);
    }
#endif
    sprintf(line, "</tr></table></center>\n");
    output(line);
    output("<hr width=10%%>\n");
}

void docroot(char **vars) 
{
#ifndef MCC_NEWS
    char line[BIG_LINE];
    pblock *pb = grab_pblock(PB_NAME, "default", "NameTrans", "document-root", 
                             NULL, NULL); 
    char *docroot = "";
    if(pb)
        docroot = pblock_findval("root", pb);
    sprintf(line, "<b>%s%s</b>\n", docroot, (vars[0] != NULL) ? vars[0] : "");
    output(line);
#endif
}

void serverroot(char **vars) 
{
    char line[BIG_LINE];
#ifdef USE_ADMSERV
    char *sroot = getenv("NETSITE_ROOT");
#else
    char *sroot = get_mag_var("#ServerRoot");
#endif
    sprintf(line, "%s%s", (sroot) ? sroot : "", (vars[0]) ? vars[0] : "");
    output(line);
}

void makeurl(char **vars)
{
    char line[BIG_LINE];

    sprintf(line,"<a href=%s target=_blank>%s</a>\n", 
                 get_serv_url(), vars[0] ? vars[0] : "");
    output(line);
}

void curservname(void)
{
    output(get_srvname(0));
}

NSAPI_PUBLIC 
void pageheader(char **vars, char **config)
{
    char line[BIG_LINE];
#if 0 /* MLM - put in to have non-working Back button */
    char *ref=get_referer(config);
    char *t;
#endif

    output("<center><table border=2 width=100%%>\n");

    util_snprintf(line, BIG_LINE, "<tr>");
    output(line);

    util_snprintf(line, BIG_LINE, "<td align=center width=100%%>");
    output(line);
    util_snprintf(line, BIG_LINE, "<hr size=0 width=0>");
    output(line);
#if 0 /* MLM - put in to have non-working Back button */
    t=strrchr(ref, '/');
    *t++='\0';
    util_snprintf(line, BIG_LINE, "<a href=\"%s/index/%s\">", ref, t);
    output(line);
    util_snprintf(line, BIG_LINE, "<img align=right src=../icons/back.gif "
                                  "width=41 height=26 border=0></a>\n");
    output(line);
#endif
    util_snprintf(line, BIG_LINE, "<FONT size=+2><b>%s</b></FONT>"
                                  "<hr size=0 width=0>"
                                  "</td>", vars[2]);
    output(line);
    
    output("</tr></table></center>\n");
}

char *_get_help_button(char *topic)
{
  char line[BIG_LINE];

  util_snprintf( line, BIG_LINE,
		 "<input type=button value=\"%s\" "
		 "onClick=\"%s\">", XP_GetAdminStr(DBT_help_),
		 topic ? helpJavaScriptForTopic( topic ) : helpJavaScript() );

  return(STRDUP(line));
}

NSAPI_PUBLIC char *helpJavaScriptForTopic( char *topic )
{
    char *tmp;
    char line[BIG_LINE];
    char *server=get_srvname(0);
    char *type;
    int	 typeLen;

    /* Get the server type, without the instance name into type */
    tmp = strchr( server, '-' );
    typeLen = tmp - server;

    type = (char *)MALLOC( typeLen + 1 );
    type[typeLen] = '\0';
    while ( typeLen-- ) {
      type[typeLen] = server[typeLen];
    }
    util_snprintf( line, BIG_LINE,
		   "if ( top.helpwin ) {"
		   "  top.helpwin.focus();"
		   "  top.helpwin.infotopic.location='%s/%s/admin/tutor?!%s';"
		   "} else {"
		   "  window.open('%s/%s/admin/tutor?%s', '"
		   INFO_IDX_NAME"_%s', "
		   HELP_WIN_OPTIONS");}",
		   getenv("SERVER_URL"), server, topic,
		   getenv("SERVER_URL"), server, topic,
		   type );
		   
    return(STRDUP(line));
}

NSAPI_PUBLIC char *helpJavaScript()
{
    char *tmp, *sn;

    tmp=STRDUP(getenv("SCRIPT_NAME"));
    if(strlen(tmp) > (unsigned)BIG_LINE)
        tmp[BIG_LINE-2]='\0';
    sn=strrchr(tmp, '/');
    if( sn )
        *sn++='\0';
    return helpJavaScriptForTopic( sn );
}

void submit(int verify, char **vars)
{
    char line[BIG_LINE];
    char outline[BIG_LINE];

    if(verify)  {
        util_snprintf(line, BIG_LINE, "<SCRIPT language="MOCHA_NAME">\n"
                      "function verify(form)  {\n"
                      "    if(confirm('Do you really want to %s?'))\n"
                      "        form.submit();\n"
                      "}\n"
                      "</SCRIPT>\n", vars[0]);
        output(line);
    }

    output("<center><table border=2 width=100%%><tr>");

    if(!verify)  {
        util_snprintf(outline, BIG_LINE, "%s%s%s%s%s",
            "<td width=33%% align=center>",
            "<input type=submit value=\"",
            XP_GetAdminStr(DBT_ok_),
            "\">",
            "</td>\n");
    }  else  {
        util_snprintf(outline, BIG_LINE, "%s%s%s%s%s%s",
            "<td width=33%% align=center>",
            "<input type=button value=\"",
            XP_GetAdminStr(DBT_ok_),
            "\" ",
            "onclick=\"verify(this.form)\">",
            "</td>\n");
    }
    output(outline);
    util_snprintf(outline, BIG_LINE, "%s%s%s%s",
        "<td width=34%% align=center>",
        "<input type=reset value=\"",
        XP_GetAdminStr(DBT_reset_),
        "\"></td>\n");
    output(outline);
        
    util_snprintf(line, BIG_LINE, "<td width=33%% align=center>%s</td>\n",
                  _get_help_button( vars[0] ));
    output(line);

    output("</tr></table></center>\n");

    output("</form>\n");

    output("<SCRIPT language="MOCHA_NAME">\n");
    output("</SCRIPT>\n");
}

void helpbutton(char *topic)
{
    output("<form><p><div align=right><table width=33%% border=2>"
           "<tr><td align=center>");
    output(_get_help_button(topic));
    output("</td></tr></table></div></form>\n");
    output("<SCRIPT language="MOCHA_NAME">\n");
    output("</SCRIPT>\n");
}

void dialogsubmit(char *topic)
{
    char line[BIG_LINE];
    char outline[BIG_LINE];

    output("<center><table border=2 width=100%%><tr>");

    util_snprintf(outline, BIG_LINE, "%s%s%s%s%s",
        "<td width=33%% align=center>",
        "<input type=submit value=\"",
        XP_GetAdminStr(DBT_done_),
        "\">",
        "</td>\n");
    output(outline);
    util_snprintf(outline, BIG_LINE, "%s%s%s%s%s",
        "<td width=34%% align=center>",
        "<input type=button value=\"",
        XP_GetAdminStr(DBT_cancel_),
        "\" "
        "onClick=\"top.close()\"></td>\n");
    output(outline);

    util_snprintf(line, BIG_LINE, "<td width=33%% align=center>%s</td>\n",
                  _get_help_button(topic));
    output(line);

    output("</tr></table></center>\n");

    output("</form>\n");

    output("<SCRIPT language="MOCHA_NAME">\n");
    output("</SCRIPT>\n");
}

void helpjsfn(void)
{
    char *tmp;
    char line[BIG_LINE];
    char *server=get_srvname(0);
    char *type;
    int	 typeLen;

    /* Get the server type, without the instance name into type */
    tmp = strchr( server, '-' );
    typeLen = tmp - server;

    type = (char *)MALLOC( typeLen + 1 );
    type[typeLen] = '\0';
    while ( typeLen-- ) {
	type[typeLen] = server[typeLen];
    }

    output("function displayHelpTopic(topic)\n");
    output("{\n");
    util_snprintf(line, BIG_LINE,
          "    if (top.helpwin) {\n"
          "        top.helpwin.focus();\n"
	  "        top.helpwin.infotopic.location='%s/%s/admin/tutor?!' + topic;\n"
          "    } else {\n"
          "        window.open('%s/%s/admin/tutor?' + topic, '"
	  INFO_IDX_NAME"_%s', "
	  HELP_WIN_OPTIONS");\n"
	  "    }\n"
	  "}\n",
	  getenv("SERVER_URL"), server,
	  getenv("SERVER_URL"), server,
	  type );
    output(line);
}

void link_referer(char **input, char **vars) 
{
  char line[BIG_LINE];

  sprintf( line, "<SCRIPT language="MOCHA_NAME">\n"
	   "document.writeln( '%s'.link( '%s' ) );\n"
	   "</SCRIPT>\n", ( vars[0] ? vars[0] : getenv( "SCRIPT_NAME" ) ),
	   cookieValue( "adminReferer", NULL ) );
  output( line );
}

int get_directive(char *string)  
{
    int index = -1;
    register int x;

    for(x=0; x < MAXTEMPLATE; x++)  {
        if(!strncmp(string, templates[x].name,
                    strlen(templates[x].name))) {
            index = x;
            break;
        }
    }
    return index;
}

NSAPI_PUBLIC int directive_is(char *target, char *directive)
{
    char *position = (target + strlen(DIRECTIVE_START));
    return(!(strncmp(directive, position, strlen(directive))));
}

char **get_vars(char *string)
{
    char **vars;
    register int x;
    int isvar;
    char scratch[BIG_LINE];
    char lastchar;

/* Initialize the vars array.
 */
    vars = (char **) MALLOC((MAXVARS)*(sizeof(char *)));
    for(x=0; x< MAXVARS; x++)  
        vars[x] = NULL;

    isvar = -1;
    x = 0;
    scratch[0] = '\0';
    lastchar = ' ';
 
    while(*string != '\0') {
        if((*string == '\"') && (lastchar != '\\'))
            if(isvar != -1)  {
                vars[x++] = (char *)STRDUP(scratch);
                isvar = -1;
                if(x == MAXVARS)
                    break;
            }  else
                isvar = 0;
        else
            if(isvar != -1)  {
                scratch[isvar++] = *string; 
                scratch[isvar] = '\0';
            }
            else
                if(*string == DIRECTIVE_END)
                    break;
        lastchar = *string;
        string++;
    }
    return vars;
}

static void output(char *line)  
{
    if(status)
        fputs(line, stdout);
}

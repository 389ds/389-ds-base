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
 * tutor.c - Take a qs, and spit out the appropriate tutorial
 *
 * All blame to Mike McCool
 */

#include <stdio.h>
#include <stdlib.h>
#include "dsgw.h"

#define BASE_MAN_DIRECTORY "manual/"
#define BASE_INFO_DIRECTORY "info/"
#define HELP_INDEX_HTML "manual/index.html"
/*#define MANUAL_HPATH "bin/lang?file=" DSGW_MANUALSHORTCUT "/"*/

/* Copied from ldapserver/lib/base/util.c */
static int
my_util_uri_is_evil(char *t)
{
    register int x;
 
    for(x = 0; t[x]; ++x) {
        if(t[x] == '/') {
            if(t[x+1] == '/')
                return 1;
            if(t[x+1] == '.') {
                switch(t[x+2]) {
                case '.':
                    if((!t[x+3]) || (t[x+3] == '/'))
                        return 1;
                case '/':
                case '\0':
                    return 1;
                }
            }
        }
#ifdef XP_WIN32
        /* On NT, the directory "abc...." is the same as "abc"
         * The only cheap way to catch this globally is to disallow
         * names with the trailing "."s.  Hopefully this is not over
         * restrictive
         */
        if ((t[x] == '.') && ( (t[x+1] == '/') || (t[x+1] == '\0') )) {
            return 1;
        }
#endif
    }
    return 0;
}


FILE *
_open_html_file( char *filename )
{
    FILE *f;
    char *mypath;
    char *p;

    p = dsgw_file2path( DSGW_MANROOT, "slapd/gw/" );
    mypath = (char *)dsgw_ch_malloc( strlen( p ) +
				       strlen( filename ) + 1 );
    sprintf( mypath, "%s%s", p, filename );

    if (!(f = fopen( mypath, "r" ))) {
	dsgw_error( DSGW_ERR_OPENHTMLFILE, filename, DSGW_ERROPT_EXIT,
		0, NULL );
    }

    free( p );
    free( mypath );

    return f;
}



/* Had to copy and paste so wouldn't set referer. */
void _my_return_html_file(char *filename, char *base)  {
    char line[BIG_LINE];
    FILE *html = _open_html_file(filename);

    if(base)  {
        char *tmp;
        char *surl=getenv("SERVER_URL");
        char *sn=dsgw_ch_strdup(getenv("SCRIPT_NAME"));
        tmp=strchr(&(sn[1]), '/');
        *tmp='\0';
        dsgw_emitf("<BASE href=\"%s%s/%s\">\n", surl, sn, base);
    }
    while( fgets(line, sizeof(line), html))  {
	dsgw_emits( line );
    }
}


int
main(
    int		argc,
    char	*argv[]
#ifdef DSGW_DEBUG
    ,char	*env[]
#endif
)
{
    char *qs = getenv("QUERY_STRING");
    char *html=NULL;
    char *base=NULL;

#ifdef DSGW_DEBUG
   dsgw_logstringarray( "env", env ); 
#endif
   
    if(qs == NULL || *qs == '\0')  {
        dsgw_send_header();
        _my_return_html_file(BASE_MAN_DIRECTORY HELP_INDEX_HTML, NULL);
        exit(0);
    } else {
	/* parse the query string: */
	auto char *p, *iter = NULL;
	
	/*get a pointer to the context. It should be the last part of the qs*/
	p = ldap_utf8strtok_r( qs, "&", &iter );
      
	/*
	 * Get the conf file name. It'll be translated
	 * into /dsgw/context/CONTEXT.conf if
	 * CONTEXT is all alphanumeric (no slahes,
	 * or dots). CONTEXT is passed into the cgi.
	 * if context=CONTEXT is not there, or PATH_INFO
	 * was used, then use dsgw.conf
	 */
	if ( iter != NULL && !strncasecmp( iter, "context=", 8 )) {
	    context = dsgw_ch_strdup( iter + 8 );
	    dsgw_form_unescape( context );
	}
	
    }

    dsgw_init( argc, argv, DSGW_METHOD_GET );

    html = (char *) dsgw_ch_malloc(strlen(qs)+10+10);
    sprintf(html, "%s.html", qs);
    if (my_util_uri_is_evil(html))  {
        dsgw_send_header();
        dsgw_emits( "<CENTER><H2>Error</H2></CENTER>\n"
		   "<P>\n"
		   "URL contains dangerous characters.  Cannot display\n"
		   "help text." );
	exit( 0 );
    }

    if(qs[0]=='!')  {
        qs++;
        if(!strncmp(qs, BASE_INFO_DIRECTORY, strlen(BASE_INFO_DIRECTORY)))  {
            sprintf(html, "%s.html", qs);
        } else if(!strncmp(qs, BASE_MAN_DIRECTORY, strlen(BASE_MAN_DIRECTORY))) {
            if(!strstr(qs, ".html"))  {
                sprintf(html, "%s.htm", qs);
            }  else  {
                sprintf(html, "%s", qs);
            }
            base=qs;
        } 
        else  {
            char line[BIG_LINE];
            FILE *map=NULL;
            char *man_index=NULL;

            man_index = dsgw_file2path ( DSGW_MANROOT, "slapd/gw/manual/index.map" );

            html[0]='\0';

            map=fopen(man_index, "r");
            if(!map) 
                goto ohwell;
            while(fgets(line, sizeof(line), map))  {
                if(line[0]==';')  
                    continue;
                else if(ldap_utf8isspace(line))
                    continue;
                else  {
                    /* parse out the line */
                    register char *head=NULL, *tail=NULL;
                    int found;

                    head=&(line[0]);
                    tail=head;
                    found=0;
                    while(*tail)  {
                        if(ldap_utf8isspace(tail) || *tail=='=')  {
                            *tail='\0';
                           found=1;
                            /* get rid of extra stuff at the end */
                            tail++;
			    while(1) {
				if (*tail == 0) {
				    ++tail; /* This looks wrong. */
				    break;
				}
				LDAP_UTF8INC(tail);
                                if((!ldap_utf8isspace(tail)) && (*tail!='='))
                                    break;
			    }
                            break;
                        }
                        LDAP_UTF8INC(tail);
                    }
                    if(!found) continue;

                    /* script name is in head */
                    if(strncasecmp(head, qs, strlen(qs)))  {
                        continue;
                    }
                    /* match found.  get the actual file name */
                    head=tail;
/* Step on CRs and LFs. */
                    while(*tail)  {
                        if((*tail=='\r') || (*tail=='\n') || (*tail==';'))  {
                            *tail='\0';
                            break;
                        }
                        LDAP_UTF8INC(tail);
                    }
#if 0
/* No longer remove whitespace at end of line.  Now is whitespace in link. */
                    while(*LDAP_UTF8DEC(tail))  {
                        if(ldap_utf8isspace(tail)) *tail='\0';
                        else break;
                    }
#endif
                    /* assumedly, head should now have the proper HTML file
                     * from the manual inside. redirect the client 'cause
                     * there's no other way to get them to jump to the 
                     * right place. 
		     * Looks like: 
		     * http://host:port/dsgw/bin/lang?context=CONTEXT&file=.MANUAL/FILE.HTM
		     * Where MANUAL is literal
		     */ 
                    dsgw_emitf("Location: %s%s/%s\n\n", 
			       gc->gc_urlpfxmain, DSGW_MANUALSHORTCUT, head);

                    fclose(map);
                    exit(0);
                }
            }
            fclose(map);
	    free( man_index );

ohwell:
            if(!html[0])
                sprintf(html, "%s%s.html", BASE_MAN_DIRECTORY, qs);
        }
        dsgw_send_header();
        _my_return_html_file(html, base);
    }  else  {
        dsgw_send_header();
        dsgw_emits("<TITLE>Directory Server Gateway Help</TITLE>\n");
        dsgw_emits("\n"); 
        dsgw_emits("<frameset BORDER=0 FRAMEBORDER=NO rows=\"57,*\" "
		"onLoad=\"top.master=top.opener.top;top.master.helpwin=self;\" "
		"onUnload=\"if (top.master) { top.master.helpwin=0; }\">\n" );
        dsgw_emitf("<frame src=\"%s?!info/infonav&context=%s\" scrolling=no "
		   "marginwidth=0 marginheight=0 "
		   "name=\"infobuttons\">\n", dsgw_getvp(DSGW_CGINUM_TUTOR), context);
        dsgw_emitf("<frame src=\"%s?!%s&context=%s\" "
		   "name=\"infotopic\">\n", dsgw_getvp(DSGW_CGINUM_TUTOR), qs, context);
        dsgw_emits("</frameset>\n");
    }
    return 1;
}

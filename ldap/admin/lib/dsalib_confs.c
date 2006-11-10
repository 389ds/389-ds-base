/** BEGIN COPYRIGHT BLOCK
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
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * Some of the simple conf stuff here. Must not call any
 * libadmin functions! This is needed by ds_config.c
 */
#if defined( XP_WIN32 )
#include <windows.h>
#endif
#include "dsalib.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ldif.h>
#include <ctype.h>
#include "nspr.h"
#include "plstr.h"

/*
 * Read the configuration info into a null-terminated list of strings.
 */
DS_EXPORT_SYMBOL char **
ds_get_conf_from_file(FILE *conf)
{
    static char config_entry[] = "dn: cn=config";
    static int cfg_ent_len = sizeof(config_entry)-1;
    int		listsize = 0;
    char        **conf_list = NULL;
    char *entry = 0;
    int lineno = 0;

    while ((entry = ldif_get_entry(conf, &lineno))) {
	char *begin = entry;
	if (!PL_strncasecmp(entry, config_entry, cfg_ent_len)) {
	    char *line = entry;
	    while ((line = ldif_getline(&entry))) {
		char *type, *value;
		int vlen = 0;
		int rc;
	        char *errmsg = NULL;

		if ( *line == '\n' || *line == '\0' ) {
		    break;
		}

		/* this call modifies line */
		rc = ldif_parse_line(line, &type, &value, &vlen, &errmsg);
		if (rc != 0)
		{
		    if ( errmsg != NULL ) {
			ds_send_error(errmsg, 0);
			PR_smprintf_free(errmsg);
		    } else {
			ds_send_error("Unknown error processing config file", 0);
		    }
		    free(begin);
		    return NULL;
		}
		listsize++;
		conf_list = (char **) realloc(conf_list, 
					      ((listsize + 1) * sizeof(char *)));
		/* this is the format expected by ds_get_config_value */
		conf_list[listsize - 1] = PR_smprintf("%s:%s", type, value);
		conf_list[listsize] = NULL;		/* always null terminated */
	    }
	}
	free(begin);
    }
			
    return(conf_list);
}

/*
 * Returns 1 if parm is in confline else 0
 */
static int
ds_parm_in_line(char *confline, char *parm)
{
    int parm_size;
 
    if ( confline == NULL )
        return(0);
    if ( parm == NULL )
        return(0);
    parm_size = strlen(parm);
    if ( parm_size == (int)NULL )
        return(0);
    if ( PL_strncasecmp(confline, parm, parm_size) == 0 )
        if ( ((int) strlen(confline)) > parm_size )
            if ( confline[parm_size] == ':' )
                return(1);
    return(0);
}
 
/*
 * Gets the string that corresponds to the parameter supplied from the
 * list of config lines.  Returns a malloc'd string.
 */
DS_EXPORT_SYMBOL char *
ds_get_value(char **ds_config, char *parm, int phase, int occurance)
{
    char        *line; 
    int         line_num = 0;
    int         cur_phase = 0;
    int         cur_occurance = 0;
 
    if ( (parm == NULL) || (ds_config == NULL) )
        return(NULL);
    if ( (phase < 0) || (occurance < 1) )
        return(NULL);
    line = ds_config[line_num];
    while ( line != NULL ) {
	if ( ds_parm_in_line(line, "database") )
	    cur_phase++;
        if ( ds_parm_in_line(line, parm) ) {    /* found it */
	    if ( phase == cur_phase )
		if ( ++cur_occurance == occurance ) {
		    /*
		     * Use ldif_parse_line() so continuation markers are
		     * handled correctly, etc.
		     */
		    char	*errmsg, *type = NULL, *value = NULL, *tmpvalue = NULL;
		    int		ldif_rc, tmpvlen = 0;
		    char	*tmpline = strdup(line);

		    if ( NULL == tmpline ) {
			ds_send_error(
				"ds_get_value() failed: strdup() returned NULL\n",
				1 /* print errno */ );
			return(NULL);
		    }

		    ldif_rc = ldif_parse_line( tmpline, &type, &tmpvalue,
						&tmpvlen, &errmsg );
		    if (ldif_rc < 0) {
			ds_send_error(errmsg, 0 /* do not print errno */);
		    } else if (ldif_rc == 0) {	/* value returned in place */
			value = strdup(tmpvalue);
		    } else {			/* malloc'd value */
			value = tmpvalue;
		    }
		    free(tmpline);
			if (errmsg) {
				PR_smprintf_free(errmsg);
			}
		    return value;
		}
        }
        line_num++;
        line = ds_config[line_num];
    }
    return(NULL);
}

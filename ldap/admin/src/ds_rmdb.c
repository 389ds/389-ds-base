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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* 
 * Delete backed-up database files.
 *
 * Anil Bhavnani
 * Removed all HTML output for DS 4.0: Rob Weltman
 */

#include <stdio.h>
#include <stdlib.h>
#include "libadminutil/admutil.h"
#include "dsalib.h"
#include "portable.h"
#include "init_ds_env.h"
#include <string.h>
#ifdef XP_UNIX
#include <unistd.h>
#endif
#ifdef XP_WIN32
#include <direct.h>
#endif
#include "nspr.h"

#ifndef MAXPATHLEN
#define MAXPATHLEN	1024
#endif

int main(int argc, char *argv[], char *envp[])
{
    char *del_file = NULL;
    char **bak_files;

    fprintf(stdout, "Content-type: text/html\n\n");

	if ( init_ds_env() )
		return 1;

    ds_become_localuser (ds_get_config (DS_REAL_CONFIG));

	/*
	 * Get value of the "deletefile" variable.
	 */
	del_file = ds_get_cgi_var("deletefile");
	if ( (NULL == del_file) || (strlen(del_file) < 1) ) {
		rpt_err( DS_UNDEFINED_VARIABLE, "deletefile", NULL, NULL );
		return 1;
	}

	bak_files = ds_get_file_list( del_file );
	if ( bak_files == NULL ) {
		rpt_err( DS_NO_SUCH_FILE, del_file, NULL, NULL );
		return 1;
	} else {
		int j;
		char buf[ MAXPATHLEN ];
		for ( j = 0; bak_files[ j ] != NULL; j++ ) {
			PR_snprintf( buf, sizeof(buf), "%s/%s", del_file, bak_files[ j ]);
			if (  unlink(buf) != 0 ) {
				rpt_err( DS_CANNOT_DELETE_FILE, buf, NULL, NULL );
				return 1;
			}
		}
		if ( rmdir( del_file ) < 0 ) {
			rpt_err( DS_CANNOT_DELETE_FILE, del_file, NULL, NULL );
			return 1;
		}
	}
	rpt_success("Success! Deleted directory.");

    return 0;
}

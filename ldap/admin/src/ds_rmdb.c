/** BEGIN COPYRIGHT BLOCK
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

#ifndef MAXPATHLEN
#define MAXPATHLEN	1024
#endif

int main(int argc, char *argv[], char *envp[])
{
    char *del_file = NULL;
    char **bak_files;
    int	file_count = 0;
    int err = 0;

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
			sprintf( buf, "%s/%s", del_file, bak_files[ j ]);
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

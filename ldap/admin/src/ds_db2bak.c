/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * Backs up the database.
 * 
 * Anil Bhavnani
 * Removed all HTML output for DS 4.0: Rob Weltman
 */

#include <stdio.h>
#include <stdlib.h>
#include "libadminutil/admutil.h"
#include "dsalib.h"
#include "init_ds_env.h"
#include <string.h>
#ifdef XP_UNIX
#include <unistd.h>
#endif

int main(int argc, char *argv[])
{
    char *filename = NULL;
	int status;

    fprintf(stdout, "Content-type: text/html\n\n");

	if ( init_ds_env() )
		return 1;

	/*
	 * Get value of the "filename" variable.
	 */
	filename = ds_get_cgi_var("filename");
	if ( (NULL == filename) || (strlen(filename) < 1) ) {
		rpt_err( DS_UNDEFINED_VARIABLE, "filename", NULL, NULL );
		return 1;
	}

    ds_become_localuser (ds_get_config (DS_REAL_CONFIG));

#define NEWDIR_MODE 0755
	/* Attempt to detect up front if file cannot be written */
	status = -1;
	/* Attempt to create the backup directory */
	if ( 0 == ds_mkdir_p(filename, NEWDIR_MODE) ) {
		char foo[256];
		FILE *f;
		/* Now attempt to create a file there (the directory might
		   already have existed */
		sprintf( foo, "%s%c%s", filename, FILE_PATHSEP, "foo" );
		f = fopen(foo, "w");
		if ( NULL != f ) {
			status = 0;
			fclose( f );
			unlink( foo );
		}
	}
	if ( status ) {
		rpt_err( DS_CANNOT_CREATE_FILE, filename, NULL, NULL );
		return 1;
	}

    ds_send_status("backing up database ...");

	status = ds_db2bak( filename );	/* prints errors as needed */

	if ( !status ) {
		rpt_success("Success! The database has been backed up.");
		return 0;
	} else {
		rpt_err( status, filename, NULL,  NULL );
		return 1;
	}
}

/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * Converts the database into an ldif file.
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
    char *subtree = NULL;
	int status;
	FILE *f;

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

	/* Attempt to detect up front if file cannot be written */
	f = fopen(filename, "w");
	if ( NULL != f ) {
		fclose( f );
		unlink( filename );
	} else {
		rpt_err( DS_CANNOT_CREATE_FILE, filename, NULL, NULL );
		return 1;
	}

	/*
	 * Get value of the "subtree" variable.
	 */
	subtree = ds_get_cgi_var("subtree");

    ds_send_status("creating LDIF file ...");

	if ( (subtree != NULL) && (*subtree != 0) ) {
		char *escaped = ds_escape_for_shell( subtree );
		status = ds_db2ldif_subtree(filename, escaped);
		free( escaped );
	} else {
		status = ds_db2ldif(filename);	/* prints errors as needed */
	}

	if ( !status ) {
		rpt_success("Success! The database has been exported.");
		return 0;
	} else {
		rpt_err( status, filename, NULL,  NULL );
		return 1;
	}
}

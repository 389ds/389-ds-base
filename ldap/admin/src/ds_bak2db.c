/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * Restores a database.
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

int main(int argc, char *argv[])
{
    int isrunning;
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
		fprintf(stdout, "Environment variable filename not defined.\n");
		rpt_err( DS_UNDEFINED_VARIABLE, "filename", NULL, NULL );
		return 1;
	}

	/* Check if server is up */
    isrunning = ds_get_updown_status();

	/* Stop it, if so */
	if (isrunning != DS_SERVER_DOWN) {
		status = ds_bring_down_server();
		if(status != DS_SERVER_DOWN)  {
			rpt_err( DS_SERVER_MUST_BE_DOWN, filename, NULL, NULL );
			return 1;
		}
	}

    ds_send_status("restoring database ...");
	status = ds_bak2db(filename);

	if ( !status ) {
		rpt_success("Success! The database has been restored.");
		status = 0;
	} else {
		rpt_err( status, filename, NULL,  NULL );
		status = 1;
	}

	/* Restart the server if we brought it down */
	if (isrunning != DS_SERVER_DOWN) {
		if(ds_bring_up_server(1) != DS_SERVER_UP)  {
			ds_send_status( "An error occurred during startup" );
		}
	}
	return status;
}

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * ds_ldif2db.c:  Converts an ldif file into a database.
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
#ifdef XP_WIN32
#include <io.h>
#endif

int main(int argc, char *argv[])
{
    int isrunning;
    char *filename = NULL;
	char *saveconfig = NULL;
	int preserve;
	int status;

	setbuf(stdout, 0);
#ifdef DEBUG_CGI
	freopen("\\tmp\\stderr.out", "w", stderr);
#else
	dup2(fileno(stdout), fileno(stderr));
#endif /* DEBUG_CGI */
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

#ifdef DEBUG_CGI
	fprintf(stderr, "filename=%s\n", filename);
#endif /* DEBUG_CGI */

	/*
	 * Get value of the "saveconfig" variable.
	 */
	saveconfig = ds_get_cgi_var("saveconfig");
	preserve = ( (saveconfig == NULL) || !(strcmp(saveconfig,"true")) );

#ifdef DEBUG_CGI
	fprintf(stderr, "preserve=%d\n", preserve);
#endif /* DEBUG_CGI */

	/* Check if server is up */
    isrunning = ds_get_updown_status();

#ifdef DEBUG_CGI
	fprintf(stderr, "isrunning=%d\n", isrunning);
#endif /* DEBUG_CGI */

	/* Stop it, if so */
	if (isrunning != DS_SERVER_DOWN) {
		status = ds_bring_down_server();
#ifdef DEBUG_CGI
		fprintf(stderr, "status=%d\n", status);
#endif /* DEBUG_CGI */
		if(status != DS_SERVER_DOWN)  {
			rpt_err( DS_SERVER_MUST_BE_DOWN, filename, NULL, NULL );
			return 1;
		}
	}

    ds_send_status("creating database ...");
	if ( preserve )
		status = ds_ldif2db_preserve(filename);	/* prints errors as needed */
	else
		status = ds_ldif2db(filename);	/* prints errors as needed */

	if ( !status ) {
		rpt_success("Success! The database has been imported.");
		status = 0;
	} else {
		rpt_err( status, filename, NULL,  NULL );
		status = 1;
	}

	/* Restart the server if we brought it down */
	if (isrunning != DS_SERVER_DOWN) {
		int retval;
		if((retval=ds_bring_up_server(1)) != DS_SERVER_UP)  {
			ds_send_status( "An error occurred during startup" );
		}
	}
	return status;
}

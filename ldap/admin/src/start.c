/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * start.c:  Starts up the server.
 * 
 * DS changes: Anil Bhavnani
 * Removed all HTML output for DS 4.0: Rob Weltman
 * Mike McCool 
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "libadminutil/admutil.h"
#include "dsalib.h"
#include "init_ds_env.h"


static char buf[1024];

int main(int argc, char *argv[])
{
    int status = -1;
	char *instanceName = 0;

    fprintf(stdout, "Content-type: text/html\n\n");

	if ( init_ds_env() )
		return 0;

	status = ds_bring_up_server(1);
	if(status == DS_SERVER_UP)  {
		rpt_success("Success! The server has been started.");
		return 0;
	}  else  {
		rpt_err( status, "", NULL,  NULL );
		return 1;
	}
}

/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * shutdown.c:  Kills the server.
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

int main(int argc, char *argv[])
{
    int	status = -1;

    fprintf(stdout, "Content-type: text/html\n\n");

	if ( init_ds_env() )
		return 1;

	status = ds_bring_down_server();
	if(status == DS_SERVER_DOWN)  {
		rpt_success("Success! The server has been shut down.");
		return 0;
	}  else  {
		rpt_err( status, "", NULL,  NULL );
		return 1;
	}
}


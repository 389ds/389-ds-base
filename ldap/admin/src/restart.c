/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * restart.c:  Stops and the starts up the server.
 * 
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "libadminutil/admutil.h"
#include "dsalib.h"
#include "init_ds_env.h"

#ifdef XP_WIN32
  #define sleep(sec) Sleep(sec)
#endif

int main(int argc, char *argv[])
{
    int status = -1;

    fprintf(stdout, "Content-type: text/html\n\n");

	if ( init_ds_env() )
		return 1;

	if (DS_SERVER_UP == ds_get_updown_status()) {
		status = ds_bring_down_server();
		if(status != DS_SERVER_DOWN)  {
			rpt_err( status, "", NULL,  NULL );
			return 1;
		}
	}
	status = ds_bring_up_server(1);
	if(status == DS_SERVER_UP)  {
		rpt_success("Success! The server has been restarted.");
		return 0;
	}  else  {
		rpt_err( status, "", NULL,  NULL );
		return 1;
	}
}

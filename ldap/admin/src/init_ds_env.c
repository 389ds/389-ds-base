/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * Set up environment for CGIs.
 * 
 * Rob Weltman
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "libadminutil/admutil.h"
#include "libadminutil/distadm.h"
#include "init_ds_env.h"
#include "dsalib.h"

int init_ds_env()
{
    char *m = getenv("REQUEST_METHOD");
    char *qs              = NULL;
	int proceed = 0;
    int _ai = ADMUTIL_Init();

	if ( m != NULL ) {
		if( !strcmp(m, "GET") )  {
			qs = GET_QUERY_STRING();
			if ( qs && *qs ) {
				ds_get_begin(qs);
			}
			proceed = 1;
		} else if(!strcmp(m, "POST"))  {
			if (ds_post_begin(stdin)) {
				proceed = 0;
			} else {
				proceed = 1;
			}
		}
	}
 
	if(!proceed) {
		char msg[2000];
		sprintf(msg, "ErrorString: REQUEST_METHOD=%s,"
				"QUERY_STRING=%s\n", 
				(m == NULL) ? "<undefined>" : m, 
				(qs == NULL) ? "<undefined>" : qs);
		rpt_err( GENERAL_FAILURE,
				 msg,
				 "",
				 "" );
		return 1;
	}

	return 0;
}

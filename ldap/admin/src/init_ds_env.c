/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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

	(void)ADMUTIL_Init();
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
		PR_snprintf(msg, sizeof(msg), "ErrorString: REQUEST_METHOD=%s,"
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

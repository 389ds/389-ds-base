/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * addindex.c:  Creates one or more indexes for specified attributes
 * 
 * Rob Weltman
 */

#include <stdio.h>
#include <stdlib.h>
#include "libadminutil/admutil.h"
#include "dsalib.h"
#include "init_ds_env.h"
#include <string.h>

int main(int argc, char *argv[])
{
	int status;
	char *attributes;
	char *attrs;
	char **attrList;
	int nAttrs;
	char *nextAttr = NULL;
	char *backendName;


    fprintf(stdout, "Content-type: text/html\n\n");

	if ( init_ds_env() )
		return 1;

	/*
	 * Get value of the "attributes" variable.
	 */
	attributes = ds_get_cgi_var("attributes");
	if ( (NULL == attributes) || (strlen(attributes) < 1) ) {
		rpt_err( DS_UNDEFINED_VARIABLE, "attributes", NULL, NULL );
		return 1;
	}


	backendName = ds_get_cgi_var("backendID");
	if ( (NULL == backendName) || (strlen(backendName) < 1) ) {
		rpt_err( DS_UNDEFINED_VARIABLE, "backendName", NULL, NULL );
		return 1;
	}
	

	attrs = strdup( attributes );
	/* Allocate for worst possible case */
	attrList = (char **)malloc(sizeof(*attrList) * (strlen(attrs)+1));
	nAttrs = 0;
	/* strtok() is not MT safe, but it is okay to call here because this is a command line */
	attrList[nAttrs++] = strtok( attrs, " " );
	do {
		nextAttr = strtok( NULL, " " );
		attrList[nAttrs++] = nextAttr;
	} while( nextAttr != NULL );

    ds_send_status((nAttrs > 1) ? "Creating indexes ..." :
				   "Creating index ...");

	status = ds_addindex( attrList, backendName );

	if ( !status ) {
		rpt_success((nAttrs > 1) ? "Success! The indexes have been created." :
					"Success! The index has been created.");
		status = 0;
	} else {
		char msg[BIG_LINE];
		sprintf( msg,"[%s] %s", backendName, attributes);
		rpt_err( status, msg, NULL,  NULL );
		status = 1;
	}

	return status;
}

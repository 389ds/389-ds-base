/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * vlvindex.c:  Creates a VLV index for a given search
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
	char *backendNames = NULL;
	char *attributes = NULL;
	char *tmparg = NULL;
	char **attrList = NULL;
	char **backendList = NULL;
	int nItem = 0;
	char *nextItem = NULL;
	int i=0;

    fprintf(stdout, "Content-type: text/html\n\n");

	if ( init_ds_env() )
		return 1;

    ds_send_status("Creating vlv index ...");

	/*
	 * Get var's value 
	 */
	backendNames = ds_get_cgi_var("backendID");
	if ( (NULL == backendNames) || (strlen(backendNames) < 1) ) {
		rpt_err( DS_UNDEFINED_VARIABLE, "backendID", NULL, NULL );
		return 1;
	}

	attributes = ds_get_cgi_var("vlvTags");
	if ( (NULL == attributes) || (strlen(attributes) < 1) ) {
		rpt_err( DS_UNDEFINED_VARIABLE, "vlvTags", NULL, NULL );
		return 1;
	}	

	tmparg = strdup( attributes );
	nItem = 0;
	for(i=0 ; i < strlen(attributes) ; i++) {
		if ( tmparg[i] == ';' ) nItem++;
	}
	/* Allocate for worst possible case */
	attrList = (char **)malloc(sizeof(*attrList)  * (nItem + 2) );
	nItem = 0;
	/* strtok() is not MT safe, but it is okay to call here because this is a command line */
	attrList[nItem++] = strtok( tmparg, ";" );
	do {
		nextItem = strtok( NULL, ";" );
		attrList[nItem++] = nextItem;
	} while( nextItem != NULL );	

	tmparg = strdup( backendNames );
	nItem = 0;
	for(i=0;i<strlen(tmparg); i++) {
		if (  tmparg[i] == ';' ) nItem++;
	}
	backendList = (char **)malloc(sizeof(*backendList) * nItem + 2);
	nItem = 0;
	backendList[nItem++] = strtok( tmparg, ";" );
	do {
		nextItem = strtok( NULL, ";" );
		backendList[nItem++] = nextItem;
	} while( nextItem != NULL );	

	status = ds_vlvindex(backendList, attrList);

	if ( !status ) {
		rpt_success("Success! The index has been created.");
		status = 0;
	} else {
		rpt_err( status, backendList[0], NULL,  NULL );
		status = 1;
	}

	return status;
}

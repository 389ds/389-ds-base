/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
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

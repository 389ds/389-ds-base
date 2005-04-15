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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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
#include "nspr.h"

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
	if (attrs == NULL) {
		rpt_err(DS_MEMORY_ERROR, "Out of memory!", NULL, NULL);
		return 1;
	}

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
		PR_snprintf( msg, sizeof(msg), "[%s] %s", backendName, attributes);
		rpt_err( status, msg, NULL,  NULL );
		status = 1;
	}

	return status;
}

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

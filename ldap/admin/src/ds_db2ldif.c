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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * Converts the database into an ldif file.
 * 
 * Anil Bhavnani
 * Removed all HTML output for DS 4.0: Rob Weltman
 */

#include <stdio.h>
#include <stdlib.h>
#include "libadminutil/admutil.h"
#include "dsalib.h"
#include "init_ds_env.h"
#include <string.h>
#ifdef XP_UNIX
#include <unistd.h>
#endif

int main(int argc, char *argv[])
{
    char *filename = NULL;
    char *subtree = NULL;
	int status;
	FILE *f;

    fprintf(stdout, "Content-type: text/html\n\n");

	if ( init_ds_env() )
		return 1;

	/*
	 * Get value of the "filename" variable.
	 */
	filename = ds_get_cgi_var("filename");
	if ( (NULL == filename) || (strlen(filename) < 1) ) {
		rpt_err( DS_UNDEFINED_VARIABLE, "filename", NULL, NULL );
		return 1;
	}

    ds_become_localuser (ds_get_config (DS_REAL_CONFIG));

	/* Attempt to detect up front if file cannot be written */
	f = fopen(filename, "w");
	if ( NULL != f ) {
		fclose( f );
		unlink( filename );
	} else {
		rpt_err( DS_CANNOT_CREATE_FILE, filename, NULL, NULL );
		return 1;
	}

	/*
	 * Get value of the "subtree" variable.
	 */
	subtree = ds_get_cgi_var("subtree");

    ds_send_status("creating LDIF file ...");

	if ( (subtree != NULL) && (*subtree != 0) ) {
		char *escaped = ds_escape_for_shell( subtree );
		status = ds_db2ldif_subtree(filename, escaped);
		free( escaped );
	} else {
		status = ds_db2ldif(filename);	/* prints errors as needed */
	}

	if ( !status ) {
		rpt_success("Success! The database has been exported.");
		return 0;
	} else {
		rpt_err( status, filename, NULL,  NULL );
		return 1;
	}
}

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* 
 * List the database backup directories.
 * No HTML - this is for DS 4.0.
 *
 * Rob Weltman
 */

#include <stdio.h>
#include <stdlib.h>
#include "dsalib.h"

int main(int argc, char *argv[], char *envp[])
{
    char **bak_dirs;

    ds_become_localuser (ds_get_config (DS_REAL_CONFIG));

	/* Tell the receiver we are about to start sending data */
	fprintf(stdout, "\n");
	bak_dirs = ds_get_bak_dirs();
	if ( bak_dirs != NULL )	/* no error */ {
		char **cur_file = bak_dirs;
		while ( *cur_file != NULL ) {
			fprintf(stdout, "%s\n", *cur_file);
			cur_file++;
		}
    }

    ds_become_original();

    return 0;
}

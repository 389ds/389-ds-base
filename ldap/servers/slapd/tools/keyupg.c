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
 *
 * keyupg.c
 *
 * Upgrade the Key from Lite to normal ( only one way )
 *
 */


#include <stdio.h>
#include <string.h>
#ifdef hpux
#include <strings.h>
#endif /* hpux */
#ifdef LINUX
#include <unistd.h>	/* needed for getopt */
#endif
#if defined( _WIN32 )
#include <windows.h>
#include "proto-ntutil.h"
#endif
#include <stdlib.h>
#include "litekey.h"


#define BUFSIZE 800
#define FILE_PATHSEP '/'

int
main (int argc, char **argv )
{


	char		*keyfile = NULL;
	int		i, ikey, nkey;
	FILE		*fp = NULL;
	int		debug =0;


	while ( (i = getopt( argc, argv, "k:f:dh" )) != EOF ) {
		switch (i){
		   case 'f':
			keyfile = strdup( optarg );
			break;
		   case 'k':
			ikey = atoi ( optarg );
			break;
		   case 'd':
			debug = 1;
			break;
		}
	}

	if ( (NULL == keyfile ) || (!ikey)) {
		fprintf (stderr, "usage:%s -k key -f key_file_path\n", argv[0]);
                exit(1);

	}

	if (debug) printf ( "Key is :%d and file is in :%s\n", ikey, keyfile);

	if ( ! is_key_validNormalKey ( ikey )) {
		printf ( "Sorry. The input key is invalid\n" );
		exit (1);
	}


	nkey = generate_directory_key ( DS_NORMAL_TYPE );

	if ( (fp = fopen ( keyfile, "r+b")) == NULL ) {
		printf ("KEYUPG Error: Could not open the the key file:%s\n", keyfile );
		exit ( 1 );
	}
	fprintf (fp, "key: %d\n", nkey );
	fclose ( fp );

	printf ("Success: Your Directory Servers have been upgraded to the full version.\n");

	return 0;
}

/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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

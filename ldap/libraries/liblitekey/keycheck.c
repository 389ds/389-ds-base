/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * keycheck.c
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <litekey.h>

#define DS_NORMAL_MAGIC_KEY	119
#define	DS_LITE_MAGIC_KEY	326
#define FILE_PATHSEP '/'
#define BUFSIZE 800

/*
 * is_directory_lite
 *
 *	Checks if the directory server installation is a normal or a
 * 	lite.  The decision is made based on the key in the key file.
 *
 *   Input:
 *	char	*root;		Pathname to install root
 *   Returns:
 *		1  - yes, it's LITE server
 *		0  - No; it's fully paid (normal)  server.
 *
 */
int is_directory_lite( char *root)
{

	char	buf[40];
	char	*bufp = buf;
	FILE	*fp = NULL;
	int	key =0;
	char	*nsroot;
	char	pathname[BUFSIZE];

	return DS_NORMAL_TYPE; /* richm: no more lite mode in DS 5.0 */
#if 0 /* no more lite mode */
	/* There are 3 ways to determine if the server is FULL or LITE.
	 * 1) Use NETSITE_ROOT variable
	 * 2) Use the root path provided 
	 * 3) Look at the current directory
	 * 
	 * If all of them fails, then it's LITE.
	*/
	nsroot = getenv("NETSITE_ROOT");

	if ( (NULL == root) && (NULL == nsroot)) {
		/* case 3 */
		sprintf ( pathname, "slapd.key" );
	} else if (NULL == nsroot) {
		/* case 2 */
		sprintf ( pathname, "%s%cbin%cslapd%cserver%cslapd.key",
				root, FILE_PATHSEP,FILE_PATHSEP,
				FILE_PATHSEP, FILE_PATHSEP);
	} else {
		/* case 1 */
		sprintf ( pathname, "%s%cbin%cslapd%cserver%cslapd.key",
				nsroot, FILE_PATHSEP,FILE_PATHSEP,
				FILE_PATHSEP, FILE_PATHSEP);
	}


	/* First read from the key file */
	if ((fp = fopen ( pathname, "r")) == NULL )
		return DS_LITE_TYPE;

	if ( fgets(buf, 40, fp) == NULL)
		return DS_LITE_TYPE;

	fclose (fp );

	/* The key is in the format: "key:123456" */
	bufp +=4;
	key = atoi (  (const char *) bufp );

	/* Now we have the key. Determine which one it is */
	if (  0 == (key % DS_NORMAL_MAGIC_KEY))
		return DS_NORMAL_TYPE;
	else if ( 0 == (key % DS_LITE_MAGIC_KEY) )
		return DS_LITE_TYPE;

	/* By defualt, it's lite */
	return DS_LITE_TYPE;
#endif /* no more lite mode */
}

/*
 * generate_lite_key
 * 	Generate a key for the product that is being used.
 *
 *   Input:
 *	type  DS_NORMAL_TYPE	- Normal
 *	      DS_LITE_TYPE	- Lite
 *   Returns:
 *	a int key.
 *
 */
int generate_directory_key( int type)
{

	int key = 0;
	int val;

	val = rand();

	if (type == DS_NORMAL_TYPE )
		key = val * DS_NORMAL_MAGIC_KEY;
	else if (type == DS_LITE_TYPE )
		key = val * DS_LITE_MAGIC_KEY;

	return key;
}

/*
 * is_key_validNormalKey
 * 
 * Check if the key ia a valid normal key or not.
 */
int 
is_key_validNormalKey ( int key )
{

	if (key <= 0 ) return 0;

	if (0 == ( key % DS_NORMAL_MAGIC_KEY ))
		return 1;

	return 0;
}

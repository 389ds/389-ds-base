/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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
	return DS_NORMAL_TYPE; /* richm: no more lite mode in DS 5.0 */
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

	key = val * DS_NORMAL_MAGIC_KEY;

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
	return 1;
}

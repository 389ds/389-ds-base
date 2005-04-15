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

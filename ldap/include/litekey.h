/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * Defines function used to determine the type of DS based on the 
 * key.
 */
#ifndef _LITEKEY_H
#define _LITEKEY_H

#include <dirlite_strings.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_NORMAL_TYPE 0
#define DS_LITE_TYPE 1

int is_directory_lite ( char *path);
int generate_directory_key( int type);
int is_key_validNormalKey ( int key );

#ifdef __cplusplus
}
#endif

#endif /* _LITEKEY_H */

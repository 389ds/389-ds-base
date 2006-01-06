/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef _NAMETABLE_H
#define _NAMETABLE_H

/*
 * a NameTable is a block that just holds an array of (dynamically allocated)
 * strings.  you can read them all in from a file, and then fetch a specific
 * entry, or just a random one.
 */
typedef struct _nametable NameTable;

/* size that the array should grow by when it fills up */
#define NT_STEP		32


NameTable *nt_new(int capacity);
void nt_destroy(NameTable *nt);
int nt_push(NameTable *nt, char *s);
int nt_load(NameTable *nt, const char *filename);
int nt_save(NameTable *nt, const char *filename);
int nt_cis_check(NameTable *nt, const char *name);
char *nt_get(NameTable *nt, int entry);
char **nt_get_all(NameTable *nt );
char *nt_getrand(NameTable *nt);
int PR_GetLine(PRFileDesc *fd, char *s, unsigned int n);
int get_large_random_number();

#endif

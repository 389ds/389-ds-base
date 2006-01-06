/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef _SDATTABLE_H
#define _SDATTABLE_H

/*
 * a SDatTable is a block that just holds an array of (dynamically allocated)
 * dn & uid pair (dn might be empty).  you can read them all in from a file,
 * and then fetch a specific entry, or just a random one.
 */
typedef struct _sdattable SDatTable;

/* size that the array should grow by when it fills up */
#define SDT_STEP		32

SDatTable *sdt_new(int capacity);
void sdt_destroy(SDatTable *sdt);
int sdt_push(SDatTable *sdt, char *dn, char *uid);
int sdt_load(SDatTable *sdt, const char *filename);
int sdt_save(SDatTable *sdt, const char *filename);
int sdt_cis_check(SDatTable *sdt, const char *name);
char *sdt_dn_get(SDatTable *sdt, int entry);
void sdt_dn_set(SDatTable *sdt, int entry, char *dn);
char *sdt_uid_get(SDatTable *sdt, int entry);
int sdt_getrand(SDatTable *sdt);
int sdt_getlen(SDatTable *sdt);

#endif

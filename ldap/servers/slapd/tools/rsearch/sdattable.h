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
 * Copyright (C) 2006 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


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

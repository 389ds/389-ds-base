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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nspr.h"
#include "nametable.h"
#include "sdattable.h"


struct _sdattable {
    char **dns;
    char **uids;
    PRUint32 capacity;
    PRUint32 size;
};

/* new searchdata table */
SDatTable *sdt_new(int capacity)
{
    SDatTable *sdt = (SDatTable *)malloc(sizeof(SDatTable));
   
    if (!sdt) return NULL;
    if (capacity > 0) {
        sdt->dns = (char **)malloc(sizeof(char *) * capacity);
        if (! sdt->dns) {
    	    free(sdt);
    	    return NULL;
        }
        sdt->uids = (char **)malloc(sizeof(char *) * capacity);
        if (! sdt->uids) {
    	    free(sdt->dns);
    	    free(sdt);
    	    return NULL;
        }
    } else {
        sdt->dns = NULL;
        sdt->uids = NULL;
    }
    sdt->capacity = capacity;
    sdt->size = 0;
    return sdt;
}

/* destroy searchdata table */
void sdt_destroy(SDatTable *sdt)
{
    int i;

    if (sdt->size) {
	for (i = 0; i < sdt->size; i++) {
	    if (sdt->dns[i])
	        free(sdt->dns[i]);
	    if (sdt->uids[i])
	        free(sdt->uids[i]);
	}
    }
    if (sdt->dns)
        free(sdt->dns);
    if (sdt->uids)
        free(sdt->uids);
    free(sdt);
}

/* push a string into the searchdata table */
int sdt_push(SDatTable *sdt, char *dn, char *uid)
{
    char **sddns, **sddns0;
    char **sduids;

    if (!dn && !uid)
	return sdt->size;

    if (sdt->size >= sdt->capacity) {
	/* expando! */
	sdt->capacity += SDT_STEP;
	sddns = (char **)realloc(sdt->dns, sizeof(char *) * sdt->capacity);
	if (!sddns) return 0;
	sddns0 = sdt->dns;
	sdt->dns = sddns;
	sduids = (char **)realloc(sdt->uids, sizeof(char *) * sdt->capacity);
	if (!sduids) {
	    sdt->dns = sddns0;	/* restore */
	    return 0;
	}
	sdt->uids = sduids;
    }

    sdt->dns[sdt->size] = dn;	/* might be null */
    sdt->uids[sdt->size] = uid;	/* never be null */
    return ++sdt->size;
}

/* push the contents of a file into the sdt, one line per entry */
int sdt_load(SDatTable *sdt, const char *filename)
{
    PRFileDesc *fd;

    fd = PR_Open(filename, PR_RDONLY, 0);
    if (!fd) return 0;

    while (PR_Available(fd) > 0) {
	int rval;
	char temp[256];
	char *dn = NULL;
	char *uid = NULL;
	while (!(rval = PR_GetLine(fd, temp, 256))) {
	    char *p;
	    if (!strncasecmp(temp, "dn:", 3)) {
		for (p = temp + 4; *p == ' ' || *p == '\t'; p++) ;
	        dn = strdup(p);
	        if (!dn) break;
	    } else if (!strncasecmp(temp, "uid:", 4)) {
		for (p = temp + 5; *p == ' ' || *p == '\t'; p++) ;
	        uid = strdup(p);
	        if (!uid) break;
	    }
	    if (uid) {	/* dn should come earlier than uid */
	        if (!sdt_push(sdt, dn, uid)) goto out;
		break;
	    }
	}
	if (rval) break;	/* PR_GetLine failed */
    }
out:
    PR_Close(fd);
    return sdt->size;
}

/* write a searchdata table out into a file */
int sdt_save(SDatTable *sdt, const char *filename)
{
    PRFileDesc *fd;
    int i;

    fd = PR_Open(filename, PR_WRONLY|PR_CREATE_FILE, 0644);
    if (!fd) return 0;

    for (i = 0; i < sdt->size; i++) {
	if (sdt->dns[i]) {
	    PR_Write(fd, "dn: ", 4);
	    PR_Write(fd, sdt->dns[i], strlen(sdt->dns[i]));
	    PR_Write(fd, "\n", 1);
	}
	if (sdt->dns[i]) {
	    PR_Write(fd, "uid: ", 5);
	    PR_Write(fd, sdt->uids[i], strlen(sdt->uids[i]));
	    PR_Write(fd, "\n", 1);
	}
    }
    PR_Close(fd);
    return 1;
}

/* painstakingly determine if a given entry is already in the list */
int sdt_cis_check(SDatTable *sdt, const char *name)
{
    int i;
    
    for (i = 0; i < sdt->size; i++) {
	if (strcasecmp(sdt->dns[i], name) == 0)
	    return 1;
	if (strcasecmp(sdt->uids[i], name) == 0)
	    return 1;
    }
    return 0;
}

/* select a specific entry */
char *sdt_dn_get(SDatTable *sdt, int entry)
{
    return sdt->dns[entry];
}

void sdt_dn_set(SDatTable *sdt, int entry, char *dn)
{
    sdt->dns[entry] = strdup(dn);
}

char *sdt_uid_get(SDatTable *sdt, int entry)
{
    return sdt->uids[entry];
}

int sdt_getrand(SDatTable *sdt)
{
    if (! sdt->size) return -1;
    /* FIXME: rand() on NT will never return a number >32k */
    return get_large_random_number() % sdt->size;
}

int sdt_getlen(SDatTable *sdt)
{
    return sdt->size;
}

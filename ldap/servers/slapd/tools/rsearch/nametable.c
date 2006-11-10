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


struct _nametable {
    char **data;
    PRUint32 capacity;
    PRUint32 size;
};

int get_large_random_number()
{
#ifdef _WIN32
	return rand();
#else
	return random();
#endif
}


/*
 * replacement for fgets
 * This isn't like the real fgets.  It fills in 's' but strips off any
 * trailing linefeed character(s).  The return value is 0 if it went
 * okay.
 */
int PR_GetLine(PRFileDesc *fd, char *s, unsigned int n)
{
    PRInt32 start, newstart;
    int x;
    char *p;

    /* grab current location in file */
    start = PR_Seek(fd, 0, PR_SEEK_CUR);
    x = PR_Read(fd, s, n-1);
    if (x <= 0) return 1;   /* EOF or other error */
    s[x] = 0;
    p = strchr(s, '\n');
    if (p == NULL) p = strchr(s, '\r');
    if (p == NULL) {
        /* assume there was one anyway */
        return 0;
    }
    *p = 0;
    newstart = start+strlen(s)+1;
    if ((p != s) && (*(p-1) == '\r')) *(p-1) = 0;
    PR_Seek(fd, newstart, PR_SEEK_SET);
    return 0;
}

/* new nametable */
NameTable *nt_new(int capacity)
{
    NameTable *nt = (NameTable *)malloc(sizeof(NameTable));
   
    if (!nt) return NULL;
    if (capacity > 0) {
        nt->data = (char **)malloc(sizeof(char *) * capacity);
        if (! nt->data) {
    	    free(nt);
    	    return NULL;
        }
    } else {
        nt->data = NULL;
    }
    nt->capacity = capacity;
    nt->size = 0;
    return nt;
}

/* destroy nametable */
void nt_destroy(NameTable *nt)
{
    int i;

    if (nt->size) {
	for (i = 0; i < nt->size; i++)
	    free(nt->data[i]);
    }
    free(nt->data);
    free(nt);
}

/* push a string into the nametable */
int nt_push(NameTable *nt, char *s)
{
    char **ndata;

    if (nt->size >= nt->capacity) {
	/* expando! */
	nt->capacity += NT_STEP;
	ndata = (char **)realloc(nt->data, sizeof(char *) * nt->capacity);
	if (!ndata) return 0;
	nt->data = ndata;
    }
    nt->data[nt->size++] = s;
    return nt->size;
}

/* push the contents of a file into the nt, one line per entry */
int nt_load(NameTable *nt, const char *filename)
{
    PRFileDesc *fd;

    fd = PR_Open(filename, PR_RDONLY, 0);
    if (!fd) return 0;

    while (PR_Available(fd) > 0) {
	char temp[256], *s;
	if (PR_GetLine(fd, temp, 256)) break;
	s = strdup(temp);
	if (!s) break;
	if (!nt_push(nt, s)) break;
    }
    PR_Close(fd);
    return nt->size;
}

/* write a nametable out into a file */
int nt_save(NameTable *nt, const char *filename)
{
    PRFileDesc *fd;
    int i;

    fd = PR_Open(filename, PR_WRONLY|PR_CREATE_FILE, 0644);
    if (!fd) return 0;

    for (i = 0; i < nt->size; i++) {
	PR_Write(fd, nt->data[i], strlen(nt->data[i]));
	PR_Write(fd, "\n", 1);
    }
    PR_Close(fd);
    return 1;
}

/* painstakingly determine if a given entry is already in the list */
int nt_cis_check(NameTable *nt, const char *name)
{
    int i;
    
    for (i = 0; i < nt->size; i++)
	if (strcasecmp(nt->data[i], name) == 0)
	    return 1;
    return 0;
}

/* select a specific entry */
char *nt_get(NameTable *nt, int entry)
{
    return nt->data[entry];
}

char *nt_getrand(NameTable *nt)
{
    if (! nt->size) return NULL;
    /* FIXME: rand() on NT will never return a number >32k */
    return nt->data[get_large_random_number() % nt->size];
}

/* get all entries */
char **nt_get_all(NameTable *nt )
{
	return nt->data ;
}

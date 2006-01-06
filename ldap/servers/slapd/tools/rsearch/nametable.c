/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

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

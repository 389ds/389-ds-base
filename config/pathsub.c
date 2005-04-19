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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
** Pathname subroutines.
**
** Brendan Eich, 8/29/95
*/
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "pathsub.h"
#ifdef USE_REENTRANT_LIBC
#include <libc_r.h>
#endif /* USE_REENTRANT_LIBC */

char *program;

void
fail(char *format, ...)
{
    int error;
    va_list ap;

#ifdef USE_REENTRANT_LIBC
    R_STRERROR_INIT_R();
#endif

    error = errno;
    fprintf(stderr, "%s: ", program);
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    if (error)

#ifdef USE_REENTRANT_LIBC
    R_STRERROR_R(errno);
	fprintf(stderr, ": %s", r_strerror_r);
#else
	fprintf(stderr, ": %s", strerror(errno));
#endif

    putc('\n', stderr);
    exit(1);
}

char *
getcomponent(char *path, char *name)
{
    if (*path == '\0')
	return 0;
    if (*path == '/') {
	*name++ = '/';
    } else {
	do {
	    *name++ = *path++;
	} while (*path != '/' && *path != '\0');
    }
    *name = '\0';
    while (*path == '/')
	path++;
    return path;
}

#ifdef UNIXWARE
/* Sigh.  The static buffer in Unixware's readdir is too small. */
struct dirent * readdir(DIR *d)
{
        static struct dirent *buf = NULL;
#define MAX_PATH_LEN 1024


        if(buf == NULL)
                buf = (struct dirent *) malloc(sizeof(struct dirent) + MAX_PATH_LEN)
;
        return(readdir_r(d, buf));
}
#endif

char *
ino2name(ino_t ino, char *dir)
{
    DIR *dp;
    struct dirent *ep;
    char *name;

    dp = opendir("..");
    if (!dp)
	fail("cannot read parent directory");
    for (;;) {
	if (!(ep = readdir(dp)))
	    fail("cannot find current directory");
	if (ep->d_ino == ino)
	    break;
    }
    name = xstrdup(ep->d_name);
    closedir(dp);
    return name;
}

void *
xmalloc(size_t size)
{
    void *p = malloc(size);
    if (!p)
	fail("cannot allocate %u bytes", size);
    return p;
}

char *
xstrdup(char *s)
{
    return strcpy(xmalloc(strlen(s) + 1), s);
}

char *
xbasename(char *path)
{
    char *cp;

    while ((cp = strrchr(path, '/')) && cp[1] == '\0')
	*cp = '\0';
    if (!cp) return path;
    return cp + 1;
}

void
xchdir(char *dir)
{
    if (chdir(dir) < 0)
	fail("cannot change directory to %s", dir);
}

int
relatepaths(char *from, char *to, char *outpath)
{
    char *cp, *cp2;
    int len;
    char buf[NAME_MAX];

    assert(*from == '/' && *to == '/');
    for (cp = to, cp2 = from; *cp == *cp2; cp++, cp2++)
	if (*cp == '\0')
	    break;
    while (cp[-1] != '/')
	cp--, cp2--;
    if (cp - 1 == to) {
	/* closest common ancestor is /, so use full pathname */
	len = strlen(strcpy(outpath, to));
	if (outpath[len] != '/') {
	    outpath[len++] = '/';
	    outpath[len] = '\0';
	}
    } else {
	len = 0;
	while ((cp2 = getcomponent(cp2, buf)) != 0) {
	    strcpy(outpath + len, "../");
	    len += 3;
	}
	while ((cp = getcomponent(cp, buf)) != 0) {
	    sprintf(outpath + len, "%s/", buf);
	    len += strlen(outpath + len);
	}
    }
    return len;
}

void
reversepath(char *inpath, char *name, int len, char *outpath)
{
    char *cp, *cp2;
    char buf[NAME_MAX];
    struct stat sb;

    cp = strcpy(outpath + PATH_MAX - (len + 1), name);
    cp2 = inpath;
    while ((cp2 = getcomponent(cp2, buf)) != 0) {
	if (strcmp(buf, ".") == 0)
	    continue;
	if (strcmp(buf, "..") == 0) {
	    if (stat(".", &sb) < 0)
		fail("cannot stat current directory");
	    name = ino2name(sb.st_ino, "..");
	    len = strlen(name);
	    cp -= len + 1;
	    strcpy(cp, name);
	    cp[len] = '/';
	    free(name);
	    xchdir("..");
	} else {
	    cp -= 3;
	    strncpy(cp, "../", 3);
	    xchdir(buf);
	}
    }
    strcpy(outpath, cp);
}

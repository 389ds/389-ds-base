/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/***********************************************************************
** NAME
**  getfilelist.c
**
** DESCRIPTION
**
**
** AUTHOR
**  Rich Megginson <richm@netscape.com>
**
***********************************************************************/

/***********************************************************************
** Includes
***********************************************************************/

#include "prio.h"
#include "slap.h"
#include "avl.h"

struct data_wrapper
{
    char **list;
    int n;
    int max;
    const char *dirname;
};

struct path_wrapper
{
    char *path;
    char *filename;
    int order;
};

static int
path_wrapper_cmp(struct path_wrapper *p1, struct path_wrapper *p2)
{
    if (p1->order < p2->order) {
        /* p1 is "earlier" so put it first */
        return -1;
    } else if (p1->order > p2->order) {
        return 1;
    }
    return strcmp(p1->filename, p2->filename);
}

static int
path_wrapper_free(caddr_t data)
{
    struct path_wrapper *p = (struct path_wrapper *)data;
    if (p != NULL) {
        slapi_ch_free_string(&(p->path));
        slapi_ch_free_string(&(p->filename));
        slapi_ch_free((void **)&p);
    }
    return 0;
}

static int
add_file_to_list(caddr_t data, caddr_t arg)
{
    struct data_wrapper *dw = (struct data_wrapper *)arg;
    struct path_wrapper *pw = (struct path_wrapper *)data;
    if (dw) {
        /* max is number of entries; the range of n is 0 - max-1 */
        PR_ASSERT(dw->n <= dw->max);
        PR_ASSERT(dw->list);
        PR_ASSERT(data);
        /* this strdup is free'd by free_filelist */
        dw->list[dw->n++] = slapi_ch_strdup(pw->path);
        return 0;
    }

    return -1;
}

static int
file_is_type_x(const char *dirname, const char *filename, PRFileType x)
{
    struct PRFileInfo64 inf;
    int status = 0;
    char *fullpath = slapi_ch_smprintf("%s/%s", dirname, filename);
    if (PR_SUCCESS == PR_GetFileInfo64(fullpath, &inf) &&
        inf.type == x)
        status = 1;

    slapi_ch_free_string(&fullpath);

    return status;
}

/* return true if the given path and file corresponds to a directory */
static int
is_a_dir(const char *dirname, const char *filename)
{
    return file_is_type_x(dirname, filename, PR_FILE_DIRECTORY);
}

/* return true if the given path and file corresponds to a regular file */
static int
is_a_file(const char *dirname, const char *filename)
{
    return file_is_type_x(dirname, filename, PR_FILE_FILE);
}

static int
matches(const char *filename, const char *pattern)
{
    Slapi_Regex *re = NULL;
    int match = 0;
    const char *error = NULL;

    if (!pattern)
        return 1; /* null pattern matches everything */

    /* Compile the pattern */
    re = slapi_re_comp(pattern, &error);
    if (re) {
        /* Matches the compiled pattern against the filename */
        match = slapi_re_exec_nt(re, filename);
        slapi_re_free(re);
    }

    return match;
}

/**
 * getfilelist will return a list of all files and directories in the
 * given directory matching the given pattern.  If dirname is NULL, the
 * current directory "." will be used.  If the pattern is NULL, all files
 * and directories will be returned.  The additional integer arguments
 * control which files and directories are selected.  The default value
 * for all of them is 0, which will not return hidden files (e.g. files
 * beginning with . on unix), but will return both files and directories
 * If nofiles is non-zero, only directory names will be returned.  If
 * nodirs is non-zero, only filenames will be returned.
 * The pattern is a grep style regular expression, not a shell or command
 * interpreter style regular expression.  For example, to get all files ending
 * in .ldif, use ".*\\.ldif" instead of "*.ldif"
 * The return value is a NULL terminated array of names.
 *
 * This takes an array of directories is order of lowest  to highest pref to
 * search. So if you have:
 * a/
 *   00file
 *   10file
 * b/
 *   05file
 *   15file
 * These will be order in preference:
 * 00file, 10file, 05file, 15file
 */
char **
get_filelist(
    const char **dirnames, /* list of directory paths; if NULL, uses "." */
    const char *pattern,   /* grep (not shell!) file pattern regex */
    int hiddenfiles,       /* if true, return hidden files and directories too */
    int nofiles,           /* if true, do not return files */
    int nodirs,            /* if true, do not return directories */
    int dirnames_size      /* Size of the dirnames array */
    )
{
    Avlnode *filetree = 0;
    PRDir *dirptr = 0;
    PRDirEntry *dirent = 0;
    PRDirFlags dirflags = PR_SKIP_BOTH & PR_SKIP_HIDDEN;
    char **retval = NULL;
    const char *dirname = NULL;
    int num = 0;
    int i = 0;
    struct data_wrapper dw;
    struct path_wrapper *pw_ptr = NULL;


    if (hiddenfiles) {
        dirflags = PR_SKIP_BOTH;
    }

    for (i = 0; i < dirnames_size; i++) {
        dirname = dirnames[i];
        /* wibrown - I feel this is a bad default */
        if (dirname == NULL) {
            dirname = ".";
        }
        if (!(dirptr = PR_OpenDir(dirname))) {
            continue;
        }
        /* read the directory entries into an ascii sorted avl tree */
        for (dirent = PR_ReadDir(dirptr, dirflags); dirent;
             dirent = PR_ReadDir(dirptr, dirflags)) {

            if (nofiles && is_a_file(dirname, dirent->name))
                continue;

            if (nodirs && is_a_dir(dirname, dirent->name))
                continue;

            if (1 == matches(dirent->name, pattern)) {
                /* this strdup is free'd by path_wrapper_free */
                pw_ptr = (struct path_wrapper *)slapi_ch_malloc(sizeof(struct path_wrapper));
                pw_ptr->path = slapi_ch_smprintf("%s/%s", dirname, dirent->name);
                pw_ptr->filename = slapi_ch_strdup(dirent->name);
                pw_ptr->order = i;
                avl_insert(&filetree, pw_ptr, path_wrapper_cmp, 0);
                num++;
            }
        }
        PR_CloseDir(dirptr);
    }

    /* allocate space for the list */
    if (num > 0) {
        retval = (char **)slapi_ch_calloc(num + 1, sizeof(char *));

        /* traverse the avl tree and copy the filenames into the list */
        dw.list = retval;
        dw.n = 0;
        dw.max = num;
        dw.dirname = dirname;
        (void)avl_apply(filetree, add_file_to_list, &dw, -1, AVL_INORDER);
        retval[num] = 0; /* set last entry to null */

        /* delete the avl tree and all its data */
    }

    avl_free(filetree, path_wrapper_free);

    return retval;
}


void
free_filelist(char **filelist)
{
    int ii;
    for (ii = 0; filelist && filelist[ii]; ++ii) {
        slapi_ch_free_string(&(filelist[ii]));
    }

    slapi_ch_free((void **)&filelist);
}

/**
 * Returns a list of files in order of "priority" where priority is defined
 * as:
 * The first two characters in the filename are digits representing a
 * number from 00 to 99.  The lower the number the higher the
 * priority.  For example, 00 is in the list before 01, and 99 is the
 * last item in the list.  The ordering of files with the same
 * priority cannot be guaranteed.  The pattern is the grep style
 * regular expression of filenames to match which is applied to the
 * end of the string.  If you are a Solaris person, you may recognize
 * this as the rules for init level initialization using shell scripts
 * under /etc/rcX.d/
 */
char **
get_priority_filelist(const char **directories, const char *pattern, int dirnames_size)
{
    char *basepattern = "^[0-9][0-9]";
    char *genericpattern = ".*"; /* used if pattern is null */
    char *bigpattern = 0;
    char **retval = 0;

    if (!pattern) {
        pattern = genericpattern;
    }

    bigpattern = slapi_ch_smprintf("%s%s", basepattern, pattern);

    retval = get_filelist(directories, bigpattern, 0, 0, 1, dirnames_size);

    slapi_ch_free_string(&bigpattern);

    return retval;
}

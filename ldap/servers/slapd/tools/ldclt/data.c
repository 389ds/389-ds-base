#ident "ldclt @(#)data.c    1.8 01/03/23"

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/*
    FILE :        data.c
    AUTHOR :    Jean-Luc SCHWING
    VERSION :       1.0
    DATE :        11 January 1999
    DESCRIPTION :
            This file implements the management of the data that
            are manipulated by ldclt.
            It is targeted to contain all the functions needed for
            the images, etc...
*/

#include <stdio.h>                              /* printf(), etc... */
#include <stdlib.h>                             /* realloc(), etc... */
#include <string.h>                             /* strlen(), etc... */
#include <errno.h>    /* errno, etc... */       /*JLS 06-03-00*/
#include <sys/types.h>                          /* Misc types... */
#include <sys/stat.h>                           /* stat(), etc... */
#include <fcntl.h>                              /* open(), etc... */
#include <lber.h>                               /* ldap C-API BER declarations */
#include <ldap.h>                               /* ldap C-API declarations */
#include <unistd.h>                             /* close(), etc... */
#include <dirent.h>                             /* opendir(), etc... */
#include <pthread.h>                            /* pthreads(), etc... */
#include <sys/mman.h>                           /* mmap(), etc... */
#include "port.h" /* Portability definitions */ /*JLS 28-11-00*/
#include "ldclt.h"                              /* This tool's include file */


/* ****************************************************************************
    FUNCTION :    getExtend
    PURPOSE :    Get the extension of the given string, i.e. the part
            that is after the last '.'
    INPUT :        str    = string to process
    OUTPUT :    None.
    RETURN :    The extension.
    DESCRIPTION :
 *****************************************************************************/
char *
getExtend(
    char *str)
{
    int i;
    for (i = strlen(str) - 1; (i >= 0) && (str[i] != '.'); i--)
        ;
    return (&(str[i + 1]));
}


/* ****************************************************************************
    FUNCTION :    loadImages
    PURPOSE :    Load the images from the given directory.
    INPUT :        dirpath    = directory where the images are located.
    OUTPUT :    None.
    RETURN :    -1 if error, 0 else.
    DESCRIPTION :
 *****************************************************************************/
int
loadImages(
    char *dirpath)
{
    DIR *dirp = NULL;       /* Directory data */
    struct dirent *direntp; /* Directory entry */
    char *fileName;         /* As read from the system */
    char name[1024];        /* To build the full path */
    struct stat stat_buf;   /* To read the image size */
    int fd = -1;            /* To open the image */
    int ret;                /* Return value */
    int rc = 0;

    /*
   * Initialization
   */
    mctx.images = NULL;
    mctx.imagesNb = 0;
    mctx.imagesLast = -1;

    if ((ret = ldclt_mutex_init(&(mctx.imagesLast_mutex))) != 0) {
        fprintf(stderr, "ldclt: %s\n", strerror(ret));
        fprintf(stderr, "Error: cannot initiate imagesLast_mutex\n");
        fflush(stderr);
        rc = -1;
        goto exit;
    }

    /*
   * Open the directory
   */
    dirp = opendir(dirpath);
    if (dirp == NULL) {
        perror(dirpath);
        fprintf(stderr, "ldlct: cannot load images from %s\n", dirpath);
        fprintf(stderr, "ldclt: try using -e imagesdir=path\n"); /*JLS 06-03-01*/
        fflush(stderr);
        rc = -1;
        goto exit;
    }

    /*
   * Process the directory.
   * We will only accept the .jpg files, as stated by the RFC.
   */
    while ((direntp = readdir(dirp)) != NULL) {
        fileName = direntp->d_name;
        if (!strcmp(getExtend(fileName), "jpg")) {
            /*
       * Allocate a new image, and initiates with its name.
       */
            mctx.imagesNb++;
            mctx.images =
                (image *)realloc(mctx.images, mctx.imagesNb * sizeof(image));
            if (mctx.images == NULL) /*JLS 06-03-00*/
            {                        /*JLS 06-03-00*/
                printf("Error: cannot realloc(mctx.images), error=%d (%s)\n",
                       errno, strerror(errno)); /*JLS 06-03-00*/
                rc = -1;
                goto exit;
            } /*JLS 06-03-00*/
            mctx.images[mctx.imagesNb - 1].name =
                (char *)malloc(strlen(fileName) + 1);
            if (mctx.images[mctx.imagesNb - 1].name == NULL) /*JLS 06-03-00*/
            {                                                /*JLS 06-03-00*/
                printf("Error: cannot malloc(mctx.images[%d]).name, error=%d (%s)\n",
                       mctx.imagesNb - 1, errno, strerror(errno)); /*JLS 06-03-00*/
                rc = -1;
                goto exit;
            } /*JLS 06-03-00*/
            strcpy(mctx.images[mctx.imagesNb - 1].name, fileName);

            /*
       * Read the image size
       */
            snprintf(name, sizeof(name), "%s/%s", dirpath, fileName);
            name[sizeof(name) - 1] = '\0';

            /*
       * Open the image
       */
            fd = open(name, O_RDONLY);
            if (fd < 0) {
                perror(name);
                fprintf(stderr, "Cannot open(%s)\n", name);
                fflush(stderr);
                rc = -1;
                goto exit;
            }

            if (fstat(fd, &stat_buf) < 0) {
                perror(name);
                fprintf(stderr, "Cannot stat(%s)\n", name);
                fflush(stderr);
                rc = -1;
                goto exit;
            }
            mctx.images[mctx.imagesNb - 1].length = stat_buf.st_size;

            /*
       * mmap() the image
       */
            mctx.images[mctx.imagesNb - 1].data = mmap(0, stat_buf.st_size,
                                                       PROT_READ, MAP_SHARED, fd, 0);
            if (mctx.images[mctx.imagesNb - 1].data == (char *)MAP_FAILED) {
                perror(name);
                fprintf(stderr, "Cannot mmap(%s)\n", name);
                fflush(stderr);
                rc = -1;
                goto exit;
            }

            /*
       * Close the image. The mmap() will remain available, and this
       * close() will save file descriptors.
       */
            if (close(fd) < 0) {
                perror(name);
                fprintf(stderr, "Cannot close(%s)\n", name);
                fflush(stderr);
                rc = -1;
                goto exit;
            } else {
                fd = -1;
            }
        }
    } /* while ((direntp = readdir (dirp)) != NULL) */
exit:
    /*
   * Close the directory
   */
    if (dirp && closedir(dirp) < 0) {
        perror(dirpath);
        fprintf(stderr, "Cannot closedir(%s)\n", dirpath);
        fflush(stderr);
        rc = -1;
    }


    /*
   * Normal end
   */
    if (fd != -1)
        close(fd);

    return rc;
}


/* ****************************************************************************
    FUNCTION :    getImage
    PURPOSE :    Add a random image to the given attribute.
    INPUT :        None.
    OUTPUT :    attribute    = the attribute where the image should
                      be added.
    RETURN :    -1 if error, 0 else.
    DESCRIPTION :
 *****************************************************************************/
int
getImage(
    LDAPMod *attr)
{
    int imageNumber; /* The image we will select */
    int ret;         /* Return value */

    /*
     * Select the next image
     */
    if ((ret = ldclt_mutex_lock(&(mctx.imagesLast_mutex))) != 0) /*JLS 29-11-00*/
    {
        fprintf(stderr,
                "Cannot mutex_lock(imagesLast_mutex), error=%d (%s)\n",
                ret, strerror(ret));
        fflush(stderr);
        return (-1);
    }
    mctx.imagesLast++;
    if (mctx.imagesLast == mctx.imagesNb)
        mctx.imagesLast = 0;
    imageNumber = mctx.imagesLast;
    if ((ret = ldclt_mutex_unlock(&(mctx.imagesLast_mutex))) != 0) {
        fprintf(stderr,
                "Cannot mutex_unlock(imagesLast_mutex), error=%d (%s)\n",
                ret, strerror(ret));
        fflush(stderr);
        return (-1);
    }

    /*
     * Create the data structure required
     */
    attr->mod_bvalues = (struct berval **)
        malloc(2 * sizeof(struct berval *));
    if (attr->mod_bvalues == NULL) /*JLS 06-03-00*/
    {                                   /*JLS 06-03-00*/
        printf("Error: cannot malloc(attribute->mod_bvalues), error=%d (%s)\n",
               errno, strerror(errno)); /*JLS 06-03-00*/
        return (-1);                    /*JLS 06-03-00*/
    }                                   /*JLS 06-03-00*/
    attr->mod_bvalues[0] = (struct berval *)malloc(sizeof(struct berval));
    if (attr->mod_bvalues[0] == NULL) /*JLS 06-03-00*/
    {                                      /*JLS 06-03-00*/
        printf("Error: cannot malloc(attribute->mod_bvalues[0]), error=%d (%s)\n",
               errno, strerror(errno)); /*JLS 06-03-00*/
        return (-1);                    /*JLS 06-03-00*/
    }                                   /*JLS 06-03-00*/
    attr->mod_bvalues[1] = NULL;

    /*
     * Fill the bvalue with the image data
     */
    attr->mod_bvalues[0]->bv_len = mctx.images[imageNumber].length;
    attr->mod_bvalues[0]->bv_val = mctx.images[imageNumber].data;

    /*
     * Normal end
     */
    return (0);
}


/* ****************************************************************************
    FUNCTION :    loadDataListFile
    PURPOSE :    Load the data list file given in argument.
    INPUT :        dlf->fname    = file to process
    OUTPUT :    dlf        = file read
    RETURN :    -1 if error, 0 else.
    DESCRIPTION :
 *****************************************************************************/
int
loadDataListFile(
    data_list_file *dlf)
{
    FILE *ifile;           /* Input file */
    char line[MAX_FILTER]; /* To read ifile */

    /*
   * Open the file
   */
    ifile = fopen(dlf->fname, "r");
    if (ifile == NULL) {
        perror(dlf->fname);
        fprintf(stderr, "Error: cannot open file \"%s\"\n", dlf->fname);
        return (-1);
    }

    /*
   * Count the entries.
   * Allocate the array.
   * Rewind the file.
   */
    for (dlf->strNb = 0; fgets(line, MAX_FILTER, ifile) != NULL; dlf->strNb++)
        ;
    dlf->str = (char **)malloc(dlf->strNb * sizeof(char *));
    if (fseek(ifile, 0, SEEK_SET) != 0) {
        perror(dlf->fname);
        fprintf(stderr, "Error: cannot rewind file \"%s\"\n", dlf->fname);
        fclose(ifile);
        return (-1);
    }

    /*
   * Read all the entries from this file
   */
    dlf->strNb = 0;
    while (fgets(line, MAX_FILTER, ifile) != NULL) {
        if ((strlen(line) > 0) && (line[strlen(line) - 1] == '\n'))
            line[strlen(line) - 1] = '\0';
        dlf->str[dlf->strNb] = strdup(line);
        dlf->strNb++;
    }

    /*
   * Close the file
   */
    if (fclose(ifile) != 0) {
        perror(dlf->fname);
        fprintf(stderr, "Error: cannot fclose file \"%s\"\n", dlf->fname);
        return (-1);
    }
    return (0);
}


/* ****************************************************************************
    FUNCTION :    dataListFile
    PURPOSE :    Find the given data_list_file either in the list of
            files already loaded, either load it.
    INPUT :        fname    = file name.
    OUTPUT :    None.
    RETURN :    NULL if error, else the requested file.
    DESCRIPTION :
 *****************************************************************************/
data_list_file *
dataListFile(char *fname)
{
    data_list_file *dlf; /* To process the request */

    /*
     * Maybe we already have loaded this file ?
     */
    for (dlf = mctx.dlf; dlf != NULL; dlf = dlf->next) {
        if (!strcmp(fname, dlf->fname)) {
            return (dlf);
        }
    }
    /*
     * Well, it looks like we should load a new file ;-)
     * Allocate a new data structure, chain it in mctx and load the file.
     */
    dlf = (data_list_file *)malloc(sizeof(data_list_file));
    if (dlf == NULL) {
        return dlf;
    }
    dlf->next = mctx.dlf;
    mctx.dlf = dlf;
    dlf->fname = strdup(fname);
    if (loadDataListFile(dlf) < 0) {
        return (NULL);
    }
    /*
     * Loaded...
     */
    return (dlf);
}

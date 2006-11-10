#ident "ldclt @(#)data.c	1.8 01/03/23"

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


/*
	FILE :		data.c
	AUTHOR :	Jean-Luc SCHWING
	VERSION :       1.0
	DATE :		11 January 1999
	DESCRIPTION :	
			This file implements the management of the data that 
			are manipulated by ldclt.
			It is targetted to contain all the functions needed for 
			the images, etc...
	LOCAL :		None.
	HISTORY :
---------+--------------+------------------------------------------------------
dd/mm/yy | Author	| Comments
---------+--------------+------------------------------------------------------
11/01/99 | JL Schwing	| Creation
---------+--------------+------------------------------------------------------
06/03/00 | JL Schwing	| 1.2 : Test malloc() return value.
---------+--------------+------------------------------------------------------
28/11/00 | JL Schwing	| 1.3 : Port on NT 4.
---------+--------------+------------------------------------------------------
30/11/00 | JL Schwing	| 1.4 : Implement loadImages for NT.
---------+--------------+------------------------------------------------------
30/11/00 | JL Schwing	| 1.5 : Port on OSF1.
---------+--------------+------------------------------------------------------
01/12/00 | JL Schwing	| 1.6 : Port on Linux.
---------+--------------+------------------------------------------------------
06/03/01 | JL Schwing	| 1.7 : Better error messages if images not found.
---------+--------------+------------------------------------------------------
23/03/01 | JL Schwing	| 1.8 : Implements data file list support in variants.
---------+--------------+------------------------------------------------------
*/

#include <stdio.h>	/* printf(), etc... */
#include <stdlib.h>	/* realloc(), etc... */
#include <string.h>	/* strlen(), etc... */
#include <errno.h>	/* errno, etc... */			/*JLS 06-03-00*/
#include <sys/types.h>	/* Misc types... */
#include <sys/stat.h>	/* stat(), etc... */
#include <fcntl.h>	/* open(), etc... */
#include <lber.h>	/* ldap C-API BER declarations */
#include <ldap.h>	/* ldap C-API declarations */
#ifndef _WIN32							/*JLS 28-11-00*/
#include <unistd.h>	/* close(), etc... */
#include <dirent.h>	/* opendir(), etc... */
#include <pthread.h>	/* pthreads(), etc... */
#include <sys/mman.h>	/* mmap(), etc... */
#endif								/*JLS 28-11-00*/

#include "port.h"	/* Portability definitions */		/*JLS 28-11-00*/
#include "ldclt.h"	/* This tool's include file */






/* ****************************************************************************
	FUNCTION :	getExtend
	PURPOSE :	Get the extension of the given string, i.e. the part
			that is after the last '.'
	INPUT :		str	= string to process
	OUTPUT :	None.
	RETURN :	The extension.
	DESCRIPTION :
 *****************************************************************************/
char *getExtend (
	char	*str)
{
  int	 i;
  for (i=strlen(str)-1; (i>=0) && (str[i]!='.') ; i--);
  return (&(str[i+1]));
}




/* ****************************************************************************
	FUNCTION :	loadImages
	PURPOSE :	Load the images from the given directory.
	INPUT :		dirpath	= directory where the images are located.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int loadImages (
	char	*dirpath)
{
#ifdef _WIN32
  WIN32_FIND_DATA	 fileData;	/* Current file */
  HANDLE		 dirContext;	/* Directory context */
  char			*findPath;	/* To build the find path */
  char			*pt;		/* To read the images */
#else /* _WIN32 */
  DIR		*dirp;		/* Directory data */
  struct dirent	*direntp;	/* Directory entry */
#endif /* _WIN32 */
  char		*fileName;	/* As read from the system */
  char		 name [1024];	/* To build the full path */
  struct stat	 stat_buf;	/* To read the image size */
  int		 fd;		/* To open the image */
  int		 ret;		/* Return value */

  /*
   * Initialization
   */
  mctx.images     = NULL;
  mctx.imagesNb   = 0;
  mctx.imagesLast = -1;

  if ((ret = ldclt_mutex_init(&(mctx.imagesLast_mutex))) != 0)
  {
    fprintf (stderr, "ldclt: %s\n", strerror (ret));
    fprintf (stderr, "Error: cannot initiate imagesLast_mutex\n");
    fflush (stderr);
    return (-1);
  }

  /*
   * Open the directory
   */
#ifdef _WIN32
  findPath = (char *) malloc (strlen (dirpath) + 5);
  strcpy (findPath, dirpath);
  strcat (findPath, "/*.*");
  dirContext = FindFirstFile (findPath, &fileData);
  if (dirContext == INVALID_HANDLE_VALUE)
  {
    fprintf (stderr, "ldlct: cannot load images from %s\n", dirpath);
    fprintf (stderr, "ldclt: try using -e imagesdir=path\n");	/*JLS 06-03-01*/
    fflush (stderr);
    free (findPath);
    return (-1);
  }
#else /* _WIN32 */
  dirp = opendir (dirpath);
  if (dirp == NULL)
  {
    perror (dirpath);
    fprintf (stderr, "ldlct: cannot load images from %s\n", dirpath);
    fprintf (stderr, "ldclt: try using -e imagesdir=path\n");	/*JLS 06-03-01*/
    fflush (stderr);
    return (-1);
  }
#endif /* _WIN32 */

  /*
   * Process the directory.
   * We will only accept the .jpg files, as stated by the RFC.
   */
#ifdef _WIN32
  fileName = fileData.cFileName;
  do
  {
#else
  while ((direntp = readdir (dirp)) != NULL)
  {
    fileName = direntp->d_name;
#endif
    if (!strcmp (getExtend (fileName), "jpg"))
    {
      /*
       * Allocate a new image, and initiates with its name.
       */
      mctx.imagesNb++;
      mctx.images = 
		(image *) realloc (mctx.images, mctx.imagesNb * sizeof (image));
      if (mctx.images == NULL)					/*JLS 06-03-00*/
      {								/*JLS 06-03-00*/
	printf ("Error: cannot realloc(mctx.images), error=%d (%s)\n",
		errno, strerror (errno));			/*JLS 06-03-00*/
	return (-1);						/*JLS 06-03-00*/
      }								/*JLS 06-03-00*/
      mctx.images[mctx.imagesNb-1].name = 
		(char *) malloc (strlen(fileName) + 1);
      if (mctx.images[mctx.imagesNb-1].name == NULL)		/*JLS 06-03-00*/
      {								/*JLS 06-03-00*/
	printf ("Error: cannot malloc(mctx.images[%d]).name, error=%d (%s)\n",
		mctx.imagesNb-1, errno, strerror (errno));	/*JLS 06-03-00*/
	return (-1);						/*JLS 06-03-00*/
      }								/*JLS 06-03-00*/
      strcpy (mctx.images[mctx.imagesNb-1].name, fileName);

      /*
       * Read the image size
       */
      strcpy (name, dirpath);
      strcat (name, "/");
      strcat (name, fileName);
      if (stat (name, &stat_buf) < 0)
      {
	perror (name);
	fprintf (stderr, "Cannot stat(%s)\n", name);
	fflush (stderr);
	return (-1);
      }
      mctx.images[mctx.imagesNb-1].length = stat_buf.st_size;

      /*
       * Open the image
       */
      fd = open (name, O_RDONLY);
      if (fd < 0)
      {
	perror (name);
	fprintf (stderr, "Cannot open(%s)\n", name);
	fflush (stderr);
	return (-1);
      }

#ifdef _WIN32
      /*
       * Allocate buffer and read the data :-(
       */
      mctx.images[mctx.imagesNb-1].data = (char *) malloc (stat_buf.st_size);
      if (mctx.images[mctx.imagesNb-1].data == NULL)
      {
	fprintf (stderr, "Cannot malloc(%d) to load %s\n",
				stat_buf.st_size, name);
	fflush (stderr);
	return (-1);
      }
      if (read (fd, mctx.images[mctx.imagesNb-1].data, stat_buf.st_size) < 0)
      {
	perror (name);
	fprintf (stderr, "Cannot read(%s)\n", name);
	fflush (stderr);
	return (-1);
      }
#else /* _WIN32 */
      /*
       * mmap() the image
       */
      mctx.images[mctx.imagesNb-1].data = mmap (0, stat_buf.st_size,
			PROT_READ, MAP_SHARED, fd, 0);
      if (mctx.images[mctx.imagesNb-1].data == (char *)MAP_FAILED)
      {
	perror (name);
	fprintf (stderr, "Cannot mmap(%s)\n", name);
	fflush (stderr);
	return (-1);
      }
#endif /* _WIN32 */

      /*
       * Close the image. The mmap() will remain available, and this
       * close() will save file descriptors.
       */
      if (close (fd) < 0)
      {
	perror (name);
	fprintf (stderr, "Cannot close(%s)\n", name);
	fflush (stderr);
	return (-1);
      }
    }
#ifdef _WIN32
  } while (FindNextFile(dirContext, &fileData) == TRUE);
#else
  } /* while ((direntp = readdir (dirp)) != NULL) */
#endif

  /*
   * Close the directory
   */
#ifndef _WIN32
  if (closedir (dirp) < 0)
  {
    perror (dirpath);
    fprintf (stderr, "Cannot closedir(%s)\n", dirpath);
    fflush (stderr);
    return (-1);
  }
#endif

  /*
   * Normal end
   */
#ifdef _WIN32
  free (findPath);
#endif
  return (0);
}






/* ****************************************************************************
	FUNCTION :	getImage
	PURPOSE :	Add a random image to the given attribute.
	INPUT :		None.
	OUTPUT :	attribute	= the attribute where the image should 
					  be added.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int getImage (
	LDAPMod		*attribute)
{
  int	 imageNumber;	/* The image we will select */
  int	 ret;		/* Return value */

  /*
   * Select the next image
   */
  if ((ret = ldclt_mutex_lock (&(mctx.imagesLast_mutex))) != 0)	/*JLS 29-11-00*/
  {
    fprintf (stderr, 
	"Cannot mutex_lock(imagesLast_mutex), error=%d (%s)\n", 
	ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }
  mctx.imagesLast++;
  if (mctx.imagesLast == mctx.imagesNb)
    mctx.imagesLast = 0;
  imageNumber = mctx.imagesLast;
  if ((ret = ldclt_mutex_unlock (&(mctx.imagesLast_mutex))) != 0)
  {
    fprintf (stderr,
	"Cannot mutex_unlock(imagesLast_mutex), error=%d (%s)\n",
	ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  /*
   * Create the data structure required
   */
  attribute->mod_bvalues = (struct berval **) 
		malloc (2 * sizeof (struct berval *));
  if (attribute->mod_bvalues == NULL)				/*JLS 06-03-00*/
  {								/*JLS 06-03-00*/
    printf ("Error: cannot malloc(attribute->mod_bvalues), error=%d (%s)\n",
		errno, strerror (errno));			/*JLS 06-03-00*/
    return (-1);						/*JLS 06-03-00*/
  }								/*JLS 06-03-00*/
  attribute->mod_bvalues[0] = (struct berval *) malloc (sizeof (struct berval));
  if (attribute->mod_bvalues[0] == NULL)			/*JLS 06-03-00*/
  {								/*JLS 06-03-00*/
    printf ("Error: cannot malloc(attribute->mod_bvalues[0]), error=%d (%s)\n",
		errno, strerror (errno));			/*JLS 06-03-00*/
    return (-1);						/*JLS 06-03-00*/
  }								/*JLS 06-03-00*/
  attribute->mod_bvalues[1] = NULL;

  /*
   * Fill the bvalue with the image data
   */
  attribute->mod_bvalues[0]->bv_len = mctx.images[imageNumber].length;
  attribute->mod_bvalues[0]->bv_val = mctx.images[imageNumber].data;

  /*
   * Normal end
   */
  return (0);
}







						/* New */	/*JLS 23-03-01*/
/* ****************************************************************************
	FUNCTION :	loadDataListFile
	PURPOSE :	Load the data list file given in argument.
	INPUT :		dlf->fname	= file to process
	OUTPUT :	dlf		= file read
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
loadDataListFile (
	data_list_file	*dlf)
{
  FILE	*ifile;			/* Input file */
  char	 line[MAX_FILTER];	/* To read ifile */

  /*
   * Open the file
   */
  ifile = fopen (dlf->fname, "r");
  if (ifile == NULL)
  {
    perror (dlf->fname);
    fprintf (stderr, "Error: cannot open file \"%s\"\n", dlf->fname);
    return (-1);
  }

  /*
   * Count the entries.
   * Allocate the array.
   * Rewind the file.
   */
  for (dlf->strNb=0 ; fgets(line, MAX_FILTER, ifile) != NULL ; dlf->strNb++);
  dlf->str = (char **) malloc (dlf->strNb * sizeof (char *));
  if (fseek (ifile, 0, SEEK_SET) != 0)
  {
    perror (dlf->fname);
    fprintf (stderr, "Error: cannot rewind file \"%s\"\n", dlf->fname);
    return (-1);
  }

  /*
   * Read all the entries from this file
   */
  dlf->strNb=0;
  while (fgets(line, MAX_FILTER, ifile) != NULL)
  {
    if ((strlen (line) > 0) && (line[strlen(line)-1]=='\n'))
      line[strlen(line)-1] = '\0';
    dlf->str[dlf->strNb] = strdup (line);
    dlf->strNb++;
  }

  /*
   * Close the file
   */
  if (fclose (ifile) != 0)
  {
    perror (dlf->fname);
    fprintf (stderr, "Error: cannot fclose file \"%s\"\n", dlf->fname);
    return (-1);
  }
  return (0);
}







						/* New */	/*JLS 23-03-01*/
/* ****************************************************************************
	FUNCTION :	dataListFile
	PURPOSE :	Find the given data_list_file either in the list of
			files already loaded, either load it.
	INPUT :		fname	= file name.
	OUTPUT :	None.
	RETURN :	NULL if error, else the requested file.
	DESCRIPTION :
 *****************************************************************************/
data_list_file *
dataListFile (
	char	*fname)
{
  data_list_file	*dlf;	/* To process the request */

  /*
   * Maybe we already have loaded this file ?
   */
  for (dlf=mctx.dlf ; dlf != NULL ; dlf=dlf->next)
    if (!strcmp (fname, dlf->fname))
      return (dlf);

  /*
   * Well, it looks like we should load a new file ;-)
   * Allocate a new data structure, chain it in mctx and load the file.
   */
  dlf = (data_list_file *) malloc (sizeof (data_list_file));
  dlf->next  = mctx.dlf;
  mctx.dlf   = dlf;
  dlf->fname = strdup (fname);
  if (loadDataListFile (dlf) < 0)
    return (NULL);

  /*
   * Loaded...
   */
  return (dlf);
}








/* End of file */

#ident "ldclt @(#)port.c	1.2 01/03/14"

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
        FILE :		port.c
        AUTHOR :        Jean-Luc SCHWING
        VERSION :       1.0
        DATE :		28 November 2000
        DESCRIPTION :	
			This file contains platform-independant version of 
			common (unix) functions in order to have portable 
			source code for either unix and windows platforms.
			It is greatly inspired from iPlanet DS 5.0 workspace.
 LOCAL :		None.
        HISTORY :
---------+--------------+------------------------------------------------------
dd/mm/yy | Author	| Comments
---------+--------------+------------------------------------------------------
28/11/00 | JL Schwing	| Creation
---------+--------------+------------------------------------------------------
14/03/01 | JL Schwing	| 1.2 : Lint cleanup.
---------+--------------+------------------------------------------------------
*/


#include <stdio.h>		/* EOF, etc... */

#ifdef _WIN32
#include <windows.h>
#include <winbase.h>
#else
#include <unistd.h>		/* sleep(), etc... */		/*JLS 14-03-01*/
#include <pthread.h>		/* pthreads(), etc... */
#endif

#include "port.h"

/************************************************************************/
/************************************************************************/
/****************         NT section              ***********************/
/************************************************************************/
/************************************************************************/ 

#ifdef _WIN32

int
ldclt_mutex_init (
	ldclt_mutex_t	*mutex)
{
  InitializeCriticalSection (mutex);
  return (0);
}

int
ldclt_mutex_lock (
	ldclt_mutex_t	*mutex)
{
  EnterCriticalSection(mutex);
  return (0);
}

int
ldclt_mutex_unlock (
	ldclt_mutex_t	*mutex)
{
  LeaveCriticalSection (mutex);
  return (0);
}

void
ldclt_sleep (
	int	 nseconds)
{
  Sleep (1000 * nseconds);
}

int
ldclt_thread_create (
	ldclt_tid	*tid,
	void		*fct,
	void		*param)
{
  CreateThread (NULL, 0, fct, param, 0, tid);
  return (0);
}

long 
lrand48 (void)
{
  return ((rand()<<8)+rand());
}

/*
 * Implements the Unix getopt function for NT systems
 */
char	*optarg;
int	 optind;
int
getopt (
	int	  argc,
	char	**argv,
	char	 *optstring)
{
  static char	**prevArgv = NULL;	/* Memorize argv to parse */
  static int	  inOption;		/* In option parsing ? */
  static int	  cNum;			/* Current char num */
  int		  c;			/* Current char */
  int		  i;			/* Loops */

  /*
   * Initialization - maybe the first time this function is called ?
   */
  if (prevArgv != argv)
  {
    prevArgv = argv;
    optind   = 0;
    inOption = 0;
  }

  /*
   * Maybe we processed the last chars of the option in the previous call
   */
  if (inOption)
  {
    if (argv[optind][cNum] == '\0')
      inOption = 0;
  }

  /*
   * Maybe we should look for '-'
   */
  if (!inOption)
  {
    optind++;				/* Next option */
    if (optind == argc)			/* No more option */
      return (EOF);
    if (argv[optind][0] != '-')		/* Not an option */
      return (EOF);
    if (argv[optind][1] == '\0')	/* Only '-' */
      return (EOF);
    cNum = 1;				/* Next char to process */
    inOption = 0;			/* We are in an option */
  }

  /*
   * See if this is a valid option
   */
  c = argv[optind][cNum];
  for (i=0 ; (i<strlen(optstring)) && (c!=optstring[i]) ; i++);
  if (c != optstring[i])		/* Not an option */
    return ('?');
  cNum++;				/* Next char */

  /*
   * Check if this option requires an argument
   * Note that for the last char of optstring, it is a valid '\0' != ':'
   */
  if (optstring[i+1] != ':')		/* No argument */
    return (c);

  /*
   * Need an argument...
   * The argument is either the end of argv[optind] or argv[++optind]
   */
  if (argv[optind][cNum] == '\0')	/* Must return next argument */
  {
    optind++;				/* Next argument */
    if (optind == argc)			/* There is no next argument */
    {
      printf ("%s: option requires an argument -- %c\n", argv[0], c);
      return ('?');
    }
    optarg   = argv[optind];	/* Set optarg to teh argument argv[] */
    inOption = 0;		/* No more in option... */
    return (c);
  }

  /*
   * Return the end of the current argv[optind]
   */
  optarg   = &(argv[optind][cNum]);	/* End of argv[optind] */
  inOption = 0;				/* No more in option */
  return (c);
}


/*
 * Implement the Unix getsubopt function for NT systems
 */
int
getsubopt(
	char	**optionp,
	char	**tokens,
	char	**valuep)
{
  int	 i;		/* Loops */
  char	*curOpt;	/* Current optionp */

  curOpt = *optionp;	/* Begin of current option */

  /*
   * Find the end of the current option
   */
  for (i=0 ; (curOpt[i]!='\0') && (curOpt[i]!=',') ; i++);
  if (curOpt[i] == '\0')
    *optionp = &(curOpt[i]);
  else
    *optionp = &(curOpt[i+1]);
  curOpt[i] = '\0';		/* Do not forget to end this string */

  /*
   * Find if there is a subvalue for this option
   */
  for (i=0 ; (curOpt[i]!='\0') && (curOpt[i]!='=') ; i++);
  if (curOpt[i] == '\0')
    *valuep = &(curOpt[i]);
  else
    *valuep = &(curOpt[i+1]);
  curOpt[i] = '\0';		/* Do not forget to end this string */

  /*
   * Find if this option is valid...
   */
  for (i=0 ; tokens[i] != NULL ; i++)
    if (!strcmp (curOpt, tokens[i]))
      return (i);

  /*
   * Not found...
   */
  return (-1);
}


#else /* NT 4 */

/************************************************************************/
/************************************************************************/
/****************         Unix section            ***********************/
/************************************************************************/
/************************************************************************/

int
ldclt_mutex_init (
	ldclt_mutex_t	*mutex)
{
  return (pthread_mutex_init (mutex, NULL));
}

int
ldclt_mutex_lock (
	ldclt_mutex_t	 *mutex)
{
  return (pthread_mutex_lock (mutex));
}

int
ldclt_mutex_unlock (
	ldclt_mutex_t	*mutex)
{
  return (pthread_mutex_unlock (mutex));
}

void
ldclt_sleep (
	int	 nseconds)
{
  sleep (nseconds);
}

int
ldclt_thread_create (
	ldclt_tid	*tid,
	void		*(*fct)(void *),
	void		*param)
{
  return (pthread_create (tid, NULL, fct, param));
}

#endif /* _WIN32 */



/* End of file */

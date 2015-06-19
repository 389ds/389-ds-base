#ident "ldclt @(#)port.c	1.2 01/03/14"

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2006 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
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
#include <unistd.h>		/* sleep(), etc... */		/*JLS 14-03-01*/
#include <pthread.h>		/* pthreads(), etc... */
#include "port.h"

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

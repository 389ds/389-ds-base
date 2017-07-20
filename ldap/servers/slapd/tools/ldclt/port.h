#ident "ldclt @(#)port.h    1.4 01/04/03"

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2006 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/*
        FILE :        port.h
        AUTHOR :        Jean-Luc SCHWING
        VERSION :       1.0
        DATE :        28 November 2000
        DESCRIPTION :
            This file contains the include (interface) definitions
            of port.c
*/

/*
 * Tuning of the code
 */

#ifdef HPUX                    /*JLS 01-12-00*/
#define LDCLT_CAST_SIGACTION 1 /*JLS 01-12-00*/
#define LDCLT_NO_DLOPEN 1      /*JLS 01-12-00*/
#endif                         /*JLS 01-12-00*/

#ifdef LINUX                   /*JLS 01-12-00*/
#define LDCLT_CAST_SIGACTION 1 /*JLS 01-12-00*/
#ifndef O_LARGEFILE            /*JLS 03-04-01*/
#define O_LARGEFILE 0100000    /*JLS 03-04-01*/
#endif                         /*JLS 03-04-01*/
#endif                         /*JLS 01-12-00*/


/************************************************************************/
/************************************************************************/
/****************         Unix section            ***********************/
/************************************************************************/
/************************************************************************/

typedef pthread_mutex_t ldclt_mutex_t;
typedef pthread_t ldclt_tid;

/*
 * Portability functions common to all platforms
 */
extern int ldclt_mutex_init(ldclt_mutex_t *mutex);
extern int ldclt_mutex_lock(ldclt_mutex_t *mutex);
extern int ldclt_mutex_unlock(ldclt_mutex_t *mutex);
extern void ldclt_sleep(int nseconds);
extern int ldclt_thread_create(ldclt_tid *tid, void *(*fct)(void *), void *param);

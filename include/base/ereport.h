/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef BASE_EREPORT_H
#define BASE_EREPORT_H

#ifndef NOINTNSAPI
#define INTNSAPI
#endif /* !NOINTNSAPI */

/*
 * ereport.h: Records transactions, reports errors to administrators, etc.
 * 
 * Rob McCool
 */

#ifndef BASE_SESSION_H
#include "session.h"
#endif /* !BASE_SESSION_H */

#ifndef PUBLIC_BASE_EREPORT_H
#include "public/base/ereport.h"
#endif /* !PUBLIC_BASE_EREPORT_H */

/* --- Begin function prototypes --- */

#ifdef INTNSAPI

NSPR_BEGIN_EXTERN_C

/*
 * INTereport logs an error of the given degree and formats the arguments with 
 * the printf() style fmt. Returns whether the log was successful. Records 
 * the current date.
 */

NSAPI_PUBLIC int INTereport(int degree, char *fmt, ...);
NSAPI_PUBLIC int INTereport_v(int degree, char *fmt, va_list args);

/*
 * INTereport_init initializes the error logging subsystem and opens the static
 * file descriptors. It returns NULL upon success and an error string upon
 * error. If a userpw is given, the logs will be chowned to that user.
 * 
 * email is the address of a person to mail upon catastrophic error. It
 * can be NULL if no e-mail is desired. INTereport_init will not duplicate
 * its own copy of this string; you must make sure it stays around and free
 * it when you shut down the server.
 */

NSAPI_PUBLIC
char *INTereport_init(char *err_fn, char *email, PASSWD pwuser, char *version);

/*
 * log_terminate closes the error and common log file descriptors.
 */
NSAPI_PUBLIC void INTereport_terminate(void);

/* For restarts */
NSAPI_PUBLIC SYS_FILE INTereport_getfd(void);

NSPR_END_EXTERN_C

/* --- End function prototypes --- */

#define ereport INTereport
#define ereport_v INTereport_v
#define ereport_init INTereport_init
#define ereport_terminate INTereport_terminate
#define ereport_getfd INTereport_getfd

#endif /* INTNSAPI */

#endif /* !BASE_EREPORT_H */

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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

NSPR_END_EXTERN_C

/* --- End function prototypes --- */

#define ereport INTereport
#define ereport_v INTereport_v

#endif /* INTNSAPI */

#endif /* !BASE_EREPORT_H */

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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef PUBLIC_NSACL_NSERRDEF_H
#define PUBLIC_NSACL_NSERRDEF_H

/*
 * Type:        NSEFrame_t
 *
 * Description:
 *
 *	This type describes the structure of an error frame.  An error
 *	frame contains the following items:
 *
 *	ef_retcode	- This is a copy of the traditional error code,
 *			  as might be returned as a function value to
 *			  indicate an error.  The purpose of the error
 *			  code is to provide the caller of a function
 *			  with sufficient information to determine how
 *			  to process the error.  That is, it does not
 *			  need to identify a specific error, but only
 *			  has to distinguish between classes of errors
 *			  as needed by the caller to respond differently.
 *			  Usually this should be a small number of values.
 *
 *	ef_errorid	- This is an integer identifier which uniquely
 *			  identifies errors in a module or library.
 *			  That is, there should be only one place in
 *			  the source code of the module or library which
 *			  generates a particular error id.  The error id
 *			  is used to select an error message in an error
 *			  message file.
 *
 *	ef_program	- This is a pointer to a string which identifies
 *			  the module or library context of ef_errorid.
 *			  The string is used to construct the name of
 *			  the message file in which an error message for
 *			  ef_errorid can be found.
 *
 *	ef_errc		- This is the number of values stored in ef_errc[]
 *			  for the current error id.
 *
 *	ef_errv		- This is an array of strings which are relevant
 *			  to a particular error id.  These strings can
 *			  be included in an error message retrieved from
 *			  a message file.  The strings in a message file
 *			  can contain "%s" sprintf() format codes.  The
 *			  ef_errv[] strings are passed to sprintf() along
 *			  with the error message string.
 */

#define NSERRMAXARG	8	/* size of ef_errv[] */

typedef struct NSEFrame_s NSEFrame_t;
struct NSEFrame_s {
    NSEFrame_t * ef_next;	/* next error frame on NSErr_t list */
    long ef_retcode;		/* error return code */
    long ef_errorid;		/* error unique identifier */
    char * ef_program;		/* context for ef_errorid */
    int ef_errc;		/* number of strings in ef_errv[] */
    char * ef_errv[NSERRMAXARG];/* arguments for formatting error message */
};

/*
 * Description (NSErr_t)
 *
 *	This type describes the structure of a header for a list of
 *	error frames.  The header contains a pointer to the first
 *	and last error frames on the list.  The first error frame
 *	is normally the one most recently generated, which usually
 *	represents the highest-level interpretation available for an
 *	error that is propogating upward in a call chain.  These
 *	structures are generally allocated as automatic or static
 *	variables.
 */

typedef struct NSErr_s NSErr_t;
struct NSErr_s {
    NSEFrame_t * err_first;			/* first error frame */
    NSEFrame_t * err_last;			/* last error frame */
    NSEFrame_t *(*err_falloc)(NSErr_t * errp);	/* error frame allocator */
    void (*err_ffree)(NSErr_t * errp,
		      NSEFrame_t * efp);	/* error frame deallocator */
};

/* Define an initializer for an NSErr_t */
#define NSERRINIT	{ 0, 0, 0, 0 }

#ifndef INTNSACL

#define nserrDispose (*__nsacl_table->f_nserrDispose)
#define nserrFAlloc (*__nsacl_table->f_nserrFAlloc)
#define nserrFFree (*__nsacl_table->f_nserrFFree)
#define nserrGenerate (*__nsacl_table->f_nserrGenerate)

#endif /* !INTNSACL */

#endif /* !PUBLIC_NSACL_NSERRDEF_H */

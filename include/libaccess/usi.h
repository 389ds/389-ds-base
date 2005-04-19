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
#ifndef __usi_h
#define __usi_h

/*
 * Description (usi.h)
 *
 *	This file defines the interface to an unsigned integer datatype.
 *	Unsigned integers are used to represent object identifiers of
 *	various sorts, including user ids and group ids.  Functions
 *	for manipulating lists of USIs are also provided in this
 *	interface.
 */

/* Define a type to contain an unsigned integer value */
typedef unsigned int USI_t;

/* Define a type to describe a list of USI_t values */
typedef struct USIList_s USIList_t;
struct USIList_s {
    int uil_count;		/* number of active values in list */
    int uil_size;		/* current size of list area in USI_t */
    USI_t * uil_list;		/* pointer to array of values */
};

/* Define macro to initialize a USIList_t structure */
#define UILINIT(uilptr) \
	{ \
	    (uilptr)->uil_count = 0; \
	    (uilptr)->uil_size = 0; \
	    (uilptr)->uil_list = 0; \
	}

/* Define a macro to replace the contents of one USIList_t with another's */
#define UILREPLACE(dst, src) \
	{ \
	    if ((dst)->uil_size > 0) { \
		FREE((dst)->uil_list); \
	    } \
	    (dst)->uil_count = (src)->uil_count; \
	    (dst)->uil_size = (src)->uil_size; \
	    (dst)->uil_list = (src)->uil_list; \
	    (src)->uil_count = 0; \
	    (src)->uil_size = 0; \
	    (src)->uil_list = 0; \
	}

/* Define a variation of UILINIT() that frees any allocated space */
#define UILFREE(uilptr) \
	{ \
	    if ((uilptr)->uil_size > 0) { \
		FREE((uilptr)->uil_list); \
	    } \
	    (uilptr)->uil_count = 0; \
	    (uilptr)->uil_size = 0; \
	    (uilptr)->uil_list = 0; \
	}

/* Define a macro to extract the current number of items in a USIList_t */
#define UILCOUNT(uilptr) ((uilptr)->uil_count)

/* Define a macro to return a pointer to the array of values */
#define UILLIST(uilptr) ((uilptr)->uil_list)

NSPR_BEGIN_EXTERN_C

/* Define functions in usi.c */
extern USI_t * usiAlloc(USIList_t * uilptr, int count);
extern int usiInsert(USIList_t * uilptr, USI_t usi);
extern int usiPresent(USIList_t * uilptr, USI_t usi);
extern int usiRemove(USIList_t * uilptr, USI_t usi);
extern int uilDuplicate(USIList_t * dstptr, USIList_t * srcptr);
extern int uilMerge(USIList_t * dstptr, USIList_t * srcptr);

NSPR_END_EXTERN_C

#endif /* __usi_h */

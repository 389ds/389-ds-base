/** BEGIN COPYRIGHT BLOCK
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

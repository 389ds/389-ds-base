/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include "base/systems.h"
#include "netsite.h"
#include "assert.h"
#include "libaccess/usi.h"

/*
 * Description (usiAlloc)
 *
 *	This function is used to initialize a USIList_t structure to
 *	reference an array of unsigned integers, where the size of the
 *	array is specified.  The caller is responsible for initializing
 *	the specified number of values in the array.
 *
 * Arguments:
 *
 *	uilptr			- pointer to list head
 *	count			- number of entries to allocate
 *
 * Returns:
 *
 *	If successful, a pointer to the USI_t array now referenced by the
 *	list head (uilptr) is returned.  An error is indicated by a null
 *	return value.
 */

USI_t * usiAlloc(USIList_t * uilptr, int count)
{
    /* Is there space already allocated for this list? */
    if (uilptr->uil_size > 0) {

	/* If it's not big enough to hold the desired size, free it */
	if (count > uilptr->uil_size) {
	    FREE(uilptr->uil_list);
	    UILINIT(uilptr);
	}
    }

    /* Do we need space? */
    if (uilptr->uil_size < count) {

	/* Yes, allocate space for the specified number of values */
	uilptr->uil_list = (USI_t *)MALLOC(sizeof(USI_t) * count);
	if (uilptr->uil_list == 0) {

	    /* Error - no memory available */
	    uilptr->uil_count = 0;
	    return 0;
	}

	uilptr->uil_size = count;
    }

    uilptr->uil_count = count;

    return uilptr->uil_list;
}

/*
 * Description (usiInsert)
 *
 *	This function is called to insert a specified USI_t value into
 *	a given list of USI_t values.  The values are maintained in an
 *	array, where they are kept in ascending order.  Duplicate values
 *	are rejected.
 *
 * Arguments:
 *
 *	uilptr			- pointer to list head
 *	usi			- value to be inserted
 *
 * Returns:
 *
 *	If the specified value is already in the list, zero is returned.
 *	If the value is successfully inserted into the list, +1 is
 *	returned.  An error is indicated by a negative return value.
 */

int usiInsert(USIList_t * uilptr, USI_t usi)
{
    int ilow, ihigh, i;
    USI_t * ids;

    ids = uilptr->uil_list;

    /* Binary search for specified group id */
    i = 0;
    for (ilow = 0, ihigh = uilptr->uil_count; ilow != ihigh; ) {

	i = (ilow + ihigh) >> 1;
	if (usi == ids[i]) {
	    /* The value is already in the list */
	    return 0;
	}

	if (usi > ids[i]) {
	    ilow = i + 1;
	}
	else {
	    ihigh = i;
	}
    }

    /* Check for empty list */
    if (uilptr->uil_count <= 0) {

	/* Any space allocated for the list yet? */
	if (uilptr->uil_size <= 0) {

	    /* No, allocate some initial space */
	    ids = (USI_t *) MALLOC(sizeof(USI_t) * 4);
	    if (ids == 0) {
		/* Error - no memory available */
		return -1;
	    }

	    uilptr->uil_size = 4;
	    uilptr->uil_list = ids;
	}

	/* Value will be inserted at ilow, which is zero */
    }
    else {

	/*
	 * Set ilow to the index at which the specified value
	 * should be inserted.
	 */
	if (usi > ids[i]) ++i;
	ilow = i;

	/* Is there space for another value? */
	if (uilptr->uil_count >= uilptr->uil_size) {

	    /* No, expand the array to hold more values */
	    ids = (USI_t *)REALLOC(ids,
				   (uilptr->uil_size + 4) * sizeof(USI_t));
	    if (ids == 0) {
		/* Error - no memory available */
		return -1;
	    }

	    uilptr->uil_size += 4;
	    uilptr->uil_list = ids;
	}

	/* Copy higher values up */
	for (i = uilptr->uil_count; i > ilow; --i) {
	    ids[i] = ids[i-1];
	}
    }

    /* Add the new value */
    ids[ilow] = usi;
    uilptr->uil_count += 1;

    return 1;
}

/*
 * Description (usiPresent)
 *
 *	This function is called to check whether a specified USI_t value
 *	is present in a given list.
 *
 * Arguments:
 *
 *	uilptr			- pointer to list head
 *	usi			- value to check for
 *
 * Returns:
 *
 *	The return value is the index of the value in the list, plus one,
 *	if the value is present in the list, 0 if it is not.
 */

int usiPresent(USIList_t * uilptr, USI_t usi)
{
    int ilow, ihigh, i;
    USI_t * ids;

    ids = uilptr->uil_list;

    /* Binary search for specified group id */
    i = 0;
    for (ilow = 0, ihigh = uilptr->uil_count; ilow != ihigh; ) {

	i = (ilow + ihigh) >> 1;
	if (usi == ids[i]) {
	    /* The value is in the list */
	    return i + 1;
	}

	if (usi > ids[i]) {
	    ilow = i + 1;
	}
	else {
	    ihigh = i;
	}
    }

    /* The value was not found */
    return 0;
}

/*
 * Description (usiRemove)
 *
 *	This function is called to remove a specified USI_t value from
 *	a given list.  The list is compressed when the value is removed.
 *
 * Arguments:
 *
 *	uilptr				- pointer to list head
 *	usi				- value to be removed
 *
 * Returns:
 *
 *	Returns the value returned by usiPresent(uilptr, usi).
 */

int usiRemove(USIList_t * uilptr, USI_t usi)
{
    USI_t * ids;
    int i, j;

    i = usiPresent(uilptr, usi);
    if (i > 0) {

	/* Compress the remaining values */
	ids = uilptr->uil_list;
	for (j = i ; j < uilptr->uil_count; ++j) {
	    ids[j-1] = ids[j];
	}

	/* Decrement the number of values and free space if none left */
	if (--uilptr->uil_count <= 0) {
	    FREE(uilptr->uil_list);
	    UILINIT(uilptr);
	}
    }

    return i;
}

/*
 * Description (uilDuplicate)
 *
 *	This function is called to make a duplicate of a specified
 *	source list, in a given destination list.  Any existing list
 *	referenced by the destination list head is either overwritten
 *	or replaced with a newly allocated list.  The values in the
 *	source list are copied to the destination.  Note that the
 *	destination list area may be larger than the source list area
 *	on return, i.e. their uil_size values may differ.
 *
 * Arguments:
 *
 *	dstptr			- pointer to destination list head
 *	srcptr			- pointer to source list head
 *
 * Returns:
 *
 *	The number of elements in the source and destination lists is
 *	returned if successful.  An error is indicated by a negative
 *	return value.
 */

int uilDuplicate(USIList_t * dstptr, USIList_t * srcptr)
{
    USI_t * idlist;
    USI_t * srclist;
    int count;
    int i;

    count = srcptr->uil_count;
    srclist = srcptr->uil_list;

    /* Allocate enough space in the destination list */
    idlist = usiAlloc(dstptr, count);
    if ((idlist == 0) && (count > 0)) {
	/* Error - insufficient memory */
	return -1;
    }

    /* Copy source values to destination */
    for (i = 0; i < count; ++i) {
	idlist[i] = srclist[i];
    }

    /* Return number of entries in destination list */
    return count;
}

/*
 * Description (uilMerge)
 *
 *	This function is called to merge the values in a source list
 *	into a destination list.  That is, any values in the source
 *	list which are not in the destination list will be inserted
 *	in it.
 *
 * Arguments:
 *
 *	dstptr			- pointer to destination list head
 *	srcptr			- pointer to source list head
 *
 * Returns:
 *
 *	The resulting number of elements in the destination list is
 *	returned if successful.  An error is indicated by a negative
 *	return value.
 */

int uilMerge(USIList_t * dstptr, USIList_t * srcptr)
{
    USIList_t mglist;		/* list head for merged list */
    USI_t * srclist = srcptr->uil_list;
    USI_t * dstlist = dstptr->uil_list;
    int isrc, idst;
    int scnt, dcnt;
    int rv;

    UILINIT(&mglist);

    scnt = srcptr->uil_count;
    dcnt = dstptr->uil_count;
    isrc = 0;
    idst = 0;

    while ((isrc < scnt) && (idst < dcnt)) {

	if (srclist[isrc] >= dstlist[idst]) {
	    rv = usiInsert(&mglist, dstlist[idst]);
	    if (rv < 0) goto punt;
	    if (srclist[isrc] == dstlist[idst]) ++isrc;
	    ++idst;
	}
	else if (srclist[isrc] < dstlist[idst]) {
	    rv = usiInsert(&mglist, srclist[isrc]);
	    if (rv < 0) goto punt;
	    ++isrc;
	}
    }

    while (isrc < scnt) {
	rv = usiInsert(&mglist, srclist[isrc]);
	if (rv < 0) goto punt;
	++isrc;
    }

    while (idst < dcnt) {
	rv = usiInsert(&mglist, dstlist[idst]);
	if (rv < 0) goto punt;
	++idst;
    }

    UILREPLACE(dstptr, &mglist);

    return dstptr->uil_count;

  punt:
    UILFREE(&mglist);
    return rv;
}


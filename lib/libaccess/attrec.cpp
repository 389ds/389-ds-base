/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Description (attrec.c)
 *
 *	This module contains routines for encoding and decoding
 *	attribute records.  See attrec.h for a description of attribute
 *	records.
 */

#include "base/systems.h"
#include "netsite.h"
#include "assert.h"
#define __PRIVATE_ATTREC
#include "libaccess/attrec.h"

/*
 * Description (NTS_Length)
 *
 *	This function returns the length of a null-terminated string.
 *	The length includes the terminating null octet.
 *
 *	Use of the NTSLENGTH() macro is recommended (see attrec.h).
 *
 * Arguments:
 *
 *	nts			- a pointer to the null-terminate string
 *				  (may be null)
 *
 * Returns:
 *
 *	The length of the string.  If 'nts' is null, the value is one,
 *	since there is always a null octet.
 */

int NTS_Length(NTS_t nts)
{
    return ((nts) ? strlen((const char *)nts) + 1 : 1);
}

/*
 * Description (NTS_Decode)
 *
 *	This function decodes a null-terminated string from a specified
 *	attribute record buffer.  It copies the string into a dynamically
 *	allocated buffer, if 'pnts' is not null, and returns a pointer
 *	to it.  The return value of the function is a pointer to the
 *	octet following the NTS in the attribute record buffer.
 *
 *	Use of the NTSDECODE() macro is recommended (see attrec.h).
 *
 * Arguments:
 *
 *	cp			- pointer into the attribute record buffer
 *	pnts			- pointer to returned reference to decoded
 *				  NTS, or null, if the decoded NTS is not
 *				  to be copied to a dynamic buffer
 *
 * Returns:
 *
 *	The function return value is a pointer to the octet following
 *	the NTS in the attribute record buffer.  A pointer to a
 *	dynamically allocated buffer containing the decoded NTS will
 *	be returned through 'pnts', if it is non-null.  This returned
 *	pointer will be null if the NTS contains only a terminating
 *	octet.
 */

ATR_t NTS_Decode(ATR_t cp, NTS_t * pnts)
{
    NTS_t nts = 0;
    int len = NTSLENGTH(cp);		/* length of the string */

    /* Are we going to return a copy of the string? */
    if (pnts) {

	/* Yes, is it more than just a null octet? */
	if (len > 1) {

	    /* Yes, allocate a buffer and copy the string to it */
	    nts = (NTS_t)MALLOC(len);
	    if (nts) {
		memcpy((void *)nts, (void *)cp, len);
	    }
	}

	/* Return a pointer to the copied string, or null */
	*pnts = nts;
    }

    /* Return pointer to octet after string */
    return cp + len;
}

/*
 * Description (NTS_Encode)
 *
 *	This function encodes a null-terminated string into a specified
 *	attribute record buffer.  It returns a pointer to the octet
 *	following the encoding.
 *
 *	Use of the NTSENCODE() macro is recommended (see attrec.h).
 *
 * Arguments:
 *
 *	cp			- pointer into the attribute record buffer
 *	nts			- pointer to the string to be encoded
 *
 * Returns:
 *
 *	A pointer to the octet following the encoding in the attribute
 *	record buffer is returned.
 */

ATR_t NTS_Encode(ATR_t cp, NTS_t nts)
{

    /* Is the string pointer null? */
    if (nts) {
	int len = NTSLENGTH(nts);

	/* No, copy the string to the attribute record buffer */
	memcpy((void *)cp, (void *)nts, len);

	/* Get pointer to octet after it */
	cp += len;
    }
    else {

	/* A null pointer indicates an empty NTS, i.e. just a null octet */
	*cp++ = 0;
    }

    /* Return a pointer to the octet after the encoding */
    return cp;
}

/*
 * Description (USI_Decode)
 *
 *	This function decodes an unsigned integer value from a specified
 *	attribute record buffer.
 *
 *	Use of the USIDECODE() macro is recommended (see attrec.h).
 *
 * Arguments:
 *
 *	cp			- pointer into the attribute record buffer
 *	pval			- pointer to returned integer value
 *
 * Returns:
 *
 *	If 'pval' is not null, the decoded integer value is returned
 *	in the referenced location.  The function return value is a
 *	pointer to the octet following the USI encoding in the attribute
 *	record buffer.
 */

ATR_t USI_Decode(ATR_t cp, USI_t * pval)
{
    int val;

    /* Is this a length value? */
    if (*(cp) & 0x80) {
	int i;
	int len;

	/* Yes, build the value from the indicated number of octets */
	len = *cp++ & 0x7;
	val = 0;
	for (i = 0; i < len; ++i) {
	    val <<= 8;
	    val |= (cp[i] & 0xff);
	}
	cp += len;
    }
    else {

	/* This octet is the value */
	val = *cp++;
    }

    /* Return the value if there's a place to put it */
    if (pval) *pval = val;

    /* Return a pointer to the next item in the attribute record */
    return cp;
}

/*
 * Description (USI_Encode)
 *
 *	This function encodes an unsigned integer value into a specified
 *	attribute record buffer.
 *
 *	Use of the USIENCODE() macro is recommended (see attrec.h).
 *
 * Arguments:
 *
 *	cp			- pointer into the attribute record buffer
 *	val			- the value to be encoded
 *
 * Returns:
 *
 *	A pointer to the octet following the generated encoding in the
 *	attribute record buffer is returned.
 */

ATR_t USI_Encode(ATR_t cp, USI_t val)
{
    /* Check size of value to be encoded */
    if (val <= 0x7f) *cp++ = val;
    else if (val <= 0xff) {
	/* Length plus 8-bit value */
	*cp++ = 0x81;
	*cp++ = val;
    }
    else if (val <= 0xffff) {
	/* Length plus 16-bit value */
	*cp++ = 0x82;
	cp[1] = val & 0xff;
	val >>= 8;
	cp[0] = val & 0xff;
	cp += 2;
    }
    else if (val <= 0xffffff) {
	/* Length plus 24-bit value */
	*cp++ = 0x83;
	cp[2] = val & 0xff;
	val >>= 8;
	cp[1] = val & 0xff;
	val >>= 8;
	cp[0] = val & 0xff;
	cp += 3;
    }
    else {
	/* Length plus 32-bit value */
	*cp++ = 0x84;
	cp[3] = val & 0xff;
	val >>= 8;
	cp[2] = val & 0xff;
	val >>= 8;
	cp[1] = val & 0xff;
	val >>= 8;
	cp[0] = val & 0xff;
	cp += 4;
    }

    /* Return a pointer to the next position in the attribute record */
    return cp;
}

/*
 * Description (USI_Insert)
 *
 *	This function is a variation of USI_Encode() that always generates
 *	the maximum-length encoding for USI value, regardless of the
 *	actual specified value.  For arguments, returns, see USI_Encode().
 *
 *	Use of the USIINSERT() macro is recommended.  The USIALLOC() macro
 *	returns the number of octets that USIINSERT() will generate.
 */

ATR_t USI_Insert(ATR_t cp, USI_t val)
{
    int i;

    assert(USIALLOC() == 5);

    *cp++ = 0x84;
    for (i = 3; i >= 0; --i) {
	cp[i] = val & 0xff;
	val >>= 8;
    }

    return cp + 5;
}

/*
 * Description (USI_Length)
 *
 *	This function returns the number of octets required to encode
 *	an unsigned integer value.
 *
 *	Use of the USILENGTH() macro is recommended (see attrec.h).
 *
 * Arguments:
 *
 *	val			- the unsigned integer value
 *
 * Returns:
 *
 *	The number of octets required to encode the specified value is
 *	returned.
 */

int USI_Length(USI_t val)
{
    return (((USI_t)(val) <= 0x7f) ? 1
				   : (((USI_t)(val) <= 0xff) ? 2
				   : (((USI_t)(val) <= 0xffff) ? 3
				   : (((USI_t)(val) <= 0xffffff) ? 4
				   : 5))));
}


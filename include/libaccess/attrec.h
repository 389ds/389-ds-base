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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef __attrec_h
#define __attrec_h

/*
 * Description (attrec.h)
 *
 *	This file describes the encoding and decoding of attribute
 *	records.  Attribute records consist of a sequence of items
 *	of the form:
 *
 *		<tag><length><contents>
 *
 *	The <tag> is an integer code which identifies a particular
 *	attribute.  The <length> is the integer length in bytes of
 *	the <contents>.  The encoding of the contents is determined
 *	by the <tag>, and is application-specific.
 *
 *	Primitive data types currently supported are unsigned
 *	integers (USI) and null-terminated strings (NTS).  The
 *	encoding of USI values less than 128 is simply an octet
 *	containing the value.  For values 128 or greater, the first
 *	octet is 0x80 plus the length of the value, in octets.
 *	This octet is followed by the indicated number of octets,
 *	containing the USI value, with the most significant bits in
 *	the first octet, and the least significant bits in the last
 *	octet.
 *
 *	Examples of USI encoding:
 *
 *		Value		Encoding (each value is an octet)
 *		    4		0x04
 *		  127		0x7f
 *		   -1		(this is not a USI)
 *		  128		0x81 0x80
 *		 1023		0x82 0x03 0xff
 *
 *	The encoding of a null-terminated string (NTS) is simply the
 *	sequence of octets which comprise the string, including the
 *	terminating null (0x00) octet.  The terminating null octet is
 *	the only null value in the string.  The character set used to
 *	encode the other string octets is ASCII.
 */

#include "usi.h"

NSPR_BEGIN_EXTERN_C

/* Define a type to reference an attribute record */
typedef unsigned char * ATR_t;

/*
 * Description (USILENGTH)
 *
 *	This macro returns the length of the USI encoding for a specified
 *	unsigned integer value.  The length is the number of octets
 *	required.  It will be greater than zero, and less than or equal
 *	to USIALLOC().  This is a partial inline optimization of
 *	USI_Length().
 */

#define USILENGTH(val)	(((USI_t)(val) <= 0x7f) ? 1 : USI_Length((USI_t)(val)))

/*
 * Description (USIALLOC)
 *
 *	This macro returns the maximum length of an unsigned integer
 *	encoding.
 */

#define USIALLOC()	(5)

/*
 * Description (USIENCODE)
 *
 *	This macro encodes a USI value into a specified buffer.  It
 *	returns a pointer to the first octet after the encoding.
 *	This is a partial inline optimization for USI_Encode().
 */

#define USIENCODE(cp, val) (((USI_t)(val) <= 0x7f) ? (*(cp) = (val), (cp)+1) \
						   : USI_Encode((cp), (val)))

/*
 * Description (USIINSERT)
 *
 *	This macro performs a variation of USIENCODE which always
 *	generates the maximum-sized USI encoding, i.e. the number of
 *	octets indicated by USIALLOC().
 */

#define USIINSERT(cp, val) USI_Insert((ATR_t)(cp), (USI_t)(val))

/*
 * Description (USIDECODE)
 *
 *	This macro decodes a USI value from a specified buffer.  It
 *	returns a pointer to the first octet after the encoding.
 *	This is a partial inline optimization for USI_Decode().
 */

#define USIDECODE(cp, pval) \
	((*(cp) & 0x80) ? USI_Decode((cp), (pval)) \
			: (((pval) ? (*(pval) = *(cp)) : 0), (cp)+1))

/* Define a type to reference a null-terminated string */
typedef unsigned char * NTS_t;

/*
 * Decription (NTSLENGTH)
 *
 *	Return the length, in octets, of a null-terminated string.
 *	It includes the terminating null octet.
 */

#define NTSLENGTH(nts) ((nts) ? strlen((char *)(nts)) + 1 : 1)

/*
 * Description (NTSENCODE)
 *
 *	This macro copies a null-terminated string to a specified
 *	attribute record buffer.  It returns a pointer to the octet
 *	following the NTS in the buffer.
 */

#define NTSENCODE(cp, nts) \
	((ATR_t)memccpy((void *)(cp), \
			(void *)((nts) ? (NTS_t)(nts) : (NTS_t)""), \
			0, NTSLENGTH(nts)))

/*
 * Description (NTSDECODE)
 *
 *	This macro decodes a null-terminated string in a specified
 *	attribute record buffer into a dynamically allocated buffer.
 *	It returns a pointer to the first octet after the NTS in the
 *	attribute record buffer.
 */

#define NTSDECODE(cp, pnts) NTS_Decode((cp), (pnts))

/* Functions in attrec.c */
extern int NTS_Length(NTS_t ntsp);
extern ATR_t NTS_Decode(ATR_t cp, NTS_t * pnts);
extern ATR_t NTS_Encode(ATR_t cp, NTS_t nts);
extern ATR_t USI_Decode(ATR_t cp, USI_t * pval);
extern ATR_t USI_Encode(ATR_t cp, USI_t val);
extern ATR_t USI_Insert(ATR_t cp, USI_t val);
extern int USI_Length(USI_t val);

NSPR_END_EXTERN_C

#endif /* __attrec_h */

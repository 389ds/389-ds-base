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

/* uniqueid.c implementation of entryid functionality */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "slap.h"

#define UIDSTR_SIZE 35 /* size of the string representation of the id */
#define MODULE "uniqueid" /* for logging */

static int isValidFormat (const char * buff);

/* All functions that strat with slapi_ are exposed to the plugins */

/* Function:	slapi_uniqueIDNew
   Description:	creates new Slapi_UniqueID object
   Parameters:	none
   Return:		pointer to the new uId object if successful
				NULL if memory allocation failed
 */

Slapi_UniqueID *slapi_uniqueIDNew ()
{
	Slapi_UniqueID *uId;
	uId = (Slapi_UniqueID*)slapi_ch_malloc (sizeof (Slapi_UniqueID));

	if (uId == NULL)
	{
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "uniqueIDNew: "
						 "failed to allocate new id.\n");
		return NULL;
	}

	memset (uId, 0, sizeof (Slapi_UniqueID));

	return uId;
}

/* Function:	slapi_uniqueIDDestroy
   Description: destroys Slapi_UniqueID objects and sets the pointer to NULL
   Parameters:  uId - id to destroy
   Return:		none
 */
	
void slapi_uniqueIDDestroy (Slapi_UniqueID **uId)
{
	if (uId && *uId)
	{
		slapi_ch_free ((void**)uId);
		*uId = NULL;
	}
}

/* Function:    slapi_uniqueIDCompare
   Description: this function lexically compares two entry ids.
                both Ids must have UUID type.
   Parameters:  uId1, uId2 - ids to compare
   Return:      -1 if uId1 <  uId2
                0  if uId2 == uId2
                1  if uId2 >  uId2
                UID_BADDATA if invalid pointer passed to the function
*/
int slapi_uniqueIDCompare (const Slapi_UniqueID *uId1, const Slapi_UniqueID *uId2){
    if (uId1 == NULL || uId2 == NULL)
	{
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "uniqueIDCompare: "
						 "NULL argument passed to the function.\n");
        return UID_BADDATA;
	}

    return(uuid_compare(uId1, uId2));
}

/* Function:    slapi_uniqueIDCompareString
   Description: this function compares two uniqueids, represented as strings
   Parameters:  uuid1, uuid2 - ids to compare
   Return:      0  if uuid1 == uuid2
                non-zero  if uuid1 != uuid2 or uuid1 == NULL or uuid2 == NULL
*/
int slapi_uniqueIDCompareString(const char *uuid1, const char *uuid2)
{
	int return_value = 0;  /* assume not equal */
	if (NULL != uuid1)
	{
		if (NULL != uuid2)
		{
			if (strcmp(uuid1, uuid2) == 0)
			{
				return_value = 1;
			}
		}
	}
	return return_value;
}

/*  Function:    slapi_uniqueIDFormat
    Description: this function converts Slapi_UniqueID to its string representation.
                 The id format is HHHHHHHH-HHHHHHHH-HHHHHHHH-HHHHHHHH
				 where H is a hex digit. The data will be outputed in the
				 network byte order.
    Parameters:  uId  - entry id
                 buff - buffer in which id is returned; caller must free this
						buffer
    Return:      UID_SUCCESS - function was successfull
                 UID_BADDATA - invalid parameter passed to the function
				 UID_MEMORY_ERROR - failed to allocate the buffer
*/
int slapi_uniqueIDFormat (const Slapi_UniqueID *uId, char **buff){
	guid_t uuid_tmp;
	char *ptr;
	
    if (uId == NULL || buff == NULL)
	{
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "uniqueIDFormat: "
						 "NULL argument passed to the function.\n");
        return UID_BADDATA;
	}

    *buff = (char*)slapi_ch_malloc (UIDSTR_SIZE + 1);
	if (*buff == NULL)
	{
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "uniqueIDFormat: "
						 "failed to allocate buffer.\n");
		return UID_MEMORY_ERROR;
	}

	uuid_tmp = *uId;
	uuid_tmp.time_low = htonl(uuid_tmp.time_low);
    uuid_tmp.time_mid = htons(uuid_tmp.time_mid);
    uuid_tmp.time_hi_and_version = htons(uuid_tmp.time_hi_and_version);

	ptr = slapi_u8_to_hex(((uint8_t *)&uuid_tmp.time_low)[0], *buff, 0);
	ptr = slapi_u8_to_hex(((uint8_t *)&uuid_tmp.time_low)[1], ptr, 0);
	ptr = slapi_u8_to_hex(((uint8_t *)&uuid_tmp.time_low)[2], ptr, 0);
	ptr = slapi_u8_to_hex(((uint8_t *)&uuid_tmp.time_low)[3], ptr, 0);
	*ptr++ = '-';
	ptr = slapi_u8_to_hex(((uint8_t *)&uuid_tmp.time_mid)[0], ptr, 0);
	ptr = slapi_u8_to_hex(((uint8_t *)&uuid_tmp.time_mid)[1], ptr, 0);
	ptr = slapi_u8_to_hex(((uint8_t *)&uuid_tmp.time_hi_and_version)[0], ptr, 0);
	ptr = slapi_u8_to_hex(((uint8_t *)&uuid_tmp.time_hi_and_version)[1], ptr, 0);
	*ptr++ = '-';
	ptr = slapi_u8_to_hex(uuid_tmp.clock_seq_hi_and_reserved, ptr, 0);
	ptr = slapi_u8_to_hex(uuid_tmp.clock_seq_low, ptr, 0);
	ptr = slapi_u8_to_hex(uuid_tmp.node[0], ptr, 0);
	ptr = slapi_u8_to_hex(uuid_tmp.node[1], ptr, 0);
	*ptr++ = '-';
	ptr = slapi_u8_to_hex(uuid_tmp.node[2], ptr, 0);
	ptr = slapi_u8_to_hex(uuid_tmp.node[3], ptr, 0);
	ptr = slapi_u8_to_hex(uuid_tmp.node[4], ptr, 0);
	ptr = slapi_u8_to_hex(uuid_tmp.node[5], ptr, 0);
	*ptr = 0;

	return UID_SUCCESS;    
}

/*  Function:    slapi_uniqueIDScan
    Description: this function converts a string buffer into uniqueID. 
    Parameters:  uId  - unique id to be returned
                 buff - buffer with uniqueID in the format returned by 
                        uniqueIDFormat function
    Return:      UID_SUCCESS - function was successfull
                 UID_BADDATA - null parameter(s) or bad format
*/
int slapi_uniqueIDScan (Slapi_UniqueID *uId, const char *buff){
    if (uId == NULL || buff == NULL)
	{
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "uniqueIDScan: "
						 "NULL argument passed to the function.\n");
        return UID_BADDATA;
	}

	if (!isValidFormat (buff))
	{
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "uniqueIDScan: "
						 "invalid data format.\n");
		return UID_BADDATA;
	}
	  
	((PRUint8 *) &uId->time_low)[0] = slapi_str_to_u8 (&(buff[0]));
	((PRUint8 *) &uId->time_low)[1] = slapi_str_to_u8 (&(buff[2]));
	((PRUint8 *) &uId->time_low)[2] = slapi_str_to_u8 (&(buff[4]));
	((PRUint8 *) &uId->time_low)[3] = slapi_str_to_u8 (&(buff[6]));
	/* next field is at 9 because we skip the - */
	((PRUint8 *) &uId->time_mid)[0] = slapi_str_to_u8 (&(buff[9]));
	((PRUint8 *) &uId->time_mid)[1] = slapi_str_to_u8 (&(buff[11]));
	((PRUint8 *) &uId->time_hi_and_version)[0] = slapi_str_to_u8 (&(buff[13]));
	((PRUint8 *) &uId->time_hi_and_version)[1] = slapi_str_to_u8 (&(buff[15]));
	/* next field is at 18 because we skip the - */
	uId->clock_seq_hi_and_reserved = slapi_str_to_u8 (&(buff[18]));
	uId->clock_seq_low = slapi_str_to_u8 (&(buff[20]));
	uId->node[0] = slapi_str_to_u8 (&(buff[22]));
	uId->node[1] = slapi_str_to_u8 (&(buff[24]));
	/* next field is at 27 because we skip the - */
	uId->node[2] = slapi_str_to_u8 (&(buff[27]));
	uId->node[3] = slapi_str_to_u8 (&(buff[29]));
	uId->node[4] = slapi_str_to_u8 (&(buff[31]));
	uId->node[5] = slapi_str_to_u8 (&(buff[33]));

	uId->time_low = ntohl(uId->time_low);
    uId->time_mid = ntohs(uId->time_mid);
    uId->time_hi_and_version = ntohs(uId->time_hi_and_version);

    return UID_SUCCESS;
}

/* Function:     slapi_uniqueIDIsUUID
   Description:  tests if given entry id is in UUID format
   Parameters:   uId - id to test
   Return        0 - it is UUID
                 1 - it is not UUID
                 UID_BADDATA - invalid data passed to the function
   Note: LPXXX - This call is not used currently. Keep it ???
 */
int slapi_uniqueIDIsUUID (const Slapi_UniqueID *uId){
    if (uId == NULL)
        return UID_BADDATA;    
	/* Shortening Slapi_UniqueID: This call does nothing */
	return (0);
}

/* Name:		slapi_uniqueIDSize
   Description:	returns size of the string version of uniqueID in bytes
   Parameters:  none
   Return:		size of the string version of uniqueID in bytes
 */
int slapi_uniqueIDSize ()
{
	return (UIDSTR_SIZE);
}

/* Name:		slapi_uniqueIDDup
   Description:	duplicates an UniqueID object
   Parameters:	uId - id to duplicate
   Return:		duplicate of the Id
 */
Slapi_UniqueID* slapi_uniqueIDDup (Slapi_UniqueID *uId)
{
	Slapi_UniqueID *uIdDup	= slapi_uniqueIDNew ();
	memcpy (uIdDup, uId, sizeof (Slapi_UniqueID));

	return uIdDup;
}

/* helper functions */

static char* format = "XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX";
static size_t format_len = 35;
/* This function verifies that buff contains data in the correct
   format (specified above). */
static int isValidFormat (const char * buff)
{
	int i;

	if (strlen (buff) != strlen (format))
		return UID_BADDATA;

	for (i = 0; i < format_len; i++)
	{
		if (format[i] == '-' && buff [i] != '-')
			return 0;
		else if (format[i] == 'X' && ! isxdigit (buff[i]))
			return 0;
	}

	return 1;
}


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

/*
 * nterrors.c: Conversion of error numbers to explanation strings
 * 
 * Aruna Victor 12/6/95
 */


#include <windows.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netsite.h>
#include <base/nterrors.h>
#include <base/nterr.h>

struct _NtHashedError {
	int ErrorNumber;
	char *ErrorString;
	struct _NtHashedError *next;
} ;

typedef struct _NtHashedError NtHashedError;

NtHashedError *hashedNtErrors[200];

#define HASH_ERROR_MODULUS 199
#define DEFAULT_ERROR_STRING "Error Number is unknown"

char *
FindError(int error)
{
    NtHashedError *tmp;

    int hashValue = error % HASH_ERROR_MODULUS;
    tmp = hashedNtErrors[hashValue];

    while(tmp) {
        if (tmp->ErrorNumber == error) {
            return tmp->ErrorString;
        }
        tmp = tmp->next;
    }
    return(DEFAULT_ERROR_STRING);
}

void
EnterError(NtHashedError *error)
{
    NtHashedError *tmp;
    int hashValue;
    int number = 199;

    hashValue = error->ErrorNumber % HASH_ERROR_MODULUS;

     if(!(tmp = hashedNtErrors[hashValue])){
        hashedNtErrors[hashValue] = error;
     } else {
        while(tmp->next) {
            tmp = tmp->next;
        }
        tmp->next = error;
    }
}

void
HashNtErrors()
{
    NtHashedError *error;
    int i = 0;
    
    while(NtErrorStrings[i].ErrorString) {
        error = (NtHashedError *)MALLOC(sizeof(NtHashedError));
        error->ErrorNumber = NtErrorStrings[i].ErrorNumber;
        error->ErrorString = NtErrorStrings[i++].ErrorString;
        error->next = NULL;
        EnterError(error);
    }
}
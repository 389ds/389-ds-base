/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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
/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* uuid.h - interface  to uuid layer. UUID is generated in accordance
            with UUIDs and GUIDs IETF draft
 */

/*
** Copyright (c) 1990- 1993, 1996 Open Software Foundation, Inc.
** Copyright (c) 1989 by Hewlett-Packard Company, Palo Alto, Ca. &
** Digital Equipment Corporation, Maynard, Mass.
** Copyright (c) 1998 Microsoft.
** To anyone who acknowledges that this file is provided "AS IS"
** without any express or implied warranty: permission to use, copy,
** modify, and distribute this file for any purpose is hereby
** granted without fee, provided that the above copyright notices and
** this notice appears in all source code copies, and that none of
** the names of Open Software Foundation, Inc., Hewlett-Packard
** Company, or Digital Equipment Corporation be used in advertising
** or publicity pertaining to distribution of the software without
** specific, written prior permission.  Neither Open Software
** Foundation, Inc., Hewlett-Packard Company, Microsoft, nor Digital Equipment
** Corporation makes any representations about the suitability of
** this software for any purpose.
*/


#ifndef UUID_H
#define UUID_H

/* Set this to what your compiler uses for 64 bit data type */
#ifdef _WIN32
#define unsigned64_t unsigned __int64
#define I64(C) C
#else
#define unsigned64_t unsigned long long
#define I64(C) C##LL
#endif

/***** uuid related data types *****/
/* 
 * DBDB These types were broken on 64-bit architectures
 * This file has been modified to fix that problem.
 */
#if defined(__LP64__) || defined (_LP64)
typedef unsigned int   unsigned32;
#else
typedef unsigned long   unsigned32;
#endif
typedef unsigned short  unsigned16;
typedef unsigned char   unsigned8;
typedef unsigned64_t uuid_time_t;
typedef struct 
{
	char nodeID[6];
} uuid_node_t;

typedef struct _guid_t 
{
	unsigned32 time_low;
	unsigned16 time_mid;
	unsigned16 time_hi_and_version;
	unsigned8  clock_seq_hi_and_reserved;
	unsigned8  clock_seq_low;
	PRUint8    node[6];
} guid_t;

enum
{
    UUID_SUCCESS,   	/* operation succeded */
    UUID_IO_ERROR,		/* file I/O failed */
    UUID_LOCK_ERROR,	/* lock creation failed */
    UUID_TIME_ERROR,	/* ran out of time sequence numbers, need 
						   time update to generate the id          */
    UUID_BAD_FORMAT,	/* data in a string buffer is not in UUID format */
	UUID_MEMORY_ERROR,	/* memory allocation failed */
	UUID_LDAP_ERROR,	/* LDAP operation failed */
	UUID_NOTFOUND,		/* generator state is missing */
	UUID_FORMAT_ERROR,	/* state entry does not contain right data */
	UUID_UNKNOWN_ERROR
};

/***** uuid interface *****/
  
/* uuid_init -- initialize uuid layer */
int  uuid_init (const char *configDir, const Slapi_DN *configDN, PRBool mtGen);

/* uuid_cleanup -- cleanup of uuid layer */
void uuid_cleanup ();

/* uuid_create -- generate a UUID */
int uuid_create(guid_t *uuid);

/* uuid_compare --  Compare two UUID's "lexically" and return
					  -1   u1 is lexically before u2
					   0   u1 is equal to u2
					   1   u1 is lexically after u2
   Note:   lexical ordering is not temporal ordering!
*/
int uuid_compare(const guid_t *u1, const guid_t *u2);

/* uuid_format -- converts UUID to its string representation and stores it
                  in the buff. buff must be at list 40 bytes long
*/
void uuid_format(const guid_t *u, char *buff);

/* uuid_create_from_name -- create a UUID using a "name" from a "name space" */
void uuid_create_from_name(guid_t * uuid,        /* resulting UUID */
						   const guid_t nsid,    /* UUID to serve as context, so identical
												    names from different name spaces generate
                                                    different UUIDs */
						   const void * name,    /* the name from which to generate a UUID */
						   int namelen);         /* the length of the name */
#endif

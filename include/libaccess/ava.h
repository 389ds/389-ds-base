/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _ava_h
#define _ava_h

#define ENTRIES_ALLOCSIZE 100
#define ORGS_ALLOCSIZE    15


#ifdef XP_WIN32
#define NSAPI_PUBLIC __declspec(dllexport)
#else /* !XP_WIN32 */
#define NSAPI_PUBLIC
#endif


typedef struct {
  char *email;
  char *locality;
  char *userid; 
  char *state;
  char *country;
  char *company;
  int numOrgs;
  char **organizations;
  char *CNEntry;
} AVAEntry;

typedef struct {
  char *userdb;
  int numEntries;
  AVAEntry **enteredTable; 
} AVATable;


#endif


/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libaccess/ava.h"
#include "libaccess/avadb.h"
#include "base/session.h"
#include "base/pblock.h"
#include "frame/req.h"
#include "frame/log.h"

#include "libadmin/libadmin.h"
#include "libaccess/avapfile.h"

#define DB_NAME "AvaMap"

enum {AVA_DB_SUCCESS=0,AVA_DB_FAILURE}; 

#ifdef XP_UNIX
#include "mcom_ndbm.h" 

USE_NSAPI int AddEntry (char *key, char *value) {
  datum keyd;
  datum valued;
  DBM *db = NULL;
  char dbpath[150];

  sprintf (dbpath, "%s%c%s", get_httpacl_dir(), FILE_PATHSEP, DB_NAME);

  db = dbm_open (dbpath, O_RDWR | O_CREAT, 0644);

  if (!db) 
    return AVA_DB_FAILURE;

  keyd.dptr = key;
  keyd.dsize = strlen (key) + 1;

  valued.dptr = value;
  valued.dsize = strlen(value) + 1;

  dbm_store (db, keyd, valued, DBM_REPLACE);
  dbm_close (db);
  
  return AVA_DB_SUCCESS;
}

USE_NSAPI int DeleteEntry (char *key) {
  datum keyd;
  DBM *db = NULL;
  char dbpath[150];

  sprintf (dbpath, "%s%c%s", get_httpacl_dir(), FILE_PATHSEP, DB_NAME);

  db = dbm_open (dbpath, O_RDWR, 0644);

  if (!db) 
    return AVA_DB_FAILURE;

  keyd.dptr = key;
  keyd.dsize = strlen (key) + 1;

  dbm_delete (db, keyd);

  dbm_close (db);
    
  return AVA_DB_SUCCESS;
}

USE_NSAPI char *GetValue (char *key) {
  datum keyd;
  datum valued;
  DBM *db = NULL;
  char dbpath[150];

  sprintf (dbpath, "%s%c%s", get_httpacl_dir(), FILE_PATHSEP, DB_NAME);

  db = dbm_open (dbpath, O_RDONLY, 0644);

  if (!db) 
    return NULL;

  keyd.dptr = key;
  keyd.dsize = strlen (key) + 1;  

  valued = dbm_fetch (db, keyd);

  dbm_close (db);

  return valued.dptr;
}

#else

#include <stdio.h>


#define lmemcpy memcpy
#define lmemcmp memcmp
#define lmemset memset

static int mkhash8(char *x,int len) {
   unsigned int i,hash = 0;
   for (i=0; i < len; i++) { hash += x[i]; }

   return (int) (hash & 0xff);
}

static void mkpath(char *target, char *dir, char sep, char *name) {
    int len;

    len = strlen(dir);
    lmemcpy(target,dir,len);
    target += len;

    *target++ = sep;

    len = strlen(name);
    lmemcpy(target,name,len);
    target += len;

    *target = 0;
}

#define DELETED_LEN 8
static char DELETED[] = { 0xff, 0x0, 0xff, 0x0, 0xff, 0x0, 0xff , 0x0 };


#define RECORD_SIZE 512
USE_NSAPI int AddEntry (char *key, char *value) {
  int empty, hash;
  char dbpath[150];
  char record[RECORD_SIZE];
  int key_len, val_len,size;
  FILE *f;

  mkpath (dbpath, get_httpacl_dir(), FILE_PATHSEP, DB_NAME);

  f = fopen(dbpath, "rb+");
  if (f == NULL) {
	f = fopen(dbpath,"wb+");
  }

  if (f == NULL) 
    return AVA_DB_FAILURE;

  key_len = strlen(key)+1;
  val_len = strlen(value);

  if ((key_len+val_len) > RECORD_SIZE) {
    fclose(f);
    return AVA_DB_FAILURE;
  }


  /* now hash the key */
  hash = mkhash8(key,key_len);
  empty = -1;

  fseek(f,hash*RECORD_SIZE,SEEK_SET);

  for (;;) {
    size= fread(record,1,RECORD_SIZE,f);
    if (size < RECORD_SIZE) {
       break;
    }
    if (lmemcmp(record,key,key_len) == 0) {
       break;
    }
    if ((empty == -1) && (lmemcmp(record,DELETED,DELETED_LEN) == 0)) {
       empty = hash;
    }
    if (record == 0) {
       break;
    }
    hash++;
  }

  if (empty != -1) { hash = empty; }
  fseek(f,hash*RECORD_SIZE,SEEK_SET);

  /* build the record */
  lmemset(record,0,RECORD_SIZE);

  lmemcpy(record,key,key_len);
  lmemcpy(&record[key_len],value,val_len);
  size= fwrite(record,1,RECORD_SIZE,f);
  if (size != RECORD_SIZE) {
    fclose(f);
    return AVA_DB_FAILURE;
  }
  fclose(f);

  return AVA_DB_SUCCESS;
}

USE_NSAPI int DeleteEntry (char *key) {
  int found,hash;
  char dbpath[150];
  char record[RECORD_SIZE];
  int key_len,size;
  FILE *f;

  mkpath (dbpath, get_httpacl_dir(), FILE_PATHSEP, DB_NAME);

  f = fopen(dbpath, "rb+");

  if (f == NULL) 
    return AVA_DB_FAILURE;

  key_len = strlen(key)+1;


  /* now hash the key */
  hash = mkhash8(key,key_len);
  found = 0;
  fseek(f,hash*RECORD_SIZE,SEEK_SET);

  for (;;) {
    size= fread(record,1,RECORD_SIZE,f);
    if (size < RECORD_SIZE) {
       break;
    }
    if (lmemcmp(record,key,key_len) == 0) {
       found++;
       break;
    }
    if (record == 0) {
       break;
    }
    hash++;
  } 

  if (!found) {
    fclose(f);
    return AVA_DB_SUCCESS;
  }
  fseek(f,hash*RECORD_SIZE,SEEK_SET);

  /* build the record */
  lmemset(record,0,RECORD_SIZE);

  lmemcpy(record,DELETED,DELETED_LEN);
  size= fwrite(record,1,RECORD_SIZE,f);
  if (size != RECORD_SIZE) {
    fclose(f);
    return AVA_DB_FAILURE;
  }
  fclose(f);
    
  return AVA_DB_SUCCESS;
}

USE_NSAPI char *GetValue (char *key) {
  int hash,size;
  char dbpath[150];
  char record[RECORD_SIZE];
  int key_len,found = 0;
  FILE *f;

  mkpath (dbpath, get_httpacl_dir(), FILE_PATHSEP, DB_NAME);

  f = fopen(dbpath, "rb");

  if (f == NULL) 
    return NULL;

  key_len = strlen(key)+1;

  /* now hash the key */
  hash = mkhash8(key,key_len);

  fseek(f,hash*RECORD_SIZE,SEEK_SET);

  for(;;) {
    size= fread(record,1,RECORD_SIZE,f);
    if (size < RECORD_SIZE) {
       break;
    }
    if (lmemcmp(record,key,key_len) == 0) {
       found++;
       break;
    }
    if (record == 0) {
       break;
    }
    hash++;
  } 

  fclose(f);
  if (!found) return NULL;

  return system_strdup(&record[key_len+1]);
}

#endif

/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libaccess/ava.h"

#include "base/session.h"
#include "base/pblock.h"
#include "frame/req.h"
#include "frame/log.h"

#include "libadmin/libadmin.h"
#include "libaccess/avapfile.h"

#define ALLOC_SIZE 20
#define SUCCESS 0

struct parsedStruct {
  char     *fileName;
  AVATable *avaTable; 
};

typedef struct parsedStruct Parsed;

/* globals for yy_error if needed */
Session *yy_sn = NULL;
Request *yy_rq = NULL;

/*This will be a dynamic array of parsedStruct*. Re-sizing if necessary.*/
struct ParsedTable {
  Parsed **parsedTable;
  int numEntries;
};

char *currFile;

static struct ParsedTable parsedFiles = {NULL, 0};

extern AVATable entryTable; /*Table where entries are stored*/
extern AVAEntry tempEntry;  /*Used to restore parser's state*/
extern linenum;

AVAEntry * AVAEntry_Dup(AVAEntry *entry) {
  int i;
  AVAEntry *newAVA = NULL;
/* copy the AVA entry */

  if (entry) {
    newAVA = (AVAEntry *) PERM_MALLOC(sizeof(AVAEntry));
    memset(newAVA,0, sizeof(AVAEntry));
    newAVA->userid = 0;
    newAVA->CNEntry = 0;
    newAVA->email = 0;
    newAVA->locality = 0;
    newAVA->state = 0;
    newAVA->country = 0;
    newAVA->company = 0;
    newAVA->organizations  = 0;
    newAVA->numOrgs = 0;
    if (entry->userid) newAVA->userid = PERM_STRDUP(entry->userid);
    if (entry->CNEntry) newAVA->CNEntry = PERM_STRDUP(entry->CNEntry);
    if (entry->email) newAVA->email = PERM_STRDUP(entry->email);
    if (entry->locality) newAVA->locality = PERM_STRDUP(entry->locality);
    if (entry->state) newAVA->state = PERM_STRDUP(entry->state);
    if (entry->country) newAVA->country = PERM_STRDUP(entry->country);
    if (entry->company) newAVA->company = PERM_STRDUP(entry->company);
    if (entry->organizations) {
      newAVA->organizations = PERM_MALLOC(sizeof(char *)*entry->numOrgs);
      newAVA->numOrgs = entry->numOrgs;
      for (i=0; i<entry->numOrgs; i++)
	newAVA->organizations[i] = PERM_STRDUP (entry->organizations[i]);
    }
  }
  return newAVA;
}

void _addAVAtoTable (AVAEntry *newAVA, AVATable *table) {
  int i;
  int insertIndex = -1;

  if (table->numEntries%ENTRIES_ALLOCSIZE == 0) {
    if (table->numEntries == 0) {
      table->enteredTable = 
	(AVAEntry**) PERM_MALLOC  (sizeof(AVAEntry*) * ENTRIES_ALLOCSIZE);
    } else {
      AVAEntry **temp;
      
      temp = 
       PERM_MALLOC(sizeof(AVAEntry*)*(table->numEntries+ENTRIES_ALLOCSIZE));
      memmove(temp, table->enteredTable, sizeof(AVAEntry*)*table->numEntries);
      PERM_FREE(table->enteredTable);
      table->enteredTable = temp;
    }
  }

  for (i=table->numEntries-1; i >= 0; i--) {
    if (strcmp(newAVA->userid, table->enteredTable[i]->userid) >  0) {
      insertIndex = i+1;
      break;
    } else {
      table->enteredTable[i+1] = table->enteredTable[i];
    }
  }

  
  table->enteredTable[(insertIndex == -1) ? 0 : insertIndex] = newAVA;
  (table->numEntries)++;
}

AVATable *AVATableDup(AVATable *table) {
  AVATable *newTable = (AVATable*)PERM_MALLOC (sizeof(AVATable));
  /* round the puppy so _addAVAtoTable still works */
  int size = (table->numEntries + (ENTRIES_ALLOCSIZE-1))/ENTRIES_ALLOCSIZE;
  int i;

  newTable->enteredTable = 
	(AVAEntry**)PERM_MALLOC(size*ENTRIES_ALLOCSIZE*sizeof(AVAEntry *));

  for (i=0; i < table->numEntries; i++) {
	newTable->enteredTable[i] = AVAEntry_Dup(table->enteredTable[i]);
  }
  newTable->numEntries = table->numEntries;
  return newTable;
}

   
 

AVAEntry *_getAVAEntry(char *groupName, AVATable *mapTable) {
  char line[BIG_LINE];
  int lh, rh, mid, cmp;;

  if (!mapTable) {
      sprintf (line, "NULL Pointer passed as mapTable when trying to get entry %s", groupName);
      report_error (SYSTEM_ERROR, "File Not Found", line);
  }
    

  lh = 0;
  rh = mapTable->numEntries-1;

  while (lh <= rh) {
    mid = lh + ((rh-lh)/2);
    cmp = strcmp(groupName, mapTable->enteredTable[mid]->userid);
    if (cmp == 0)
      return mapTable->enteredTable[mid];
    else if (cmp > 0)
      lh = mid + 1;
    else
      rh = mid - 1;
  }

  return NULL;

} 

AVATable *_getTable (char *fileName) {
  int lh, rh, mid, cmp;
  AVATable *table = NULL;

  /*First checks to see if it's already been parsed*/

  lh = 0;
  rh = parsedFiles.numEntries-1;
  while (lh <= rh) {
    mid = lh + ((rh - lh)/2);
    cmp = strcmp(fileName, parsedFiles.parsedTable[mid]->fileName);
    if (cmp == SUCCESS) {
      return parsedFiles.parsedTable[mid]->avaTable;
    } else if (cmp < SUCCESS) {
      rh = mid-1;
    } else {
      lh = mid+1;
    }
  }

  yyin = fopen (fileName, "r");

  if (yyin) {
    if (!yyparse()) {
      table = _wasParsed (fileName);
      table->userdb = NULL;
    }
    fclose (yyin);
  }

  return table;
}

int _hasBeenParsed (char *aclFileName){
  return (_getTable(aclFileName) != NULL);
}

AVATable* _wasParsed (char *inFileName) {
  Parsed *newEntry;
  int i;

  if (!inFileName)
    return NULL;

  newEntry = (Parsed*) PERM_MALLOC (sizeof(Parsed));
  newEntry->fileName = PERM_STRDUP (inFileName);
  newEntry->avaTable = AVATableDup(&entryTable);

  if (parsedFiles.numEntries % ALLOC_SIZE == 0) {
    if (parsedFiles.numEntries) {
      Parsed **temp;

      temp = PERM_MALLOC (sizeof(Parsed*)*(parsedFiles.numEntries + ALLOC_SIZE));
      if (!temp)
	return NULL;
      memcpy (temp, parsedFiles.parsedTable, sizeof(Parsed*)*parsedFiles.numEntries);
      PERM_FREE (parsedFiles.parsedTable);
      parsedFiles.parsedTable = temp;
    } else {
      parsedFiles.parsedTable = 
	(Parsed**) PERM_MALLOC (sizeof (Parsed*) * ALLOC_SIZE);
      if (!parsedFiles.parsedTable)
	return NULL;
    }
  } 
  for (i=parsedFiles.numEntries; i > 0; i--) {
    if (strcmp(newEntry->fileName,parsedFiles.parsedTable[i-1]->fileName) < 0) {
      parsedFiles.parsedTable[i] = parsedFiles.parsedTable[i-1];
    } else {
      break;
    }
  }
  parsedFiles.parsedTable[i] = newEntry;
  parsedFiles.numEntries++;
  
/*Initialize parser structures to resemble that before parse*/
  entryTable.numEntries = 0;
  tempEntry.country = tempEntry.company = tempEntry.CNEntry = NULL;
  tempEntry.email = tempEntry.locality = tempEntry.state = NULL; 
  linenum = 1;

  return newEntry->avaTable;
}

AVAEntry *_deleteAVAEntry (char *group, AVATable *table) {
  int removeIndex;
  int lh, rh, mid, cmp;
  AVAEntry *entry = NULL;

  if (!group || !table)
    return NULL;

  lh = 0;
  rh = table->numEntries - 1;

  while (lh <= rh) {
    mid = lh + ((rh-lh)/2);
    cmp = strcmp (group, table->enteredTable[mid]->userid);
    if (cmp == SUCCESS) {
      removeIndex = mid;
      break;
    } else if (cmp < SUCCESS) {
      rh = mid-1;
    } else {
      lh = mid+1;
    }
  }

  if (lh > rh)
    return NULL;
  
  entry = table->enteredTable[removeIndex];

  memmove ((char*)(table->enteredTable)+(sizeof(AVAEntry*)*removeIndex),
	   (char*)(table->enteredTable)+(sizeof(AVAEntry*)*(removeIndex+1)),
	   (table->numEntries - removeIndex - 1)*sizeof(AVAEntry*));
  
  (table->numEntries)--;

  return entry;
}

void AVAEntry_Free (AVAEntry *entry) {
  int i;

  if (entry) {
    if (entry->userid)
      PERM_FREE (entry->userid);
    if (entry->CNEntry)
      PERM_FREE (entry->CNEntry);
    if (entry->email)
      PERM_FREE (entry->email);
    if (entry->locality)
      PERM_FREE (entry->locality);
    if (entry->state)
      PERM_FREE (entry->state);
    if (entry->country)
      PERM_FREE (entry->country);
    if (entry->company)
      PERM_FREE (entry->company);
    if (entry->organizations) {
      for (i=0; i<entry->numOrgs; i++)
	PERM_FREE (entry->organizations[i]);
      PERM_FREE(entry->organizations);
    }
  }
}

void PrintHeader(FILE *outfile){

  fprintf (outfile,"/*This file is generated automatically by the admin server\n");
  fprintf (outfile," *Any changes you make manually may be lost if other\n");
  fprintf (outfile," *changes are made through the admin server.\n");
  fprintf (outfile," */\n\n\n");

}

void writeOutEntry (FILE *outfile, AVAEntry *entry) {
  int i;

  /*What should I do if the group id is not there?*/
  if (!entry || !(entry->userid))
    report_error (SYSTEM_ERROR, "AVA-DB Failure",
		  "Bad entry passed to write out function");

  fprintf (outfile,"%s: {\n", entry->userid);
  if (entry->CNEntry)
    fprintf (outfile,"\tCN=\"%s\"\n", entry->CNEntry);
  if (entry->email)
    fprintf (outfile,"\tE=\"%s\"\n", entry->email);
  if (entry->company)
    fprintf (outfile,"\tO=\"%s\"\n", entry->company);
  if (entry->organizations) {
    for (i=0; i < entry->numOrgs; i++) {
      fprintf (outfile, "\tOU=\"%s\"\n", entry->organizations[i]);
    }
  }
  if (entry->locality)
    fprintf (outfile,"\tL=\"%s\"\n",entry->locality);
  if (entry->state)
    fprintf (outfile,"\tST=\"%s\"\n",entry->state);
  if (entry->country)
    fprintf (outfile,"\tC=\"%s\"\n", entry->country);

  fprintf (outfile,"}\n\n\n");

}

void writeOutFile (char *authdb, AVATable *table) {
  char line[BIG_LINE];
  char mess[200];
  FILE *newfile;
  int i;

  sprintf (line, "%s%c%s%c%s.%s", get_authdb_dir(), FILE_PATHSEP, authdb, FILE_PATHSEP,
	   AUTH_DB_FILE, AVADB_TAG);

  if (!table) {
    sprintf (mess, "The structure for file %s was not loaded before writing out", line);
    report_error (SYSTEM_ERROR, "Internal Error", mess);
  }

  newfile = fopen (line, "w");

  if (!newfile) {
    sprintf (mess, "Could not open file %s for writing.", line);
    report_error(FILE_ERROR, "No File", mess);
  }

  PrintHeader (newfile);

  for (i=0;i < table->numEntries; i++) {
    writeOutEntry (newfile, table->enteredTable[i]);
  }

  fclose(newfile);
}


void
logerror(char *error,int line,char *file) {
  /* paranoia */
  /*ava-mapping is only functin that initializes yy_sn and yy_rq*/
  if ((yy_sn != NULL) && (yy_rq != NULL)) {
    log_error (LOG_FAILURE, "ava-mapping", yy_sn, yy_rq,
	       "Parse error line %d of %s: %s", line, file, error); 
  } else {
    char errMess[250];

    sprintf (errMess, "Parse error line %d of %s: %s", line, file, error);
    report_error (SYSTEM_ERROR, "Failure: Loading AVA-DB Table", errMess);
  }
}


void outputAVAdbs(char *chosen) {
  char *authdbdir = get_authdb_dir();
  char **listings;
  int i;
  int numListings = 0;
  int hasOptions = 0;
 
  listings = list_auth_dbs(authdbdir);
  
  while (listings[numListings++] != NULL);

  for (i=0; listings[i] != NULL ; i++) {
    if (!hasOptions) {
      printf ("<select name=\"%s\"%s onChange=\"form.submit()\">",AVA_DB_SEL,
	      (numListings > SELECT_OVERFLOW)?"size=5":"");
      hasOptions = 1;
    }

    printf ("<option value=\"%s\"%s>%s\n",listings[i],
	   (strcmp(chosen, listings[i]) == 0) ? "SELECTED":"",listings[i]);
  }

  if (hasOptions) 
    printf ("</select>\n");
  else 
    printf ("<i><b>Insert an AVA-Database entry first</b></i>\n");/*This should never happen,
								   *since I never create an empty
								   *avadb file,
								   *but one never knows
								   */

}

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
%{

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "libaccess/ava.h"
#include "libaccess/avapfile.h"
#include "netsite.h"

extern int linenum;
extern char yytext[];

static void AddDefType (int defType, char *defId);
static void AddAVA (char* userID);

void yyerror(const char* string);
extern void logerror(const char* string,int num, char *file);

AVAEntry tempEntry;
AVATable entryTable;

%}

%union {
  char *string;
  int  num;
}

%token DEF_C DEF_CO DEF_OU DEF_CN EQ_SIGN DEF_START
%token DEF_L DEF_E DEF_ST
%token <string> USER_ID DEF_ID

%type <num> def.type

%start source

%%

source: ava.database
     |
     ;


ava.database: ava.database ava 
     |        ava              
     ;

ava: USER_ID definitions  {AddAVA($1);};

definitions: definition.list
     |
     ;

definition.list: definition.list definition
     |           definition                   
     ;


definition: def.type EQ_SIGN DEF_ID {AddDefType($1, $3);};

def.type: DEF_C    {$$ = DEF_C; }
     |    DEF_CO   {$$ = DEF_CO;}
     |    DEF_OU   {$$ = DEF_OU;}
     |    DEF_CN   {$$ = DEF_CN;}
     |    DEF_L    {$$ = DEF_L; }
     |    DEF_E    {$$ = DEF_E; }
     |    DEF_ST   {$$ = DEF_ST;}
     ;

%%

void yyerror(const char* string) {
 logerror(string,linenum,currFile);
}


void AddDefType (int defType, char *defId) {
  switch (defType) {
    case DEF_C:
      tempEntry.country = defId;
      break;
    case DEF_CO:
      tempEntry.company = defId;
      break;
    case DEF_OU:
      if (tempEntry.numOrgs % ORGS_ALLOCSIZE == 0) {
	if (tempEntry.numOrgs == 0) {
	  tempEntry.organizations =
	    PERM_MALLOC  (sizeof (char*) * ORGS_ALLOCSIZE);
	} else {
	  char **temp;
	  temp = 
	    PERM_MALLOC(sizeof(char*) * (tempEntry.numOrgs + ORGS_ALLOCSIZE));
	  memcpy (temp, tempEntry.organizations, 
		  sizeof(char*)*tempEntry.numOrgs);
	  PERM_FREE (tempEntry.organizations);
	  tempEntry.organizations = temp;
	}
      }
      tempEntry.organizations[tempEntry.numOrgs++] = defId;
      break;
    case DEF_CN:
      tempEntry.CNEntry = defId;
      break; 
    case DEF_E:
      tempEntry.email = defId;
      break;
    case DEF_L:
      tempEntry.locality = defId;
      break;
    case DEF_ST:
      tempEntry.state = defId;
      break;
    default:
      break;
  }
}

void AddAVA (char* userID) {
  AVAEntry *newAVA;

  newAVA = (AVAEntry*)PERM_MALLOC(sizeof(AVAEntry));
  if (!newAVA) {
    yyerror ("Out of Memory in AddAVA");
    return;
  }
  *newAVA = tempEntry;
  newAVA->userid = userID;

  _addAVAtoTable (newAVA, &entryTable);

  tempEntry.CNEntry = tempEntry.userid = tempEntry.country = tempEntry.company = 0;
  tempEntry.email = tempEntry.locality = tempEntry.state = NULL;
  tempEntry.numOrgs = 0;
}

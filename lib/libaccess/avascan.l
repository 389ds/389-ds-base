/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
%{
 
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "y.tab.h"
#include "libaccess/ava.h"
#include "netsite.h"
 
int linenum = 1;
int first_time = 1;
int old_state;
int num_nested_comments = 0;

extern AVAEntry tempEntry;   
extern AVATable entryTable;  
                             
void strip_quotes(void);
 
%}
 
%s  COMMENT NORM DEFINES DEF_TYPE 
 
uc_letter                           [A-Z]                               
lc_letter                           [a-z]
digit                               [0-9]
under_score                         _

letter                              ([A-Z,a-z])

white_space                         ([ \t]*)
identifier                          ([_,A-Z,a-z][_,A-Z,a-z,0-9]*)
def_identifier                      (({white_space}{identifier})+)
text                                (\"[^\"]*\")
comments                            (([^"*/"\n])*)



%%

%{
   if (first_time) {
      BEGIN NORM;
      first_time = tempEntry.numOrgs = 0;
      old_state = NORM;
      tempEntry.userid = 0;
      tempEntry.country = 0;
      tempEntry.CNEntry = 0;
      tempEntry.email = 0;
      tempEntry.locality = 0;
      tempEntry.state = 0;
      entryTable.numEntries = 0;
   }
%}


"/*"                  {BEGIN COMMENT; num_nested_comments++;}
<COMMENT>"*/"         {num_nested_comments--; 
                       if (!num_nested_comments) BEGIN old_state;}
<COMMENT>.   {;}

<NORM>{identifier}  {yylval.string = PERM_STRDUP(yytext);
			 return USER_ID;}
<NORM>":"{white_space}\{ {BEGIN DEF_TYPE;
                          old_state = DEF_TYPE;}

<DEF_TYPE>"C"               {BEGIN DEFINES; old_state = DEFINES;
                             return DEF_C; }
<DEF_TYPE>"O"               {BEGIN DEFINES; old_state = DEFINES;
                             return DEF_CO;}
<DEF_TYPE>"OU"              {BEGIN DEFINES; old_state = DEFINES;
                             return DEF_OU;}
<DEF_TYPE>"CN"              {BEGIN DEFINES; old_state = DEFINES;
                             return DEF_CN;}
<DEF_TYPE>"L"               {BEGIN DEFINES; old_state = DEFINES;
                             return DEF_L;}
<DEF_TYPE>"E"               {BEGIN DEFINES; old_state = DEFINES;
                             return DEF_E;}
<DEF_TYPE>"ST"              {BEGIN DEFINES; old_state = DEFINES;
                             return DEF_ST;}
<DEF_TYPE>"}"               {BEGIN NORM;old_state = NORM;}

<DEFINES>=                     {return EQ_SIGN;}
<DEFINES>{text}  {BEGIN DEF_TYPE; old_state = DEF_TYPE;
                                strip_quotes();
                                return DEF_ID;}

{white_space}   {;}
\n              {linenum++;}
.               {yyerror("Bad input character");} 
%%

int yywrap () {
 return 1;
}

void strip_quotes(void) {
  yytext[strlen(yytext)-1]= '\0';
  yylval.string = PERM_STRDUP(&yytext[1]);
}

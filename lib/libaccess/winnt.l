/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include <stdio.h>
# define U(x) x
# define NLSTATE yyprevious=YYNEWLINE
# define BEGIN yybgin = yysvec + 1 +
# define INITIAL 0
# define YYLERR yysvec
# define YYSTATE (yyestate-yysvec-1)
# define YYOPTIM 1
# define YYLMAX BUFSIZ
#ifndef __cplusplus
# define output(c) (void)putc(c,yyout)
#else
# define lex_output(c) (void)putc(c,yyout)
#endif

#if defined(__cplusplus) || defined(__STDC__)

#if defined(__cplusplus) && defined(__EXTERN_C__)
extern "C" {
#endif
	int yyback(int *, int);
	int yyinput(void);
	int yylook(void);
	void yyoutput(int);
	int yyracc(int);
	int yyreject(void);
	void yyunput(int);
	int yylex(void);
#ifdef YYLEX_E
	void yywoutput(wchar_t);
	wchar_t yywinput(void);
#endif
#ifndef yyless
	void yyless(int);
#endif
#ifndef yywrap
	int yywrap(void);
#endif
#ifdef LEXDEBUG
	void allprint(char);
	void sprint(char *);
#endif
#if defined(__cplusplus) && defined(__EXTERN_C__)
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
	void exit(int);
#ifdef __cplusplus
}
#endif

#endif
# define unput(c) {yytchar= (c);if(yytchar=='\n')yylineno--;*yysptr++=yytchar;}
# define yymore() (yymorfg=1)
#ifndef __cplusplus
# define input() (((yytchar=yysptr>yysbuf?U(*--yysptr):getc(yyin))==10?(yylineno++,yytchar):yytchar)==EOF?0:yytchar)
#else
# define lex_input() (((yytchar=yysptr>yysbuf?U(*--yysptr):getc(yyin))==10?(yylineno++,yytchar):yytchar)==EOF?0:yytchar)
#endif
#define ECHO fprintf(yyout, "%s",yytext)
# define REJECT { nstr = yyreject(); goto yyfussy;}
int yyleng; extern char yytext[];
int yymorfg;
extern char *yysptr, yysbuf[];
int yytchar;
FILE *yyin = NULL, *yyout = NULL;
extern int yylineno;
struct yysvf { 
	struct yywork *yystoff;
	struct yysvf *yyother;
	int *yystops;};
struct yysvf *yyestate;
extern struct yysvf yysvec[], *yybgin;

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "y.tab.h"
#include "libaccess/ava.h"
/*#include "netsite.h" */

int linenum = 1;
int first_time = 1;
int old_state;
int num_nested_comments = 0;

extern AVAEntry tempEntry;
extern AVATable entryTable;

void strip_quotes(void);

# define COMMENT 2
# define NORM 4
# define DEFINES 6
# define DEF_TYPE 8
# define YYNEWLINE 10
yylex(){
int nstr; extern int yyprevious;

	if (yyin == NULL) yyin = stdin;
	if (yyout == NULL)  yyout = stdout;
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
#ifdef __cplusplus
/* to avoid CC and lint complaining yyfussy not being used ...*/
static int __lex_hack = 0;
if (__lex_hack) goto yyfussy;
#endif
while((nstr = yylook()) >= 0)
yyfussy: switch(nstr){
case 0:
if(yywrap()) return(0); break;
case 1:

# line 58 "avascan.l"
                 {BEGIN COMMENT; num_nested_comments++;}
break;
case 2:

# line 59 "avascan.l"
        {num_nested_comments--; 
                       if (!num_nested_comments) BEGIN old_state;}
break;
case 3:

# line 61 "avascan.l"
  {;}
break;
case 4:

# line 63 "avascan.l"
 {yylval.string = system_strdup(yytext);
			 return USER_ID;}
break;
case 5:

# line 65 "avascan.l"
{BEGIN DEF_TYPE;
                          old_state = DEF_TYPE;}
break;
case 6:

# line 68 "avascan.l"
              {BEGIN DEFINES; old_state = DEFINES;
                             return DEF_C; }
break;
case 7:

# line 70 "avascan.l"
              {BEGIN DEFINES; old_state = DEFINES;
                             return DEF_CO;}
break;
case 8:

# line 72 "avascan.l"
             {BEGIN DEFINES; old_state = DEFINES;
                             return DEF_OU;}
break;
case 9:

# line 74 "avascan.l"
             {BEGIN DEFINES; old_state = DEFINES;
                             return DEF_CN;}
break;
case 10:

# line 76 "avascan.l"
              {BEGIN DEFINES; old_state = DEFINES;
                             return DEF_L;}
break;
case 11:

# line 78 "avascan.l"
              {BEGIN DEFINES; old_state = DEFINES;
                             return DEF_E;}
break;
case 12:

# line 80 "avascan.l"
             {BEGIN DEFINES; old_state = DEFINES;
                             return DEF_ST;}
break;
case 13:

# line 82 "avascan.l"
              {BEGIN NORM;old_state = NORM;}
break;
case 14:

# line 84 "avascan.l"
                    {return EQ_SIGN;}
break;
case 15:

# line 85 "avascan.l"
 {BEGIN DEF_TYPE; old_state = DEF_TYPE;
                                strip_quotes();
                                return DEF_ID;}
break;
case 16:

# line 89 "avascan.l"
  {;}
break;
case 17:

# line 90 "avascan.l"
             {linenum++;}
break;
case 18:

# line 91 "avascan.l"
              {yyerror("Bad input character");}
break;
case -1:
break;
default:
(void)fprintf(yyout,"bad switch yylook %d",nstr);
} return(0); }
/* end of yylex */

int yywrap () {
 return 1;
}

void strip_quotes(void) {
  yytext[strlen(yytext)-1]= '\0';
  yylval.string = system_strdup(&yytext[1]);
}
int yyvstop[] = {
0,

16,
0,

16,
0,

16,
0,

16,
0,

16,
0,

16,
0,

16,
0,

16,
0,

16,
0,

16,
0,

18,
0,

16,
18,
0,

17,
0,

18,
0,

3,
18,
0,

3,
16,
18,
0,

3,
18,
0,

3,
18,
0,

4,
18,
0,

18,
0,

18,
0,

14,
18,
0,

6,
18,
0,

11,
18,
0,

10,
18,
0,

7,
18,
0,

18,
0,

13,
18,
0,

16,
0,

1,
0,

2,
0,

4,
0,

5,
0,

15,
0,

9,
0,

8,
0,

12,
0,
0};
# define YYTYPE unsigned char
struct yywork { YYTYPE verify, advance; } yycrank[] = {
0,0,	0,0,	1,11,	0,0,	
0,0,	0,0,	0,0,	0,0,	
0,0,	0,0,	1,12,	1,13,	
0,0,	3,15,	12,29,	0,0,	
20,33,	0,0,	0,0,	0,0,	
0,0,	3,16,	3,13,	0,0,	
0,0,	0,0,	0,0,	0,0,	
0,0,	0,0,	0,0,	0,0,	
0,0,	9,11,	0,0,	1,11,	
0,0,	12,29,	7,21,	20,33,	
8,21,	9,12,	9,13,	14,30,	
0,0,	1,11,	3,15,	4,17,	
1,14,	1,11,	2,14,	7,14,	
4,18,	8,14,	3,17,	5,19,	
3,15,	17,31,	5,14,	3,18,	
3,15,	6,19,	10,14,	21,35,	
6,14,	7,22,	9,11,	8,22,	
0,0,	5,20,	0,0,	21,35,	
21,35,	0,0,	0,0,	6,20,	
9,11,	0,0,	0,0,	9,14,	
9,11,	23,37,	10,23,	0,0,	
10,24,	27,39,	26,38,	0,0,	
0,0,	0,0,	0,0,	10,25,	
0,0,	0,0,	10,26,	0,0,	
21,36,	0,0,	10,27,	9,23,	
0,0,	9,24,	0,0,	0,0,	
0,0,	0,0,	21,35,	0,0,	
9,25,	0,0,	21,35,	9,26,	
0,0,	0,0,	0,0,	9,27,	
0,0,	0,0,	0,0,	0,0,	
0,0,	0,0,	0,0,	0,0,	
0,0,	0,0,	0,0,	0,0,	
0,0,	0,0,	20,34,	0,0,	
0,0,	0,0,	0,0,	0,0,	
0,0,	19,32,	0,0,	0,0,	
10,28,	19,32,	19,32,	19,32,	
19,32,	19,32,	19,32,	19,32,	
19,32,	19,32,	19,32,	0,0,	
0,0,	0,0,	0,0,	0,0,	
0,0,	9,28,	19,32,	19,32,	
19,32,	19,32,	19,32,	19,32,	
19,32,	19,32,	19,32,	19,32,	
19,32,	19,32,	19,32,	19,32,	
19,32,	19,32,	19,32,	19,32,	
19,32,	19,32,	19,32,	19,32,	
19,32,	19,32,	19,32,	19,32,	
0,0,	0,0,	0,0,	0,0,	
19,32,	0,0,	19,32,	19,32,	
19,32,	19,32,	19,32,	19,32,	
19,32,	19,32,	19,32,	19,32,	
19,32,	19,32,	19,32,	19,32,	
19,32,	19,32,	19,32,	19,32,	
19,32,	19,32,	19,32,	19,32,	
19,32,	19,32,	19,32,	19,32,	
0,0};
struct yysvf yysvec[] = {
0,	0,	0,
yycrank+-1,	0,		yyvstop+1,
yycrank+-3,	yysvec+1,	yyvstop+3,
yycrank+-12,	0,		yyvstop+5,
yycrank+-5,	yysvec+3,	yyvstop+7,
yycrank+-11,	yysvec+1,	yyvstop+9,
yycrank+-17,	yysvec+1,	yyvstop+11,
yycrank+-4,	yysvec+1,	yyvstop+13,
yycrank+-6,	yysvec+1,	yyvstop+15,
yycrank+-32,	0,		yyvstop+17,
yycrank+-15,	yysvec+9,	yyvstop+19,
yycrank+0,	0,		yyvstop+21,
yycrank+5,	0,		yyvstop+23,
yycrank+0,	0,		yyvstop+26,
yycrank+1,	0,		yyvstop+28,
yycrank+0,	0,		yyvstop+30,
yycrank+0,	yysvec+12,	yyvstop+33,
yycrank+10,	0,		yyvstop+37,
yycrank+0,	yysvec+14,	yyvstop+40,
yycrank+93,	0,		yyvstop+43,
yycrank+7,	0,		yyvstop+46,
yycrank+-62,	0,		yyvstop+48,
yycrank+0,	0,		yyvstop+50,
yycrank+3,	0,		yyvstop+53,
yycrank+0,	0,		yyvstop+56,
yycrank+0,	0,		yyvstop+59,
yycrank+1,	0,		yyvstop+62,
yycrank+1,	0,		yyvstop+65,
yycrank+0,	0,		yyvstop+67,
yycrank+0,	yysvec+12,	yyvstop+70,
yycrank+0,	0,		yyvstop+72,
yycrank+0,	0,		yyvstop+74,
yycrank+0,	yysvec+19,	yyvstop+76,
yycrank+0,	yysvec+20,	0,	
yycrank+0,	0,		yyvstop+78,
yycrank+0,	yysvec+21,	0,	
yycrank+0,	0,		yyvstop+80,
yycrank+0,	0,		yyvstop+82,
yycrank+0,	0,		yyvstop+84,
yycrank+0,	0,		yyvstop+86,
0,	0,	0};
struct yywork *yytop = yycrank+215;
struct yysvf *yybgin = yysvec+1;
char yymatch[] = {
  0,   1,   1,   1,   1,   1,   1,   1, 
  1,   9,  10,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,   1,   1,   1,   1, 
  9,   1,  34,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,  44,   1,   1,   1, 
 48,  48,  48,  48,  48,  48,  48,  48, 
 48,  48,   1,   1,   1,   1,   1,   1, 
  1,  44,  44,  44,  44,  44,  44,  44, 
 44,  44,  44,  44,  44,  44,  44,  44, 
 44,  44,  44,  44,  44,  44,  44,  44, 
 44,  44,  44,   1,   1,   1,   1,  44, 
  1,  44,  44,  44,  44,  44,  44,  44, 
 44,  44,  44,  44,  44,  44,  44,  44, 
 44,  44,  44,  44,  44,  44,  44,  44, 
 44,  44,  44,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,   1,   1,   1,   1, 
  1,   1,   1,   1,   1,   1,   1,   1, 
0};
char yyextra[] = {
0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,
0};
/*	Copyright (c) 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)ncform	6.7	93/06/07 SMI"

int yylineno =1;
# define YYU(x) x
# define NLSTATE yyprevious=YYNEWLINE
char yytext[YYLMAX];
struct yysvf *yylstate [YYLMAX], **yylsp, **yyolsp;
char yysbuf[YYLMAX];
char *yysptr = yysbuf;
int *yyfnd;
extern struct yysvf *yyestate;
int yyprevious = YYNEWLINE;
#if defined(__cplusplus) || defined(__STDC__)
int yylook(void)
#else
yylook()
#endif
{
	register struct yysvf *yystate, **lsp;
	register struct yywork *yyt;
	struct yysvf *yyz;
	int yych, yyfirst;
	struct yywork *yyr;
# ifdef LEXDEBUG
	int debug;
# endif
	char *yylastch;
	/* start off machines */
# ifdef LEXDEBUG
	debug = 0;
# endif
	yyfirst=1;
	if (!yymorfg)
		yylastch = yytext;
	else {
		yymorfg=0;
		yylastch = yytext+yyleng;
		}
	for(;;){
		lsp = yylstate;
		yyestate = yystate = yybgin;
		if (yyprevious==YYNEWLINE) yystate++;
		for (;;){
# ifdef LEXDEBUG
			if(debug)fprintf(yyout,"state %d\n",yystate-yysvec-1);
# endif
			yyt = yystate->yystoff;
			if(yyt == yycrank && !yyfirst){  /* may not be any transitions */
				yyz = yystate->yyother;
				if(yyz == 0)break;
				if(yyz->yystoff == yycrank)break;
				}
#ifndef __cplusplus
			*yylastch++ = yych = input();
#else
			*yylastch++ = yych = lex_input();
#endif
			if(yylastch > &yytext[YYLMAX]) {
				fprintf(yyout,"Input string too long, limit %d\n",YYLMAX);
				exit(1);
			}
			yyfirst=0;
		tryagain:
# ifdef LEXDEBUG
			if(debug){
				fprintf(yyout,"char ");
				allprint(yych);
				putchar('\n');
				}
# endif
			yyr = yyt;
			if ( (int)yyt > (int)yycrank){
				yyt = yyr + yych;
				if (yyt <= yytop && yyt->verify+yysvec == yystate){
					if(yyt->advance+yysvec == YYLERR)	/* error transitions */
						{unput(*--yylastch);break;}
					*lsp++ = yystate = yyt->advance+yysvec;
					if(lsp > &yylstate[YYLMAX]) {
						fprintf(yyout,"Input string too long, limit %d\n",YYLMAX);
						exit(1);
					}
					goto contin;
					}
				}
# ifdef YYOPTIM
			else if((int)yyt < (int)yycrank) {		/* r < yycrank */
				yyt = yyr = yycrank+(yycrank-yyt);
# ifdef LEXDEBUG
				if(debug)fprintf(yyout,"compressed state\n");
# endif
				yyt = yyt + yych;
				if(yyt <= yytop && yyt->verify+yysvec == yystate){
					if(yyt->advance+yysvec == YYLERR)	/* error transitions */
						{unput(*--yylastch);break;}
					*lsp++ = yystate = yyt->advance+yysvec;
					if(lsp > &yylstate[YYLMAX]) {
						fprintf(yyout,"Input string too long, limit %d\n",YYLMAX);
						exit(1);
					}
					goto contin;
					}
				yyt = yyr + YYU(yymatch[yych]);
# ifdef LEXDEBUG
				if(debug){
					fprintf(yyout,"try fall back character ");
					allprint(YYU(yymatch[yych]));
					putchar('\n');
					}
# endif
				if(yyt <= yytop && yyt->verify+yysvec == yystate){
					if(yyt->advance+yysvec == YYLERR)	/* error transition */
						{unput(*--yylastch);break;}
					*lsp++ = yystate = yyt->advance+yysvec;
					if(lsp > &yylstate[YYLMAX]) {
						fprintf(yyout,"Input string too long, limit %d\n",YYLMAX);
						exit(1);
					}
					goto contin;
					}
				}
			if ((yystate = yystate->yyother) && (yyt= yystate->yystoff) != yycrank){
# ifdef LEXDEBUG
				if(debug)fprintf(yyout,"fall back to state %d\n",yystate-yysvec-1);
# endif
				goto tryagain;
				}
# endif
			else
				{unput(*--yylastch);break;}
		contin:
# ifdef LEXDEBUG
			if(debug){
				fprintf(yyout,"state %d char ",yystate-yysvec-1);
				allprint(yych);
				putchar('\n');
				}
# endif
			;
			}
# ifdef LEXDEBUG
		if(debug){
			fprintf(yyout,"stopped at %d with ",*(lsp-1)-yysvec-1);
			allprint(yych);
			putchar('\n');
			}
# endif
		while (lsp-- > yylstate){
			*yylastch-- = 0;
			if (*lsp != 0 && (yyfnd= (*lsp)->yystops) && *yyfnd > 0){
				yyolsp = lsp;
				if(yyextra[*yyfnd]){		/* must backup */
					while(yyback((*lsp)->yystops,-*yyfnd) != 1 && lsp > yylstate){
						lsp--;
						unput(*yylastch--);
						}
					}
				yyprevious = YYU(*yylastch);
				yylsp = lsp;
				yyleng = yylastch-yytext+1;
				yytext[yyleng] = 0;
# ifdef LEXDEBUG
				if(debug){
					fprintf(yyout,"\nmatch ");
					sprint(yytext);
					fprintf(yyout," action %d\n",*yyfnd);
					}
# endif
				return(*yyfnd++);
				}
			unput(*yylastch);
			}
		if (yytext[0] == 0  /* && feof(yyin) */)
			{
			yysptr=yysbuf;
			return(0);
			}
#ifndef __cplusplus
		yyprevious = yytext[0] = input();
		if (yyprevious>0)
			output(yyprevious);
#else
		yyprevious = yytext[0] = lex_input();
		if (yyprevious>0)
			lex_output(yyprevious);
#endif
		yylastch=yytext;
# ifdef LEXDEBUG
		if(debug)putchar('\n');
# endif
		}
	}
#if defined(__cplusplus) || defined(__STDC__)
int yyback(int *p, int m)
#else
yyback(p, m)
	int *p;
#endif
{
	if (p==0) return(0);
	while (*p) {
		if (*p++ == m)
			return(1);
	}
	return(0);
}
	/* the following are only used in the lex library */
#if defined(__cplusplus) || defined(__STDC__)
int yyinput(void)
#else
yyinput()
#endif
{
#ifndef __cplusplus
	return(input());
#else
	return(lex_input());
#endif
	}
#if defined(__cplusplus) || defined(__STDC__)
void yyoutput(int c)
#else
yyoutput(c)
  int c; 
#endif
{
#ifndef __cplusplus
	output(c);
#else
	lex_output(c);
#endif
	}
#if defined(__cplusplus) || defined(__STDC__)
void yyunput(int c)
#else
yyunput(c)
   int c; 
#endif
{
	unput(c);
	}

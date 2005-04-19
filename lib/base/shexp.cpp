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
/*
 * shexp.c: shell-like wildcard match routines
 *
 *
 * See shexp.h for public documentation.
 *
 * Rob McCool
 * 
 */

#include "shexp.h"
#include <ctype.h>    /* isalpha, tolower */


/* ----------------------------- shexp_valid ------------------------------ */


int valid_subexp(char *exp, char stop) 
{
    register int x,y,t;
    int nsc,tld;

    x=0;nsc=0;tld=0;

    while(exp[x] && (exp[x] != stop)) {
        switch(exp[x]) {
          case '~':
            if(tld) return INVALID_SXP;
            else ++tld;
          case '*':
          case '?':
          case '^':
          case '$':
            ++nsc;
            break;
          case '[':
            ++nsc;
            if((!exp[++x]) || (exp[x] == ']'))
                return INVALID_SXP;
            for(++x;exp[x] && (exp[x] != ']');++x)
                if(exp[x] == '\\')
                    if(!exp[++x])
                        return INVALID_SXP;
            if(!exp[x])
                return INVALID_SXP;
            break;
          case '(':
            ++nsc;
            while(1) {
                if(exp[++x] == ')')
                    return INVALID_SXP;
                for(y=x;(exp[y]) && (exp[y] != '|') && (exp[y] != ')');++y)
                    if(exp[y] == '\\')
                        if(!exp[++y])
                            return INVALID_SXP;
                if(!exp[y])
                    return INVALID_SXP;
                t = valid_subexp(&exp[x],exp[y]);
                if(t == INVALID_SXP)
                    return INVALID_SXP;
                x+=t;
                if(exp[x] == ')') {
                    break;
                }
            }
            break;
          case ')':
          case ']':
            return INVALID_SXP;
          case '\\':
            if(!exp[++x])
                return INVALID_SXP;
          default:
            break;
        }
        ++x;
    }
    if((!stop) && (!nsc))
        return NON_SXP;
    return ((exp[x] == stop) ? x : INVALID_SXP);
}

NSAPI_PUBLIC int shexp_valid(char *exp) {
    int x;

    x = valid_subexp(exp, '\0');
    return (x < 0 ? x : VALID_SXP);
}


/* ----------------------------- shexp_match ----------------------------- */


#define MATCH 0
#define NOMATCH 1
#define ABORTED -1

int _shexp_match(char *str, char *exp);

int handle_union(char *str, char *exp) 
{
    char *e2 = (char *) MALLOC(sizeof(char)*strlen(exp));
    register int t,p2,p1 = 1;
    int cp;

    while(1) {
        for(cp=1;exp[cp] != ')';cp++)
            if(exp[cp] == '\\')
                ++cp;
        for(p2 = 0;(exp[p1] != '|') && (p1 != cp);p1++,p2++) {
            if(exp[p1] == '\\')
                e2[p2++] = exp[p1++];
            e2[p2] = exp[p1];
        }
        for(t=cp+1;(e2[p2] = exp[t]);++t,++p2);
        if(_shexp_match(str,e2) == MATCH) {
            FREE(e2);
            return MATCH;
        }
        if(p1 == cp) {
            FREE(e2);
            return NOMATCH;
        }
        else ++p1;
    }
}


int _shexp_match(char *str, char *exp) 
{
    register int x,y;
    int ret,neg;

    ret = 0;
    for(x=0,y=0;exp[y];++y,++x) {
        if((!str[x]) && (exp[y] != '(') && (exp[y] != '$') && (exp[y] != '*'))
            ret = ABORTED;
        else {
            switch(exp[y]) {
              case '$':
                if( (str[x]) )
                    ret = NOMATCH;
                else
                    --x;             /* we don't want loop to increment x */
                break;
              case '*':
                while(exp[++y] == '*');
                if(!exp[y])
                    return MATCH;
                while(str[x]) {
                    switch(_shexp_match(&str[x++],&exp[y])) {
                    case NOMATCH:
                        continue;
                    case ABORTED:
                        ret = ABORTED;
                        break;
                    default:
                        return MATCH;
                    }
                    break;
                }
                if((exp[y] == '$') && (exp[y+1] == '\0') && (!str[x]))
                    return MATCH;
                else
                    ret = ABORTED;
                break;
              case '[':
                if((neg = ((exp[++y] == '^') && (exp[y+1] != ']'))))
                    ++y;
                
                if((isalnum(exp[y])) && (exp[y+1] == '-') && 
                   (isalnum(exp[y+2])) && (exp[y+3] == ']'))
                    {
                        int start = exp[y], end = exp[y+2];
                        
                        /* Droolproofing for pinheads not included */
                        if(neg ^ ((str[x] < start) || (str[x] > end))) {
                            ret = NOMATCH;
                            break;
                        }
                        y+=3;
                    }
                else {
                    int matched;
                    
                    for(matched=0;exp[y] != ']';y++)
                        matched |= (str[x] == exp[y]);
                    if(neg ^ (!matched))
                        ret = NOMATCH;
                }
                break;
              case '(':
                return handle_union(&str[x],&exp[y]);
                break;
              case '?':
                break;
              case '\\':
                ++y;
              default:
#ifdef XP_UNIX
                if(str[x] != exp[y])
#else /* XP_WIN32 */
                if(strnicmp(str + x, exp + y, 1))
#endif /* XP_WIN32 */
                    ret = NOMATCH;
                break;
            }
        }
        if(ret)
            break;
    }
    return (ret ? ret : (str[x] ? NOMATCH : MATCH));
}

NSAPI_PUBLIC int shexp_match(char *str, char *xp) {
    register int x;
    char *exp = STRDUP(xp);

    for(x=strlen(exp)-1;x;--x) {
        if((exp[x] == '~') && (exp[x-1] != '\\')) {
            exp[x] = '\0';
            if(_shexp_match(str,&exp[++x]) == MATCH)
                goto punt;
            break;
        }
    }
    if(_shexp_match(str,exp) == MATCH) {
        FREE(exp);
        return 0;
    }

  punt:
    FREE(exp);
    return 1;
}


/* ------------------------------ shexp_cmp ------------------------------- */


NSAPI_PUBLIC int shexp_cmp(char *str, char *exp)
{
    switch(shexp_valid(exp)) {
      case INVALID_SXP:
        return -1;
      case NON_SXP:
#ifdef XP_UNIX
        return (strcmp(exp,str) ? 1 : 0);
#else  /* XP_WIN32 */
        return (stricmp(exp,str) ? 1 : 0);
#endif /* XP_WIN32 */
      default:
        return shexp_match(str, exp);
    }
}


/* ---------------------------- shexp_casecmp ----------------------------- */


NSAPI_PUBLIC int shexp_casecmp(char *str, char *exp)
{
    char *lstr = STRDUP(str), *lexp = STRDUP(exp), *t;
    int ret;

    for(t = lstr; *t; t++)
        if(isalpha(*t)) *t = tolower(*t);
    for(t = lexp; *t; t++)
        if(isalpha(*t)) *t = tolower(*t);

    switch(shexp_valid(lexp)) {
      case INVALID_SXP:
        ret = -1;
        break;
      case NON_SXP:
        ret = (strcmp(lexp, lstr) ? 1 : 0);
        break;
      default:
        ret = shexp_match(lstr, lexp);
    }
    FREE(lstr);
    FREE(lexp);
    return ret;
}


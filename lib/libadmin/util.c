/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* 
 * util.c:  Miscellaneous stuffs
 *            
 * All blame to Mike McCool
 */

#include "libadmin/libadmin.h"
#include "base/util.h"
#include "private/pprio.h"

#include <base/file.h>
#ifdef XP_UNIX
#include <dirent.h>
#include <sys/types.h>
#include <fcntl.h>
#else
#include <sys/stat.h>
#endif /* WIN32? */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef XP_UNIX
SYS_FILE lf;
#elif defined(XP_WIN32)
HANDLE lf;
#endif

char *get_flock_path(void)
{
    char *result="";
    char *port=getenv("SERVER_PORT");
#ifdef XP_UNIX
    result=(char *) MALLOC(strlen("/tmp/lock.%%s.")+strlen(port)+4);
    sprintf(result, "/tmp/lock.%%s.%s", port);
#endif
    return result;
}

/* Open a file with locking, close a file with unlocking. */
FILE *fopen_l(char *path, char *mode)
{
    FILE *f = fopen(path, mode);
    char *lockpath;
    char *sn="admserv";
    char *flp=get_flock_path();
 
    if(f == NULL) return NULL;
    lockpath=(char *) MALLOC(strlen(sn)+strlen(flp)+16);
    sprintf(lockpath, flp, sn);
#ifdef XP_UNIX
    if( (lf=system_fopenRW(lockpath)) == SYS_ERROR_FD)
        report_error(FILE_ERROR, lockpath, "Could not open file.");
    if(system_flock(lf)==IO_ERROR)
        report_error(FILE_ERROR, lockpath, "Could not lock file.");
#elif defined(XP_WIN32)
    /* Using mutexes because if the CGI program dies, the mutex will be
     * automatically released by the OS for another process to grab.  
     * Semaphores do not have this property; and if the CGI program crashes,
     * the admin server would be effectively crippled.
     */
	if ( (lf = CreateMutex(NULL, 0, lockpath)) == NULL) {
        report_error(FILE_ERROR, lockpath, "Could not create admin mutex.");
	} else {
        if ( WaitForSingleObject(lf, 60*1000) == WAIT_FAILED) {
            report_error(FILE_ERROR, lockpath, "Unable to obtain mutex after 60 seconds.");
        }
	}
#endif /* XP_UNIX */
    return f;
}

void fclose_l(FILE *f)
{
    fclose(f);
#ifdef XP_UNIX
    if(system_ulock(lf)==IO_ERROR)
        report_error(FILE_ERROR, NULL, "Could not unlock lock file.");
    system_fclose(lf);
#elif defined(XP_WIN32)
    if (lf) {
        ReleaseMutex(lf);
        CloseHandle(lf);
    }
#endif /* XP_UNIX */
}
 
char *alert_word_wrap(char *str, int width, char *linefeed)
{
    char *ans = NULL;
    int counter=0;
    int lsc=0, lsa=0;
    register int strc=0, ansc=0;
    register int x=0;
 
    /* assume worst case */
    ans = (char *) MALLOC((strlen(str)*strlen(linefeed))+32);
 
    for(strc=0, ansc=0; str[strc]; /*none*/)  {
        if(str[strc]=='\n')  {
            counter=0;
            lsc=0, lsa=0;
            for(x=0; linefeed[x]; x++)  {
                ans[ansc++]=linefeed[x];
            }
            strc++;
        }  else if(str[strc]=='\r')  {
            strc++;
        }  else if(str[strc]=='\\')  {
            ans[ansc++]='\\';
            ans[ansc++]=strc++;
        }  else  {
            if(counter==width)  {
                if(lsc && lsa)  {
                    strc=lsc;
                    ansc=lsa;
 
                    counter=0;
                    lsc=0, lsa=0;
                    for(x=0; linefeed[x]; x++)  {
                        ans[ansc++]=linefeed[x];
                    }
                    strc++;
                }  else  {
                /* else, you're a loser, I'm breaking your big word anyway */
                    counter=0;
                    lsc=0, lsa=0;
                    for(x=0; linefeed[x]; x++)  {
                        ans[ansc++]=linefeed[x];
                    }
                    strc++;
                }
            }  else  {
                if(str[strc] == ' ')  {
                    lsc=strc;
                    lsa=ansc;
                }
                ans[ansc++]=str[strc++];
                counter++;
            }
        }
    }
    ans[ansc]='\0';
    return ans;
}

/***********************************************************************
** FUNCTION:	cookieValue
** DESCRIPTION:
**   Get the current value of the cookie variable
** INPUTS:	var - the name of the cookie variable
**		val - if non-NULL, set the in-memory copy of the var
** OUTPUTS:	None
** RETURN:	NULL if the var doesn't exist, else the value
** SIDE EFFECTS:
**	Eats memory
** RESTRICTIONS:
**	Don't screw around with the returned string, if anything else wants
**	to use it.
** MEMORY:	This is a memory leak, so only use it in CGIs
** ALGORITHM:
**	If it's never been called, build a memory structure of the
**	cookie variables.
**	Look for the passed variable, and return its value, or NULL
***********************************************************************/

NSAPI_PUBLIC char *
cookieValue( char *var, char *val )
{
  static char	**vars = NULL;
  static char	**vals = NULL;
  static int	numVars = -1;
  int		i;

  if ( numVars == -1 ) {	/* first time, init the structure */
    char	*cookie = getenv( "HTTP_COOKIE" );

    if ( cookie && *cookie ) {
      int	len = strlen( cookie );
      int	foundVal = 0;

      cookie = STRDUP( cookie );
      numVars = 0;
      vars = (char **)MALLOC( sizeof( char * ) );
      vals = (char **)MALLOC( sizeof( char * ) );
      vars[0] = cookie;
      for ( i = 0 ; i < len ; ++i ) {
	if ( ( ! foundVal ) && ( cookie[i] == '=' ) ) {	
	  vals[numVars++] = cookie + i + 1;
	  cookie[i] = '\0';
	  foundVal = 1;
	} else if ( ( cookie[i] == ';' ) && ( cookie[i+1] == ' ' ) ) {
	  cookie[i] = '\0';
	  vals = (char **) REALLOC( vals,
				    sizeof( char * ) * ( numVars + 1 ) );
	  vars = (char **) REALLOC( vars,
				    sizeof( char * ) * ( numVars + 1 ) );
	  vars[numVars] = cookie + i + 2;
	  i += 2;
	  foundVal = 0;
	}
      }
    } else {	/* no cookie, no vars */
      numVars = 0;
    }
  }
  for ( i = 0 ; i < numVars ; ++i ) {
    if ( strcmp( vars[i], var ) == 0 ) {
      if ( val ) {
	vals[i] = STRDUP( val );
      } else {
	return vals[i];
      }
    }
  }
  return NULL;
}

static int adm_initialized=0;

/* Initialize NSPR for all the base functions we use */
NSAPI_PUBLIC int ADM_Init(void)
{
    if(!adm_initialized)  {
        NSPR_INIT("AdminPrograms");
        adm_initialized=1;
    }
    return 0;
}

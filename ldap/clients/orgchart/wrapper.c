/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */

/***********************************************************************
** Includes
***********************************************************************/

#include	<stdlib.h>
#include	<limits.h>
#include	<string.h>
#include <sys/types.h>
#include <sys/stat.h>

/*#include "libadmin/libadmin.h"*/

#ifdef XP_UNIX
#include	<unistd.h>
#define PERL "../../bin/slapd/admin/bin/perl"
#define PATH_SEP '/'
#ifndef PATH_MAX
#define PATH_MAX 512
#endif
#else
#include	<direct.h>
#include	<process.h>
#define PERL "..\\..\\bin\\slapd\\admin\\bin\\perl.exe"
#define PATH_SEP '\\'
#define PATH_MAX 512
#endif

char *get_perl_file(char *);


/*
 * Use environment to figure out what admin perl script to execute
 */

void
main( int argc, char **argv )
{
  char	script[PATH_MAX];
  struct stat statbuf;

  printf("Content-type:text/html;charset=UTF-8\n\n<html>Hi\n");

  get_perl_file(script);
  
  if (strchr(script, '/') != NULL || strchr(script, '\\') != NULL) {
    printf("Paths not allowed. Filenames only.\n");
    exit(0);
  }

  printf("<br>script:%s</html>\n", script);
  if (stat(script, &statbuf) != 0) {
    printf("Can't find %s\n", script);
    exit(0);
  }

  execl( PERL, script, script, 0 );
}

char *
get_perl_file(char *script) {
  char *qs = getenv("QUERY_STRING");
  char *p1 = NULL;
  char *p2 = NULL;
  
  if (qs == NULL || *qs == '\0') {
    printf("No QUERY_STRING found\n");
    exit(0);
  }
  p1 = strstr(qs, "file=");
  if (p1 == NULL) {
    printf("No file variable in QUERY_STRING found.\n");
    exit(0);
  }

  p1 += 5;

  for (p2 = p1; *p2 != '\0' && *p2 != '&'; p2++);

  strncpy(script, p1, p2-p1);
  script[p2-p1] = '\0';
}

/* --- BEGIN COPYRIGHT BLOCK ---
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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


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

char *get_perl_file(char *, size_t);


/*
 * Use environment to figure out what admin perl script to execute
 */

void
main( int argc, char **argv )
{
  char	script[PATH_MAX];
  struct stat statbuf;

  printf("Content-type:text/html;charset=UTF-8\n\n<html>Hi\n");

  get_perl_file(script, sizeof(script)-1);
  
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
get_perl_file(char *script, size_t scriptsize) {
  char *qs = getenv("QUERY_STRING");
  char *p1 = NULL;
  char *p2 = NULL;
  size_t maxsize;
  
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

  maxsize = (scriptsize < (p2-p1)) ? scriptsize : (p2-p1);

  PL_strncpyz(script, p1, maxsize);
  script[maxsize] = '\0';
}

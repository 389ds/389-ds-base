/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include "pio.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

int iii_pio_procparse (
	const char *cmd,
	int count,
	struct iii_pio_parsetab *tb
)
{
  FILE *fp;
  char buf[8192];
  int rc = 0;

  fp = popen(cmd,"r");

  if (fp == NULL) {
    return -1;
  }
  
  while (fgets(buf,8192,fp) != NULL && rc >= 0) {
    char *rp;
    int i;

    rp = strchr(buf,'\n');

    if (rp) {
      *rp = '\0';
    }

    rp = strchr(buf,':');

#if defined(__osf__)
    if (rp == NULL) {
      rp = strchr(buf,'=');
    }
#endif

    if (rp == NULL) continue;
    
    *rp = '\0';
    rp++;
    while(isspace(*rp)) rp++;

    for (i = 0; i < count; i++) {
      if (strcmp(tb[i].token,buf) == 0) {
	rc = (tb[i].fn)(buf,rp);
	break;
      }
    }
  }
  
  pclose(fp);
  
  return rc;
}

int iii_pio_getnum (
	const char *cmd,
	long *valPtr
)
{
  FILE *fp;
  char buf[8192];
  int rc = 0;
  
  fp = popen(cmd,"r");
  
  if (fp == NULL) {
    return -1;
  }
  
  if (fgets(buf,8192,fp) == NULL) {
    pclose(fp);
    return -1;
  }

  pclose(fp);

  if (!(isdigit(*buf))) {
    return -1;
  }
  
  *valPtr = atol(buf);
  
  return 0;
}

/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef _H_III_PIO_H
#define _H_III_PIO_H

#include <stdio.h>

struct iii_pio_parsetab {
  char *token;
  int (*fn)(char *,char *);
};

#define III_PIO_SZ(x) (sizeof(x)/sizeof(struct iii_pio_parsetab))

extern int iii_pio_procparse (
	const char *cmd,
	int count,
	struct iii_pio_parsetab *
);

extern int iii_pio_getnum (
	const char *cmd,
	long *valPtr
);

#endif /* _H_III_PIO_H */



/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* cl5.h - changelog related function */

#ifndef CL5_H
#define CL5_H

#include "cl5_api.h"	/* changelog access APIs */

typedef struct changelog5Config
{
	char *dir;
/* These 2 parameters are needed for changelog trimming. Already present in 5.0 */
	char *maxAge;
	int	 maxEntries;
/* the changelog DB configuration parameters are defined as CL5DBConfig in cl5_api.h */
	CL5DBConfig dbconfig;	
}changelog5Config;

/* initializes changelog*/
int changelog5_init();
/* cleanups changelog data */
void changelog5_cleanup();
/* initializes changelog configurationd */
int changelog5_config_init();
/* cleanups config data */
void changelog5_config_cleanup();
/* reads changelog configuration */
int changelog5_read_config (changelog5Config *config); 
/* cleanups the content of the config structure */
void changelog5_config_done (changelog5Config *config);
/* frees the content and the config structure */
void changelog5_config_free (changelog5Config **config);

#endif

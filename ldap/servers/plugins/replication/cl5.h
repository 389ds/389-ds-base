/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* cl5.h - changelog related function */

#ifndef CL5_H
#define CL5_H

#include "cl5_api.h" /* changelog access APIs */

typedef struct changelog5Config
{
    char *dir;
    /* These 2 parameters are needed for changelog trimming. Already present in 5.0 */
    char *maxAge;
    int maxEntries;
    /* the changelog DB configuration parameters are defined as CL5DBConfig in cl5_api.h */
    CL5DBConfig dbconfig;
    char *symmetricKey;
    long compactInterval;
    long trimInterval;
    char *compactTime;
} changelog5Config;

/* initializes changelog*/
int changelog5_init(void);
/* cleanups changelog data */
void changelog5_cleanup(void);
/* initializes changelog configurationd */
int changelog5_config_init(void);
/* cleanups config data */
void changelog5_config_cleanup(void);
/* reads changelog configuration */
int changelog5_read_config(changelog5Config *config);
/* cleanups the content of the config structure */
void changelog5_config_done(changelog5Config *config);
/* frees the content and the config structure */
void changelog5_config_free(changelog5Config **config);

#define MAX_TRIALS 50 /* number of retries on db operations */

#endif

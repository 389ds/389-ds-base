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
    /* These 3 parameters are needed for changelog trimming. */
    char *maxAge;
    int maxEntries;
    long trimInterval;
    /* configuration of changelog encryption */
    char *encryptionAlgorithm;
    char *symmetricKey;
} changelog5Config;

/* upgrade changelog*/
int changelog5_upgrade(void);
/* initialize changelog*/
int changelog5_init(void);
/* cleanups changelog data */
void changelog5_cleanup(void);
/* initializes changelog configurationd */
int changelog5_config_init(void);
/* cleanups config data */
void changelog5_config_cleanup(void);
/* reads changelog configuration */
int changelog5_read_config(changelog5Config *config);
/* transforms entry to internal config */
void changelog5_extract_config(Slapi_Entry *entry, changelog5Config *config);
/* registeri/unregister functions to handle config changes */
int changelog5_register_config_callbacks(const char *dn, Replica *replica);
int changelog5_remove_config_callbacks(const char *dn);
/* cleanups the content of the config structure */
void changelog5_config_done(changelog5Config *config);
/* frees the content and the config structure */
void changelog5_config_free(changelog5Config **config);

#define MAX_TRIALS 50 /* number of retries on db operations */

#endif

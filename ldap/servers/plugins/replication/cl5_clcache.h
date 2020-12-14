#ifndef CL5_CLCACHE_H
#define CL5_CLCACHE_H

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


#include "db.h"
#include "slapi-private.h"

typedef struct clc_buffer CLC_Buffer;

int clcache_init(DB_ENV **dbenv);
void clcache_set_config(void);
int clcache_get_buffer(CLC_Buffer **buf, DB *db, ReplicaId consumer_rid, const RUV *consumer_ruv, const RUV *local_ruv);
int clcache_load_buffer(CLC_Buffer *buf, CSN **anchorCSN, int *continue_on_miss, char *initial_starting_csn);
void clcache_return_buffer(CLC_Buffer **buf);
int clcache_get_next_change(CLC_Buffer *buf, void **key, size_t *keylen, void **data, size_t *datalen, CSN **csn, char *initial_starting_csn);
void clcache_destroy(void);

#endif

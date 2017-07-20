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


#ifndef _COLLATE_H_
#define _COLLATE_H_

#include <stddef.h> /* size_t */

struct indexer_t;

typedef void (*ix_destroy_t)(struct indexer_t *);
typedef struct berval **(*ix_index_t)(struct indexer_t *, struct berval **values, struct berval **prefixes /* inserted into each key */);

typedef struct indexer_t
{
    char *ix_oid;
    ix_index_t ix_index; /* map values to index keys */
    ix_destroy_t ix_destroy;
    void *ix_etc; /* whatever state the implementation needs */
} indexer_t;

extern void
collation_init(char *configpath);

extern int
collation_config(size_t argc, char **argv, const char *fname, size_t lineno);

extern indexer_t *
collation_indexer_create(const char *oid);

#define COLLATE_PLUGIN_SUBSYSTEM "collation-plugin"

#endif

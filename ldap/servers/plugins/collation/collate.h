/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef _COLLATE_H_
#define _COLLATE_H_

#include <stddef.h> /* size_t */
#include <dirlite_strings.h> /* PLUGIN_MAGIC_VENDOR_STR */

struct indexer_t;

typedef void (*ix_destroy_t) (struct indexer_t*);
typedef struct berval** (*ix_index_t) (struct indexer_t*, struct berval** values,
				       struct berval** prefixes /* inserted into each key */);

typedef struct indexer_t
{
    char*        ix_oid;
    ix_index_t   ix_index; /* map values to index keys */
    ix_destroy_t ix_destroy;
    void*        ix_etc; /* whatever state the implementation needs */
} indexer_t;

extern void
collation_init( char *configpath );

extern int
collation_config (size_t argc, char** argv, const char* fname, size_t lineno);

extern indexer_t*
collation_indexer_create (const char* oid);

#endif

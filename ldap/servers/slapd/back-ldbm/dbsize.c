/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * dbsize.c - ldbm backend routine which returns the size (in bytes)
 * that the database occupies on disk.
 */

#include "back-ldbm.h"

int
ldbm_db_size( Slapi_PBlock *pb )
{
	struct ldbminfo		*li;
	unsigned int		size;
	int			rc;

	slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
	rc = dblayer_database_size(li, &size);
	slapi_pblock_set( pb, SLAPI_DBSIZE, &size );

	return rc;
}

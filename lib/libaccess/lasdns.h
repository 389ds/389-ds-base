/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

typedef	struct LASDnsContext {
	PRHashTable	*Table;	
	pool_handle_t   *pool;
} LASDnsContext_t;

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

typedef struct LASIpTree {
	struct LASIpTree	*action[2];
} LASIpTree_t;

typedef	struct LASIpContext {
	LASIpTree_t	*treetop; /* Top of the pattern tree	*/
} LASIpContext_t;

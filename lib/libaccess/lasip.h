/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

typedef struct LASIpTree {
	struct LASIpTree	*action[2];
} LASIpTree_t;

typedef	struct LASIpContext {
	LASIpTree_t	*treetop; /* Top of the pattern tree	*/
} LASIpContext_t;

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


typedef struct LASIpTree
{
    struct LASIpTree *action[2];
} LASIpTree_t;

typedef struct LASIpContext
{
    LASIpTree_t *treetop;      /* Top of the pattern tree */
    LASIpTree_t *treetop_ipv6; /* Top of the IPv6 pattern tree */
} LASIpContext_t;

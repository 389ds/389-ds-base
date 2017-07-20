/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include <string.h>
#include "slap.h"
#include "slapi-plugin.h"

#define USN_PLUGIN_SUBSYSTEM "usn-plugin"

#define USN_CSNGEN_ID 65535

#define USN_LAST_USN "lastusn"
#define USN_LAST_USN_ATTR_CORE_LEN 7 /* lastusn */

#define USN_COUNTER_BUF_LEN 64 /* enough size for 64 bit integers */

/* usn.c */
void usn_set_identity(void *identity);
void *usn_get_identity(void);

/* usn_cleanup.c */
int usn_cleanup_start(Slapi_PBlock *pb);
int usn_cleanup_close(void);

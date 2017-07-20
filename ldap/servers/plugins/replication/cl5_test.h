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

/* cl5_test.h - changelog test cases */

typedef enum {
    TEST_BASIC,          /* open-close-delete, read-write-delete */
    TEST_BACKUP_RESTORE, /* test backup and recovery */
    TEST_ITERATION,      /* similar to iteration used by replica upsate protocol */
    TEST_TRIMMING,       /* test changelog trimming */
    TEST_PERFORMANCE,    /* test read/write performance */
    TEST_PERFORMANCE_MT, /* test multithreaded performance */
    TEST_LDIF,           /* test cl2ldif and ldif2cl */
    TEST_ALL             /* collective test */
} TestType;

void testChangelog(TestType type);

/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* cl5_test.h - changelog test cases */

typedef enum
{
	TEST_BASIC,			/* open-close-delete, read-write-delete */
	TEST_BACKUP_RESTORE,/* test backup and recovery */
	TEST_ITERATION,		/* similar to iteration used by replica upsate protocol */
	TEST_TRIMMING,		/* test changelog trimming */
	TEST_PERFORMANCE,	/* test read/write performance */	 
	TEST_PERFORMANCE_MT,/* test multithreaded performance */
	TEST_LDIF,			/* test cl2ldif and ldif2cl */
	TEST_ALL			/* collective test */
} TestType;

void testChangelog (TestType type);


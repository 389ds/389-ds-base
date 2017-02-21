/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * All rights reserved.
 *
 * License: License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#pragma once

#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h> // for uint64_t
#include <stdarg.h> // For va_start / va_end
#include <inttypes.h> // For PRI*
#include <string.h> // for memset
// From NSPR
#include <prlock.h> // For locking in structs
// #include <pratom.h> // For atomic increments.
// We use gcc atomic operations instead.
#include <prlog.h> // For pr_assert
#include <prthread.h>


#include <assert.h> // For assertions.
#include <pthread.h> // for threads

#define SDS_CACHE_ALIGNMENT 64

void sds_log(char *id, char *msg, ...);



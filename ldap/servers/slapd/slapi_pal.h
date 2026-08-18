/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

/*
 * Header for the slapi platform abstraction layer.
 *
 * This implements a number of functions that help to provide vendor
 * neutral requests. Candidates for this are memory, thread, disk size
 * and other operations.
 *
 * Basically anywhere you see a "ifdef PLATFORM" is a candidate
 * for this.
 */

#pragma once

#include <inttypes.h>

/**
 * Structure that contains our system memory information in bytes and pages.
 *
 */
typedef struct slapi_pal_meminfo_
{
    uint64_t pagesize_bytes;
    uint64_t system_total_pages;
    uint64_t system_total_bytes;
    uint64_t process_consumed_pages;
    uint64_t process_consumed_bytes;
    /* This value may be limited by cgroup or others. */
    uint64_t system_available_pages;
    uint64_t system_available_bytes;
} slapi_pal_meminfo;

/**
 * Allocate and returne a populated memory info structure. This will be NULL
 * on error, or contain a structure populated with platform information on
 * success. You should free this with spal_meminfo_destroy.
 *
 * \return slapi_pal_meminfo * pointer to structure containing data, or NULL.
 */
slapi_pal_meminfo *spal_meminfo_get(void);

/**
 * Destroy an allocated memory info structure. The caller is responsible for
 * ensuring this is called.
 *
 * \param mi the allocated slapi_pal_meminfo structure from spal_meminfo_get();
 */
void spal_meminfo_destroy(slapi_pal_meminfo *mi);

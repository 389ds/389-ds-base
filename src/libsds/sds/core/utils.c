/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#include "../sds_internal.h"
#include <sds.h>

/* uint64_t as a key functions. */

int64_t
sds_uint64_t_compare(void *a, void *b) {
    uint64_t ua = *(uint64_t *)a;
    uint64_t ub = *(uint64_t *)b;
    if (ua > ub) {
        return 1;
    } else if (ua < ub) {
        return -1;
    }
    return 0;
}

void *
sds_uint64_t_dup(void *key) {
#ifdef DEBUG
    sds_log("sds_uint64_t_dup", "dup %" PRIu64" ", key);
#endif
    uint64_t *newkey = sds_malloc(sizeof(uint64_t));
    *newkey = *(uint64_t *)key;
    return (void *)newkey;
}

void
sds_uint64_t_free(void *key) {
    uint64_t *ukey = key;
#ifdef DEBUG
    sds_log("sds_uint64_t_free", "freeing %" PRIu64"  @ %p", *ukey, key);
#endif
    sds_free(ukey);
    return;
}

uint64_t
sds_uint64_t_size(void *key __attribute__((unused))) {
    return sizeof(uint64_t);
}

/* We have to provide some wrappers to strcmp and such for casting */
int64_t
sds_strcmp(void *a, void *b) {
    return (int64_t)strcmp((const char *)a, (const char *)b);
}

void *
sds_strdup(void *key) {
    return (void *)strdup((const char *)key);
}


/*
 * sds_log
 *
 * This allows us to write a log message to an output.
 * Similar to malloc, by defining this, we can change the impl later.
 */
void
sds_log(char *id, char *msg, ...) {
    printf("%s: ", id);
    va_list subs;
    va_start(subs, msg);
    vprintf(msg, subs);
    va_end(subs);
    printf("\n");
    return;
}

/*
 * sds_malloc
 *
 * By wrapping our malloc call, we can forcefully check for malloc errors
 * so that we don't need to in our callers.
 *
 * It also will allow a change to the allocator in the future if required.
 */
void *
sds_malloc(size_t size) {
    void *ptr = NULL;
    ptr = malloc(size);
    if (ptr == NULL) {
        sds_log("sds_malloc", "CRITICAL: Unable to allocate memory!");
        exit (1);
    }
    return ptr;
}

void *
sds_calloc(size_t size) {
    void *ptr = NULL;
    ptr = calloc(1, size);
    if (ptr == NULL) {
        sds_log("sds_calloc", "CRITICAL: Unable to allocate memory!");
        exit (1);
    }
    return ptr;
}

void *
sds_memalign(size_t size, size_t alignment)
{
    void *ptr = NULL;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        sds_log("sds_memalign", "CRITICAL: Unable to allocate memory!");
        exit (1);
    }
    return ptr;
}

void
sds_free(void *ptr) {
    if (ptr != NULL) {
        free(ptr);
    }
}




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

/* slapi_ch_malloc.c - malloc routines that test returns from malloc and friends */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* strdup */
#include <sys/types.h>
#include <sys/socket.h>
#include "slap.h"

#define OOM_PREALLOC_SIZE 65536
static void *oom_emergency_area = NULL;
static PRLock *oom_emergency_lock = NULL;

#define SLAPD_MODULE "memory allocator"

static const char *const oom_advice =
    "\nThe server has probably allocated all available virtual memory. To solve\n"
    "this problem, make more virtual memory available to your server, or reduce\n"
    "one or more of the following server configuration settings:\n"
    "  nsslapd-cachesize        (Database Settings - Maximum entries in cache)\n"
    "  nsslapd-cachememsize     (Database Settings - Memory available for cache)\n"
    "  nsslapd-dbcachesize      (LDBM Plug-in Settings - Maximum cache size)\n"
    "  nsslapd-import-cachesize (LDBM Plug-in Settings - Import cache size).\n"
    "Can't recover; calling exit(1).\n";

static void
create_oom_buffer(void)
{
    /* ensure that we have space to allow for shutdown calls to malloc()
     * from should we run out of memory.
     */
    if (oom_emergency_area == NULL) {
        oom_emergency_area = malloc(OOM_PREALLOC_SIZE);
        oom_emergency_lock = PR_NewLock();
    }
}

/* called when we have just detected an out of memory condition, before
 * we make any other library calls.  Note that slapi_log_err() calls malloc,
 * indirectly.  By making 64KB free, we should be able to have a few
 * mallocs' succeed before we shut down.
 */
void
oom_occurred(void)
{
    int tmp_errno = errno; /* callers will need the error from malloc */
    if (oom_emergency_lock == NULL) {
        return;
    }

    PR_Lock(oom_emergency_lock);
    if (oom_emergency_area) {
        free(oom_emergency_area);
        oom_emergency_area = NULL;
    }
    PR_Unlock(oom_emergency_lock);
    errno = tmp_errno;
}

static void
log_negative_alloc_msg(const char *op, const char *units, unsigned long size)
{
    slapi_log_err(SLAPI_LOG_ERR, SLAPD_MODULE,
                  "cannot %s %lu %s;\n"
                  "trying to allocate 0 or a negative number of %s is not portable and\n"
                  "gives different results on different platforms.\n",
                  op, size, units, units);
}

char *
slapi_ch_malloc(
    unsigned long size)
{
    char *newmem;

    if (size <= 0) {
        log_negative_alloc_msg("malloc", "bytes", size);
        return 0;
    }

    if ((newmem = (char *)malloc(size)) == NULL) {
        int oserr = errno;

        oom_occurred();
        slapi_log_err(SLAPI_LOG_ERR, SLAPD_MODULE,
                      "malloc of %lu bytes failed; OS error %d (%s)%s\n",
                      size, oserr, slapd_system_strerror(oserr), oom_advice);
        exit(1);
    }
    /* So long as this happens once, we are happy, put it in ch_malloc. */
    create_oom_buffer();

    return (newmem);
}

/* See slapi-plugin.h */
char *
slapi_ch_memalign(uint32_t size, uint32_t alignment)
{
    char *newmem;

    if (posix_memalign((void **)&newmem, alignment, size) != 0) {
        int oserr = errno;

        oom_occurred();
        slapi_log_err(SLAPI_LOG_ERR, SLAPD_MODULE,
                      "malloc of %" PRIu32 " bytes failed; OS error %d (%s)%s\n",
                      size, oserr, slapd_system_strerror(oserr), oom_advice);
        exit(1);
    }

    return (newmem);
}

char *
slapi_ch_realloc(
    char *block,
    unsigned long size)
{
    char *newmem;

    if (block == NULL) {
        return (slapi_ch_malloc(size));
    }

    if (size <= 0) {
        log_negative_alloc_msg("realloc", "bytes", size);
        return block;
    }

    if ((newmem = (char *)realloc(block, size)) == NULL) {
        int oserr = errno;

        oom_occurred();
        slapi_log_err(SLAPI_LOG_ERR, SLAPD_MODULE,
                      "realloc of %lu bytes failed; OS error %d (%s)%s\n",
                      size, oserr, slapd_system_strerror(oserr), oom_advice);
        exit(1);
    }

    return (newmem);
}

char *
slapi_ch_calloc(
    unsigned long nelem,
    unsigned long size)
{
    char *newmem;

    if (size <= 0) {
        log_negative_alloc_msg("calloc", "bytes", size);
        return 0;
    }

    if (nelem <= 0) {
        log_negative_alloc_msg("calloc", "elements", nelem);
        return 0;
    }

    if ((newmem = (char *)calloc(nelem, size)) == NULL) {
        int oserr = errno;

        oom_occurred();
        slapi_log_err(SLAPI_LOG_ERR, SLAPD_MODULE,
                      "calloc of %lu elems of %lu bytes failed; OS error %d (%s)%s\n",
                      nelem, size, oserr, slapd_system_strerror(oserr), oom_advice);
        exit(1);
    }

    return (newmem);
}

char *
slapi_ch_strdup(const char *s1)
{
    char *newmem;

    /* strdup pukes on NULL strings...bail out now */
    if (NULL == s1)
        return NULL;
    newmem = strdup(s1);
    if (newmem == NULL) {
        int oserr = errno;
        oom_occurred();
        slapi_log_err(SLAPI_LOG_ERR, SLAPD_MODULE,
                      "strdup of %lu characters failed; OS error %d (%s)%s\n",
                      (unsigned long)strlen(s1), oserr, slapd_system_strerror(oserr),
                      oom_advice);
        exit(1);
    }

    return newmem;
}

struct berval *
slapi_ch_bvdup(const struct berval *v)
{
    struct berval *newberval = ber_bvdup((struct berval *)v);
    if (newberval == NULL) {
        int oserr = errno;

        oom_occurred();
        slapi_log_err(SLAPI_LOG_ERR, SLAPD_MODULE,
                      "ber_bvdup of %lu bytes failed; OS error %d (%s)%s\n",
                      (unsigned long)v->bv_len, oserr, slapd_system_strerror(oserr),
                      oom_advice);
        exit(1);
    }
    return newberval;
}

struct berval **
slapi_ch_bvecdup(struct berval **v)
{
    struct berval **newberval = NULL;
    if (v != NULL) {
        size_t i = 0;
        while (v[i] != NULL)
            ++i;
        newberval = (struct berval **)slapi_ch_malloc((i + 1) * sizeof(struct berval *));
        newberval[i] = NULL;
        while (i-- > 0) {
            newberval[i] = slapi_ch_bvdup(v[i]);
        }
    }
    return newberval;
}

/*
 *  Function: slapi_ch_free
 *
 *  Returns: nothing
 *
 *  Description: frees the pointer, and then sets it to NULL to
 *               prevent free-memory writes.
 *               Note: pass in the address of the pointer you want to free.
 *               Note: you can pass in null pointers, it's cool.
 */
void
slapi_ch_free(void **ptr)
{
    /* Man 3 free
     * If ptr is NULL, no operation is performed. We only need to check ptr
     * has a value so that *ptr won't SIGSEGV
     */
    if (ptr == NULL) {
        return;
    }

    free(*ptr);
    *ptr = NULL;
    return;
}


/* just like slapi_ch_free, takes the address of the struct berval pointer */
void
slapi_ch_bvfree(struct berval **v)
{
    if (v == NULL || *v == NULL)
        return;

    slapi_ch_free((void **)&((*v)->bv_val));
    slapi_ch_free((void **)v);

    return;
}

/* just like slapi_ch_free, but the argument is the address of a string
   This helps with compile time error checking
*/
void
slapi_ch_free_string(char **s)
{
    slapi_ch_free((void **)s);
}

/*
  This function is just like PR_smprintf.  It works like sprintf
  except that it allocates enough memory to hold the result
  string and returns that allocated memory to the caller.  The
  caller must use slapi_ch_free_string to free the memory.
  It should only be used in those situations that will eventually free
  the memory using slapi_ch_free_string e.g. allocating a string
  that will be freed as part of pblock cleanup, or passed in to create
  a Slapi_DN, or things of that nature.  If you have control of the
  flow such that the memory will be allocated and freed in the same
  scope, better to just use PR_smprintf and PR_smprintf_free instead
  because it is likely faster.
*/
/*
  This implementation is the same as PR_smprintf.
  The above comment does not apply to this function for now.
  see [150809] for more details.
  WARNING - with this fix, this means we are now mixing PR_Malloc with
  slapi_ch_free.  Which is ok for now - they both use malloc/free from
  the operating system.  But if this changes in the future, this
  function will have to change as well.
*/
char *
slapi_ch_smprintf(const char *fmt, ...)
{
    char *p = NULL;
    va_list ap;

    if (NULL == fmt) {
        return NULL;
    }

    va_start(ap, fmt);
    p = PR_vsmprintf(fmt, ap);
    va_end(ap);

    return p;
}

/* Constant time memcmp. Does not shortcircuit on failure! */
/* This relies on p1 and p2 both being size at least n! */
int
slapi_ct_memcmp(const void *p1, const void *p2, size_t n)
{
    int result = 0;
    const unsigned char *_p1 = (const unsigned char *)p1;
    const unsigned char *_p2 = (const unsigned char *)p2;

    if (_p1 == NULL || _p2 == NULL) {
        return 2;
    }

    for (size_t i = 0; i < n; i++) {
        if (_p1[i] ^ _p2[i]) {
            result = 1;
        }
    }
    return result;
}

#define SYS_CACHELINE_SIZE "/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size"
static
size_t cache_line_size() 
{
    PRFileDesc *prfd;
    char buf[10] = {0};
    size_t i = 0;

    prfd = PR_Open(SYS_CACHELINE_SIZE, PR_RDONLY, SLAPD_DEFAULT_FILE_MODE);
    if (PR_Read(prfd, buf, sizeof (buf)) < 0) {
        slapi_log_err(SLAPI_LOG_ERR, "cache_line_size", "Can not read %s\n", SYS_CACHELINE_SIZE);
        PR_Close(prfd);
        goto done;
    }
    PR_Close(prfd);
    i = atoi(buf);

done:
    if ((i == 0) || (i > 64)) {
        slapi_log_err(SLAPI_LOG_INFO, "cache_line_size", "unexpected cpu cache line size => assign 64\n");
        i = 64;
    }
    return i;
}

pthread_mutex_t*
slapi_pthread_mutex_alloc(int type)
{
    int32_t cacheLineSiz = cache_line_size();
    int32_t aligned_size = ((sizeof(pthread_mutex_t) + cacheLineSiz - 1) / cacheLineSiz ) * cacheLineSiz;
    pthread_mutex_t *aligned_mutex;
    pthread_mutex_t orig_mutex = {0};

    /* Allocate the unaligned mutex */
    pthread_mutexattr_t monitor_attr = {0};
    pthread_mutexattr_init(&monitor_attr);
    pthread_mutexattr_settype(&monitor_attr, type);
    if (pthread_mutex_init(&orig_mutex, &monitor_attr) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_pthread_mutex_alloc", "pthread_mutex_init failed\n");
        return NULL;
    }
    /* now align it to buffer aligned to the cpu cache line */
    aligned_mutex = (pthread_mutex_t *) slapi_ch_memalign(aligned_size, cacheLineSiz);
    memmove(aligned_mutex, (const void *) &orig_mutex, sizeof(pthread_mutex_t));
    return(aligned_mutex);
}

void
slapi_pthread_mutex_free(pthread_mutex_t **mutex)
{
    slapi_ch_free((void **) mutex);
}


/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2017 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

/*
 * Implementation of functions to abstract from platform
 * specific issues.
 */

/* Provide ch_malloc etc. */
#include <slapi-plugin.h>
/* Provide slapi_log_err macro wrapper */
#include <slapi-private.h>
#include <slapi_pal.h>

/* Assert macros */
#include <assert.h>
/* Access errno */
#include <errno.h>

/* For getpagesize */
#include <unistd.h>

/* For rlimit */
#include <sys/time.h>
#include <sys/resource.h>

#ifdef OS_solaris
#include <sys/procfs.h>
#endif

#if defined ( hpux )
#include <sys/pstat.h>
#endif

static int_fast32_t
_spal_rlimit_get(int resource, uint64_t *soft_limit, uint64_t *hard_limit) {
    struct rlimit rl = {0};

    if (getrlimit(resource, &rl) != 0) {
        int errsrv = errno;
        slapi_log_err(SLAPI_LOG_ERR, "_spal_rlimit_mem_get", "Failed to access system resource limits %d\n", errsrv);
        return 1;
    }

    if (rl.rlim_cur != RLIM_INFINITY) {
        *soft_limit = (uint64_t)rl.rlim_cur;
    }
    if (rl.rlim_max != RLIM_INFINITY) {
        *hard_limit = (uint64_t)rl.rlim_max;
    }

    return 0;
}


#ifdef LINUX
static int_fast32_t
_spal_uint64_t_file_get(char *name, char *prefix, uint64_t *dest) {
    FILE *f;
    char s[40] = {0};
    size_t prefix_len = strlen(prefix);

    /* Make sure we can fit into our buffer */
    assert((prefix_len + 20) < 39);

    f = fopen(name, "r");
    if (!f) {    /* fopen failed */
        int errsrv = errno;
        slapi_log_err(SLAPI_LOG_ERR,"_spal_get_uint64_t_file", "Unable to open file \"%s\". errno=%d\n", name, errsrv);
        return 1;
    }

    int_fast32_t retval = 0;
    while (! feof(f)) {
        if (!fgets(s, 39, f)) {
            retval = 1;
            break; /* error or eof */
        }
        if (feof(f)) {
            retval = 1;
            break;
        }
        if (strncmp(s, prefix, prefix_len) == 0) {
            sscanf(s + prefix_len, "%"SCNu64, dest);
            break;
        }
    }
    fclose(f);
    return retval;
}



slapi_pal_meminfo *
spal_meminfo_get() {
    slapi_pal_meminfo *mi = (slapi_pal_meminfo *)slapi_ch_calloc(1, sizeof(slapi_pal_meminfo));

    mi->pagesize_bytes = getpagesize();

    /*
     * We have to compare values from a number of sources to ensure we have
     * the correct result.
     */

    char f_proc_status[30] = {0};
    sprintf(f_proc_status, "/proc/%d/status", getpid());
    char *p_vmrss = "VmRSS:";
    uint64_t vmrss = 0;

    if (_spal_uint64_t_file_get(f_proc_status, p_vmrss, &vmrss)) {
        slapi_log_err(SLAPI_LOG_ERR, "spal_meminfo_get", "Unable to retrieve vmrss\n");
    }

    /* vmrss is in kb, so convert to bytes */
    vmrss = vmrss * 1024;

    uint64_t rl_mem_soft = 0;
    uint64_t rl_mem_hard = 0;
    uint64_t rl_mem_soft_avail = 0;

    if (_spal_rlimit_get(RLIMIT_AS, &rl_mem_soft, &rl_mem_hard)) {
        slapi_log_err(SLAPI_LOG_ERR, "spal_meminfo_get", "Unable to retrieve memory rlimit\n");
    }

    if (rl_mem_soft != 0 && rl_mem_soft > vmrss) {
        rl_mem_soft_avail = rl_mem_soft - vmrss;
    }

    char *f_meminfo = "/proc/meminfo";
    char *p_memtotal = "MemTotal:";
    char *p_memavail = "MemAvailable:";

    uint64_t memtotal = 0;
    uint64_t memavail = 0;

    if (_spal_uint64_t_file_get(f_meminfo, p_memtotal, &memtotal)) {
        slapi_log_err(SLAPI_LOG_ERR, "spal_meminfo_get", "Unable to retrieve %s : %s\n", f_meminfo, p_memtotal);
    }

    if (_spal_uint64_t_file_get(f_meminfo, p_memavail, &memavail)) {
        slapi_log_err(SLAPI_LOG_ERR, "spal_meminfo_get", "Unable to retrieve %s : %s\n", f_meminfo, p_memavail);
    }

    /* Both memtotal and memavail are in kb */
    memtotal = memtotal * 1024;
    memavail = memavail * 1024;

    /* Now, compare the values and make a choice to which is provided */

    /* Process consumed memory */
    mi->process_consumed_bytes = vmrss;
    mi->process_consumed_pages = vmrss / mi->pagesize_bytes;

    /* System Total memory */
    /*                       If we have a memtotal, OR if no memtotal but rlimit */
    if (rl_mem_hard != 0 && ((memtotal != 0 && rl_mem_hard < memtotal) || memtotal == 0)) {
        mi->system_total_bytes = rl_mem_hard;
        mi->system_total_pages = rl_mem_hard / mi->pagesize_bytes;
    } else if (memtotal != 0) {
        mi->system_total_bytes = memtotal;
        mi->system_total_pages = memtotal / mi->pagesize_bytes;
    } else {
        slapi_log_err(SLAPI_LOG_CRIT, "spal_meminfo_get", "Unable to determine system total memory!\n");
        spal_meminfo_destroy(mi);
        return NULL;
    }

    /* System Available memory */

    if (rl_mem_soft_avail != 0 && ((memavail != 0 && (rl_mem_soft_avail) < memavail) || memavail == 0)) {
        mi->system_available_bytes = rl_mem_soft_avail;
        mi->system_available_pages = rl_mem_soft_avail / mi->pagesize_bytes;
    } else if (rl_mem_soft != 0 && ((memavail != 0 && (rl_mem_soft) < memavail) || memavail == 0)) {
        mi->system_available_bytes = rl_mem_soft;
        mi->system_available_pages = rl_mem_soft / mi->pagesize_bytes;
    } else if (memavail != 0) {
        mi->system_available_bytes = memavail;
        mi->system_available_pages = memavail / mi->pagesize_bytes;
    } else {
        slapi_log_err(SLAPI_LOG_CRIT, "spal_meminfo_get", "Unable to determine system available memory!\n");
        spal_meminfo_destroy(mi);
        return NULL;
    }

    slapi_log_err(SLAPI_LOG_TRACE, "spal_meminfo_get", "{pagesize_bytes = %"PRIu64", system_total_pages = %"PRIu64", system_total_bytes = %"PRIu64", process_consumed_pages = %"PRIu64", process_consumed_bytes = %"PRIu64", system_available_pages = %"PRIu64", system_available_bytes = %"PRIu64"},\n",
        mi->pagesize_bytes, mi->system_total_pages, mi->system_total_bytes, mi->process_consumed_pages, mi->process_consumed_bytes, mi->system_available_pages, mi->system_available_bytes);

    return mi;
}


#endif

#ifdef OS_solaris
uint64_t
_spal_solaris_resident_pages_get() {
    uint64_t procpages = 0;
    struct prpsinfo psi = {0};
    char fn[40];
    int fd;

    sprintf(fn, "/proc/%d", getpid());
    fd = open(fn, O_RDONLY);
    if (fd >= 0) {
        if (ioctl(fd, PIOCPSINFO, (void *)&psi) == 0) {
            procpages = (uint64_t)psi.pr_size;
        }
        close(fd);
    }
    return procpages;
}

slapi_pal_meminfo *
spal_meminfo_get() {
    slapi_pal_meminfo *mi = (slapi_pal_meminfo *)slapi_ch_calloc(1, sizeof(slapi_pal_meminfo));

    uint64_t rl_mem_soft = 0;
    uint64_t rl_mem_hard = 0;

    if (_spal_rlimit_get(RLIMIT_AS, &rl_mem_soft, &rl_mem_hard)) {
        slapi_log_err(SLAPI_LOG_ERR, "spal_meminfo_get", "Unable to retrieve memory rlimit\n");
    }

    mi->pagesize_bytes = sysconf(_SC_PAGESIZE);
    mi->system_total_pages = sysconf(_SC_PHYS_PAGES);
    mi->system_total_bytes = mi->system_total_pages * mi->pagesize_bytes;
    mi->system_available_bytes = rl_mem_soft;
    if (rl_mem_soft != 0) {
        mi->system_available_pages = rl_mem_soft / mi->pagesize_bytes;
    }
    mi->process_consumed_pages = _spal_solaris_resident_pages_get();
    mi->process_consumed_bytes = mi->process_consumed_pages * mi->pagesize_bytes;

    return mi;

}
#endif

#ifdef HPUX
#endif

void
spal_meminfo_destroy(slapi_pal_meminfo *mi) {
    slapi_ch_free((void **)&mi);
}

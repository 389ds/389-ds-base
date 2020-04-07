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

/* directory check */
#include <dirent.h>

#ifdef OS_solaris
#include <sys/procfs.h>
#endif

#if defined(hpux)
#include <sys/pstat.h>
#endif

#include <sys/param.h>

/* This warns if we have less than 128M avail */
#define SPAL_WARN_MIN_BYTES 134217728

#define CG2_HEADER_FORMAT "0::"
#define CG2_HEADER_LEN strlen(CG2_HEADER_FORMAT)

static int_fast32_t
_spal_rlimit_get(int resource, uint64_t *soft_limit, uint64_t *hard_limit)
{
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
_spal_uint64_t_file_get(char *name, char *prefix, uint64_t *dest)
{
    FILE *f;
    char s[40] = {0};
    size_t prefix_len = 0;

    if (prefix != NULL) {
        prefix_len = strlen(prefix);
    }

    /* Make sure we can fit into our buffer */
    assert((prefix_len + 20) < 39);

    f = fopen(name, "r");
    if (!f) { /* fopen failed */
        int errsrv = errno;
        slapi_log_err(SLAPI_LOG_ERR, "_spal_get_uint64_t_file", "Unable to open file \"%s\". errno=%d\n", name, errsrv);
        return 1;
    }

    int_fast32_t retval = 0;
    while (!feof(f)) {
        if (!fgets(s, 39, f)) {
            retval = 1;
            break; /* error or eof */
        }
        if (feof(f)) {
            retval = 1;
            break;
        }
        if (prefix_len > 0 && strncmp(s, prefix, prefix_len) == 0) {
            sscanf(s + prefix_len, "%" SCNu64, dest);
            break;
        } else if (prefix_len == 0) {
            sscanf(s, "%" SCNu64, dest);
            break;
        }
    }
    fclose(f);
    return retval;
}

static int_fast32_t
_spal_dir_exist(char *path)
{
    DIR* dir = opendir(path);
    if (dir) {
        closedir(dir);
        return 1;
    }
    return 0;
}

static char *
_spal_cgroupv2_path() {
    FILE *f;
    char s[MAXPATHLEN + 1] = {0};
    char *res = NULL;
    /* We discover our path by looking at /proc/self/cgroup */
    f = fopen("/proc/self/cgroup", "r");
    if (!f) {
        int errsrv = errno;
        slapi_log_err(SLAPI_LOG_ERR, "_spal_get_uint64_t_file", "Unable to open file \"/proc/self/cgroup\". errno=%d\n", errsrv);
        return NULL;
    }

    if (feof(f) == 0) {
        if (fgets(s, MAXPATHLEN, f) != NULL) {
            /* we now have a path in s, and the last byte must be NULL */
            if ((strlen(s) >= CG2_HEADER_LEN) && strncmp(s, CG2_HEADER_FORMAT, CG2_HEADER_LEN) == 0) {
                res = slapi_ch_calloc(1, MAXPATHLEN + 17);
                snprintf(res, MAXPATHLEN + 16, "/sys/fs/cgroup%s", s + CG2_HEADER_LEN);
                /* This always has a new line, so replace it if possible. */
                size_t nl = strlen(res) - 1;
                res[nl] = '\0';
            }
        }
    }
    /* Will return something like /sys/fs/cgroup/system.slice/system-dirsrv.slice/dirsrv@standalone1.service */

    fclose(f);
    return res;
}


slapi_pal_meminfo *
spal_meminfo_get()
{
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

    if (rl_mem_soft != 0 && vmrss != 0 && rl_mem_soft > vmrss) {
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

    /*
     * Oracle Enterprise Linux doesn't provide a valid memavail value, so fall
     * back to 80% of memtotal.
     */
    if (memavail == 0) {
        memavail = memtotal * 0.8;
    } else {
        memavail = memavail * 1024;
    }

    /* If it's possible, get our cgroup info */
    uint64_t cg_mem_soft = 0;
    uint64_t cg_mem_hard = 0;
    uint64_t cg_mem_usage = 0;
    uint64_t cg_mem_soft_avail = 0;

    /* We have cgroup v1, so attempt to use it. */
    if (_spal_dir_exist("/sys/fs/cgroup/memory")) {
        char *f_cg_soft = "/sys/fs/cgroup/memory/memory.soft_limit_in_bytes";
        char *f_cg_hard = "/sys/fs/cgroup/memory/memory.limit_in_bytes";
        char *f_cg_usage = "/sys/fs/cgroup/memory/memory.usage_in_bytes";
        slapi_log_err(SLAPI_LOG_INFO, "spal_meminfo_get", "Found cgroup v1\n");

        if (_spal_uint64_t_file_get(f_cg_soft, NULL, &cg_mem_soft)) {
            slapi_log_err(SLAPI_LOG_WARNING, "spal_meminfo_get", "Unable to retrieve %s. There may be no cgroup support on this platform\n", f_cg_soft);
        }

        if (_spal_uint64_t_file_get(f_cg_hard, NULL, &cg_mem_hard)) {
            slapi_log_err(SLAPI_LOG_WARNING, "spal_meminfo_get", "Unable to retrieve %s. There may be no cgroup support on this platform\n", f_cg_hard);
        }

        if (_spal_uint64_t_file_get(f_cg_usage, NULL, &cg_mem_usage)) {
            slapi_log_err(SLAPI_LOG_WARNING, "spal_meminfo_get", "Unable to retrieve %s. There may be no cgroup support on this platform\n", f_cg_usage);
        }

    } else {
        /* We might have cgroup v2. Attempt to get the controller path ... */
        char *ctrlpath = _spal_cgroupv2_path();
        if (ctrlpath != NULL) {

            char s[MAXPATHLEN + 33] = {0};
            slapi_log_err(SLAPI_LOG_INFO, "spal_meminfo_get", "Found cgroup v2 -> %s\n", ctrlpath);
            /* There are now three files we care about - memory.current, memory.high and memory.max */
            /* For simplicity we re-use soft and hard */
            /* If _spal_uint64_t_file_get() hit's "max" then these remain at 0 */
            snprintf(s, MAXPATHLEN + 32, "%s/memory.current", ctrlpath);
            if (_spal_uint64_t_file_get(s, NULL, &cg_mem_usage)) {
                slapi_log_err(SLAPI_LOG_WARNING, "spal_meminfo_get", "Unable to retrieve %s. There may be no cgroup support on this platform\n", s);
            }

            snprintf(s, MAXPATHLEN + 32, "%s/memory.high", ctrlpath);
            if (_spal_uint64_t_file_get(s, NULL, &cg_mem_soft)) {
                slapi_log_err(SLAPI_LOG_WARNING, "spal_meminfo_get", "Unable to retrieve %s. There may be no cgroup support on this platform\n", s);
            }

            snprintf(s, MAXPATHLEN + 32, "%s/memory.max", ctrlpath);
            if (_spal_uint64_t_file_get(s, NULL, &cg_mem_hard)) {
                slapi_log_err(SLAPI_LOG_WARNING, "spal_meminfo_get", "Unable to retrieve %s. There may be no cgroup support on this platform\n", s);
            }

            slapi_ch_free_string(&ctrlpath);
        } else {
            slapi_log_err(SLAPI_LOG_WARNING, "spal_meminfo_get", "cgroups v1 or v2 unable to be read - may not be on this platform ...\n");
        }
    }

    /*
     * In some conditions, like docker, we only have a *hard* limit set.
     * This obviously breaks our logic, so we need to make sure we correct this
     */
    if ((cg_mem_hard != 0 && cg_mem_soft == 0) || (cg_mem_hard < cg_mem_soft)) {
        /* Right, we only have a hard limit. Impose a 20% watermark. */
        cg_mem_soft = cg_mem_hard * 0.8;
    }

    if (cg_mem_usage != 0 && (cg_mem_soft != 0 || cg_mem_hard != 0)) {
        if (cg_mem_soft > cg_mem_usage) {
            cg_mem_soft_avail = cg_mem_soft - cg_mem_usage;
        } else if (cg_mem_hard > cg_mem_usage) {
            cg_mem_soft_avail = cg_mem_hard - cg_mem_usage;
        } else {
            slapi_log_err(SLAPI_LOG_CRIT, "spal_meminfo_get", "Your cgroup memory usage exceeds your hard limit?");
        }
    }


    /* Now, compare the values and make a choice to which is provided */

    /* Process consumed memory */
    mi->process_consumed_bytes = vmrss;
    mi->process_consumed_pages = vmrss / mi->pagesize_bytes;

    /* System Total memory */
    /*                       If we have a memtotal, OR if no memtotal but rlimit */
    if (rl_mem_hard != 0 &&
        ((memtotal != 0 && rl_mem_hard < memtotal) || memtotal == 0) &&
        ((cg_mem_hard != 0 && rl_mem_hard < cg_mem_hard) || cg_mem_hard == 0)) {
        slapi_log_err(SLAPI_LOG_TRACE, "spal_meminfo_get", "system_total_bytes - using rlimit\n");
        mi->system_total_bytes = rl_mem_hard;
        mi->system_total_pages = rl_mem_hard / mi->pagesize_bytes;
    } else if (cg_mem_hard != 0 && ((memtotal != 0 && cg_mem_hard < memtotal) || memtotal == 0)) {
        slapi_log_err(SLAPI_LOG_TRACE, "spal_meminfo_get", "system_total_bytes - using cgroup\n");
        mi->system_total_bytes = cg_mem_hard;
        mi->system_total_pages = cg_mem_hard / mi->pagesize_bytes;
    } else if (memtotal != 0) {
        slapi_log_err(SLAPI_LOG_TRACE, "spal_meminfo_get", "system_total_bytes - using memtotal\n");
        mi->system_total_bytes = memtotal;
        mi->system_total_pages = memtotal / mi->pagesize_bytes;
    } else {
        slapi_log_err(SLAPI_LOG_CRIT, "spal_meminfo_get", "Unable to determine system total memory!\n");
        spal_meminfo_destroy(mi);
        return NULL;
    }

    /* System Available memory */

    if (rl_mem_soft_avail != 0 &&
        ((memavail != 0 && (rl_mem_soft_avail) < memavail) || memavail == 0) &&
        ((cg_mem_soft_avail != 0 && rl_mem_soft_avail < cg_mem_soft_avail) || cg_mem_soft_avail == 0)) {
        slapi_log_err(SLAPI_LOG_TRACE, "spal_meminfo_get", "system_available_bytes - using rlimit\n");
        mi->system_available_bytes = rl_mem_soft_avail;
        mi->system_available_pages = rl_mem_soft_avail / mi->pagesize_bytes;
    } else if (cg_mem_soft_avail != 0 && ((memavail != 0 && (cg_mem_soft_avail) < memavail) || memavail == 0)) {
        slapi_log_err(SLAPI_LOG_TRACE, "spal_meminfo_get", "system_available_bytes - using cgroup\n");
        mi->system_available_bytes = cg_mem_soft_avail;
        mi->system_available_pages = cg_mem_soft_avail / mi->pagesize_bytes;
    } else if (memavail != 0) {
        slapi_log_err(SLAPI_LOG_TRACE, "spal_meminfo_get", "system_available_bytes - using memavail\n");
        mi->system_available_bytes = memavail;
        mi->system_available_pages = memavail / mi->pagesize_bytes;
    } else {
        slapi_log_err(SLAPI_LOG_CRIT, "spal_meminfo_get", "Unable to determine system available memory!\n");
        spal_meminfo_destroy(mi);
        return NULL;
    }

    if (mi->system_available_bytes < SPAL_WARN_MIN_BYTES) {
        slapi_log_err(SLAPI_LOG_CRIT, "spal_meminfo_get", "Your system is reporting %" PRIu64" bytes available, which is less than the minimum recommended %" PRIu64 " bytes\n",
            mi->system_available_bytes, SPAL_WARN_MIN_BYTES);
        slapi_log_err(SLAPI_LOG_CRIT, "spal_meminfo_get", "This indicates heavy memory pressure or incorrect system resource allocation\n");
        slapi_log_err(SLAPI_LOG_CRIT, "spal_meminfo_get", "Directory Server *may* crash as a result!!!\n");
    }

    slapi_log_err(SLAPI_LOG_TRACE, "spal_meminfo_get", "{pagesize_bytes = %" PRIu64 ", system_total_pages = %" PRIu64 ", system_total_bytes = %" PRIu64 ", process_consumed_pages = %" PRIu64 ", process_consumed_bytes = %" PRIu64 ", system_available_pages = %" PRIu64 ", system_available_bytes = %" PRIu64 "},\n",
                  mi->pagesize_bytes, mi->system_total_pages, mi->system_total_bytes, mi->process_consumed_pages, mi->process_consumed_bytes, mi->system_available_pages, mi->system_available_bytes);

    return mi;
}


#endif

#ifdef OS_solaris
uint64_t
_spal_solaris_resident_pages_get()
{
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
spal_meminfo_get()
{
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
spal_meminfo_destroy(slapi_pal_meminfo *mi)
{
    slapi_ch_free((void **)&mi);
}

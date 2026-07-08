/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2026 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "slap.h"
#include "plstr.h"
#include "threadpool_stats.h"

#define TP_STATS_COMPONENT "threadpool_stats"
#define TP_STATS_DIR_SUFFIX ".monitor"
#define TP_STATS_FILENAME "threadpool"
#define TP_STATS_HEARTBEAT_INTERVAL_MS 1000
#define TP_STATS_ARCHIVE_KEEP 5

static tp_stats_header_t *tp_stats_header = NULL;
static int tp_stats_fd = -1;
static size_t tp_stats_len = 0;
static uint32_t tp_stats_max_workers = 0;
static char *tp_stats_path = NULL;
static Slapi_Eq_Context tp_stats_eq_ctx = NULL;

static uint64_t
tp_stats_mono_ns(void)
{
    struct timespec ts = slapi_current_rel_time_hr();
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

static void
tp_store_32(uint32_t *ptr, uint32_t val, int memorder)
{
    slapi_atomic_store_32((int32_t *)ptr, (int32_t)val, memorder);
}

static uint32_t
tp_load_32(uint32_t *ptr, int memorder)
{
    return (uint32_t)slapi_atomic_load_32((int32_t *)ptr, memorder);
}

static tp_worker_slot_t *
tp_stats_slot_at(tp_stats_header_t *header, uint32_t idx)
{
    return (tp_worker_slot_t *)((uint8_t *)header +
                                TP_STATS_HEADER_SIZE +
                                ((size_t)idx * TP_STATS_WORKER_SLOT_SIZE));
}

static tp_worker_slot_t *
tp_stats_get_slot(uint32_t worker_idx)
{
    if (tp_stats_header == NULL || worker_idx == 0 || worker_idx > tp_stats_max_workers) {
        return NULL;
    }

    return tp_stats_slot_at(tp_stats_header, worker_idx - 1);
}

static const char *
tp_stats_state_name(uint32_t state)
{
    switch (state) {
    case TP_WORKER_STATE_UNUSED:
        return "unused";
    case TP_WORKER_STATE_IDLE:
        return "idle";
    case TP_WORKER_STATE_BUSY:
        return "busy";
    case TP_WORKER_STATE_EXITED:
        return "exited";
    default:
        return "unknown";
    }
}

static const char *
tp_stats_op_name(uint32_t op_tag, char *buf, size_t buflen)
{
    switch ((ber_tag_t)op_tag) {
    case 0:
        return "";
    case LDAP_REQ_BIND:
        return "bind";
    case LDAP_REQ_UNBIND:
        return "unbind";
    case LDAP_REQ_SEARCH:
        return "search";
    case LDAP_REQ_MODIFY:
        return "modify";
    case LDAP_REQ_ADD:
        return "add";
    case LDAP_REQ_DELETE:
        return "delete";
    case LDAP_REQ_MODRDN:
        return "modrdn";
    case LDAP_REQ_COMPARE:
        return "compare";
    case LDAP_REQ_ABANDON:
        return "abandon";
    case LDAP_REQ_EXTENDED:
        return "extended";
    default:
        snprintf(buf, buflen, "%" PRIu32, op_tag);
        return buf;
    }
}

/*
 * <rundir>/slapd-<instance>.monitor/threadpool, matching what the dsctl
 * reader derives from the instance config. Returns NULL when rundir or
 * the slapd-<instance> name cannot be resolved: a file at any other path
 * would be unreachable for the reader, so the caller disables the
 * feature instead.
 */
static char *
tp_stats_make_path(void)
{
    char *rundir = config_get_rundir();
    char *configdir = config_get_configdir();
    char *instname = NULL;
    char *path = NULL;

    if (configdir != NULL) {
        instname = PL_strrstr(configdir, "slapd-");
    }

    if (rundir != NULL && instname != NULL) {
        path = slapi_ch_smprintf("%s/%s%s/%s", rundir, instname,
                                 TP_STATS_DIR_SUFFIX, TP_STATS_FILENAME);
    }
    slapi_ch_free_string(&rundir);
    slapi_ch_free_string(&configdir);
    return path;
}

/*
 * Create the per-instance monitor directory the status file lives in.
 * A pre-existing entry is accepted only when lstat says it is a real
 * directory owned by the server (a planted symlink fails the check).
 * The directory is never removed at shutdown: crash archives stay in it.
 */
static int
tp_stats_prepare_dir(const char *path)
{
    const char *slash = strrchr(path, '/');
    struct stat st = {0};
    char *dir = NULL;
    int rc = -1;

    if (slash == NULL) {
        return -1;
    }
    dir = slapi_ch_smprintf("%.*s", (int)(slash - path), path);

    if (mkdir(dir, 0750) != 0) {
        if (errno != EEXIST) {
            int err = errno;
            slapi_log_err(SLAPI_LOG_WARNING, TP_STATS_COMPONENT,
                          "Could not create thread-pool monitor directory %s: %d (%s)\n",
                          dir, err, slapd_system_strerror(err));
            goto done;
        }
        if (lstat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid()) {
            slapi_log_err(SLAPI_LOG_WARNING, TP_STATS_COMPONENT,
                          "Refusing unsafe thread-pool monitor directory %s (mode=%o uid=%ld)\n",
                          dir, (unsigned int)st.st_mode, (long)st.st_uid);
            goto done;
        }
    }

    if (chmod(dir, 0750) != 0) {
        int err = errno;
        slapi_log_err(SLAPI_LOG_WARNING, TP_STATS_COMPONENT,
                      "Could not set permissions on thread-pool monitor directory %s: %d (%s)\n",
                      dir, err, slapd_system_strerror(err));
        goto done;
    }
    rc = 0;

done:
    slapi_ch_free_string(&dir);
    return rc;
}

static int
tp_stats_name_cmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static bool
tp_stats_is_archive_suffix(const char *suffix)
{
    if (strlen(suffix) != 15 || suffix[8] != '-') {
        return false;
    }
    for (int i = 0; i < 15; i++) {
        if (i != 8 && !isdigit((unsigned char)suffix[i])) {
            return false;
        }
    }
    return true;
}

/* Remove the oldest archives so at most TP_STATS_ARCHIVE_KEEP remain */
static void
tp_stats_prune_archives(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *base = NULL;
    size_t baselen;
    char *dir = NULL;
    DIR *dirp = NULL;
    struct dirent *entry = NULL;
    char **names = NULL;
    size_t count = 0;

    if (slash == NULL) {
        return;
    }
    base = slash + 1;
    baselen = strlen(base);
    dir = slapi_ch_smprintf("%.*s", (int)(slash - path), path);
    dirp = opendir(dir);
    if (dirp == NULL) {
        slapi_ch_free_string(&dir);
        return;
    }

    while ((entry = readdir(dirp)) != NULL) {
        const char *name = entry->d_name;
        if (strncmp(name, base, baselen) != 0 || name[baselen] != '.' ||
            !tp_stats_is_archive_suffix(name + baselen + 1)) {
            continue;
        }
        names = (char **)slapi_ch_realloc((char *)names, (count + 1) * sizeof(char *));
        names[count++] = slapi_ch_strdup(name);
    }
    closedir(dirp);

    if (count > TP_STATS_ARCHIVE_KEEP) {
        /* The timestamp suffix sorts lexicographically in time order */
        qsort(names, count, sizeof(char *), tp_stats_name_cmp);
        for (size_t i = 0; i < count - TP_STATS_ARCHIVE_KEEP; i++) {
            char *victim = slapi_ch_smprintf("%s/%s", dir, names[i]);
            if (unlink(victim) != 0) {
                int err = errno;
                slapi_log_err(SLAPI_LOG_WARNING, TP_STATS_COMPONENT,
                              "Could not remove old thread-pool status archive %s: %d (%s)\n",
                              victim, err, slapd_system_strerror(err));
            } else {
                slapi_log_err(SLAPI_LOG_INFO, TP_STATS_COMPONENT,
                              "Removed old thread-pool status archive %s\n", victim);
            }
            slapi_ch_free_string(&victim);
        }
    }

    for (size_t i = 0; i < count; i++) {
        slapi_ch_free_string(&names[i]);
    }
    slapi_ch_free((void **)&names);
    slapi_ch_free_string(&dir);
}

/*
 * Preserve a leftover status file from a crashed previous run by renaming
 * it to <path>.YYYYMMDD-HHMMSS (the rotated-log naming). Never unlinks and
 * never fails startup: on any failure it returns having done nothing and
 * the caller's unlink handles the leftover as before. Only a genuine crash
 * leftover is preserved: the magic must match and shutdown_clean must be
 * unset (tp_stats_close sets it before attempting the unlink).
 */
static void
tp_stats_archive_crash_file(const char *path)
{
    tp_stats_header_t hdr = {0};
    struct stat st = {0};
    struct tm tms = {0};
    char tbuf[32] = {0};
    char *archive = NULL;
    time_t now;
    ssize_t nread;
    int fd;

    fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        return;
    }
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        st.st_nlink != 1 || st.st_size < (off_t)TP_STATS_HEADER_SIZE) {
        close(fd);
        return;
    }
    nread = pread(fd, &hdr, sizeof(hdr), 0);
    close(fd);
    if (nread != (ssize_t)sizeof(hdr) || hdr.magic != TP_STATS_MAGIC ||
        hdr.shutdown_clean != 0) {
        return;
    }

    now = slapi_current_utc_time();
    if (localtime_r(&now, &tms) == NULL ||
        strftime(tbuf, sizeof(tbuf), "%Y%m%d-%H%M%S", &tms) == 0) {
        return;
    }

    archive = slapi_ch_smprintf("%s.%s", path, tbuf);
    if (rename(path, archive) != 0) {
        int err = errno;
        slapi_log_err(SLAPI_LOG_WARNING, TP_STATS_COMPONENT,
                      "Could not preserve thread-pool status file %s from unclean shutdown: "
                      "%d (%s); removing it\n",
                      path, err, slapd_system_strerror(err));
        slapi_ch_free_string(&archive);
        return;
    }

    slapi_log_err(SLAPI_LOG_NOTICE, TP_STATS_COMPONENT,
                  "Previous server run did not shut down cleanly; "
                  "thread-pool status preserved as %s\n",
                  archive);
    slapi_ch_free_string(&archive);
    tp_stats_prune_archives(path);
}

static void
tp_stats_cleanup_open_failure(int fd, char **path)
{
    if (fd >= 0) {
        close(fd);
    }
    if (path != NULL && *path != NULL) {
        unlink(*path);
        slapi_ch_free_string(path);
    }
}

void
tp_collect_gauges(tp_gauges_t *out)
{
    long cur_connections;

    if (out == NULL) {
        return;
    }

    out->cur_work_queue = (uint64_t)get_work_q_size();
    out->max_work_queue = (uint64_t)get_work_q_size_max();
    out->cur_busy_workers = (uint64_t)get_busy_worker_count();
    out->max_busy_workers = (uint64_t)get_max_busy_worker_count();
    out->ops_initiated = (uint64_t)g_get_num_ops_initiated();
    out->ops_completed = (uint64_t)g_get_num_ops_completed();

    cur_connections = g_get_current_conn_count();
    out->cur_connections = cur_connections > 0 ? (uint64_t)cur_connections : 0;
}

static void
tp_stats_publish_gauges(tp_stats_header_t *header, tp_gauges_t *gauges)
{
    slapi_atomic_store_64(&header->cur_work_queue, gauges->cur_work_queue, __ATOMIC_RELAXED);
    slapi_atomic_store_64(&header->max_work_queue, gauges->max_work_queue, __ATOMIC_RELAXED);
    slapi_atomic_store_64(&header->cur_busy_workers, gauges->cur_busy_workers, __ATOMIC_RELAXED);
    slapi_atomic_store_64(&header->max_busy_workers, gauges->max_busy_workers, __ATOMIC_RELAXED);
    slapi_atomic_store_64(&header->ops_initiated, gauges->ops_initiated, __ATOMIC_RELAXED);
    slapi_atomic_store_64(&header->ops_completed, gauges->ops_completed, __ATOMIC_RELAXED);
    slapi_atomic_store_64(&header->cur_connections, gauges->cur_connections, __ATOMIC_RELAXED);
}

static void
tp_stats_heartbeat(time_t when __attribute__((unused)), void *arg __attribute__((unused)))
{
    tp_gauges_t gauges = {0};
    tp_stats_header_t *header = tp_stats_header;

    if (header == NULL) {
        return;
    }

    tp_collect_gauges(&gauges);
    tp_stats_publish_gauges(header, &gauges);
    slapi_atomic_store_64(&header->heartbeat_wall_sec, (uint64_t)slapi_current_utc_time(), __ATOMIC_RELAXED);
    slapi_atomic_store_64(&header->heartbeat_mono_ns, tp_stats_mono_ns(), __ATOMIC_RELEASE);
}

int
tp_stats_init(uint32_t max_workers)
{
    tp_stats_header_t *header = NULL;
    void *mapping = MAP_FAILED;
    struct stat st = {0};
    char *path = NULL;
    int fd = -1;
    int rc;
    size_t len;

    if (!config_get_thread_pool_stats()) {
        slapi_log_err(SLAPI_LOG_INFO, TP_STATS_COMPONENT,
                      "Thread-pool status diagnostics disabled by " CONFIG_THREAD_POOL_STATS_ATTRIBUTE "\n");
        return 0;
    }

    if (max_workers == 0) {
        slapi_log_err(SLAPI_LOG_WARNING, TP_STATS_COMPONENT,
                      "Thread-pool status mmap disabled: worker count is zero\n");
        return -1;
    }

    if (tp_stats_header != NULL) {
        return 0;
    }

    len = TP_STATS_HEADER_SIZE + ((size_t)max_workers * TP_STATS_WORKER_SLOT_SIZE);
    path = tp_stats_make_path();
    if (path == NULL) {
        slapi_log_err(SLAPI_LOG_WARNING, TP_STATS_COMPONENT,
                      "Thread-pool status mmap disabled: could not resolve runtime path\n");
        return -1;
    }

    if (tp_stats_prepare_dir(path) != 0) {
        slapi_ch_free_string(&path);
        return -1;
    }

    /* Best effort: on any failure the leftover falls through to the unlink below */
    tp_stats_archive_crash_file(path);

    if (unlink(path) != 0 && errno != ENOENT) {
        int err = errno;
        struct stat lst = {0};
        const char *kind = "file";

        if (lstat(path, &lst) == 0 && S_ISLNK(lst.st_mode)) {
            kind = "symlink";
        }
        slapi_log_err(SLAPI_LOG_WARNING, TP_STATS_COMPONENT,
                      "Could not remove stale thread-pool status %s %s: %d (%s). "
                      "Possible SELinux denial; thread-pool status diagnostics are disabled\n",
                      kind, path, err, slapd_system_strerror(err));
        slapi_ch_free_string(&path);
        return -1;
    }

    fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0640);
    if (fd < 0) {
        int err = errno;
        slapi_log_err(SLAPI_LOG_WARNING, TP_STATS_COMPONENT,
                      "Could not create thread-pool status file %s: %d (%s)\n",
                      path, err, slapd_system_strerror(err));
        tp_stats_cleanup_open_failure(fd, &path);
        return -1;
    }

    if (fstat(fd, &st) != 0) {
        int err = errno;
        slapi_log_err(SLAPI_LOG_WARNING, TP_STATS_COMPONENT,
                      "Could not inspect thread-pool status file %s: %d (%s)\n",
                      path, err, slapd_system_strerror(err));
        tp_stats_cleanup_open_failure(fd, &path);
        return -1;
    }

    if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1) {
        slapi_log_err(SLAPI_LOG_WARNING, TP_STATS_COMPONENT,
                      "Refusing unsafe thread-pool status file %s (mode=%o uid=%ld nlink=%ld)\n",
                      path, (unsigned int)st.st_mode, (long)st.st_uid, (long)st.st_nlink);
        tp_stats_cleanup_open_failure(fd, &path);
        return -1;
    }

    if (fchmod(fd, 0640) != 0) {
        int err = errno;
        slapi_log_err(SLAPI_LOG_WARNING, TP_STATS_COMPONENT,
                      "Could not set permissions on thread-pool status file %s: %d (%s)\n",
                      path, err, slapd_system_strerror(err));
        tp_stats_cleanup_open_failure(fd, &path);
        return -1;
    }

    /*
     * Reserve backing pages up front: ftruncate alone leaves a sparse file,
     * and a store into an unbacked page takes SIGBUS when the filesystem is
     * full. With the reservation, slot and heartbeat writes can never fault.
     * posix_fallocate returns the error code instead of setting errno.
     */
    rc = posix_fallocate(fd, 0, (off_t)len);
    if (rc == EOPNOTSUPP || rc == EINVAL) {
        /* Filesystem without fallocate support: fall back to a sparse file. */
        if (ftruncate(fd, (off_t)len) != 0) {
            int err = errno;
            slapi_log_err(SLAPI_LOG_WARNING, TP_STATS_COMPONENT,
                          "Could not size thread-pool status file %s: %d (%s)\n",
                          path, err, slapd_system_strerror(err));
            tp_stats_cleanup_open_failure(fd, &path);
            return -1;
        }
    } else if (rc != 0) {
        slapi_log_err(SLAPI_LOG_WARNING, TP_STATS_COMPONENT,
                      "Could not reserve space for thread-pool status file %s: %d (%s)\n",
                      path, rc, slapd_system_strerror(rc));
        tp_stats_cleanup_open_failure(fd, &path);
        return -1;
    }

    mapping = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        int err = errno;
        slapi_log_err(SLAPI_LOG_WARNING, TP_STATS_COMPONENT,
                      "Could not map thread-pool status file %s: %d (%s)\n",
                      path, err, slapd_system_strerror(err));
        tp_stats_cleanup_open_failure(fd, &path);
        return -1;
    }

    memset(mapping, 0, len);
    header = (tp_stats_header_t *)mapping;
    header->ver_major = TP_STATS_VER_MAJOR;
    header->ver_minor = TP_STATS_VER_MINOR;
    header->header_size = TP_STATS_HEADER_SIZE;
    header->worker_slot_size = TP_STATS_WORKER_SLOT_SIZE;
    header->max_workers = max_workers;
    header->server_pid = (uint64_t)getpid();
    header->start_wall_sec = (uint64_t)slapi_current_utc_time();

    tp_stats_header = header;
    tp_stats_fd = fd;
    tp_stats_len = len;
    tp_stats_max_workers = max_workers;
    tp_stats_path = path;

    tp_stats_heartbeat(0, NULL);
    slapi_atomic_store_64(&header->magic, TP_STATS_MAGIC, __ATOMIC_RELEASE);

    slapi_log_err(SLAPI_LOG_INFO, TP_STATS_COMPONENT,
                  "Thread-pool status mmap ready at %s (%zu bytes, %" PRIu32 " workers)\n",
                  tp_stats_path, tp_stats_len, max_workers);
    return 0;
}

/*
 * Register the periodic heartbeat. Must run only after init_op_threads():
 * the callback reads per_thread_snmp_vars through g_get_num_ops_initiated(),
 * and alloc_per_thread_snmp_vars() reallocates that array with no
 * synchronization against readers.
 */
void
tp_stats_start_heartbeat(void)
{
    if (tp_stats_header == NULL || tp_stats_eq_ctx != NULL) {
        return;
    }

    tp_stats_eq_ctx = slapi_eq_repeat_rel(tp_stats_heartbeat, NULL,
                                          slapi_current_rel_time_t(),
                                          TP_STATS_HEARTBEAT_INTERVAL_MS);
    if (tp_stats_eq_ctx == NULL) {
        slapi_log_err(SLAPI_LOG_WARNING, TP_STATS_COMPONENT,
                      "Thread-pool status file %s was created, but heartbeat registration failed\n",
                      tp_stats_path);
    }
}

void
tp_stats_close(void)
{
    tp_stats_header_t *header = tp_stats_header;

    if (header == NULL) {
        return;
    }

    if (tp_stats_eq_ctx != NULL) {
        slapi_eq_cancel_rel(tp_stats_eq_ctx);
        tp_stats_eq_ctx = NULL;
    }

    tp_store_32(&header->shutdown_clean, 1, __ATOMIC_RELEASE);

    if (tp_stats_path != NULL) {
        if (unlink(tp_stats_path) != 0 && errno != ENOENT) {
            int err = errno;
            slapi_log_err(SLAPI_LOG_WARNING, TP_STATS_COMPONENT,
                          "Could not remove thread-pool status file %s: %d (%s)\n",
                          tp_stats_path, err, slapd_system_strerror(err));
        }
        slapi_ch_free_string(&tp_stats_path);
    }

    if (tp_stats_fd >= 0) {
        close(tp_stats_fd);
        tp_stats_fd = -1;
    }

    /*
     * Do not munmap: worker threads are unjoinable and a late slot write into
     * an unmapped region would crash shutdown. The mapping dies with ns-slapd.
     */
    tp_stats_header = NULL;
    tp_stats_len = 0;
    tp_stats_max_workers = 0;
}

void
tp_stats_worker_idle(uint32_t worker_idx)
{
    tp_worker_slot_t *slot = tp_stats_get_slot(worker_idx);

    if (slot == NULL) {
        return;
    }

    slapi_atomic_store_64(&slot->conn_id, 0, __ATOMIC_RELAXED);
    slapi_atomic_store_64(&slot->op_id, 0, __ATOMIC_RELAXED);
    tp_store_32(&slot->op_tag, 0, __ATOMIC_RELAXED);
    slapi_atomic_store_64(&slot->start_ns, 0, __ATOMIC_RELAXED);
    tp_store_32(&slot->state, TP_WORKER_STATE_IDLE, __ATOMIC_RELEASE);
}

void
tp_stats_worker_busy(uint32_t worker_idx)
{
    tp_worker_slot_t *slot = tp_stats_get_slot(worker_idx);

    if (slot == NULL) {
        return;
    }

    tp_store_32(&slot->state, TP_WORKER_STATE_BUSY, __ATOMIC_RELEASE);
}

void
tp_stats_worker_operation_start(uint32_t worker_idx, uint64_t conn_id, uint64_t op_id, uint32_t op_tag)
{
    tp_worker_slot_t *slot = tp_stats_get_slot(worker_idx);

    if (slot == NULL) {
        return;
    }

    slapi_atomic_store_64(&slot->conn_id, conn_id, __ATOMIC_RELAXED);
    slapi_atomic_store_64(&slot->op_id, op_id, __ATOMIC_RELAXED);
    tp_store_32(&slot->op_tag, op_tag, __ATOMIC_RELAXED);
    slapi_atomic_store_64(&slot->start_ns, tp_stats_mono_ns(), __ATOMIC_RELAXED);
    tp_store_32(&slot->state, TP_WORKER_STATE_BUSY, __ATOMIC_RELEASE);
}

void
tp_stats_worker_operation_done(uint32_t worker_idx)
{
    tp_worker_slot_t *slot = tp_stats_get_slot(worker_idx);

    if (slot == NULL) {
        return;
    }

    /*
     * start_ns is the in-flight sentinel (op_id 0 is a valid first op on a
     * connection): clear it first so a reader that still sees it set finds
     * the op fields intact.
     */
    slapi_atomic_store_64(&slot->start_ns, 0, __ATOMIC_RELAXED);
    tp_store_32(&slot->op_tag, 0, __ATOMIC_RELAXED);
    slapi_atomic_store_64(&slot->op_id, 0, __ATOMIC_RELAXED);
}

void
tp_stats_worker_exited(uint32_t worker_idx)
{
    tp_worker_slot_t *slot = tp_stats_get_slot(worker_idx);

    if (slot == NULL) {
        return;
    }

    tp_store_32(&slot->state, TP_WORKER_STATE_EXITED, __ATOMIC_RELEASE);
}

void
tp_stats_as_entry(Slapi_Entry *e)
{
    tp_stats_header_t *header = tp_stats_header;
    struct berval val;
    struct berval *vals[2];
    uint64_t now_ns;

    vals[0] = &val;
    vals[1] = NULL;
    attrlist_delete(&e->e_attrs, TP_STATS_ATTR_THREADPOOL_WORKER);

    if (header == NULL) {
        return;
    }

    now_ns = tp_stats_mono_ns();
    for (uint32_t i = 0; i < header->max_workers; i++) {
        char buf[256];
        char op_buf[32];
        uint32_t state;
        uint32_t op_tag;
        uint64_t start_ns;
        uint64_t duration_ns = 0;
        const char *op_name;

        tp_worker_slot_t *slot = tp_stats_slot_at(header, i);

        state = tp_load_32(&slot->state, __ATOMIC_ACQUIRE);
        if (state == TP_WORKER_STATE_UNUSED) {
            continue;
        }

        op_tag = tp_load_32(&slot->op_tag, __ATOMIC_RELAXED);
        start_ns = slapi_atomic_load_64(&slot->start_ns, __ATOMIC_RELAXED);
        /* start_ns is the in-flight sentinel; op_id 0 is a valid first op */
        if (start_ns != 0 && now_ns >= start_ns) {
            duration_ns = now_ns - start_ns;
        }

        op_name = tp_stats_op_name(op_tag, op_buf, sizeof(op_buf));
        snprintf(buf, sizeof(buf),
                 "worker=%" PRIu32 " state=%s op=%s duration_ns=%" PRIu64,
                 i + 1, tp_stats_state_name(state), op_name, duration_ns);
        val.bv_val = buf;
        val.bv_len = strlen(buf);
        attrlist_merge(&e->e_attrs, TP_STATS_ATTR_THREADPOOL_WORKER, vals);
    }
}

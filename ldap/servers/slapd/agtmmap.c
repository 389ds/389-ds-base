/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/********************************************************************
 *
 *      agtmmap.c: Memory Map interface for SNMP sub-agent for
 *            Directory Server stats (for UNIX environment).
 *
 *      Revision History:
 *      07/22/97        Created                 Steve Ross
 *
 *
 **********************************************************************/


#include "agtmmap.h"
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

agt_mmap_context_t mmap_tbl[2] = {{AGT_MAP_UNINIT, -1, (caddr_t)-1},
                                  {AGT_MAP_UNINIT, -1, (caddr_t)-1}};

#define CHECK_MAP_FAILURE(addr) ((addr)==NULL || (addr) == (caddr_t) -1)


/****************************************************************************
 *
 *  agt_set_fmode () - try to increase file mode if some flags are missing.
 *
 *
 * Inputs:
 *    fd -> The file descriptor.
 *
 *    mode -> the wanted mode
 *
 * Outputs:           None
 * Return Values:     None
 *
 ****************************************************************************/
static void
agt_set_fmode(int fd, mode_t mode)
{
    /* ns-slapd umask is 0022 which is usually fine.
     * but ldap-agen needs S_IWGRP permission on snmp semaphore and mmap file
     * ( when SELinux is enforced process with uid=0 does not bypass the file permission
     * (unless the unfamous dac_override capability is set)
     * Changing umask could lead to race conditions so it is better to check the
     * file permission and change them if needed and if the process own the file.
     */
    struct stat fileinfo = {0};
    if (fstat(fd, &fileinfo) == 0 && fileinfo.st_uid == getuid() &&
        (fileinfo.st_mode & mode) != mode) {
        (void) fchmod(fd, fileinfo.st_mode | mode);
    }
}

/****************************************************************************
 *
 *  agt_sem_open () - Like sem_open but ignores umask
 *
 *
 * Inputs:            see sem_open man page.
 * Outputs:           see sem_open man page.
 * Return Values:     see sem_open man page.
 *
 ****************************************************************************/
sem_t *
agt_sem_open(const char *name, int oflag, mode_t mode, unsigned int value)
{
    sem_t *sem = sem_open(name, oflag, mode, value);
    char *semname = NULL;

    if (sem != NULL) {
        if (asprintf(&semname, "/dev/shm/sem.%s", name+1) > 0) {
            int fd = open(semname, O_RDONLY);
            if (fd >= 0) {
                agt_set_fmode(fd, mode);
                (void) close(fd);
            }
            free(semname);
            semname = NULL;
        }
    }
    return sem;
}

/****************************************************************************
 *
 *  agt_mopen_stats () - open and Memory Map the stats file.  agt_mclose_stats()
 *              must be called prior to invoking agt_mopen_stats() again.
 * Inputs:
 *     statsfile ->  Name of stats file including full path or NULL.
 *                      If NULL, default (slapd.stats) is assumed.
 *    mode      ->  Must be one of O_RDONLY / O_RDWR.
 *              O_RDWR creates the file if it does not exist.
 * Outputs:
 *    hdl      ->  Opaque handle to the mapped file. Should be
 *              passed to a subsequent agt_mupdate_stats() or
 *              agt_mread_stats() or agt_mclose_stats() call.
 * Return Values:
 *              Returns 0 on successfully doing the memmap or error codes
 *               as defined in <errno.h>, otherwise.
 *
 ****************************************************************************/
int
agt_mopen_stats(char *statsfile, int mode, int *hdl)
{
    caddr_t fp;
    char *path;
    char *buf;
    int rc = 0;
    int fd;
    int err;
    size_t sz;
    struct stat fileinfo;
    mode_t rw_mode = S_IWUSR | S_IRUSR | S_IRGRP | S_IWGRP | S_IROTH;

    switch (mode) {
    case O_RDONLY:
        if (mmap_tbl[0].maptype != AGT_MAP_UNINIT) {
            *hdl = 0;
            rc = EEXIST; /* We already mapped it once */
            goto bail;
        }
        break;

    case O_RDWR:
        if (mmap_tbl[1].maptype != AGT_MAP_UNINIT) {
            *hdl = 1;
            rc = EEXIST; /* We already mapped it once */
            goto bail;
        }
        break;

    default:
        rc = EINVAL; /* Invalid (mode) parameter */
        goto bail;

    } /* end switch */


    if (statsfile != NULL)
        path = statsfile;
    else
        path = AGT_STATS_FILE;

    switch (mode) {
    case O_RDONLY:
        if ((fd = open(path, O_RDONLY)) < 0) {
            err = errno;
#if (0)
            fprintf(stderr, "returning errno =%d from %s(line: %d)\n", err, __FILE__, __LINE__);
#endif
            rc = err;
            goto bail;
        }

        fp = mmap(NULL, sizeof(struct agt_stats_t), PROT_READ, MAP_PRIVATE, fd, 0);

        if (fp == (caddr_t)-1) {
            err = errno;
            close(fd);
#if (0)
            fprintf(stderr, "returning errno =%d from %s(line: %d)\n", err, __FILE__, __LINE__);
#endif
            rc = err;
            goto bail;
        }

        mmap_tbl[0].maptype = AGT_MAP_READ;
        mmap_tbl[0].fd = fd;
        mmap_tbl[0].fp = fp;
        *hdl = 0;
#if (0)
        fprintf(stderr, "%s@%d> opened fp = %d\n", __FILE__, __LINE__, fp);
#endif
        rc = 0;
        break;

    case O_RDWR:
        fd = open(path, O_RDWR | O_CREAT, rw_mode);
        if (fd < 0) {
            err = errno;
#if (0)
            fprintf(stderr, "returning errno =%d from %s(line: %d)\n", err, __FILE__, __LINE__);
#endif
            rc = err;
            goto bail;
        }
        agt_set_fmode(fd, rw_mode);

        if (fstat(fd, &fileinfo) != 0) {
            close(fd);
            rc = errno;
            goto bail;
        }

        sz = sizeof(struct agt_stats_t);
        /* st_size is an off_t, which is signed. sz, size_t is unsigned. */
        if (fileinfo.st_size < (off_t)sz) {
            /* Without this we will get segv when we try to read/write later */
            buf = calloc(1, sz);
            if (!buf) {
                rc = errno;
                close(fd);
                goto bail;
            }
            if (write(fd, buf, sz) < 0) {
                err = errno;
#if (0)
                fprintf(stderr, "write failed errno=%d from %s(line: %d)\n", err, __FILE__, __LINE__);
#endif
                rc = err;
                free(buf);
                close(fd);
                goto bail;
            }
            free(buf);
        }

        fp = mmap(NULL, sz, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, 0);

        if (fp == (caddr_t)-1) {
            err = errno;
            close(fd);
#if (0)
            fprintf(stderr, "returning errno =%d from %s(line: %d)\n", err, __FILE__, __LINE__);
#endif
            rc = err;
            goto bail;
        }

        mmap_tbl[1].maptype = AGT_MAP_RDWR;
        mmap_tbl[1].fd = fd;
        mmap_tbl[1].fp = fp;
        *hdl = 1;

        rc = 0;
        break;
    } /* end switch */

bail:
    return rc;
} /* agt_mopen_stats () */


/****************************************************************************
 *
 *  agt_mclose_stats () - Close the Memory Map'ed the stats file.
 *
 *
 * Inputs:
 *    hdl      ->  Opaque handle to the mapped file. Should be have been
 *              returned by an earlier call to agt_mopen_stats().
 *
 * Outputs:          <NONE>
 *
 * Return Values:
 *              Returns 0 on normal completion or error codes
 *               as defined in <errno.h>, otherwise.
 *
 ****************************************************************************/
int
agt_mclose_stats(int hdl)
{
    if ((hdl > 1) || (hdl < 0)) {
        return (EINVAL); /* Inavlid handle */
    }

    if (mmap_tbl[hdl].maptype == AGT_MAP_UNINIT)
        return (0);

    if (!CHECK_MAP_FAILURE(mmap_tbl[hdl].fp)) {
        munmap(mmap_tbl[hdl].fp, sizeof(struct agt_stats_t));
        mmap_tbl[hdl].fp = (caddr_t)-1;
        close(mmap_tbl[hdl].fd);
        mmap_tbl[hdl].fd = -1;
        mmap_tbl[hdl].maptype = AGT_MAP_UNINIT;
        return (0);
    }

    return EINVAL;
} /* agt_mclose_stats () */


int
agt_mread_stats(int hdl, struct hdr_stats_t *pHdrInfo, struct ops_stats_t *pDsOpsTbl, struct entries_stats_t *pDsEntTbl)
{
    struct agt_stats_t *pfile_stats;

    if ((hdl > 1) || (hdl < 0)) {
        return (EINVAL);
    }

    if ((mmap_tbl[hdl].maptype != AGT_MAP_READ) && (mmap_tbl[hdl].maptype != AGT_MAP_RDWR)) {
        return (EINVAL); /* Inavlid handle */
    }

    if (CHECK_MAP_FAILURE(mmap_tbl[hdl].fp)) {
        return (EFAULT); /* Something got corrupted */
    }

    pfile_stats = (struct agt_stats_t *)(mmap_tbl[hdl].fp);

    if (pHdrInfo != NULL) {
        /* Header */
        pHdrInfo->restarted = pfile_stats->hdr_stats.restarted;
        pHdrInfo->startTime = pfile_stats->hdr_stats.startTime;
        pHdrInfo->updateTime = pfile_stats->hdr_stats.updateTime;
        strncpy(pHdrInfo->dsVersion, pfile_stats->hdr_stats.dsVersion,
                SNMP_FIELD_LENGTH - 1);
        pHdrInfo->dsVersion[SNMP_FIELD_LENGTH - 1] = (char)0;
        strncpy(pHdrInfo->dsName, pfile_stats->hdr_stats.dsName,
                SNMP_FIELD_LENGTH - 1);
        pHdrInfo->dsName[SNMP_FIELD_LENGTH - 1] = (char)0;
        strncpy(pHdrInfo->dsDescription, pfile_stats->hdr_stats.dsDescription,
                SNMP_FIELD_LENGTH - 1);
        pHdrInfo->dsDescription[SNMP_FIELD_LENGTH - 1] = (char)0;
        strncpy(pHdrInfo->dsOrganization, pfile_stats->hdr_stats.dsOrganization,
                SNMP_FIELD_LENGTH - 1);
        pHdrInfo->dsOrganization[SNMP_FIELD_LENGTH - 1] = (char)0;
        strncpy(pHdrInfo->dsLocation, pfile_stats->hdr_stats.dsLocation,
                SNMP_FIELD_LENGTH - 1);
        pHdrInfo->dsLocation[SNMP_FIELD_LENGTH - 1] = (char)0;
        strncpy(pHdrInfo->dsContact, pfile_stats->hdr_stats.dsContact,
                SNMP_FIELD_LENGTH - 1);
        pHdrInfo->dsContact[SNMP_FIELD_LENGTH - 1] = (char)0;
    }

    if (pDsOpsTbl != NULL) {
        /* Ops Table */
        pDsOpsTbl->dsAnonymousBinds = pfile_stats->ops_stats.dsAnonymousBinds;
        pDsOpsTbl->dsUnAuthBinds = pfile_stats->ops_stats.dsUnAuthBinds;
        pDsOpsTbl->dsSimpleAuthBinds = pfile_stats->ops_stats.dsSimpleAuthBinds;
        pDsOpsTbl->dsStrongAuthBinds = pfile_stats->ops_stats.dsStrongAuthBinds;
        pDsOpsTbl->dsBindSecurityErrors = pfile_stats->ops_stats.dsBindSecurityErrors;
        pDsOpsTbl->dsInOps = pfile_stats->ops_stats.dsInOps;
        pDsOpsTbl->dsReadOps = pfile_stats->ops_stats.dsReadOps;
        pDsOpsTbl->dsCompareOps = pfile_stats->ops_stats.dsCompareOps;
        pDsOpsTbl->dsAddEntryOps = pfile_stats->ops_stats.dsAddEntryOps;
        pDsOpsTbl->dsRemoveEntryOps = pfile_stats->ops_stats.dsRemoveEntryOps;
        pDsOpsTbl->dsModifyEntryOps = pfile_stats->ops_stats.dsModifyEntryOps;
        pDsOpsTbl->dsModifyRDNOps = pfile_stats->ops_stats.dsModifyRDNOps;
        pDsOpsTbl->dsListOps = pfile_stats->ops_stats.dsListOps;
        pDsOpsTbl->dsSearchOps = pfile_stats->ops_stats.dsSearchOps;
        pDsOpsTbl->dsOneLevelSearchOps = pfile_stats->ops_stats.dsOneLevelSearchOps;
        pDsOpsTbl->dsWholeSubtreeSearchOps = pfile_stats->ops_stats.dsWholeSubtreeSearchOps;
        pDsOpsTbl->dsReferrals = pfile_stats->ops_stats.dsReferrals;
        pDsOpsTbl->dsChainings = pfile_stats->ops_stats.dsChainings;
        pDsOpsTbl->dsSecurityErrors = pfile_stats->ops_stats.dsSecurityErrors;
        pDsOpsTbl->dsErrors = pfile_stats->ops_stats.dsErrors;
        pDsOpsTbl->dsConnections = pfile_stats->ops_stats.dsConnections;
        pDsOpsTbl->dsConnectionsInMaxThreads = pfile_stats->ops_stats.dsConnectionsInMaxThreads;
        pDsOpsTbl->dsMaxThreadsHits = pfile_stats->ops_stats.dsMaxThreadsHits;
    }

    if (pDsEntTbl != NULL) {
        /* Entries Table */
        pDsEntTbl->dsSupplierEntries = pfile_stats->entries_stats.dsSupplierEntries;
        pDsEntTbl->dsCopyEntries = pfile_stats->entries_stats.dsCopyEntries;
        pDsEntTbl->dsCacheEntries = pfile_stats->entries_stats.dsCacheEntries;
        pDsEntTbl->dsCacheHits = pfile_stats->entries_stats.dsCacheHits;
        pDsEntTbl->dsConsumerHits = pfile_stats->entries_stats.dsConsumerHits;
    }

    return (0);
}

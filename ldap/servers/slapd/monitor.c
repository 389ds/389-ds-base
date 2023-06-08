/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2019 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * Copyright (c) 1995 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <sys/socket.h>
#include "slap.h"
#include "fe.h"
#include "wthreads.h"

int32_t
monitor_info(Slapi_PBlock *pb __attribute__((unused)),
             Slapi_Entry *e,
             Slapi_Entry *entryAfter __attribute__((unused)),
             int *returncode,
             char *returntext __attribute__((unused)),
             void *arg __attribute__((unused)))
{
    char buf[BUFSIZ];
    struct berval val;
    struct berval *vals[2];
    time_t curtime = slapi_current_utc_time();
    struct tm utm;
    Slapi_Backend *be;
    char *cookie;
    op_thread_stats_t wthreads;

    vals[0] = &val;
    vals[1] = NULL;

    /* "version" value */
    val.bv_val = slapd_get_version_value();
    val.bv_len = strlen(val.bv_val);
    attrlist_replace(&e->e_attrs, "version", vals);
    slapi_ch_free((void **)&val.bv_val);

    val.bv_len = snprintf(buf, sizeof(buf), "%" PRIu64, g_get_active_threadcnt());
    val.bv_val = buf;
    attrlist_replace(&e->e_attrs, "threads", vals);

    connection_table_as_entry(the_connection_table, e);

    val.bv_len = snprintf(buf, sizeof(buf), "%" PRIu64, g_get_num_ops_initiated());
    val.bv_val = buf;
    attrlist_replace(&e->e_attrs, "opsinitiated", vals);

    val.bv_len = snprintf(buf, sizeof(buf), "%" PRIu64, g_get_num_ops_completed());
    val.bv_val = buf;
    attrlist_replace(&e->e_attrs, "opscompleted", vals);

    val.bv_len = snprintf(buf, sizeof(buf), "%" PRIu64, g_get_num_entries_sent());
    val.bv_val = buf;
    attrlist_replace(&e->e_attrs, "entriessent", vals);

    val.bv_len = snprintf(buf, sizeof(buf), "%" PRIu64, g_get_num_bytes_sent());
    val.bv_val = buf;
    attrlist_replace(&e->e_attrs, "bytessent", vals);

    gmtime_r(&curtime, &utm);
    strftime(buf, sizeof(buf), "%Y%m%d%H%M%SZ", &utm);
    val.bv_val = buf;
    val.bv_len = strlen(buf);
    attrlist_replace(&e->e_attrs, "currenttime", vals);

    gmtime_r(&starttime, &utm);
    strftime(buf, sizeof(buf), "%Y%m%d%H%M%SZ", &utm);
    val.bv_val = buf;
    val.bv_len = strlen(buf);
    attrlist_replace(&e->e_attrs, "starttime", vals);

    val.bv_len = snprintf(buf, sizeof(buf), "%d", be_nbackends_public());
    val.bv_val = buf;
    attrlist_replace(&e->e_attrs, "nbackends", vals);

    if (g_pc.getstats_cb) {
        g_pc.getstats_cb(&wthreads);

        val.bv_len = snprintf(buf, sizeof(buf), "%d", wthreads.waitingthreads);
        val.bv_val = buf;
        attrlist_replace(&e->e_attrs, "waitingthreads", vals);

        val.bv_len = snprintf(buf, sizeof(buf), "%d", wthreads.busythreads);
        val.bv_val = buf;
        attrlist_replace(&e->e_attrs, "busythreads", vals);

        val.bv_len = snprintf(buf, sizeof(buf), "%d", wthreads.maxbusythreads);
        val.bv_val = buf;
        attrlist_replace(&e->e_attrs, "maxbusythreads", vals);

        val.bv_len = snprintf(buf, sizeof(buf), "%d", wthreads.waitingjobs);
        val.bv_val = buf;
        attrlist_replace(&e->e_attrs, "waitingjobs", vals);

        val.bv_len = snprintf(buf, sizeof(buf), "%d", wthreads.maxwaitingjobs);
        val.bv_val = buf;
        attrlist_replace(&e->e_attrs, "maxwaitingjobs", vals);
    }

    /*
     * Loop through the backends, and stuff the monitor dn's
     * into the entry we're sending back
     */
    attrlist_delete(&e->e_attrs, "backendmonitordn");
    cookie = NULL;
    be = slapi_get_first_backend(&cookie);
    while (be) {
        if (!be->be_private) {
            Slapi_DN dn;
            slapi_sdn_init(&dn);
            be_getmonitordn(be, &dn);
            val.bv_val = (char *)slapi_sdn_get_dn(&dn);
            val.bv_len = strlen(val.bv_val);
            attrlist_merge(&e->e_attrs, "backendmonitordn", vals);
            slapi_sdn_done(&dn);
        }
        be = slapi_get_next_backend(cookie);
    }

    slapi_ch_free((void **)&cookie);

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}


int32_t
monitor_disk_info (Slapi_PBlock *pb __attribute__((unused)),
                   Slapi_Entry *e,
                   Slapi_Entry *entryAfter __attribute__((unused)),
                   int *returncode,
                   char *returntext __attribute__((unused)),
                   void *arg __attribute__((unused)))
{
    int32_t rc = LDAP_SUCCESS;
    char **dirs = NULL;
    struct berval val;
    struct berval *vals[2];
    uint64_t total_space;
    uint64_t avail_space;
    uint64_t used_space;

    vals[0] = &val;
    vals[1] = NULL;

    disk_mon_get_dirs(&dirs);

    for (size_t i = 0; dirs && dirs[i]; i++) {
    	char buf[BUFSIZ] = {0};
        rc = disk_get_info(dirs[i], &total_space, &avail_space, &used_space);
        if (rc == 0 && total_space > 0 && used_space > 0) {
            val.bv_len = snprintf(buf, sizeof(buf),
                    "partition=\"%s\" size=\"%" PRIu64 "\" used=\"%" PRIu64 "\" available=\"%" PRIu64 "\" use%%=\"%" PRIu64 "\"",
                    dirs[i], total_space, used_space, avail_space, used_space * 100 / total_space);
            val.bv_val = buf;
            attrlist_merge(&e->e_attrs, "dsDisk", vals);
        }
    }
    slapi_ch_array_free(dirs);

    *returncode = rc;
    return SLAPI_DSE_CALLBACK_OK;
}

/*
 * Return a malloc'd version value.
 * Used for the monitor entry's 'version' attribute.
 * Used for the root DSE's 'vendorVersion' attribute.
 */
char *
slapd_get_version_value(void)
{
    char *versionstring, *buildnum, *vs;

    versionstring = config_get_versionstring();
    buildnum = config_get_buildnum();
    vs = slapi_ch_smprintf("%s B%s", versionstring, buildnum);
    slapi_ch_free_string(&buildnum);
    slapi_ch_free_string(&versionstring);

    return vs;
}

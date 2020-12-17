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


#include "retrocl.h"

typedef struct _trim_status
{
    time_t ts_c_max_age;     /* Constraint  - max age of a changelog entry */
    time_t ts_s_last_trim;   /* Status - last time we trimmed */
    int ts_s_initialized;    /* Status - non-zero if initialized */
    int ts_s_trimming;       /* non-zero if trimming in progress */
    PRLock *ts_s_trim_mutex; /* protects ts_s_trimming */
} trim_status;
static trim_status ts = {0L, 0L, 0, 0, NULL};
static int trim_interval = DEFAULT_CHANGELOGDB_TRIM_INTERVAL; /* in second */

/*
 * All standard changeLogEntry attributes (initialized in get_cleattrs)
 */
static const char *cleattrs[10] = {NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL};

static int retrocl_trimming = 0;
static Slapi_Eq_Context retrocl_trim_ctx = NULL;

/*
 * Function: get_cleattrs
 *
 * Returns: an array of pointers to attribute names.
 *
 * Arguments: None.
 *
 * Description: Initializes, if necessary, and returns an array of char *s
 *              with attribute names used for retrieving changeLogEntry
 *              entries from the directory.
 */
static const char **
get_cleattrs(void)
{
    if (cleattrs[0] == NULL) {
        cleattrs[0] = retrocl_objectclass;
        cleattrs[1] = retrocl_changenumber;
        cleattrs[2] = retrocl_targetdn;
        cleattrs[3] = retrocl_changetype;
        cleattrs[4] = retrocl_newrdn;
        cleattrs[5] = retrocl_deleteoldrdn;
        cleattrs[6] = retrocl_changes;
        cleattrs[7] = retrocl_newsuperior;
        cleattrs[8] = retrocl_changetime;
        cleattrs[9] = NULL;
    }
    return cleattrs;
}

/*
 * Function: delete_changerecord
 *
 * Returns: LDAP_ error code
 *
 * Arguments: the number of the change to delete
 *
 * Description:
 *
 */

static int
delete_changerecord(changeNumber cnum)
{
    Slapi_PBlock *pb;
    char *dnbuf;
    int delrc;

    dnbuf = slapi_ch_smprintf("%s=%ld, %s", retrocl_changenumber, cnum,
                              RETROCL_CHANGELOG_DN);
    pb = slapi_pblock_new();
    slapi_delete_internal_set_pb(pb, dnbuf, NULL /*controls*/, NULL /* uniqueid */,
                                 g_plg_identity[PLUGIN_RETROCL], 0 /* actions */);
    slapi_delete_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &delrc);
    slapi_pblock_destroy(pb);

    if (delrc != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME,
                      "delete_changerecord: could not delete change record %lu (rc: %d)\n",
                      cnum, delrc);
    } else {
        slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
                      "delete_changerecord: deleted changelog entry \"%s\"\n", dnbuf);
    }
    slapi_ch_free((void **)&dnbuf);
    return delrc;
}

/*
 * Function: handle_getchangetime_result
 * Arguments: op - pointer to Operation struct for this operation
 *            err - error code returned from search
 * Returns: nothing
 * Description: result handler for get_changetime().  Sets the crt_err
 *              field of the cnum_result_t struct to the error returned
 *              from the backend.
 */
static void
handle_getchangetime_result(int err, void *callback_data)
{
    cnum_result_t *crt = callback_data;

    if (crt == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME,
                      "handle_getchangetime_result: callback_data NULL\n");
    } else {
        crt->crt_err = err;
    }
}

/*
 * Function: handle_getchangetime_search
 * Arguments: op - pointer to Operation struct for this operation
 *            e - entry returned by backend
 * Returns: 0 in all cases
 * Description: Search result operation handler for get_changetime().
 *              Sets fields in the cnum_result_t struct pointed to by
 *              op->o_handler_data.
 */
static int
handle_getchangetime_search(Slapi_Entry *e, void *callback_data)
{
    cnum_result_t *crt = callback_data;
    int rc;
    Slapi_Attr *attr;

    if (crt == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME,
                      "handle_getchangetime_search: op->o_handler_data NULL\n");
    } else if (crt->crt_nentries > 0) {
        /* only return the first entry, I guess */
        slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME,
                      "handle_getchangetime_search: multiple entries returned\n");
    } else {
        crt->crt_nentries++;
        crt->crt_time = 0;

        if (NULL != e) {
            Slapi_Value *sval = NULL;
            const struct berval *val = NULL;
            rc = slapi_entry_attr_find(e, retrocl_changetime, &attr);
            /* Bug 624442: Logic checking for lack of timestamp was
               reversed. */
            if (0 != rc || slapi_attr_first_value(attr, &sval) == -1 ||
                (val = slapi_value_get_berval(sval)) == NULL ||
                NULL == val->bv_val) {
                crt->crt_time = 0;
            } else {
                crt->crt_time = parse_localTime(val->bv_val);
            }
        }
    }

    return 0;
}


/*
 * Function: get_changetime
 * Arguments: cnum - number of change record to retrieve
 * Returns: Taking the retrocl_changetime of the 'cnum' entry,
 * it converts it into time_t (parse_localTime) and returns this time value.
 * It returns 0 in the following cases:
 *  - changerecord entry has not retrocl_changetime
 *  - attr_changetime attribute has no value
 *  - attr_changetime attribute value is empty
 *
 * Description: Retrieve retrocl_changetime ("changetime") from a changerecord whose number is "cnum".
 */
static time_t
get_changetime(changeNumber cnum, int *err)
{
    cnum_result_t crt, *crtp = &crt;
    char fstr[16 + CNUMSTR_LEN + 2];
    Slapi_PBlock *pb;

    if (cnum == 0UL) {
        if (err != NULL) {
            *err = LDAP_PARAM_ERROR;
        }
        return 0;
    }
    crtp->crt_nentries = crtp->crt_err = 0;
    crtp->crt_time = 0;
    PR_snprintf(fstr, sizeof(fstr), "%s=%ld", retrocl_changenumber, cnum);

    pb = slapi_pblock_new();
    slapi_search_internal_set_pb(pb, RETROCL_CHANGELOG_DN,
                                 LDAP_SCOPE_SUBTREE, fstr,
                                 (char **)get_cleattrs(), /* cast const */
                                 0 /* attrsonly */,
                                 NULL /* controls */, NULL /* uniqueid */,
                                 g_plg_identity[PLUGIN_RETROCL],
                                 0 /* actions */);

    slapi_search_internal_callback_pb(pb, crtp,
                                      handle_getchangetime_result,
                                      handle_getchangetime_search, NULL);
    if (err != NULL) {
        *err = crtp->crt_err;
    }

    slapi_pblock_destroy(pb);

    return (crtp->crt_time);
}

/*
 * Function: trim_changelog
 *
 * Arguments: none
 *
 * Returns: 0 on success, -1 on failure
 *
 * Description: Trims the changelog, according to the constraints
 * described by the ts structure.
 */
static int
trim_changelog(void)
{
    int rc = 0, ldrc, done;
    time_t now;
    changeNumber first_in_log = 0, last_in_log = 0;
    int num_deleted = 0;
    int me, lt;


    now = slapi_current_rel_time_t();

    PR_Lock(ts.ts_s_trim_mutex);
    me = ts.ts_c_max_age;
    lt = ts.ts_s_last_trim;
    PR_Unlock(ts.ts_s_trim_mutex);

    if (now - lt >= trim_interval) {

        /*
     * Trim the changelog.  Read sequentially through all the
     * entries, deleting any which do not meet the criteria
     * described in the ts structure.
     */
        done = 0;

        while (!done && retrocl_trimming == 1) {
            int did_delete;

            did_delete = 0;
            first_in_log = retrocl_get_first_changenumber();
            if (0UL == first_in_log) {
                slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
                              "trim_changelog: no changelog records "
                              "to trim\n");
                /* Bail out - we can't do any useful work */
                break;
            }

            last_in_log = retrocl_get_last_changenumber();
            if (last_in_log == first_in_log) {
                /* Always leave at least one entry in the change log */
                break;
            }
            if (me > 0L) {
                time_t change_time = get_changetime(first_in_log, &ldrc);
                if (change_time) {
                    if ((change_time + me) < now) {
                        retrocl_set_first_changenumber(first_in_log + 1);
                        ldrc = delete_changerecord(first_in_log);
                        num_deleted++;
                        did_delete = 1;
                    }
                } else {
                    /* What to do if there's no timestamp? Just delete it. */
                    retrocl_set_first_changenumber(first_in_log + 1);
                    ldrc = delete_changerecord(first_in_log);
                    num_deleted++;
                    did_delete = 1;
                }
            }
            if (!did_delete) {
                done = 1;
            }
        }
    } else {
        slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME, "Not yet time to trim: %ld < (%d+%d)\n",
                      now, lt, trim_interval);
    }
    PR_Lock(ts.ts_s_trim_mutex);
    ts.ts_s_trimming = 0;
    ts.ts_s_last_trim = now;
    PR_Unlock(ts.ts_s_trim_mutex);
    if (num_deleted > 0) {
        slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
                      "trim_changelog: removed %d change records\n",
                      num_deleted);
    }
    return rc;
}

static int retrocl_active_threads;

/*
 * Function: changelog_trim_thread_fn
 *
 * Returns: nothing
 *
 * Arguments: none
 *
 * Description: the thread creation callback.  retrocl_active_threads is
 * provided for debugging purposes.
 *
 */

static void
changelog_trim_thread_fn(void *arg __attribute__((unused)))
{
    PR_AtomicIncrement(&retrocl_active_threads);
    trim_changelog();
    PR_AtomicDecrement(&retrocl_active_threads);
}


/*
 * Function: retrocl_housekeeping
 * Arguments: cur_time - the current time
 * Returns: nothing
 * Description: Determines if it is time to trim the changelog database,
 *              and if so, determines if the changelog database needs to
 *              be trimmed.  If so, a thread is started which will trim
 *              the database.
 */

void
retrocl_housekeeping(time_t cur_time, void *noarg __attribute__((unused)))
{
    int ldrc;

    if (retrocl_be_changelog == NULL) {
        slapi_log_err(SLAPI_LOG_TRACE, RETROCL_PLUGIN_NAME, "retrocl_housekeeping - not housekeeping if no cl be\n");
        return;
    }

    if (!ts.ts_s_initialized) {
        slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME, "retrocl_housekeeping - called before "
                                                          "trimming constraints set\n");
        return;
    }

    PR_Lock(ts.ts_s_trim_mutex);
    if (!ts.ts_s_trimming) {
        int must_trim = 0;
        /* See if we need to trim */
        /* Has enough time elapsed since our last check? */
        if (cur_time - ts.ts_s_last_trim >= (ts.ts_c_max_age)) {
            /* Is the first entry too old? */
            time_t first_time;
            /*
         * good we could avoid going to the database to retrieve
         * this time information if we cached the last value we'd read.
         * But a client might have deleted it over protocol.
         */
            first_time = retrocl_getchangetime(SLAPI_SEQ_FIRST, &ldrc);
            slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
                          "cltrim: ldrc=%d, first_time=%ld, cur_time=%ld\n",
                          ldrc, first_time, cur_time);
            if (LDAP_SUCCESS == ldrc && first_time > (time_t)0L &&
                first_time + ts.ts_c_max_age < cur_time) {
                must_trim = 1;
            }
        }
        if (must_trim) {
            slapi_log_err(SLAPI_LOG_TRACE, RETROCL_PLUGIN_NAME, "retrocl_housekeeping - changelog about to create thread\n");
            /* Start a thread to trim the changelog */
            ts.ts_s_trimming = 1;
            if (PR_CreateThread(PR_USER_THREAD,
                                changelog_trim_thread_fn, NULL,
                                PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD,
                                RETROCL_DLL_DEFAULT_THREAD_STACKSIZE) == NULL) {
                slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME, "retrocl_housekeeping - "
                                                                  "Unable to create changelog trimming thread\n");
            }
        } else {
            slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
                          "retrocl_housekeeping - changelog does not need to be trimmed\n");
        }
    }
    PR_Unlock(ts.ts_s_trim_mutex);
}

/*
 * Function: retrocl_init_trimming
 *
 * Returns: none, exits on fatal error
 *
 * Arguments: none
 *
 * Description: called during startup
 *
 */

void
retrocl_init_trimming(void)
{
    const char *cl_maxage;
    time_t ageval = 0; /* Don't trim, by default */
    const char *cl_trim_interval;

    cl_maxage = retrocl_get_config_str(CONFIG_CHANGELOG_MAXAGE_ATTRIBUTE);
    if (cl_maxage) {
        if (slapi_is_duration_valid(cl_maxage)) {
            ageval = slapi_parse_duration(cl_maxage);
            slapi_ch_free_string((char **)&cl_maxage);
        } else {
            slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME,
                          "retrocl_init_trimming: ignoring invalid %s value %s; "
                          "not trimming retro changelog.\n",
                          CONFIG_CHANGELOG_MAXAGE_ATTRIBUTE, cl_maxage);
            slapi_ch_free_string((char **)&cl_maxage);
            return;
        }
    }

    cl_trim_interval = retrocl_get_config_str(CONFIG_CHANGELOG_TRIM_INTERVAL);
    if (cl_trim_interval) {
        trim_interval = strtol(cl_trim_interval, (char **)NULL, 10);
        if (0 == trim_interval) {
            slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME,
                          "retrocl_init_trimming: ignoring invalid %s value %s; "
                          "resetting the default %d\n",
                          CONFIG_CHANGELOG_TRIM_INTERVAL, cl_trim_interval,
                          DEFAULT_CHANGELOGDB_TRIM_INTERVAL);
            trim_interval = DEFAULT_CHANGELOGDB_TRIM_INTERVAL;
        }
        slapi_ch_free_string((char **)&cl_trim_interval);
    }

    ts.ts_c_max_age = ageval;
    ts.ts_s_last_trim = (time_t)0L;
    ts.ts_s_trimming = 0;
    if ((ts.ts_s_trim_mutex = PR_NewLock()) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME, "set_changelog_trim_constraints: "
                                                          "cannot create new lock.\n");
        exit(1);
    }
    ts.ts_s_initialized = 1;
    retrocl_trimming = 1;

    retrocl_trim_ctx = slapi_eq_repeat_rel(retrocl_housekeeping,
                                           NULL, (time_t)0,
                                           /* in milliseconds */
                                           trim_interval * 1000);
}

/*
 * Function: retrocl_stop_trimming
 *
 * Returns: none
 *
 * Arguments: none
 *
 * Description: called when server is shutting down to ensure trimming stops
 * eventually.
 *
 */

void
retrocl_stop_trimming(void)
{
    if (retrocl_trimming) {
        /* RetroCL trimming config was valid and trimming struct allocated
         * Let's free them
         */
        retrocl_trimming = 0;
        if (retrocl_trim_ctx) {
            slapi_eq_cancel_rel(retrocl_trim_ctx);
            retrocl_trim_ctx = NULL;
        }
        PR_DestroyLock(ts.ts_s_trim_mutex);
        ts.ts_s_trim_mutex = NULL;
    }
}

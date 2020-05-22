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

static changeNumber retrocl_internal_cn = 0;
static changeNumber retrocl_first_cn = 0;
static int check_last_changenumber = 0;

/*
 * Function: a2changeNumber
 *
 * Returns: changeNumber (long)
 *
 * Arguments: string
 *
 * Description: parses the string to a changenumber.  changenumbers are
 * positive integers.
 *
 */

static changeNumber
a2changeNumber(const char *p)
{
    changeNumber c;

    c = strntoul((char *)p, strlen(p), 10);
    return c;
}

/*
 * Function: handle_cnum_entry
 * Arguments: op - pointer to Operation struct for this operation
 *            e - pointer to returned entry.
 * Returns: nothing
 * Description: Search result handler for retrocl_getchangenum().  Sets the
 *              op->o_handler_data to point to a structure which contains
 *              the changenumber retrieved and an error code.
 */
static int
handle_cnum_entry(Slapi_Entry *e, void *callback_data)
{
    cnumRet *cr = (cnumRet *)callback_data;
    Slapi_Value *sval = NULL;
    const struct berval *value;

    cr->cr_cnum = 0UL;
    cr->cr_time = NULL;

    if (NULL != e) {
        Slapi_Attr *chattr = NULL;
        sval = NULL;
        value = NULL;
        if (slapi_entry_attr_find(e, retrocl_changenumber, &chattr) == 0) {
            slapi_attr_first_value(chattr, &sval);
            if (NULL != sval) {
                value = slapi_value_get_berval(sval);
                if (NULL != value && NULL != value->bv_val &&
                    '\0' != value->bv_val[0]) {
                    cr->cr_cnum = a2changeNumber(value->bv_val);
                }
            }
        }
        chattr = NULL;
        sval = NULL;
        value = NULL;

        chattr = NULL;
        sval = NULL;
        value = NULL;
        if (slapi_entry_attr_find(e, retrocl_changetime, &chattr) == 0) {
            slapi_attr_first_value(chattr, &sval);
            if (NULL != sval) {
                value = slapi_value_get_berval(sval);
                if (NULL != value && NULL != value->bv_val &&
                    '\0' != value->bv_val[0]) {
                    cr->cr_time = slapi_ch_strdup(value->bv_val);
                }
            }
        }
    }
    return 0;
}


/*
 * Function: handle_cnum_result
 * Arguments: err - error code returned from search
 *            callback_data - private data for callback
 * Returns: nothing
 * Description: result handler for retrocl_getchangenum().  Sets the cr_lderr
 *              field of the cnumRet struct to the error returned
 *              from the backend.
 */
static void
handle_cnum_result(int err, void *callback_data)
{
    cnumRet *cr = (cnumRet *)callback_data;
    cr->cr_lderr = err;
}

/*
 * Function: retrocl_get_changenumbers
 *
 * Returns: 0/-1
 *
 * Arguments: none
 *
 * Description: reads the first and last entry in the changelog to obtain
 * the starting and ending change numbers.
 *
 */

int
retrocl_get_changenumbers(void)
{
    cnumRet cr;

    if (retrocl_be_changelog == NULL)
        return -1;

    cr.cr_cnum = 0;
    cr.cr_time = 0;

    slapi_seq_callback(RETROCL_CHANGELOG_DN, SLAPI_SEQ_FIRST,
                       (char *)retrocl_changenumber, /* cast away const */
                       NULL, NULL, 0, &cr, NULL, handle_cnum_result,
                       handle_cnum_entry, NULL);

    slapi_rwlock_wrlock(retrocl_cn_lock);

    retrocl_first_cn = cr.cr_cnum;
    slapi_ch_free((void **)&cr.cr_time);

    slapi_seq_callback(RETROCL_CHANGELOG_DN, SLAPI_SEQ_LAST,
                       (char *)retrocl_changenumber, /* cast away const */
                       NULL, NULL, 0, &cr, NULL, handle_cnum_result,
                       handle_cnum_entry, NULL);

    retrocl_internal_cn = cr.cr_cnum;

    slapi_log_err(SLAPI_LOG_PLUGIN, "retrocl", "Got changenumbers %lu and %lu\n",
                  retrocl_first_cn,
                  retrocl_internal_cn);

    slapi_rwlock_unlock(retrocl_cn_lock);

    slapi_ch_free((void **)&cr.cr_time);

    return 0;
}

/*
 * Function: retrocl_getchangetime
 * Arguments: type - one of SLAPI_SEQ_FIRST, SLAPI_SEQ_LAST
 * Returns: The time of the requested change record.  If the return value is
 *          NO_TIME, the changelog could not be read.
 *          If err is non-NULL, the memory it points to is set the the
 *          error code returned from the backend.  If "type" is not valid,
 *          *err is set to -1.
 * Description: Get the first or last changenumber stored in the changelog,
 *              depending on the value of argument "type".
 */
time_t
retrocl_getchangetime(int type, int *err)
{
    cnumRet cr = {0};
    time_t ret;

    if (type != SLAPI_SEQ_FIRST && type != SLAPI_SEQ_LAST) {
        if (err != NULL) {
            *err = -1;
        }
        return NO_TIME;
    }
    slapi_seq_callback(RETROCL_CHANGELOG_DN, type,
                       (char *)retrocl_changenumber, /* cast away const */
                       NULL,
                       NULL, 0, &cr, NULL,
                       handle_cnum_result, handle_cnum_entry, NULL);

    if (err != NULL) {
        *err = cr.cr_lderr;
    }

    if (NULL == cr.cr_time) {
        ret = NO_TIME;
    } else {
        ret = parse_localTime(cr.cr_time);
    }
    slapi_ch_free((void **)&cr.cr_time);
    return ret;
}

/*
 * Function: retrocl_forget_changenumbers
 *
 * Returns: none
 *
 * Arguments: none
 *
 * Description: used only when the server is shutting down
 *
 */

void
retrocl_forget_changenumbers(void)
{
    slapi_rwlock_wrlock(retrocl_cn_lock);
    retrocl_first_cn = 0;
    retrocl_internal_cn = 0;
    slapi_rwlock_unlock(retrocl_cn_lock);
}

/*
 * Function: retrocl_get_first_changenumber
 *
 * Returns: changeNumber
 *
 * Arguments: none
 *
 * Description: used in root DSE
 *
 */

changeNumber
retrocl_get_first_changenumber(void)
{
    changeNumber cn;

    slapi_rwlock_rdlock(retrocl_cn_lock);
    cn = retrocl_first_cn;
    slapi_rwlock_unlock(retrocl_cn_lock);

    return cn;
}

/*
 * Function: retrocl_set_first_changenumber
 *
 * Returns: none
 *
 * Arguments: changenumber
 *
 * Description: used in changelog trimming
 *
 */

void
retrocl_set_first_changenumber(changeNumber cn)
{
    slapi_rwlock_wrlock(retrocl_cn_lock);
    retrocl_first_cn = cn;
    slapi_rwlock_unlock(retrocl_cn_lock);
}


/*
 * Function: retrocl_get_last_changenumber
 *
 * Returns:
 *
 * Arguments:
 *
 * Description: used in root DSE
 *
 */

changeNumber
retrocl_get_last_changenumber(void)
{
    changeNumber cn;

    slapi_rwlock_rdlock(retrocl_cn_lock);
    cn = retrocl_internal_cn;
    slapi_rwlock_unlock(retrocl_cn_lock);

    return cn;
}

/*
 * Function: retrocl_commit_changenumber
 *
 * Returns: none
 *
 * Arguments: none, lock must be held
 *
 * Description: NOTE! MUST BE PRECEEDED BY retrocl_assign_changenumber
 *
 */

void
retrocl_commit_changenumber(void)
{
    slapi_rwlock_wrlock(retrocl_cn_lock);
    if (retrocl_first_cn == 0) {
        retrocl_first_cn = retrocl_internal_cn;
    }
    slapi_rwlock_unlock(retrocl_cn_lock);
}

/*
 * Function: retrocl_release_changenumber
 *
 * Returns: none
 *
 * Arguments: none, lock must be held
 *
 * Description: NOTE! MUST BE PRECEEDED BY retrocl_assign_changenumber
 *
 */

void
retrocl_release_changenumber(void)
{
    slapi_rwlock_wrlock(retrocl_cn_lock);
    retrocl_internal_cn--;
    slapi_rwlock_unlock(retrocl_cn_lock);
}

/*
 * Function: retrocl_update_lastchangenumber
 *
 * Returns: 0/-1
 *
 * Arguments: none.  The caller should have taken write lock for the change numbers
 *
 * Description: reads the last entry in the changelog to obtain
 * the last change number.
 *
 */

int
retrocl_update_lastchangenumber(void)
{
    cnumRet cr;

    if (retrocl_be_changelog == NULL)
        return -1;

    slapi_rwlock_unlock(retrocl_cn_lock);
    cr.cr_cnum = 0;
    cr.cr_time = 0;
    slapi_seq_callback(RETROCL_CHANGELOG_DN, SLAPI_SEQ_LAST,
                       (char *)retrocl_changenumber, /* cast away const */
                       NULL, NULL, 0, &cr, NULL, handle_cnum_result,
                       handle_cnum_entry, NULL);

    slapi_rwlock_wrlock(retrocl_cn_lock);
    retrocl_internal_cn = cr.cr_cnum;
    slapi_log_err(SLAPI_LOG_PLUGIN, "retrocl", "Refetched last changenumber =  %lu \n",
                  retrocl_internal_cn);

    slapi_ch_free((void **)&cr.cr_time);

    return 0;
}


/*
 * Function: retrocl_assign_changenumber
 *
 * Returns: change number, 0 on error
 *
 * Arguments: none.  Lock must be held.
 *
 * Description: NOTE! MUST BE FOLLOWED BY retrocl_commit_changenumber or
 * retrocl_release_changenumber
 *
 */

changeNumber
retrocl_assign_changenumber(void)
{
    changeNumber cn;

    /* Before we assign the changenumber; we should check for the
     * validity of the internal assignment of retrocl_internal_cn
     * we had from the startup */

    slapi_rwlock_wrlock(retrocl_cn_lock);

    if ((check_last_changenumber) ||
        ((retrocl_internal_cn <= retrocl_first_cn) &&
         (retrocl_internal_cn > 1))) {
        /* the numbers have become out of sync - retrocl_get_changenumbers
         * gets called only once during startup and it may have had a problem
         * getting the last changenumber.
         * If there was any problem then update the lastchangenumber from the changelog db.
         * This function is being called by only the thread that is actually writing
         * to the changelog.
         *
         * after the first change was applied both _cn numbers are 1, that's ok
         */
        retrocl_update_lastchangenumber();
        check_last_changenumber = 0;
    }
    retrocl_internal_cn++;
    cn = retrocl_internal_cn;

    slapi_rwlock_unlock(retrocl_cn_lock);

    return cn;
}

void
retrocl_set_check_changenumber(void)
{
    slapi_rwlock_wrlock(retrocl_cn_lock);
    check_last_changenumber = 1;
    slapi_rwlock_unlock(retrocl_cn_lock);
}

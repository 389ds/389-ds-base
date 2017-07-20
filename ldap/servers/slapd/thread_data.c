/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2012 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

/*
 *   Thread Local Storage Functions
 */
#include <slapi-plugin.h>
#include <prthread.h>

void td_dn_destructor(void *priv);

/*
 * Thread Local Storage Indexes
 */
static PRUintn td_requestor_dn; /* TD_REQUESTOR_DN */
static PRUintn td_plugin_list;  /* SLAPI_TD_PLUGIN_LIST_LOCK - integer set to 1 or zero */

/*
 *   Index types defined in slapi-plugin.h
 *
 *   #define  SLAPI_TD_REQUESTOR_DN   1
 *   ...
 *   ...
 */


/*
 *  The Process:
 *
 *   [1]  Create new index type macro in slapi-plugin.h
 *   [2]  Create new static "PRUintn" index
 *   [3]  Update these functions with the new index:
 *          slapi_td_init()
 *          slapi_td_set_val()
 *          slapi_td_get_val()
 *   [4]  Create wrapper functions if so desired, and update slapi_plugin.h
 *   [5]  Create destructor (if necessary)
 */

int
slapi_td_init(int indexType)
{
    switch (indexType) {
    case SLAPI_TD_REQUESTOR_DN:
        if (PR_NewThreadPrivateIndex(&td_requestor_dn, td_dn_destructor) == PR_FAILURE) {
            return PR_FAILURE;
        }
        break;
    case SLAPI_TD_PLUGIN_LIST_LOCK:
        if (PR_NewThreadPrivateIndex(&td_plugin_list, NULL) == PR_FAILURE) {
            return PR_FAILURE;
        }
        break;

    default:
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}

/*
 *  Caller needs to cast value to (void *)
 */
int
slapi_td_set_val(int indexType, void *value)
{
    switch (indexType) {
    case SLAPI_TD_REQUESTOR_DN:
        if (td_requestor_dn) {
            if (PR_SetThreadPrivate(td_requestor_dn, value) == PR_FAILURE) {
                return PR_FAILURE;
            }
        } else {
            return PR_FAILURE;
        }
        break;
    case SLAPI_TD_PLUGIN_LIST_LOCK:
        if (td_plugin_list) {
            if (PR_SetThreadPrivate(td_plugin_list, value) == PR_FAILURE) {
                return PR_FAILURE;
            }
        } else {
            return PR_FAILURE;
        }
        break;
    default:
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}

/*
 *  Caller needs to cast value to (void **)
 */
void
slapi_td_get_val(int indexType, void **value)
{
    switch (indexType) {
    case SLAPI_TD_REQUESTOR_DN:
        if (td_requestor_dn) {
            *value = PR_GetThreadPrivate(td_requestor_dn);
        } else {
            *value = NULL;
        }
        break;
    case SLAPI_TD_PLUGIN_LIST_LOCK:
        if (td_plugin_list) {
            *value = PR_GetThreadPrivate(td_plugin_list);
        } else {
            *value = 0;
        }
        break;
    default:
        *value = NULL;
        return;
    }
}

/*
 *  Wrapper Functions
 */

/* plugin list locking */
int
slapi_td_plugin_lock_init()
{
    if (slapi_td_init(SLAPI_TD_PLUGIN_LIST_LOCK) == PR_FAILURE) {
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}

int
slapi_td_set_plugin_locked()
{
    int val = 12345;

    if (slapi_td_set_val(SLAPI_TD_PLUGIN_LIST_LOCK, (void *)&val) == PR_FAILURE) {
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}

int
slapi_td_set_plugin_unlocked()
{
    if (slapi_td_set_val(SLAPI_TD_PLUGIN_LIST_LOCK, NULL) == PR_FAILURE) {
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}

int
slapi_td_get_plugin_locked()
{
    int *value = 0;

    slapi_td_get_val(SLAPI_TD_PLUGIN_LIST_LOCK, (void **)&value);
    if (value) {
        return 1;
    } else {
        return 0;
    }
}

/* requestor dn */
int
slapi_td_dn_init()
{
    if (slapi_td_init(SLAPI_TD_REQUESTOR_DN) == PR_FAILURE) {
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}

int
slapi_td_set_dn(char *value)
{
    if (slapi_td_set_val(SLAPI_TD_REQUESTOR_DN, (void *)value) == PR_FAILURE) {
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}

void
slapi_td_get_dn(char **value)
{
    slapi_td_get_val(SLAPI_TD_REQUESTOR_DN, (void **)value);
}


/*
 *   Destructor Functions
 */

void
td_dn_destructor(void *priv)
{
    slapi_ch_free((void **)&priv);
}

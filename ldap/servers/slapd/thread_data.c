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
#include "slap.h"
#include <prthread.h>

void td_dn_destructor(void *priv);

/*
 * Thread Local Storage Indexes
 */
static PRUintn td_requestor_dn; /* TD_REQUESTOR_DN */
static PRUintn td_plugin_list;  /* SLAPI_TD_PLUGIN_LIST_LOCK - integer set to 1 or zero */
static PRUintn td_conn_id;
static PRUintn td_op_id;
static PRUintn td_op_internal_id;
static PRUintn td_op_internal_nested_state;
static PRUintn td_op_internal_nested_count;

/* defines for internal logging */
#define NOTNESTED 0
#define NESTED 1
#define UNNESTED 2

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
slapi_td_init(void)
{
    int32_t init_val = 0;

    if (PR_NewThreadPrivateIndex(&td_requestor_dn, td_dn_destructor) == PR_FAILURE) {
        slapi_log_err(SLAPI_LOG_CRIT, "slapi_td_init", "Failed it create private thread index for td_requestor_dn/td_dn_destructor\n");
        return PR_FAILURE;
    }

    if (PR_NewThreadPrivateIndex(&td_plugin_list, NULL) == PR_FAILURE) {
        slapi_log_err(SLAPI_LOG_CRIT, "slapi_td_init", "Failed it create private thread index for td_plugin_list\n");
        return PR_FAILURE;
    }

    if(PR_NewThreadPrivateIndex(&td_conn_id, NULL) == PR_FAILURE){
        slapi_log_err(SLAPI_LOG_CRIT, "slapi_td_init", "Failed it create private thread index for td_conn_id\n");
        return PR_FAILURE;
    }
    slapi_td_set_val(SLAPI_TD_CONN_ID, (void *)&init_val);

    if(PR_NewThreadPrivateIndex(&td_op_id, NULL) == PR_FAILURE){
        slapi_log_err(SLAPI_LOG_CRIT, "slapi_td_init", "Failed it create private thread index for td_op_id\n");
        return PR_FAILURE;
    }
    slapi_td_set_val(SLAPI_TD_OP_ID, (void *)&init_val);

    if(PR_NewThreadPrivateIndex(&td_op_internal_id, NULL) == PR_FAILURE){
        slapi_log_err(SLAPI_LOG_CRIT, "slapi_td_init", "Failed it create private thread index for td_op_internal_id\n");
        return PR_FAILURE;
    }
    slapi_td_set_val(SLAPI_TD_OP_INTERNAL_ID, (void *)&init_val);

    if(PR_NewThreadPrivateIndex(&td_op_internal_nested_count, NULL) == PR_FAILURE){
        slapi_log_err(SLAPI_LOG_CRIT, "slapi_td_init", "Failed it create private thread index for td_op_internal_nested_count\n");
        return PR_FAILURE;
    }
    slapi_td_set_val(SLAPI_TD_OP_NESTED_COUNT, (void *)&init_val);

    if(PR_NewThreadPrivateIndex(&td_op_internal_nested_state, NULL) == PR_FAILURE){
        slapi_log_err(SLAPI_LOG_CRIT, "slapi_td_init", "Failed it create private thread index for td_op_internal_nested_state\n");
        return PR_FAILURE;
    }
    slapi_td_set_val(SLAPI_TD_OP_NESTED_STATE, (void *)&init_val);


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
    case SLAPI_TD_CONN_ID:
        if(td_conn_id){
            if(PR_SetThreadPrivate(td_conn_id, value) == PR_FAILURE){
                return PR_FAILURE;
            }
        } else {
            return PR_FAILURE;
        }
        break;
    case SLAPI_TD_OP_ID:
        if(td_op_id){
            if(PR_SetThreadPrivate(td_op_id, value) == PR_FAILURE){
                return PR_FAILURE;
            }
        } else {
            return PR_FAILURE;
        }
        break;
    case SLAPI_TD_OP_INTERNAL_ID:
        if(td_op_internal_id){
            if(PR_SetThreadPrivate(td_op_internal_id, value) == PR_FAILURE){
                return PR_FAILURE;
            }
        } else {
            return PR_FAILURE;
        }
        break;
    case SLAPI_TD_OP_NESTED_COUNT:
        if(td_op_internal_nested_count){
            if(PR_SetThreadPrivate(td_op_internal_nested_count, value) == PR_FAILURE){
                return PR_FAILURE;
            }
        } else {
            return PR_FAILURE;
        }
        break;
    case SLAPI_TD_OP_NESTED_STATE:
        if(td_op_internal_nested_state){
            if(PR_SetThreadPrivate(td_op_internal_nested_state, value) == PR_FAILURE){
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
    case SLAPI_TD_CONN_ID:
        if(td_conn_id){
            *value = PR_GetThreadPrivate(td_conn_id);
        } else {
            *value = 0;
        }
        break;
    case SLAPI_TD_OP_ID:
        if(td_op_id){
            *value = PR_GetThreadPrivate(td_op_id);
        } else {
            *value = 0;
        }
        break;
    case SLAPI_TD_OP_INTERNAL_ID:
        if(td_op_internal_id){
            *value = PR_GetThreadPrivate(td_op_internal_id);
        } else {
            *value = 0;
        }
        break;
    case SLAPI_TD_OP_NESTED_COUNT:
        if(td_op_internal_nested_count){
            *value = PR_GetThreadPrivate(td_op_internal_nested_count);
        } else {
            *value = 0;
        }
        break;
    case SLAPI_TD_OP_NESTED_STATE:
        if(td_op_internal_nested_state){
            *value = PR_GetThreadPrivate(td_op_internal_nested_state);
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
 * Increment the internal operation count.  Since internal operations
 * can be nested via plugins calling plugins we need to keep track of
 * this.  If we become nested, and finally become unnested (back to the
 * original internal op), then we have to bump the op id number twice
 * for the next new (unnested) internal op.
 */
void
slapi_td_internal_op_start(void)
{
    int32_t initial_count = 1;
    int32_t *id_count_ptr = NULL;
    int32_t *nested_state_ptr = NULL;
    int32_t *nested_count_ptr = NULL;
    uint64_t *connid = NULL;

    slapi_td_get_val(SLAPI_TD_CONN_ID, (void **)&connid);
    if (connid == NULL){
        /* No connection id, just return */
        return;
    }

    /* increment the internal op id counter */
    slapi_td_get_val(SLAPI_TD_OP_INTERNAL_ID, (void **)&id_count_ptr);
    if (id_count_ptr == NULL){
        id_count_ptr = &initial_count;
    } else {
        (*id_count_ptr)++;
    }

    /*
     * Bump the nested count so we can maintain our counts after plugins call
     * plugins, etc.
     */
    slapi_td_get_val(SLAPI_TD_OP_NESTED_COUNT, (void **)&nested_count_ptr);
    (*nested_count_ptr)++;

    /* Now check for special cases in the nested count */
    if (*nested_count_ptr == 2){
        /* We are now nested, mark it as so */
        slapi_td_get_val(SLAPI_TD_OP_NESTED_STATE, (void **)&nested_state_ptr);
        *nested_state_ptr = NESTED;
        slapi_td_set_val(SLAPI_TD_OP_NESTED_STATE, (void *)nested_state_ptr);
    } else if (*nested_count_ptr == 1) {
        /*
         * Back to the beginning, but if we were previously nested then the
         * internal op id count is off
         */
        slapi_td_get_val(SLAPI_TD_OP_NESTED_STATE, (void **)&nested_state_ptr);
        if (*nested_state_ptr == UNNESTED){
            /* We were nested but anymore, need to bump the internal id count again */
            *nested_state_ptr = NOTNESTED;  /* reset nested state */
            slapi_td_set_val(SLAPI_TD_OP_NESTED_STATE, (void *)nested_state_ptr);
            (*id_count_ptr)++;
        }
    }
    slapi_td_set_val(SLAPI_TD_OP_NESTED_COUNT, (void *)nested_count_ptr);
    slapi_td_set_val(SLAPI_TD_OP_INTERNAL_ID, (void *)id_count_ptr);
}

/*
 * Decrement the nested count.  If we were actually nested (2 levels deep or more)
 * then we need to lower the op id.  If we were nested and are now unnested we need
 * to mark this in the TD so on the next new internal op we set the its op id to the
 * correct/expected/next-sequential value.
 */
void
slapi_td_internal_op_finish(void)
{
    int32_t *nested_count_ptr = NULL;
    int32_t *nested_state_ptr = NULL;
    int32_t *id_count_ptr = NULL;
    uint64_t *connid = NULL;

    slapi_td_get_val(SLAPI_TD_OP_INTERNAL_ID, (void **)&connid);
    if (connid == NULL){
        /* No connection id, just return */
        return;
    }

    slapi_td_get_val(SLAPI_TD_OP_NESTED_COUNT, (void **)&nested_count_ptr);
    if (nested_count_ptr){
        if ( *nested_count_ptr > 1 ){
            /* Nested op just finished, decr op id */
            slapi_td_get_val(SLAPI_TD_OP_INTERNAL_ID, (void **)&id_count_ptr);
            if (id_count_ptr){
                (*id_count_ptr)--;
                slapi_td_set_val(SLAPI_TD_OP_INTERNAL_ID, (void *)id_count_ptr);
            }
            if ( (*nested_count_ptr - 1) == 1 ){
                /*
                 * Okay we are back to the beginning, We were nested but not
                 * anymore.  So when we start the next internal op on this
                 * conn we need to double increment the internal op id to
                 * maintain the correct op id sequence.  Set the nested state
                 * to "unnested".
                 */
                slapi_td_get_val(SLAPI_TD_OP_NESTED_STATE, (void **)&nested_state_ptr);
                (*nested_state_ptr) = UNNESTED;
                slapi_td_set_val(SLAPI_TD_OP_NESTED_STATE, (void *)nested_state_ptr);
            }
        }
        /* decrement nested count */
        (*nested_count_ptr)--;
        slapi_td_set_val(SLAPI_TD_OP_NESTED_COUNT, (void *)nested_count_ptr);
    }
}

/*
 *   Destructor Functions
 */

void
td_dn_destructor(void *priv)
{
    slapi_ch_free((void **)&priv);
}


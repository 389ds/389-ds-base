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
#include <pthread.h>

/*
 * Thread Local Storage Indexes
 */
static pthread_key_t td_requestor_dn; /* TD_REQUESTOR_DN */
static pthread_key_t td_plugin_list;  /* SLAPI_TD_PLUGIN_LIST_LOCK - integer set to 1 or zero */
static pthread_key_t td_op_state;

/*
 *   Destructor Functions
 */

static void
td_dn_destructor(void *priv)
{
    slapi_ch_free((void **)&priv);
}

static void
td_op_state_destroy(void *priv) {
    slapi_ch_free((void **)&priv);
}

int32_t
slapi_td_init(void)
{
    if (pthread_key_create(&td_requestor_dn, td_dn_destructor) != 0) {
        slapi_log_err(SLAPI_LOG_CRIT, "slapi_td_init", "Failed it create private thread index for td_requestor_dn/td_dn_destructor\n");
        return PR_FAILURE;
    }

    if (pthread_key_create(&td_plugin_list, NULL) != 0) {
        slapi_log_err(SLAPI_LOG_CRIT, "slapi_td_init", "Failed it create private thread index for td_plugin_list\n");
        return PR_FAILURE;
    }

    if(pthread_key_create(&td_op_state, td_op_state_destroy) != 0){
        slapi_log_err(SLAPI_LOG_CRIT, "slapi_td_init", "Failed it create private thread index for td_op_state\n");
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}


/*
 *  Wrapper Functions
 */

/* plugin list locking */
int32_t
slapi_td_set_plugin_locked()
{
    int32_t val = 12345;

    if (pthread_setspecific(td_plugin_list, (void *)&val) != 0) {
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}

int32_t
slapi_td_set_plugin_unlocked()
{
    if (pthread_setspecific(td_plugin_list, NULL) != 0) {
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}

int32_t
slapi_td_get_plugin_locked()
{
    int32_t *value = pthread_getspecific(td_plugin_list);

    if (value == NULL) {
        return 0;
    }
    return 1;
}

/* requestor dn */
int32_t
slapi_td_set_dn(char *value)
{
    if (pthread_setspecific(td_requestor_dn, value) != 0) {
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}

void
slapi_td_get_dn(char **value)
{
    if (value) {
        *value = pthread_getspecific(td_requestor_dn);
    }
}

/* Worker op-state */
struct slapi_td_log_op_state_t *
slapi_td_get_log_op_state() {
    return pthread_getspecific(td_op_state);
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
    struct slapi_td_log_op_state_t *op_state = pthread_getspecific(td_op_state);

    /* Allocate if needed */
    if (op_state == NULL) {
        op_state = (struct slapi_td_log_op_state_t *)slapi_ch_calloc(1, sizeof(struct slapi_td_log_op_state_t));
        if (pthread_setspecific(td_op_state, op_state) != 0) {
            slapi_log_err(SLAPI_LOG_CRIT, "slapi_td_internal_op_start",
                          "Failed to set op_state to td_op_state. OOM?\n");
            return;
        }
    }

    /* increment the internal op id counter */
    op_state->op_int_id += 1;

    /*
     * Bump the nested count so we can maintain our counts after plugins call
     * plugins, etc.
     */
    op_state->op_nest_count += 1;

    /* Now check for special cases in the nested count */
    if (op_state->op_nest_count == 2){
        /* We are now nested, mark it as so */
        /* THERE IS A BETTER WAY! We should track parent op structs instead! */
        op_state->op_nest_state = OP_STATE_NESTED;
    } else if (op_state->op_nest_count == 1) {
        /*
         * Back to the beginning, but if we were previously nested then the
         * internal op id count is off
         */
        if (op_state->op_nest_state == OP_STATE_UNNESTED){
            /* We were nested but anymore, need to bump the internal id count again */
            op_state->op_nest_state = OP_STATE_NOTNESTED; /* reset nested state */
            op_state->op_int_id += 1;
        }
    }
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
    struct slapi_td_log_op_state_t *op_state = pthread_getspecific(td_op_state);

    /* Allocate if needed - should be unreachable!*/
    PR_ASSERT(op_state);
    if (op_state == NULL) {
        op_state = (struct slapi_td_log_op_state_t *)slapi_ch_calloc(1, sizeof(struct slapi_td_log_op_state_t));
        if (pthread_setspecific(td_op_state, op_state) != 0) {
            slapi_log_err(SLAPI_LOG_CRIT, "slapi_td_internal_op_finish",
                          "Failed to set op_state to td_op_state. OOM?\n");
            return;
        }
    }

    if ( op_state->op_nest_count > 1 ){
        /* Nested op just finished, decr op id */
        op_state->op_int_id -= 1;

        if ( (op_state->op_nest_count - 1) == 1 ){
            /*
             * Okay we are back to the beginning, We were nested but not
             * anymore.  So when we start the next internal op on this
             * conn we need to double increment the internal op id to
             * maintain the correct op id sequence.  Set the nested state
             * to "unnested".
             */
            op_state->op_nest_state = OP_STATE_UNNESTED;
        }
    }
    /* decrement nested count */
    op_state->op_nest_count -= 1;
}

void
slapi_td_reset_internal_logging(uint64_t new_conn_id, int32_t new_op_id)
{
    struct slapi_td_log_op_state_t *op_state = pthread_getspecific(td_op_state);

    /* Allocate if needed */
    if (op_state == NULL) {
        op_state = (struct slapi_td_log_op_state_t *)slapi_ch_calloc(1, sizeof(struct slapi_td_log_op_state_t));
        if (pthread_setspecific(td_op_state, op_state) != 0) {
            slapi_log_err(SLAPI_LOG_CRIT, "slapi_td_internal_op_finish",
                          "Failed to set op_state to td_op_state. OOM?\n");
            return;
        }
    }
    op_state->conn_id = new_conn_id;
    op_state->op_id = new_op_id;
    op_state->op_int_id = 0;
    op_state->op_nest_count = 0;
    op_state->op_nest_state = OP_STATE_NOTNESTED;
}

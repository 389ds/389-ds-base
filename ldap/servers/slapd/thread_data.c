/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2023 Red Hat, Inc.
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
static pthread_key_t td_requestor_dn = 0; /* TD_REQUESTOR_DN */
static pthread_key_t td_plugin_list = 0;  /* SLAPI_TD_PLUGIN_LIST_LOCK - integer set to 1 or zero */
static pthread_key_t td_op_state = 0;
static pthread_key_t td_attr_syntax_oid_table = 0;
static pthread_key_t td_attr_syntax_name_table = 0;

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

    /* Attribute syntax tables */
    if(pthread_key_create(&td_attr_syntax_oid_table, NULL) != 0){
        slapi_log_err(SLAPI_LOG_CRIT, "slapi_td_init", "Failed it create private thread index for td_attr_syntax_oid_table\n");
        return PR_FAILURE;
    }
    if(pthread_key_create(&td_attr_syntax_name_table, NULL) != 0){
        slapi_log_err(SLAPI_LOG_CRIT, "slapi_td_init", "Failed it create private thread index for td_attr_syntax_name_table\n");
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}


/*
 *  Wrapper Functions
 */

/* attr syntax tables */
int32_t
slapi_td_set_attr_syntax_name_table(PLHashTable *ht)
{
    if (pthread_setspecific(td_attr_syntax_name_table, ht) != 0) {
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}
void
slapi_td_get_attr_syntax_name_table(PLHashTable **ht)
{
    if (ht) {
        *ht = pthread_getspecific(td_attr_syntax_name_table);
    }
}

int32_t
slapi_td_set_attr_syntax_oid_table(PLHashTable *ht)
{
    if (pthread_setspecific(td_attr_syntax_oid_table, ht) != 0) {
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}
void
slapi_td_get_attr_syntax_oid_table(PLHashTable **ht)
{
    if (ht) {
        *ht = pthread_getspecific(td_attr_syntax_oid_table);
    }
}

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
    char *dn = pthread_getspecific(td_requestor_dn);
    slapi_ch_free_string(&dn);
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
 * Increment the internal operation count.  Unless we are nested, in that case
 * do not update the internal op counter.  If we just became "unnested" then
 * update the state to keep the counters on track.
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

    /* Bump the nested count */
    op_state->op_nest_count += 1;

    if (op_state->op_nest_count > 1){
        /* We are nested */
        op_state->op_nest_state = OP_STATE_NESTED;
    } else {
        /* We are not nested */
        op_state->op_int_id += 1;
        if (op_state->op_nest_state == OP_STATE_PREV_NESTED) {
            /* But we were just previously nested, so update the state */
            op_state->op_nest_state = OP_STATE_NOTNESTED;
        }
    }
}

/*
 * Decrement the nested count.  If we were nested and we are NOW unnested
 * then we need to reset the state so on the next new internal op we set the
 * counters to the correct/expected/next-sequential value.
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
    /* decrement nested count */
    op_state->op_nest_count -= 1;

    /* If we were nested, but NOT anymore, then update the state */
    if ( op_state->op_nest_state == OP_STATE_NESTED && op_state->op_nest_count == 1){
        op_state->op_nest_state = OP_STATE_PREV_NESTED;
    }
}

void
slapi_td_reset_internal_logging(uint64_t new_conn_id, int32_t new_op_id, time_t start_time)
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
    op_state->conn_starttime = start_time;
}

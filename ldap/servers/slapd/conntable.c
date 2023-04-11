/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * Copyright (C) 2019 William Brown <william@blackhats.net.au>
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * Connection Table
 *
 * The connection table exists to serve a few purposes:
 * 1. To prevent memory allocations of a (reasonably) large structures in a high-churn part
 *    of the codebase. (Connection from slap.h).
 * 2. To allow monitoring to iterate over the current set of active connections that exist
 *    in the server.
 * 3. To provide free connection slots to connections that have just been accepted.
 *
 * Requirement number 2 is the very interesting one, as it means that we need a central location
 * to store the connections to allow metric gatherings. Requirement 1 means less today in 2019
 * due to the improvements in malloc, and to aid tools like ASAN.
 *
 * == history ==
 * The connection table previously used a simple method to allocate. The connection table can only
 * be allocated by one acceptor at a time, which is protected by the connection table lock. The
 * table is iterated over and each connection was locked to determine if it was in use. This iteration
 * especially on high CT sizes could be very long, especially as:
 *
 * 1. The CT was always iterated from 0 -> size, meaning the "bottom end" of the table was always
 *    likely to be full, causing delays.
 * 2. The connection lock itself is what is used to protect that sockets IO, so if a connection was
 *    writing at the time, the CT would delay waiting on the connection IO, only to find that the
 *    connection was allocated anyway.
 *
 * Clearly this is an issue. In mid 2019 this behaviour was subtley altered, such that when we
 * attempted to lock we used pthread try_lock (nspr locks did not have this capability). If the try
 * lock fails this means a connection must be in use, so we can skip it. This yielded a 30%
 * improvement in throughput.
 *
 * == current ==
 * The new design is to create a parallel freelist of open slots into the table, which is protected
 * under the connection table lock. This should move the allocation algorithm from O(n) worst case
 * to O(1) worst case as we always recieve and empty slot *or* ct full. It also reduces lock/atomic
 * contention on the CPU to improve things.
 *
 * The freelist is a ringbuffer of pointers to the connection table. On a small scale it looks like:
 *
 *  |--------------------------------------------|
 *  | slot 1 | slot 2 | slot 3 | slot 4 | slot 5 |
 *  | _ ptr  | _ ptr  | _ ptr  | _ ptr  | _ ptr  |
 *  |--------------------------------------------|
 *     ^  ^- conn_next
 *     |
 *     \-- conn_free
 *
 * As we allocate, we shift conn_next through the list, yielding the ptr that was stored (and
 * setting it to NULL as we proceed)
 *
 *  |--------------------------------------------|
 *  | slot 1 | slot 2 | slot 3 | slot 4 | slot 5 |
 *  | _NULL  | _NULL  | _ ptr  | _ ptr  | _ ptr  |
 *  |--------------------------------------------|
 *     ^                  ^- conn_next
 *     |
 *     \-- conn_free
 *
 * When a connection is "freed" we return it to conn_free, which is then also slid up.
 *
 *  |--------------------------------------------|
 *  | slot 1 | slot 2 | slot 3 | slot 4 | slot 5 |
 *  | _ ptr  | _NULL  | _ ptr  | _ ptr  | _ ptr  |
 *  |--------------------------------------------|
 *              ^         ^- conn_next
 *              |
 *              \-- conn_free
 *
 * If all connections are exhausted, conn_next will == conn_next, as conn_next must have proceeded
 * to the end of the ring, and then wrapped back allocating all previous until we meet with conn_free.
 *
 *  |--------------------------------------------|
 *  | slot 1 | slot 2 | slot 3 | slot 4 | slot 5 |
 *  | _NULL  | _NULL  | _NULL  | _NULL  | _NULL  |
 *  |--------------------------------------------|
 *              ^ ^- conn_next
 *              |
 *              \-- conn_free
 *
 * This means allocations of conns will keep failing until a connection is returned.
 *
 *  |--------------------------------------------|
 *  | slot 1 | slot 2 | slot 3 | slot 4 | slot 5 |
 *  | _NULL  | _ ptr  | _NULL  | _NULL  | _NULL  |
 *  |--------------------------------------------|
 *             ^- conn_next ^
 *                          |
 *                          \-- conn_free
 *
 * And now conn_next can begin to allocate again.
 *
 *
 *  -- invariants
 * * when conn_free is slid back to meet conn_next, there can be no situation where another
 *   connection is returned, as none must allocated  -if they were allocated, conn_free would have
 *   moved_along.
 * * the ring buffer must be as large as conntable.
 * * We do not check conn_next == conn_free (that's the starting state), but we check if the
 *   slot at conn_next is NULL, which must imply that conn_free has nothing to return.
 * * connection_table_move_connection_out_of_active_list is the only function able to return a
 *   connection to the freelist, as it is the function that is called when the event system has
 *   determined all IO's are complete, or unable to complete. This function is what prepares the
 *   connection for re-use, which is why it's the only place the freelist can be appended to.
 *
 */

#include "fe.h"

Connection_Table *
connection_table_new(int table_size)
{
    Connection_Table *ct;
    size_t i = 0;
    int ct_list = 0;
    int free_idx = 0;
    ber_len_t maxbersize = config_get_maxbersize();
    ct = (Connection_Table *)slapi_ch_calloc(1, sizeof(Connection_Table));
    ct->list_num = config_get_num_listeners();
    ct->num_active = (int *)slapi_ch_calloc(1, ct->list_num * sizeof(int));
    ct->size = table_size - (table_size % ct->list_num);
    ct->list_size = ct->size/ct->list_num;
    ct->c = (Connection **)slapi_ch_calloc(1, ct->size * sizeof(Connection *));
    ct->fd = (struct POLL_STRUCT **)slapi_ch_calloc(1, ct->list_num * sizeof(struct POLL_STRUCT*));
    ct->table_mutex = PR_NewLock();
    /* Allocate the freelist */
    ct->c_freelist = (Connection **)slapi_ch_calloc(1, ct->size * sizeof(Connection *));
    /* NEVER use slot 0, this is a list pointer */
    ct->conn_next_offset = 1;
    ct->conn_free_offset = 1;

    slapi_log_err(SLAPI_LOG_INFO, "connection_table_new", "Number of connection sub-tables %d, each containing %d slots.\n",
        ct->list_num, ct->list_size);

    pthread_mutexattr_t monitor_attr = {0};
    pthread_mutexattr_init(&monitor_attr);
    pthread_mutexattr_settype(&monitor_attr, PTHREAD_MUTEX_RECURSIVE);
    for (ct_list = 0; ct_list < ct->list_num; ct_list++) {
        ct->c[ct_list] = (Connection *)slapi_ch_calloc(1, ct->list_size * sizeof(Connection));
        ct->fd[ct_list] = (struct POLL_STRUCT *)slapi_ch_calloc(1, ct->list_size * sizeof(struct POLL_STRUCT));
        /* We rely on the fact that we called calloc, which zeros the block, so we don't
        * init any structure element unless a zero value is troublesome later
        */
        for (i = 0; i < ct->list_size; i++) {
            /*
            * Technically this is a no-op due to calloc, but we should always be
            * careful with things like this ....
            */
            ct->c[ct_list][i].c_state = CONN_STATE_FREE;
            /* Start the conn setup. */

            LBER_SOCKET invalid_socket;
            /* DBDB---move this out of here once everything works */
            ct->c[ct_list][i].c_sb = ber_sockbuf_alloc();
            invalid_socket = SLAPD_INVALID_SOCKET;
            ct->c[ct_list][i].c_sd = SLAPD_INVALID_SOCKET;
            ber_sockbuf_ctrl(ct->c[ct_list][i].c_sb, LBER_SB_OPT_SET_FD, &invalid_socket);
            ber_sockbuf_ctrl(ct->c[ct_list][i].c_sb, LBER_SB_OPT_SET_MAX_INCOMING, &maxbersize);
            /* all connections start out invalid */
            ct->fd[ct_list][i].fd = SLAPD_INVALID_SOCKET;

            /* The connection table has a double linked list running through it.
            * This is used to find out which connections should be looked at
            * in the poll loop.  Slot 0 in the table is always the head of
            * the linked list.  Each slot has a c_next and c_prev which are
            * pointers back into the array of connection slots. */
            ct->c[ct_list][i].c_next = NULL;
            ct->c[ct_list][i].c_prev = NULL;
            ct->c[ct_list][i].c_ci = i;
            ct->c[ct_list][i].c_fdi = SLAPD_INVALID_SOCKET_INDEX;

            if (pthread_mutex_init(&(ct->c[ct_list][i].c_mutex), &monitor_attr) != 0) {
                slapi_log_err(SLAPI_LOG_ERR, "connection_table_new", "pthread_mutex_init failed\n");
                exit(1);
            }

            ct->c[ct_list][i].c_pdumutex = PR_NewLock();
            if (ct->c[ct_list][i].c_pdumutex == NULL) {
                slapi_log_err(SLAPI_LOG_ERR, "connection_table_new", "PR_NewLock failed\n");
                exit(1);
            }

            /* Ready to rock, mark as such. */
            ct->c[ct_list][i].c_state = CONN_STATE_INIT;

            /* Map multiple ct lists to a single freelist. */
            ct->c_freelist[free_idx++] = &(ct->c[ct_list][i]);
        }
    }

    return ct;
}

void
connection_table_free(Connection_Table *ct)
{
    /* Release the freelist */
    slapi_ch_free((void **)&ct->c_freelist);
    for (size_t ct_list = 0; ct_list < ct->list_num; ct_list++) {
        /* Now release the connections. */
        for (size_t i = 0; i < ct->list_size; i++) {
            /* Free the contents of the connection structure */
            Connection *c = &(ct->c[ct_list][i]);
            connection_done(c);
        }

        slapi_ch_free((void **)&ct->c[ct_list]);
        slapi_ch_free((void **)&ct->fd[ct_list]);
    }
    slapi_ch_free((void **)&ct->c);
    slapi_ch_free((void **)&ct->fd);
    PR_DestroyLock(ct->table_mutex);
    slapi_ch_free((void *)&ct->num_active);
    slapi_ch_free((void **)&ct);
}

void
connection_table_abandon_all_operations(Connection_Table *ct)
{
    for (size_t ct_list = 0; ct_list < ct->list_num; ct_list++) {
        for (size_t i = 0; i < ct->list_size; i++) {
            if (ct->c[ct_list][i].c_state != CONN_STATE_FREE) {
                pthread_mutex_lock(&(ct->c[ct_list][i].c_mutex));
                connection_abandon_operations(&ct->c[ct_list][i]);
                pthread_mutex_unlock(&(ct->c[ct_list][i].c_mutex));
            }
        }
    }
}

void
connection_table_disconnect_all(Connection_Table *ct)
{
    for (size_t ct_list = 0; ct_list < ct->list_num; ct_list++) {
        for (size_t i = 0; i < ct->list_size; i++) {
            if (ct->c[ct_list][i].c_state != CONN_STATE_FREE) {
                Connection *c = &(ct->c[ct_list][i]);
                pthread_mutex_lock(&(c->c_mutex));
                disconnect_server_nomutex(c, c->c_connid, -1, SLAPD_DISCONNECT_ABORT, ECANCELED);
                pthread_mutex_unlock(&(c->c_mutex));
            }
        }
    }
}

/* Given a file descriptor for a socket, this function will return
 * a slot in the connection table to use.
 *
 * Note: this function is only called from the slapd_daemon (listener)
 * thread, which means it will never be called by two threads at
 * the same time.
 *
 * Returns a Connection on success
 * Returns NULL on failure
 */
Connection *
connection_table_get_connection(Connection_Table *ct, int sd)
{
    PR_Lock(ct->table_mutex);

    PR_ASSERT(ct->conn_next_offset != 0);
    Connection *c = ct->c_freelist[ct->conn_next_offset];
    if (c != NULL) {
        /* We allocated it, so now NULL the slot and move forward. */
        ct->c_freelist[ct->conn_next_offset] = NULL;
        /* Handle overflow. */
        ct->conn_next_offset = (ct->conn_next_offset + 1) % ct->size;
        if (ct->conn_next_offset == 0) {
            /* Never use slot 0 */
            ct->conn_next_offset += 1;
        }
        PR_Unlock(ct->table_mutex);
    } else {
        /* couldn't find a Connection, table must be full */
        slapi_log_err(SLAPI_LOG_CONNS, "connection_table_get_connection", "Max open connections reached\n");
        PR_Unlock(ct->table_mutex);
        return NULL;
    }

    /* Now prep the slot for usage. */
    PR_ASSERT(c != NULL);
    PR_ASSERT(c->c_next == NULL);
    PR_ASSERT(c->c_prev == NULL);
    PR_ASSERT(c->c_extension == NULL);
    PR_ASSERT(c->c_state == CONN_STATE_INIT);
    /* Let's make sure there's no cruft left on there from the last time this connection was used. */

    /*
     * Note: no need to lock c->c_mutex because this function is only
     * called by one thread (the slapd_daemon thread), and if we got this
     * far then `c' is not being used by any operation threads, etc. The
     * memory ordering will be provided by the work queue sending c to a
     * thread.
     */
    connection_cleanup(c);
    /* pointer to connection table that owns this connection */
    c->c_ct = ct;

    return c;
}

/* active connection iteration functions */

Connection *
connection_table_get_first_active_connection(Connection_Table *ct, size_t listnum)
{
    return ct->c[listnum][0].c_next;
}

Connection *
connection_table_get_next_active_connection(Connection_Table *ct __attribute__((unused)), Connection *c)
{
    return c->c_next;
}

int
connection_table_iterate_active_connections(Connection_Table *ct, void *arg, Connection_Table_Iterate_Function f)
{
    /* Locking in this area seems rather undeveloped, I think because typically only one
     * thread accesses the connection table (daemon thread). But now we allow other threads
     * to iterate over the table. So we use the "new mutex mutex" to lock the table.
     */
    Connection *current_conn = NULL;
    int ret = 0;
    size_t i = 0;
    PR_Lock(ct->table_mutex);
    for (i = 0; i < ct->list_num; i++) {
        current_conn = connection_table_get_first_active_connection(ct, i);
        while (current_conn) {
            ret = f(current_conn, arg);
            if (ret) {
                break;
            }
            current_conn = connection_table_get_next_active_connection(ct, current_conn);
        }
    }
    PR_Unlock(ct->table_mutex);
    return ret;
}

#ifdef FOR_DEBUGGING
static void
connection_table_dump_active_connection(Connection *c)
{
    slapi_log_err(SLAPI_LOG_DEBUG, "connection_table_dump_active_connection", "conn=%p fd=%d refcnt=%d c_flags=%d\n"
                                                                              "mutex=%p next=%p prev=%p ctlist=%d\n\n",
                  c, c->c_sd, c->c_refcnt, c->c_flags,
                  c->c_mutex, c->c_next, c->c_prev, c->c_ct_list);
}

static void
connection_table_dump_active_connections(Connection_Table *ct)
{
    Connection *c;
    int i = 0;

    PR_Lock(ct->table_mutex);
    for (i = 0; i < ct->numlists; i++) {
        slapi_log_err(SLAPI_LOG_DEBUG, "connection_table_dump_active_connections", "********** BEGIN DUMP ************\n");
        c = connection_table_get_first_active_connection(ct, i);
        while (c) {
            connection_table_dump_active_connection(c);
            c = connection_table_get_next_active_connection(ct, c);
        }
    }

    slapi_log_err(SLAPI_LOG_DEBUG, "connection_table_dump_active_connections", "********** END DUMP ************\n");
    PR_Unlock(ct->table_mutex);
}
#endif

/*
 * There's a double linked list of active connections running through the array
 * of connections. This function removes a connection (by index) from that
 * list. This list is used to find the connections that should be used in the
 * poll call.
 *
 * We only remove from the active list when the connection is ready to be reused,
 * in other words, this is the "connection free" function. This is where we readd
 * the connection slot to the freelist for re-use.
 */
int

connection_table_move_connection_out_of_active_list(Connection_Table *ct, Connection *c)
{
    int c_sd; /* for logging */
    /* we always have previous element because list contains a dummy header */;
    PR_ASSERT(c->c_prev);
    if (c->c_prev == NULL) {
        /* c->c_prev is set when the connection is moved ON the active list
         * So this connection is already OUT of the active list
         *
         * Not sure how to recover from here.
         * Considering c->c_prev is NULL we can assume refcnt is now 0
         * and connection_cleanup was already called.
         * If it is not the case, then consequences are:
         *  - Leak some memory (connext, unsent page result entries, various buffers)
         *  - hanging connection (fd not closed)
         * A option would be to call connection_cleanup here.
         *
         * The logged message helps to know how frequently the problem exists
         */
        slapi_log_err(SLAPI_LOG_CRIT,
                      "connection_table_move_connection_out_of_active_list",
                      "conn %d is already OUT of the active list (refcnt is %d)\n",
                      c->c_sd, c->c_refcnt);

        return 0;
    }

#ifdef FOR_DEBUGGING
    slapi_log_err(SLAPI_LOG_DEBUG, "connection_table_move_connection_out_of_active_list", "Moving connection out of active list\n");
    connection_table_dump_active_connection(c);
#endif

    c_sd = c->c_sd;
    /*
     * Note: the connection will NOT be moved off the active list if any other threads still hold
     * a reference to the connection (that is, its reference count must be 1 or less).
     */
    if (c->c_refcnt > 1) {
        slapi_log_err(SLAPI_LOG_CONNS,
                      "connection_table_move_connection_out_of_active_list",
                      "Not moving conn %d out of active list because refcnt is %d\n",
                      c_sd, c->c_refcnt);
        return 1; /* failed */
    }

    /* We need to lock here because we're modifying the linked list */
    PR_Lock(ct->table_mutex);

    /* Decrement the number of active connections on the ct list this connection was assigned. */
    (*(ct->num_active + c->c_ct_list))--;
    c->c_ct_list = -1;

    c->c_prev->c_next = c->c_next;

    if (c->c_next) {
        c->c_next->c_prev = c->c_prev;
    }

    connection_release_nolock(c);

    /* Clean the pointer. */
    connection_cleanup(c);

    /* Finally, place the connection back into the freelist for use */
    PR_ASSERT(c->c_refcnt == 0);
    PR_ASSERT(ct->conn_free_offset != 0);
    PR_ASSERT(ct->c_freelist[ct->conn_free_offset] == NULL);
    ct->c_freelist[ct->conn_free_offset] = c;
    ct->conn_free_offset = (ct->conn_free_offset + 1) % ct->size;
    if (ct->conn_free_offset == 0) {
        /* Never use slot 0 */
        ct->conn_free_offset += 1;
    }

    PR_Unlock(ct->table_mutex);

    slapi_log_err(SLAPI_LOG_CONNS, "connection_table_move_connection_out_of_active_list",
                  "Moved conn %d out of active list and freed\n", c_sd);

#ifdef FOR_DEBUGGING
    connection_table_dump_active_connections(ct);
#endif
    return 0; /* success */
}

/*
 * There's a double linked list of active connections running through the array
 * of connections. This function adds a connection (by index) to the head of
 * that list. This list is used to find the connections that should be used in the
 * poll call.
 */
void
connection_table_move_connection_on_to_active_list(Connection_Table *ct, Connection *c)
{
    PR_ASSERT(c->c_next == NULL);
    PR_ASSERT(c->c_prev == NULL);

    PR_Lock(ct->table_mutex);

    if (connection_acquire_nolock(c)) {
        PR_ASSERT(0);
    }

#ifdef FOR_DEBUGGING
    slapi_log_err(SLAPI_LOG_DEBUG, "connection_table_move_connection_on_to_active_list",
                  "Moving connection into active list\n");
    connection_table_dump_active_connection(c);
#endif

    /* Get the least used ct list and incremant its number of active connections. */
    c->c_ct_list = connection_table_get_list(ct);
    (*(ct->num_active + c->c_ct_list))++;

    c->c_next = ct->c[c->c_ct_list][0].c_next;
    if (c->c_next != NULL) {
        c->c_next->c_prev = c;
    }
    c->c_prev = &(ct->c[c->c_ct_list][0]);
    ct->c[c->c_ct_list][0].c_next = c;

    PR_Unlock(ct->table_mutex);

#ifdef FOR_DEBUGGING
    connection_table_dump_active_connections(ct);
#endif
}

/* Find a connection table list with the lowest number of connections. */
int
connection_table_get_list(Connection_Table *ct)
{
    size_t i;
    int list = 0;
    int lowest = ct->num_active[0];
    for (i = 1; i < ct->list_num; i++) {
        if (*(ct->num_active + i) < lowest) {
            lowest = *(ct->num_active + i);
            list = i;
        }
    }
    return list;
}

/*
 * Replace the following attributes within the entry 'e' with
 * information about the connection table:
 *    connection            // multivalued; one value for each active connection
 *    currentconnections    // single valued; an integer count
 *    totalconnections        // single valued; an integer count
 *    dtablesize            // single valued; an integer size
 *    readwaiters            // single valued; an integer count
 */
void
connection_table_as_entry(Connection_Table *ct, Slapi_Entry *e)
{
    char buf[BUFSIZ];
    char maxthreadbuf[BUFSIZ];
    struct berval val;
    struct berval *vals[2];
    size_t ct_list;
    size_t i;
    int nconns, nreadwaiters;
    struct tm utm;

    vals[0] = &val;
    vals[1] = NULL;

    attrlist_delete(&e->e_attrs, "connection");
    nconns = 0;
    nreadwaiters = 0;
    for (ct_list = 0; ct_list < (ct != NULL ? ct->list_num : 0); ct_list++) {
        for (i = 0; i < ct->list_size; i++) {
            PR_Lock(ct->table_mutex);
            if (ct->c[ct_list][i].c_state == CONN_STATE_FREE) {
                PR_Unlock(ct->table_mutex);
                continue;
            }
            /* Can't take c_mutex if holding table_mutex; temporarily unlock */
            PR_Unlock(ct->table_mutex);

            pthread_mutex_lock(&(ct->c[ct_list][i].c_mutex));
            if (ct->c[ct_list][i].c_sd != SLAPD_INVALID_SOCKET) {
                char buf2[SLAPI_TIMESTAMP_BUFSIZE+1];
                size_t lendn = ct->c[ct_list][i].c_dn ? strlen(ct->c[ct_list][i].c_dn) : 6; /* "NULLDN" */
                size_t lenip = ct->c[ct_list][i].c_ipaddr ? strlen(ct->c[ct_list][i].c_ipaddr) : 0;
                size_t lenconn = 1;
                uint64_t connid = ct->c[ct_list][i].c_connid;
                char *bufptr = &buf[0];
                char *newbuf = NULL;
                int maxthreadstate = 0;

                /* Get the connid length */
                while (connid > 9) {
                    lenconn++;
                    connid /= 10;
                }

                if (ct->c[ct_list][i].c_flags & CONN_FLAG_MAX_THREADS) {
                    maxthreadstate = 1;
                }

                nconns++;
                if (ct->c[ct_list][i].c_gettingber) {
                    nreadwaiters++;
                }

                gmtime_r(&ct->c[ct_list][i].c_starttime, &utm);
                strftime(buf2, SLAPI_TIMESTAMP_BUFSIZE, "%Y%m%d%H%M%SZ", &utm);

                /*
                * Max threads per connection stats
                *
                * Appended output "1:2:3"
                *
                * 1 = Connection max threads state:  1 is in max threads, 0 is not
                * 2 = The number of times this thread has hit max threads
                * 3 = The number of operations attempted that were blocked
                *     by max threads.
                */
                snprintf(maxthreadbuf, sizeof(maxthreadbuf), "%d:%" PRIu64 ":%" PRIu64 "",
                        maxthreadstate, ct->c[ct_list][i].c_maxthreadscount,
                        ct->c[ct_list][i].c_maxthreadsblocked);

                if ((lenconn + lenip + lendn + strlen(maxthreadbuf)) > (BUFSIZ - 54)) {
                    /*
                    * 54 =  8 for the colon separators +
                    *       6 for the "i" counter +
                    *      15 for buf2 (date) +
                    *      10 for c_opsinitiated +
                    *      10 for c_opscompleted +
                    *       1 for c_gettingber +
                    *       3 for "ip=" +
                    *       1 for NULL terminator
                    */
                    newbuf = (char *)slapi_ch_malloc(lenconn + lendn + lenip + strlen(maxthreadbuf) + 54);
                    bufptr = newbuf;
                }

                sprintf(bufptr, "%zu:%s:%d:%d:%s%s:%s:%s:%" PRIu64 ":ip=%s",
                        i,
                        buf2,
                        ct->c[ct_list][i].c_opsinitiated,
                        ct->c[ct_list][i].c_opscompleted,
                        ct->c[ct_list][i].c_gettingber ? "r" : "-",
                        "",
                        ct->c[ct_list][i].c_dn ? ct->c[ct_list][i].c_dn : "NULLDN",
                        maxthreadbuf,
                        ct->c[ct_list][i].c_connid,
                        ct->c[ct_list][i].c_ipaddr);
                val.bv_val = bufptr;
                val.bv_len = strlen(bufptr);
                attrlist_merge(&e->e_attrs, "connection", vals);
                slapi_ch_free_string(&newbuf);
            }
            pthread_mutex_unlock(&(ct->c[ct_list][i].c_mutex));
        }
    }

    snprintf(buf, sizeof(buf), "%d", nconns);
    val.bv_val = buf;
    val.bv_len = strlen(buf);
    attrlist_replace(&e->e_attrs, "currentconnections", vals);

    snprintf(buf, sizeof(buf), "%" PRIu64, slapi_counter_get_value(num_conns));
    val.bv_val = buf;
    val.bv_len = strlen(buf);
    attrlist_replace(&e->e_attrs, "totalconnections", vals);

    snprintf(buf, sizeof(buf), "%" PRIu64, slapi_counter_get_value(conns_in_maxthreads));
    val.bv_val = buf;
    val.bv_len = strlen(buf);
    attrlist_replace(&e->e_attrs, "currentconnectionsatmaxthreads", vals);

    snprintf(buf, sizeof(buf), "%" PRIu64, slapi_counter_get_value(max_threads_count));
    val.bv_val = buf;
    val.bv_len = strlen(buf);
    attrlist_replace(&e->e_attrs, "maxthreadsperconnhits", vals);

    snprintf(buf, sizeof(buf), "%d", (ct != NULL ? ct->size : 0));
    val.bv_val = buf;
    val.bv_len = strlen(buf);
    attrlist_replace(&e->e_attrs, "dtablesize", vals);

    snprintf(buf, sizeof(buf), "%d", nreadwaiters);
    val.bv_val = buf;
    val.bv_len = strlen(buf);
    attrlist_replace(&e->e_attrs, "readwaiters", vals);
}

void
connection_table_dump_activity_to_errors_log(Connection_Table *ct)
{
    size_t i;
    size_t l;
    for (l = 0; l < ct->list_num; l++) {
        for (i = 0; i < ct->list_size; i++) {
            Connection *c = &(ct->c[l][i]);
            if (c->c_state) {
                /* Find the connection we are referring to */
                int ct_list = c->c_fdi;
                pthread_mutex_lock(&(c->c_mutex));
                if ((c->c_sd != SLAPD_INVALID_SOCKET) &&
                    (ct_list >= 0) && (c->c_prfd == ct->fd[l][ct_list].fd)) {
                    int r = ct->fd[l][ct_list].out_flags & SLAPD_POLL_FLAGS;
                    if (r) {
                        slapi_log_err(SLAPI_LOG_CONNS, "connection_table_dump_activity_to_errors_log",
                                    "activity on %zu%s\n", i, r ? "r" : "");
                    }
                }
                pthread_mutex_unlock(&(c->c_mutex));
            }
        }
    }
}

#if 0
void dump_op_list(FILE *file, Operation *op);

void
connection_table_dump(Connection_Table *ct)
{
    FILE *file;

    file = fopen("/tmp/slapd.conn", "a+");
    if (file != NULL)
    {
        int    i;
        fprintf(file, "=============pid=%d==================\n", getpid());
        for ( i = 0; i < ct->size; i++ )
        {
            if ( (ct->c[i].c_sd == SLAPD_INVALID_SOCKET) && (ct->c[i].c_connid == 0) )
            {
                continue;
            }
            fprintf(file, "c[%d].c_dn=0x%x\n", i, ct->c[i].c_dn);
            dump_op_list(file, ct->c[i].c_ops);
            fprintf(file, "c[%d].c_sb.sb_sd=%d\n", i, ct->c[i].c_sd);
            fprintf(file, "c[%d].c_connid=%d\n", i, ct->c[i].c_connid);
            fprintf(file, "c[%d].c_opsinitiated=%d\n", i, ct->c[i].c_opsinitiated);
            fprintf(file, "c[%d].c_opscompleted=%d\n", i, ct->c[i].c_opscompleted);
        }
        fclose(file);
    }
}

static const char *
op_status2str( int o_status )
{
    const char *s = "unknown";

    switch( o_status ) {
    case SLAPI_OP_STATUS_PROCESSING:
        s = "processing";
        break;
    case SLAPI_OP_STATUS_ABANDONED:
        s = "abandoned";
        break;
    case SLAPI_OP_STATUS_WILL_COMPLETE:
        s = "will_complete";
        break;
    case SLAPI_OP_STATUS_RESULT_SENT:
        s = "result_sent";
        break;
    }

    return s;
}

void
dump_op_list(FILE *file, Operation *op)
{
    Operation       *tmp;

        for ( tmp = op; tmp != NULL; tmp = tmp->o_next )
    {
        fprintf(file,
         "(o_msgid=%d, o_tag=%d, o_sdn=0x%x, o_opid=%d, o_connid=%d, o_status=%s)\n",
        tmp->o_msgid, tmp->o_tag, slapi_sdn_get_dn(&tmp->o_sdn), tmp->o_connid,
        *tmp->o_tid, op_status2str( tmp->o_status ));
    }
}
#endif /* 0 */

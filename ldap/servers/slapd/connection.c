
    *lenp = 0;
    if (NULL == lber) {
        return LBER_DEFAULT;
    }
    *lenp = lber->ber_len;
    return lber->ber_tag;
}

/*
 * Utility function called by  connection_read_operation(). This is a
 * small wrapper on top of libldap's ber_get_next_buffer_ext().
 *
 * Caller must hold conn->c_mutex
 *
 * Return value:
 *   0: Success
 *      case 1) If there was not enough data in the buffer to complete the
 *      message, go to the next cycle. In this case, bytes_scanned is set
 *      to a positive number and *tagp is set to LBER_DEFAULT.
 *      case 2) Complete.  *tagp == (tag of the message) and bytes_scanned is
 *      set to a positive number.
 *  -1: Failure
 *      case 1) *tagp == LBER_OVERFLOW: the length is either bigger than
 *      ber_uint_t type or the value preset via
 *      LBER_SOCKBUF_OPT_MAX_INCOMING_SIZE option
 *      case 2) *tagp == LBER_DEFAULT: memory error or tag mismatch
 */
static int
get_next_from_buffer(void *buffer __attribute__((unused)), size_t buffer_size __attribute__((unused)), ber_len_t *lenp, ber_tag_t *tagp, BerElement *ber, Connection *conn)
{
    PRErrorCode err = 0;
    PRInt32 syserr = 0;
    ber_len_t bytes_scanned = 0;

    *lenp = 0;
    *tagp = ber_get_next(conn->c_sb, &bytes_scanned, ber);
    /* openldap ber_get_next doesn't return partial bytes_scanned if it hasn't
       read a whole pdu - so we have to check the errno for the
       "would block" condition meaning openldap needs more data to read */
    if ((LBER_OVERFLOW == *tagp || LBER_DEFAULT == *tagp) && 0 == bytes_scanned &&
        !SLAPD_SYSTEM_WOULD_BLOCK_ERROR(errno)) {
        if ((LBER_OVERFLOW == *tagp) || (errno == ERANGE)) {
            ber_len_t maxbersize = conn->c_maxbersize;
            ber_len_t tmplen = 0;
            (void)_ber_get_len(ber, &tmplen);
            /* openldap does not differentiate between length == 0
               and length > max - all we know is that there was a
               problem with the length - assume too big */
            err = SLAPD_DISCONNECT_BER_TOO_BIG;
            log_ber_too_big_error(conn, tmplen, maxbersize);
        } else {
            err = SLAPD_DISCONNECT_BAD_BER_TAG;
        }
        syserr = errno;
        /* Bad stuff happened, like the client sent us some junk */
        slapi_log_err(SLAPI_LOG_CONNS, "get_next_from_buffer",
                      "ber_get_next failed for connection %" PRIu64 "\n", conn->c_connid);
        /* reset private buffer */
        conn->c_private->c_buffer_bytes = conn->c_private->c_buffer_offset = 0;

        /* drop connection */
        disconnect_server_nomutex(conn, conn->c_connid, -1, err, syserr);
        return -1;
    } else if (CONNECTION_BUFFER_OFF == conn->c_private->use_buffer) {
        *lenp = bytes_scanned;
        if ((LBER_OVERFLOW == *tagp || LBER_DEFAULT == *tagp) && 0 == bytes_scanned &&
            SLAPD_SYSTEM_WOULD_BLOCK_ERROR(errno)) {
            return -2; /* tells connection_read_operation we need to try again */
        }
    } /* else, openldap_read_function will advance c_buffer_offset,
         nothing to do (we had to previously with mozldap) */
    return 0;
}

/* Either read read data into the connection buffer, or fail with err set */
static int
connection_read_ldap_data(Connection *conn, PRInt32 *err)
{
    int ret = 0;
    ret = PR_Recv(conn->c_prfd, conn->c_private->c_buffer, conn->c_private->c_buffer_size, 0, PR_INTERVAL_NO_WAIT);
    if (ret < 0) {
        *err = PR_GetError();
    } else if (CONNECTION_BUFFER_ADAPT == conn->c_private->use_buffer) {
        if ((ret == conn->c_private->c_buffer_size) && (conn->c_private->c_buffer_size < BUFSIZ)) {
            /* we read exactly what we requested - there could be more that we could have read */
            /* so increase the buffer size */
            conn->c_private->c_buffer_size *= 2;
            if (conn->c_private->c_buffer_size > BUFSIZ) {
                conn->c_private->c_buffer_size = BUFSIZ;
            }
            conn->c_private->c_buffer = slapi_ch_realloc(conn->c_private->c_buffer, conn->c_private->c_buffer_size);
        }
    }
    return ret;
}

static size_t
conn_buffered_data_avail_nolock(Connection *conn, int *conn_closed)
{
    if ((conn->c_sd == SLAPD_INVALID_SOCKET) || (conn->c_flags & CONN_FLAG_CLOSING)) {
        /* connection is closed - ignore the buffer */
        *conn_closed = 1;
        return 0;
    } else {
        *conn_closed = 0;
        return conn->c_private->c_buffer_bytes - conn->c_private->c_buffer_offset;
    }
}

/* Function to convert a PRNetAddr to a normalized IPv4 string and keep original address string. */
static void
normalize_IPv4(const PRNetAddr *addr, char *normalizedAddr, size_t normalizedAddrSize, char *originalAddr, size_t originalAddrSize)
{
    /* Keep the original address string */
    PR_NetAddrToString(addr, originalAddr, originalAddrSize);

    if (PR_IsNetAddrType(addr, PR_IpAddrV4Mapped)) {
        /* Handle IPv4-mapped-to-IPv6 */
        PRNetAddr v4addr;
        /* v4addr gets the lower 32 bits of addr6 (the IPv4 address in the IPv6 format) */
        v4addr.inet.family = PR_AF_INET;
        v4addr.inet.ip = addr->ipv6.ip.pr_s6_addr32[3];
        PR_NetAddrToString(&v4addr, normalizedAddr, normalizedAddrSize);
    } else {
        /* If it's not an IPv4-mapped IPv6 address, just keep the original format */
        strncpy(normalizedAddr, originalAddr, normalizedAddrSize - 1);
        normalizedAddr[normalizedAddrSize - 1] = '\0';
    }
    originalAddr[originalAddrSize - 1] = '\0';
}

/* Upon returning from this function, we have either:
   1. Read a PDU successfully.
   2. Detected some error condition with the connection which requires closing it.
   3. In Turbo mode, we Timed out without seeing any data.

   We also handle the case where we read ahead beyond the current PDU
   by buffering the data and setting the 'remaining_data' flag.

 */
int
connection_read_operation(Connection *conn, Operation *op, ber_tag_t *tag, int *remaining_data)
{
    ber_len_t len = 0;
    int ret = 0;
    int haproxy_rc = 0;
    int32_t waits_done = 0;
    ber_int_t msgid;
    int new_operation = 1; /* Are we doing the first I/O read for a new operation ? */
    PRErrorCode err = 0;
    PRInt32 syserr = 0;
    size_t buffer_data_avail;
    int conn_closed = 0;
    PRNetAddr pr_netaddr_from = {0};
    PRNetAddr pr_netaddr_dest = {0};
    char buf_ip[INET6_ADDRSTRLEN + 1] = {0};
    char buf_haproxy_destip[INET6_ADDRSTRLEN + 1] = {0};
    char str_ip[INET6_ADDRSTRLEN + 1] = {0};
    char str_haproxy_ip[INET6_ADDRSTRLEN + 1] = {0};
    char str_haproxy_destip[INET6_ADDRSTRLEN + 1] = {0};
    int trusted_matches_ip_found = 0;
    int trusted_matches_destip_found = 0;
    struct berval **bvals = NULL;
    int proxy_connection = 0;

    pthread_mutex_lock(&(conn->c_mutex));
    /*
     * if the socket is still valid, get the ber element
     * waiting for us on this connection. timeout is handled
     * in the low-level read_function.
     */
    if ((conn->c_sd == SLAPD_INVALID_SOCKET) ||
        (conn->c_flags & CONN_FLAG_CLOSING)) {
        ret = CONN_DONE;
        goto done;
    }

    *tag = LBER_DEFAULT;
    /* First check to see if we have buffered data from "before" */
    if ((buffer_data_avail = conn_buffered_data_avail_nolock(conn, &conn_closed))) {
        /* If so, use that data first */
        if (0 != get_next_from_buffer(conn->c_private->c_buffer + conn->c_private->c_buffer_offset,
                                      buffer_data_avail,
                                      &len, tag, op->o_ber, conn)) {
            ret = CONN_DONE;
            goto done;
        }
        new_operation = 0;
    }

    /* If we still haven't seen a complete PDU, read from the network */
    while (*tag == LBER_DEFAULT) {
        int32_t ioblocktimeout_waits = conn->c_ioblocktimeout / CONN_TURBO_TIMEOUT_INTERVAL;
        /* We should never get here with data remaining in the buffer */
        PR_ASSERT(!new_operation || !conn_buffered_data_avail_nolock(conn, &conn_closed));
        /* We make a non-blocking read call */
        if (CONNECTION_BUFFER_OFF != conn->c_private->use_buffer) {
            /* Process HAProxy header */
            if (conn->c_haproxyheader_read == 0) {
                conn->c_haproxyheader_read = 1;
                /*
                * We only check for HAProxy header if nsslapd-haproxy-trusted-ip is configured.
                * If it is we proceed with the connection only if it's comming from trusted
                * proxy server with correct and complete header.
                */
                if  ((bvals = g_get_haproxy_trusted_ip()) != NULL) {
                    /* Can we have an unknown address at that point? */
                    if ((haproxy_rc = haproxy_receive(conn->c_sd, &proxy_connection, &pr_netaddr_from, &pr_netaddr_dest)) == HAPROXY_ERROR) {
                        slapi_log_err(SLAPI_LOG_CONNS, "connection_read_operation", "Error reading HAProxy header.\n");
                        disconnect_server_nomutex(conn, conn->c_connid, -1, SLAPD_DISCONNECT_PROXY_INVALID_HEADER, EPROTO);
                        ret = CONN_DONE;
                        goto done;
                    }

                    /* If bval is NULL and we don't have an error - we still want the proper log and update */
                    if ((haproxy_rc == HAPROXY_HEADER_PARSED) && (proxy_connection)) {
                        /* Normalize IP addresses for logging */
                        normalize_IPv4(conn->cin_addr, buf_ip, sizeof(buf_ip), str_ip, sizeof(str_ip));
                        normalize_IPv4(&pr_netaddr_dest, buf_haproxy_destip, sizeof(buf_haproxy_destip),
                                       str_haproxy_destip, sizeof(str_haproxy_destip));

                        /* Initialize match result - will be set to 0 if both IPs match */
                        haproxy_rc = -1;

                        /*
                         * Validate connection against trusted IPs/subnets.
                         *
                         * Both the HAProxy client IP and the destination IP from the HAProxy header
                         * must match one of the configured trusted IP addresses or subnets.
                         * This allows configurations where DS instance and HAProxy are on the same machine.
                         *
                         * Uses parsed binary entries for efficient matching during connection handling.
                         */
                        haproxy_trusted_entry_t *parsed_entries = NULL;
                        size_t parsed_count = 0;

                        parsed_entries = g_get_haproxy_trusted_ip_parsed(&parsed_count);

                        if (parsed_entries && parsed_count > 0) {
                            /* Use parsed binary entries for matching */
                            trusted_matches_ip_found = haproxy_ip_matches_parsed(conn->cin_addr,
                                                                                 parsed_entries,
                                                                                 parsed_count);
                            trusted_matches_destip_found = haproxy_ip_matches_parsed(&pr_netaddr_dest,
                                                                                     parsed_entries,
                                                                                     parsed_count);
                        } else {
                            /* Parsed entries should always be available after config load.
                             * If they're not, this indicates a configuration initialization failure. */
                            slapi_log_err(SLAPI_LOG_ERR, "connection_read_operation",
                                          "HAProxy trusted IPs not properly initialized - disconnecting connection\n");
                            disconnect_server_nomutex(conn, conn->c_connid, -1,
                                                      SLAPD_DISCONNECT_PROXY_UNKNOWN, EPROTO);
                            ret = CONN_DONE;
                            goto done;
                        }

                        if (trusted_matches_ip_found && trusted_matches_destip_found) {
                            haproxy_rc = 0;
                        }

                        if (haproxy_rc == -1) {
                            slapi_log_err(SLAPI_LOG_CONNS, "connection_read_operation", "HAProxy header received from unknown source.\n");
                            disconnect_server_nomutex(conn, conn->c_connid, -1, SLAPD_DISCONNECT_PROXY_UNKNOWN, EPROTO);
                            ret = CONN_DONE;
                            goto done;
                        }
                        /* Get the HAProxy header client IP address */
                        PR_NetAddrToString(&pr_netaddr_from, str_haproxy_ip, sizeof(str_haproxy_ip));

                        /* Replace cin_addr and cin_destaddr in the Connection struct with received addresses */
                        slapi_ch_free((void**)&conn->cin_addr);
                        slapi_ch_free((void**)&conn->cin_destaddr);
                        conn->cin_addr = (PRNetAddr*)slapi_ch_malloc(sizeof(PRNetAddr));
                        conn->cin_destaddr = (PRNetAddr*)slapi_ch_malloc(sizeof(PRNetAddr));
                        memcpy(conn->cin_addr, &pr_netaddr_from, sizeof(PRNetAddr));
                        memcpy(conn->cin_destaddr, &pr_netaddr_dest, sizeof(PRNetAddr));
                        conn->c_ipaddr = slapi_ch_strdup(str_haproxy_ip);
                        conn->c_serveripaddr = slapi_ch_strdup(str_haproxy_destip);
                        slapi_log_access(LDAP_DEBUG_STATS,
                                         "conn=%" PRIu64 " fd=%d HAProxy new_address_from=%s to new_address_dest=%s\n",
                                         conn->c_connid, conn->c_sd, str_haproxy_ip, str_haproxy_destip);
                        slapi_log_security_tcp(conn, SECURITY_HAPROXY_SUCCESS, 0, "");
                    }
                }
            }
            ret = connection_read_ldap_data(conn, &err);
        } else {
            ret = get_next_from_buffer(NULL, 0, &len, tag, op->o_ber, conn);
            if (ret == -1) {
                ret = CONN_DONE;
                goto done; /* get_next_from_buffer does the disconnect stuff */
            } else if (ret == 0) {
                ret = len;
            }
            *remaining_data = 0;
        }
        if (ret <= 0) {
            if (0 == ret) {
                /* Connection is closed */
                disconnect_server_nomutex(conn, conn->c_connid, -1, SLAPD_DISCONNECT_BAD_BER_TAG, 0);
                conn->c_gettingber = 0;
                signal_listner(conn->c_ct_list);
                ret = CONN_DONE;
                goto done;
            }
            /* err = PR_GetError(); */
            /* If we would block, we need to poll for a while */
            syserr = PR_GetOSError();
            if (SLAPD_PR_WOULD_BLOCK_ERROR(err) ||
                SLAPD_SYSTEM_WOULD_BLOCK_ERROR(syserr)) {
                struct PRPollDesc pr_pd;
                PRIntervalTime timeout = PR_MillisecondsToInterval(CONN_TURBO_TIMEOUT_INTERVAL);
                pr_pd.fd = (PRFileDesc *)conn->c_prfd;
                pr_pd.in_flags = PR_POLL_READ;
                pr_pd.out_flags = 0;
                /*
                 * To avoid deadlocks and allow other worker threads to flush
                 * results for this connection, we must yield c_mutex while
                 * waiting for I/O.
                 *
                 * Lock Hierarchy: c_mutex -> c_pdumutex
                 *
                 * We acquire c_pdumutex while holding c_mutex to ensure no other
                 * thread begins a read/write operation during the transition.
                 * We then release c_mutex to allow other threads to access
                 * connection metadata (e.g., for logging or result flushing).
                 */
                PR_Lock(conn->c_pdumutex);
                pthread_mutex_unlock(&(conn->c_mutex));

                /* Admit a pending result writer before polling again. */
                if (slapi_atomic_load_32(&conn->c_pdu_writers_waiting, __ATOMIC_ACQUIRE) > 0) {
                    conn->c_pdu_handoff_pending = PR_TRUE;
                    while (conn->c_pdu_handoff_pending) {
                        if (PR_WaitCondVar(conn->c_pdu_writer_cv, PR_INTERVAL_NO_TIMEOUT) != PR_SUCCESS) {
                            err = PR_GetError();
                            syserr = PR_GetOSError();
                            conn->c_pdu_handoff_pending = PR_FALSE;
                            PR_Unlock(conn->c_pdumutex);
                            pthread_mutex_lock(&(conn->c_mutex));
                            slapi_log_err(SLAPI_LOG_ERR, "connection_read_operation",
                                          "PDU writer wait for connection %" PRIu64 " failed: %d (%s)\n",
                                          conn->c_connid, err, slapd_pr_strerror(err));
                            disconnect_server_nomutex(conn, conn->c_connid, -1, err, syserr);
                            ret = CONN_DONE;
                            goto done;
                        }
                    }
                    /* Recheck the connection after the writer has run. */
                    PR_Unlock(conn->c_pdumutex);
                    pthread_mutex_lock(&(conn->c_mutex));
                    if ((conn->c_sd == SLAPD_INVALID_SOCKET) ||
                        (conn->c_flags & CONN_FLAG_CLOSING)) {
                        ret = CONN_DONE;
                        goto done;
                    }
                    pr_pd.fd = (PRFileDesc *)conn->c_prfd;
                    PR_Lock(conn->c_pdumutex);
                    pthread_mutex_unlock(&(conn->c_mutex));
                }

                ret = PR_Poll(&pr_pd, 1, timeout);

                /*
                 * DEADLOCK AVOIDANCE: We MUST release c_pdumutex BEFORE
                 * re-acquiring c_mutex.
                 *
                 * If we held c_pdumutex while waiting for c_mutex, we could
                 * deadlock with a thread that holds c_mutex and is waiting
                 * for c_pdumutex (e.g., in flush_ber() called from a SASL bind).
                 */
                PR_Unlock(conn->c_pdumutex);
                pthread_mutex_lock(&(conn->c_mutex));

                /* check if the connection was closed while we were waiting */
                if ((conn->c_sd == SLAPD_INVALID_SOCKET) ||
                    (conn->c_flags & CONN_FLAG_CLOSING)) {
                    ret = CONN_DONE;
                    goto done;
                }
                waits_done++;
                /* Did we time out ? */
                if (0 == ret) {
                    /* We timed out, should the server shutdown ? */
                    if (op_shutdown) {
                        ret = CONN_SHUTDOWN;
                        goto done;
                    }
                    /* We timed out, is this the first read in a PDU ? */
                    if (new_operation) {
                        /* If so, we return */
                        ret = CONN_TIMEDOUT;
                        goto done;
                    } else {
                        /* Otherwise we loop, unless we exceeded the ioblock timeout */
                        if (waits_done > ioblocktimeout_waits) {
                            slapi_log_err(SLAPI_LOG_CONNS, "connection_read_operation",
                                          "ioblocktimeout expired on connection %" PRIu64 "\n", conn->c_connid);
                            disconnect_server_nomutex(conn, conn->c_connid, -1,
                                                      SLAPD_DISCONNECT_IO_TIMEOUT, 0);
                            ret = CONN_DONE;
                            goto done;
                        } else {

                            /* The turbo mode may cause threads starvation.
                                  Do a yield here to reduce the starving.
                            */
                            PR_Sleep(PR_INTERVAL_NO_WAIT);

                            continue;
                        }
                    }
                }
                if (-1 == ret) {
                    /* PR_Poll call failed */
                    err = PR_GetError();
                    syserr = PR_GetOSError();
                    slapi_log_err(SLAPI_LOG_ERR, "connection_read_operation",
                                  "PR_Poll for connection %" PRIu64 " returns %d (%s)\n", conn->c_connid, err, slapd_pr_strerror(err));
                    /* If this happens we should close the connection */
                    disconnect_server_nomutex(conn, conn->c_connid, -1, err, syserr);
                    ret = CONN_DONE;
                    goto done;
                }
                slapi_log_err(SLAPI_LOG_CONNS,
                              "connection_read_operation", "connection %" PRIu64 " waited %d times for read to be ready\n", conn->c_connid, waits_done);
            } else {
                /* Some other error, typically meaning bad stuff */
                syserr = PR_GetOSError();
                slapi_log_err(SLAPI_LOG_CONNS, "connection_read_operation",
                              "PR_Recv for connection %" PRIu64 " returns %d (%s)\n", conn->c_connid, err, slapd_pr_strerror(err));
                /* If this happens we should close the connection */
                disconnect_server_nomutex(conn, conn->c_connid, -1, err, syserr);
                ret = CONN_DONE;
                goto done;
            }
        } else {
            /* We read some data off the network, do something with it */
            if (CONNECTION_BUFFER_OFF != conn->c_private->use_buffer) {
                conn->c_private->c_buffer_bytes = ret;
                conn->c_private->c_buffer_offset = 0;

                if (get_next_from_buffer(conn->c_private->c_buffer,
                                         conn->c_private->c_buffer_bytes - conn->c_private->c_buffer_offset,
                                         &len, tag, op->o_ber, conn) != 0) {
                    ret = CONN_DONE;
                    goto done;
                }
            }
            slapi_log_err(SLAPI_LOG_CONNS,
                          "connection_read_operation", "connection %" PRIu64 " read %d bytes\n", conn->c_connid, ret);

            new_operation = 0;
            ret = CONN_FOUND_WORK_TO_DO;
            waits_done = 0; /* got some data: reset counter */
        }
    }
    /* If there is remaining buffered data, set the flag to tell the caller. */
    if (conn_buffered_data_avail_nolock(conn, &conn_closed)) {
        *remaining_data = 1;
    } else if (conn_closed) {
        /* connection closed */
        ret = CONN_DONE;
        goto done;
    }

    if (*tag != LDAP_TAG_MESSAGE) {
        /*
         * We received a non-LDAP message.  Log and close connection.
         */
        slapi_log_err(SLAPI_LOG_ERR,
                      "connection_read_operation", "conn=%" PRIu64 " received a non-LDAP message (tag 0x%lx, expected 0x%lx)\n",
                      conn->c_connid, *tag, LDAP_TAG_MESSAGE);
        disconnect_server_nomutex(conn, conn->c_connid, -1,
                                  SLAPD_DISCONNECT_BAD_BER_TAG, EPROTO);
        ret = CONN_DONE;
        goto done;
    }

    if ((*tag = ber_get_int(op->o_ber, &msgid)) != LDAP_TAG_MSGID) {
        /* log, close and send error */
        slapi_log_err(SLAPI_LOG_ERR,
                      "connection_read_operation", "conn=%" PRIu64 " unable to read tag for incoming request\n", conn->c_connid);
        disconnect_server_nomutex(conn, conn->c_connid, -1, SLAPD_DISCONNECT_BAD_BER_TAG, EPROTO);
        ret = CONN_DONE;
        goto done;
    }
    if (is_ber_too_big(conn, len)) {
        disconnect_server_nomutex(conn, conn->c_connid, -1, SLAPD_DISCONNECT_BER_TOO_BIG, 0);
        ret = CONN_DONE;
        goto done;
    }
    op->o_msgid = msgid;

    *tag = ber_peek_tag(op->o_ber, &len);
    switch (*tag) {
    case LBER_ERROR:
    case LDAP_TAG_LDAPDN: /* optional username, for CLDAP */
        /* log, close and send error */
        slapi_log_err(SLAPI_LOG_ERR,
                      "connection_read_operation", "conn=%" PRIu64 " ber_peek_tag returns 0x%lx\n", conn->c_connid, *tag);
        disconnect_server_nomutex(conn, conn->c_connid, -1, SLAPD_DISCONNECT_BER_PEEK, EPROTO);
        ret = CONN_DONE;
        goto done;
    default:
        break;
    }
    op->o_tag = *tag;
done:
    pthread_mutex_unlock(&(conn->c_mutex));
    return ret;
}

void
connection_make_readable(Connection *conn)
{
    pthread_mutex_lock(&(conn->c_mutex));
    conn->c_gettingber = 0;
    pthread_mutex_unlock(&(conn->c_mutex));
    signal_listner(conn->c_ct_list);
}

void
connection_make_readable_nolock(Connection *conn)
{
    conn->c_gettingber = 0;
    slapi_log_err(SLAPI_LOG_CONNS, "connection_make_readable_nolock", "making readable conn %" PRIu64 " fd=%d\n",
                  conn->c_connid, conn->c_sd);
}

/*
 * Figure out the operation completion rate for this connection
 */
void
connection_check_activity_level(Connection *conn)
{
    int current_count = 0;
    int delta_count = 0;
    pthread_mutex_lock(&(conn->c_mutex));
    /* get the current op count */
    current_count = conn->c_opscompleted;
    /* compare to the previous op count */
    delta_count = current_count - conn->c_private->previous_op_count;
    /* delta is the rate, store that */
    conn->c_private->operation_rate = delta_count;
    /* store current count in the previous count slot */
    conn->c_private->previous_op_count = current_count;
    /* update the last checked time */
    conn->c_private->previous_count_check_time = slapi_current_rel_time_t();
    pthread_mutex_unlock(&(conn->c_mutex));
    slapi_log_err(SLAPI_LOG_CONNS, "connection_check_activity_level", "conn %" PRIu64 " activity level = %d\n", conn->c_connid, delta_count);
}

typedef struct table_iterate_info_struct
{
    int connection_count;
    int rank_count;
    int our_rate;
} table_iterate_info;

int
table_iterate_function(Connection *conn, void *arg)
{
    int ret = 0;
    table_iterate_info *pinfo = (table_iterate_info *)arg;
    pinfo->connection_count++;
    if (conn->c_private->operation_rate > pinfo->our_rate) {
        pinfo->rank_count++;
    }
    return ret;
}

/*
 * Scan the list of active connections, evaluate our relative rank
 * for connection activity.
 */
void
connection_find_our_rank(Connection *conn, int *connection_count, int *our_rank)
{
    table_iterate_info info = {0};
    info.our_rate = conn->c_private->operation_rate;
    connection_table_iterate_active_connections(the_connection_table, &info, &table_iterate_function);
    *connection_count = info.connection_count;
    *our_rank = info.rank_count;
}

/*
 * Evaluate the turbo policy for this connection
 */
void
connection_enter_leave_turbo(Connection *conn, int current_turbo_flag, int *new_turbo_flag)
{
    int current_mode = 0;
    int new_mode = 0;
    int connection_count = 0;
    int our_rank = 0;
    int threshold_rank = 0;
    pthread_mutex_lock(&(conn->c_mutex));
    /* We can already be in turbo mode, or not */
    current_mode = current_turbo_flag;
    if (pagedresults_in_use_nolock(conn)) {
        /* PAGED_RESULTS does not need turbo mode */
        new_mode = 0;
    } else if (conn->c_private->operation_rate == 0) {
        /* The connection is ranked by the passed activities. If some other
         * connection have more activity, increase rank by one. The highest
         * rank is least activity, good candidates to move out of turbo mode.
         * However, if no activity on all the connections, then every
         * connection gets 0 rank, so none move out.
         * No bother to do so much calcuation, short-cut to non-turbo mode
         * if no activities in passed interval */
        new_mode = 0;
    } else {
        double activet = 0.0;
        connection_find_our_rank(conn, &connection_count, &our_rank);
        slapi_log_err(SLAPI_LOG_CONNS, "connection_enter_leave_turbo",
                      "conn %" PRIu64 " turbo rank = %d out of %d conns\n", conn->c_connid, our_rank, connection_count);
        activet = (double)g_get_active_threadcnt();
        threshold_rank = (int)(activet * ((double)CONN_TURBO_PERCENTILE / 100.0));

        /* adjust threshold_rank according number of connections,
         less turbo threads as more connections,
         one measure to reduce thread startvation.
       */
        if (connection_count > threshold_rank) {
            threshold_rank -= (connection_count - threshold_rank) / 5;
        }

        if (current_mode && (our_rank - CONN_TURBO_HYSTERESIS) < threshold_rank) {
            /* We're currently in turbo mode */
            /* Policy says that we stay in turbo mode provided
               connection activity is still high.
             */
            new_mode = 1;
        } else if (!current_mode && (our_rank + CONN_TURBO_HYSTERESIS) < threshold_rank) {
            /* We're currently not in turbo mode */
            /* Policy says that we go into turbo mode if
               recent connection activity is high.
             */
            new_mode = 1;
        }
    }
    pthread_mutex_unlock(&(conn->c_mutex));
    if (current_mode != new_mode) {
        if (current_mode) {
            slapi_log_err(SLAPI_LOG_CONNS, "connection_enter_leave_turbo", "conn %" PRIu64 " leaving turbo mode\n", conn->c_connid);
        } else {
            slapi_log_err(SLAPI_LOG_CONNS, "connection_enter_leave_turbo", "conn %" PRIu64 " entering turbo mode\n", conn->c_connid);
        }
    }
    *new_turbo_flag = new_mode;
}

static void
connection_threadmain(void *arg)
{
    Slapi_PBlock *pb = slapi_pblock_new();
    int32_t *snmp_vars_idx = (int32_t *) arg;
    char tname[16];
    snprintf(tname, sizeof(tname), "worker-%d", *snmp_vars_idx);
    slapi_set_thread_name(tname);
    /* wait forever for new pb until one is available or shutdown */
    int32_t interval = 0; /* used be  10 seconds */
    Connection *conn = NULL;
    Operation *op;
    ber_tag_t tag = 0;
    int thread_turbo_flag = 0;
    int ret = 0;
    int more_data = 0;
    int replication_connection = 0; /* If this connection is from a replication supplier, we want to ensure that operation processing is serialized */
    int doshutdown = 0;
    int maxthreads = 0;
    long bypasspollcnt = 0;
    bool is_busy = false;

#if defined(hpux)
    /* Arrange to ignore SIGPIPE signals. */
    SIGNAL(SIGPIPE, SIG_IGN);
#endif
    thread_private_snmp_vars_set_idx(*snmp_vars_idx);

    while (1) {
        int is_timedout = 0;
        time_t curtime = 0;
        bool read_handed_off = false;
        bool starttls_operation = false;

        if (op_shutdown) {
            slapi_log_err(SLAPI_LOG_TRACE, "connection_threadmain",
                          "op_thread received shutdown signal\n");
            if (is_busy) {
                slapi_atomic_decr_32(&current_busy_workers, __ATOMIC_ACQ_REL);
            }
            slapi_pblock_destroy(pb);
            g_decr_active_threadcnt();
            return;
        }

        if (!thread_turbo_flag && !more_data) {
	        Connection *pb_conn = NULL;

            /* Mark this worker as idle before blocking on the work queue */
            if (is_busy) {
                is_busy = false;
                slapi_atomic_decr_32(&current_busy_workers, __ATOMIC_ACQ_REL);
            }

            /* If more data is left from the previous connection_read_operation,
               we should finish the op now.  Client might be thinking it's
               done sending the request and wait for the response forever.
               [blackflag 624234] */
            ret = connection_wait_for_new_work(pb, interval);

            switch (ret) {
            case CONN_NOWORK:
                PR_ASSERT(interval != 0); /* this should never happen */
                continue;
            case CONN_SHUTDOWN:
                slapi_log_err(SLAPI_LOG_TRACE, "connection_threadmain",
                              "op_thread received shutdown signal\n");
                if (is_busy) {
                    slapi_atomic_decr_32(&current_busy_workers, __ATOMIC_ACQ_REL);
                }
                slapi_pblock_destroy(pb);
                g_decr_active_threadcnt();
                return;
            case CONN_FOUND_WORK_TO_DO:
                /* note - don't need to lock here - connection should only
                   be used by this thread - since c_gettingber is set to 1
                   in connection_activity when the conn is added to the
                   work queue, setup_pr_read_pds won't add the connection prfd
                   to the poll list */
                slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
                if (pb_conn == NULL) {
                    slapi_log_err(SLAPI_LOG_ERR, "connection_threadmain", "pb_conn is NULL\n");
                    if (is_busy) {
                        slapi_atomic_decr_32(&current_busy_workers, __ATOMIC_ACQ_REL);
                    }
                    slapi_pblock_destroy(pb);
                    g_decr_active_threadcnt();
                    return;
                }

                pthread_mutex_lock(&(pb_conn->c_mutex));
                if (pb_conn->c_anonlimits_set == 0) {
                    /*
                     * We have a new connection, set the anonymous reslimit idletimeout
                     * if applicable.
                     */
                    char *anon_dn = config_get_anon_limits_dn();
                    int idletimeout;
                    /* If an anonymous limits dn is set, use it to set the limits. */
                    if (anon_dn && (strlen(anon_dn) > 0)) {
                        Slapi_DN *anon_sdn = slapi_sdn_new_normdn_byref(anon_dn);
                        reslimit_update_from_dn(pb_conn, anon_sdn);
                        slapi_sdn_free(&anon_sdn);
                        if (slapi_reslimit_get_integer_limit(pb_conn,
                                                             pb_conn->c_idletimeout_handle,
                                                             &idletimeout) == SLAPI_RESLIMIT_STATUS_SUCCESS) {
                            pb_conn->c_idletimeout = idletimeout;
                        }
                    }
                    slapi_ch_free_string(&anon_dn);
                    /*
                     * Set connection as initialized to avoid setting anonymous limits
                     * multiple times on the same connection
                     */
                    pb_conn->c_anonlimits_set = 1;
                }

                /* must hold c_mutex so that it synchronizes the IO layer push
                 * with a potential pending sasl bind that is registering the IO layer
                 */
                connection_apply_io_layers_or_disconnect(pb_conn);
                pthread_mutex_unlock(&(pb_conn->c_mutex));
                break;
            default:
                break;
            }
        } else {

            /* The turbo mode may cause threads starvation.
               Do a yield here to reduce the starving
            */
            PR_Sleep(PR_INTERVAL_NO_WAIT);

            pthread_mutex_lock(&(conn->c_mutex));
            /* Make our own pb in turbo mode */
            connection_make_new_pb(pb, conn);
            connection_apply_io_layers_or_disconnect(conn);
            pthread_mutex_unlock(&(conn->c_mutex));
            if (!config_check_referral_mode()) {
                slapi_counter_increment(g_get_per_thread_snmp_vars()->server_tbl.dsOpInitiated);
                slapi_counter_increment(g_get_per_thread_snmp_vars()->ops_tbl.dsInOps);
            }
        }
        /* Once we're here we have a pb - mark this worker as busy */
        if (!is_busy) {
            is_busy = true;
            int32_t val = slapi_atomic_incr_32(&current_busy_workers, __ATOMIC_ACQ_REL);
            /*
             * Best-effort high-water mark: without a CAS primitive two threads
             * could race and briefly regress the value, but it self-corrects on
             * the next higher peak.  Acceptable for a monitoring-only metric.
             */
            while (val > slapi_atomic_load_32(&max_busy_workers, __ATOMIC_RELAXED)) {
                slapi_atomic_store_32(&max_busy_workers, val, __ATOMIC_RELAXED);
            }
        }
        slapi_pblock_get(pb, SLAPI_CONNECTION, &conn);
        slapi_pblock_get(pb, SLAPI_OPERATION, &op);
        if (conn == NULL || op == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "connection_threadmain", "NULL param: conn (0x%p) op (0x%p)\n", conn, op);
            if (is_busy) {
                slapi_atomic_decr_32(&current_busy_workers, __ATOMIC_ACQ_REL);
            }
            slapi_pblock_destroy(pb);
            g_decr_active_threadcnt();
            return;
        }
        maxthreads = conn->c_max_threads_per_conn;
        more_data = 0;
        ret = connection_read_operation(conn, op, &tag, &more_data);
        if ((ret == CONN_DONE) || (ret == CONN_TIMEDOUT)) {
            slapi_log_err(SLAPI_LOG_CONNS, "connection_threadmain",
                          "conn %" PRIu64 " read not ready due to %d - thread_turbo_flag %d more_data %d "
                          "ops_initiated %d refcnt %d flags %d\n",
                          conn->c_connid, ret, thread_turbo_flag, more_data,
                          conn->c_opsinitiated, conn->c_refcnt, conn->c_flags);
        } else if (ret == CONN_FOUND_WORK_TO_DO) {
            slapi_log_err(SLAPI_LOG_CONNS, "connection_threadmain",
                          "conn %" PRIu64 " read operation successfully - thread_turbo_flag %d more_data %d "
                          "ops_initiated %d refcnt %d flags %d\n",
                          conn->c_connid, thread_turbo_flag, more_data,
                          conn->c_opsinitiated, conn->c_refcnt, conn->c_flags);
        }

        curtime = slapi_current_rel_time_t();
#define DB_PERF_TURBO 1
#if defined(DB_PERF_TURBO)
        /* If it's been a while since we last did it ... */
        if (curtime - conn->c_private->previous_count_check_time > CONN_TURBO_CHECK_INTERVAL) {
            if (config_get_enable_turbo_mode()) {
                int new_turbo_flag = 0;
                /* Check the connection's activity level */
                connection_check_activity_level(conn);
                /* And if appropriate, change into or out of turbo mode */
                connection_enter_leave_turbo(conn, thread_turbo_flag, &new_turbo_flag);
                thread_turbo_flag = new_turbo_flag;
            } else {
                thread_turbo_flag = 0;
            }
        }

        /* turn off turbo mode immediately if any pb waiting in global queue */
        if (thread_turbo_flag && !WORK_Q_EMPTY) {
            thread_turbo_flag = 0;
            slapi_log_err(SLAPI_LOG_CONNS, "connection_threadmain",
                          "conn %" PRIu64 " leaving turbo mode - pb_q is not empty %d\n",
                          conn->c_connid, work_q_size);
        }
#endif

        switch (ret) {
        case CONN_DONE:
        /* This means that the connection was closed, so clear turbo mode */
        /*FALLTHROUGH*/
        case CONN_TIMEDOUT:
            thread_turbo_flag = 0;
            is_timedout = 1;
            /* In the case of CONN_DONE, more_data could have been set to 1
                 * in connection_read_operation before an error was encountered.
                 * In that case, we need to set more_data to 0 - even if there is
                 * more data available, we're not going to use it anyway.
                 * In the case of CONN_TIMEDOUT, it is only used in one place, and
                 * more_data will never be set to 1, so it is safe to set it to 0 here.
                 * We need more_data to be 0 so the connection will be processed
                 * correctly at the end of this function.
                 */
            more_data = 0;
            /* note:
                 * should call connection_make_readable after the op is removed
                 * connection_make_readable(conn);
                 */
            slapi_log_err(SLAPI_LOG_CONNS, "connection_threadmain",
                          "conn %" PRIu64 " leaving turbo mode due to %d\n",
                          conn->c_connid, ret);
            goto done;
        case CONN_SHUTDOWN:
            slapi_log_err(SLAPI_LOG_TRACE, "connection_threadmain",
                          "op_thread received shutdown signal\n");
            g_decr_active_threadcnt();
            doshutdown = 1;
            goto done; /* To destroy pb, jump to done once */
        default:
            break;
        }

        /* if we got here, then we had some read activity */
        if (thread_turbo_flag) {
            /* turbo mode avoids handle_pr_read_ready which avoids setting c_idlesince
               update c_idlesince here since, if we got some read activity, we are
               not idle */
            conn->c_idlesince = curtime;
        }

        /*
         * Do not put the connection back to the read ready poll list
         * if the operation is unbind.  Unbind will close the socket.
         * Similarly, if we are in turbo mode, don't send the socket
         * back to the poll set.
         * more_data: [blackflag 624234]
         * If the connection is from a replication supplier, don't make it readable here.
         * We want to ensure that replication operations are processed strictly in the order
         * they are received off the wire.
         */
        replication_connection = conn->c_isreplication_session;

        /*
         * For StartTLS, we exit turbo mode and do not make the connection
         * readable until start_tls() has finished.
         */
        if (connection_is_start_tls(op)) {
            starttls_operation = true;
            thread_turbo_flag = 0;
            slapi_log_err(SLAPI_LOG_CONNS, "connection_threadmain",
                          "conn %" PRIu64 " deferring readability for StartTLS"
                          " (more_data=%d)\n",
                          conn->c_connid, more_data);
        }

        if ((tag != LDAP_REQ_UNBIND) && !thread_turbo_flag && !replication_connection &&
            !starttls_operation) {
            if (!more_data) {
                conn->c_flags &= ~CONN_FLAG_MAX_THREADS;
                pthread_mutex_lock(&(conn->c_mutex));
                connection_make_readable_nolock(conn);
                pthread_mutex_unlock(&(conn->c_mutex));
                /* once the connection is readable, another thread may access conn,
                 * so need locking from here on */
                signal_listner(conn->c_ct_list);
                read_handed_off = true;
            } else { /* more data in conn - just put back on work_q - bypass poll */
                bypasspollcnt++;
                pthread_mutex_lock(&(conn->c_mutex));
                /* don't do this if it would put us over the max threads per conn */
                if (conn->c_threadnumber < maxthreads) {
                    /* for turbo, c_idlesince is set above - for !turbo and
                     * !more_data, we put the conn back in the poll loop and
                     * c_idlesince is set in handle_pr_read_ready - since we
                     * are bypassing both of those, we set idlesince here
                     */
                    conn->c_idlesince = curtime;
                    if (connection_activity(conn, maxthreads) == 0) {
                        read_handed_off = true;
                        slapi_log_err(SLAPI_LOG_CONNS, "connection_threadmain", "conn %" PRIu64 " queued because more_data\n",
                                      conn->c_connid);
                    }
                } else {
                    /* keep count of how many times maxthreads has blocked an operation */
                    conn->c_maxthreadsblocked++;
                    conn->c_flagblocked = true;
                    if (conn->c_maxthreadsblocked == 1 && connection_has_psearch(conn)) {
                        slapi_log_err(SLAPI_LOG_NOTICE, "connection_threadmain",
                                "Connection (conn=%" PRIu64 ") has a running persistent search "
                                "that has exceeded the maximum allowed threads per connection. "
                                "New operations will be blocked.\n",
                                conn->c_connid);
                    }
                }
                pthread_mutex_unlock(&(conn->c_mutex));
            }
        }

        /* are we in referral-only mode? */
        if (config_check_referral_mode() && tag != LDAP_REQ_UNBIND) {
            referral_mode_reply(pb);
            goto done;
        }

        /* check if new password is required */
        if (connection_need_new_password(conn, op, pb)) {
            goto done;
        }

        /* if this is a bulk import, only "add" and "import done"
         * are allowed */
        if (conn->c_flags & CONN_FLAG_IMPORT) {
            if ((tag != LDAP_REQ_ADD) && (tag != LDAP_REQ_EXTENDED)) {
                /* no cookie for you. */
                slapi_log_err(SLAPI_LOG_ERR, "connection_threadmain", "Attempted operation %lu "
                                                                      "from within bulk import\n",
                              tag);
                slapi_send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL,
                                       NULL, 0, NULL);
                goto done;
            }
        }

        /*
         * Fix bz 1931820 issue (the check to set OP_FLAG_REPLICATED may be done
         * before replication session is properly set).
         */
        if (replication_connection) {
            operation_set_flag(op, OP_FLAG_REPLICATED);
        }

        /*
         * Call the do_<operation> function to process this request.
         */
        connection_dispatch_operation(conn, op, pb);

    done:
        if (doshutdown) {
            if (is_busy) {
                slapi_atomic_decr_32(&current_busy_workers, __ATOMIC_ACQ_REL);
            }
            pthread_mutex_lock(&(conn->c_mutex));
            connection_remove_operation_ext(pb, conn, op);
            connection_make_readable_nolock(conn);
            conn->c_threadnumber--;
            slapi_counter_decrement(conns_in_maxthreads);
            slapi_counter_decrement(g_get_per_thread_snmp_vars()->ops_tbl.dsConnectionsInMaxThreads);
            connection_release_nolock(conn);
            pthread_mutex_unlock(&(conn->c_mutex));
            signal_listner(conn->c_ct_list);
            slapi_pblock_destroy(pb);
            return;
        }
        /*
         * done with this operation. delete it from the op
         * queue for this connection, delete the number of
         * threads devoted to this connection, and see if
         * there's more work to do right now on this conn.
         */

        /* number of ops on this connection */
        PR_AtomicIncrement(&conn->c_opscompleted);
        /* total number of ops for the server */
        slapi_counter_increment(g_get_per_thread_snmp_vars()->server_tbl.dsOpCompleted);
        /*
         * Do not loop on buffered data after work has been queued or
         * this worker handled StartTLS.
         */
        if (read_handed_off || starttls_operation) {
            more_data = 0;
        }
        /* If this op isn't a persistent search, remove it */
        if (op->o_flags & OP_FLAG_PS) {
            /* Release the connection (i.e. decrease refcnt) at the condition
             * this thread will not loop on it.
             * If we are in turbo mode (dedicated to that connection) or
             * more_data (continue reading buffered req) this thread
             * continues to hold the connection
             */
            if (!thread_turbo_flag && !more_data) {
                pthread_mutex_lock(&(conn->c_mutex));
                connection_release_nolock(conn); /* psearch acquires ref to conn - release this one now */
                pthread_mutex_unlock(&(conn->c_mutex));
            }
            /* ps_add makes a shallow copy of the pb - so we
             * can't free it or init it here - just set operation to NULL.
             * ps_send_results will call connection_remove_operation_ext to free it
             * The connection_thread private pblock ('pb') has be cloned and should only
             * be reinit (slapi_pblock_init)
             */
            slapi_pblock_set(pb, SLAPI_OPERATION, NULL);
            slapi_pblock_init(pb);
        } else {
            /* delete from connection operation queue & decr refcnt */
            int conn_closed = 0;
            pthread_mutex_lock(&(conn->c_mutex));
            connection_remove_operation_ext(pb, conn, op);

            /* If we're in turbo mode, we keep our reference to the connection alive */
            /* can't use the more_data var because connection could have changed in another thread */
            slapi_log_err(SLAPI_LOG_CONNS, "connection_threadmain", "conn %" PRIu64 " check more_data %d thread_turbo_flag %d"
                          "repl_conn_bef %d, repl_conn_now %d\n",
                          conn->c_connid, more_data, thread_turbo_flag,
                          replication_connection, conn->c_isreplication_session);
            if (!read_handed_off) {
                if (starttls_operation ||
                    (!replication_connection && conn->c_isreplication_session)) {
                    /* Do not continue processing buffered data after StartTLS
                     * or when the connection becomes a replication session. */
                    more_data = 0;
                } else {
                    /* normal connection or already established replication connection */
                    more_data = conn_buffered_data_avail_nolock(conn, &conn_closed) ? 1 : 0;
                }
            }
            if (!more_data) {
                if (!thread_turbo_flag) {
                    int32_t need_wakeup = 0;

                    /*
                     * Don't release the connection now.
                     * But note down what to do.
                     */
                    if (replication_connection || (1 == is_timedout) ||
                        starttls_operation) {
                        if (!(conn->c_flags & CONN_FLAG_CLOSING)) {
                            connection_make_readable_nolock(conn);
                        }
                        need_wakeup = 1;
                    }
                    if (!need_wakeup) {
                        if (conn->c_threadnumber == maxthreads) {
                            need_wakeup = 1;
                        } else {
                            need_wakeup = 0;
                        }
                    }

                    if (conn->c_threadnumber == maxthreads) {
                        conn->c_flags &= ~CONN_FLAG_MAX_THREADS;
                        slapi_counter_decrement(conns_in_maxthreads);
                        slapi_counter_decrement(g_get_per_thread_snmp_vars()->ops_tbl.dsConnectionsInMaxThreads);
                    }
                    conn->c_threadnumber--;
                    connection_release_nolock(conn);
                    /* If need_wakeup, call signal_listner once.
                     * Need to release the connection (refcnt--)
                     * before that call.
                     */
                    if (need_wakeup) {
                        signal_listner(conn->c_ct_list);
                        need_wakeup = 0;
                    }
                }
            }
            pthread_mutex_unlock(&(conn->c_mutex));
        }
    } /* while (1) */
}

/* thread need to hold conn->c_mutex before calling this function */
int
connection_activity(Connection *conn, int maxthreads)
{
    struct Slapi_op_stack *op_stack_obj;

    if (connection_acquire_nolock(conn) == -1) {
        slapi_log_err(SLAPI_LOG_CONNS,
                      "connection_activity", "Could not acquire lock in connection_activity as conn %" PRIu64 " closing fd=%d\n",
                      conn->c_connid, conn->c_sd);
        /* XXX how to handle this error? */
        /* MAB: 25 Jan 01: let's return on error and pray this won't leak */
        return (-1);
    }

    /* set these here so setup_pr_read_pds will not add this conn back to the poll array */
    conn->c_gettingber = 1;
    conn->c_threadnumber++;
    if (conn->c_threadnumber == maxthreads) {
        conn->c_flags |= CONN_FLAG_MAX_THREADS;
        conn->c_maxthreadscount++;
        slapi_counter_increment(max_threads_count);
        slapi_counter_increment(conns_in_maxthreads);
        slapi_counter_increment(g_get_per_thread_snmp_vars()->ops_tbl.dsConnectionsInMaxThreads);
        slapi_counter_increment(g_get_per_thread_snmp_vars()->ops_tbl.dsMaxThreadsHits);
    }
    op_stack_obj = connection_get_operation();
    connection_add_operation(conn, op_stack_obj->op);
    /* Add conn to the end of the work queue.  */
    /* have to do this last - add_work_q will signal waiters in connection_wait_for_new_work */
    add_work_q((work_q_item *)conn, op_stack_obj);

    if (!config_check_referral_mode()) {
        slapi_counter_increment(g_get_per_thread_snmp_vars()->server_tbl.dsOpInitiated);
        slapi_counter_increment(g_get_per_thread_snmp_vars()->ops_tbl.dsInOps);
    }
    return 0;
}

/* add_work_q():  will add a work_q_item to the end of the global work queue. The work queue
    is implemented as a single link list. */

static void
add_work_q(work_q_item *wqitem, struct Slapi_op_stack *op_stack_obj)
{
    struct Slapi_work_q *new_work_q = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "add_work_q", "=>\n");

    new_work_q = create_work_q();
    new_work_q->work_item = wqitem;
    new_work_q->op_stack_obj = op_stack_obj;
    new_work_q->next_work_item = NULL;

    pthread_mutex_lock(&work_q_lock);
    if (tail_work_q == NULL) {
        tail_work_q = new_work_q;
        head_work_q = new_work_q;
    } else {
        tail_work_q->next_work_item = new_work_q;
        tail_work_q = new_work_q;
    }
    PR_AtomicIncrement(&work_q_size); /* increment q size */
    if (work_q_size > work_q_size_max) {
        work_q_size_max = work_q_size;
    }
    pthread_cond_signal(&work_q_cv); /* notify waiters in connection_wait_for_new_work */
    pthread_mutex_unlock(&work_q_lock);
}

/* get_work_q(): will get a work_q_item from the beginning of the work queue, return NULL if
    the queue is empty.  This should only be called from connection_wait_for_new_work
    with the work_q_lock held */

static work_q_item *
get_work_q(struct Slapi_op_stack **op_stack_obj)
{
    struct Slapi_work_q *tmp = NULL;
    work_q_item *wqitem;

    slapi_log_err(SLAPI_LOG_TRACE, "get_work_q", "=>\n");
    if (head_work_q == NULL) {
        slapi_log_err(SLAPI_LOG_TRACE, "get_work_q", "The work queue is empty.\n");
        return NULL;
    }

    tmp = head_work_q;
    if (head_work_q == tail_work_q) {
        tail_work_q = NULL;
    }
    head_work_q = tmp->next_work_item;

    wqitem = tmp->work_item;
    *op_stack_obj = tmp->op_stack_obj;
    PR_AtomicDecrement(&work_q_size); /* decrement q size */
    /* Free the memory used by the item found. */
    destroy_work_q(&tmp);

    return (wqitem);
}

/* Work queue metric getters - lock-free reads using atomics */

int32_t
get_work_q_size(void)
{
    int32_t val = slapi_atomic_load_32((int32_t *)&work_q_size, __ATOMIC_ACQUIRE);
    return val > 0 ? val : 0;
}

int32_t
get_work_q_size_max(void)
{
    int32_t val = slapi_atomic_load_32((int32_t *)&work_q_size_max, __ATOMIC_ACQUIRE);
    return val > 0 ? val : 0;
}

int32_t
get_busy_worker_count(void)
{
    int32_t val = slapi_atomic_load_32(&current_busy_workers, __ATOMIC_ACQUIRE);
    return val > 0 ? val : 0;
}

int32_t
get_max_busy_worker_count(void)
{
    int32_t val = slapi_atomic_load_32(&max_busy_workers, __ATOMIC_ACQUIRE);
    return val > 0 ? val : 0;
}

/* Helper functions common to both varieties of connection code: */

/* op_thread_cleanup() : This function is called by daemon thread when it gets
    the slapd_shutdown signal.  It will set op_shutdown to 1 and notify
    all thread waiting on op_thread_cv to terminate.  */

void
op_thread_cleanup()
{
    slapi_log_err(SLAPI_LOG_INFO, "op_thread_cleanup",
                  "slapd shutting down - signaling operation threads - op stack size %d max work q size %d max work q stack size %d\n",
                  op_stack_size, work_q_size_max, work_q_stack_size_max);

    PR_AtomicIncrement(&op_shutdown);
    pthread_mutex_lock(&work_q_lock);
    pthread_cond_broadcast(&work_q_cv); /* tell any thread waiting in connection_wait_for_new_work to shutdown */
    pthread_mutex_unlock(&work_q_lock);
}

/* do this after all worker threads have terminated */
void
connection_post_shutdown_cleanup()
{
    struct Slapi_op_stack *stack_obj;
    int stack_cnt = 0;
    struct Slapi_work_q *work_q;
    int work_cnt = 0;

    while ((work_q = (struct Slapi_work_q *)PR_StackPop(work_q_stack))) {
        Connection *conn = (Connection *)work_q->work_item;
        stack_obj = work_q->op_stack_obj;
        if (stack_obj) {
            if (conn) {
                connection_remove_operation(conn, stack_obj->op);
            }
            connection_done_operation(conn, stack_obj);
        }
        slapi_ch_free((void **)&work_q);
        work_cnt++;
    }
    PR_DestroyStack(work_q_stack);
    work_q_stack = NULL;
    while ((stack_obj = (struct Slapi_op_stack *)PR_StackPop(op_stack))) {
        operation_free(&stack_obj->op, NULL);
        slapi_ch_free((void **)&stack_obj);
        stack_cnt++;
    }
    PR_DestroyStack(op_stack);
    op_stack = NULL;
    slapi_log_err(SLAPI_LOG_INFO, "connection_post_shutdown_cleanup",
                  "slapd shutting down - freed %d work q stack objects - freed %d op stack objects\n",
                  work_cnt, stack_cnt);
}

static void
connection_add_operation(Connection *conn, Operation *op)
{
    Operation **olist = &conn->c_ops;
    int id = conn->c_opsinitiated++;
    PRUint64 connid = conn->c_connid;
    Operation **tmp;

    /* slapi_ch_stop_recording(); */

    for (tmp = olist; *tmp != NULL; tmp = &(*tmp)->o_next)
        ; /* NULL */

    *tmp = op;
    op->o_opid = id;
    op->o_connid = connid;
    /* Call the plugin extension constructors */
    op->o_extension = factory_create_extension(get_operation_object_type(), op, conn);
}

/*
 * Find an Operation on the Connection, and zap it in the butt.
 * Call this function with conn->c_mutex locked.
 */
void
connection_remove_operation(Connection *conn, Operation *op)
{
    Operation **olist = &conn->c_ops;
    Operation **tmp;

    for (tmp = olist; *tmp != NULL && *tmp != op; tmp = &(*tmp)->o_next)
        ; /* NULL */

    if (*tmp == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "connection_remove_operation", "Can't find op %d for conn %" PRIu64 "\n",
                      (int)op->o_msgid, conn->c_connid);
    } else {
        *tmp = (*tmp)->o_next;
    }
}

void
connection_remove_operation_ext(Slapi_PBlock *pb, Connection *conn, Operation *op)
{
    connection_remove_operation(conn, op);
    void *op_stack_elem = slapi_pblock_get_op_stack_elem(pb);
    connection_done_operation(conn, op_stack_elem);
    slapi_pblock_set(pb, SLAPI_OPERATION, NULL);
    slapi_pblock_init(pb);
}

/*
 * Return a non-zero value if any operations are pending on conn.
 * Operation op2ignore is ignored (okay to pass NULL). Typically, op2ignore
 *    is the caller's op (because the caller wants to check if all other
 *    ops are done).
 * If test_resultsent is non-zero, operations that have already sent
 *    a result to the client are ignored.
 * Call this function with conn->c_mutex locked.
 */
int
connection_operations_pending(Connection *conn, Operation *op2ignore, int test_resultsent)
{
    Operation *op;

    PR_ASSERT(conn != NULL);

    for (op = conn->c_ops; op != NULL; op = op->o_next) {
        if (op == op2ignore) {
            continue;
        }
        if (!test_resultsent || op->o_status != SLAPI_OP_STATUS_RESULT_SENT) {
            break;
        }
    }

    return (op != NULL);
}


/* Copy the authorization identity from the connection struct into the
 * operation struct.  We do this late, because an operation might start
 * before authentication is complete, at least on an SSL connection.
 * We want each operation to get its authorization identity after the
 * SSL software has had its chance to finish the SSL handshake;
 * that is, after the first few bytes of the request are received.
 * In particular, we want the first request from an LDAPS client
 * to have an authorization identity derived from the initial SSL
 * handshake.  We also copy the SSF at this time.
 */
static void
op_copy_identity(Connection *conn, Operation *op)
{
    size_t dnlen;
    size_t typelen;

    pthread_mutex_lock(&(conn->c_mutex));
    dnlen = conn->c_dn ? strlen(conn->c_dn) : 0;
    typelen = conn->c_authtype ? strlen(conn->c_authtype) : 0;

    slapi_sdn_done(&op->o_sdn);
    slapi_ch_free_string(&(op->o_authtype));
    if (dnlen <= 0 && typelen <= 0) {
        op->o_authtype = NULL;
    } else {
        slapi_sdn_set_dn_byval(&op->o_sdn, conn->c_dn);
        op->o_authtype = slapi_ch_strdup(conn->c_authtype);
        /* set the thread data bind dn index */
        slapi_td_set_dn(slapi_ch_strdup(conn->c_dn));
    }
    /* XXX We should also copy c_client_cert into *op here; it's
     * part of the authorization identity.  The operation's copy
     * (not c_client_cert) should be used for access control.
     */

    /* copy isroot flag as well so root DN privileges are preserved */
    op->o_isroot = conn->c_isroot;

    /* copy the highest SSF (between local, SASL, and SSL/TLS)
     * into the operation for use by access control. */
    if ((conn->c_sasl_ssf >= conn->c_ssl_ssf) && (conn->c_sasl_ssf >= conn->c_local_ssf)) {
        op->o_ssf = conn->c_sasl_ssf;
    } else if ((conn->c_ssl_ssf >= conn->c_sasl_ssf) && (conn->c_ssl_ssf >= conn->c_local_ssf)) {
        op->o_ssf = conn->c_ssl_ssf;
    } else {
        op->o_ssf = conn->c_local_ssf;
    }

    pthread_mutex_unlock(&(conn->c_mutex));
}

/* Sets the SSL SSF in the connection struct. */
static void
connection_set_ssl_ssf(Connection *conn)
{
    pthread_mutex_lock(&(conn->c_mutex));

    if (conn->c_flags & CONN_FLAG_SSL) {
        SSL_SecurityStatus(conn->c_prfd, NULL, NULL, NULL, &(conn->c_ssl_ssf), NULL, NULL);
    } else {
        conn->c_ssl_ssf = 0;
    }

    pthread_mutex_unlock(&(conn->c_mutex));
}

static int
is_ber_too_big(const Connection *conn, ber_len_t ber_len)
{
    if (ber_len > conn->c_maxbersize) {
        log_ber_too_big_error(conn, ber_len, conn->c_maxbersize);
        return 1;
    }
    return 0;
}


/*
 * Pass 0 for maxbersize if you do not have it handy. It is also OK to pass
 * 0 for ber_len, in which case a slightly less informative message is
 * logged.
 */
static void
log_ber_too_big_error(const Connection *conn, ber_len_t ber_len, ber_len_t maxbersize)
{
    if (0 == maxbersize) {
        maxbersize = conn->c_maxbersize;
    }
    if (0 == ber_len) {
        slapi_log_err(SLAPI_LOG_ERR, "log_ber_too_big_error",
                      "conn=%" PRIu64 " fd=%d Incoming BER Element was too long, max allowable"
                      " is %" BERLEN_T " bytes. Change the nsslapd-maxbersize attribute in"
                      " cn=config to increase.\n",
                      conn->c_connid, conn->c_sd, maxbersize);
    } else if (ber_len < maxbersize) {
        /* This means the request was misformed, not too large. */
        slapi_log_err(SLAPI_LOG_ERR, "log_ber_too_big_error",
                      "conn=%" PRIu64 " fd=%d Incoming BER Element may be misformed. "
                      "This may indicate an attempt to use TLS on a plaintext port, "
                      "IE ldaps://localhost:389. Check your client LDAP_URI settings.\n",
                      conn->c_connid, conn->c_sd);
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "log_ber_too_big_error",
                      "conn=%" PRIu64 " fd=%d Incoming BER Element was %" BERLEN_T " bytes, max allowable"
                      " is %" BERLEN_T " bytes. Change the nsslapd-maxbersize attribute in"
                      " cn=config to increase.\n",
                      conn->c_connid, conn->c_sd, ber_len, maxbersize);
    }
}


void
disconnect_server(Connection *conn, PRUint64 opconnid, int opid, PRErrorCode reason, PRInt32 error)
{
    pthread_mutex_lock(&(conn->c_mutex));
    disconnect_server_nomutex(conn, opconnid, opid, reason, error);
    pthread_mutex_unlock(&(conn->c_mutex));
}

static ps_wakeup_all_fn_ptr ps_wakeup_all_fn = NULL;

/*
 * disconnect_server - close a connection. takes the connection to close,
 * the connid associated with the operation generating the close (so we
 * don't accidentally close a connection that's not ours), and the opid
 * of the operation generating the close (for logging purposes).
 */

void
disconnect_server_nomutex_ext(Connection *conn, PRUint64 opconnid, int opid, PRErrorCode reason, PRInt32 error, int schedule_closure_job)
{
    if ((conn->c_sd != SLAPD_INVALID_SOCKET &&
         conn->c_connid == opconnid) &&
        !(conn->c_flags & CONN_FLAG_CLOSING))
    {
        slapi_log_err(SLAPI_LOG_CONNS, "disconnect_server_nomutex_ext",
                "Setting conn %" PRIu64 " fd=%d to be disconnected: reason %d\n",
                conn->c_connid, conn->c_sd, reason);
        /*
         * PR_Close must be called before anything else is done because
         * of NSPR problem on NT which requires that the socket on which
         * I/O timed out is closed before any other I/O operation is
         * attempted by the thread.
         * WARNING :  As of today the current code does not fulfill the
         * requirements above.
         */

        /* Mark that the socket should be closed on this connection.
         * We don't want to actually close the socket here, because
         * the listener thread could be PR_Polling over it right now.
         * The last thread to stop using the connection will do the closing.
         */
        conn->c_flags |= CONN_FLAG_CLOSING;
        g_decrement_current_conn_count();

        if(error) {
            slapi_log_access(LDAP_DEBUG_STATS,
                             "conn=%" PRIu64 " op=%d fd=%d Disconnect - %s - %s\n",
                             conn->c_connid, opid, conn->c_sd,
                             slapd_system_strerror(error),
                             slapd_pr_strerror(reason));
        } else {
            slapi_log_access(LDAP_DEBUG_STATS,
                             "conn=%" PRIu64 " op=%d fd=%d Disconnect - %s\n",
                             conn->c_connid, opid, conn->c_sd,
                             slapd_pr_strerror(reason));
        }
        slapi_log_security_tcp(conn, SECURITY_TCP_ERROR, reason, slapd_pr_strerror(reason));

        if (!config_check_referral_mode()) {
            slapi_counter_decrement(g_get_per_thread_snmp_vars()->ops_tbl.dsConnections);
        }

        conn->c_gettingber = 0;
        connection_abandon_operations(conn);
        /* needed here to ensure simple paged results timeout properly and
         * don't impact subsequent ops */
        pagedresults_reset_timedout_nolock(conn);

        if (!config_check_referral_mode()) {
            /*
             * If any of the outstanding operations on this
             * connection were persistent searches, then
             * ding all the persistent searches to get them
             * to notice that their operations have been abandoned.
             */
            if (connection_has_psearch(conn)) {
                if (NULL == ps_wakeup_all_fn) {
                    if (get_entry_point(ENTRY_POINT_PS_WAKEUP_ALL,
                                        (caddr_t *)(&ps_wakeup_all_fn)) == 0) {
                        (ps_wakeup_all_fn)();
                    }
                } else {
                    (ps_wakeup_all_fn)();
                }
            }
        }

    } else {
        /* We avoid logging an invalid conn=0 connection as it is not a real connection. */
        if (!(conn->c_sd == SLAPD_INVALID_SOCKET && conn->c_connid == 0)) {
            slapi_log_err(SLAPI_LOG_CONNS, "disconnect_server_nomutex_ext",
                    "Not setting conn %d to be disconnected: %s\n",
                    conn->c_sd,
                    (conn->c_sd == SLAPD_INVALID_SOCKET) ? "socket is invalid" :
                            ((conn->c_connid != opconnid) ? "conn id does not match op conn id" :
                                        ((conn->c_flags & CONN_FLAG_CLOSING) ? "conn is closing" : "unknown")));
        }
    }
}

void
disconnect_server_nomutex(Connection *conn, PRUint64 opconnid, int opid, PRErrorCode reason, PRInt32 error)
{
    disconnect_server_nomutex_ext(conn, opconnid, opid, reason, error, 1);
}

void
connection_abandon_operations(Connection *c)
{
    Operation *op;
    for (op = c->c_ops; op != NULL; op = op->o_next) {
        /* abandon the operation only if it is not yet
           completed (i.e., no result has been sent yet to
           the client */
        /* sync repl uses the persist mode, and it cannot prevent
         * setting o_status, but has to be abandonned
         * handle it here until a better solution is found
         */
        if (op->o_status != SLAPI_OP_STATUS_RESULT_SENT ||
            (op->o_flags & OP_FLAG_PS)) {
            op->o_status = SLAPI_OP_STATUS_ABANDONED;
        }
    }
}

/*
 * Return unread bytes in c_buffer or PR_Available on the socket
 * when connection-buffer is off. If PR_Available fails we return
 * 0 so its not treated as leftover data.
 * Must be called within c->c_mutex.
 */
size_t
connection_read_buffer_data_len(Connection *c)
{
    int conn_closed = 0;
    PRInt32 avail;
    size_t n;

    if (c == NULL) {
        return 0;
    }
    /* Connection buffer has data ? */
    if (c->c_private != NULL) {
        n = conn_buffered_data_avail_nolock(c, &conn_closed);
        if (n > 0 || conn_closed) {
            return n;
        }
    }
    /* Socket has data ? */
    if (c->c_prfd != NULL) {
        avail = PR_Available(c->c_prfd);
        if (avail < 0) {
            slapi_log_err(SLAPI_LOG_CONNS, "connection_read_buffer_data_len",
                          "PR_Available failed on conn %" PRIu64 " fd=%d: %s\n",
                          c->c_connid, c->c_sd, slapd_pr_strerror(PR_GetError()));
            return 0;
        }
        return (size_t)avail;
    }
    return 0;
}

/*
 * Clear any data left in c_buffer and drain the socket buffer.
 * Must be called within c->c_mutex.
 */
void
connection_reset_read_buffer(Connection *c)
{
    if (c == NULL || c->c_private == NULL) {
        return;
    }
    c->c_private->c_buffer_bytes = 0;
    c->c_private->c_buffer_offset = 0;
    if (c->c_sb) {
        ber_sockbuf_ctrl(c->c_sb, LBER_SB_OPT_DRAIN, NULL);
    }
}

/* must be called within c->c_mutex */
void
connection_set_io_layer_cb(Connection *c, Conn_IO_Layer_cb push_cb, Conn_IO_Layer_cb pop_cb, void *cb_data)
{
    c->c_push_io_layer_cb = push_cb;
    c->c_pop_io_layer_cb = pop_cb;
    c->c_io_layer_cb_data = cb_data;
}

/* must be called within c->c_mutex */
int
connection_call_io_layer_callbacks(Connection *c)
{
    int rv = 0;
    if (c->c_pop_io_layer_cb) {
        rv = (c->c_pop_io_layer_cb)(c, c->c_io_layer_cb_data);
        c->c_pop_io_layer_cb = NULL;
    }
    if (!rv && c->c_push_io_layer_cb) {
        rv = (c->c_push_io_layer_cb)(c, c->c_io_layer_cb_data);
        c->c_push_io_layer_cb = NULL;
    }
    c->c_io_layer_cb_data = NULL;

    return rv;
}

/*
 * Install I/O layers or disconnect on failure so the worker
 * cannot keep reading the connection in the clear.
 * Must be called within c->c_mutex.
 */
static void
connection_apply_io_layers_or_disconnect(Connection *conn)
{
    if (connection_call_io_layer_callbacks(conn) == 0) {
        return;
    }
    slapi_log_err(SLAPI_LOG_ERR, "connection_apply_io_layers_or_disconnect",
                  "Could not add/remove IO layers from conn %" PRIu64 "\n",
                  conn->c_connid);
    disconnect_server_nomutex(conn, conn->c_connid, -1, SLAPD_DISCONNECT_ABORT, 0);
}

int32_t
connection_has_psearch(Connection *c)
{
    Operation *o;

    for (o = c->c_ops; o != NULL; o = o->o_next) {
        if (o->o_flags & OP_FLAG_PS) {
            return 1;
        }
    }

    return 0;
}

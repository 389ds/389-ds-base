/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include "slap.h"
#include "slapi-plugin.h"
#include "fe.h"
#include <sasl/sasl.h>
#include <arpa/inet.h>

/*
 * I/O Shim Layer for SASL Encryption
 * The 'handle' is a pointer to a sasl_connection structure.
 */

#define SASL_IO_BUFFER_SIZE 1024
#define SASL_IO_BUFFER_NOT_ENCRYPTED -99
#define SASL_IO_BUFFER_START_SIZE 7

    /*
 * SASL sends its encrypted PDU's with an embedded 4-byte length
 * at the beginning (in network byte order). We peek inside the
 * received data off the wire to find this length, and use it
 * to determine when we have read an entire SASL PDU.
 * So when we have that there is no need for the SASL layer
 * to do any fancy buffering with it, we always hand it
 * a full packet.
 */

    struct PRFilePrivate
{
    char *decrypted_buffer;
    uint32_t decrypted_buffer_size;
    uint32_t decrypted_buffer_count;
    uint32_t decrypted_buffer_offset;
    char *encrypted_buffer;
    uint32_t encrypted_buffer_size;
    uint32_t encrypted_buffer_count;
    uint32_t encrypted_buffer_offset;
    Connection *conn;         /* needed for connid and sasl_conn context */
    PRBool send_encrypted;    /* can only send encrypted data after the first read -
                              that is, we cannot send back an encrypted response
                              to the bind request that established the sasl io */
    const char *send_buffer;  /* encrypted buffer to send to client */
    unsigned int send_size;   /* size of the encrypted buffer */
    unsigned int send_offset; /* number of bytes sent so far */
};

typedef PRFilePrivate sasl_io_private;

static PRInt32 PR_CALLBACK
sasl_io_recv(PRFileDesc *fd, void *buf, PRInt32 len, PRIntn flags, PRIntervalTime timeout);

static void
debug_print_layers(PRFileDesc *fd __attribute__((unused)))
{
#if 0
    PR_ASSERT(fd->higher == NULL); /* this is the topmost layer */
    while (fd) {
        PRSocketOptionData sod;
        PRInt32 err;

        slapi_log_err(SLAPI_LOG_CONNS,
                       "debug_print_layers", "fd %d sasl_io_recv = %p\n",
                        PR_FileDesc2NativeHandle(fd), sasl_io_recv );
        slapi_log_err(SLAPI_LOG_CONNS,
                   "debug_print_layers", "fd name %s type = %d recv = %p\n",
                   PR_GetNameForIdentity(fd->identity),
                   PR_GetDescType(fd),
                   fd->methods->recv ? fd->methods->recv : NULL );
        sod.option = PR_SockOpt_Nonblocking;
        if (PR_FAILURE == PR_GetSocketOption(fd, &sod)) {
            err = PR_GetError();
            slapi_log_err(SLAPI_LOG_CONNS,
                            "debug_print_layers", "Error getting nonblocking option: %d %s\n",
                            err, slapd_pr_strerror(err) );
        } else {
            slapi_log_err(SLAPI_LOG_CONNS,
                           "debug_print_layers", "Non blocking %d\n", sod.value.non_blocking );
        }
        sod.option = PR_SockOpt_Reuseaddr;
        if (PR_FAILURE == PR_GetSocketOption(fd, &sod)) {
            err = PR_GetError();
            slapi_log_err(SLAPI_LOG_CONNS,
                            "debug_print_layers", "Error getting reuseaddr option: %d %s\n",
                            err, slapd_pr_strerror(err) );
        } else {
            slapi_log_err(SLAPI_LOG_CONNS,
                           "debug_print_layers", "reuseaddr %d\n", sod.value.reuse_addr );
        }
        sod.option = PR_SockOpt_RecvBufferSize;
        if (PR_FAILURE == PR_GetSocketOption(fd, &sod)) {
            err = PR_GetError();
            slapi_log_err(SLAPI_LOG_CONNS,
                            "debug_print_layers", "Error getting recvbuffer option: %d %s\n",
                            err, slapd_pr_strerror(err) );
        } else {
            slapi_log_err(SLAPI_LOG_CONNS,
                           "debug_print_layers", "recvbuffer %d\n", sod.value.recv_buffer_size );
        }
        fd = fd->lower;
    }
#endif
}

static void
sasl_io_init_buffers(sasl_io_private *sp)
{
    sp->decrypted_buffer = slapi_ch_malloc(SASL_IO_BUFFER_SIZE);
    sp->decrypted_buffer_size = SASL_IO_BUFFER_SIZE;
    sp->encrypted_buffer = slapi_ch_malloc(SASL_IO_BUFFER_SIZE);
    sp->encrypted_buffer_size = SASL_IO_BUFFER_SIZE;
}


static void
sasl_io_resize_encrypted_buffer(sasl_io_private *sp, uint32_t requested_size)
{
    if (requested_size > sp->encrypted_buffer_size) {
        sp->encrypted_buffer = slapi_ch_realloc(sp->encrypted_buffer, requested_size);
        sp->encrypted_buffer_size = requested_size;
    }
}

static void
sasl_io_resize_decrypted_buffer(sasl_io_private *sp, uint32_t requested_size)
{
    if (requested_size > sp->decrypted_buffer_size) {
        sp->decrypted_buffer = slapi_ch_realloc(sp->decrypted_buffer, requested_size);
        sp->decrypted_buffer_size = requested_size;
    }
}

static int
sasl_io_reading_packet(sasl_io_private *sp)
{
    return (sp->encrypted_buffer_count > 0);
}

static int
sasl_io_finished_packet(sasl_io_private *sp)
{
    return (sp->encrypted_buffer_count && (sp->encrypted_buffer_offset == sp->encrypted_buffer_count));
}

static const char *const sasl_LayerName = "SASL";
static PRDescIdentity sasl_LayerID;
static PRIOMethods sasl_IoMethods;
static PRCallOnceType sasl_callOnce = {0, 0, 0};

static sasl_io_private *
sasl_get_io_private(PRFileDesc *fd)
{
    sasl_io_private *sp;

    PR_ASSERT(fd != NULL);
    PR_ASSERT(fd->methods->file_type == PR_DESC_LAYERED);
    PR_ASSERT(fd->identity == sasl_LayerID);

    sp = (sasl_io_private *)fd->secret;
    return sp;
}

/*
 * return values:
 * 0 - connection was closed
 * 1 - success
 * -1 - error
 */
static PRInt32
sasl_io_start_packet(PRFileDesc *fd, PRIntn flags, PRIntervalTime timeout, PRInt32 *err)
{
    unsigned char buffer[SASL_IO_BUFFER_START_SIZE];
    sasl_io_private *sp = sasl_get_io_private(fd);
    Connection *c = sp->conn;
    int32_t amount = sizeof(buffer);
    int32_t ret = 0;
    uint32_t packet_length = 0;
    int32_t saslio_limit;

    *err = 0;
    debug_print_layers(fd);
    /* first we need the length bytes */
    ret = PR_Recv(fd->lower, buffer, amount, flags, timeout);
    slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_start_packet",
                  "Read sasl packet length returned %d on connection %" PRIu64 "\n",
                  ret, c->c_connid);
    if (ret <= 0) {
        *err = PR_GetError();
        if (ret == 0) {
            slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_start_packet",
                          "Connection closed while reading sasl packet length on connection %" PRIu64 "\n",
                          c->c_connid);
        } else {
            slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_start_packet",
                          "Error reading sasl packet length on connection %" PRIu64 " %d:%s\n",
                          c->c_connid, *err, slapd_pr_strerror(*err));
        }
        return ret;
    }
    /*
     * Read the bytes and add them to sp->encrypted_buffer
     * - if offset < 7, tell caller we didn't read enough bytes yet
     * - if offset >= 7, decode the length and proceed.
     */
    if ((ret + sp->encrypted_buffer_offset) > sp->encrypted_buffer_size) {
        sasl_io_resize_encrypted_buffer(sp, ret + sp->encrypted_buffer_offset);
    }
    memcpy(sp->encrypted_buffer + sp->encrypted_buffer_offset, buffer, ret);
    sp->encrypted_buffer_offset += ret;
    if (sp->encrypted_buffer_offset < sizeof(buffer)) {
        slapi_log_err(SLAPI_LOG_CONNS,
                      "sasl_io_start_packet", "Read only %d bytes of sasl packet "
                                              "length on connection %" PRIu64 "\n",
                      ret, c->c_connid);
#if defined(EWOULDBLOCK)
        errno = EWOULDBLOCK;
#elif defined(EAGAIN)
        errno = EAGAIN;
#endif
        PR_SetError(PR_WOULD_BLOCK_ERROR, errno);
        return PR_FAILURE;
    }

    /*
     * Check if an LDAP operation was sent unencrypted
     */
    if (!sp->send_encrypted && *sp->encrypted_buffer == LDAP_TAG_MESSAGE) {
        struct berval bv;
        BerElement *ber = NULL;
        struct berval tmp_bv;
        ber_len_t maxbersize = config_get_maxbersize();
        ber_len_t ber_len = 0;
        ber_tag_t tag = 0;

        slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_start_packet", "conn=%" PRIu64 " fd=%d "
                                                               "Sent an LDAP message that was not encrypted.\n",
                      c->c_connid,
                      c->c_sd);

        /* Build a berval so we can get the length before reading in the entire packet */
        bv.bv_val = sp->encrypted_buffer;
        bv.bv_len = sp->encrypted_buffer_offset;
        if ((ber_len = slapi_berval_get_msg_len(&bv, 0)) == -1) {
            goto done;
        }

        /* Is the ldap operation too large? */
        if (ber_len > maxbersize) {
            slapi_log_err(SLAPI_LOG_ERR, "sasl_io_start_packet",
                          "conn=%" PRIu64 " fd=%d Incoming BER Element was too long, max allowable "
                          "is %" BERLEN_T " bytes. Change the nsslapd-maxbersize attribute in "
                          "cn=config to increase.\n",
                          c->c_connid, c->c_sd, maxbersize);
            PR_SetError(PR_IO_ERROR, 0);
            return PR_FAILURE;
        }
        /*
         * Bump the ber length by 2 for the tag/length we skipped over when calculating the berval length.
         * We now have the total "packet" size, so we know exactly what is left to read in.
         */
        ber_len += 2;

        /*
         * Read in the rest of the packet.
         *
         * sp->encrypted_buffer_offset is the total number of bytes that have been written
         * to the buffer.  Once we have the complete LDAP packet we'll set it back to zero,
         * and adjust the sp->encrypted_buffer_count.
         */
        while (sp->encrypted_buffer_offset < ber_len) {
            unsigned char mybuf[SASL_IO_BUFFER_SIZE];

            ret = PR_Recv(fd->lower, mybuf, SASL_IO_BUFFER_SIZE, flags, timeout);
            if (ret == PR_WOULD_BLOCK_ERROR || (ret == 0 && sp->encrypted_buffer_offset < ber_len)) {
/*
                 * Need more data, go back and try to get more data from connection_read_operation()
                 * We can return and continue to update sp->encrypted_buffer because we have
                 * maintained the current size in encrypted_buffer_offset.
                 */
#if defined(EWOULDBLOCK)
                errno = EWOULDBLOCK;
#elif defined(EAGAIN)
                errno = EAGAIN;
#endif
                PR_SetError(PR_WOULD_BLOCK_ERROR, errno);
                return PR_FAILURE;
            } else if (ret > 0) {
                slapi_log_err(SLAPI_LOG_CONNS,
                              "sasl_io_start_packet",
                              "Continued: read sasl packet length returned %d on connection %" PRIu64 "\n",
                              ret, c->c_connid);
                if ((ret + sp->encrypted_buffer_offset) > sp->encrypted_buffer_size) {
                    sasl_io_resize_encrypted_buffer(sp, ret + sp->encrypted_buffer_offset);
                }
                memcpy(sp->encrypted_buffer + sp->encrypted_buffer_offset, mybuf, ret);
                sp->encrypted_buffer_offset += ret;
            } else if (ret < 0) {
                *err = PR_GetError();
                slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_start_packet",
                              "Error reading sasl packet length on connection "
                              "%" PRIu64 " %d:%s\n",
                              c->c_connid, *err, slapd_pr_strerror(*err));
                return ret;
            }
        }

        /*
         * Reset the berval with the updated buffer, and create the berElement
         */
        bv.bv_val = sp->encrypted_buffer;
        bv.bv_len = sp->encrypted_buffer_offset;

        if ((ber = ber_init(&bv)) == NULL) {
            goto done;
        }

/*
         * Start parsing the berElement.  First skip this tag, and move on to the
         * tag msgid
         */
        ber_skip_tag(ber, &ber_len);
        if (ber_peek_tag(ber, &ber_len) == LDAP_TAG_MSGID) {
/*
             * Skip the entire msgid element, so we can get to the LDAP op tag
             */
            if (ber_skip_element(ber, &tmp_bv) == LDAP_TAG_MSGID) {
                /*
                 * We only allow unbind operations to be processed for unencrypted operations
                 */
                if ((tag = ber_peek_tag(ber, &ber_len)) == LDAP_REQ_UNBIND) {
                    slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_start_packet", "conn=%" PRIu64 " fd=%d "
                                                                           "Received unencrypted UNBIND operation.\n",
                                  c->c_connid,
                                  c->c_sd);
                    sp->encrypted_buffer_count = sp->encrypted_buffer_offset;
                    sp->encrypted_buffer_offset = 0;
                    ber_free(ber, 1);
                    return SASL_IO_BUFFER_NOT_ENCRYPTED;
                }
                slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_start_packet", "conn=%" PRIu64 " fd=%d "
                                                                       "Error: received an LDAP message (tag 0x%lx) that was not encrypted.\n",
                              c->c_connid, c->c_sd, (long unsigned int)tag);
            }
        }

    done:
        /* If we got here we have garbage, or a denied LDAP operation */
        slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_start_packet", "conn=%" PRIu64 " fd=%d "
                                                               "Error: received an invalid message that was not encrypted.\n",
                      c->c_connid, c->c_sd);

        if (NULL != ber) {
            ber_free(ber, 1);
        }
        PR_SetError(PR_IO_ERROR, 0);

        return PR_FAILURE;
    }

    /* At this point, sp->encrypted_buffer_offset == sizeof(buffer) */
    /* Decode the length */
    packet_length = ntohl(*(uint32_t *)sp->encrypted_buffer);
    /* add length itself (for Cyrus SASL library) */
    packet_length += sizeof(uint32_t);

    slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_start_packet",
                  "read sasl packet length %" PRIu32 " on connection %" PRIu64 "\n",
                  packet_length, c->c_connid);

    /* Check if the packet length is larger than our max allowed.  A
     * setting of -1 means that we allow any size SASL IO packet. */
    saslio_limit = config_get_maxsasliosize();
    if ((saslio_limit != -1) && (packet_length > saslio_limit)) {
        slapi_log_err(SLAPI_LOG_ERR, "sasl_io_start_packet",
                      "SASL encrypted packet length exceeds maximum allowed limit (length=%" PRIu32 ", limit=%" PRIu32")."
                      "  Change the nsslapd-maxsasliosize attribute in cn=config to increase limit.\n",
                      packet_length, config_get_maxsasliosize());
        PR_SetError(PR_BUFFER_OVERFLOW_ERROR, 0);
        *err = PR_BUFFER_OVERFLOW_ERROR;
        return -1;
    }

    sasl_io_resize_encrypted_buffer(sp, packet_length);
    /* Cyrus SASL implementation expects to have the length at the first
       4 bytes */
    sp->encrypted_buffer_count = packet_length;

    return 1;
}

static PRInt32
sasl_io_read_packet(PRFileDesc *fd, PRIntn flags, PRIntervalTime timeout, PRInt32 *err)
{
    PRInt32 ret = 0;
    sasl_io_private *sp = sasl_get_io_private(fd);
    Connection *c = sp->conn;
    uint32_t bytes_remaining_to_read = sp->encrypted_buffer_count - sp->encrypted_buffer_offset;

    slapi_log_err(SLAPI_LOG_CONNS,
                  "sasl_io_read_packet", "Reading %" PRIu32" bytes for connection %" PRIu64 "\n",
                  bytes_remaining_to_read, c->c_connid);
    ret = PR_Recv(fd->lower, sp->encrypted_buffer + sp->encrypted_buffer_offset, bytes_remaining_to_read, flags, timeout);
    if (ret <= 0) {
        *err = PR_GetError();
        if (ret == 0) {
            slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_read_packet",
                          "Connection closed while reading sasl packet on connection %" PRIu64 "\n", c->c_connid);
        } else {
            slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_read_packet",
                          "Error reading sasl packet on connection %" PRIu64 " %d:%s\n",
                          c->c_connid, *err, slapd_pr_strerror(*err));
        }
        return ret;
    }
    sp->encrypted_buffer_offset += ret;
    return ret;
}

static PRInt32 PR_CALLBACK
sasl_io_recv(PRFileDesc *fd, void *buf, PRInt32 len, PRIntn flags, PRIntervalTime timeout)
{
    sasl_io_private *sp = sasl_get_io_private(fd);
    Connection *c = sp->conn;
    int32_t ret = 0;
    uint32_t bytes_in_buffer = 0;
    int32_t err = 0;

    /* Do we have decrypted data buffered from 'before' ? */
    bytes_in_buffer = sp->decrypted_buffer_count - sp->decrypted_buffer_offset;
    slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_recv",
                  "Connection %" PRIu64 " len %d bytes_in_buffer %" PRIu32 "\n",
                  c->c_connid, len, bytes_in_buffer);
    slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_recv",
                  "Connection %" PRIu64 " len %d encrypted buffer count %" PRIu32 "\n",
                  c->c_connid, len, sp->encrypted_buffer_count);
    if (0 == bytes_in_buffer) {
        /* If there wasn't buffered decrypted data, we need to get some... */
        if (!sasl_io_reading_packet(sp)) {
            /* First read the packet length and so on */
            ret = sasl_io_start_packet(fd, flags, timeout, &err);
            if (SASL_IO_BUFFER_NOT_ENCRYPTED == ret) {
                /*
                 * Special case: we received unencrypted data that was actually
                 * an unbind.  Copy it to the buffer and return its length.
                 */
                memcpy(buf, sp->encrypted_buffer, sp->encrypted_buffer_count);
                return sp->encrypted_buffer_count;
            }
            if (0 >= ret) {
                /* timeout, connection closed, or error */
                return ret;
            }
        }
        /* We now have the packet length
         * we now must read more data off the wire until we have the complete packet
         */
        ret = sasl_io_read_packet(fd, flags, timeout, &err);
        if (0 >= ret) {
            return ret; /* read packet will set pr error */
        }
        /* If we have not read the packet yet, we cannot return any decrypted data to the
         * caller - so just tell the caller we don't have enough data yet
         * this is equivalent to recv() returning EAGAIN on a non-blocking socket
         * the caller must handle this condition and poll() or similar to know
         * when more data arrives
         */
        if (!sasl_io_finished_packet(sp)) {
            slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_recv",
                          "Connection %" PRIu64 " - not finished reading packet yet\n", c->c_connid);
#if defined(EWOULDBLOCK)
            errno = EWOULDBLOCK;
#elif defined(EAGAIN)
            errno = EAGAIN;
#endif
            PR_SetError(PR_WOULD_BLOCK_ERROR, errno);
            return PR_FAILURE;
        }
        /* We have the full encrypted buffer now - decrypt it */
        {
            const char *output_buffer = NULL;
            unsigned int output_length = 0;
            slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_recv",
                          "Finished reading packet for connection %" PRIu64 "\n", c->c_connid);
            /* Now decode it */
            ret = sasl_decode(c->c_sasl_conn, sp->encrypted_buffer, sp->encrypted_buffer_count, &output_buffer, &output_length);
            /* even if decode fails, need re-initialize the encrypted_buffer */
            sp->encrypted_buffer_offset = 0;
            sp->encrypted_buffer_count = 0;
            if (SASL_OK == ret) {
                slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_recv",
                              "Decoded packet length %u for connection %" PRIu64 "\n", output_length, c->c_connid);
                if (output_length) {
                    sasl_io_resize_decrypted_buffer(sp, output_length);
                    memcpy(sp->decrypted_buffer, output_buffer, output_length);
                    sp->decrypted_buffer_count = output_length;
                    sp->decrypted_buffer_offset = 0;
                    bytes_in_buffer = output_length;
                }
            } else {
                slapi_log_err(SLAPI_LOG_ERR, "sasl_io_recv",
                              "Failed to decode packet for connection %" PRIu64 "\n", c->c_connid);
                PR_SetError(PR_IO_ERROR, 0);
                return PR_FAILURE;
            }
        }
    }
    /* Finally, return data from the buffer to the caller */
    {
        uint32_t bytes_to_return = sp->decrypted_buffer_count - sp->decrypted_buffer_offset;
        if (bytes_to_return > len) {
            bytes_to_return = len;
        }
        /* Copy data from the decrypted buffer starting at the offset */
        memcpy(buf, sp->decrypted_buffer + sp->decrypted_buffer_offset, bytes_to_return);
        if (bytes_in_buffer == bytes_to_return) {
            sp->decrypted_buffer_offset = 0;
            sp->decrypted_buffer_count = 0;
            slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_recv",
                          "All decrypted data returned for connection %" PRIu64 "\n", c->c_connid);
        } else {
            sp->decrypted_buffer_offset += bytes_to_return;
            slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_recv",
                          "Returning %" PRIu32 " bytes to caller %" PRIu32 " bytes left to return for connection %" PRIu64 "\n",
                          bytes_to_return, sp->decrypted_buffer_count - sp->decrypted_buffer_offset, c->c_connid);
        }
        ret = bytes_to_return;
    }
    if (ret > 0) {
        /* we actually read something - we can now send encrypted data */
        sp->send_encrypted = PR_TRUE;
    }
    return ret;
}

static void
reset_send_info(sasl_io_private *sp)
{
    sp->send_buffer = NULL;
    sp->send_size = 0;
    sp->send_offset = 0;
}

PRInt32
sasl_io_send(PRFileDesc *fd, const void *buf, PRInt32 amount, PRIntn flags, PRIntervalTime timeout)
{
    PRInt32 ret = 0;
    sasl_io_private *sp = sasl_get_io_private(fd);
    Connection *c = sp->conn;

    slapi_log_err(SLAPI_LOG_CONNS,
                  "sasl_io_send", "Writing %d bytes\n", amount);
    if (sp->send_encrypted) {
        /* Get SASL to encrypt the buffer */
        if (NULL == sp->send_buffer) {
            ret = sasl_encode(c->c_sasl_conn, buf, amount, &sp->send_buffer, &sp->send_size);
            if (ret != SASL_OK) {
                const char *saslerr = sasl_errdetail(c->c_sasl_conn);
                slapi_log_err(SLAPI_LOG_ERR,
                              "sasl_io_send", "Could not encode %d bytes - sasl error %s\n",
                              amount, saslerr ? saslerr : "unknown");
                reset_send_info(sp);
                PR_SetError(PR_IO_ERROR, 0);
                return PR_FAILURE;
            }
            slapi_log_err(SLAPI_LOG_CONNS,
                          "sasl_io_send", "Encoded as %d bytes\n", sp->send_size);
            sp->send_offset = 0;
        } else if ((amount > 0) && (sp->send_offset >= sp->send_size)) {
            /* something went wrong - we sent too many bytes */
            slapi_log_err(SLAPI_LOG_ERR,
                          "sasl_io_send", "Client requested to send %d bytes but we "
                                          "already sent %d bytes\n",
                          amount, (sp->send_offset >= sp->send_size));
            reset_send_info(sp);
            PR_SetError(PR_BUFFER_OVERFLOW_ERROR, EMSGSIZE);
            return PR_FAILURE;
        }
        ret = PR_Send(fd->lower, sp->send_buffer + sp->send_offset,
                      sp->send_size - sp->send_offset, flags, timeout);
        /* we need to return the amount of cleartext sent */
        if (ret == (sp->send_size - sp->send_offset)) {
            ret = amount;        /* sent amount of data requested by caller */
            reset_send_info(sp); /* done with this buffer, ready for next buffer */
        } else if (ret > 0) {    /* could not send the entire encrypted buffer - tell caller we're blocked */
            slapi_log_err(SLAPI_LOG_CONNS,
                          "sasl_io_send", "error: only sent %d of %d encoded bytes\n", ret,
                          (sp->send_size - sp->send_offset));
            sp->send_offset += ret;
            ret = PR_FAILURE;
#if defined(EWOULDBLOCK)
            errno = EWOULDBLOCK;
#elif defined(EAGAIN)
            errno = EAGAIN;
#endif
            PR_SetError(PR_WOULD_BLOCK_ERROR, errno);
        }
        /* else - ret is error - caller will handle */
    } else {
        ret = PR_Send(fd->lower, buf, amount, flags, timeout);
    }

    return ret;
}

/*
 * Need to handle cases where caller uses PR_Write instead of
 * PR_Send on the network socket
 */
static PRInt32 PR_CALLBACK
sasl_io_write(PRFileDesc *fd, const void *buf, PRInt32 amount)
{
    return sasl_io_send(fd, buf, amount, 0, PR_INTERVAL_NO_TIMEOUT);
}

static PRStatus PR_CALLBACK
sasl_pop_IO_layer(PRFileDesc *stack, int doclose)
{
    PRFileDesc *layer = NULL;
    sasl_io_private *sp = NULL;
    PRStatus rv = 0;
    PRDescIdentity id = PR_TOP_IO_LAYER;

    /* see if stack has the sasl io layer */
    if (!sasl_LayerID || !stack) {
        slapi_log_err(SLAPI_LOG_CONNS,
                      "sasl_pop_IO_layer", "No SASL IO layer\n");
        return PR_SUCCESS;
    }

    /* if we're not being called during PR_Close, then we just want to
       pop the sasl io layer if it is on the stack */
    if (!doclose) {
        id = sasl_LayerID;
        if (!PR_GetIdentitiesLayer(stack, id)) {
            slapi_log_err(SLAPI_LOG_CONNS,
                          "sasl_pop_IO_layer", "No SASL IO layer\n");
            return PR_SUCCESS;
        }
    }

    /* remove the layer from the stack */
    layer = PR_PopIOLayer(stack, id);
    if (!layer) {
        slapi_log_err(SLAPI_LOG_CONNS,
                      "sasl_pop_IO_layer", "Could not pop SASL IO layer\n");
        return PR_FAILURE;
    }

    /* get our private data and clean it up */
    sp = sasl_get_io_private(layer);

    if (sp) {
        slapi_log_err(SLAPI_LOG_CONNS,
                      "sasl_pop_IO_layer", "Removing SASL IO layer\n");
        /* Free the buffers */
        slapi_ch_free_string(&sp->encrypted_buffer);
        slapi_ch_free_string(&sp->decrypted_buffer);
        slapi_ch_free((void **)&sp);
    }
    layer->secret = NULL;
    if (layer->dtor) {
        layer->dtor(layer);
    }

    if (doclose) {
        rv = stack->methods->close(stack);
    } else {
        rv = PR_SUCCESS;
    }

    return rv;
}

static PRStatus PR_CALLBACK
closeLayer(PRFileDesc *stack)
{
    PRStatus rv = 0;
    slapi_log_err(SLAPI_LOG_CONNS,
                  "closeLayer", "Closing SASL IO layer\n");
    rv = sasl_pop_IO_layer(stack, 1 /* do close */);
    if (PR_SUCCESS != rv) {
        slapi_log_err(SLAPI_LOG_CONNS,
                      "closeLayer", "Error closing SASL IO layer\n");
        return rv;
    }

    return rv;
}

static PRStatus PR_CALLBACK
initialize(void)
{
    sasl_LayerID = PR_GetUniqueIdentity(sasl_LayerName);
    if (PR_INVALID_IO_LAYER == sasl_LayerID) {
        return PR_FAILURE;
    } else {
        const PRIOMethods *defaults = PR_GetDefaultIOMethods();
        if (!defaults) {
            return PR_FAILURE;
        } else {
            memcpy(&sasl_IoMethods, defaults, sizeof(sasl_IoMethods));
        }
    }
    /* Customize methods: */
    sasl_IoMethods.recv = sasl_io_recv;
    sasl_IoMethods.send = sasl_io_send;
    sasl_IoMethods.close = closeLayer;
    sasl_IoMethods.write = sasl_io_write; /* some code uses PR_Write instead of PR_Send */
    return PR_SUCCESS;
}

/*
 * Push the SASL I/O layer on top of the current NSPR I/O layer of the prfd used
 * by the connection.
 * must be called with the connection lock (c_mutex) held or in a condition in which
 * no other threads are accessing conn->c_prfd
 */
int
sasl_io_enable(Connection *c, void *data __attribute__((unused)))
{
    PRStatus rv = PR_CallOnce(&sasl_callOnce, initialize);
    if (PR_SUCCESS == rv) {
        PRFileDesc *layer = NULL;
        sasl_io_private *sp = NULL;

        if (c->c_flags & CONN_FLAG_CLOSING) {
            slapi_log_err(SLAPI_LOG_ERR, "sasl_io_enable",
                          "Cannot enable SASL security on connection in CLOSING state\n");
            return PR_FAILURE;
        }
        layer = PR_CreateIOLayerStub(sasl_LayerID, &sasl_IoMethods);
        sp = (sasl_io_private *)slapi_ch_calloc(1, sizeof(sasl_io_private));
        sasl_io_init_buffers(sp);
        layer->secret = sp;
        sp->conn = c;
        rv = PR_PushIOLayer(c->c_prfd, PR_TOP_IO_LAYER, layer);
        if (rv) {
            slapi_log_err(SLAPI_LOG_ERR, "sasl_io_enable",
                          "Error enabling sasl io on connection %" PRIu64 " %d:%s\n",
                          c->c_connid, rv, slapd_pr_strerror(rv));
        } else {
            slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_enable",
                          "Enabled sasl io on connection %" PRIu64 " \n", c->c_connid);
            debug_print_layers(c->c_prfd);
        }
    }
    return (int)rv;
}

/*
 * Remove the SASL I/O layer from the top of the current NSPR I/O layer of the prfd used
 * by the connection.  Must either be called within the connection lock, or be
 * called while the connection (c_prfd) is not being referenced by another thread.
 */
int
sasl_io_cleanup(Connection *c, void *data __attribute__((unused)))
{
    int ret = 0;

    slapi_log_err(SLAPI_LOG_CONNS, "sasl_io_cleanup",
                  "Connection %" PRIu64 "\n", c->c_connid);

    ret = sasl_pop_IO_layer(c->c_prfd, 0 /* do not close */);

    c->c_sasl_ssf = 0;

    return ret;
}

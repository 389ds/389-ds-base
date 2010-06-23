/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#include "slap.h"
#include "slapi-plugin.h"
#include "fe.h"
#include <sasl.h>

/*
 * I/O Shim Layer for SASL Encryption
 * The 'handle' is a pointer to a sasl_connection structure.
 */

#define SASL_IO_BUFFER_SIZE 1024 
 
/*
 * SASL sends its encrypted PDU's with an embedded 4-byte length
 * at the beginning (in network byte order). We peek inside the
 * received data off the wire to find this length, and use it
 * to determine when we have read an entire SASL PDU.
 * So when we have that there is no need for the SASL layer
 * to do any fancy buffering with it, we always hand it
 * a full packet.
 */
 
struct PRFilePrivate {
    char *decrypted_buffer;
    size_t decrypted_buffer_size;
    size_t decrypted_buffer_count;
    size_t decrypted_buffer_offset;
    char *encrypted_buffer;
    size_t encrypted_buffer_size;
    size_t encrypted_buffer_count;
    size_t encrypted_buffer_offset;
    Connection *conn; /* needed for connid and sasl_conn context */
    PRBool send_encrypted; /* can only send encrypted data after the first read -
                              that is, we cannot send back an encrypted response
                              to the bind request that established the sasl io */
    const char *send_buffer; /* encrypted buffer to send to client */
    unsigned int send_size; /* size of the encrypted buffer */
    unsigned int send_offset; /* number of bytes sent so far */    
};

typedef PRFilePrivate sasl_io_private;

static PRInt32 PR_CALLBACK
sasl_io_recv(PRFileDesc *fd, void *buf, PRInt32 len, PRIntn flags,
             PRIntervalTime timeout);

static void
debug_print_layers(PRFileDesc *fd)
{
#if 0
    PR_ASSERT(fd->higher == NULL); /* this is the topmost layer */
    while (fd) {
        PRSocketOptionData sod;
        PRInt32 err;

        LDAPDebug2Args( LDAP_DEBUG_CONNS,
                       "debug_print_layers: fd %d sasl_io_recv = %p\n",
                        PR_FileDesc2NativeHandle(fd), sasl_io_recv );
        LDAPDebug( LDAP_DEBUG_CONNS,
                   "debug_print_layers: fd name %s type = %d recv = %p\n",
                   PR_GetNameForIdentity(fd->identity),
                   PR_GetDescType(fd),
                   fd->methods->recv ? fd->methods->recv : NULL );
        sod.option = PR_SockOpt_Nonblocking;
        if (PR_FAILURE == PR_GetSocketOption(fd, &sod)) {
            err = PR_GetError();
            LDAPDebug2Args( LDAP_DEBUG_CONNS,
                            "debug_print_layers: error getting nonblocking option: %d %s\n",
                            err, slapd_pr_strerror(err) );
        } else {
            LDAPDebug1Arg( LDAP_DEBUG_CONNS,
                           "debug_print_layers: non blocking %d\n", sod.value.non_blocking );
        }
        sod.option = PR_SockOpt_Reuseaddr;
        if (PR_FAILURE == PR_GetSocketOption(fd, &sod)) {
            err = PR_GetError();
            LDAPDebug2Args( LDAP_DEBUG_CONNS,
                            "debug_print_layers: error getting reuseaddr option: %d %s\n",
                            err, slapd_pr_strerror(err) );
        } else {
            LDAPDebug1Arg( LDAP_DEBUG_CONNS,
                           "debug_print_layers: reuseaddr %d\n", sod.value.reuse_addr );
        }
        sod.option = PR_SockOpt_RecvBufferSize;
        if (PR_FAILURE == PR_GetSocketOption(fd, &sod)) {
            err = PR_GetError();
            LDAPDebug2Args( LDAP_DEBUG_CONNS,
                            "debug_print_layers: error getting recvbuffer option: %d %s\n",
                            err, slapd_pr_strerror(err) );
        } else {
            LDAPDebug1Arg( LDAP_DEBUG_CONNS,
                           "debug_print_layers: recvbuffer %d\n", sod.value.recv_buffer_size );
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


static void sasl_io_resize_encrypted_buffer(sasl_io_private *sp, size_t requested_size)
{
    if (requested_size > sp->encrypted_buffer_size) {
        sp->encrypted_buffer = slapi_ch_realloc(sp->encrypted_buffer, requested_size);
        sp->encrypted_buffer_size = requested_size;
    }
}

static void sasl_io_resize_decrypted_buffer(sasl_io_private *sp, size_t requested_size)
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
    return (sp->encrypted_buffer_count  && (sp->encrypted_buffer_offset == sp->encrypted_buffer_count) );
}

static const char* const sasl_LayerName = "SASL";
static PRDescIdentity sasl_LayerID;
static PRIOMethods sasl_IoMethods;
static PRCallOnceType sasl_callOnce = {0,0};

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
    PRInt32 ret = 0;
    unsigned char buffer[4];
    size_t packet_length = 0;
    size_t saslio_limit;
    sasl_io_private *sp = sasl_get_io_private(fd);
    Connection *c = sp->conn;

    *err = 0;
    debug_print_layers(fd);
    /* first we need the length bytes */
    ret = PR_Recv(fd->lower, buffer, sizeof(buffer), flags, timeout);
    LDAPDebug( LDAP_DEBUG_CONNS,
               "read sasl packet length returned %d on connection %" NSPRIu64 "\n", ret, c->c_connid, 0 );
    if (ret <= 0) {
        *err = PR_GetError();
        if (ret == 0) {
            LDAPDebug1Arg( LDAP_DEBUG_CONNS,
                       "sasl_io_start_packet: connection closed while reading sasl packet length on connection %" NSPRIu64 "\n", c->c_connid );
        } else {
            LDAPDebug( LDAP_DEBUG_CONNS,
                       "sasl_io_start_packet: error reading sasl packet length on connection %" NSPRIu64 " %d:%s\n", c->c_connid, *err, slapd_pr_strerror(*err) );
        }
        return ret;
    }
    /*
     * NOTE: A better way to do this would be to read the bytes and add them to 
     * sp->encrypted_buffer - if offset < 4, tell caller we didn't read enough
     * bytes yet - if offset >= 4, decode the length and proceed.  However, it
     * is highly unlikely that a request to read 4 bytes will return < 4 bytes,
     * perhaps only in error conditions, in which case the ret < 0 case above
     * will run
     */
    if (ret < sizeof(buffer)) {
        LDAPDebug( LDAP_DEBUG_ANY,
                   "sasl_io_start_packet: failed - read only %d bytes of sasl packet length on connection %" NSPRIu64 "\n", ret, c->c_connid, 0 );
	    PR_SetError(PR_IO_ERROR, 0);
	    return -1;        
    }
    if (ret == sizeof(buffer)) {
        /* Decode the length (could use ntohl here ??) */
        packet_length = buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
        /* add length itself (for Cyrus SASL library) */
        packet_length += 4;

        LDAPDebug( LDAP_DEBUG_CONNS,
                   "read sasl packet length %ld on connection %" NSPRIu64 "\n", packet_length, c->c_connid, 0 );

        /* Check if the packet length is larger than our max allowed.  A
         * setting of -1 means that we allow any size SASL IO packet. */
        saslio_limit = config_get_maxsasliosize();
        if(((long)saslio_limit != -1) && (packet_length > saslio_limit)) {
            LDAPDebug( LDAP_DEBUG_ANY,
                "SASL encrypted packet length exceeds maximum allowed limit (length=%ld, limit=%ld)."
                "  Change the nsslapd-maxsasliosize attribute in cn=config to increase limit.\n",
                 packet_length, config_get_maxsasliosize(), 0);
            PR_SetError(PR_BUFFER_OVERFLOW_ERROR, 0);
            *err = PR_BUFFER_OVERFLOW_ERROR;
            return -1;
        }

        sasl_io_resize_encrypted_buffer(sp, packet_length);
        /* Cyrus SASL implementation expects to have the length at the first 
           4 bytes */
        memcpy(sp->encrypted_buffer, buffer, 4);
        sp->encrypted_buffer_count = packet_length;
        sp->encrypted_buffer_offset = 4;
    }

    return 1;
}

static PRInt32
sasl_io_read_packet(PRFileDesc *fd, PRIntn flags, PRIntervalTime timeout, PRInt32 *err)
{
    PRInt32 ret = 0;
    sasl_io_private *sp = sasl_get_io_private(fd);
    Connection *c = sp->conn;
    size_t bytes_remaining_to_read = sp->encrypted_buffer_count - sp->encrypted_buffer_offset;

    LDAPDebug2Args( LDAP_DEBUG_CONNS,
               "sasl_io_read_packet: reading %d bytes for connection %" NSPRIu64 "\n",
               bytes_remaining_to_read,
               c->c_connid );
    ret = PR_Recv(fd->lower, sp->encrypted_buffer + sp->encrypted_buffer_offset, bytes_remaining_to_read, flags, timeout);
    if (ret <= 0) {
        *err = PR_GetError();
        if (ret == 0) {
            LDAPDebug1Arg( LDAP_DEBUG_CONNS,
                       "sasl_io_read_packet: connection closed while reading sasl packet on connection %" NSPRIu64 "\n", c->c_connid );
        } else {
            LDAPDebug( LDAP_DEBUG_CONNS,
                       "sasl_io_read_packet: error reading sasl packet on connection %" NSPRIu64 " %d:%s\n", c->c_connid, *err, slapd_pr_strerror(*err) );
        }
        return ret;
    }
    sp->encrypted_buffer_offset += ret;
    return ret;
}

static PRInt32 PR_CALLBACK
sasl_io_recv(PRFileDesc *fd, void *buf, PRInt32 len, PRIntn flags,
             PRIntervalTime timeout)
{
    sasl_io_private *sp = sasl_get_io_private(fd);
    Connection *c = sp->conn;
    PRInt32 ret = 0;
    size_t bytes_in_buffer = 0;
    PRInt32 err = 0;

    /* Do we have decrypted data buffered from 'before' ? */
    bytes_in_buffer = sp->decrypted_buffer_count - sp->decrypted_buffer_offset;
    LDAPDebug( LDAP_DEBUG_CONNS,
               "sasl_io_recv for connection %" NSPRIu64 " len %d bytes_in_buffer %d\n", c->c_connid, len, bytes_in_buffer );
    LDAPDebug( LDAP_DEBUG_CONNS,
               "sasl_io_recv for connection %" NSPRIu64 " len %d encrypted buffer count %d\n", c->c_connid, len, sp->encrypted_buffer_count );
    if (0 == bytes_in_buffer) {
        /* If there wasn't buffered decrypted data, we need to get some... */
        if (!sasl_io_reading_packet(sp)) {
            /* First read the packet length and so on */
            ret = sasl_io_start_packet(fd, flags, timeout, &err);
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
            LDAPDebug( LDAP_DEBUG_CONNS,
                       "sasl_io_recv for connection %" NSPRIu64 " - not finished reading packet yet\n", c->c_connid, 0, 0 );
            PR_SetError(PR_WOULD_BLOCK_ERROR, 0);
            return PR_FAILURE;
        }
        /* We have the full encrypted buffer now - decrypt it */
        {
            const char *output_buffer = NULL;
            unsigned int output_length = 0;
            LDAPDebug1Arg( LDAP_DEBUG_CONNS,
                       "sasl_io_recv finished reading packet for connection %" NSPRIu64 "\n", c->c_connid );
            /* Now decode it */
            ret = sasl_decode(c->c_sasl_conn,sp->encrypted_buffer,sp->encrypted_buffer_count,&output_buffer,&output_length);
            if (SASL_OK == ret) {
                LDAPDebug2Args( LDAP_DEBUG_CONNS,
                           "sasl_io_recv decoded packet length %d for connection %" NSPRIu64 "\n", output_length, c->c_connid );
                if (output_length) {
                    sasl_io_resize_decrypted_buffer(sp,output_length);
                    memcpy(sp->decrypted_buffer,output_buffer,output_length);
                    sp->decrypted_buffer_count = output_length;
                    sp->decrypted_buffer_offset = 0;
                    sp->encrypted_buffer_offset = 0;
                    sp->encrypted_buffer_count = 0;
                    bytes_in_buffer = output_length;
                }
            } else {
                LDAPDebug1Arg( LDAP_DEBUG_ANY,
                "sasl_io_recv failed to decode packet for connection %" NSPRIu64 "\n", c->c_connid );
                PR_SetError(PR_IO_ERROR, 0);
                return PR_FAILURE;
            }
        }
    }
    /* Finally, return data from the buffer to the caller */
    {
        size_t bytes_to_return = sp->decrypted_buffer_count - sp->decrypted_buffer_offset;
        if (bytes_to_return > len) {
            bytes_to_return = len;
        }
        /* Copy data from the decrypted buffer starting at the offset */
        memcpy(buf, sp->decrypted_buffer + sp->decrypted_buffer_offset, bytes_to_return);
        if (bytes_in_buffer == bytes_to_return) {
            sp->decrypted_buffer_offset = 0;
            sp->decrypted_buffer_count = 0;
            LDAPDebug1Arg( LDAP_DEBUG_CONNS,
                       "sasl_io_recv all decrypted data returned for connection %" NSPRIu64 "\n", c->c_connid );
        } else {
            sp->decrypted_buffer_offset += bytes_to_return;
            LDAPDebug( LDAP_DEBUG_CONNS,
                       "sasl_io_recv returning %d bytes to caller %d bytes left to return for connection %" NSPRIu64 "\n",
                       bytes_to_return,
                       sp->decrypted_buffer_count - sp->decrypted_buffer_offset,
                       c->c_connid );
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
sasl_io_send(PRFileDesc *fd, const void *buf, PRInt32 amount,
             PRIntn flags, PRIntervalTime timeout)
{
    PRInt32 ret = 0;
    sasl_io_private *sp = sasl_get_io_private(fd);
    Connection *c = sp->conn;

    LDAPDebug1Arg( LDAP_DEBUG_CONNS,
                   "sasl_io_send writing %d bytes\n", amount );
    if (sp->send_encrypted) {
        /* Get SASL to encrypt the buffer */
        if (NULL == sp->send_buffer) {
            ret = sasl_encode(c->c_sasl_conn, buf, amount, &sp->send_buffer, &sp->send_size);
            if (ret != SASL_OK) {
                const char *saslerr = sasl_errdetail(c->c_sasl_conn);
                LDAPDebug2Args( LDAP_DEBUG_ANY,
                                "sasl_io_send could not encode %d bytes - sasl error %s\n",
                                amount, saslerr ? saslerr : "unknown" );
                reset_send_info(sp);
                PR_SetError(PR_IO_ERROR, 0);
                return PR_FAILURE;
            }
            LDAPDebug1Arg( LDAP_DEBUG_CONNS,
                           "sasl_io_send encoded as %d bytes\n", sp->send_size );
            sp->send_offset = 0;
        } else if ((amount > 0) && (sp->send_offset >= sp->send_size)) {
            /* something went wrong - we sent too many bytes */
            LDAPDebug2Args( LDAP_DEBUG_ANY,
                           "sasl_io_send - client requested to send %d bytes but we "
                            "already sent %d bytes\n", amount, (sp->send_offset >= sp->send_size));
            reset_send_info(sp);
            PR_SetError(PR_BUFFER_OVERFLOW_ERROR, EMSGSIZE);
            return PR_FAILURE;
        }
        ret = PR_Send(fd->lower, sp->send_buffer + sp->send_offset,
                      sp->send_size - sp->send_offset, flags, timeout);
        /* we need to return the amount of cleartext sent */
        if (ret == (sp->send_size - sp->send_offset)) {
            ret = amount; /* sent amount of data requested by caller */
            reset_send_info(sp); /* done with this buffer, ready for next buffer */
        } else if (ret > 0) { /* could not send the entire encrypted buffer - tell caller we're blocked */
            LDAPDebug2Args( LDAP_DEBUG_CONNS,
                       "sasl_io_send error: only sent %d of %d encoded bytes\n", ret,
                            (sp->send_size - sp->send_offset) );
            sp->send_offset += ret;
            ret = PR_FAILURE;
            PR_SetError(PR_WOULD_BLOCK_ERROR, 0);
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
sasl_pop_IO_layer(PRFileDesc* stack, int doclose)
{
    PRFileDesc* layer = NULL;
    sasl_io_private *sp = NULL;
    PRStatus rv = 0;
    PRDescIdentity id = PR_TOP_IO_LAYER;

    /* see if stack has the sasl io layer */
    if (!sasl_LayerID || !stack) {
        LDAPDebug0Args( LDAP_DEBUG_CONNS,
                        "sasl_pop_IO_layer: no SASL IO layer\n" );
        return PR_SUCCESS;
    }

    /* if we're not being called during PR_Close, then we just want to
       pop the sasl io layer if it is on the stack */
    if (!doclose) {
        id = sasl_LayerID;
        if (!PR_GetIdentitiesLayer(stack, id)) {
            LDAPDebug0Args( LDAP_DEBUG_CONNS,
                            "sasl_pop_IO_layer: no SASL IO layer\n" );
            return PR_SUCCESS;
        }
    }

    /* remove the layer from the stack */
    layer = PR_PopIOLayer(stack, id);
    if (!layer) {
        LDAPDebug0Args( LDAP_DEBUG_CONNS,
                        "sasl_pop_IO_layer: error - could not pop SASL IO layer\n" );
        return PR_FAILURE;
    }

    /* get our private data and clean it up */
    sp = sasl_get_io_private(layer);

    if (sp) {
        LDAPDebug0Args( LDAP_DEBUG_CONNS,
                        "sasl_pop_IO_layer: removing SASL IO layer\n" );
        /* Free the buffers */
        slapi_ch_free_string(&sp->encrypted_buffer);
        slapi_ch_free_string(&sp->decrypted_buffer);
        slapi_ch_free((void**)&sp);
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
closeLayer(PRFileDesc* stack)
{
    PRStatus rv = 0;
    LDAPDebug0Args( LDAP_DEBUG_CONNS,
                    "closeLayer: closing SASL IO layer\n" );
    rv = sasl_pop_IO_layer(stack, 1 /* do close */);
    if (PR_SUCCESS != rv) {
        LDAPDebug0Args( LDAP_DEBUG_CONNS,
                    "closeLayer: error closing SASL IO layer\n" );
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
        const PRIOMethods* defaults = PR_GetDefaultIOMethods();
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
sasl_io_enable(Connection *c, void *data /* UNUSED */)
{
    PRStatus rv = PR_CallOnce(&sasl_callOnce, initialize);
    if (PR_SUCCESS == rv) {
        PRFileDesc* layer = NULL;
        sasl_io_private *sp = NULL;

        if ( c->c_flags & CONN_FLAG_CLOSING ) {
            slapi_log_error( SLAPI_LOG_FATAL, "sasl_io_enable",
                             "Cannot enable SASL security on connection in CLOSING state\n");
            return PR_FAILURE;
        }
        layer = PR_CreateIOLayerStub(sasl_LayerID, &sasl_IoMethods);
        sp = (sasl_io_private*) slapi_ch_calloc(1, sizeof(sasl_io_private));
        sasl_io_init_buffers(sp);
        layer->secret = sp;
        sp->conn = c;
        rv = PR_PushIOLayer(c->c_prfd, PR_TOP_IO_LAYER, layer);
        if (rv) {
            LDAPDebug( LDAP_DEBUG_ANY,
                       "sasl_io_enable: error enabling sasl io on connection %" NSPRIu64 " %d:%s\n", c->c_connid, rv, slapd_pr_strerror(rv) );
        } else {
            LDAPDebug( LDAP_DEBUG_CONNS,
                       "sasl_io_enable: enabled sasl io on connection %" NSPRIu64 " \n", c->c_connid, 0, 0 );
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
sasl_io_cleanup(Connection *c, void *data /* UNUSED */)
{
    int ret = 0;

    LDAPDebug( LDAP_DEBUG_CONNS,
               "sasl_io_cleanup for connection %" NSPRIu64 "\n", c->c_connid, 0, 0 );

    ret = sasl_pop_IO_layer(c->c_prfd, 0 /* do not close */);

    c->c_sasl_ssf = 0;

    return ret;
}

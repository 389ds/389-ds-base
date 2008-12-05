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
 
struct _sasl_io_private {
    struct lextiof_socket_private *real_handle;
    struct lber_x_ext_io_fns *real_iofns;
    char *decrypted_buffer;
    size_t decrypted_buffer_size;
    size_t decrypted_buffer_count;
    size_t decrypted_buffer_offset;
    char *encrypted_buffer;
    size_t encrypted_buffer_size;
    size_t encrypted_buffer_count;
    size_t encrypted_buffer_offset;
    Connection *conn;    
};

int
sasl_io_enable(Connection *c)
{
    int ret = 0;

    LDAPDebug( LDAP_DEBUG_CONNS,
                "sasl_io_enable for connection %" NSPRIu64 "\n", c->c_connid, 0, 0 );
    /* Flag that we should enable SASL I/O for the next read operation on this connection */
    c->c_enable_sasl_io = 1;
    
    return ret;
}

static void
sasl_io_init_buffers(sasl_io_private *sp)
{
    sp->decrypted_buffer = slapi_ch_malloc(SASL_IO_BUFFER_SIZE);
    sp->decrypted_buffer_size = SASL_IO_BUFFER_SIZE;
    sp->encrypted_buffer = slapi_ch_malloc(SASL_IO_BUFFER_SIZE);
    sp->encrypted_buffer_size = SASL_IO_BUFFER_SIZE;
}

/* This function should be called under the connection mutex */
int
sasl_io_setup(Connection *c)
{
    int ret = 0;
    struct lber_x_ext_io_fns func_pointers = {0};
    struct lber_x_ext_io_fns *real_iofns = (struct lber_x_ext_io_fns *) slapi_ch_malloc(LBER_X_EXTIO_FNS_SIZE);
    sasl_io_private *sp = (sasl_io_private*) slapi_ch_calloc(1, sizeof(sasl_io_private));

    LDAPDebug( LDAP_DEBUG_CONNS,
                "sasl_io_setup for connection %" NSPRIu64 "\n", c->c_connid, 0, 0 );
    /* Get the current functions and store them for later */
    real_iofns->lbextiofn_size = LBER_X_EXTIO_FNS_SIZE;
    ber_sockbuf_get_option( c->c_sb, LBER_SOCKBUF_OPT_EXT_IO_FNS, real_iofns );
    sp->real_iofns = real_iofns; /* released in sasl_io_cleanup */

    /* Set up the private structure */
    sp->real_handle = (struct lextiof_socket_private*) c->c_prfd;
    sp->conn = c;
    /* Store the private structure in the connection */
    c->c_sasl_io_private = sp;
    /* Insert the sasl i/o functions into the ber layer */
    func_pointers.lbextiofn_size = LBER_X_EXTIO_FNS_SIZE;
    func_pointers.lbextiofn_read = sasl_read_function;
    func_pointers.lbextiofn_write = sasl_write_function;
    func_pointers.lbextiofn_writev = NULL;
    func_pointers.lbextiofn_socket_arg = (struct lextiof_socket_private *) sp;
    ret = ber_sockbuf_set_option( c->c_sb, LBER_SOCKBUF_OPT_EXT_IO_FNS, &func_pointers);
    /* Setup the data buffers for the fast read path */
    sasl_io_init_buffers(sp);
    /* Reset the enable flag, so we don't process it again */
    c->c_enable_sasl_io = 0;
    /* Mark the connection as having SASL I/O */
    c->c_sasl_io = 1;
    return ret;
}

int
sasl_io_cleanup(Connection *c)
{
    int ret = 0;
    sasl_io_private *sp = c->c_sasl_io_private;
    if (sp) {
        LDAPDebug( LDAP_DEBUG_CONNS,
                "sasl_io_cleanup for connection %" NSPRIu64 "\n", c->c_connid, 0, 0 );
        /* Free the buffers */
        slapi_ch_free((void**)&(sp->encrypted_buffer));
        slapi_ch_free((void**)&(sp->decrypted_buffer));
        /* Put the I/O functions back how they were */
        if (NULL != sp->real_iofns) {
            ber_sockbuf_set_option( c->c_sb, LBER_SOCKBUF_OPT_EXT_IO_FNS, sp->real_iofns );
            slapi_ch_free((void**)&(sp->real_iofns));
        }
        slapi_ch_free((void**)&sp);
        c->c_sasl_io_private = NULL;
        c->c_enable_sasl_io = 0;
        c->c_sasl_io = 0;
        c->c_sasl_ssf = 0;
    }
    return ret;
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

static int
sasl_io_start_packet(Connection *c, PRInt32 *err)
{
    int ret = 0;
    unsigned char buffer[4];
    size_t packet_length = 0;
    size_t saslio_limit;
    
    ret = PR_Recv(c->c_prfd,buffer,sizeof(buffer),0,PR_INTERVAL_NO_WAIT);
    if (ret < 0) {
        *err = PR_GetError();
        return -1;
    }
    if (ret != 0 && ret < sizeof(buffer)) {
        LDAPDebug( LDAP_DEBUG_ANY,
            "failed to read sasl packet length on connection %" NSPRIu64 "\n", c->c_connid, 0, 0 );
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
            return -1;
        }

        sasl_io_resize_encrypted_buffer(c->c_sasl_io_private, packet_length);
        /* Cyrus SASL implementation expects to have the length at the first 
           4 bytes */
        memcpy(c->c_sasl_io_private->encrypted_buffer, buffer, 4);
        c->c_sasl_io_private->encrypted_buffer_count = packet_length;
        c->c_sasl_io_private->encrypted_buffer_offset = 4;
    }
    return 0;
}
static int
sasl_io_read_packet(Connection *c, PRInt32 *err)
{
    PRInt32 ret = 0;
    sasl_io_private *sp = c->c_sasl_io_private;
    size_t bytes_remaining_to_read = sp->encrypted_buffer_count - sp->encrypted_buffer_offset;

    ret = PR_Recv(c->c_prfd,sp->encrypted_buffer + sp->encrypted_buffer_offset,bytes_remaining_to_read,0,PR_INTERVAL_NO_WAIT);
    if (ret < 0) {
        *err = PR_GetError();
        return -1;
    }
    if (ret > 0) {
        sp->encrypted_buffer_offset += ret;
    }
    return ret;
}

/* Special recv function for the server connection code */
/* Here, we return bytes to the caller, either the bytes
   remaining in the decrypted data buffer, from 'before',
   or the number of bytes we get decrypted from sasl,
   or the requested number of bytes whichever is lower.
 */
int
sasl_recv_connection(Connection *c, char *buffer, size_t count,PRInt32 *err)
{
    int ret = 0;
    size_t bytes_in_buffer = 0;
    sasl_io_private *sp = c->c_sasl_io_private;

    *err = 0;
    LDAPDebug( LDAP_DEBUG_CONNS,
                "sasl_recv_connection for connection %" NSPRIu64 "\n", c->c_connid, 0, 0 );
    /* Do we have decrypted data buffered from 'before' ? */
    bytes_in_buffer = sp->decrypted_buffer_count - sp->decrypted_buffer_offset;
    if (0 == bytes_in_buffer) {
        /* If there wasn't buffered decrypted data, we need to get some... */
        if (!sasl_io_reading_packet(sp)) {
            /* First read the packet length and so on */
            ret = sasl_io_start_packet(c, err);
            if (0 != ret) {
                /* Most likely the i/o timed out */
                return ret;
            }
        }
        /* We now have the packet length
         * we now must read more data off the wire until we have the complete packet
         */
        do {
            ret = sasl_io_read_packet(c,err);
            if (0 == ret || -1 == ret) {
                return ret;
            }
        } while (!sasl_io_finished_packet(sp));
        /* We are there. */
        {
            const char *output_buffer = NULL;
            unsigned int output_length = 0;
            LDAPDebug( LDAP_DEBUG_CONNS,
            "sasl_recv_connection finished reading packet for connection %" NSPRIu64 "\n", c->c_connid, 0, 0 );
            /* Now decode it */
            ret = sasl_decode(c->c_sasl_conn,sp->encrypted_buffer,sp->encrypted_buffer_count,&output_buffer,&output_length);
            if (SASL_OK == ret) {
                LDAPDebug( LDAP_DEBUG_CONNS,
                "sasl_recv_connection decoded packet length %d for connection %" NSPRIu64 "\n", output_length, c->c_connid, 0 );
                if (output_length) {
                    sasl_io_resize_decrypted_buffer(sp,output_length);
                    memcpy(sp->decrypted_buffer,output_buffer,output_length);
                    sp->decrypted_buffer_count = output_length;
                    sp->decrypted_buffer_offset = 0;
                    sp->encrypted_buffer_offset = 0;
                    sp->encrypted_buffer_count = 0;
                }
            } else {
                LDAPDebug( LDAP_DEBUG_ANY,
                "sasl_recv_connection failed to decode packet for connection %" NSPRIu64 "\n", c->c_connid, 0, 0 );
            }
        }
    }
    /* Finally, return data from the buffer to the caller */
    {
        size_t bytes_to_return = sp->decrypted_buffer_count - sp->decrypted_buffer_offset;
        if (bytes_to_return > count) {
            bytes_to_return = count;
        }
        /* Copy data from the decrypted buffer starting at the offset */
        memcpy(buffer, sp->decrypted_buffer + sp->decrypted_buffer_offset, bytes_to_return);
        if (bytes_in_buffer == bytes_to_return) {
            sp->decrypted_buffer_offset = 0;
            sp->decrypted_buffer_count = 0;
            } else {
                sp->decrypted_buffer_offset += bytes_to_return;
        }
        ret = bytes_to_return;
    }
    return ret;
}
         
int
sasl_read_function(int ignore, void *buffer, int count, struct lextiof_socket_private *handle )
{
    int ret = 0;
    sasl_io_private *sp = (sasl_io_private*) handle;

    /* First we look to see if we have buffered data that we can return to the caller */
    if ( (NULL == sp->decrypted_buffer) || ((sp->decrypted_buffer_count - sp->decrypted_buffer_offset) <= 0) )  {
        /* If we didn't have buffered data, we need to perform I/O and decrypt */
        PRUint32 buffer_length = 0;
        /* Read the packet length */
        ret = read_function(0, &buffer_length, sizeof(buffer_length), sp->real_handle);
        if (ret) {
        }
        /* Read the payload */
        ret = read_function(0, sp->encrypted_buffer, buffer_length, sp->real_handle);
        if (ret) {
        }
        /* Now we can call sasl to decrypt */
        /* ret = sasl_decode(sp->conn->c_sasl_conn,sp->encrypted_buffer, buffer_length, sp->decrypted_buffer, &sp->decrypted_buffer_count ); */
    }
    /* If things went well, copy the payload for the caller */
    if ( 0 == ret ) {
/*        size_t real_count = 0;

        if (count >= (sp->buffer_count - sp->buffer_offset) ) {
            real_count = count;
        } else {
            real_count = (sp->buffer_count - sp->buffer_offset);
        }
        memcpy(buffer, sp->buffer, real_count);
        sp->buffer_offset += real_count;  */
    }
    
    return ret;
}

int
sasl_write_function(int ignore, const void *buffer, int count, struct lextiof_socket_private *handle)
{
    int ret = 0;
    sasl_io_private *sp = (sasl_io_private*) handle;
    const char *crypt_buffer = NULL;
    unsigned crypt_buffer_size = 0;

    LDAPDebug( LDAP_DEBUG_CONNS,
                "sasl_write_function writing %d bytes\n", count, 0, 0 );
    /* Get SASL to encrypt the buffer */
    ret = sasl_encode(sp->conn->c_sasl_conn, buffer, count, &crypt_buffer, &crypt_buffer_size);
    LDAPDebug( LDAP_DEBUG_CONNS,
                "sasl_write_function encoded as %d bytes\n", crypt_buffer_size, 0, 0 );

    ret = write_function(0, crypt_buffer, crypt_buffer_size, sp->real_handle);
    if (ret) {
    }
    
    return ret;
}
    

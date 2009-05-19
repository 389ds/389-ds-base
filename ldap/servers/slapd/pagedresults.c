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
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "slap.h"

/*
 * Parse the value from an LDAPv3 "Simple Paged Results" control.  They look
 * like this:
 * 
 *   realSearchControlValue ::= SEQUENCE {
 *   size INTEGER (0..maxInt),
 *   -- requested page size from client
 *   -- result set size estimate from server
 *   cookie OCTET STRING
 *   }
 *
 * Return an LDAP error code (LDAP_SUCCESS if all goes well).
 */
int
pagedresults_parse_control_value( struct berval *psbvp,
                                  ber_int_t *pagesize, int *curr_search_count )
{
    int rc = LDAP_SUCCESS;
    struct berval cookie = {0};

    if ( NULL == pagesize || NULL == curr_search_count ) {
        return LDAP_OPERATIONS_ERROR;
    }

    if ( psbvp->bv_len == 0 || psbvp->bv_val == NULL )
    {
        rc = LDAP_PROTOCOL_ERROR;
    }
    else
    {
        BerElement *ber = ber_init( psbvp );
        if ( ber == NULL )
        {
            rc = LDAP_OPERATIONS_ERROR;
        }
        else
        {
            if ( ber_scanf( ber, "{io}", pagesize, &cookie ) == LBER_ERROR )
            {
                rc = LDAP_PROTOCOL_ERROR;
            }
            /* the ber encoding is no longer needed */
            ber_free(ber, 1);
            if ( cookie.bv_len <= 0 ) {
                *curr_search_count = 0;
            } else {
                /* not an error */
                char *ptr = slapi_ch_malloc(cookie.bv_len + 1);
                memcpy(ptr, cookie.bv_val, cookie.bv_len);
                *(ptr+cookie.bv_len) = '\0';
                *curr_search_count = strtol(ptr, NULL, 10);
                slapi_ch_free_string(&ptr);
            }
            slapi_ch_free((void **)&cookie.bv_val);
        }
    }

    return rc;
}

/*
 * controlType = LDAP_CONTROL_PAGEDRESULTS;
 * criticality = n/a;
 * controlValue:
 *   realSearchControlValue ::= SEQUENCE {
 *   size INTEGER (0..maxInt),
 *   -- requested page size from client
 *   -- result set size estimate from server
 *   cookie OCTET STRING
 *   }
 */
void
pagedresults_set_response_control( Slapi_PBlock *pb, int iscritical,
                                    ber_int_t pagesize, int curr_search_count )
{
    LDAPControl **resultctrls = NULL;
    LDAPControl pr_respctrl;
    BerElement *ber = NULL;
    struct berval *berval = NULL;
    char *cookie_str = NULL;
    int found = 0;
    int i;

    if ( (ber = der_alloc()) == NULL )
    {
        goto bailout;
    }

    /* begin sequence, payload, end sequence */
    if (curr_search_count < 0) {
        cookie_str = slapi_ch_strdup("");
    } else {
        cookie_str = slapi_ch_smprintf("%d", curr_search_count);
    }
    ber_printf ( ber, "{io}", pagesize, cookie_str, strlen(cookie_str) );
    if ( ber_flatten ( ber, &berval ) != LDAP_SUCCESS )
    {
        goto bailout;
    }
    pr_respctrl.ldctl_oid = LDAP_CONTROL_PAGEDRESULTS;
    pr_respctrl.ldctl_iscritical = iscritical;
    pr_respctrl.ldctl_value.bv_val = berval->bv_val;
    pr_respctrl.ldctl_value.bv_len = berval->bv_len;

    slapi_pblock_get ( pb, SLAPI_RESCONTROLS, &resultctrls );
    for (i = 0; resultctrls && resultctrls[i]; i++)
    {
        if (strcmp(resultctrls[i]->ldctl_oid, LDAP_CONTROL_PAGEDRESULTS) == 0)
        {
            /*
             * We get here if search returns more than one entry
             * and this is not the first entry.
             */
            ldap_control_free ( resultctrls[i] );
            resultctrls[i] = slapi_dup_control (&pr_respctrl);
            found = 1;
            break;
        }
    }

    if ( !found )
    {
        /* slapi_pblock_set() will dup the control */
        slapi_pblock_set ( pb, SLAPI_ADD_RESCONTROL, &pr_respctrl );
    }

bailout:
    slapi_ch_free_string(&cookie_str);
    ber_free ( ber, 1 );    /* ber_free() checks for NULL param */
    ber_bvfree ( berval );    /* ber_bvfree() checks for NULL param */
}

/* setters and getters for the connection */
Slapi_Backend *
pagedresults_get_current_be(Connection *conn)
{
    Slapi_Backend *be = NULL;
    if (conn) {
        PR_Lock(conn->c_mutex);
        be = conn->c_current_be;
        PR_Unlock(conn->c_mutex);
    }
    return be;
}

int
pagedresults_set_current_be(Connection *conn, Slapi_Backend *be)
{
    int rc = -1;
    if (conn) {
        PR_Lock(conn->c_mutex);
        conn->c_current_be = be;
        PR_Unlock(conn->c_mutex);
        rc = 0;
    }
    return rc;
}

void *
pagedresults_get_search_result(Connection *conn)
{
    void *sr = NULL;
    if (conn) {
        PR_Lock(conn->c_mutex);
        sr = conn->c_search_result_set;
        PR_Unlock(conn->c_mutex);
    }
    return sr;
}

int
pagedresults_set_search_result(Connection *conn, void *sr)
{
    int rc = -1;
    if (conn) {
        PR_Lock(conn->c_mutex);
        conn->c_search_result_set = sr;
        PR_Unlock(conn->c_mutex);
        rc = 0;
    }
    return rc;
}

int
pagedresults_get_search_result_count(Connection *conn)
{
    int count = 0;
    if (conn) {
        PR_Lock(conn->c_mutex);
        count = conn->c_search_result_count;
        PR_Unlock(conn->c_mutex);
    }
    return count;
}

int
pagedresults_set_search_result_count(Connection *conn, int count)
{
    int rc = -1;
    if (conn) {
        PR_Lock(conn->c_mutex);
        conn->c_search_result_count = count;
        PR_Unlock(conn->c_mutex);
        rc = 0;
    }
    return rc;
}

int
pagedresults_get_with_sort(Connection *conn)
{
    int flags = 0;
    if (conn) {
        PR_Lock(conn->c_mutex);
        flags = conn->c_flags&CONN_FLAG_PAGEDRESULTS_WITH_SORT;
        PR_Unlock(conn->c_mutex);
    }
    return flags;
}

int
pagedresults_set_with_sort(Connection *conn, int flags)
{
    int rc = -1;
    if (conn) {
        PR_Lock(conn->c_mutex);
        if (flags & OP_FLAG_SERVER_SIDE_SORTING)
          conn->c_flags |= CONN_FLAG_PAGEDRESULTS_WITH_SORT;
        PR_Unlock(conn->c_mutex);
        rc = 0;
    }
    return rc;
}

int
pagedresults_get_sort_result_code(Connection *conn)
{
    int code = 0;
    if (conn) {
        PR_Lock(conn->c_mutex);
        code = conn->c_sort_result_code;
        PR_Unlock(conn->c_mutex);
    }
    return code;
}

int
pagedresults_set_sort_result_code(Connection *conn, int code)
{
    int rc = -1;
    if (conn) {
        PR_Lock(conn->c_mutex);
        conn->c_sort_result_code = code;
        PR_Unlock(conn->c_mutex);
        rc = 0;
    }
    return rc;
}

int
pagedresults_set_timelimit(Connection *conn, time_t timelimit)
{
    int rc = -1;
    if (conn) {
        PR_Lock(conn->c_mutex);
        conn->c_timelimit = timelimit;
        PR_Unlock(conn->c_mutex);
        rc = 0;
    }
    return rc;
}


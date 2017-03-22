/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* Connection Table */

#include "fe.h"

Connection_Table *
connection_table_new(int table_size)
{
	Connection_Table *ct;
	int i = 0;
	ber_len_t maxbersize = config_get_maxbersize();

	ct= (Connection_Table*)slapi_ch_calloc( 1, sizeof(Connection_Table) );
	ct->size= table_size;
	ct->c = (Connection *)slapi_ch_calloc( 1, table_size * sizeof(Connection) );
	ct->fd = (struct POLL_STRUCT *)slapi_ch_calloc(1, table_size * sizeof(struct POLL_STRUCT));
	ct->table_mutex = PR_NewLock();

	/* We rely on the fact that we called calloc, which zeros the block, so we don't
	 * init any structure element unless a zero value is troublesome later 
	 */
	for ( i = 0; i < table_size; i++ )
	{
		LBER_SOCKET invalid_socket;
		/* DBDB---move this out of here once everything works */
		ct->c[i].c_sb = ber_sockbuf_alloc();
		invalid_socket = SLAPD_INVALID_SOCKET;
		ct->c[i].c_sd = SLAPD_INVALID_SOCKET;
#if defined(USE_OPENLDAP)
		ber_sockbuf_ctrl( ct->c[i].c_sb, LBER_SB_OPT_SET_FD, &invalid_socket );
		ber_sockbuf_ctrl( ct->c[i].c_sb, LBER_SB_OPT_SET_MAX_INCOMING, &maxbersize );
#else
		ber_sockbuf_set_option( ct->c[i].c_sb, LBER_SOCKBUF_OPT_DESC, &invalid_socket );
		/* openldap by default does not use readahead - the implementation is
		   via a sockbuf_io layer */
		ber_sockbuf_set_option( ct->c[i].c_sb, LBER_SOCKBUF_OPT_NO_READ_AHEAD, LBER_OPT_ON );
		ber_sockbuf_set_option( ct->c[i].c_sb, LBER_SOCKBUF_OPT_MAX_INCOMING_SIZE, &maxbersize );
#endif /* !USE_OPENLDAP */
		/* all connections start out invalid */
		ct->fd[i].fd = SLAPD_INVALID_SOCKET;

		/* The connection table has a double linked list running through it.
		 * This is used to find out which connections should be looked at
		 * in the poll loop.  Slot 0 in the table is always the head of 
		 * the linked list.  Each slot has a c_next and c_prev which are
		 * pointers back into the array of connection slots. */
		ct->c[i].c_next = NULL;
		ct->c[i].c_prev = NULL;
		ct->c[i].c_ci= i;
		ct->c[i].c_fdi= SLAPD_INVALID_SOCKET_INDEX;
	}
	return ct;
}

void connection_table_free(Connection_Table *ct)
{
	int i;
	for (i = 0; i < ct->size; i++)
	{
		/* Free the contents of the connection structure */
		Connection *c= &(ct->c[i]);
		connection_done(c);
	}
	slapi_ch_free((void**)&ct->c);
	slapi_ch_free((void**)&ct->fd);
	PR_DestroyLock(ct->table_mutex);
	slapi_ch_free((void**)&ct);
}

void
connection_table_abandon_all_operations(Connection_Table *ct)
{
	int	i;
	for ( i = 0; i < ct->size; i++ )
	{
		if ( ct->c[i].c_mutex )
		{
			PR_EnterMonitor(ct->c[i].c_mutex);
			connection_abandon_operations( &ct->c[i] );
			PR_ExitMonitor(ct->c[i].c_mutex);
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
	Connection *c= NULL;
    int index, count;

    index = sd % ct->size;
    for( count = 0; count < ct->size; count++, index = (index + 1) % ct->size)
    {
        /* Do not use slot 0, slot 0 is head of the list of active connections */
        if ( index == 0 )
        {
		    continue;
		}
		else if( ct->c[index].c_mutex == NULL )
		{
		    break;
        }
	
		if( connection_is_free (&(ct->c[index])))
		{
		    break;
		}
    }
    
    if ( count < ct->size )
    {
        /* Found an available Connection */
        c= &(ct->c[index]);
		PR_ASSERT(c->c_next==NULL);
		PR_ASSERT(c->c_prev==NULL);
		PR_ASSERT(c->c_extension==NULL);
		if ( c->c_mutex == NULL )
		{
			PR_Lock( ct->table_mutex );
			c->c_mutex = PR_NewMonitor();
			c->c_pdumutex = PR_NewLock();
			PR_Unlock( ct->table_mutex );
			if ( c->c_mutex == NULL || c->c_pdumutex == NULL )
			{
				c->c_mutex = NULL;
				c->c_pdumutex = NULL;
				LDAPDebug( LDAP_DEBUG_ANY,"PR_NewLock failed\n",0, 0, 0 );
				exit(1);
			}
		}
		/* Let's make sure there's no cruft left on there from the last time this connection was used. */
		/* Note: no need to lock c->c_mutex because this function is only
		 * called by one thread (the slapd_daemon thread), and if we got this
		 * far then `c' is not being used by any operation threads, etc.
		 */
		connection_cleanup(c);
#ifdef ENABLE_NUNC_STANS
		/* NOTE - ok to do this here even if enable_nunc_stans is off */
		c->c_ct = ct; /* pointer to connection table that owns this connection */
#endif
    }
    else
    {
        /* couldn't find a Connection */
        LDAPDebug( LDAP_DEBUG_CONNS, "max open connections reached\n", 0, 0, 0);
    }
	return c;
}

/* active connection iteration functions */

Connection* 
connection_table_get_first_active_connection (Connection_Table *ct)
{
    return ct->c[0].c_next;
}

Connection* 
connection_table_get_next_active_connection (Connection_Table *ct, Connection *c)
{
    return c->c_next;
}

int connection_table_iterate_active_connections(Connection_Table *ct, void* arg, Connection_Table_Iterate_Function f)
{
	/* Locking in this area seems rather undeveloped, I think because typically only one
	 * thread accesses the connection table (daemon thread). But now we allow other threads
	 * to iterate over the table. So we use the "new mutex mutex" to lock the table.
	 */
	Connection *current_conn = NULL;
	int ret = 0;
	PR_Lock(ct->table_mutex);
	current_conn = connection_table_get_first_active_connection (ct);
	while (current_conn) {
		ret = f(current_conn, arg);
		if (ret) {
			break;
		}
		current_conn = connection_table_get_next_active_connection (ct, current_conn);
	}
	PR_Unlock(ct->table_mutex);
	return ret;
}

#ifdef FOR_DEBUGGING
static void 
connection_table_dump_active_connection (Connection *c)
{
    slapi_log_error(SLAPI_LOG_FATAL, "connection", "conn=%p fd=%d refcnt=%d c_flags=%d\n"
			        "mutex=%p next=%p prev=%p\n\n",  c, c->c_sd, c->c_refcnt, c->c_flags,
                    c->c_mutex, c->c_next, c->c_prev);
}

static void 
connection_table_dump_active_connections (Connection_Table *ct)
{
    Connection* c;

	PR_Lock(ct->table_mutex);
    slapi_log_error(SLAPI_LOG_FATAL, "connection", "********** BEGIN DUMP ************\n");
    c = connection_table_get_first_active_connection (ct);
    while (c)
    {
        connection_table_dump_active_connection (c);
        c = connection_table_get_next_active_connection (ct, c);
    }

    slapi_log_error(SLAPI_LOG_FATAL, "connection", "********** END DUMP ************\n");
	PR_Unlock(ct->table_mutex);
}
#endif

/*
 * There's a double linked list of active connections running through the array
 * of connections. This function removes a connection (by index) from that
 * list. This list is used to find the connections that should be used in the
 * poll call. 
 */
int
connection_table_move_connection_out_of_active_list(Connection_Table *ct,Connection *c)
{
    int c_sd; /* for logging */
    /* we always have previous element because list contains a dummy header */;
    PR_ASSERT (c->c_prev);

#ifdef FOR_DEBUGGING
    slapi_log_error(SLAPI_LOG_FATAL, "connection", "Moving connection out of active list\n");
    connection_table_dump_active_connection (c);
#endif

    c_sd = c->c_sd;
    /*
     * Note: the connection will NOT be moved off the active list if any other threads still hold
     * a reference to the connection (that is, its reference count must be 1 or less).
     */
    if(c->c_refcnt > 1) {
	    LDAPDebug2Args(LDAP_DEBUG_CONNS,
		           "not moving conn %d out of active list because refcnt is %d\n",
		           c_sd, c->c_refcnt);
	    return 1; /* failed */
    }
	
    /* We need to lock here because we're modifying the linked list */
    PR_Lock(ct->table_mutex);

    c->c_prev->c_next = c->c_next;

    if (c->c_next)
    {
        c->c_next->c_prev = c->c_prev;
    }

    connection_release_nolock (c);

    connection_cleanup (c);

    PR_Unlock(ct->table_mutex);

    LDAPDebug1Arg(LDAP_DEBUG_CONNS, "moved conn %d out of active list and freed\n", c_sd);
	
#ifdef FOR_DEBUGGING
    connection_table_dump_active_connections (ct);
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
connection_table_move_connection_on_to_active_list(Connection_Table *ct,Connection *c)
{
	PR_ASSERT(c->c_next==NULL);
	PR_ASSERT(c->c_prev==NULL);

	PR_Lock(ct->table_mutex);

	if (connection_acquire_nolock (c)) {
		PR_ASSERT(0);
	}

#ifdef FOR_DEBUGGING
    slapi_log_error(SLAPI_LOG_FATAL, "connection", "Moving connection into active list\n");
    connection_table_dump_active_connection (c);
#endif

	c->c_next = ct->c[0].c_next;
	if ( c->c_next != NULL )
	{
		c->c_next->c_prev = c;
	}
	c->c_prev = &(ct->c[0]);
	ct->c[0].c_next = c;

    PR_Unlock(ct->table_mutex);

#ifdef FOR_DEBUGGING
	connection_table_dump_active_connections (ct);
#endif
}

/*
 * Replace the following attributes within the entry 'e' with
 * information about the connection table:
 *    connection			// multivalued; one value for each active connection
 *    currentconnections	// single valued; an integer count
 *    totalconnections		// single valued; an integer count
 *    dtablesize			// single valued; an integer size
 *    readwaiters			// single valued; an integer count
 */
void
connection_table_as_entry(Connection_Table *ct, Slapi_Entry *e)
{
	char buf[BUFSIZ];
	char maxthreadbuf[BUFSIZ];
	struct berval val;
	struct berval	*vals[2];
	int	i, nconns, nreadwaiters;
	struct tm utm;

	vals[0] = &val;
	vals[1] = NULL;

	attrlist_delete( &e->e_attrs, "connection");
	nconns = 0;
	nreadwaiters = 0;
	for ( i = 0; i < (ct!=NULL?ct->size:0); i++ )
	{
		PR_Lock( ct->table_mutex );
		if ( (ct->c[i].c_mutex == NULL) || (ct->c[i].c_mutex == (PRMonitor*)-1) )
		{
			PR_Unlock( ct->table_mutex );
			continue;
		}
		/* Can't take c_mutex if holding table_mutex; temporarily unlock */ 
		PR_Unlock( ct->table_mutex );

		PR_EnterMonitor(ct->c[i].c_mutex);
		if ( ct->c[i].c_sd != SLAPD_INVALID_SOCKET )
		{
			char buf2[20];
			int lendn = ct->c[i].c_dn ? strlen(ct->c[i].c_dn) : 6; /* "NULLDN" */
			char *bufptr = &buf[0];
			char *newbuf = NULL;
			int maxthreadstate = 0;

			if(ct->c[i].c_flags & CONN_FLAG_MAX_THREADS){
				maxthreadstate = 1;
			}

			nconns++;
			if ( ct->c[i].c_gettingber )
			{
				nreadwaiters++;
			}

			gmtime_r( &ct->c[i].c_starttime, &utm );
			strftime( buf2, sizeof(buf2), "%Y%m%d%H%M%SZ", &utm );

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
			snprintf(maxthreadbuf, sizeof(maxthreadbuf), "%d:%"NSPRIu64":%"NSPRIu64"",
				maxthreadstate, ct->c[i].c_maxthreadscount,
				ct->c[i].c_maxthreadsblocked);

			if ((lendn + strlen(maxthreadbuf)) > (BUFSIZ - 46)) {
				/*
				 * 46 = 4 for the "i" couter + 20 for buf2 +
				 *     10 for c_opsinitiated + 10 for c_opscompleted +
				 *      1 for c_gettingber + 1
				 */
				newbuf = (char *) slapi_ch_malloc(lendn + strlen(maxthreadbuf) + 46);
				bufptr = newbuf;
			}

			sprintf( bufptr, "%d:%s:%d:%d:%s%s:%s:%s", i,
			    buf2, 
			    ct->c[i].c_opsinitiated, 
			    ct->c[i].c_opscompleted,
			    ct->c[i].c_gettingber ? "r" : "-",
			    "",
			    ct->c[i].c_dn ? ct->c[i].c_dn : "NULLDN",
			    maxthreadbuf
			    );
			val.bv_val = bufptr;
			val.bv_len = strlen( bufptr );
			attrlist_merge( &e->e_attrs, "connection", vals );
			slapi_ch_free_string(&newbuf);
		}
		PR_ExitMonitor(ct->c[i].c_mutex);
	}

	snprintf( buf, sizeof(buf), "%d", nconns );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attrlist_replace( &e->e_attrs, "currentconnections", vals );

	snprintf( buf, sizeof(buf), "%" NSPRIu64, slapi_counter_get_value(num_conns));
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attrlist_replace( &e->e_attrs, "totalconnections", vals );

	snprintf( buf, sizeof(buf), "%" NSPRIu64, slapi_counter_get_value(conns_in_maxthreads));
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attrlist_replace( &e->e_attrs, "currentconnectionsatmaxthreads", vals );

	snprintf( buf, sizeof(buf), "%" NSPRIu64, slapi_counter_get_value(max_threads_count));
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attrlist_replace( &e->e_attrs, "maxthreadsperconnhits", vals );

	snprintf( buf, sizeof(buf), "%d", (ct!=NULL?ct->size:0) );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attrlist_replace( &e->e_attrs, "dtablesize", vals );

	snprintf( buf, sizeof(buf), "%d", nreadwaiters );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attrlist_replace( &e->e_attrs, "readwaiters", vals );
}

void
connection_table_dump_activity_to_errors_log(Connection_Table *ct)
{
	int i;

	for ( i = 0; i < ct->size; i++ )
	{
		Connection *c= &(ct->c[i]);
		if ( c->c_mutex )
		{
			/* Find the connection we are referring to */
			int j= c->c_fdi;
			PR_EnterMonitor(c->c_mutex);
			if ( (c->c_sd != SLAPD_INVALID_SOCKET) && 
			     (j >= 0) && (c->c_prfd == ct->fd[j].fd) )
			{
				int r = ct->fd[j].out_flags & SLAPD_POLL_FLAGS;
				if ( r )
				{
					LDAPDebug( LDAP_DEBUG_CONNS,"activity on %d%s\n", i, r ? "r" : "",0 );
				}
			}
			PR_ExitMonitor(c->c_mutex);
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
		int	i;
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


/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include "cb.h"

/*
 * Most of the complicated connection-related code lives in this file.  Some
 * general notes about how we manage our connections to "remote" LDAP servers:
 *
 * 1) Each farm server we have a relationship with is managed independently.
 *
 * 2) We may simultaneously issue multiple requests on a single LDAP
 *    connection.  Each server has a "maxconcurrency" configuration
 *    parameter associated with it that caps the number of outstanding operations
 *    per connection.  For each connection we maintain a "usecount"
 *    which is used to track the number of threads using the connection.
 *
 * 3) IMPORTANT NOTE: This connexion management is stateless i.e there is no garanty that
 *    operation from the same incoming client connections are sent to the same
 *    outgoing connection to the farm server. Today, this is not a problem because
 *    all controls we support are stateless. The implementation of the abandon
 *    operation takes this limitation into account. 
 *
 * 4) We may open more than one connection to a server. Each farm server 
 *    has a "maxconnections" configuration parameter associated with it 
 *    that caps the number of connections.
 *
 * 5) If no connection is available to service a request , threads
 *    go to sleep on a condition variable and one is woken up each time
 *    a connection's "usecount" is decremented.
 *
 * 6) If we see an LDAP_CONNECT_ERROR or LDAP_SERVER_DOWN error on a
 *    session handle, we mark its status as CB_LDAP_STATUS_DOWN and
 *    close it as soon as all threads using it release it.  Connections
 *    marked as "down" are not counted against the "maxconnections" limit.
 *
 * 7) We close and reopen connections that have been open for more than
 *    the server's configured connection lifetime.  This is done to ensure
 *    that we reconnect to a primary server after failover occurs.  If no
 *    lifetime is configured or it is set to 0, we never close and reopen
 *    connections.
 */

static void cb_close_and_dispose_connection ( cb_outgoing_conn * conn );
static void cb_check_for_stale_connections(cb_conn_pool * pool);

PRUint32 PR_GetThreadID(PRThread *thread);

/* returns the threadId of the current thread modulo MAX_CONN_ARRAY 
=> gives the position of the thread in the array of secure connections */
static int PR_ThreadSelf() {
	PRThread *thr = PR_GetCurrentThread();
	PRUint32 myself = PR_GetThreadID(thr);
	myself &= 0x000007FF ;
	return myself;
}

static int PR_MyThreadId() {
	PRThread *thr = PR_GetCurrentThread();
	PRUint32 myself = PR_GetThreadID(thr);
	return myself;
}

/*
** Close outgoing connections
*/

void cb_close_conn_pool(cb_conn_pool * pool) {

	cb_outgoing_conn	*conn, *nextconn;
	int					secure = pool->secure;
	int					i = 0;

	slapi_lock_mutex( pool->conn.conn_list_mutex );

	if (secure) {
		for (i=0; i< MAX_CONN_ARRAY; i++) {
			for (conn = pool->connarray[i]; conn != NULL; conn = nextconn) {
			  if ( conn->status != CB_CONNSTATUS_OK ) {
			    slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
					     "cb_close_conn_pool: unexpected connection state (%d)\n",conn->status);
			  }
			  nextconn=conn->next;
			  cb_close_and_dispose_connection(conn);
			}
		}
	} 
	else {
        for ( conn = pool->conn.conn_list; conn != NULL; conn = nextconn ) {
        	if ( conn->status != CB_CONNSTATUS_OK ) {
  			slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
				"cb_close_conn_pool: unexpected connection state (%d)\n",conn->status);
		}
		nextconn=conn->next;
		cb_close_and_dispose_connection(conn);
        }
	}

	pool->conn.conn_list=NULL;
	pool->conn.conn_list_count=0;

	slapi_unlock_mutex( pool->conn.conn_list_mutex );
}

/*
 * Get an LDAP session handle for communicating with the farm servers.
 *
 * Returns an LDAP eror code, typically:
 *      LDAP_SUCCESS
 *      LDAP_TIMELIMIT_EXCEEDED
 *      LDAP_CONNECT_ERROR
 * NOTE : if maxtime NULL, use operation timeout
 */

int cb_get_connection(cb_conn_pool * pool, LDAP ** lld, cb_outgoing_conn ** cc,struct timeval * maxtime, char **errmsg) {

	int 				rc=LDAP_SUCCESS;          /* optimistic */
	cb_outgoing_conn	*conn=NULL;
	cb_outgoing_conn	*connprev=NULL;
	LDAP				*ld=NULL;
	time_t				endbefore=0;
	int 				checktime=0;
	struct timeval		bind_to, op_to;
	unsigned int 		maxconcurrency,maxconnections;
	char 				*password,*binddn,*hostname;
	unsigned int 		port;
	int 				secure;
	static	char		*error1="Can't contact remote server : %s";
	static	char		*error2="Can't bind to remote server : %s";
	int					isMultiThread = ENABLE_MULTITHREAD_PER_CONN ; /* by default, we enable multiple operations per connection */
	
	/*
	** return an error if we can't get a connection
	** before the operation timeout has expired
        ** bind_timeout: timeout for the bind operation (if bind needed)
	**   ( checked in ldap_result )
	** op_timeout: timeout for the op that needs a connection
	**   ( checked in the loop )
	*/
	*cc=NULL;

	PR_RWLock_Rlock(pool->rwl_config_lock);
	maxconcurrency=pool->conn.maxconcurrency;
	maxconnections=pool->conn.maxconnections;
	bind_to.tv_sec = pool->conn.bind_timeout.tv_sec;
	bind_to.tv_usec = pool->conn.bind_timeout.tv_usec;
	op_to.tv_sec = pool->conn.op_timeout.tv_sec;
	op_to.tv_usec = pool->conn.op_timeout.tv_usec;

	/* SD 02/10/2000 temp fix                        */
	/* allow dynamic update of the binddn & password */
	/* host, port and security mode			 */
	/* previous values are NOT freed when changed    */
	/* won't likely to be changed often		 */
	/* pointers put in the waste basket fields and   */
	/* freed when the backend is stopped.            */

	password=pool->password;
	binddn=pool->binddn;
	hostname=pool->hostname;
	port=pool->port;
	secure=pool->secure;

	PR_RWLock_Unlock(pool->rwl_config_lock);

	if (secure) {
		isMultiThread = DISABLE_MULTITHREAD_PER_CONN ;
	}

	/* For stupid admins */
	if (maxconnections <=0) {
                slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                	"<== cb_get_connection error (no connection available)\n");
		if ( errmsg ) {
			*errmsg = slapi_ch_malloc(CB_BUFSIZE);
			sprintf(*errmsg,error1,"no connection available");
		}
		return LDAP_CONNECT_ERROR;
        }

	if (maxtime) {
		if (maxtime->tv_sec != 0) {
			checktime=1;
        		endbefore = current_time() + maxtime->tv_sec;

			/* make sure bind to <= operation timeout */
			if ((bind_to.tv_sec==0) || (bind_to.tv_sec > maxtime->tv_sec))
				bind_to.tv_sec=maxtime->tv_sec;
		}
	} else {
		if (op_to.tv_sec != 0) {
			checktime=1;
        		endbefore = current_time() + op_to.tv_sec;

			/* make sure bind to <= operation timeout */
			if ((bind_to.tv_sec==0) || (bind_to.tv_sec > op_to.tv_sec))
				bind_to.tv_sec=op_to.tv_sec;
		}
	}

 	/*
	 * Close (or mark to be closed) any connections for this farm server that have
 	 * exceeded the maximum connection lifetime.
	 */

	cb_check_for_stale_connections(pool);
	
	/*
	 * Look for an available, already open connection
	 */

    	slapi_lock_mutex( pool->conn.conn_list_mutex );

	if (cb_debug_on()) {
  		slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
        		"==> cb_get_connection server %s conns: %d maxconns: %d\n",
        		hostname, pool->conn.conn_list_count, maxconnections );
	}

	for (;;) {

		/* time limit mgmt */
		if (checktime) {
			if (current_time() > endbefore ) {
  				slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
        				"cb_get_connection server %s expired.\n", hostname );
				if ( errmsg ) {
					*errmsg = slapi_ch_malloc(CB_BUFSIZE);
					sprintf(*errmsg,error1,"timelimit exceeded");
				}
				rc=LDAP_TIMELIMIT_EXCEEDED;
				conn=NULL;
				ld=NULL;
                         	goto unlock_and_return;    
			}
		}

        	/*
         	 * First, look for an available, already open/bound connection
         	 */

		if (secure) {
			for (conn = pool->connarray[PR_ThreadSelf()]; conn != NULL; conn = conn->next) {
			  if ((conn->ThreadId == PR_MyThreadId()) && (conn->status == CB_CONNSTATUS_OK &&
			        conn->refcount < maxconcurrency)){
			        if (cb_debug_on()) {
                			slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                        		"<= cb_get_connection server found conn 0x%x to use)\n", conn );
				}
				goto unlock_and_return;         /* found one */
			  }
			}
		} 
		else {
        	connprev = NULL;
        	for ( conn = pool->conn.conn_list; conn != NULL; conn = conn->next ) {
			if (cb_debug_on()) {
                		slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                        		"list: conn 0x%x status %d refcount %d\n", conn, 
					conn->status, conn->refcount );
			}

            		if ( conn->status == CB_CONNSTATUS_OK
                    		&& conn->refcount < maxconcurrency ) {
				if (cb_debug_on()) {
                			slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                        		"<= cb_get_connection server found conn 0x%x to use)\n", conn );
				}
                		goto unlock_and_return;         /* found one */
            		}
            		connprev = conn;
        	}
		}

 		if ( secure || pool->conn.conn_list_count <maxconnections) {

			int version=LDAP_VERSION3;

			/* check wether the security libraries are correctly initialized */
			if (secure && slapd_security_library_is_initialized() != 1) {
			    slapi_log_error(
					    SLAPI_LOG_FATAL, CB_PLUGIN_SUBSYSTEM,
					    "SSL Not Initialized, Chaining Backend over SSL FAILED\n");
			    rc = LDAP_CONNECT_ERROR;
			    goto unlock_and_return;
			 }

            		/*
             		 * we have not exceeded the maximum number of connections allowed,
             		 * so we initialize a new one and add it to the end of our list.
             		 */

			/* No need to lock. url can't be changed dynamically */
			if ((ld=slapi_ldap_init(hostname,port,secure,isMultiThread))== NULL) { 
				if (cb_debug_on()) {
                        		slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                               		"Can't contact server <%s> port <%d>.\n", hostname, port);
				}
				if ( errmsg ) {
					*errmsg = slapi_ch_malloc(CB_BUFSIZE);
					sprintf(*errmsg,error1,"unknown reason");
				}
				rc = LDAP_CONNECT_ERROR;
				goto unlock_and_return;
			}

                	ldap_set_option( ld, LDAP_OPT_PROTOCOL_VERSION, &version );
			/* Don't chase referrals */ 
                	ldap_set_option( ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF );

                	/* no controls and simple bind only */
			/* For now, bind even if no user to detect error */
			/* earlier					 */
                	if (pool->bindit) {
				int 				msgid;
				LDAPMessage                  	*res=NULL;
				int 				parse_rc;
				PRErrorCode			prerr = 0;
				LDAPControl                     **serverctrls=NULL;
				char 				**referrals=NULL;
				
				char *plain = NULL;
				int ret  = -1;
	
				rc=LDAP_SUCCESS;

				if (cb_debug_on()) {
                               		slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                                       	"Bind to to server <%s> port <%d> as <%s>\n",
                               		hostname, port, binddn);
				}

				ret = pw_rever_decode(password, &plain, CB_CONFIG_USERPASSWORD);

				/* Pb occured in decryption: stop now, binding will fail */
				if ( ret == -1 )
				{
					if (cb_debug_on()) {
                               			slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                                       		"Internal credentials decoding error\n.",
                                       		0, 0, 0);
					}
					rc = LDAP_LOCAL_ERROR;
					goto unlock_and_return;
				}

				/* Password-based client authentication */

				if (( msgid = ldap_simple_bind( ld, binddn, plain)) <0) {
					rc=ldap_get_lderrno( ld, NULL, NULL );
					prerr=PR_GetError();
				}
				if ( ret == 0 ) free(plain); /* free plain only if it has been duplicated */

				if ( rc != LDAP_SUCCESS ) {
					if (cb_debug_on()) {
						slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
								"Can't bind to server <%s> port <%d>. "
								"(LDAP error %d - %s; "
								SLAPI_COMPONENT_NAME_NSPR " error %d - %s)\n",
								hostname, port, rc,
								ldap_err2string(rc),
								prerr, slapd_pr_strerror(prerr));
					}
					if ( errmsg ) {
						*errmsg = slapi_ch_malloc(CB_BUFSIZE);
						sprintf(*errmsg,error2, ldap_err2string(rc));
					}
					rc = LDAP_CONNECT_ERROR;
					goto unlock_and_return;
				}

				rc = ldap_result( ld, msgid, 0, &bind_to, &res );
				switch (rc) {
				case -1:
					rc = ldap_get_lderrno( ld, NULL, NULL );
					if (cb_debug_on()) {
						slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
							"Can't bind to server <%s> port <%d>. "
							"(LDAP error %d - %s; "
							SLAPI_COMPONENT_NAME_NSPR " error %d - %s)\n",
							hostname, port, rc,
							ldap_err2string(rc),
							prerr, slapd_pr_strerror(prerr));
					}
					if ( errmsg ) {
						*errmsg = slapi_ch_malloc(CB_BUFSIZE);
						sprintf(*errmsg,error2,ldap_err2string(rc));
					}
					rc = LDAP_CONNECT_ERROR;
					goto unlock_and_return;
				case 0:
					if (cb_debug_on()) {
                                	slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                                        	"Can't bind to server <%s> port <%d>. (%s)\n",
                                        	hostname, port, "time-out expired");
					}
					rc = LDAP_CONNECT_ERROR;
					goto unlock_and_return;
				default:

					parse_rc = ldap_parse_result( ld, res, &rc, NULL, 
       						NULL, &referrals, &serverctrls, 1 );

      					if ( parse_rc != LDAP_SUCCESS ) {
						if (cb_debug_on()) {
                                		slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                                        		"Can't bind to server <%s> port <%d>. (%s)\n",
                                        		hostname, port, ldap_err2string(parse_rc));
						}
						if ( errmsg ) {
	                                                *errmsg = slapi_ch_malloc(CB_BUFSIZE);
							sprintf(*errmsg,error2,ldap_err2string(parse_rc));
						}
						rc = parse_rc;
						goto unlock_and_return;
					}

				  	if ( rc != LDAP_SUCCESS ) {
						if (cb_debug_on()) {
                                		slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                                        		"Can't bind to server <%s> port <%d>. (%s)\n",
                                        		hostname, port, ldap_err2string(rc));
						}
						if ( errmsg ) {
	                                                *errmsg = slapi_ch_malloc(CB_BUFSIZE);
							sprintf(*errmsg,error2, ldap_err2string(rc));
						}
						goto unlock_and_return;
					}

					if ( serverctrls ) 
					{
					    int i;
					    for( i = 0; serverctrls[ i ] != NULL; ++i ) 
					    {
						if ( !(strcmp( serverctrls[ i ]->ldctl_oid, LDAP_CONTROL_PWEXPIRED)) )
						{
						    /* Bind is successful but password has expired */
						    slapi_log_error(SLAPI_LOG_FATAL, CB_PLUGIN_SUBSYSTEM, 
								    "Succesfully bound as %s to remote server %s:%d, "
								    "but password has expired.\n",
								    binddn, hostname, port);
						}					
						else if ( !(strcmp( serverctrls[ i ]->ldctl_oid, LDAP_CONTROL_PWEXPIRING)) )
						{
						    /* The password is expiring in n seconds */
						    if ( (serverctrls[ i ]->ldctl_value.bv_val != NULL) &&
							 (serverctrls[ i ]->ldctl_value.bv_len > 0) )
						    {
							int password_expiring = atoi( serverctrls[ i ]->ldctl_value.bv_val );
							slapi_log_error(SLAPI_LOG_FATAL, CB_PLUGIN_SUBSYSTEM, 
									"Succesfully bound as %s to remote server %s:%d, "
									"but password is expiring in %d seconds.\n",
									binddn, hostname, port, password_expiring);
						    }
						}
					    }	
					    ldap_controls_free(serverctrls);
					}

					if (referrals) 
					    charray_free(referrals);
				}
			}

			conn = (cb_outgoing_conn *) slapi_ch_malloc(sizeof(cb_outgoing_conn));
			conn->ld=ld;
			conn->status=CB_CONNSTATUS_OK;
			conn->refcount=0;	/* incremented below */
			conn->opentime=current_time();
			conn->ThreadId=PR_MyThreadId(); /* store the thread id */
			conn->next=NULL;
			if (secure) {
				if (pool->connarray[PR_ThreadSelf()] == NULL) {
				  pool->connarray[PR_ThreadSelf()] = conn;
				}
				else {
				  conn->next = pool->connarray[PR_ThreadSelf()];
				  pool->connarray[PR_ThreadSelf()] = conn ;
				}
			}
			else {
				if ( NULL == connprev ) {
					pool->conn.conn_list = conn;
				} else {
					connprev->next=conn;
				}
			}

			++pool->conn.conn_list_count;
				
			if (cb_debug_on()) {
                      	slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                    		"<= cb_get_connection added new conn 0x%x, "
                    		"conn count now %d\n", conn->ld, pool->conn.conn_list_count );
			}
            		goto unlock_and_return;             /* got a new one */
		}

		if (cb_debug_on()) {
        	slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                "... cb_get_connection waiting for conn to free up\n" );
		}
		
   		if (!secure) slapi_wait_condvar( pool->conn.conn_list_cv, NULL );

		if (cb_debug_on()) {
        		slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                	"... cb_get_connection awake again\n" );
		}
    	} 

unlock_and_return:
	if ( conn != NULL ) {
        	++conn->refcount;
		*lld=conn->ld;
		*cc=conn;
		if (cb_debug_on()) {
  		slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                "<== cb_get_connection ld=0x%x (concurrency now %d)\n",*lld, conn->refcount );
		}

    	} else {
		if ( NULL != ld ) {
    			slapi_ldap_unbind( ld );
		}

		if (cb_debug_on()) {
	        	slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
       		         "<== cb_get_connection error %d\n", rc );
		}
    	}

	slapi_unlock_mutex(pool->conn.conn_list_mutex);
    	return( rc );
}

/*
 * We are done with the connection handle because the
 * LDAP operation has completed.
 */

void cb_release_op_connection(cb_conn_pool* pool, LDAP *lld, int dispose) {

	cb_outgoing_conn	*conn;
	cb_outgoing_conn        *connprev = NULL;
	int			secure    = pool->secure;
	int			myself    = 0;

       	slapi_lock_mutex(pool->conn.conn_list_mutex);
	/*
     	 * find the connection structure this ld is part of
         */

	if (secure) {
	  myself = PR_ThreadSelf();
	  for (conn = pool->connarray[myself]; conn != NULL; conn = conn->next ) {
		if ( lld == conn->ld ) 
            		break;
        	connprev = conn;
	  }
	} 
	else {
	  for ( conn = pool->conn.conn_list; conn != NULL; conn = conn->next ){
		if ( lld == conn->ld ) 
            		break;
        	connprev = conn;
	  }
	}

	if ( conn == NULL ) {               /* ld not found -- unexpected */
        	slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                "==> cb_release_op_connection ld=0x%x not found\n", lld );
    	} else {

        	--conn->refcount;

		if (cb_debug_on()) {
                	slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                        	"release conn 0x%x status %d refcount after release %d\n", conn,
                                        conn->status, conn->refcount );
                }

        	if ( dispose ) {
            		conn->status = CB_CONNSTATUS_DOWN;
        	}
 
        	if ( conn->status != CB_CONNSTATUS_OK && conn->refcount == 0 ) {

            		/*
             		 * remove from server's connection list
             	 	 */

			if (!secure) {
			  if ( connprev == NULL ) {
                		pool->conn.conn_list = conn->next;
			  } else {
                		connprev->next = conn->next;
			  }
			}
			else {
			  if ( connprev == NULL ) {
                		pool->connarray[myself] = conn->next;
			  } else {
                		connprev->next = conn->next;
			  }
			}

            		--pool->conn.conn_list_count;
 
            		/*
             		 * close connection and free memory
             		 */
            		cb_close_and_dispose_connection( conn );
        	}     
    	}

 	/*
     	 * wake up a thread that is waiting for a connection
	 */

    	if (!secure) slapi_notify_condvar( pool->conn.conn_list_cv, 0 );
      
    	slapi_unlock_mutex( pool->conn.conn_list_mutex );
}
 

static void
cb_close_and_dispose_connection( cb_outgoing_conn *conn )
{
    slapi_ldap_unbind( conn->ld );
    conn->ld = NULL;
    slapi_ch_free( (void **)&conn );
}

static void cb_check_for_stale_connections(cb_conn_pool * pool) {

        cb_outgoing_conn 	* connprev, *conn, *conn_next;
	time_t 			curtime;
	int 			connlifetime;
	int 			myself;

       	PR_RWLock_Rlock(pool->rwl_config_lock);
	connlifetime=pool->conn.connlifetime;
       	PR_RWLock_Unlock(pool->rwl_config_lock);
	
       	connprev = NULL;
	conn_next = NULL;

        slapi_lock_mutex(pool->conn.conn_list_mutex);
 
	if (connlifetime > 0)
                curtime=current_time();
	
	if (pool->secure) {
		myself = PR_ThreadSelf();
		for (conn = pool->connarray[myself]; conn != NULL; conn = conn_next){
		  if ((conn->status == CB_CONNSTATUS_STALE) ||
		    	(( connlifetime > 0) && (curtime - conn->opentime > connlifetime))) {
				if ( conn->refcount == 0 ) {
					if (cb_debug_on()) {
                        			slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
							"cb_check_for_stale_connections: conn 0x%x idle and stale\n",conn);
					}
                               		--pool->conn.conn_list_count;
					if (connprev == NULL) {
					  pool->connarray[myself] = conn->next ;
					}
					else {
					  connprev->next = conn->next ;
					}
					conn_next = conn->next ;
                                	cb_close_and_dispose_connection( conn );
					continue;
				}
                        	/* Connection is stale but in use                           */
                        	/* Mark to be disposed later but let it in the backend list */
                        	/* so that it is counted as a valid connection              */
				else {
				  conn->status = CB_CONNSTATUS_STALE;
				}
                        	if (cb_debug_on()) {
                        	        slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                       		                "cb_check_for_stale_connections: conn 0x%x stale\n",conn);
                        	}
			}
			connprev = conn ;
			conn_next = conn->next;
		}
        	slapi_unlock_mutex(pool->conn.conn_list_mutex);
		return;
	}

	for ( conn = pool->conn.conn_list; conn != NULL; conn=conn_next  ) {
		if ((conn->status == CB_CONNSTATUS_STALE) ||
		    (( connlifetime > 0) && (curtime - conn->opentime > connlifetime))) {
			if ( conn->refcount == 0 ) {

                                 /* Connection idle & stale. Remove and free. */

                                if ( NULL == connprev )
                                        pool->conn.conn_list = conn->next;
                                else
                                        connprev->next=conn->next;

				if (cb_debug_on()) {
                        		slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
						"cb_check_for_stale_connections: conn 0x%x idle and stale\n",conn);
				}
                                --pool->conn.conn_list_count;
				conn_next=conn->next;
                                cb_close_and_dispose_connection( conn );
				continue;
			}

                        /* Connection is stale but in use                           */
                        /* Mark to be disposed later but let it in the backend list */
                        /* so that it is counted as a valid connection              */
			else {
			  conn->status = CB_CONNSTATUS_STALE;
			}
                        if (cb_debug_on()) {
                                slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                                        "cb_check_for_stale_connections: conn 0x%x stale\n",conn);
                        }
		}
                connprev = conn;
		conn_next=conn->next;
	}
				
	/* Generate an event to wake up threads waiting */
	/* for a conn to be released. Useful to detect  */
	/* exceeded time limit. May be expensive        */

        slapi_notify_condvar( pool->conn.conn_list_cv, 0 );

        slapi_unlock_mutex(pool->conn.conn_list_mutex);
}

/*
 * close all open connections in preparation for server shutdown, etc.
 * WARNING: Don't wait for current operations to complete
 */

void
cb_close_all_connections( Slapi_Backend * be )
{

        cb_outgoing_conn        *conn, *next_conn;
        cb_backend_instance * cb= cb_get_instance(be);
	int 			i;

        slapi_lock_mutex(cb->pool->conn.conn_list_mutex);
	if (cb->pool->secure) {
	  for (i=0; i< MAX_CONN_ARRAY; i++) {
	    for (conn = cb->pool->connarray[i]; conn != NULL; conn = next_conn ){
	      next_conn = conn->next;
	      cb_close_and_dispose_connection(conn);
	    }	
	  }
	} else {
	  for ( conn = cb->pool->conn.conn_list; conn != NULL; conn = next_conn ) {
	    next_conn=conn->next;
	    cb_close_and_dispose_connection(conn);
	  }
	}
        slapi_unlock_mutex(cb->pool->conn.conn_list_mutex);

        slapi_lock_mutex(cb->bind_pool->conn.conn_list_mutex);
	if (cb->bind_pool->secure) {
	  for (i=0; i< MAX_CONN_ARRAY; i++) {
	    for (conn = cb->bind_pool->connarray[i]; conn != NULL; conn = next_conn ){
	      next_conn=conn->next;
	      cb_close_and_dispose_connection(conn);
	    }
	  }
	} else {
	  for ( conn = cb->bind_pool->conn.conn_list; conn != NULL; conn = next_conn ) {
	    next_conn=conn->next;
	    cb_close_and_dispose_connection(conn);
	  }
	}
        slapi_unlock_mutex(cb->bind_pool->conn.conn_list_mutex);
}

/* Mark used connections as stale and close unsued connections */
/* Called when the target farm url has changed		       */

void cb_stale_all_connections( cb_backend_instance * cb)
{
        cb_outgoing_conn        *conn, *next_conn, *prev_conn;
	int 			notify=0;
	int 			i, j;
	cb_conn_pool 		*pools[3];

	pools[0]=cb->pool;
	pools[1]=cb->bind_pool;
	pools[2]=NULL;
	
	for (i=0; pools[i]; i++) {	
	  slapi_lock_mutex(pools[i]->conn.conn_list_mutex);		
	  for (j=0; j< MAX_CONN_ARRAY; j++) {
	    prev_conn=NULL;
	    for (conn = pools[i]->connarray[j]; conn != NULL; conn=next_conn) {
	      next_conn=conn->next;
	      if (conn->refcount > 0) {
		/*
		** Connection is stale but in use
		** Mark to be disposed later but let it in the backend list
		** so that it is counted as a valid connection
		*/      	
		conn->status = CB_CONNSTATUS_STALE;
		prev_conn=conn;
	      } else {
		if (prev_conn == NULL) {
		  pools[i]->connarray[j]=next_conn;
		} else {
		  prev_conn->next=next_conn;
		}
		cb_close_and_dispose_connection(conn);
		pools[i]->conn.conn_list_count--;
	      }
	    }
	  }       	
	  prev_conn = NULL ;	
	  for ( conn = pools[i]->conn.conn_list; conn != NULL; conn = next_conn ) {
	    next_conn=conn->next;
	    if (conn->refcount > 0) {
       	       	/*
		** Connection is stale but in use
		** Mark to be disposed later but let it in the backend list
		** so that it is counted as a valid connection
		*/
	      conn->status = CB_CONNSTATUS_STALE;
	      prev_conn=conn;
	    } 
	    else {
       	       	if (conn==pools[i]->conn.conn_list) {
       	       		pools[i]->conn.conn_list=next_conn;
       	       	} else {
       	       		prev_conn->next=next_conn;
       	       	}
       	       	cb_close_and_dispose_connection(conn);
                pools[i]->conn.conn_list_count--;
       	       	notify=1;
	    }
	  }
	  if (notify && (! pools[i]->secure)) {
	    slapi_notify_condvar( pools[i]->conn.conn_list_cv, 0 );
	  }
	  slapi_unlock_mutex(pools[i]->conn.conn_list_mutex);
	}
}



/**************************************************************************/
/* Need to use our own connect function until we've switched to C-SDK 4.1 */
/* to have a timeout in the connect system call.                          */
/**************************************************************************/

static int global_connect_to;

#if 0

/* Taken from C-SDK 4.1 */
#include <fcntl.h>
#include <errno.h>
#define LDAP_X_IO_TIMEOUT_NO_TIMEOUT (-1)

static int
nsldapi_os_connect_with_to(LBER_SOCKET sockfd, struct sockaddr *saptr,
        int salen)
{
#ifndef _WIN32
        int             flags;
#endif /* _WIN32 */
        int             n, error;
        int             len;
        fd_set          rset, wset;
        struct timeval  tval;
#ifdef _WIN32
        int             nonblock = 1;
        int             block = 0;
        fd_set          eset;
#endif /* _WIN32 */
	
	int msec=global_connect_to;	/* global */

#ifdef _WIN32
        ioctlsocket(sockfd, FIONBIO, &nonblock);
#else
        flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
#endif /* _WIN32 */

        error = 0;
        if ((n = connect(sockfd, saptr, salen)) < 0)
#ifdef _WIN32
                if ((n != SOCKET_ERROR) &&  (WSAGetLastError() != WSAEWOULDBLOCK)) {
#else
                if (errno != EINPROGRESS) {
#endif /* _WIN32 */
                        return (-1);
                }

        /* success */
        if (n == 0)
                goto done;

        FD_ZERO(&rset);
        FD_SET(sockfd, &rset);
        wset = rset;

#ifdef _WIN32
        eset = rset;
#endif /* _WIN32 */

        if (msec < 0 && msec != LDAP_X_IO_TIMEOUT_NO_TIMEOUT) {
                msec = LDAP_X_IO_TIMEOUT_NO_TIMEOUT;
        } else {
                if (msec != 0)
                        tval.tv_sec = msec / 1000;
                else
                        tval.tv_sec = 0;
                tval.tv_usec = 0;
        }

        /* if timeval structure == NULL, select will block indefinitely */
        /*                      != NULL, and value == 0, select will */
        /*                               not block */
        /* Windows is a bit quirky on how it behaves w.r.t nonblocking */
        /* connects.  If the connect fails, the exception fd, eset, is */
        /* set to show the failure.  The first argument in select is */
        /* ignored */

#ifdef _WIN32
        if ((n = select(sockfd +1, &rset, &wset, &eset,
                (msec != LDAP_X_IO_TIMEOUT_NO_TIMEOUT) ? &tval : NULL)) == 0) {
                errno = WSAETIMEDOUT;
                return (-1);
        }
        /* if wset is set, the connect worked */
        if (FD_ISSET(sockfd, &wset) || FD_ISSET(sockfd, &rset)) {
                len = sizeof(error);
                if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (char *)&error, &len)
                        < 0)
                        return (-1);
                goto done;
	}

        /* if eset is set, the connect failed */
        if (FD_ISSET(sockfd, &eset)) {
                return (-1);
        }

        /* failure on select call */
        if (n == SOCKET_ERROR) {
                return (-1);
        }
#else
        if ((n = select(sockfd +1, &rset, &wset, NULL,
                (msec != LDAP_X_IO_TIMEOUT_NO_TIMEOUT) ? &tval : NULL)) == 0) {
                errno = ETIMEDOUT;
                return (-1);
        }
        if (FD_ISSET(sockfd, &rset) || FD_ISSET(sockfd, &wset)) {
                len = sizeof(error);
                if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (char *)&error, &len)
                        < 0)
                        return (-1);
        }
#endif /* _WIN32 */
done:
#ifdef _WIN32
        ioctlsocket(sockfd, FIONBIO, &block);
#else
        fcntl(sockfd, F_SETFL, flags);
#endif /* _WIN32 */

        if (error) {
                errno = error;
                return (-1);
        }

        return (0);
}

#endif

/* Try to figure out if a farm server is still alive */

int cb_ping_farm(cb_backend_instance *cb, cb_outgoing_conn * cnx,time_t end_time) {

	char 			*attrs[]	={"1.1",NULL};
	int 			rc;
    struct timeval          timeout;
	LDAP 			*ld;
	LDAPMessage		*result;
#if 0
	struct 			ldap_io_fns iof;
#endif
	time_t 			now;
	if (cb->max_idle_time <=0)	/* Heart-beat disabled */
		return LDAP_SUCCESS;

	if (cnx && (cnx->status != CB_CONNSTATUS_OK ))	/* Known problem */
		return LDAP_SERVER_DOWN;

	now = current_time();
	if (end_time && ((now <= end_time) || (end_time <0))) return LDAP_SUCCESS;
	
	ld=slapi_ldap_init(cb->pool->hostname,cb->pool->port,cb->pool->secure,0); 
	if (NULL == ld) {
		cb_update_failed_conn_cpt( cb );
		return LDAP_SERVER_DOWN;
	}

#if 0
	memset(&iof,0,sizeof(struct ldap_io_fns));
	if (LDAP_SUCCESS !=ldap_get_option(ld,LDAP_OPT_IO_FN_PTRS,&iof)) {
		slapi_ldap_unbind( ld );
		cb_update_failed_conn_cpt( cb );
		return LDAP_SERVER_DOWN;
	}

	iof.liof_connect = nsldapi_os_connect_with_to;
	if (LDAP_SUCCESS !=ldap_set_option(ld,LDAP_OPT_IO_FN_PTRS,&iof)) {
		slapi_ldap_unbind( ld );
		cb_update_failed_conn_cpt( cb );
		return LDAP_SERVER_DOWN;
	}

#endif
	
	timeout.tv_sec=cb->max_test_time;
	timeout.tv_usec=0;
	
	global_connect_to=cb->max_test_time * 1000;	/* Reuse the same for the connect */
 	rc=ldap_search_ext_s(ld ,NULL,LDAP_SCOPE_BASE,"objectclass=*",attrs,1,NULL, 
		NULL, &timeout, 1,&result);
	if ( LDAP_SUCCESS != rc ) {
		slapi_ldap_unbind( ld );
		cb_update_failed_conn_cpt( cb );
		return LDAP_SERVER_DOWN; 
	}
	
	ldap_msgfree(result);
	slapi_ldap_unbind( ld );
	cb_reset_conn_cpt( cb );
	return LDAP_SUCCESS;
}



void cb_update_failed_conn_cpt ( cb_backend_instance *cb ) {
	/* if the chaining BE is already unavailable, we do nothing*/
	time_t now;
	if (cb->monitor_availability.farmserver_state == FARMSERVER_AVAILABLE) {
		slapi_lock_mutex(cb->monitor_availability.cpt_lock);
			cb->monitor_availability.cpt ++;
		slapi_unlock_mutex(cb->monitor_availability.cpt_lock);
		if (cb->monitor_availability.cpt >= CB_NUM_CONN_BEFORE_UNAVAILABILITY ) {
	       		/* we reach the limit of authorized failed connections => we setup the chaining BE state to unavailable */
	       		now = current_time();
			slapi_lock_mutex(cb->monitor_availability.lock_timeLimit);
	       		       cb->monitor_availability.unavailableTimeLimit = now + CB_UNAVAILABLE_PERIOD ;
			slapi_unlock_mutex(cb->monitor_availability.lock_timeLimit);
	       		cb->monitor_availability.farmserver_state = FARMSERVER_UNAVAILABLE ;
			slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
				"cb_update_failed_conn_cpt: Farm server unavailable");
	       	}
		
	}
}

void cb_reset_conn_cpt( cb_backend_instance *cb ) {
	if (cb->monitor_availability.cpt > 0) { 
		slapi_lock_mutex(cb->monitor_availability.cpt_lock);
			cb->monitor_availability.cpt = 0 ;
			if (cb->monitor_availability.farmserver_state == FARMSERVER_UNAVAILABLE) {
				cb->monitor_availability.farmserver_state = FARMSERVER_AVAILABLE ;
			        slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
				"cb_reset_conn_cpt: Farm server is back");
			}
		slapi_unlock_mutex(cb->monitor_availability.cpt_lock);
	}
}

int cb_check_availability( cb_backend_instance *cb, Slapi_PBlock *pb ) {
	/* check wether the farmserver is available or not */
        int rc ;
	time_t now ;
	if ( cb->monitor_availability.farmserver_state == FARMSERVER_UNAVAILABLE ){
		slapi_lock_mutex(cb->monitor_availability.lock_timeLimit);
		now = current_time();
		if (now >= cb->monitor_availability.unavailableTimeLimit) {
		    cb->monitor_availability.unavailableTimeLimit = now + CB_INFINITE_TIME ; /* to be sure only one thread can do the test */
		    slapi_unlock_mutex(cb->monitor_availability.lock_timeLimit);
		} 
		else {
		    slapi_unlock_mutex(cb->monitor_availability.lock_timeLimit);		
		    cb_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, "FARM SERVER TEMPORARY UNAVAILABLE", 0, NULL) ;
		    return FARMSERVER_UNAVAILABLE ;
		}
	        slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
				"cb_check_availability: ping the farm server and check if it's still unavailable");
		if ((rc = cb_ping_farm(cb, NULL, 0)) != LDAP_SUCCESS) { /* farm still unavailable... Just change the timelimit */ 
		    slapi_lock_mutex(cb->monitor_availability.lock_timeLimit);
		    now = current_time();
		    cb->monitor_availability.unavailableTimeLimit = now + CB_UNAVAILABLE_PERIOD ;
		    slapi_unlock_mutex(cb->monitor_availability.lock_timeLimit);		
		    cb_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, "FARM SERVER TEMPORARY UNAVAILABLE", 0, NULL) ;
	            slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
				"cb_check_availability: Farm server still unavailable");
		    return FARMSERVER_UNAVAILABLE ;
		}
		else {
		    /* farm is back !*/
		    slapi_lock_mutex(cb->monitor_availability.lock_timeLimit);
		    now = current_time();
		    cb->monitor_availability.unavailableTimeLimit = now ; /* the unavailable period is finished */
		    slapi_unlock_mutex(cb->monitor_availability.lock_timeLimit);	
		    /* The farmer server state backs to FARMSERVER_AVAILABLE, but this already done in cb_ping_farm, and also the reset of cpt*/
		    return FARMSERVER_AVAILABLE ;
		}
        }
	return FARMSERVER_AVAILABLE ;
}

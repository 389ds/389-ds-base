#ident "@(#)ldap-private.h	1.6 06/10/98 SMI"

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
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2006 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef _LDAP_PRIVATE_H
#define _LDAP_PRIVATE_H

#ifdef _REENTRANT
#ifndef MAX_THREAD_ID
#define MAX_THREAD_ID 500
#endif /* MAX_THREAD_ID */
#else /* _REENTRANT */
#ifndef MAX_THREAD_ID
#define MAX_THREAD_ID 1
#endif /* MAX_THREAD_ID */
#endif /* _REENTRANT */

#define COMPAT20
#define COMPAT30
#if defined(COMPAT20) || defined(COMPAT30)
#define COMPAT
#endif

#ifdef LDAP_DNS
#define LDAP_OPT_DNS		0x00000001	/* use DN & DNS */
#endif /* LDAP_DNS */

/*
#define DBG_LOCK1(st) printf("%d> %s %d:%s\n", _thr_self(), st, __LINE__, __FILE__);
#define DBG_LOCK2(ld,st) printf("%d> %s ld_lockcount=%d %d:%s\n",  _thr_self(), st, (ld)->ld_lockcount, __LINE__, __FILE__);
*/
#define DBG_LOCK1(st)
#define DBG_LOCK2(ld,st)

#define LOCK_RESPONSE(ld) \
	if ((ld)->ld_response_lockthread != _thr_self()) { \
		DBG_LOCK1("waiting for response lock") \
		pthread_mutex_lock( &((ld)->ld_response_mutex) ); \
		DBG_LOCK1("got response lock") \
		(ld)->ld_response_lockthread = _thr_self(); \
	} else  { \
	        (ld)->ld_response_lockcount++; \
		DBG_LOCK2(ld, "fake ldap lock") \
	}

#define UNLOCK_RESPONSE(ld) \
	if ((ld)->ld_response_lockcount==0) { \
		(ld)->ld_response_lockthread = 0; \
		pthread_mutex_unlock( &((ld)->ld_response_mutex) ); \
		DBG_LOCK1("freed response lock") \
	} else  { \
	        (ld)->ld_response_lockcount--; \
	        DBG_LOCK2(ld, "fake ldap unlock") \
	}

#define LOCK_LDAP(ld) \
	if ((ld)->ld_lockthread != _thr_self()) { \
	        DBG_LOCK1("waiting for ldap lock") \
                pthread_mutex_lock( &((ld)->ld_ldap_mutex) ); \
		DBG_LOCK1("got ldap lock") \
		(ld)->ld_lockthread = _thr_self(); \
	} else  { \
	        (ld)->ld_lockcount++; \
		DBG_LOCK2(ld, "fake ldap lock") \
	}

#define UNLOCK_LDAP(ld) \
	if ((ld)->ld_lockcount==0) { \
                (ld)->ld_lockthread = 0; \
		pthread_mutex_unlock( &((ld)->ld_ldap_mutex) ); \
		DBG_LOCK1("freed ldap lock") \
	} else  { \
	        (ld)->ld_lockcount--; \
	        DBG_LOCK2(ld, "fake ldap unlock") \
	}

#define LOCK_POLL(ld) 	pthread_mutex_lock( &ld->ld_poll_mutex )
#define UNLOCK_POLL(ld) pthread_mutex_unlock( &ld->ld_poll_mutex )

/*
 * This structure represents both ldap messages and ldap responses.
 * These are really the same, except in the case of search responses,
 * where a response has multiple messages.
 */
typedef struct ldapmsg {
	int		lm_msgid;	/* the message id */
	int		lm_msgtype;	/* the message type */
	BerElement	*lm_ber;	/* the ber encoded message contents */
	struct ldapmsg	*lm_chain;	/* for search - next msg in the resp */
	struct ldapmsg	*lm_next;	/* next response */
	unsigned long	lm_time;	/* used to maintain cache */
} _struct_LDAPMessage;

typedef struct ldap_filt_list {
    char			*lfl_tag;
    char			*lfl_pattern;
    char			*lfl_delims;
    LDAPFiltInfo		*lfl_ilist;
    struct ldap_filt_list	*lfl_next;
} _struct_FiltList;

typedef struct ldap_filt_desc {
	LDAPFiltList		*lfd_filtlist;
	LDAPFiltInfo		*lfd_curfip;
	LDAPFiltInfo		lfd_retfi;
	char			lfd_filter[ LDAP_FILT_MAXSIZ ];
	char			*lfd_curval;
	char			*lfd_curvalcopy;
	char			**lfd_curvalwords;
	char			*lfd_filtprefix;
	char			*lfd_filtsuffix;
} _struct_FiltDesc;

/*
 * structure for tracking LDAP server host, ports, DNs, etc.
 */
typedef struct ldap_server {
	char			*lsrv_host;
	char			*lsrv_dn;	/* if NULL, use default */
	int			lsrv_port;
	struct ldap_server	*lsrv_next;
} LDAPServer;


/*
 * structure for representing an LDAP server connection
 */
typedef struct ldap_conn {
	Sockbuf			*lconn_sb;
	int			lconn_refcnt;
	unsigned long		lconn_lastused;	/* time */
	int			lconn_status;
#define LDAP_CONNST_NEEDSOCKET		1
#define LDAP_CONNST_CONNECTING		2
#define LDAP_CONNST_CONNECTED		3
	LDAPServer		*lconn_server;
	char			*lconn_krbinstance;
	struct ldap_conn	*lconn_next;
} LDAPConn;

/*
 * Structure used to keep track of search references
 */
typedef struct ldap_reference {
	char ** lref_refs;
	struct ldap_reference *lref_next;
} LDAPRef;

/*
 * structure used to track outstanding requests
 */
typedef struct ldapreq {
	int		lr_msgid;	/* the message id */
	int		lr_status;	/* status of request */
#define LDAP_REQST_INPROGRESS	1
#define LDAP_REQST_CHASINGREFS	2
#define LDAP_REQST_NOTCONNECTED	3
#define LDAP_REQST_WRITING	4
	int		lr_outrefcnt;	/* count of outstanding referrals */
	int		lr_origid;	/* original request's message id */
	int		lr_parentcnt;	/* count of parent requests */
	int		lr_res_msgtype;	/* result message type */
	int		lr_res_errno;	/* result LDAP errno */
	char		*lr_res_error;	/* result error string */
	char		*lr_res_matched;/* result matched DN string */
	BerElement	*lr_ber;	/* ber encoded request contents */
	LDAPConn	*lr_conn;	/* connection used to send request */
	LDAPRef		*lr_references;
	char	 **lr_ref_followed; /* referral being followed */
	char	 **lr_ref_unfollowed; /* Not being followed */
	char	 **lr_ref_tofollow; /* referral to follow if the one being
								   followed fails. */
	struct ldapreq	*lr_parent;	/* request that spawned this referral */
	struct ldapreq	*lr_refnext;	/* next referral spawned */
	struct ldapreq	*lr_prev;	/* previous request */
	struct ldapreq	*lr_next;	/* next request */
} LDAPRequest;

/*
 * structure for client cache
 */
#define LDAP_CACHE_BUCKETS	31	/* cache hash table size */
typedef struct ldapcache {
	LDAPMessage	*lc_buckets[LDAP_CACHE_BUCKETS];/* hash table */
	LDAPMessage	*lc_requests;			/* unfulfilled reqs */
	long		lc_timeout;			/* request timeout */
	long		lc_maxmem;			/* memory to use */
	long		lc_memused;			/* memory in use */
	int		lc_enabled;			/* enabled? */
	unsigned long	lc_options;			/* options */
#define LDAP_CACHE_OPT_CACHENOERRS	0x00000001
#define LDAP_CACHE_OPT_CACHEALLERRS	0x00000002
}  LDAPCache;
#define NULLLDCACHE ((LDAPCache *)NULL)

/*
 * structure representing an ldap connection
 */
typedef struct ldap {
	Sockbuf		ld_sb;		/* socket descriptor & buffer */
	char		*ld_host;
	int		ld_version;
	char		ld_lberoptions;
	int		ld_deref;

	int		ld_timelimit;
	int		ld_sizelimit;

	LDAPFiltDesc	*ld_filtd;	/* from getfilter for ufn searches */
	char		*ld_ufnprefix;	/* for incomplete ufn's */

	int		ld_errno[MAX_THREAD_ID];	/* thread-specific */
#define ld_errno ld_errno[ldap_thr_index()]
	char		*ld_error[MAX_THREAD_ID];	/* thread-specific */
#define ld_error ld_error[ldap_thr_index()]
	char		*ld_matched[MAX_THREAD_ID];	/* thread-specific */
#define ld_matched ld_matched[ldap_thr_index()]
	char		**ld_referrals[MAX_THREAD_ID];	/* thread-specific */
#define ld_referrals ld_referrals[ldap_thr_index()]
	LDAPControl	**ld_ret_ctrls[MAX_THREAD_ID];	/* thread-specific */
#define ld_ret_ctrls ld_ret_ctrls[ldap_thr_index()]
	int		ld_msgid;

	int ld_follow_referral; /* flag set to true if lib follow referrals */
	LDAPRequest	*ld_requests;	/* list of outstanding requests -- referrals*/

	LDAPMessage	*ld_responses;	/* list of outstanding responses */
	int		*ld_abandoned;	/* array of abandoned requests */

	pthread_mutex_t	ld_response_mutex; /* mutex for responses part of structure */
	pthread_t	ld_response_lockthread; /* thread which currently holds the response lock */
	int		ld_response_lockcount;  /* response lock depth */
	
	char		*ld_attrbuffer[MAX_THREAD_ID];
#define ld_attrbuffer ld_attrbuffer[ldap_thr_index()]
	LDAPCache	*ld_cache;	/* non-null if cache is initialized */
	char		*ld_cldapdn;	/* DN used in connectionless search */

	/* it is OK to change these next four values directly */
	int		ld_cldaptries;	/* connectionless search retry count */
	int		ld_cldaptimeout;/* time between retries */
	int		ld_refhoplimit;	/* limit on referral nesting */
	int ld_restart; /* Decide if continue after interruption */
#ifdef LDAP_SSL
	int ld_use_ssl;
	char *ld_ssl_key;
#endif 
	unsigned long	ld_options;	/* boolean options */

	/* do not mess with the rest though */
	char		*ld_defhost;	/* full name of default server */
	int		ld_defport;	/* port of default server */
	BERTranslateProc ld_lber_encode_translate_proc;
	BERTranslateProc ld_lber_decode_translate_proc;

	LDAPConn	*ld_defconn;	/* default connection */
	LDAPConn	*ld_conns;	/* list of server connections */
	void		*ld_selectinfo;	/* platform specifics for select */

	LDAP_REBIND_FUNCTION *ld_rebindproc;
	void *ld_rebind_extra_arg;
/* 	int		(*ld_rebindproc)( struct ldap *ld, char **dnp, */
/* 				char **passwdp, int *authmethodp, int freeit ); */
				/* routine to get info needed for re-bind */

	pthread_mutex_t	ld_ldap_mutex; /* mutex for thread dependent part of struct */
	pthread_t	ld_lockthread; /* thread which currently holds the lock */
	int		ld_lockcount;  /* lock depth */
	pthread_mutex_t	ld_poll_mutex; /* a seperate lock for polling */

	LDAPControl **ld_srvctrls; /* Controls used by ldap and server */
	LDAPControl **ld_cltctrls; /* Client side controls */

/* KE: Lists of unsolicited notifications */
	LDAPMessage *ld_notifs[MAX_THREAD_ID];
#define ld_notifs ld_notifs[ldap_thr_index()]
} _struct_LDAP;

/*
 * handy macro to check whether LDAP struct is set up for CLDAP or not
 */
#define LDAP_IS_CLDAP( ld )	( ld->ld_sb.sb_naddr > 0 )


#endif /* _LDAP_PRIVATE_H */

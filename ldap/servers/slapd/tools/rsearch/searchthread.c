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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef XP_UNIX
#include <unistd.h>
#endif
#include <time.h>
#include <errno.h>
#include "nspr.h"
#include <sys/types.h>	
#include <sys/socket.h>	
#include <netinet/tcp.h>	/* for TCP_NODELAY */
#include "ldap.h"
#include "rsearch.h"
#include "searchthread.h"

#ifndef LBER_SOCKET
#ifdef LBER_SOCKET_T
#define LBER_SOCKET LBER_SOCKET_T
#else
#define LBER_SOCKET int
#endif
#endif

/* local data for a search thread */
struct _searchthread {
    PRUint32 searchCount;
    PRUint32 failCount;
    double mintime;
    double maxtime;
    LDAP *ld;
    LDAP *ld2;	/* aux LDAP handle */
    LBER_SOCKET soc;
    PRThread *tid;
    PRLock *lock;
    int id;
    int alive;
	int retry;
};

/* new searchthread */
SearchThread *st_new(void)
{
    SearchThread *st = (SearchThread *)malloc(sizeof(SearchThread));

    if (!st) return NULL;
    st->searchCount = st->failCount = 0;
    st->mintime = 10000;
    st->maxtime = 0;
    st->ld = NULL;
    st->ld2 = NULL;
    st->soc = -1;
    st->tid = NULL;
    st->id = 0;
    st->alive = 1;
    st->lock = PR_NewLock();
	st->retry = 0;
    return st;
}

void st_setThread(SearchThread *st, PRThread *tid, int id)
{
    st->tid = tid;
    st->id = id;
}

int st_getThread(SearchThread *st, PRThread **tid)
{
    if (tid) *tid = st->tid;
    return st->id;
}

void st_seed(SearchThread *st) {
	time_t t = time(0);
	t -= st->id * 1000;
	srand((unsigned int)t);
}

static void st_enableTCPnodelay(SearchThread *st)
{
    int val = 1;

    if (st->soc < 0) {
        if (ldap_get_option(st->ld, LDAP_OPT_DESC, (void *)&st->soc)
	    != LDAP_SUCCESS) {
	    fprintf(stderr, "T%d: failed on ldap_get_option\n", st->id);
	    return;
        }
    }
    if (setsockopt(st->soc, IPPROTO_TCP, TCP_NODELAY, (char *)&val,
		   sizeof(val)))
	fprintf(stderr, "T%d: failed in setsockopt 1\n", st->id);
}

/* abruptly disconnect an LDAP connection without unbinding */
static void st_disconnect(SearchThread *st)
{
    if (st->soc < 0) {
        if (ldap_get_option(st->ld, LDAP_OPT_DESC, (void *)&st->soc)
	    != LDAP_SUCCESS) {
	    fprintf(stderr, "T%d: failed on ldap_get_option\n", st->id);
	    return;
        }
    }
#ifdef XP_WIN
    if (closesocket(st->soc))
	fprintf(stderr, "T%d: failed to disconnect\n", st->id);
#else
    if (close(st->soc))
	fprintf(stderr, "T%d: failed to disconnect\n", st->id);
#endif
    st->soc = -1;
}

static int st_bind_core(SearchThread *st, LDAP **ld, char *dn, char *pw)
{
	int ret = 0;
	int retry = 0;
    while (1) {
        struct berval bvcreds = {0, NULL};
        bvcreds.bv_val = pw;
        bvcreds.bv_len = pw ? strlen(pw) : 0;
        ret = ldap_sasl_bind_s(*ld, dn, LDAP_SASL_SIMPLE, &bvcreds,
                               NULL, NULL, NULL);
        if (LDAP_SUCCESS == ret) {
            break;
        } else if (LDAP_CONNECT_ERROR == ret && retry < 10) {
			retry++;
		} else {
            fprintf(stderr, "T%d: failed to bind, ldap_simple_bind_s"
                            "(%s, %s) returned 0x%x (errno %d)\n", 
                            st->id, dn, pw, ret, errno);
            *ld = NULL;
            return 0;
        }
    }
	return 1;
}

static int st_bind(SearchThread *st)
{
    if (!st->ld) {
#if defined(USE_OPENLDAP)
        int ret = 0;
        char *ldapurl = NULL;

        st->ld = NULL;
        ldapurl = PR_smprintf("ldap://%s:%d", hostname, port);
        ret = ldap_initialize(&st->ld, ldapurl);
        PR_smprintf_free(ldapurl);
        ldapurl = NULL;
        if (ret) {
            fprintf(stderr, "T%d: failed to init: %s port %d: %d:%s\n", st->id, hostname, port,
                    ret, ldap_err2string(ret));
            return 0;
        }
#else
        st->ld = ldap_init(hostname, port);
#endif
        if (!st->ld) {
            fprintf(stderr, "T%d: failed to init\n", st->id);
            return 0;
        }
    }
    if (!st->ld2) {        /* aux LDAP handle */
#if defined(USE_OPENLDAP)
        int ret = 0;
        char *ldapurl = NULL;

        st->ld2 = NULL;
        ldapurl = PR_smprintf("ldap://%s:%d", hostname, port);
        ret = ldap_initialize(&st->ld2, ldapurl);
        PR_smprintf_free(ldapurl);
        ldapurl = NULL;
        if (ret) {
            fprintf(stderr, "T%d: failed to init: %s port %d: %d:%s\n", st->id, hostname, port,
                    ret, ldap_err2string(ret));
            return 0;
        }
#else
        st->ld2 = ldap_init(hostname, port);
#endif
        if (!st->ld2) {
            fprintf(stderr, "T%d: failed to init 2\n", st->id);
            return 0;
        }
        if (0 == st_bind_core(st, &(st->ld2), strlen(bindDN) ? bindDN : NULL,
            strlen(bindPW) ? bindPW : NULL)) {
            return 0;
        }
    }

    if (opType != op_delete && opType != op_modify && opType != op_idxmodify &&
               sdattable && sdt_getlen(sdattable) > 0) {
        int e;
        char *dn, *uid, *upw;

        do {
            e = sdt_getrand(sdattable);
        } while (e < 0);
        dn = sdt_dn_get(sdattable, e);
        uid = sdt_uid_get(sdattable, e);
		/*  in this test, assuming uid == password unless told otherwise */
		upw = (userPW) ? userPW : uid;

        if (useBFile) {

            if (dn) {
                if (0 == st_bind_core(st, &(st->ld), dn, upw)) {
                    return 0;
                }
            } else if (uid) {
                char filterBuffer[100];
                char *pFilter;
				char *filterTemplate = (uidFilter) ? uidFilter : "(uid=%s)";
                struct timeval timeout;
                int scope = LDAP_SCOPE_SUBTREE, attrsOnly = 0;
                LDAPMessage *result;
                int retry = 0;
    
                pFilter = filterBuffer;
				sprintf(filterBuffer, filterTemplate, uid);
                timeout.tv_sec = 3600;
                timeout.tv_usec = 0;
                while (1) {
                    int ret = ldap_search_ext_s(st->ld2, suffix, scope, pFilter,
                                                NULL, attrsOnly, NULL, NULL,
                                                &timeout, -1, &result);
                    if (LDAP_SUCCESS == ret) {
                        break;
                    } else if ((LDAP_CONNECT_ERROR == ret ||
                               (LDAP_TIMEOUT == ret)) && retry < 10) {
                        retry++;
                    } else {
                        fprintf(stderr, "T%d: failed to search 1, error=0x%x\n",
                                st->id, ret);
                        return 0;
                    }
                }
                dn = ldap_get_dn(st->ld2, result);
    
                if (0 == st_bind_core(st, &(st->ld), dn, upw)) {
                    return 0;
                }
            } else {
                fprintf(stderr, "T%d: no data found, dn: %p, uid: %p\n", 
                        st->id, dn, uid);
                return 0;
            }
        } else {
            if (0 == st_bind_core(st, &(st->ld), dn, upw)) {
                return 0;
            }
        }
    } else {
        if (0 == st_bind_core(st, &(st->ld), strlen(bindDN) ? bindDN : NULL,
                              strlen(bindPW) ? bindPW : NULL)) {
            return 0;
        }
    }
    if (st->soc < 0) {
        if (ldap_get_option(st->ld, LDAP_OPT_DESC, (void *)&st->soc)
            != LDAP_SUCCESS) {
            fprintf(stderr, "T%d: failed on ldap_get_option\n", st->id);
            return 0;
        }
    }
    if (setLinger) {
        int val;
        struct linger l;
        val = sizeof(struct linger);
        l.l_onoff = 1;
        l.l_linger = 0;
        if (setsockopt(st->soc, SOL_SOCKET, SO_LINGER, (char *)&l, val) < 0) {
            fprintf(stderr, "T%d: failed in setsockopt 2, errno %d (%d)\n",
                    st->id, errno, (int)st->soc);
            st->soc = -1;
            return 0;
        }
    }
    return 1;
}

static void st_unbind(SearchThread *st)
{
    if (ldap_unbind_ext(st->ld, NULL, NULL) != LDAP_SUCCESS)
	fprintf(stderr, "T%d: failed to unbind\n", st->id);
    st->ld = NULL;
    st->soc = -1;
}

static int st_search(SearchThread *st)
{
    char filterBuffer[100];
    char *pFilter;
    struct timeval timeout;
    struct timeval *timeoutp;
    int scope, attrsOnly = 0;
    LDAPMessage *result;
    int ret;

    scope = myScope;
    if (ntable || numeric) {
        char *s = NULL;
        char num[8];

        if (! numeric) {
            do {
                s = nt_getrand(ntable);
            } while ((s) && (strlen(s) < 1));
        } else {
            sprintf(num, "%d", get_large_random_number() % numeric);
            s = num;
        }
        sprintf(filterBuffer, "%s%s", filter, s);
        pFilter = filterBuffer;
    } else {
        pFilter = filter;
    }

    /* Try to get attributes from the attrNameTable */
    if (!attrToReturn)
        attrToReturn = nt_get_all(attrTable);

    if (searchTimelimit <= 0) {
        timeoutp = NULL;
    } else {
        timeout.tv_sec = searchTimelimit;
        timeout.tv_usec = 0;
        timeoutp = &timeout;
    }
    ret = ldap_search_ext_s(st->ld, suffix, scope, pFilter, attrToReturn,
                            attrsOnly, NULL, NULL, timeoutp, -1, &result);
    if (ret != LDAP_SUCCESS) {
        fprintf(stderr, "T%d: failed to search 2, error=0x%02X\n",
                st->id, ret);
    }
    ldap_msgfree(result);
    return ret;
}

static void st_make_random_tel_number(char *pstr)
{
    static char *area_codes[] = {"303", "415", "408", "650", "216", "580", 0};

    int idx = rand() % 6;

    sprintf(pstr, "+1 %s %03d %04d",
	    area_codes[idx], rand() % 1000, rand() % 10000);
}

static int st_modify_nonidx(SearchThread *st)
{
    LDAPMod *attrs[2];
    LDAPMod attr_description;
    int e;
    int rval;
    char *dn = NULL;
    char description[256];
    char *description_values[2];

    /* Decide what entry to modify, for this we need a table */
    if (NULL == sdattable || sdt_getlen(sdattable) == 0) {
        fprintf(stderr, "-m option requires a DN file.  Use -B file.\n");
        return 0;
    }

    /* Get the target dn */
    do {
        e = sdt_getrand(sdattable);
    } while (e < 0);
    dn = sdt_dn_get(sdattable, e);

    sprintf(description, "%s modified at %lu", dn, time(NULL));
    description_values[0] = description;
    description_values[1] = NULL;

    attrs[0] = &attr_description;
    attrs[1] = NULL;

    attr_description.mod_op = LDAP_MOD_REPLACE;
    attr_description.mod_type = "description";
    attr_description.mod_values = description_values;

    rval = ldap_modify_ext_s(st->ld, dn, attrs, NULL, NULL);
    if (rval != LDAP_SUCCESS) {
        fprintf(stderr, "T%d: Failed to modify error=0x%x\n", st->id, rval);
        fprintf(stderr, "dn: %s\n", dn);
    }
    return rval;
}

static int st_modify_idx(SearchThread *st)
{
    LDAPMod *attrs[2];
    LDAPMod attr_telephonenumber;
    int e;
    int rval;
    char *dn = NULL;
    char telno[32];
    char *telephonenumber_values[2];

    /* Decide what entry to modify, for this we need a table */
    if (NULL == sdattable || sdt_getlen(sdattable) == 0) {
        fprintf(stderr, "-m option requires a DN file.  Use -B file.\n");
        return 0;
    }

    /* Get the target dn */
    do {
        e = sdt_getrand(sdattable);
    } while (e < 0);
    dn = sdt_dn_get(sdattable, e);

    /* Make new mod values */
    st_make_random_tel_number(telno);

    telephonenumber_values[0] = telno;
    telephonenumber_values[1] = NULL;

    attrs[0] = &attr_telephonenumber;
    attrs[1] = NULL;

    attr_telephonenumber.mod_op = LDAP_MOD_REPLACE;
    attr_telephonenumber.mod_type = "telephonenumber";
    attr_telephonenumber.mod_values = telephonenumber_values;

    rval = ldap_modify_ext_s(st->ld, dn, attrs, NULL, NULL);
    if (rval != LDAP_SUCCESS) {
        fprintf(stderr, "T%d: Failed to modify error=0x%x\n", st->id, rval);
        fprintf(stderr, "dn: %s\n", dn);
    }
    return rval;
}

static int st_compare(SearchThread *st)
{
    int rval;
    int compare_true;
    int correct_answer;
    int e;
    char *dn = NULL;
    char *uid = NULL;
    char uid0[100];
    struct berval bvvalue = {0, NULL};

    /* Decide what entry to modify, for this we need a table */
    if (NULL == sdattable || sdt_getlen(sdattable) == 0) {
        fprintf(stderr, "-c option requires a DN file.  Use -B file.\n");
        return 0;
    }

    /* Get the target dn */
    do {
        e = sdt_getrand(sdattable);
    } while (e < 0);
    dn = sdt_dn_get(sdattable, e);
    uid = sdt_uid_get(sdattable, e);

    compare_true = ( (rand() % 5) < 2 );

    if (!compare_true) {
        strcpy(uid0, uid);
        uid0[0] = '@';        /* make it not matched */
        uid = uid0;
    }
    bvvalue.bv_val = uid;
    bvvalue.bv_len = uid ? strlen(uid) : 0;
    rval = ldap_compare_ext_s(st->ld, dn, "uid", &bvvalue, NULL, NULL);
    correct_answer = compare_true ? LDAP_COMPARE_TRUE : LDAP_COMPARE_FALSE;
    if (rval == correct_answer) {
		rval = LDAP_SUCCESS;
    } else {
        fprintf(stderr, "T%d: Failed to compare error=0x%x (%d)\n",
                        st->id, rval, correct_answer);
        fprintf(stderr, "dn: %s, uid: %s\n", dn, uid);
    }
    return rval;
}

static int st_delete(SearchThread *st)
{
    char *dn = NULL;
    int rval;
    int e;

    /* Decide what entry to modify, for this we need a table */
    if (NULL == sdattable || sdt_getlen(sdattable) == 0) {
        fprintf(stderr, "-d option requires a DN file.  Use -B file.\n");
        return 0;
    }

    /* Get the target dn */
    do {
        e = sdt_getrand(sdattable);
    } while (e < 0);
    dn = sdt_dn_get(sdattable, e);

    rval = ldap_delete_ext_s(st->ld, dn, NULL, NULL);
    if (rval != LDAP_SUCCESS) {
        if (rval == LDAP_NO_SUCH_OBJECT) {
			rval = LDAP_SUCCESS;
        } else {
            fprintf(stderr, "T%d: Failed to delete error=0x%x\n", st->id, rval);
            fprintf(stderr, "dn: %s\n", dn);
        }
    }
    return rval;
}

/* the main thread */
void search_start(void *v)
{
    SearchThread *st = (SearchThread *)v;
    PRIntervalTime timer;
    int notBound = 1, res = LDAP_SUCCESS, searches = 0;
    PRUint32 span;

    st_seed(st);
    st->alive = 1;
    st->ld = 0;
    while (1) {
        timer = PR_IntervalNow();
        
        /* bind if we need to */
        if (doBind || notBound) {
            res = st_bind(st);
            if (noDelay)
                st_enableTCPnodelay(st);
            if (!res) {
                st_unbind(st);
                continue;        /* error */
            }
            notBound = 0;
        }

        /* do the operation */
        if (!noOp) {
            switch(opType) {
            case op_modify:
                res = st_modify_nonidx(st);
                break;
            case op_idxmodify:
                res = st_modify_idx(st);
                break;
            case op_search:
                res = st_search(st);
                break;
            case op_compare:
                res = st_compare(st);
                break;
            case op_delete:
                res = st_delete(st);
                break;
            default:
                fprintf(stderr, "Illegal operation type specified.\n");
                return;
            }
        }
		else {
			/* Fake status for NOOP */
			res = LDAP_SUCCESS;
		}
        if (LDAP_SUCCESS == res) {
            st->retry = 0;
        } else if (LDAP_CONNECT_ERROR == res && st->retry < 10) {
            st->retry++;
        } else {
               break;        /* error */
        }
        if (doBind) {
            if (noUnBind)
                st_disconnect(st);
            st_unbind(st);
        } else if (reconnect) {
            searches++;
            if (searches >= reconnect) {
                /* unceremoniously disconnect, reconnect next cycle */
                st_disconnect(st);
                st_unbind(st);
                notBound = 1;
                searches = 0;
            }
        }
        
        span = PR_IntervalToMilliseconds(PR_IntervalNow()-timer);
        /* update data */
        PR_Lock(st->lock);
        if (0 == st->retry) {    /* only when succeeded */
            st->searchCount++;
            if (st->mintime > span)
                st->mintime = span;
            if (st->maxtime < span)
                st->maxtime = span;
        }
        st->alive = 1;
        PR_Unlock(st->lock);
    }
}

/* fetches the current min/max times and the search count, and clears them */
void st_getCountMinMax(SearchThread *st, PRUint32 *count, PRUint32 *min,
		       PRUint32 *max)
{
    PR_Lock(st->lock);
    if (count) {
	*count = st->searchCount;
	st->searchCount = 0;
    }
    if (min) {
	*min = st->mintime;
	st->mintime = 10000;
    }
    if (max) {
	*max = st->maxtime;
	st->maxtime = 0;
    }
    st->alive--;
    PR_Unlock(st->lock);
}

int st_alive(SearchThread *st)
{
    int alive;

    PR_Lock(st->lock);
    alive = st->alive;
    PR_Unlock(st->lock);
    return alive;
}


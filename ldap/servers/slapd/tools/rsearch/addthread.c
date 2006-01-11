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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef XP_UNIX
#include <unistd.h>
#endif
#include "nspr.h"
#include <netinet/tcp.h>	/* for TCP_NODELAY */
#include "ldap.h"
#include "addthread.h"
#include "infadd.h"


/* local data for a search thread */
struct _addthread {
    PRUint32 addCount;
    PRUint32 addTotal;
    PRUint32 failCount;
    double mintime;
    double maxtime;
    LDAP *ld;
    PRThread *tid;
    PRLock *lock;
    int id;
    int alive;
    char *blob;
	int retry;
};


/*** unique id generator ***/
static unsigned long uniqueid = 0;
void at_initID(unsigned long i)
{
    uniqueid = i;	/* before threading */
}

unsigned long getID(void)
{
    static PRLock *lock = NULL;
    unsigned long ret;

    if (!lock) {
	/* initialize */
	lock = PR_NewLock();
    }
    PR_Lock(lock);
    ret = uniqueid++;
    PR_Unlock(lock);
    return ret;
}

    
/* new addthread */
AddThread *at_new(void)
{
    AddThread *at = (AddThread *)malloc(sizeof(AddThread));

    if (!at) return NULL;
    at->addCount = at->failCount = at->addTotal = 0;
    at->mintime = 10000;
    at->maxtime = 0;
    at->ld = NULL;
    at->tid = NULL;
    at->id = 0;
    at->alive = 1;
    at->retry = 0;
    at->lock = PR_NewLock();
    at->blob = NULL;
    /* make sure the id generator has initialized */
    getID();
    return at;
}

static void at_bail(AddThread *at)
{
    PR_Lock(at->lock);
    at->alive = -10;
    PR_Unlock(at->lock);
}

void at_setThread(AddThread *at, PRThread *tid, int id)
{
    at->tid = tid;
    at->id = id;
}

int at_getThread(AddThread *at, PRThread **tid)
{
    if (tid) *tid = at->tid;
    return at->id;
}


static void at_enableTCPnodelay(AddThread *at)
{
    LBER_SOCKET s = 0;
    int val = 1;

    if (ldap_get_option(at->ld, LDAP_OPT_DESC, (void *)&s) != LDAP_SUCCESS) {
	fprintf(stderr, "T%d: failed on ldap_get_option\n", at->id);
	return;
    }
    if (setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(val)))
	fprintf(stderr, "T%d: failed in setsockopt\n", at->id);
}

/* NOTE: currently these are unused */
#if 0
/* abruptly disconnect an LDAP connection without unbinding */
static void at_disconnect(AddThread *at)
{
    LBER_SOCKET s = 0;
    
    if (ldap_get_option(at->ld, LDAP_OPT_DESC, (void *)&s) != LDAP_SUCCESS) {
	fprintf(stderr, "T%d: failed on ldap_get_option\n", at->id);
	return;
    }
#ifdef XP_WIN
    if (closesocket(s))
	fprintf(stderr, "T%d: failed to disconnect\n", at->id);
#else
    if (close(s))
	fprintf(stderr, "T%d: failed to disconnect\n", at->id);
#endif
}
#endif

static void at_bind(AddThread *at)
{
    int ret;
    int retry = 0;

    at->ld = ldap_init(hostname, port);
    if (! at->ld) {
        fprintf(stderr, "T%d: failed to init: %s port %d\n", at->id, hostname, port);
        return;
    }
    while (retry < 10)
    {
        ret = ldap_simple_bind_s(at->ld, strlen(username) ? username : NULL,
                                 strlen(password) ? password : NULL);
        if (LDAP_SUCCESS == ret) {
            return;        /* ok */
        } else if (LDAP_CONNECT_ERROR == ret) {
            retry++;
        } else {
            break;
        }
    }
    fprintf(stderr, "T%d: failed to bind, ldap_simple_bind_s returned %d\n", 
                   at->id,  ret);
}

#if 0
static void at_unbind(AddThread *at)
{
    if (ldap_unbind(at->ld) != LDAP_SUCCESS)
	fprintf(stderr, "T%d: failed to unbind\n", at->id);
}
#endif  /* 0 */

static void at_random_tel_number(char *s)
{
    static char *areaCode[] = { "303", "408", "415", "423", "510",
				"650", "714", "803", "864", "901" };
    int index = rand() % 10;
   
    sprintf(s, "+1 %s %03d %04d", areaCode[index], rand()%1000, rand()%10000);
}

static int at_add(AddThread *at)
{
    LDAPMod *attrs[10];
    LDAPMod attr_cn, attr_sn, attr_givenname,
        attr_objectclass, attr_uid, attr_mail, attr_telephonenumber,
        attr_audio, attr_password;
    struct berval audio_berval;
    struct berval *audio_values[2];
    char dn[100], uid[10], telno[20], *sn, *givenname, cn[50], mail[50];
    char *cn_values[2], *sn_values[2], *givenname_values[2];
    char *uid_values[2], *mail_values[2], *telno_values[2];
#if 1
    char *objectclass_values[] = { "top", "person", "organizationalPerson",
                                   "inetOrgPerson", NULL };
#else
    char *objectclass_values[] = { "inetOrgPerson", NULL };
#endif
    int ret;

    /* make up the strings */
    sprintf(uid, "%lu", getID());
    at_random_tel_number(telno);
    sn = nt_getrand(family_names);
    givenname = nt_getrand(given_names);
    sprintf(cn, "%s %s %s", givenname, sn, uid);
    sprintf(mail, "%s%s@example.com", givenname, uid);
    sprintf(dn, "cn=%s,%s", cn, suffix);

    cn_values[0] = cn;
    cn_values[1] = NULL;
    sn_values[0] = sn;
    sn_values[1] = NULL;
    givenname_values[0] = givenname;
    givenname_values[1] = NULL;
    uid_values[0] = uid;
    uid_values[1] = NULL;
    mail_values[0] = mail;
    mail_values[1] = NULL;
    telno_values[0] = telno;
    telno_values[1] = NULL;
    
    attrs[0] = &attr_objectclass;
    attrs[1] = &attr_cn;
    attrs[2] = &attr_sn;
    attrs[3] = &attr_givenname;
    attrs[4] = &attr_uid;
    attrs[5] = &attr_password;
    attrs[6] = &attr_mail;
    attrs[7] = &attr_telephonenumber;
    if (blobsize > 0) {
        audio_values[0] = &audio_berval;
        audio_values[1] = 0;
        audio_berval.bv_len = (blobsize > 32000) ?
            ((long)rand() * 1039) % blobsize :
            (rand() % blobsize);
        audio_berval.bv_val = at->blob;
        attr_audio.mod_op = LDAP_MOD_BVALUES;
        attr_audio.mod_type = "audio";
        attr_audio.mod_values = (char **)&audio_values;
        attrs[8] = &attr_audio;
        attrs[9] = 0;
    }
    else
        attrs[8] = 0;

    attr_cn.mod_op = LDAP_MOD_ADD;
    attr_cn.mod_type = "cn";
    attr_cn.mod_values = cn_values;
    attr_sn.mod_op = LDAP_MOD_ADD;
    attr_sn.mod_type = "sn";
    attr_sn.mod_values = sn_values;
    attr_givenname.mod_op = LDAP_MOD_ADD;
    attr_givenname.mod_type = "givenname";
    attr_givenname.mod_values = givenname_values;
    attr_objectclass.mod_op = LDAP_MOD_ADD;
    attr_objectclass.mod_type = "objectClass";
    attr_objectclass.mod_values = objectclass_values;
    attr_uid.mod_op = LDAP_MOD_ADD;
    attr_uid.mod_type = "uid";
    attr_uid.mod_values = uid_values;
    attr_password.mod_op = LDAP_MOD_ADD;
    attr_password.mod_type = "userpassword";
    attr_password.mod_values = uid_values;
    attr_mail.mod_op = LDAP_MOD_ADD;
    attr_mail.mod_type = "mail";
    attr_mail.mod_values = mail_values;
    attr_telephonenumber.mod_op = LDAP_MOD_ADD;
    attr_telephonenumber.mod_type = "telephonenumber";
    attr_telephonenumber.mod_values = telno_values;

#if 0
    for (i = 0; attrs[i]; i++) {
        fprintf(stderr, "attr '%s': ", attrs[i]->mod_type);
        if (strcasecmp(attrs[i]->mod_type, "audio") == 0)
            fprintf(stderr, "binary data len=%lu\n", ((struct berval **)(attrs[i]->mod_values))[0]->bv_len);
        else 
            fprintf(stderr, "'%s'\n", attrs[i]->mod_values[0]);
    }
#endif
    ret = ldap_add_s(at->ld, dn, attrs);
	if (ret != LDAP_SUCCESS) {
        fprintf(stderr, "T%d: failed to add, error = %d\n", at->id, ret);
    }
    return ret;
}


/* the main thread */
void infadd_start(void *v)
{
    AddThread *at = (AddThread *)v;
    PRIntervalTime timer;
    PRUint32 span, i;
    int notBound = 1;
    int ret;

    /* make the blob if necessary */
    if (blobsize > 0) {
        at->blob = (char *)malloc(blobsize);
        if (! at->blob) {
            fprintf(stderr, "T%d: can't allocate blob!\n", at->id);
            return;
        }
        for (i = 0; i < blobsize; i++)
            at->blob[i] = (char)(rand() & 0xff);
    }

    at->alive = 1;
    while (1) {
        timer = PR_IntervalNow();
        
        /* bind if we need to */
        if (notBound) {
            at_bind(at);
            if (noDelay)
                at_enableTCPnodelay(at);
            notBound = 0;
        }

        ret = at_add(at);
        if (LDAP_SUCCESS == ret) {
            span = PR_IntervalToMilliseconds(PR_IntervalNow()-timer);
            /* update data */
            PR_Lock(at->lock);
            at->addCount++;
            at->addTotal++;
            if (at->mintime > span)
                at->mintime = span;
            if (at->maxtime < span)
                at->maxtime = span;
            at->alive = 1;
            at->retry = 0;
            PR_Unlock(at->lock);
        } else if (LDAP_CONNECT_ERROR == ret && at->retry < 10) {
            PR_Lock(at->lock);
            at->retry++;
            PR_Unlock(at->lock);
        } else {
            at_bail(at);
            return;
        }
        
    }
}

/* fetches the current min/max times and the search count, and clears them */
void at_getCountMinMax(AddThread *at, PRUint32 *count, PRUint32 *min,
		       PRUint32 *max, PRUint32 *total)
{
    PR_Lock(at->lock);
    if (count) {
        *count = at->addCount;
        at->addCount = 0;
    }
    if (min) {
        *min = at->mintime;
        at->mintime = 10000;
    }
    if (max) {
        *max = at->maxtime;
        at->maxtime = 0;
    }
    if (total)
        *total = at->addTotal;
    at->alive--;
    PR_Unlock(at->lock);
}

int at_alive(AddThread *at)
{
    int alive;

    PR_Lock(at->lock);
    alive = at->alive;
    PR_Unlock(at->lock);
    return alive;
}


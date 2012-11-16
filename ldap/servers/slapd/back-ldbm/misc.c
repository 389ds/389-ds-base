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
 * Copyright (C) 2005 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* misc.c - backend misc routines */

#include "back-ldbm.h"

/* Takes a return code supposed to be errno or from lidb
   which we don't expect to see and prints a handy log message */
void ldbm_nasty(const char* str, int c, int err)
{
    char *msg = NULL;
    char buffer[200];
    if (err == DB_LOCK_DEADLOCK) {
        PR_snprintf(buffer,200,"%s WARNING %d",str,c);
        LDAPDebug(LDAP_DEBUG_TRACE,"%s, err=%d %s\n",
                  buffer,err,(msg = dblayer_strerror( err )) ? msg : "");
   } else if (err == DB_RUNRECOVERY) {
        LDAPDebug2Args(LDAP_DEBUG_ANY, "FATAL ERROR at %s (%d); "
                  "server stopping as database recovery needed.\n", str, c);
        exit(1);
    } else {
        PR_snprintf(buffer,200,"%s BAD %d",str,c);
        LDAPDebug(LDAP_DEBUG_ANY, "%s, err=%d %s\n",
                  buffer, err, (msg = dblayer_strerror( err )) ? msg : "");
    }
}

/* Put a message in the access log, complete with connection ID and operation ID */
void ldbm_log_access_message(Slapi_PBlock *pblock,char *string)
{
    int ret = 0;
    PRUint64 connection_id = 0;
    int operation_id = 0;
    Operation *operation = NULL; /* DBDB this is sneaky---opid should be covered by the API directly */

    ret = slapi_pblock_get(pblock,SLAPI_OPERATION,&operation);
    if (0 != ret) {
        return;
    }
    ret = slapi_pblock_get(pblock,SLAPI_CONN_ID,&connection_id);
    if (0 != ret) {
        return;
    }
    operation_id = operation->o_opid;
    slapi_log_access( LDAP_DEBUG_STATS, "conn=%" NSPRIu64 " op=%d %s\n",connection_id, operation_id,string);
}

int return_on_disk_full(struct ldbminfo  *li)
{
    dblayer_remember_disk_filled(li);
    return SLAPI_FAIL_DISKFULL;
}


/* System Indexes */

static const char *systemIndexes[] = {
    "aci",
    LDBM_ENTRYDN_STR,
    LDBM_ENTRYRDN_STR,
    LDBM_NUMSUBORDINATES_STR,
    LDBM_TOMBSTONE_NUMSUBORDINATES_STR,
    LDBM_PARENTID_STR,
    SLAPI_ATTR_OBJECTCLASS,
    SLAPI_ATTR_UNIQUEID,
    SLAPI_ATTR_NSCP_ENTRYDN,
    ATTR_NSDS5_REPLCONFLICT,
    SLAPI_ATTR_ENTRYUSN,
    NULL
};


int
ldbm_attribute_always_indexed(const char *attrtype)
{
    int r= 0;
    if(NULL != attrtype)
    {
        int i=0;
        while (!r && systemIndexes[i] != NULL)
        {
            if(!strcasecmp(attrtype,systemIndexes[i]))
            {
                r= 1;
            }
            i++;
        }
    }
    return(r);
}



/*
 * Given an entry dn and a uniqueid, compute the
 * DN of the entry's tombstone. Returns a pointer
 * to an allocated block of memory.
 */
char *
compute_entry_tombstone_dn(const char *entrydn, const char *uniqueid)
{
    char *tombstone_dn;

    PR_ASSERT(NULL != entrydn);
    PR_ASSERT(NULL != uniqueid);

    tombstone_dn = slapi_ch_smprintf("%s=%s,%s",
        SLAPI_ATTR_UNIQUEID,
        uniqueid,
        entrydn);
    return tombstone_dn;
}

char *
compute_entry_tombstone_rdn(const char *entryrdn, const char *uniqueid)
{
    char *tombstone_rdn;

    PR_ASSERT(NULL != entryrdn);
    PR_ASSERT(NULL != uniqueid);

    tombstone_rdn = slapi_ch_smprintf("%s=%s,%s",
        SLAPI_ATTR_UNIQUEID,
        uniqueid,
        entryrdn);
    return tombstone_rdn;
}


/* mark a backend instance "busy"
 * returns 0 on success, -1 if the instance is ALREADY busy
 */
int instance_set_busy(ldbm_instance *inst)
{
    PR_Lock(inst->inst_config_mutex);
    if (inst->inst_flags & INST_FLAG_BUSY) {
        PR_Unlock(inst->inst_config_mutex);
        return -1;
    }

    inst->inst_flags |= INST_FLAG_BUSY;
    PR_Unlock(inst->inst_config_mutex);
    return 0;
}

int instance_set_busy_and_readonly(ldbm_instance *inst)
{
    PR_Lock(inst->inst_config_mutex);
    if (inst->inst_flags & INST_FLAG_BUSY) {
        PR_Unlock(inst->inst_config_mutex);
        return -1;
    }

    inst->inst_flags |= INST_FLAG_BUSY;

    /* save old readonly state */
    if (slapi_be_get_readonly(inst->inst_be)) {
        inst->inst_flags |= INST_FLAG_READONLY;
    } else {
        inst->inst_flags &= ~INST_FLAG_READONLY;
    }
    /* 
     * Normally, acquire rlock on be_lock, then lock inst_config_mutex.
     * instance_set_busy_and_readonly should release inst_config_mutex
     * before acquiring wlock on be_lock in slapi_mtn_be_set_readonly.
     */
    PR_Unlock(inst->inst_config_mutex);
    slapi_mtn_be_set_readonly(inst->inst_be, 1);

    return 0;
}

/* mark a backend instance to be not "busy" anymore */
void instance_set_not_busy(ldbm_instance *inst)
{
    int readonly;

    PR_Lock(inst->inst_config_mutex);
    inst->inst_flags &= ~INST_FLAG_BUSY;
    /* set backend readonly flag to match instance flags again
     * (sometimes the instance changes the readonly status when it's busy)
     */
    readonly = (inst->inst_flags & INST_FLAG_READONLY ? 1 : 0);
    slapi_mtn_be_set_readonly(inst->inst_be, readonly);
    PR_Unlock(inst->inst_config_mutex);
}

void
allinstance_set_not_busy(struct ldbminfo *li)
{
    ldbm_instance *inst;
    Object *inst_obj;

    /* server is up -- mark all backends busy */
    for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
        inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
        inst = (ldbm_instance *)object_get_data(inst_obj);
        instance_set_not_busy(inst);
    }
}

void
allinstance_set_busy(struct ldbminfo *li)
{
    ldbm_instance *inst;
    Object *inst_obj;

    /* server is up -- mark all backends busy */
    for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
        inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
        inst = (ldbm_instance *)object_get_data(inst_obj);
        if (instance_set_busy(inst)) {
            LDAPDebug1Arg(LDAP_DEBUG_TRACE, "could not set instance [%s] as busy, probably already busy\n",
                      inst->inst_name);
        }
    }
}

int
is_anyinstance_busy(struct ldbminfo *li)
{
    ldbm_instance *inst;
    Object *inst_obj;
    int rval = 0;

    /* server is up -- mark all backends busy */
    for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
        inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
        inst = (ldbm_instance *)object_get_data(inst_obj);
        PR_Lock(inst->inst_config_mutex);
        rval = inst->inst_flags & INST_FLAG_BUSY;
        PR_Unlock(inst->inst_config_mutex);
        if (0 != rval) {
            break;
        }
    }
    if (inst_obj)
        object_release(inst_obj);
    return rval;
}

/*
 * delete the given file/directory and its sub files/directories
 */
int
ldbm_delete_dirs(char *path)
{
    PRDir *dirhandle = NULL;
    PRDirEntry *direntry = NULL;
    char fullpath[MAXPATHLEN];
    int rval = 0;
    PRFileInfo info;

    dirhandle = PR_OpenDir(path);
    if (! dirhandle)
    {
        PR_Delete(path);
        return 0;
    }

    while (NULL != (direntry =
                    PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT)))
    {
        if (! direntry->name)
            break;

        PR_snprintf(fullpath, MAXPATHLEN, "%s/%s", path, direntry->name);
        rval = PR_GetFileInfo(fullpath, &info);
        if (PR_SUCCESS == rval)
        {
            if (PR_FILE_DIRECTORY == info.type)
                rval += ldbm_delete_dirs(fullpath);
        }
        if (PR_FILE_DIRECTORY != info.type)
            PR_Delete(fullpath);
    }
    PR_CloseDir(dirhandle);
    /* remove the directory itself too */
    rval += PR_RmDir(path);
    return rval;
}

char
get_sep(char *path)
{
    if (NULL == path)
        return '/';    /* default */
    if (NULL != strchr(path, '/'))
        return '/';
    if (NULL != strchr(path, '\\'))
        return '\\';
    return '/';    /* default */
}

/* mkdir -p */
int
mkdir_p(char *dir, unsigned int mode)
{
    PRFileInfo info;
    int rval;
    char sep = get_sep(dir);

    rval = PR_GetFileInfo(dir, &info);
    if (PR_SUCCESS == rval)
    {
        if (PR_FILE_DIRECTORY != info.type)    /* not a directory */
        {
            PR_Delete(dir);
            if (PR_SUCCESS != PR_MkDir(dir, mode))
            {
                LDAPDebug(LDAP_DEBUG_ANY, "mkdir_p %s: error %d (%s)\n",
                    dir, PR_GetError(),slapd_pr_strerror(PR_GetError()));
                return -1;
            }
        }
        return 0;
    }
    else
    {
        /* does not exist */
        char *p, *e;
        char c[2] = {0, 0};
        int len = strlen(dir);
        rval = 0;

        e = dir + len - 1;
        if (*e == sep)
        {
            c[1] = *e;
            *e = '\0';
        }

        c[0] = '/';
        p = strrchr(dir, sep);
        if (NULL != p)
        {
            *p = '\0';
            rval = mkdir_p(dir, mode);
            *p = c[0];
        }
        if (c[1])
            *e = c[1];
        if (0 != rval)
            return rval;
        if (PR_SUCCESS != PR_MkDir(dir, mode))
        {
            LDAPDebug(LDAP_DEBUG_ANY, "mkdir_p %s: error %d (%s)\n",
                    dir, PR_GetError(),slapd_pr_strerror(PR_GetError()));
            return -1;
        }
        return 0;
    }
}

/* This routine checks to see if there is a callback registered for retrieving
 * RUV updates to add to the datastore transaction.  If so, it allocates a
 * modify_context for consumption by the caller. */
int
ldbm_txn_ruv_modify_context( Slapi_PBlock *pb, modify_context *mc )
{
    char *uniqueid = NULL;
    backend *be;
    Slapi_Mods *smods = NULL;
    struct backentry *bentry;
    entry_address bentry_addr;
    IFP fn = NULL;
    int rc = 0;
    back_txn txn = {NULL};

    slapi_pblock_get(pb, SLAPI_TXN_RUV_MODS_FN, (void *)&fn);
    slapi_pblock_get(pb, SLAPI_TXN, &txn.back_txn_txn);
    
    if (NULL == fn) {
        return (0);
    }

    rc = (*fn)(pb, &uniqueid, &smods);

    /* Either something went wrong when the RUV callback tried to assemble
     * the updates for us, or there were no updates because the op doesn't
     * target a replica. */
    if (1 != rc || NULL == smods || NULL == uniqueid) {
        return (rc);
    }

    slapi_pblock_get( pb, SLAPI_BACKEND, &be);

    bentry_addr.sdn = NULL;
    bentry_addr.udn = NULL;
    bentry_addr.uniqueid = uniqueid;

    /* Note: if we find the bentry, it will stay locked until someone calls
     * modify_term on the mc we'll be associating the bentry with */
    bentry = find_entry2modify_only( pb, be, &bentry_addr, &txn );

    if (NULL == bentry) {
	/* Uh oh, we couldn't find and lock the RUV entry! */
        LDAPDebug( LDAP_DEBUG_ANY, "Error: ldbm_txn_ruv_modify_context failed to retrieve and lock RUV entry\n",
            0, 0, 0 );
        rc = -1;
        goto done;
    }

    modify_init( mc, bentry );

    if (modify_apply_mods_ignore_error( mc, smods, LDAP_TYPE_OR_VALUE_EXISTS )) {
        LDAPDebug( LDAP_DEBUG_ANY, "Error: ldbm_txn_ruv_modify_context failed to apply updates to RUV entry(%s)\n",
            backentry_get_ndn(mc->old_entry), 0 , 0 );
        rc = -1;
        modify_term( mc, be );
    }

done:
    slapi_ch_free_string( &uniqueid );
    /* No need to free smods; they get freed along with the modify context */

    return (rc);
}


int
is_fullpath(char *path)
{
    int len;
    if (NULL == path || '\0' == *path)
        return 0;

    if ('/' == *path || '\\' == *path)
        return 1;

    len = strlen(path);
    if (len > 2)
    {
        if (':' == path[1] && ('/' == path[2] || '\\' == path[2])) /* Windows */
            return 1;
    }
    return 0;
}

/* the problem with getline is that it inserts \0 for every
   newline \n or \r - this is a problem when you just want
   to grab some value from the ldif string but do not
   want to change the ldif string because it will be
   parsed again in the future
   openldap ldif_getline() is more of a problem because
   it does this for every comment line too, whereas mozldap
   ldif_getline() just skips comment lines
*/
static void
ldif_getline_fixline(char *start, char *end)
{
    while (start && (start < end)) {
        if (*start == '\0') {
            /* the original ldif string will usually end with \n \0
               ldif_getline will turn this into \0 \0
               in this case, we don't want to turn it into
               \r \n we want \n \0
            */
            if ((start < (end - 1)) && (*(start + 1) == '\0')) {
                *start = '\r';
                start++;
            }
            *start = '\n';
            start++;
        } else {
            start++;
        }
    }

    return;
}

/* 
 * Get value of type from string.
 * Note: this function is very primitive.  It does not support multi values.
 * This could be used to retrieve a single value as a string from raw data
 * read from db.
 */
/* caller is responsible to release "value" */
int
get_value_from_string(const char *string, char *type, char **value)
{
    int rc = -1;
    size_t typelen = 0;
    char *ptr = NULL;
    char *copy = NULL;
    char *tmpptr = NULL;
    char *startptr = NULL;
    struct berval tmptype = {0, NULL};
    struct berval bvvalue = {0, NULL};
    int freeval = 0;

    if (NULL == string || NULL == type || NULL == value) {
        return rc;
    }
    *value = NULL;
    tmpptr = (char *)string;
    ptr = PL_strcasestr(tmpptr, type);
    if (NULL == ptr) {
        return rc;
    }

    typelen = strlen(type);
    startptr = tmpptr;
    while (NULL != (ptr = ldif_getline(&tmpptr))) {
        if ((0 != PL_strncasecmp(ptr, type, typelen)) ||
            (*(ptr + typelen) != ';' && *(ptr + typelen) != ':')) {
            /* did not match */
            ldif_getline_fixline(startptr, tmpptr);
            startptr = tmpptr;
            continue;
        }
        /* matched */
        copy = slapi_ch_strdup(ptr);
        ldif_getline_fixline(startptr, tmpptr);
        startptr = tmpptr;
        rc = slapi_ldif_parse_line(copy, &tmptype, &bvvalue, &freeval);
        if (0 > rc || NULL == tmptype.bv_val ||
            NULL == bvvalue.bv_val || 0 >= bvvalue.bv_len) {
            slapi_log_error(SLAPI_LOG_FATAL, "get_value_from_string", "parse "
                                             "failed: %d\n", rc);
            if (freeval) {
                slapi_ch_free_string(&bvvalue.bv_val);
            }
            rc = -1; /* set non-0 to rc */
            goto bail;
        }
        if (0 != PL_strncasecmp(type, tmptype.bv_val, tmptype.bv_len)) {
            slapi_log_error(SLAPI_LOG_FATAL, "get_value_from_string", "type "
                                             "does not match: %s != %s\n", 
                                             type, tmptype.bv_val);
            if (freeval) {
                slapi_ch_free_string(&bvvalue.bv_val);
            }
            rc = -1; /* set non-0 to rc */
            goto bail;
        }
        rc = 0; /* set 0 to rc */
        if (freeval) {
            *value = bvvalue.bv_val; /* just hand off the memory */
            bvvalue.bv_val = NULL;
        } else { /* make a copy */
            *value = (char *)slapi_ch_malloc(bvvalue.bv_len + 1);
            memcpy(*value, bvvalue.bv_val, bvvalue.bv_len);
            *(*value + bvvalue.bv_len) = '\0';
        }
        slapi_ch_free_string(&copy);
    }
bail:
    slapi_ch_free_string(&copy);
    return rc;
}

/* 
 * Get value array of type from string.
 * multi-value support for get_value_from_string
 */
/* caller is responsible to release "valuearray" */
int
get_values_from_string(const char *string, char *type, char ***valuearray)
{
    int rc = -1;
    size_t typelen = 0;
    char *ptr = NULL;
    char *copy = NULL;
    char *tmpptr = NULL;
    char *startptr = NULL;
    struct berval tmptype = {0, NULL};
    struct berval bvvalue = {0, NULL};
    int freeval = 0;
    char *value = NULL;
    int idx = 0;
#define get_values_INITIALMAXCNT 1
    int maxcnt = get_values_INITIALMAXCNT;

    if (NULL == string || NULL == type || NULL == valuearray) {
        return rc;
    }
    *valuearray = NULL;
    tmpptr = (char *)string;
    ptr = PL_strcasestr(tmpptr, type);
    if (NULL == ptr) {
        return rc;
    }

    typelen = strlen(type);
    startptr = tmpptr;
    while (NULL != (ptr = ldif_getline(&tmpptr))) {
        if ((0 != PL_strncasecmp(ptr, type, typelen)) ||
            (*(ptr + typelen) != ';' && *(ptr + typelen) != ':')) {
            /* did not match */
            ldif_getline_fixline(startptr, tmpptr);
            startptr = tmpptr;
            continue;
        }
        /* matched */
        copy = slapi_ch_strdup(ptr);
        ldif_getline_fixline(startptr, tmpptr);
        startptr = tmpptr;
        rc = slapi_ldif_parse_line(copy, &tmptype, &bvvalue, &freeval);
        if (0 > rc || NULL == bvvalue.bv_val || 0 >= bvvalue.bv_len) {
            continue;
        }
        if (0 != PL_strncasecmp(type, tmptype.bv_val, tmptype.bv_len)) {
            char *p = PL_strchr(tmptype.bv_val, ';'); /* subtype ? */
            if (p) {
                if (0 != strncasecmp(type, tmptype.bv_val, p - tmptype.bv_val)) {
                    slapi_log_error(SLAPI_LOG_FATAL, "get_values_from_string", 
                                    "type does not match: %s != %s\n", 
                                    type, tmptype.bv_val);
                    if (freeval) {
                        slapi_ch_free_string(&bvvalue.bv_val);
                    }
                    goto bail;
                }
            } else {
                slapi_log_error(SLAPI_LOG_FATAL, "get_values_from_string", 
                                "type does not match: %s != %s\n", 
                                type, tmptype.bv_val);
                if (freeval) {
                    slapi_ch_free_string(&bvvalue.bv_val);
                }
                goto bail;
            }
        }
        if (freeval) {
            value = bvvalue.bv_val; /* just hand off memory */
            bvvalue.bv_val = NULL;
        } else { /* copy */
            value = (char *)slapi_ch_malloc(bvvalue.bv_len + 1);
            memcpy(value, bvvalue.bv_val, bvvalue.bv_len);
            *(value + bvvalue.bv_len) = '\0';
        }
        if ((get_values_INITIALMAXCNT == maxcnt) || !valuearray || 
            (idx + 1 >= maxcnt)) {
            maxcnt *= 2;
            *valuearray = (char **)slapi_ch_realloc((char *)*valuearray, 
                                                    sizeof(char *) * maxcnt);
        }
        (*valuearray)[idx++] = value;
        (*valuearray)[idx] = NULL;
        slapi_ch_free_string(&copy);
    }
bail:
    slapi_ch_free_string(&copy);
    return rc;
}

void
normalize_dir(char *dir)
{
    char *p = NULL;
    int l = 0;

    if (NULL == dir) {
        return;
    }
    l = strlen(dir);

    for (p = dir + l - 1; p && *p && (p > dir); p--) {
        if ((' ' != *p) && ('\t' != *p) && ('/' != *p) && ('\\' != *p)) {
            break;
        }
    }
    *(p+1) = '\0';
}


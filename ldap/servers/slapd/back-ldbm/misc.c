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

    tombstone_dn = slapi_ch_smprintf("%s=%s, %s",
        SLAPI_ATTR_UNIQUEID,
        uniqueid,
        entrydn);
    return tombstone_dn;
}

char *
compute_entry_tombstone_rdn(const char *entryrdn, const char *uniqueid)
{
    char *tombstone_rdn;

    PR_ASSERT(NULL != entrydn);
    PR_ASSERT(NULL != uniqueid);

    tombstone_rdn = slapi_ch_smprintf("%s=%s, %s",
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
    slapi_mtn_be_set_readonly(inst->inst_be, 1);

    PR_Unlock(inst->inst_config_mutex);
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
    if (inst_obj)
        object_release(inst_obj);
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
        instance_set_busy(inst);
    }
    if (inst_obj)
        object_release(inst_obj);
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
    char *tmptype = NULL;
    char *valueptr = NULL;
#if defined (USE_OPENLDAP)
    ber_len_t valuelen;
#else
    int valuelen;
#endif

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
    while (NULL != (ptr = ldif_getline(&tmpptr))) {
        if ((0 != PL_strncasecmp(ptr, type, typelen)) ||
            (*(ptr + typelen) != ';' && *(ptr + typelen) != ':')) {
            /* did not match */
            /* ldif_getline replaces '\n' and '\r' with '\0' */
            if ('\0' == *(tmpptr - 1)) {
                *(tmpptr - 1) = '\n';
            }
            if ('\0' == *(tmpptr - 2)) {
                *(tmpptr - 2) = '\r';
            }
            continue;
        }
        /* matched */
        copy = slapi_ch_strdup(ptr);
        /* ldif_getline replaces '\n' and '\r' with '\0' */
        if ('\0' == *(tmpptr - 1)) {
            *(tmpptr - 1) = '\n';
        }
        if ('\0' == *(tmpptr - 2)) {
            *(tmpptr - 2) = '\r';
        }
        rc = ldif_parse_line(copy, &tmptype, &valueptr, &valuelen);
        if (0 > rc || NULL == valueptr || 0 >= valuelen) {
            slapi_log_error(SLAPI_LOG_FATAL, "get_value_from_string", "parse "
                                             "failed: %d\n", rc);
            goto bail;
        }
        if (0 != strcasecmp(type, tmptype)) {
            slapi_log_error(SLAPI_LOG_FATAL, "get_value_from_string", "type "
                                             "does not match: %s != %s\n", 
                                             type, tmptype);
            goto bail;
        }
        *value = (char *)slapi_ch_malloc(valuelen + 1);
        memcpy(*value, valueptr, valuelen);
        *(*value + valuelen) = '\0';
    }
bail:
    slapi_ch_free_string(&copy);
    return rc;
}

/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* misc.c - backend misc routines */

#include "back-ldbm.h"

/* Takes a return code supposed to be errno or from lidb
   which we don't expect to see and prints a handy log message */
void ldbm_nasty(const char* str, int c, int err)
{
    char *msg = NULL;
    char buffer[200];
    if (err == DB_LOCK_DEADLOCK) {
        sprintf(buffer,"%s WARNING %d",str,c);
    LDAPDebug(LDAP_DEBUG_TRACE,"%s, err=%d %s\n",
          buffer,err,(msg = dblayer_strerror( err )) ? msg : "");
   } else if (err == DB_RUNRECOVERY) {
        LDAPDebug(LDAP_DEBUG_ANY,"FATAL ERROR at %s (%d); server stopping as database recovery needed.\n", str,c,0);
    exit(1);
    } else {
        sprintf(buffer,"%s BAD %d",str,c);
    LDAPDebug(LDAP_DEBUG_ANY,"%s, err=%d %s\n",
          buffer,err,(msg = dblayer_strerror( err )) ? msg : "");
    }
}

/* Put a message in the access log, complete with connection ID and operation ID */
void ldbm_log_access_message(Slapi_PBlock *pblock,char *string)
{
    int ret = 0;
    int connection_id = 0;
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
    slapi_log_access( LDAP_DEBUG_STATS, "conn=%d op=%d %s\n",connection_id, operation_id,string);
}

int return_on_disk_full(struct ldbminfo  *li)
{
    dblayer_remember_disk_filled(li);
    return SLAPI_FAIL_DISKFULL;
}


/* System Indexes */

static const char *systemIndexes[] = {
    "entrydn",
    "parentid",
    "objectclass",
    "aci",
    "numsubordinates",
    SLAPI_ATTR_UNIQUEID,
    SLAPI_ATTR_NSCP_ENTRYDN,
    ATTR_NSDS5_REPLCONFLICT,
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
    const char *tombstone_dn_pattern = "%s=%s, %s";
    char *tombstone_dn;

    PR_ASSERT(NULL != entrydn);
    PR_ASSERT(NULL != uniqueid);

    tombstone_dn = slapi_ch_malloc(strlen(SLAPI_ATTR_UNIQUEID) +
        strlen(tombstone_dn_pattern) +
        strlen(uniqueid) +
        strlen(entrydn) + 1);
    sprintf(tombstone_dn, tombstone_dn_pattern,
        SLAPI_ATTR_UNIQUEID,
        uniqueid,
        entrydn);
    return tombstone_dn;
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

        sprintf(fullpath, "%s/%s", path, direntry->name);
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

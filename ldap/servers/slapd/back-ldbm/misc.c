/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* misc.c - backend misc routines */

#include "back-ldbm.h"

void
ldbm_set_error(Slapi_PBlock *pb, int retval, int *ldap_result_code, char **ldap_result_message)
{
    int opreturn = 0;
    if (!(*ldap_result_code)) {
        slapi_pblock_get(pb, SLAPI_RESULT_CODE, ldap_result_code);
    }
    if (!(*ldap_result_code)) {
        *ldap_result_code = LDAP_OPERATIONS_ERROR;
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, ldap_result_code);
    }
    slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
    if (!opreturn) {
        slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, *ldap_result_code ? ldap_result_code : &retval);
    }
    slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, ldap_result_message);
}

/* Takes a return code supposed to be errno or from lidb
   which we don't expect to see and prints a handy log message */
void
ldbm_nasty(char *func, const char *str, int c, int err)
{
    const char *msg = NULL;
    char buffer[200];
    if (err == DBI_RC_RETRY) {
        PR_snprintf(buffer, 200, "%s WARNING %d", str, c);
        slapi_log_err(SLAPI_LOG_BACKLDBM, func, "%s, err=%d %s\n",
                      buffer, err, (msg = dblayer_strerror(err)) ? msg : "");
    } else if (err == DBI_RC_RUNRECOVERY) {
        slapi_log_err(SLAPI_LOG_ERR, func, "%s (%d); "
                                           "server stopping as database recovery needed.\n",
                      str, c);
        exit(1);
    } else {
        PR_snprintf(buffer, 200, "%s BAD %d", str, c);
        slapi_log_err(SLAPI_LOG_ERR, func, "%s, err=%d %s\n",
                      buffer, err, (msg = dblayer_strerror(err)) ? msg : "");
    }
}

/* Put a message in the access log, complete with connection ID and operation ID */
void
ldbm_log_access_message(Slapi_PBlock *pblock, char *string)
{
    int ret = 0;
    PRUint64 connection_id = 0;
    int operation_id = 0;
    Operation *operation = NULL; /* DBDB this is sneaky---opid should be covered by the API directly */

    ret = slapi_pblock_get(pblock, SLAPI_OPERATION, &operation);
    if (0 != ret) {
        return;
    }
    ret = slapi_pblock_get(pblock, SLAPI_CONN_ID, &connection_id);
    if (0 != ret) {
        return;
    }
    operation_id = operation->o_opid;
    slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d %s\n",
                     connection_id, operation_id, string);
}

int
return_on_disk_full(struct ldbminfo *li)
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
    SLAPI_ATTR_PARENTID,
    NULL};


int
ldbm_attribute_always_indexed(const char *attrtype)
{
    int r = 0;
    if (NULL != attrtype) {
        int i = 0;
        while (!r && systemIndexes[i] != NULL) {
            if (!strcasecmp(attrtype, systemIndexes[i])) {
                r = 1;
            }
            i++;
        }
    }
    return (r);
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
int
instance_set_busy(ldbm_instance *inst)
{
    PR_Lock(inst->inst_config_mutex);
    if (is_instance_busy(inst)) {
        PR_Unlock(inst->inst_config_mutex);
        return -1;
    }

    inst->inst_flags |= INST_FLAG_BUSY;
    PR_Unlock(inst->inst_config_mutex);
    return 0;
}

int
instance_set_busy_and_readonly(ldbm_instance *inst)
{
    PR_Lock(inst->inst_config_mutex);
    if (is_instance_busy(inst)) {
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
void
instance_set_not_busy(ldbm_instance *inst)
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
            slapi_log_err(SLAPI_LOG_TRACE, "allinstance_set_busy",
                          "Could not set instance [%s] as busy, probably already busy\n",
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

int
is_instance_busy(ldbm_instance *inst)
{
    return inst->inst_flags & INST_FLAG_BUSY;
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
    PRFileInfo64 info;

    dirhandle = PR_OpenDir(path);
    if (!dirhandle) {
        PR_Delete(path);
        return 0;
    }

    while (NULL != (direntry =
                        PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT))) {
        if (!direntry->name)
            break;

        PR_snprintf(fullpath, MAXPATHLEN, "%s/%s", path, direntry->name);
        rval = PR_GetFileInfo64(fullpath, &info);
        if (PR_SUCCESS == rval) {
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

/* This routine checks to see if there is a callback registered for retrieving
 * RUV updates to add to the datastore transaction.  If so, it allocates a
 * modify_context for consumption by the caller. */
int
ldbm_txn_ruv_modify_context(Slapi_PBlock *pb, modify_context *mc)
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
    /* or, the CSN is already covered by the RUV */
    if (1 != rc || NULL == smods || NULL == uniqueid) {
        return (rc);
    }

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);

    bentry_addr.sdn = NULL;
    bentry_addr.udn = NULL;
    bentry_addr.uniqueid = uniqueid;

    /* Note: if we find the bentry, it will stay locked until someone calls
     * modify_term on the mc we'll be associating the bentry with */
    bentry = find_entry2modify_only(pb, be, &bentry_addr, &txn, NULL);

    if (NULL == bentry) {
        /* Uh oh, we couldn't find and lock the RUV entry! */
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_txn_ruv_modify_context", "Failed to retrieve and lock RUV entry\n");
        rc = -1;
        goto done;
    }

    modify_init(mc, bentry);

    if (modify_apply_mods_ignore_error(mc, smods, LDAP_TYPE_OR_VALUE_EXISTS)) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_txn_ruv_modify_context", "Failed to apply updates to RUV entry\n");
        rc = -1;
        modify_term(mc, be);
    }

done:
    slapi_ch_free_string(&uniqueid);
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
    if (len > 2) {
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
    const char *ptr = NULL;
    const char *tmpptr = NULL;
    struct berval tmptype = {0, NULL};
    struct berval bvvalue = {0, NULL};
    int freeval = 0;
    struct berval copy = {0};

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
    while (NULL != (ptr = ldif_getline_ro(&tmpptr))) {
        if ((0 != PL_strncasecmp(ptr, type, typelen)) ||
            (*(ptr + typelen) != ';' && *(ptr + typelen) != ':')) {
            /* did not match */
            continue;
        }
        /* matched */
        dup_ldif_line(&copy, ptr, tmpptr);
        rc = slapi_ldif_parse_line(copy.bv_val, &tmptype, &bvvalue, &freeval);
        if (0 > rc || NULL == tmptype.bv_val ||
            NULL == bvvalue.bv_val || 0 >= bvvalue.bv_len) {
            slapi_log_err(SLAPI_LOG_ERR, "get_value_from_string",
                          "parse failed: %d\n", rc);
            if (freeval) {
                slapi_ch_free_string(&bvvalue.bv_val);
            }
            rc = -1; /* set non-0 to rc */
            goto bail;
        }
        if (0 != PL_strncasecmp(type, tmptype.bv_val, tmptype.bv_len)) {
            slapi_log_err(SLAPI_LOG_ERR, "get_value_from_string",
                          "type does not match: %s != %s\n", type, tmptype.bv_val);
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
    }
bail:
    slapi_ch_free_string(&copy.bv_val);
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
    const char *ptr = NULL;
    const char *tmpptr = NULL;
    struct berval tmptype = {0, NULL};
    struct berval bvvalue = {0, NULL};
    int freeval = 0;
    char *value = NULL;
    int idx = 0;
#define get_values_INITIALMAXCNT 1
    int maxcnt = get_values_INITIALMAXCNT;
    struct berval copy = {0};

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
    while (NULL != (ptr = ldif_getline_ro(&tmpptr))) {
        if ((0 != PL_strncasecmp(ptr, type, typelen)) ||
            (*(ptr + typelen) != ';' && *(ptr + typelen) != ':')) {
            /* did not match */
            continue;
        }
        /* matched */
        dup_ldif_line(&copy, ptr, tmpptr);
        rc = slapi_ldif_parse_line(copy.bv_val, &tmptype, &bvvalue, &freeval);
        if (0 > rc || NULL == bvvalue.bv_val || 0 >= bvvalue.bv_len) {
            continue;
        }
        if (0 != PL_strncasecmp(type, tmptype.bv_val, tmptype.bv_len)) {
            char *p = PL_strchr(tmptype.bv_val, ';'); /* subtype ? */
            if (p) {
                if (0 != strncasecmp(type, tmptype.bv_val, p - tmptype.bv_val)) {
                    slapi_log_err(SLAPI_LOG_ERR, "get_values_from_string",
                                  "type does not match: %s != %s\n",
                                  type, tmptype.bv_val);
                    if (freeval) {
                        slapi_ch_free_string(&bvvalue.bv_val);
                    }
                    goto bail;
                }
            } else {
                slapi_log_err(SLAPI_LOG_ERR, "get_values_from_string",
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
    }
bail:
    slapi_ch_free_string(&copy.bv_val);
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
    *(p + 1) = '\0';
}

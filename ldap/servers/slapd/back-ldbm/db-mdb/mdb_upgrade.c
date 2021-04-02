/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2019 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/* upgrade.c --- upgrade from a previous version of the database */

#include "mdb_layer.h"

/*
 * ldbm_compat_versions holds DBVERSION strings for all versions of the
 * database with which we are (upwards) compatible.  If check_db_version
 * encounters a database with a version that is not listed in this array,
 * we display a warning message.
 */

db_upgrade_info mdb_ldbm_version_suss[] = {
    /*
     * char *old_version_string;
     * int   old_dbversion_major;
     * int   old_dbversion_minor;
     * int   type;
     * int   action;
     */
    /* for mdb/#.#/..., we don't have to put the version number in the 2nd col
       since DBVERSION keeps it */
    {BDB_IMPL, 4, 0, DBVERSION_NEW_IDL, DBVERSION_UPGRADE_4_5, 1},
    {LDBM_VERSION, 4, 2, DBVERSION_NEW_IDL, DBVERSION_NO_UPGRADE, 0},
    {LDBM_VERSION_OLD, 4, 2, DBVERSION_OLD_IDL, DBVERSION_NO_UPGRADE, 0},
    {LDBM_VERSION_62, 4, 2, DBVERSION_OLD_IDL, DBVERSION_NO_UPGRADE, 0},
    {LDBM_VERSION_61, 3, 3, DBVERSION_OLD_IDL, DBVERSION_UPGRADE_3_4, 0},
    {LDBM_VERSION_60, 3, 3, DBVERSION_OLD_IDL, DBVERSION_UPGRADE_3_4, 0},
    {NULL, 0, 0, 0, 0, 0}};


/* clear the following flag to suppress "database files do not exist" warning
int ldbm_warn_if_no_db = 0;
*/
/* global LDBM version in the db home */

int
mdb_lookup_dbversion(char *dbversion, int flag)
{
#ifdef TODO
    int i, matched = 0;
    int rval = 0; /* == DBVERSION_NO_UPGRADE */

    for (i = 0; mdb_ldbm_version_suss[i].old_version_string != NULL; ++i) {
        if (PL_strncasecmp(dbversion, mdb_ldbm_version_suss[i].old_version_string,
                           strlen(mdb_ldbm_version_suss[i].old_version_string)) == 0) {
            matched = 1;
            break;
        }
    }
    if (matched) {
        if (flag & DBVERSION_TYPE) /* lookup request for type */
        {
            rval |= mdb_ldbm_version_suss[i].type;
            if (strstr(dbversion, BDB_RDNFORMAT)) {
                /* dbversion contains rdn-format == subtree-rename format */
                rval |= DBVERSION_RDN_FORMAT;
            }
        }
        if (flag & DBVERSION_ACTION) /* lookup request for action */
        {
            int dbmajor = 0, dbminor = 0;
            if (mdb_ldbm_version_suss[i].is_dbd) {
                /* case of mdb/#.#/... */
                char *p = strchr(dbversion, '/');
                char *endp = dbversion + strlen(dbversion);
                if (NULL != p && p < endp) {
                    char *dotp = strchr(++p, '.');
                    if (NULL != dotp) {
                        *dotp = '\0';
                        dbmajor = strtol(p, (char **)NULL, 10);
                        dbminor = strtol(++dotp, (char **)NULL, 10);
                    } else {
                        dbmajor = strtol(p, (char **)NULL, 10);
                    }
                }
            } else {
                dbmajor = mdb_ldbm_version_suss[i].old_dbversion_major;
                dbminor = mdb_ldbm_version_suss[i].old_dbversion_minor;
            }
            if (dbmajor < DB_VERSION_MAJOR) {
                /* 3 -> 4 or 5 -> 6 */
                rval |= mdb_ldbm_version_suss[i].action;
            } else if (dbminor < DB_VERSION_MINOR) {
                /* 4.low -> 4.high */
                rval |= DBVERSION_UPGRADE_4_4;
            }
        }
    }
    return rval;
#endif /* TODO */
}

/*
 * this function reads the db/DBVERSION file and check
 * 1) if the db version is supported, and
 * 2) if the db version requires some migration operation
 *
 * return: 0: supported
 *         DBVERSION_NOT_SUPPORTED: not supported
 *
 * action: 0: nothing is needed
 *         DBVERSION_UPGRADE_3_4: db3->db4 uprev is needed
 *         DBVERSION_UPGRADE_4_4: db4->db4 uprev is needed
 *         DBVERSION_UPGRADE_4_5: db4->db  uprev is needed
 */
int
mdb_check_db_version(struct ldbminfo *li, int *action)
{
#ifdef TODO
    int value = 0;
    int result = 0;
    char *ldbmversion = NULL;
    char *dataversion = NULL;

    *action = 0;
    result = mdb_version_read(li, li->li_directory, &ldbmversion, &dataversion);
    if (result != 0) {
        return 0;
    } else if (NULL == ldbmversion || '\0' == *ldbmversion) {
        slapi_ch_free_string(&ldbmversion);
        slapi_ch_free_string(&dataversion);
        return 0;
    }

    value = mdb_lookup_dbversion(ldbmversion, DBVERSION_TYPE | DBVERSION_ACTION);
    if (!value) {
        slapi_log_err(SLAPI_LOG_ERR, "mdb_check_db_version",
                      "Database version mismatch (expecting "
                      "'%s' but found '%s' in directory %s)\n",
                      LDBM_VERSION, ldbmversion, li->li_directory);
        /*
         * A non-zero return here will cause slapd to exit during startup.
         */
        slapi_ch_free_string(&ldbmversion);
        slapi_ch_free_string(&dataversion);
        return DBVERSION_NOT_SUPPORTED;
    }
    if (value & DBVERSION_UPGRADE_3_4) {
        mdb_set_recovery_required(li);
        *action = DBVERSION_UPGRADE_3_4;
    } else if (value & DBVERSION_UPGRADE_4_4) {
        mdb_set_recovery_required(li);
        *action = DBVERSION_UPGRADE_4_4;
    } else if (value & DBVERSION_UPGRADE_4_5) {
        mdb_set_recovery_required(li);
        *action = DBVERSION_UPGRADE_4_5;
    }
    if (value & DBVERSION_RDN_FORMAT) {
        if (entryrdn_get_switch()) {
            /* nothing to do */
        } else {
            *action |= DBVERSION_NEED_RDN2DN;
        }
    } else {
        if (entryrdn_get_switch()) {
            *action |= DBVERSION_NEED_DN2RDN;
        } else {
            /* nothing to do */
        }
    }
    slapi_ch_free_string(&ldbmversion);
    slapi_ch_free_string(&dataversion);
    return 0;
#endif /* TODO */
}

/*
 * this function reads the db/<inst>/DBVERSION file and check
 * 1) if the db version is supported, and
 * 2) if the db version matches the idl configuration
 *    (nsslapd-idl-switch: new|old)
 *    note that old idl will disappear from the next major update (6.5? 7.0?)
 *
 * return: 0: supported and the version matched
 *         DBVERSION_NEED_IDL_OLD2NEW: old->new uprev is needed
 *                                     (used in convindices)
 *         DBVERSION_NEED_IDL_NEW2OLD: old db is found, for the new idl config
 *         DBVERSION_NOT_SUPPORTED: not supported
 *
 *         DBVERSION_UPGRADE_3_4: db3->db4 uprev is needed
 *         DBVERSION_UPGRADE_4_4: db4->db4 uprev is needed
 *         DBVERSION_UPGRADE_4_5: db4->db  uprev is needed
 */
int
mdb_check_db_inst_version(ldbm_instance *inst)
{
#ifdef TODO
    int value = 0;
    char *ldbmversion = NULL;
    char *dataversion = NULL;
    int rval = 0;
    int result = 0;
    char inst_dir[MAXPATHLEN * 2];
    char *inst_dirp = NULL;

    inst_dirp =
        dblayer_get_full_inst_dir(inst->inst_li, inst, inst_dir, MAXPATHLEN * 2);

    result = mdb_version_read(inst->inst_li, inst_dirp, &ldbmversion, &dataversion);
    if (result != 0) {
        return rval;
    } else if (NULL == ldbmversion || '\0' == *ldbmversion) {
        slapi_ch_free_string(&ldbmversion);
        slapi_ch_free_string(&dataversion);
        return rval;
    }

    value = mdb_lookup_dbversion(ldbmversion, DBVERSION_TYPE | DBVERSION_ACTION);
    if (!value) {
        slapi_log_err(SLAPI_LOG_ERR, "mdb_check_db_inst_version",
                      "Database version mismatch (expecting "
                      "'%s' but found '%s' in directory %s)\n",
                      LDBM_VERSION, ldbmversion, inst->inst_dir_name);
        /*
         * A non-zero return here will cause slapd to exit during startup.
         */
        slapi_ch_free_string(&ldbmversion);
        slapi_ch_free_string(&dataversion);
        return DBVERSION_NOT_SUPPORTED;
    }

    /* recognize the difference between an old/new database regarding idl
     * (406922) */
    if (idl_get_idl_new() && !(value & DBVERSION_NEW_IDL)) {
        rval |= DBVERSION_NEED_IDL_OLD2NEW;
    } else if (!idl_get_idl_new() && !(value & DBVERSION_OLD_IDL)) {
        rval |= DBVERSION_NEED_IDL_NEW2OLD;
    }
    if (value & DBVERSION_UPGRADE_3_4) {
        rval |= DBVERSION_UPGRADE_3_4;
    } else if (value & DBVERSION_UPGRADE_4_4) {
        rval |= DBVERSION_UPGRADE_4_4;
    } else if (value & DBVERSION_UPGRADE_4_5) {
        rval |= DBVERSION_UPGRADE_4_5;
    }
    if (value & DBVERSION_RDN_FORMAT) {
        if (entryrdn_get_switch()) {
            /* nothing to do */
        } else {
            rval |= DBVERSION_NEED_RDN2DN;
        }
    } else {
        if (entryrdn_get_switch()) {
            rval |= DBVERSION_NEED_DN2RDN;
        } else {
            /* nothing to do */
        }
    }
    if (inst_dirp != inst_dir)
        slapi_ch_free_string(&inst_dirp);
    slapi_ch_free_string(&ldbmversion);
    slapi_ch_free_string(&dataversion);
    return rval;
#endif /* TODO */
}

/*
 * adjust_idl_switch
 * if the current nsslapd-idl-switch is different from ldbmversion,
 * update the value of nsslapd-idl-switch (in LDBM_CONFIG_ENTRY)
 */
int
mdb_adjust_idl_switch(char *ldbmversion, struct ldbminfo *li)
{
#ifdef TODO
    int rval = 0;

    if (!li->li_idl_update) {
        /* we are not overriding the idl type */
        return rval;
    }

    li->li_flags |= LI_FORCE_MOD_CONFIG;
    if ((0 == PL_strncasecmp(ldbmversion, BDB_IMPL, strlen(BDB_IMPL))) ||
        (0 == PL_strcmp(ldbmversion, LDBM_VERSION))) /* db: new idl */
    {
        if (!idl_get_idl_new()) /* config: old idl */
        {
            replace_ldbm_config_value(CONFIG_IDL_SWITCH, "new", li);
            slapi_log_err(SLAPI_LOG_WARNING, "mdb_adjust_idl_switch",
                          "Dbversion %s does not meet nsslapd-idl-switch: \"old\"; "
                          "nsslapd-idl-switch is updated to \"new\"\n",
                          ldbmversion);
        }
    } else if ((0 == strcmp(ldbmversion, LDBM_VERSION_OLD)) ||
               (0 == PL_strcmp(ldbmversion, LDBM_VERSION_61)) ||
               (0 == PL_strcmp(ldbmversion, LDBM_VERSION_62)) ||
               (0 == strcmp(ldbmversion, LDBM_VERSION_60))) /* db: old */
    {
        if (idl_get_idl_new()) /* config: new */
        {
            replace_ldbm_config_value(CONFIG_IDL_SWITCH, "old", li);
            slapi_log_err(SLAPI_LOG_WARNING, "mdb_adjust_idl_switch",
                          "Dbversion %s does not meet nsslapd-idl-switch: \"new\"; "
                          "nsslapd-idl-switch is updated to \"old\"\n",
                          ldbmversion);
        }
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "mdb_adjust_idl_switch",
                      "Dbversion %s is not supported\n",
                      ldbmversion);
        rval = -1;
    }

    /* ldbminfo is a common resource; should clean up when the job is done */
    li->li_flags &= ~LI_FORCE_MOD_CONFIG;
    return rval;
#endif /* TODO */
}

/* Do the work to upgrade a database if needed */
/* When we're called, the database files have been opened, and any
recovery needed has been performed. */
int
mdb_ldbm_upgrade(ldbm_instance *inst, int action)
{
#ifdef TODO
    int rval = 0;

    if (0 == action) {
        return rval;
    }

    /* upgrade from db3 to db4 or db4 to db5 */
    if (action & (DBVERSION_UPGRADE_3_4 | DBVERSION_UPGRADE_4_5)) {
        rval = mdb_update_db_ext(inst, LDBM_SUFFIX_OLD, LDBM_SUFFIX);
        if (0 == rval) {
            slapi_log_err(SLAPI_LOG_ERR, "mdb_ldbm_upgrade",
                          "Upgrading instance %s supporting mdb %d.%d "
                          "was successfully done.\n",
                          inst->inst_name, DB_VERSION_MAJOR, DB_VERSION_MINOR);
        } else {
            /* recovery effort ... */
            mdb_update_db_ext(inst, LDBM_SUFFIX, LDBM_SUFFIX_OLD);
        }
    }

    return rval;
#endif /* TODO */
}

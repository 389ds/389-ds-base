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


/* upgrade.c --- upgrade from a previous version of the database */

#include "back-ldbm.h"

/*
 * ldbm_compat_versions holds DBVERSION strings for all versions of the
 * database with which we are (upwards) compatible.  If check_db_version
 * encounters a database with a version that is not listed in this array,
 * we display a warning message.
 */

db_upgrade_info ldbm_version_suss[] = {
    /*
     * char *old_version_string;
     * int   old_dbversion_major;
     * int   old_dbversion_minor;
     * int   type;
     * int   action;
     */
    /* for bdb/#.#/..., we don't have to put the version number in the 2nd col
       since DBVERSION keeps it */
    {BDB_IMPL,         4, 0, DBVERSION_NEW_IDL, DBVERSION_UPGRADE_4_5, 1},
    {LDBM_VERSION,     4, 2, DBVERSION_NEW_IDL, DBVERSION_NO_UPGRADE,  0},
    {LDBM_VERSION_OLD, 4, 2, DBVERSION_OLD_IDL, DBVERSION_NO_UPGRADE,  0}, 
    {LDBM_VERSION_62,  4, 2, DBVERSION_OLD_IDL, DBVERSION_NO_UPGRADE,  0}, 
    {LDBM_VERSION_61,  3, 3, DBVERSION_OLD_IDL, DBVERSION_UPGRADE_3_4, 0}, 
    {LDBM_VERSION_60,  3, 3, DBVERSION_OLD_IDL, DBVERSION_UPGRADE_3_4, 0}, 
    {NULL,             0, 0, 0,                 0,                     0}
};


/* clear the following flag to suppress "database files do not exist" warning
int ldbm_warn_if_no_db = 0;
*/
/* global LDBM version in the db home */

int
lookup_dbversion(char *dbversion, int flag)
{
    int i, matched = 0;
    int rval = 0; /* == DBVERSION_NO_UPGRADE */

    for ( i = 0; ldbm_version_suss[i].old_version_string != NULL; ++i )
    {
        if (PL_strncasecmp(dbversion, ldbm_version_suss[i].old_version_string,
                          strlen(ldbm_version_suss[i].old_version_string)) == 0)
        {
            matched = 1;
            break;
        }
    }
    if ( matched )
    {
        if ( flag & DBVERSION_TYPE ) /* lookup request for type */
        {
            rval |= ldbm_version_suss[i].type;
            if (strstr(dbversion, BDB_RDNFORMAT)) {
                /* dbversion contains rdn-format == subtree-rename format */
                rval |= DBVERSION_RDN_FORMAT;
            }
        }
        if ( flag & DBVERSION_ACTION ) /* lookup request for action */
        {
            int dbmajor = 0, dbminor = 0;
            if (ldbm_version_suss[i].is_dbd)
            {
                /* case of bdb/#.#/... */
                char *p = strchr(dbversion, '/');
                char *endp = dbversion + strlen(dbversion);
                if (NULL != p && p < endp)
                {
                    char *dotp = strchr(++p, '.');
                    if (NULL != dotp)
                    {
                        *dotp = '\0';
                        dbmajor = strtol(p, (char **)NULL, 10);
                        dbminor = strtol(++dotp, (char **)NULL, 10);
                    }
                    else
                    {
                        dbmajor = strtol(p, (char **)NULL, 10);
                    }
                }
            }
            else
            {
                dbmajor = ldbm_version_suss[i].old_dbversion_major;
                dbminor = ldbm_version_suss[i].old_dbversion_minor;
            }
            if (dbmajor < DB_VERSION_MAJOR)
            {
                /* 3 -> 4 or 5 -> 6 */
                rval |= ldbm_version_suss[i].action;
            }
            else if (dbminor < DB_VERSION_MINOR)
            {
                /* 4.low -> 4.high */
                rval |= DBVERSION_UPGRADE_4_4;
            }
        }
    }
    return rval;
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
check_db_version( struct ldbminfo *li, int *action )
{
    int value = 0;
    char *ldbmversion = NULL;
    char *dataversion = NULL;

    *action = 0;
    dbversion_read(li, li->li_directory, &ldbmversion, &dataversion);
    if (NULL == ldbmversion || '\0' == *ldbmversion) {
        slapi_ch_free_string(&dataversion);
        return 0;
    }

    value = lookup_dbversion( ldbmversion, DBVERSION_TYPE | DBVERSION_ACTION );
    if ( !value )
    {
        LDAPDebug( LDAP_DEBUG_ANY,
           "ERROR: Database version mismatch (expecting "
           "'%s' but found '%s' in directory %s)\n",
            LDBM_VERSION, ldbmversion, li->li_directory );
        /*
         * A non-zero return here will cause slapd to exit during startup.
         */
        slapi_ch_free_string(&ldbmversion);
        slapi_ch_free_string(&dataversion);
        return DBVERSION_NOT_SUPPORTED;
    }
    if ( value & DBVERSION_UPGRADE_3_4 )
    {
        dblayer_set_recovery_required(li);
        *action = DBVERSION_UPGRADE_3_4;
    }
    else if ( value & DBVERSION_UPGRADE_4_4 )
    {
        dblayer_set_recovery_required(li);
        *action = DBVERSION_UPGRADE_4_4;
    }
    else if ( value & DBVERSION_UPGRADE_4_5 )
    {
        dblayer_set_recovery_required(li);
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
check_db_inst_version( ldbm_instance *inst )
{
    int value = 0;
    char *ldbmversion = NULL;
    char *dataversion = NULL;
    int rval = 0;
    char inst_dir[MAXPATHLEN*2];
    char *inst_dirp = NULL;

    inst_dirp =
        dblayer_get_full_inst_dir(inst->inst_li, inst, inst_dir, MAXPATHLEN*2);

    dbversion_read(inst->inst_li, inst_dirp, &ldbmversion, &dataversion);
    if (NULL == ldbmversion || '\0' == *ldbmversion) {
        return rval;
    }
    
    value = lookup_dbversion( ldbmversion, DBVERSION_TYPE | DBVERSION_ACTION );
    if ( !value )
    {
        LDAPDebug( LDAP_DEBUG_ANY,
           "ERROR: Database version mismatch (expecting "
           "'%s' but found '%s' in directory %s)\n",
            LDBM_VERSION, ldbmversion, inst->inst_dir_name );
        /*
         * A non-zero return here will cause slapd to exit during startup.
         */
        slapi_ch_free_string(&ldbmversion);
        slapi_ch_free_string(&dataversion);
        return DBVERSION_NOT_SUPPORTED;
    }

    /* recognize the difference between an old/new database regarding idl
     * (406922) */
    if (idl_get_idl_new() && !(value & DBVERSION_NEW_IDL) )
    {
        rval |= DBVERSION_NEED_IDL_OLD2NEW;
    }
    else if (!idl_get_idl_new() && !(value & DBVERSION_OLD_IDL) )
    {
        rval |= DBVERSION_NEED_IDL_NEW2OLD;
    }
    if ( value & DBVERSION_UPGRADE_3_4 )
    {
        rval |= DBVERSION_UPGRADE_3_4;
    }
    else if ( value & DBVERSION_UPGRADE_4_4 )
    {
        rval |= DBVERSION_UPGRADE_4_4;
    }
    else if ( value & DBVERSION_UPGRADE_4_5 )
    {
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
}

/*
 * adjust_idl_switch
 * if the current nsslapd-idl-switch is different from ldbmversion,
 * update the value of nsslapd-idl-switch (in LDBM_CONFIG_ENTRY)
 */
int
adjust_idl_switch(char *ldbmversion, struct ldbminfo *li)
{
    int rval = 0;

    li->li_flags |= LI_FORCE_MOD_CONFIG;
    if ((0 == PL_strncasecmp(ldbmversion, BDB_IMPL, strlen(BDB_IMPL))) ||
        (0 == PL_strcmp(ldbmversion, LDBM_VERSION)))    /* db: new idl */
    {
        if (!idl_get_idl_new())   /* config: old idl */
        {
            replace_ldbm_config_value(CONFIG_IDL_SWITCH, "new", li);
            LDAPDebug(LDAP_DEBUG_ANY, 
                "Warning: Dbversion %s does not meet nsslapd-idl-switch: \"old\"; "
                "nsslapd-idl-switch is updated to \"new\"\n",

                ldbmversion, 0, 0);
        }
    }
    else if ((0 == strcmp(ldbmversion, LDBM_VERSION_OLD)) ||
             (0 == PL_strcmp(ldbmversion, LDBM_VERSION_61)) ||
             (0 == PL_strcmp(ldbmversion, LDBM_VERSION_62)) ||
             (0 == strcmp(ldbmversion, LDBM_VERSION_60)))    /* db: old */
    {
        if (idl_get_idl_new())   /* config: new */
        {
            replace_ldbm_config_value(CONFIG_IDL_SWITCH, "old", li);
            LDAPDebug(LDAP_DEBUG_ANY, 
                "Warning: Dbversion %s does not meet nsslapd-idl-switch: \"new\"; "
                "nsslapd-idl-switch is updated to \"old\"\n",
                ldbmversion, 0, 0);
        }
    }
    else
    {
         LDAPDebug(LDAP_DEBUG_ANY, 
                   "Warning: Dbversion %s is not supported\n", 
                   ldbmversion, 0, 0);
         rval = 1;
    }

    /* ldbminfo is a common resource; should clean up when the job is done */
    li->li_flags &= ~LI_FORCE_MOD_CONFIG;
    return rval;
}

/* Do the work to upgrade a database if needed */
/* When we're called, the database files have been opened, and any
recovery needed has been performed. */
int ldbm_upgrade(ldbm_instance *inst, int action)
{
    int rval = 0;

    if (0 == action)
    {
        return rval;
    }

    /* upgrade from db3 to db4 or db4 to db5 */
    if (action & (DBVERSION_UPGRADE_3_4|DBVERSION_UPGRADE_4_5))
    {
        rval = dblayer_update_db_ext(inst, LDBM_SUFFIX_OLD, LDBM_SUFFIX);
        if (0 == rval)
        {
            LDAPDebug(LDAP_DEBUG_ANY, "ldbm_upgrade: "
                      "Upgrading instance %s supporting bdb %d.%d "
                      "was successfully done.\n",
                      inst->inst_name, DB_VERSION_MAJOR, DB_VERSION_MINOR);
        }
        else
        {
            /* recovery effort ... */
            dblayer_update_db_ext(inst, LDBM_SUFFIX, LDBM_SUFFIX_OLD);
        }
    }

    return rval;
}

/* Here's the upgrade process : 
    Delete all the keys from the parentid index
    Scan the id2entry file:
        Remove any hassubordinates attribute present
        Update the parentid index, maintaining a hash of high-count parents
    Scan the newly created parentid index updating the subordinatecount attributes.

    Most of the functionality is implemented in the import code.
 */
#if 0
static int upgrade_db_3x_40(backend *be)
{
    struct ldbminfo *li = (struct ldbminfo *) be->be_database->plg_private;
    int ret = 0;
    back_txn        txn;

    static char* indexes_modified[] = {"parentid", "numsubordinates", NULL};

    LDAPDebug( LDAP_DEBUG_ANY, "WARNING: Detected a database older than this server, upgrading data...\n",0,0,0);

    dblayer_txn_init(li,&txn);
    ret = dblayer_txn_begin(li,NULL,&txn);
    if (0 != ret) {
        ldbm_nasty(filename,69,ret);
        goto error;
    }
    ret = indexfile_delete_all_keys(be,"parentid",&txn);
    if (0 != ret) {
        ldbm_nasty(filename,70,ret);
        goto error;
    }

    {
        Slapi_Mods smods;
           slapi_mods_init(&smods,1);
        /* Mods are to remove the hassubordinates attribute */
        slapi_mods_add(&smods, LDAP_MOD_DELETE, "hassubordinates", 0, NULL);
        /* This function takes care of generating the subordinatecount attribute and indexing it */
        ret = indexfile_primary_modifyall(be,slapi_mods_get_ldapmods_byref(&smods),indexes_modified,&txn);
        slapi_mods_done(&smods);
    }

    if (0 != ret) {
        ldbm_nasty(filename,61,ret);
    }

error:
    if (0 != ret ) {
        dblayer_txn_abort(li,&txn);
    } else {
        ret = dblayer_txn_commit(li,&txn);
        if (0 != ret) {
            ldbm_nasty(filename,60,ret);
        } else {
            /* Now update DBVERSION file */
        }
    }
    if (0 == ret) {
        LDAPDebug( LDAP_DEBUG_ANY, "...upgrade complete.\n",0,0,0);
    } else {
        LDAPDebug( LDAP_DEBUG_ANY, "ERROR: Attempt to upgrade the older database FAILED.\n",0,0,0);
    }
    return ret;
}

#endif

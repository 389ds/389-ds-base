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

#include <stdio.h>
#include <string.h>
#include "portable.h"
#include "slapi-plugin.h"
#include "slap.h"

/* include NSPR header files */
#include "prthread.h"
#include "prlock.h"
#include "prerror.h"
#include "prcvar.h"
#include "prio.h"

/* get file mode flags for unix */
#ifndef _WIN32
#include <sys/stat.h>
#endif

#ifdef _WIN32
#define REFERINT_DEFAULT_FILE_MODE	0
#else
#define REFERINT_DEFAULT_FILE_MODE S_IRUSR | S_IWUSR
#endif

#define REFERINT_PLUGIN_SUBSYSTEM   "referint-plugin"   /* used for logging */
#define MAX_LINE 2048
#define READ_BUFSIZE  4096
#define MY_EOF   0 

/* function prototypes */
int referint_postop_init( Slapi_PBlock *pb ); 
int referint_postop_del( Slapi_PBlock *pb ); 
int referint_postop_modrdn( Slapi_PBlock *pb ); 
int referint_postop_start( Slapi_PBlock *pb);
int referint_postop_close( Slapi_PBlock *pb);
int update_integrity(char **argv, Slapi_DN *sDN, char *newrDN, Slapi_DN *newsuperior, int logChanges);
int GetNextLine(char *dest, int size_dest, PRFileDesc *stream);
int my_fgetc(PRFileDesc *stream);
void referint_thread_func(void *arg);
void writeintegritylog(Slapi_PBlock *pb, char *logfilename, Slapi_DN *sdn, char *newrdn, Slapi_DN *newsuperior, Slapi_DN *requestorsdn);

/* global thread control stuff */
static PRLock 		*referint_mutex = NULL;       
static PRThread		*referint_tid = NULL;
static PRLock 		*keeprunning_mutex = NULL;
static PRCondVar    *keeprunning_cv = NULL;
static int keeprunning = 0;
static int refint_started = 0;

static Slapi_PluginDesc pdesc = { "referint", VENDOR, DS_PACKAGE_VERSION, "referential integrity plugin" };
static int allow_repl = 0;
static void* referint_plugin_identity = NULL;

static int use_txn = 0;

#ifdef _WIN32
int *module_ldap_debug = 0;

void plugin_init_debug_level(int *level_ptr)
{
	module_ldap_debug = level_ptr;
}
#endif

static void
referint_lock()
{
    if (use_txn) { /* no lock if betxn is enabled */
        return;
    }
    if (NULL == referint_mutex) {
        referint_mutex = PR_NewLock();
    }
    if (referint_mutex) {
        PR_Lock(referint_mutex);
    }
}

static void
referint_unlock()
{
    if (use_txn) { /* no lock if betxn is enabled */
        return;
    }
    if (referint_mutex) {
        PR_Unlock(referint_mutex);
    }
}

int
referint_postop_init( Slapi_PBlock *pb )
{
    Slapi_Entry *plugin_entry = NULL;
    char *plugin_type = NULL;
    int delfn = SLAPI_PLUGIN_POST_DELETE_FN;
    int mdnfn = SLAPI_PLUGIN_POST_MODRDN_FN;

    /*
     *  Get plugin identity and stored it for later use.
     *  Used for internal operations.
     */
    slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &referint_plugin_identity);
    PR_ASSERT (referint_plugin_identity);

    /* get the args */
    if ((slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_ENTRY, &plugin_entry) == 0) &&
         plugin_entry &&
         (plugin_type = slapi_entry_attr_get_charptr(plugin_entry, "nsslapd-plugintype")) &&
         plugin_type && strstr(plugin_type, "betxn"))
    {
        delfn = SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN;
        mdnfn = SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN;
        use_txn = 1;
    }
    if(plugin_entry){
        char *allow_repl_updates;

        allow_repl_updates = slapi_entry_attr_get_charptr(plugin_entry, "nsslapd-pluginAllowReplUpdates");
        if(allow_repl_updates && strcasecmp(allow_repl_updates,"on")==0){
            allow_repl = 1;
        }
        slapi_ch_free_string(&allow_repl_updates);
    }
    slapi_ch_free_string(&plugin_type);

    if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01 ) != 0 ||
         slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&pdesc ) != 0 ||
         slapi_pblock_set( pb, delfn, (void *) referint_postop_del ) != 0 ||
         slapi_pblock_set( pb, mdnfn, (void *) referint_postop_modrdn ) != 0 ||
         slapi_pblock_set( pb, SLAPI_PLUGIN_START_FN, (void *) referint_postop_start ) != 0 ||
         slapi_pblock_set( pb, SLAPI_PLUGIN_CLOSE_FN, (void *) referint_postop_close ) != 0)
    {
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM, "referint_postop_init failed\n" );
        return( -1 );
    }

    return( 0 );
}


int
referint_postop_del( Slapi_PBlock *pb )
{
    Slapi_DN *sdn = NULL;
    char **argv;
    int argc;
    int delay;
    int logChanges=0;
    int isrepop = 0;
    int oprc;
    int rc;

    if (0 == refint_started) {
        /* not initialized yet */
        return SLAPI_PLUGIN_SUCCESS;
    }

    if ( slapi_pblock_get( pb, SLAPI_IS_REPLICATED_OPERATION, &isrepop ) != 0  ||
         slapi_pblock_get( pb, SLAPI_DELETE_TARGET_SDN, &sdn ) != 0  ||
         slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &oprc) != 0)
    {
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
            "referint_postop_del: could not get parameters\n" );
        return SLAPI_PLUGIN_FAILURE;
    }
    /*
     *  This plugin should only execute if the delete was successful
     *  and this is not a replicated op(unless its allowed)
     */
    if(oprc != 0 || (isrepop && !allow_repl)){
        return SLAPI_PLUGIN_SUCCESS;
    }
    /* get the args */
    if ( slapi_pblock_get( pb, SLAPI_PLUGIN_ARGC, &argc ) != 0) {
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
            "referint_postop failed to get argc\n" );
        return SLAPI_PLUGIN_FAILURE;
    }
    if ( slapi_pblock_get( pb, SLAPI_PLUGIN_ARGV, &argv ) != 0) {
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
            "referint_postop failed to get argv\n" );
        return SLAPI_PLUGIN_FAILURE;
    }

    if(argv == NULL){
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
            "referint_postop_del, args are NULL\n" );
        return SLAPI_PLUGIN_FAILURE;
    }

    if (argc >= 3) {
        /* argv[0] will be the delay */
        delay = atoi(argv[0]);

        /* argv[2] will be wether or not to log changes */
        logChanges = atoi(argv[2]);

        if(delay == -1){
            /* integrity updating is off */
            rc = SLAPI_PLUGIN_SUCCESS;
        } else if(delay == 0){ /* no delay */
            /* call function to update references to entry */
            rc = update_integrity(argv, sdn, NULL, NULL, logChanges);
        } else {
            /* write the entry to integrity log */
            writeintegritylog(pb, argv[1], sdn, NULL, NULL, NULL /* slapi_get_requestor_sdn(pb) */);
            rc = SLAPI_PLUGIN_SUCCESS;
        }
    } else {
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
            "referint_postop insufficient arguments supplied\n" );
        return SLAPI_PLUGIN_FAILURE;
    }

    return( rc );
}

int
referint_postop_modrdn( Slapi_PBlock *pb )
{
    Slapi_DN *sdn = NULL;
    Slapi_DN *newsuperior;
    char *newrdn;
    char **argv;
    int oprc;
    int rc;
    int argc = 0;
    int delay;
    int logChanges=0;
    int isrepop = 0;

    if ( slapi_pblock_get( pb, SLAPI_IS_REPLICATED_OPERATION, &isrepop ) != 0  ||
         slapi_pblock_get( pb, SLAPI_MODRDN_TARGET_SDN, &sdn ) != 0 ||
         slapi_pblock_get( pb, SLAPI_MODRDN_NEWRDN, &newrdn ) != 0 ||
         slapi_pblock_get( pb, SLAPI_MODRDN_NEWSUPERIOR_SDN, &newsuperior ) != 0 ||
         slapi_pblock_get( pb, SLAPI_PLUGIN_OPRETURN, &oprc) != 0 )
    {
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
            "referint_postop_modrdn: could not get parameters\n" );
        return SLAPI_PLUGIN_FAILURE;
    }
    /*
     *  This plugin should only execute if the delete was successful
     *  and this is not a replicated op (unless its allowed)
     */
    if(oprc != 0 || (isrepop && !allow_repl)){
        return SLAPI_PLUGIN_SUCCESS;
    }
    /* get the args */
    if ( slapi_pblock_get( pb, SLAPI_PLUGIN_ARGC, &argc ) != 0) {
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
            "referint_postop failed to get argv\n" );
        return SLAPI_PLUGIN_FAILURE;
    }
    if ( slapi_pblock_get( pb, SLAPI_PLUGIN_ARGV, &argv ) != 0) {
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
            "referint_postop failed to get argv\n" );
        return SLAPI_PLUGIN_FAILURE;
    }
    if(argv == NULL){
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
            "referint_postop_modrdn, args are NULL\n" );
        return SLAPI_PLUGIN_FAILURE;
    }

    if (argc >= 3) {
        /* argv[0] will always be the delay */
        delay = atoi(argv[0]);

        /* argv[2] will be wether or not to log changes */
        logChanges = atoi(argv[2]);
    } else {
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
            "referint_postop_modrdn insufficient arguments supplied\n" );
        return SLAPI_PLUGIN_FAILURE;
    }

    if(delay == -1){
        /* integrity updating is off */
        rc = SLAPI_PLUGIN_SUCCESS;
    } else if(delay == 0){ /* no delay */
        /* call function to update references to entry */
        rc = update_integrity(argv, sdn, newrdn, newsuperior, logChanges);
    } else {
        /* write the entry to integrity log */
        writeintegritylog(pb, argv[1], sdn, newrdn, newsuperior, NULL /* slapi_get_requestor_sdn(pb) */);
        rc = SLAPI_PLUGIN_SUCCESS;
    }

    return( rc );
}

int isFatalSearchError(int search_result)
{
    /*   Make sure search result is fatal 
     *   Some conditions that happen quite often are not fatal 
     *   for example if you have two suffixes and one is null, you will always
     *   get no such object, however this is not a fatal error.
     *   Add other conditions to the if statement as they are found
     */
    switch(search_result) {
        case LDAP_REFERRAL:
        case LDAP_NO_SUCH_OBJECT: return 0 ;
    }
    return 1;
}

static int
_do_modify(Slapi_PBlock *mod_pb, Slapi_DN *entrySDN, LDAPMod **mods)
{
    int rc = 0;

    slapi_pblock_init(mod_pb);

    if(allow_repl){
    	/* Must set as a replicated operation */
    	slapi_modify_internal_set_pb_ext(mod_pb, entrySDN, mods, NULL, NULL,
                                         referint_plugin_identity, OP_FLAG_REPLICATED);
    } else {
    	slapi_modify_internal_set_pb_ext(mod_pb, entrySDN, mods, NULL, NULL,
                                         referint_plugin_identity, 0);
    }
    slapi_modify_internal_pb(mod_pb);
    slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    
    return rc;
}

/*
 * update one attribute value per _do_modify
 */
static int
_update_one_per_mod(Slapi_DN *entrySDN,      /* DN of the searched entry */
                    Slapi_Attr *attr,        /* referred attribute */
                    char *attrName,
                    Slapi_DN *origDN,        /* original DN that was modified */
                    char *newRDN,            /* new RDN from modrdn */
                    const char *newsuperior, /* new superior from modrdn */
                    Slapi_PBlock *mod_pb)
{
    LDAPMod attribute1, attribute2;
    LDAPMod *list_of_mods[3];
    char *values_del[2];
    char *values_add[2];
    char *newDN = NULL;
    char **dnParts = NULL;
    char *sval = NULL;
    char *newvalue = NULL;
    char *p = NULL;
    size_t dnlen = 0;
    int rc = 0;

    if (NULL == newRDN && NULL == newsuperior) {
        /* in delete mode */
        /* delete old dn so set that up */
        values_del[0] = (char *)slapi_sdn_get_dn(origDN);
        values_del[1] = NULL;
        attribute1.mod_type = attrName;
        attribute1.mod_op = LDAP_MOD_DELETE;
        attribute1.mod_values = values_del;
        list_of_mods[0] = &attribute1;
        /* terminate list of mods. */
        list_of_mods[1] = NULL;
        rc = _do_modify(mod_pb, entrySDN, list_of_mods);
        if (rc) {
            slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
                "_update_one_value: entry %s: deleting \"%s: %s\" failed (%d)"
                "\n", slapi_sdn_get_dn(entrySDN), attrName, slapi_sdn_get_dn(origDN), rc);
        }
    } else {
        /* in modrdn mode */
        const char *superior = NULL;
        int nval = 0;
        Slapi_Value *v = NULL;

        if (NULL == origDN) {
            slapi_log_error(SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
                            "_update_one_value: NULL dn was passed\n");
            goto bail;
        }
        /* need to put together rdn into a dn */
        dnParts = slapi_ldap_explode_dn( slapi_sdn_get_dn(origDN), 0 );
        if (NULL == dnParts) {
            slapi_log_error(SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
                            "_update_one_value: failed to explode dn %s\n",
                            slapi_sdn_get_dn(origDN));
            goto bail;
        }
        if (NULL == newRDN) {
            newRDN = dnParts[0];
        }
        if (newsuperior) {
            superior = newsuperior;
        } else {
            /* do not free superior */
            superior = slapi_dn_find_parent(slapi_sdn_get_dn(origDN));
        }
        /* newRDN and superior are already normalized. */
        newDN = slapi_ch_smprintf("%s,%s", newRDN, superior);
        slapi_dn_ignore_case(newDN);
        /* 
         * Compare the modified dn with the value of 
         * the target attribute of referint to find out
         * the modified dn is the ancestor (case 2) or
         * the value itself (case 1).
         *
         * E.g., 
         * (case 1) 
         * modrdn: uid=A,ou=B,o=C --> uid=A',ou=B',o=C
         *            (origDN)             (newDN)
         * member: uid=A,ou=B,ou=C --> uid=A',ou=B',ou=C
         *            (sval)               (newDN)
         *
         * (case 2) 
         * modrdn: ou=B,o=C --> ou=B',o=C
         *         (origDN)      (newDN)
         * member: uid=A,ou=B,ou=C --> uid=A,ou=B',ou=C
         *         (sval)              (sval' + newDN)
         */
        for (nval = slapi_attr_first_value(attr, &v); nval != -1;
             nval = slapi_attr_next_value(attr, nval, &v)) {
            p = NULL;
            dnlen = 0;

            /* DN syntax, which should be a string */
            sval = slapi_ch_strdup(slapi_value_get_string(v));
            rc = slapi_dn_normalize_case_ext(sval, 0,  &p, &dnlen);
            if (rc == 0) { /* sval is passed in; not terminated */
                *(p + dnlen) = '\0';
                sval = p;
            } else if (rc > 0) {
                slapi_ch_free_string(&sval);
                sval = p;
            }
            /* else: (rc < 0) Ignore the DN normalization error for now. */

            p = PL_strstr(sval, slapi_sdn_get_ndn(origDN));
            if (p == sval) {
                /* (case 1) */
                values_del[0] = sval;
                values_del[1] = NULL;
                attribute1.mod_type = attrName;
                attribute1.mod_op = LDAP_MOD_DELETE;
                attribute1.mod_values = values_del;
                list_of_mods[0] = &attribute1;

                values_add[0] = newDN;
                values_add[1] = NULL;
                attribute2.mod_type = attrName;
                attribute2.mod_op = LDAP_MOD_ADD;
                attribute2.mod_values = values_add;
                list_of_mods[1] = &attribute2;
                list_of_mods[2] = NULL;
                rc = _do_modify(mod_pb, entrySDN, list_of_mods);
                if (rc) {
                    slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
                        "_update_one_value: entry %s: replacing \"%s: %s\" "
                        "with \"%s: %s\" failed (%d)\n",
                        slapi_sdn_get_dn(entrySDN), attrName, 
                        slapi_sdn_get_dn(origDN), attrName, newDN, rc);
                }
            } else if (p) {
                char bak;
                /* (case 2) */
                values_del[0] = sval;
                values_del[1] = NULL;
                attribute1.mod_type = attrName;
                attribute1.mod_op = LDAP_MOD_DELETE;
                attribute1.mod_values = values_del;
                list_of_mods[0] = &attribute1;

                bak = *p;
                *p = '\0';
                /* newRDN and superior are already normalized. */
                newvalue = slapi_ch_smprintf("%s%s", sval, newDN);
                *p = bak;
                values_add[0]=newvalue;
                values_add[1]=NULL;
                attribute2.mod_type = attrName;
                attribute2.mod_op = LDAP_MOD_ADD;
                attribute2.mod_values = values_add;
                list_of_mods[1] = &attribute2;
                list_of_mods[2] = NULL;
                rc = _do_modify(mod_pb, entrySDN, list_of_mods);
                if (rc) {
                    slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
                        "_update_one_value: entry %s: replacing \"%s: %s\" "
                        "with \"%s: %s\" failed (%d)\n",
                        slapi_sdn_get_dn(entrySDN), attrName, sval, attrName, newvalue, rc);
                }
                slapi_ch_free_string(&newvalue);
            } 
            /* else: value does not include the modified DN.  Ignore it. */
            slapi_ch_free_string(&sval);
        }

        /* cleanup memory allocated for dnParts and newDN */
        if (dnParts){
            slapi_ldap_value_free(dnParts);
            dnParts = NULL;
        }
        slapi_ch_free_string(&newDN);
    }
bail:
    return rc;
}

/*
 * update multiple attribute values per _do_modify
 */
static int
_update_all_per_mod(Slapi_DN *entrySDN,      /* DN of the searched entry */
                    Slapi_Attr *attr,        /* referred attribute */
                    char *attrName,
                    Slapi_DN *origDN,        /* original DN that was modified */
                    char *newRDN,            /* new RDN from modrdn */
                    const char *newsuperior, /* new superior from modrdn */
                    Slapi_PBlock *mod_pb)
{
    Slapi_Mods *smods = NULL;
    char *newDN = NULL;
    char **dnParts = NULL;
    char *sval = NULL;
    char *newvalue = NULL;
    char *p = NULL;
    size_t dnlen = 0;
    int rc = 0;
    int nval = 0;

    slapi_attr_get_numvalues(attr, &nval);

    if (NULL == newRDN && NULL == newsuperior) {
        /* in delete mode */
        LDAPMod *mods[2];
        char *values_del[2];
        LDAPMod attribute1;

        /* delete old dn so set that up */
        values_del[0] = (char *)slapi_sdn_get_dn(origDN);
        values_del[1] = NULL;
        attribute1.mod_type = attrName;
        attribute1.mod_op = LDAP_MOD_DELETE;
        attribute1.mod_values = values_del;
        mods[0] = &attribute1;
        /* terminate list of mods. */
        mods[1] = NULL;
        rc = _do_modify(mod_pb, entrySDN, mods);
        if (rc) {
            slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
                "_update_all_per_mod: entry %s: deleting \"%s: %s\" failed (%d)"
                "\n", slapi_sdn_get_dn(entrySDN), attrName, slapi_sdn_get_dn(origDN), rc);
        }
    } else {
        /* in modrdn mode */
        const char *superior = NULL;
        int nval = 0;
        Slapi_Value *v = NULL;

        if (NULL == origDN) {
            slapi_log_error(SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
                            "_update_all_per_mod: NULL dn was passed\n");
            goto bail;
        }
        /* need to put together rdn into a dn */
        dnParts = slapi_ldap_explode_dn( slapi_sdn_get_dn(origDN), 0 );
        if (NULL == dnParts) {
            slapi_log_error(SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
                            "_update_all_per_mod: failed to explode dn %s\n",
                            slapi_sdn_get_dn(origDN));
            goto bail;
        }
        if (NULL == newRDN) {
            newRDN = dnParts[0];
        }
        if (newsuperior) {
            superior = newsuperior;
        } else {
            /* do not free superior */
            superior = slapi_dn_find_parent(slapi_sdn_get_dn(origDN));
        }
        /* newRDN and superior are already normalized. */
        newDN = slapi_ch_smprintf("%s,%s", newRDN, superior);
        slapi_dn_ignore_case(newDN);
        /* 
         * Compare the modified dn with the value of 
         * the target attribute of referint to find out
         * the modified dn is the ancestor (case 2) or
         * the value itself (case 1).
         *
         * E.g., 
         * (case 1) 
         * modrdn: uid=A,ou=B,o=C --> uid=A',ou=B',o=C
         *            (origDN)             (newDN)
         * member: uid=A,ou=B,ou=C --> uid=A',ou=B',ou=C
         *            (sval)               (newDN)
         *
         * (case 2) 
         * modrdn: ou=B,o=C --> ou=B',o=C
         *         (origDN)      (newDN)
         * member: uid=A,ou=B,ou=C --> uid=A,ou=B',ou=C
         *         (sval)              (sval' + newDN)
         */
        slapi_attr_get_numvalues(attr, &nval);
        smods = slapi_mods_new();
        slapi_mods_init(smods, 2 * nval + 1);

        for (nval = slapi_attr_first_value(attr, &v);
             nval != -1;
             nval = slapi_attr_next_value(attr, nval, &v)) {
            p = NULL;
            dnlen = 0;

            /* DN syntax, which should be a string */
            sval = slapi_ch_strdup(slapi_value_get_string(v));
            rc = slapi_dn_normalize_case_ext(sval, 0,  &p, &dnlen);
            if (rc == 0) { /* sval is passed in; not terminated */
                *(p + dnlen) = '\0';
                sval = p;
            } else if (rc > 0) {
                slapi_ch_free_string(&sval);
                sval = p;
            }
            /* else: (rc < 0) Ignore the DN normalization error for now. */

            p = PL_strstr(sval, slapi_sdn_get_ndn(origDN));
            if (p == sval) {
                /* (case 1) */
                slapi_mods_add_string(smods, LDAP_MOD_DELETE, attrName, sval);
                slapi_mods_add_string(smods, LDAP_MOD_ADD, attrName, newDN);

            } else if (p) {
                /* (case 2) */
                slapi_mods_add_string(smods, LDAP_MOD_DELETE, attrName, sval);
                *p = '\0';
                newvalue = slapi_ch_smprintf("%s%s", sval, newDN);
                slapi_mods_add_string(smods, LDAP_MOD_ADD, attrName, newvalue);
                slapi_ch_free_string(&newvalue);
            } 
            /* else: value does not include the modified DN.  Ignore it. */
            slapi_ch_free_string(&sval);
        }
        rc = _do_modify(mod_pb, entrySDN, slapi_mods_get_ldapmods_byref(smods));
        if (rc) {
            slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
                        "_update_all_per_mod: entry %s failed (%d)\n",
                        slapi_sdn_get_dn(entrySDN), rc);
        }

        /* cleanup memory allocated for dnParts and newDN */
        if (dnParts){
            slapi_ldap_value_free(dnParts);
            dnParts = NULL;
        }
        slapi_ch_free_string(&newDN);
        slapi_mods_free(&smods);
    }

bail:
    return rc;
}

int
update_integrity(char **argv, Slapi_DN *origSDN,
                 char *newrDN, Slapi_DN *newsuperior, 
                 int logChanges)
{
    Slapi_PBlock *search_result_pb = NULL;
    Slapi_PBlock *mod_pb = slapi_pblock_new();
    Slapi_Entry  **search_entries = NULL;
    Slapi_DN *sdn = NULL;
    Slapi_Attr *attr = NULL;
    void *node = NULL;
    const char *origDN = slapi_sdn_get_dn(origSDN);
    const char *search_base = NULL;
    char *attrName = NULL;
    char *filter = NULL;
    char *attrs[2];
    int search_result;
    int nval = 0;
    int i, j;
    int rc = SLAPI_PLUGIN_SUCCESS;

    if ( argv == NULL ){
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
            "referint_postop required config file arguments missing\n" );
        rc = SLAPI_PLUGIN_FAILURE;
        goto free_and_return;
    } 
    /*
     *  For now, just putting attributes to keep integrity on in conf file,
     *  until resolve the other timing mode issue
     */
    search_result_pb = slapi_pblock_new();

    /* Search each namingContext in turn */
    for ( sdn = slapi_get_first_suffix( &node, 0 ); sdn != NULL;
          sdn = slapi_get_next_suffix( &node, 0 ))
    {
        Slapi_Backend *be = slapi_be_select(sdn);
        search_base = slapi_sdn_get_dn( sdn );

        for(i = 3; argv[i] != NULL; i++){
            filter = slapi_filter_sprintf("(%s=*%s%s)", argv[i], ESC_NEXT_VAL, origDN);
            if ( filter ) {
                /* Need only the current attribute and its subtypes */
                attrs[0] = argv[i];
                attrs[1] = NULL;

                /* Use new search API */
                slapi_pblock_init(search_result_pb);
                slapi_pblock_set(search_result_pb, SLAPI_BACKEND, be);
                slapi_search_internal_set_pb(search_result_pb, search_base, 
                    LDAP_SCOPE_SUBTREE, filter, attrs, 0 /* attrs only */,
                    NULL, NULL, referint_plugin_identity, 0);
                slapi_search_internal_pb(search_result_pb);
  
                slapi_pblock_get( search_result_pb, SLAPI_PLUGIN_INTOP_RESULT, &search_result);

                /* if search successfull then do integrity update */
                if(search_result == LDAP_SUCCESS)
                {
                    slapi_pblock_get(search_result_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                                     &search_entries);

                    for(j = 0; search_entries[j] != NULL; j++){
                        attr = NULL;
                        attrName = NULL;
                        /*
                         *  Loop over all the attributes of the entry and search
                         *  for the integrity attribute and its subtypes
                         */
                        for (slapi_entry_first_attr(search_entries[j], &attr); attr; 
                             slapi_entry_next_attr(search_entries[j], attr, &attr))
                        {
                            /*
                             *  Take into account only the subtypes of the attribute
                             *  in argv[i] having the necessary value  - origDN
                             */
                            slapi_attr_get_type(attr, &attrName);
                            if (slapi_attr_type_cmp(argv[i], attrName,
                                                    SLAPI_TYPE_CMP_SUBTYPE) == 0)
                            {
                                nval = 0;
                                slapi_attr_get_numvalues(attr, &nval);
                                /* 
                                 * We want to reduce the "modify" call as much as
                                 * possible. But if an entry contains 1000s of 
                                 * attributes which need to be updated by the 
                                 * referint plugin (e.g., a group containing 1000s 
                                 * of members), we want to avoid to allocate too 
                                 * many mods * in one "modify" call.
                                 * This is a compromise: If an attribute type has
                                 * more than 128 values, we update the attribute 
                                 * value one by one. Otherwise, update all values
                                 * in one "modify" call.
                                 */
                                if (nval > 128) {
                                    rc = _update_one_per_mod(
                                         slapi_entry_get_sdn(search_entries[j]),
                                         attr, attrName, origSDN, newrDN, 
                                         slapi_sdn_get_dn(newsuperior),
                                         mod_pb);
                                } else {
                                    rc = _update_all_per_mod(
                                         slapi_entry_get_sdn(search_entries[j]),
                                         attr, attrName, origSDN, newrDN, 
                                         slapi_sdn_get_dn(newsuperior),
                                         mod_pb);
                                }
                                /* Should we stop if one modify returns an error? */
                            }
                        }
                    }
                } else {
                    if (isFatalSearchError(search_result)){
                        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
                            "update_integrity search (base=%s filter=%s) returned "
                            "error %d\n", search_base, filter, search_result);
                        rc = SLAPI_PLUGIN_FAILURE;
                        goto free_and_return;
                    }
                }
                slapi_ch_free_string(&filter);
            }
            slapi_free_search_results_internal(search_result_pb);
        }
    }
    /* if got here, then everything good rc = 0 */
    rc = SLAPI_PLUGIN_SUCCESS;

free_and_return:
    /* free filter and search_results_pb */
    slapi_ch_free_string(&filter);

    slapi_pblock_destroy(mod_pb);
    if (search_result_pb) {
        slapi_free_search_results_internal(search_result_pb);
        slapi_pblock_destroy(search_result_pb);
    }
 
    return(rc);
}

int referint_postop_start( Slapi_PBlock *pb)
{
    char **argv;
    int argc = 0;
 
    /* get args */ 
    if ( slapi_pblock_get( pb, SLAPI_PLUGIN_ARGC, &argc ) != 0 ) { 
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
            "referint_postop failed to get argv\n" );
        return( -1 );
    } 
    if ( slapi_pblock_get( pb, SLAPI_PLUGIN_ARGV, &argv ) != 0 ) { 
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
            "referint_postop failed to get argv\n" );
        return( -1 );
    } 
    if(argv == NULL){
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
            "args were null in referint_postop_start\n" );
        return( -1  );
    }
    /*
     *  Only bother to start the thread if you are in delay mode.
     *     0  = no delay,
     *     -1 = integrity off
     */
    if (argc >= 1) {
        if(atoi(argv[0]) > 0){
            /* initialize the cv and lock */
            if (!use_txn && (NULL == referint_mutex)) {
                referint_mutex = PR_NewLock();
            }
            keeprunning_mutex = PR_NewLock();
            keeprunning_cv = PR_NewCondVar(keeprunning_mutex);
            keeprunning =1;

            referint_tid = PR_CreateThread (PR_USER_THREAD,
                               referint_thread_func,
                               (void *)argv,
                               PR_PRIORITY_NORMAL,
                               PR_GLOBAL_THREAD,
                               PR_UNJOINABLE_THREAD,
                               SLAPD_DEFAULT_THREAD_STACKSIZE);
            if ( referint_tid == NULL ) {
                slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
                    "referint_postop_start PR_CreateThread failed\n" );
                exit( 1 );
            }
        }
    } else {
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
            "referint_postop_start insufficient arguments supplied\n" );
        return( -1 );
    }

    refint_started = 1;
    return(0);
}

int referint_postop_close( Slapi_PBlock *pb)
{
    /* signal the thread to exit */
    if (NULL != keeprunning_mutex) {
        PR_Lock(keeprunning_mutex);
        keeprunning=0;
        if (NULL != keeprunning_cv) {
            PR_NotifyCondVar(keeprunning_cv);
        }
        PR_Unlock(keeprunning_mutex);
    }

    refint_started = 0;
    return(0);
}

void
referint_thread_func(void *arg)
{
    PRFileDesc *prfd = NULL;
    char **plugin_argv = (char **)arg;
    char *logfilename;
    char thisline[MAX_LINE];
    char delimiter[]="\t\n";
    char *ptoken;
    char *tmprdn;
    char *iter = NULL;
    Slapi_DN *sdn = NULL;
    Slapi_DN *tmpsuperior = NULL;
    int logChanges = 0;
    int delay;
    int no_changes;

    if(plugin_argv == NULL){
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
            "referint_thread_func not get args \n" );
        return;
    }

    delay = atoi(plugin_argv[0]);
    logfilename = plugin_argv[1]; 
    logChanges = atoi(plugin_argv[2]);
    /*
     * keep running this thread until plugin is signaled to close
     */
    while(1){ 
        no_changes=1;
        while(no_changes){
            PR_Lock(keeprunning_mutex);
            if(keeprunning == 0){
                PR_Unlock(keeprunning_mutex);
                break;
            }
            PR_Unlock(keeprunning_mutex);

            referint_lock();
            if (( prfd = PR_Open( logfilename, PR_RDONLY, REFERINT_DEFAULT_FILE_MODE )) == NULL ){
                referint_unlock();
                /* go back to sleep and wait for this file */
                PR_Lock(keeprunning_mutex);
                PR_WaitCondVar(keeprunning_cv, PR_SecondsToInterval(delay));
                PR_Unlock(keeprunning_mutex);
            } else {
                no_changes = 0;
            }
        }
        /*
         *  Check keep running here, because after break out of no
         *  changes loop on shutdown, also need to break out of this
         *  loop before trying to do the changes. The server
         *  will pick them up on next startup as file still exists
         */
        PR_Lock(keeprunning_mutex);
        if(keeprunning == 0){
            PR_Unlock(keeprunning_mutex);
            break;
        }
        PR_Unlock(keeprunning_mutex);  

        while( GetNextLine(thisline, MAX_LINE, prfd) ){
            ptoken = ldap_utf8strtok_r(thisline, delimiter, &iter);
            sdn = slapi_sdn_new_normdn_byref(ptoken);
            ptoken = ldap_utf8strtok_r (NULL, delimiter, &iter);

            if(!strcasecmp(ptoken, "NULL")) {
                tmprdn = NULL;
            } else {
                tmprdn = slapi_ch_smprintf("%s", ptoken);
            }

            ptoken = ldap_utf8strtok_r (NULL, delimiter, &iter);
            if (!strcasecmp(ptoken, "NULL")) {
                tmpsuperior = NULL;
            } else {
                tmpsuperior = slapi_sdn_new_normdn_byref(ptoken);
            }
            ptoken = ldap_utf8strtok_r (NULL, delimiter, &iter);
            if (strcasecmp(ptoken, "NULL") != 0) {
                /* Set the bind DN in the thread data */
                if(slapi_td_set_dn(slapi_ch_strdup(ptoken))){
                    slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,"Failed to set thread data\n");
                }
            }

            update_integrity(plugin_argv, sdn, tmprdn, tmpsuperior, logChanges);

            slapi_sdn_free(&sdn);
            slapi_ch_free_string(&tmprdn);
            slapi_sdn_free(&tmpsuperior);
        }

        PR_Close(prfd);

        /* remove the original file */
        if( PR_SUCCESS != PR_Delete(logfilename) ){
            slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
                "referint_postop_close could not delete \"%s\"\n", logfilename );
        }

        /* unlock and let other writers back at the file */
        referint_unlock();

        /* wait on condition here */
        PR_Lock(keeprunning_mutex);
        PR_WaitCondVar(keeprunning_cv, PR_SecondsToInterval(delay));
        PR_Unlock(keeprunning_mutex);
    }

    /* cleanup resources allocated in start  */
    if (NULL != keeprunning_mutex) {
        PR_DestroyLock(keeprunning_mutex);
    }
    if (NULL != referint_mutex) {
        PR_DestroyLock(referint_mutex);
    }
    if (NULL != keeprunning_cv) {
        PR_DestroyCondVar(keeprunning_cv);
    }
}

int my_fgetc(PRFileDesc *stream)
{
    static char buf[READ_BUFSIZE]  =  "\0";
    static int  position           =  READ_BUFSIZE;
    int         retval;
    int         err;

    /* check if we need to load the buffer */
    if( READ_BUFSIZE == position )
    {
        memset(buf, '\0', READ_BUFSIZE);
        if( ( err = PR_Read(stream, buf, READ_BUFSIZE) ) >= 0)
        {
            /* it read some data */;
            position = 0;
        }else{
            /* an error occurred */
            return err;
        }
    }

    /* try to read some data */
    if( '\0' == buf[position])
    {
        /* out of data, return eof */
        retval = MY_EOF;
        position = READ_BUFSIZE;
    }else{
        retval = buf[position];
        position++;
    }

    return retval;
}

int
GetNextLine(char *dest, int size_dest, PRFileDesc *stream) {

    char nextchar ='\0';
    int  done     = 0;
    int  i        = 0;

    while(!done){
        if( ( nextchar = my_fgetc(stream) ) != 0){
            if( i < (size_dest - 1) ){
                dest[i] = nextchar;
                i++;
                if(nextchar == '\n'){
                    /* end of line reached */
                    done = 1;
                }
            } else {
                /* no more room in buffer */
                done = 1;
            }
        } else {
            /* error or end of file */
            done = 1;
        }
    }
    dest[i] =  '\0';
    
    /* return size of string read */
    return i;
}

/*
 *  Write this record to the log file
 */
void
writeintegritylog(Slapi_PBlock *pb, char *logfilename, Slapi_DN *sdn,
                  char *newrdn, Slapi_DN *newsuperior, Slapi_DN *requestorsdn)
{
    PRFileDesc *prfd;
    char buffer[MAX_LINE];
    int len_to_write = 0;
    int rc;
    const char *requestordn = NULL;
    const char *newsuperiordn = NULL;
    size_t reqdn_len = 0;
    
    /*
     * Use this lock to protect file data when update integrity is occuring.
     * If betxn is enabled, this mutex is ignored; transaction itself takes
     * the role.
     */
    referint_lock();
    if (( prfd = PR_Open( logfilename, PR_WRONLY | PR_CREATE_FILE | PR_APPEND,
          REFERINT_DEFAULT_FILE_MODE )) == NULL ) 
    {
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
            "referint_postop could not write integrity log \"%s\" "
        SLAPI_COMPONENT_NAME_NSPR " %d (%s)\n",
            logfilename, PR_GetError(), slapd_pr_strerror(PR_GetError()) );

        PR_Unlock(referint_mutex);
        referint_unlock();
        return;
    }
    /*
     *  Make sure we have enough room in our buffer before trying to write it.
     *  add length of dn +  5(three tabs, a newline, and terminating \0)
     */
    len_to_write = slapi_sdn_get_ndn_len(sdn) + 5;

    if(newrdn == NULL){
        /* add the length of "NULL" */
        len_to_write += 4;
    } else {
        /* add the length of the newrdn */
        len_to_write += strlen(newrdn);
    }
    newsuperiordn = slapi_sdn_get_dn(newsuperior);
    if(NULL == newsuperiordn)
    {
        /* add the length of "NULL" */
        len_to_write += 4;
    } else {
        /* add the length of the newsuperior */
        len_to_write += slapi_sdn_get_ndn_len(newsuperior);
    }
    slapi_pblock_get(pb, SLAPI_REQUESTOR_DN, &requestordn);
    if (requestorsdn && (requestordn = slapi_sdn_get_udn(requestorsdn)) &&
        (reqdn_len = strlen(requestordn))) {
        len_to_write += reqdn_len;
    } else {
        len_to_write += 4; /* "NULL" */
    }

    if(len_to_write > MAX_LINE ){
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
                         "referint_postop could not write integrity log:"
                         " line length exceeded. It will not be able"
                         " to update references to this entry.\n");
    } else {
        PR_snprintf(buffer, MAX_LINE, "%s\t%s\t%s\t%s\t\n", slapi_sdn_get_dn(sdn),
                    (newrdn != NULL) ? newrdn : "NULL",
                    (newsuperiordn != NULL) ? newsuperiordn : "NULL",
                    requestordn ? requestordn : "NULL");
        if (PR_Write(prfd,buffer,strlen(buffer)) < 0){
            slapi_log_error(SLAPI_LOG_FATAL,REFERINT_PLUGIN_SUBSYSTEM,
                " writeintegritylog: PR_Write failed : The disk"
                " may be full or the file is unwritable :: NSPR error - %d\n",
            PR_GetError());
        }
    }

    /* If file descriptor is closed successfully, PR_SUCCESS */
    rc = PR_Close(prfd);
    if (rc != PR_SUCCESS){
        slapi_log_error(SLAPI_LOG_FATAL,REFERINT_PLUGIN_SUBSYSTEM,
            " writeintegritylog: failed to close the file descriptor prfd; NSPR error - %d\n",
            PR_GetError());
    }
    referint_unlock();
}

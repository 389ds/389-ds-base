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
/* ldbm_config.c - Handles configuration information that is global to all ldbm instances. */

#include "back-ldbm.h"
#include "dblayer.h"

/* Forward declarations */
static int parse_ldbm_config_entry(struct ldbminfo *li, Slapi_Entry *e, config_info *config_array);

/* Forward callback declarations */
int ldbm_config_search_entry_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
int ldbm_config_modify_entry_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);

static char *ldbm_skeleton_entries[] =
{
    "dn:cn=config, cn=%s, cn=plugins, cn=config\n"
    "objectclass:top\n"
    "objectclass:extensibleObject\n"
    "cn:config\n",

    "dn:cn=monitor, cn=%s, cn=plugins, cn=config\n"
    "objectclass:top\n"
    "objectclass:extensibleObject\n"
    "cn:monitor\n",

    "dn:cn=database, cn=monitor, cn=%s, cn=plugins, cn=config\n"
    "objectclass:top\n"
    "objectclass:extensibleObject\n"
    "cn:database\n",

    ""
};

/* Used to add an array of entries, like the one above and 
 * ldbm_instance_skeleton_entries in ldbm_instance_config.c, to the dse. 
 * Returns 0 on success.
 */
int ldbm_config_add_dse_entries(struct ldbminfo *li, char **entries, char *string1, char *string2, char *string3, int flags)
{
    int x;
    Slapi_Entry *e;
    Slapi_PBlock *util_pb = NULL;
    int rc;
    char entry_string[512];
    int dont_write_file = 0;

    if (flags & LDBM_INSTANCE_CONFIG_DONT_WRITE) {
        dont_write_file = 1;
    }

    for(x = 0; strlen(entries[x]) > 0; x++) {
        util_pb = slapi_pblock_new();
        PR_snprintf(entry_string, 512, entries[x], string1, string2, string3);
        e = slapi_str2entry(entry_string, 0);
        slapi_add_entry_internal_set_pb(util_pb, e, NULL, li->li_identity, 0);
        slapi_pblock_set(util_pb, SLAPI_DSE_DONT_WRITE_WHEN_ADDING, 
                         &dont_write_file);
        if ((rc = slapi_add_internal_pb(util_pb)) != LDAP_SUCCESS) {
            LDAPDebug(LDAP_DEBUG_ANY, "Unable to add config entries to the DSE: %d\n", rc, 0, 0);
        }
        slapi_pblock_destroy(util_pb);
    }

    return 0;
}

/* used to add a single entry, special case of above */
int ldbm_config_add_dse_entry(struct ldbminfo *li, char *entry, int flags)
{
    char *entries[] = { "%s", "" };

    return ldbm_config_add_dse_entries(li, entries, entry, NULL, NULL, flags);
}

/* Finds an entry in a config_info array with the given name.  Returns
 * the entry on success and NULL when not found.
 */
config_info *get_config_info(config_info *config_array, char *attr_name)
{
    int x;

    for(x = 0; config_array[x].config_name != NULL; x++) {
        if (!strcasecmp(config_array[x].config_name, attr_name)) {
            return &(config_array[x]);
        }
    }
    return NULL;
}

/*------------------------------------------------------------------------
 * Get and set functions for ldbm and dblayer variables
 *----------------------------------------------------------------------*/
static void *ldbm_config_lookthroughlimit_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;

    return (void *) (li->li_lookthroughlimit);
}

static int ldbm_config_lookthroughlimit_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;

    /* Do whatever we can to make sure the data is ok. */

    if (apply) {
        li->li_lookthroughlimit = val;
    }

    return retval;
}

static void *ldbm_config_mode_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;

    return (void *) (li->li_mode);
}

static int ldbm_config_mode_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;

    /* Do whatever we can to make sure the data is ok. */

    if (apply) {
        li->li_mode = val;
    }

    return retval;
}

static void *ldbm_config_allidsthreshold_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;

    return (void *) (li->li_allidsthreshold);
}

static int ldbm_config_allidsthreshold_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;

    /* Do whatever we can to make sure the data is ok. */

    /* Catch attempts to configure a stupidly low allidsthreshold */
    if (val < 100) {
        val = 100;
    }

    if (apply) {
        li->li_allidsthreshold = val;
    }

    return retval;
}

static void *ldbm_config_directory_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;

    /* Remember get functions of type string need to return
     * alloced memory. */
    return (void *) slapi_ch_strdup(li->li_new_directory);
}

static int ldbm_config_directory_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    char *val = (char *) value;
    char tmpbuf[BUFSIZ];

    errorbuf[0] = '\0';

    if (!apply) {
        /* we should really do some error checking here. */
        return retval;
    }

    if (CONFIG_PHASE_RUNNING == phase) {
        slapi_ch_free((void **) &(li->li_new_directory));
        li->li_new_directory = slapi_ch_strdup(val);
        LDAPDebug(LDAP_DEBUG_ANY, "New db directory location will not take affect until the server is restarted\n", 0, 0, 0);
    } else {
        if (!strcmp(val, "get default")) {
            /* Generate the default db directory name. The default db directory 
             * should be the instance directory with a '/db' thrown on the end. 
             * We need to read cn=config to get the instance dir. */
            /* We use this funky "get default" string for the caller to 
             * tell us that it has no idea what the db directory should
             * be.  This code figures it out be reading cn=config. */
            
            Slapi_PBlock *search_pb;
            Slapi_Entry **entries = NULL;
            Slapi_Attr *attr = NULL;
            Slapi_Value *v = NULL;
            const char *s = NULL;
            int res;

            search_pb = slapi_pblock_new();
            slapi_search_internal_set_pb(search_pb, "cn=config", LDAP_SCOPE_BASE, 
                                         "objectclass=*", NULL, 0, NULL, NULL, li->li_identity, 0);
            slapi_search_internal_pb(search_pb);
            slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &res);

            if (res != LDAP_SUCCESS) {
                LDAPDebug(LDAP_DEBUG_ANY, "ERROR: ldbm plugin unable to read cn=config\n",
                          0, 0, 0);
                goto done;
            }

            slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
            if (NULL == entries) {
                LDAPDebug(LDAP_DEBUG_ANY, "ERROR: ldbm plugin unable to read cn=config\n",
                          0, 0, 0);
                res = LDAP_OPERATIONS_ERROR;
                goto done;
            }
            
            res = slapi_entry_attr_find(entries[0], "nsslapd-instancedir", &attr);
            if (res != 0 || attr == NULL) {
                LDAPDebug(LDAP_DEBUG_ANY, 
                          "ERROR: ldbm plugin unable to read attribute nsslapd-instancedir from cn=config\n",
                          0, 0, 0);
                res = LDAP_OPERATIONS_ERROR;
                goto done;
            }

            if ( slapi_attr_first_value(attr,&v) != 0
                    || ( NULL == v )
                    || ( NULL == ( s = slapi_value_get_string( v )))) {
                LDAPDebug(LDAP_DEBUG_ANY, 
                          "ERROR: ldbm plugin unable to read attribute nsslapd-instancedir from cn=config\n",
                          0, 0, 0);
                res = LDAP_OPERATIONS_ERROR;
                goto done;
            }

done:
            slapi_pblock_destroy(search_pb);
            if (res != LDAP_SUCCESS) {
                return res;
            }
            PR_snprintf(tmpbuf, BUFSIZ, "%s/db", s );
            val = tmpbuf;
        }
        slapi_ch_free((void **) &(li->li_new_directory));
        slapi_ch_free((void **) &(li->li_directory));
        li->li_new_directory = slapi_ch_strdup(val);
        li->li_directory = slapi_ch_strdup(val);
    }

    return retval;
}

static void *ldbm_config_dbcachesize_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) (li->li_new_dbcachesize);
}

static int ldbm_config_dbcachesize_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    size_t val = (size_t) value;

    if (apply) {
        /* Stop the user configuring a stupidly small cache */
        /* min: 8KB (page size) * def thrd cnts (threadnumber==20). */
#define DBDEFMINSIZ     500000
        if (val < DBDEFMINSIZ) {
            LDAPDebug( LDAP_DEBUG_ANY,"WARNING: cache too small, increasing to %dK bytes\n", DBDEFMINSIZ/1000, 0, 0);
            val = DBDEFMINSIZ;
        } 
        
        if (CONFIG_PHASE_RUNNING == phase) {
            li->li_new_dbcachesize = val;
            LDAPDebug(LDAP_DEBUG_ANY, "New db cache size will not take affect until the server is restarted\n", 0, 0, 0);
        } else {
            li->li_new_dbcachesize = val;
            li->li_dbcachesize = val;
        }
        
    }
    
    return retval;
}

static void *ldbm_config_maxpassbeforemerge_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;

    return (void *) (li->li_maxpassbeforemerge);
}

static int ldbm_config_maxpassbeforemerge_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;
    
    if (apply) {
        if (val < 0) {
            LDAPDebug( LDAP_DEBUG_ANY,"WARNING: maxpassbeforemerge will not take negative value\n", 0, 0, 0);
            val = 100;
        } 
        
		li->li_maxpassbeforemerge = val;
    }

    return retval;
}


static void *ldbm_config_dbncache_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;

    return (void *) (li->li_new_dbncache);
}

static int ldbm_config_dbncache_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;
    
    if (apply) {
        if (val < 0) {
            LDAPDebug( LDAP_DEBUG_ANY,"WARNING: ncache will not take negative value\n", 0, 0, 0);
            val = 0;
        } 
        
        if (CONFIG_PHASE_RUNNING == phase) {
            li->li_new_dbncache = val;
            LDAPDebug(LDAP_DEBUG_ANY, "New db ncache will not take affect until the server is restarted\n", 0, 0, 0);
        } else {
            li->li_new_dbncache = val;
            li->li_dbncache = val;
        }
        
    }
    
    return retval;
}

static void *ldbm_config_db_logdirectory_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;

    /* Remember get functions of type string need to return
     * alloced memory. */
    /* if dblayer_log_directory is set to a string different from "" 
     * then it has been set, return this variable
     * otherwise it is set to default, use the instance home directory
     */
    if (strlen(li->li_dblayer_private->dblayer_log_directory) > 0)
        return (void *) slapi_ch_strdup(li->li_dblayer_private->dblayer_log_directory);
    else
        return (void *) slapi_ch_strdup(li->li_new_directory);

}

static int ldbm_config_db_logdirectory_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    char *val = (char *) value;
    
    if (apply) {
        slapi_ch_free((void **) &(li->li_dblayer_private->dblayer_log_directory));
        li->li_dblayer_private->dblayer_log_directory = slapi_ch_strdup(val);
    }
    
    return retval;
}

static void *ldbm_config_db_durable_transactions_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_durable_transactions;
}

static int ldbm_config_db_durable_transactions_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;
        
    if (apply) {
        li->li_dblayer_private->dblayer_durable_transactions = val;
    }
        
    return retval;
}

static void *ldbm_config_db_lockdown_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_lockdown;
}

static int ldbm_config_db_lockdown_set(
    void *arg, 
    void *value, 
    char *errorbuf, 
    int phase, 
    int apply
) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;

    if (apply) {
        li->li_dblayer_private->dblayer_lockdown = val;
    }

    return retval;
}

static void *ldbm_config_db_circular_logging_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_circular_logging;
}

static int ldbm_config_db_circular_logging_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;
    
    if (apply) {
        li->li_dblayer_private->dblayer_circular_logging = val;
    }
    
    return retval;
}

static void *ldbm_config_db_transaction_logging_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_enable_transactions;
}

static int ldbm_config_db_transaction_logging_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;
    
    if (apply) {
        li->li_dblayer_private->dblayer_enable_transactions = val;
    }
    
    return retval;
}

static void *ldbm_config_db_logbuf_size_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_logbuf_size;
}

static int ldbm_config_db_logbuf_size_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    size_t val = (size_t) value;
    
    if (apply) {
        li->li_dblayer_private->dblayer_logbuf_size = val;
    }
    
    return retval;
}

static void *ldbm_config_db_checkpoint_interval_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_checkpoint_interval;
}

static int ldbm_config_db_checkpoint_interval_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;
    
    if (apply) {
        li->li_dblayer_private->dblayer_checkpoint_interval = val;
    }
    
    return retval;
}

static void *ldbm_config_db_page_size_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_page_size;
}

static int ldbm_config_db_page_size_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    size_t val = (size_t) value;
    
    if (apply) {
        li->li_dblayer_private->dblayer_page_size = val;
    }
    
    return retval;
}

static void *ldbm_config_db_index_page_size_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_index_page_size;
}

static int ldbm_config_db_index_page_size_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    size_t val = (size_t) value;
    
    if (apply) {
        li->li_dblayer_private->dblayer_index_page_size = val;
    }
    
    return retval;
}

static void *ldbm_config_db_idl_divisor_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_idl_divisor;
}

static int ldbm_config_db_idl_divisor_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;
    
    if (apply) {
        li->li_dblayer_private->dblayer_idl_divisor = val;
    }
    
    return retval;
}

static void *ldbm_config_db_logfile_size_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_logfile_size;
}

static int ldbm_config_db_logfile_size_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    size_t val = (size_t) value;
    
    if (apply) {
        li->li_dblayer_private->dblayer_logfile_size = val;
    }
    
    return retval;
}

static void *ldbm_config_db_spin_count_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_spin_count;
}

static int ldbm_config_db_spin_count_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;
    
    if (apply) {
        li->li_dblayer_private->dblayer_spin_count = val;
    }
    
    return retval;
}

static void *ldbm_config_db_trickle_percentage_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_trickle_percentage;
}

static int ldbm_config_db_trickle_percentage_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;
    
    if (val < 0 || val > 100) {
        PR_snprintf(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
					"Error: Invalid value for %s (%d). Must be between 0 and 100\n", CONFIG_DB_TRICKLE_PERCENTAGE, val);
        LDAPDebug(LDAP_DEBUG_ANY, "%s", errorbuf, 0, 0);
            return LDAP_UNWILLING_TO_PERFORM;
    }
    
    if (apply) {
        li->li_dblayer_private->dblayer_trickle_percentage = val;
    }
    
    return retval;
}

static void *ldbm_config_db_verbose_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_verbose;
}

static int ldbm_config_db_verbose_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;
    
    if (apply) {
        li->li_dblayer_private->dblayer_verbose = val;
    }
    
    return retval;
}

static void *ldbm_config_db_debug_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_debug;
}

static int ldbm_config_db_debug_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;
    
    if (apply) {
        li->li_dblayer_private->dblayer_debug = val;
    }
    
    return retval;
}

static void *ldbm_config_db_named_regions_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_named_regions;
}

static int ldbm_config_db_named_regions_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;
    
    if (apply) {
        li->li_dblayer_private->dblayer_named_regions = val;
    }
    
    return retval;
}

static void *ldbm_config_db_private_mem_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_private_mem;
}

static int ldbm_config_db_private_mem_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;
    
    if (apply) {
        li->li_dblayer_private->dblayer_private_mem = val;
    }
    
    return retval;
}

static void *ldbm_config_db_private_import_mem_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_private_import_mem;
}

static int ldbm_config_db_private_import_mem_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;
    
    if (apply) {
        li->li_dblayer_private->dblayer_private_import_mem = val;
    }
    
    return retval;
}

static void *ldbm_config_db_shm_key_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;

    return (void *) li->li_dblayer_private->dblayer_shm_key;
}

static int ldbm_config_db_shm_key_set(
    void *arg, 
    void *value, 
    char *errorbuf, 
    int phase, 
    int apply
) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;

    if (apply) {
        li->li_dblayer_private->dblayer_shm_key = val;
    }

    return retval;
}

static void *ldbm_config_db_lock_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_lock_config;
}


static int ldbm_config_db_lock_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    size_t val = (size_t) value;

    if (apply) {
        if (CONFIG_PHASE_RUNNING == phase) {
            li->li_dblayer_private->dblayer_lock_config = val;
            LDAPDebug(LDAP_DEBUG_ANY, "New db cache size will not take affect until the server is restarted\n", 0, 0, 0);
        } else {
            li->li_dblayer_private->dblayer_lock_config = val;
        }
        
    }

    return retval;
}
static void *ldbm_config_db_cache_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->dblayer_cache_config;
}

static int ldbm_config_db_cache_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;
    
    if (apply) {
        li->li_dblayer_private->dblayer_cache_config = val;
    }
    
    return retval;
}

static void *ldbm_config_db_debug_checkpointing_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    return (void *) li->li_dblayer_private->db_debug_checkpointing;
}

static int ldbm_config_db_debug_checkpointing_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;
    
    if (apply) {
        li->li_dblayer_private->db_debug_checkpointing = val;
    }
    
    return retval;
}

static void *ldbm_config_db_home_directory_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    /* Remember get functions of type string need to return
     * alloced memory. */
    return (void *) slapi_ch_strdup(li->li_dblayer_private->dblayer_dbhome_directory);
}

static int ldbm_config_db_home_directory_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    char *val = (char *) value;
    
    if (apply) {
        slapi_ch_free((void **) &(li->li_dblayer_private->dblayer_dbhome_directory));
        li->li_dblayer_private->dblayer_dbhome_directory = slapi_ch_strdup(val);
    }
    
    return retval;
}

static void *ldbm_config_import_cache_autosize_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)(li->li_import_cache_autosize);
}

static int ldbm_config_import_cache_autosize_set(void *arg, void *value, char *errorbuf,
				   int phase, int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    if (apply)
	li->li_import_cache_autosize = (int)value;
    return LDAP_SUCCESS;
}

static void *ldbm_config_cache_autosize_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)(li->li_cache_autosize);
}

static int ldbm_config_cache_autosize_set(void *arg, void *value, char *errorbuf,
				   int phase, int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    if (apply)
	li->li_cache_autosize = (int)value;
    return LDAP_SUCCESS;
}

static void *ldbm_config_cache_autosize_split_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)(li->li_cache_autosize_split);
}

static int ldbm_config_cache_autosize_split_set(void *arg, void *value, char *errorbuf,
					 int phase, int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    if (apply)
	li->li_cache_autosize_split = (int)value;
    return LDAP_SUCCESS;
}

static void *ldbm_config_import_cachesize_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    return (void *)(li->li_import_cachesize);
}

static int ldbm_config_import_cachesize_set(void *arg, void *value, char *errorbuf,
                                     int phase, int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    if (apply)
        li->li_import_cachesize = (size_t)value;
    return LDAP_SUCCESS;
}

static void *ldbm_config_index_buffer_size_get(void *arg)
{
    return (void *)import_get_index_buffer_size();
}

static int ldbm_config_index_buffer_size_set(void *arg, void *value, char *errorbuf,
                                     int phase, int apply)
{
    if (apply)
        import_configure_index_buffer_size((size_t)value);
    return LDAP_SUCCESS;
}

static void *ldbm_config_idl_get_idl_new(void *arg)
{
    if (idl_get_idl_new())
        return slapi_ch_strdup("new");
    else
        return slapi_ch_strdup("old");
}

static int ldbm_config_idl_set_tune(void *arg, void *value, char *errorbuf,
                             int phase, int apply)
{
    if (!strcasecmp("new", value))
        idl_set_tune(4096);
    else
        idl_set_tune(0);
    return LDAP_SUCCESS;
}

static void *ldbm_config_serial_lock_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *) arg;

    return (void *) li->li_fat_lock;
}

static int ldbm_config_serial_lock_set(void *arg, void *value, char *errorbuf,
                             int phase, int apply)
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    if (apply) {
        li->li_fat_lock = (int) value;
    }
    
    return LDAP_SUCCESS;
}

static void *ldbm_config_legacy_errcode_get(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *) arg;

    return (void *) li->li_legacy_errcode;
}

static int ldbm_config_legacy_errcode_set(void *arg, void *value, char *errorbuf,
                             int phase, int apply)
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    
    if (apply) {
        li->li_legacy_errcode = (int) value;
    }
    
    return LDAP_SUCCESS;
}

static int
ldbm_config_set_bypass_filter_test(void *arg, void *value, char *errorbuf,
                             int phase, int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    if (apply) {
        char *myvalue = (char *)value;

        if (0 == strcasecmp(myvalue, "on")) {
            li->li_filter_bypass = 1;
            li->li_filter_bypass_check = 0;
        } else if (0 == strcasecmp(myvalue, "verify")) {
            li->li_filter_bypass = 1;
            li->li_filter_bypass_check = 1;
        } else {
            li->li_filter_bypass = 0;
            li->li_filter_bypass_check = 0;
        }
    }
    return LDAP_SUCCESS;
}

static void *ldbm_config_get_bypass_filter_test(void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    char *retstr = NULL;

    if (li->li_filter_bypass) {
        if (li->li_filter_bypass_check) {
            /* meaningful only if is bypass filter test called */
            retstr = slapi_ch_strdup("verify");
        } else {
            retstr = slapi_ch_strdup("on");
        }
    } else {
        retstr = slapi_ch_strdup("off");
    }
    return (void *)retstr;
}

static int ldbm_config_set_use_vlv_index(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int val = (int) value;
        
    if (apply) {
        if (val) {
            li->li_use_vlv = 1;
        } else {
            li->li_use_vlv = 0;
        }
    }
    return LDAP_SUCCESS;
}

static void *ldbm_config_get_use_vlv_index(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;

    return (void *) li->li_use_vlv;
}

static int
ldbm_config_exclude_from_export_set( void *arg, void *value, char *errorbuf,
		int phase, int apply)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;

    if ( apply ) {
        if ( NULL != li->li_attrs_to_exclude_from_export ) {
            charray_free( li->li_attrs_to_exclude_from_export );
            li->li_attrs_to_exclude_from_export = NULL;
        }
        
        if ( NULL != value ) {
            char *dupvalue = slapi_ch_strdup( value );
            li->li_attrs_to_exclude_from_export = str2charray( dupvalue, " " );
            slapi_ch_free((void**)&dupvalue);
        }
    }
    
    return LDAP_SUCCESS;
}

static void *
ldbm_config_exclude_from_export_get( void *arg )
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    char	*p, *retstr = NULL;
    size_t	len = 0;
    
    if ( NULL != li->li_attrs_to_exclude_from_export &&
         NULL != li->li_attrs_to_exclude_from_export[0] ) {
        int		i;
        
        for ( i = 0; li->li_attrs_to_exclude_from_export[i] != NULL; ++i ) {
            len += strlen( li->li_attrs_to_exclude_from_export[i] ) + 1;
        }
        p = retstr = slapi_ch_malloc( len );
        for ( i = 0; li->li_attrs_to_exclude_from_export[i] != NULL; ++i ) {
            if ( i > 0 ) {
                *p++ = ' ';
            }
            strcpy( p, li->li_attrs_to_exclude_from_export[i] );
            p += strlen( p );
        }
        *p = '\0';
    } else {
        retstr = slapi_ch_strdup( "" );
    }
    
    return (void *)retstr;
}

static void *ldbm_config_db_tx_max_get(void *arg) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;

    return (void *) li->li_dblayer_private->dblayer_tx_max;
}

static int ldbm_config_db_tx_max_set(
    void *arg, 
    void *value, 
    char *errorbuf, 
    int phase, 
    int apply
) 
{
    struct ldbminfo *li = (struct ldbminfo *) arg;
    int retval = LDAP_SUCCESS;
    int val = (int) value;

    if (apply) {
        li->li_dblayer_private->dblayer_tx_max = val;
    }

    return retval;
}


/*------------------------------------------------------------------------
 * Configuration array for ldbm and dblayer variables
 *----------------------------------------------------------------------*/
static config_info ldbm_config[] = {
	{CONFIG_LOOKTHROUGHLIMIT, CONFIG_TYPE_INT, "5000", &ldbm_config_lookthroughlimit_get, &ldbm_config_lookthroughlimit_set, CONFIG_FLAG_ALWAYS_SHOW|CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
	{CONFIG_MODE, CONFIG_TYPE_INT_OCTAL, "0600", &ldbm_config_mode_get, &ldbm_config_mode_set, CONFIG_FLAG_ALWAYS_SHOW|CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
	{CONFIG_IDLISTSCANLIMIT, CONFIG_TYPE_INT, "4000", &ldbm_config_allidsthreshold_get, &ldbm_config_allidsthreshold_set, CONFIG_FLAG_ALWAYS_SHOW},
	{CONFIG_DIRECTORY, CONFIG_TYPE_STRING, "", &ldbm_config_directory_get, &ldbm_config_directory_set, CONFIG_FLAG_ALWAYS_SHOW|CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
	{CONFIG_DBCACHESIZE, CONFIG_TYPE_SIZE_T, "10000000", &ldbm_config_dbcachesize_get, &ldbm_config_dbcachesize_set, CONFIG_FLAG_ALWAYS_SHOW|CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
	{CONFIG_DBNCACHE, CONFIG_TYPE_INT, "0", &ldbm_config_dbncache_get, &ldbm_config_dbncache_set, CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
	{CONFIG_MAXPASSBEFOREMERGE, CONFIG_TYPE_INT, "100", &ldbm_config_maxpassbeforemerge_get, &ldbm_config_maxpassbeforemerge_set, 0},
	
	/* dblayer config attributes */
	{CONFIG_DB_LOGDIRECTORY, CONFIG_TYPE_STRING, "", &ldbm_config_db_logdirectory_get, &ldbm_config_db_logdirectory_set, CONFIG_FLAG_ALWAYS_SHOW},
	{CONFIG_DB_DURABLE_TRANSACTIONS, CONFIG_TYPE_ONOFF, "on", &ldbm_config_db_durable_transactions_get, &ldbm_config_db_durable_transactions_set, CONFIG_FLAG_ALWAYS_SHOW},
	{CONFIG_DB_CIRCULAR_LOGGING, CONFIG_TYPE_ONOFF, "on", &ldbm_config_db_circular_logging_get, &ldbm_config_db_circular_logging_set, 0},
	{CONFIG_DB_TRANSACTION_LOGGING, CONFIG_TYPE_ONOFF, "on", &ldbm_config_db_transaction_logging_get, &ldbm_config_db_transaction_logging_set, CONFIG_FLAG_ALWAYS_SHOW},
	{CONFIG_DB_CHECKPOINT_INTERVAL, CONFIG_TYPE_INT, "60", &ldbm_config_db_checkpoint_interval_get, &ldbm_config_db_checkpoint_interval_set, CONFIG_FLAG_ALWAYS_SHOW|CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
	{CONFIG_DB_TRANSACTION_BATCH, CONFIG_TYPE_INT, "0", &dblayer_get_batch_transactions, &dblayer_set_batch_transactions, CONFIG_FLAG_ALWAYS_SHOW|CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
	{CONFIG_DB_LOGBUF_SIZE, CONFIG_TYPE_SIZE_T, "0", &ldbm_config_db_logbuf_size_get, &ldbm_config_db_logbuf_size_set, CONFIG_FLAG_ALWAYS_SHOW},
	{CONFIG_DB_PAGE_SIZE, CONFIG_TYPE_SIZE_T, "0", &ldbm_config_db_page_size_get, &ldbm_config_db_page_size_set, 0},
	{CONFIG_DB_INDEX_PAGE_SIZE, CONFIG_TYPE_SIZE_T, "0", &ldbm_config_db_index_page_size_get, &ldbm_config_db_index_page_size_set, 0},
	{CONFIG_DB_IDL_DIVISOR, CONFIG_TYPE_INT, "0", &ldbm_config_db_idl_divisor_get, &ldbm_config_db_idl_divisor_set, 0},
	{CONFIG_DB_LOGFILE_SIZE, CONFIG_TYPE_SIZE_T, "0", &ldbm_config_db_logfile_size_get, &ldbm_config_db_logfile_size_set, 0},
	{CONFIG_DB_TRICKLE_PERCENTAGE, CONFIG_TYPE_INT, "5", &ldbm_config_db_trickle_percentage_get, &ldbm_config_db_trickle_percentage_set, 0},
	{CONFIG_DB_SPIN_COUNT, CONFIG_TYPE_INT, "0", &ldbm_config_db_spin_count_get, &ldbm_config_db_spin_count_set, 0},
	{CONFIG_DB_VERBOSE, CONFIG_TYPE_ONOFF, "off", &ldbm_config_db_verbose_get, &ldbm_config_db_verbose_set, 0},
	{CONFIG_DB_DEBUG, CONFIG_TYPE_ONOFF, "on", &ldbm_config_db_debug_get, &ldbm_config_db_debug_set, 0},
	{CONFIG_DB_NAMED_REGIONS, CONFIG_TYPE_ONOFF, "off", &ldbm_config_db_named_regions_get, &ldbm_config_db_named_regions_set, 0},
	{CONFIG_DB_LOCK, CONFIG_TYPE_INT, "10000", &ldbm_config_db_lock_get, &ldbm_config_db_lock_set, 0},
	{CONFIG_DB_PRIVATE_MEM, CONFIG_TYPE_ONOFF, "off", &ldbm_config_db_private_mem_get, &ldbm_config_db_private_mem_set, 0},
	{CONFIG_DB_PRIVATE_IMPORT_MEM, CONFIG_TYPE_ONOFF, "on", &ldbm_config_db_private_import_mem_get, &ldbm_config_db_private_import_mem_set, CONFIG_FLAG_ALWAYS_SHOW|CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
	{CONFIG_DB_SHM_KEY, CONFIG_TYPE_LONG, "389389", &ldbm_config_db_shm_key_get, &ldbm_config_db_shm_key_set, 0},
	{CONFIG_DB_CACHE, CONFIG_TYPE_INT, "0", &ldbm_config_db_cache_get, &ldbm_config_db_cache_set, 0},
	{CONFIG_DB_DEBUG_CHECKPOINTING, CONFIG_TYPE_ONOFF, "off", &ldbm_config_db_debug_checkpointing_get, &ldbm_config_db_debug_checkpointing_set, 0},
	{CONFIG_DB_HOME_DIRECTORY, CONFIG_TYPE_STRING, "", &ldbm_config_db_home_directory_get, &ldbm_config_db_home_directory_set, 0},
	{CONFIG_IMPORT_CACHE_AUTOSIZE, CONFIG_TYPE_INT, "-1", &ldbm_config_import_cache_autosize_get, &ldbm_config_import_cache_autosize_set, CONFIG_FLAG_ALWAYS_SHOW|CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
	{CONFIG_CACHE_AUTOSIZE, CONFIG_TYPE_INT, "0", &ldbm_config_cache_autosize_get, &ldbm_config_cache_autosize_set, 0},
	{CONFIG_CACHE_AUTOSIZE_SPLIT, CONFIG_TYPE_INT, "50", &ldbm_config_cache_autosize_split_get, &ldbm_config_cache_autosize_split_set, 0},
	{CONFIG_IMPORT_CACHESIZE, CONFIG_TYPE_SIZE_T, "20000000", &ldbm_config_import_cachesize_get, &ldbm_config_import_cachesize_set, CONFIG_FLAG_ALWAYS_SHOW|CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
#if defined(USE_NEW_IDL)
	{CONFIG_IDL_SWITCH, CONFIG_TYPE_STRING, "new", &ldbm_config_idl_get_idl_new, &ldbm_config_idl_set_tune, CONFIG_FLAG_ALWAYS_SHOW},
#else
	{CONFIG_IDL_SWITCH, CONFIG_TYPE_STRING, "old", &ldbm_config_idl_get_idl_new, &ldbm_config_idl_set_tune, CONFIG_FLAG_ALWAYS_SHOW},
#endif
	{CONFIG_BYPASS_FILTER_TEST, CONFIG_TYPE_STRING, "on", &ldbm_config_get_bypass_filter_test, &ldbm_config_set_bypass_filter_test, CONFIG_FLAG_ALWAYS_SHOW|CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
	{CONFIG_USE_VLV_INDEX, CONFIG_TYPE_ONOFF, "on", &ldbm_config_get_use_vlv_index, &ldbm_config_set_use_vlv_index, CONFIG_FLAG_ALWAYS_SHOW|CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
	{CONFIG_DB_LOCKDOWN, CONFIG_TYPE_ONOFF, "off", &ldbm_config_db_lockdown_get, &ldbm_config_db_lockdown_set, 0},
	{CONFIG_INDEX_BUFFER_SIZE, CONFIG_TYPE_INT, "0", &ldbm_config_index_buffer_size_get, &ldbm_config_index_buffer_size_set, 0},
	{CONFIG_EXCLUDE_FROM_EXPORT, CONFIG_TYPE_STRING,
			CONFIG_EXCLUDE_FROM_EXPORT_DEFAULT_VALUE,
			&ldbm_config_exclude_from_export_get,
			&ldbm_config_exclude_from_export_set, CONFIG_FLAG_ALWAYS_SHOW},
	{CONFIG_DB_TX_MAX, CONFIG_TYPE_INT, "200", &ldbm_config_db_tx_max_get, &ldbm_config_db_tx_max_set, 0},
	{CONFIG_SERIAL_LOCK, CONFIG_TYPE_ONOFF, "on", &ldbm_config_serial_lock_get, &ldbm_config_serial_lock_set, CONFIG_FLAG_ALWAYS_SHOW|CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
	{CONFIG_USE_LEGACY_ERRORCODE, CONFIG_TYPE_ONOFF, "off", &ldbm_config_legacy_errcode_get, &ldbm_config_legacy_errcode_set, 0},
	{NULL, 0, NULL, NULL, NULL, 0}
};

void ldbm_config_setup_default(struct ldbminfo *li) 
{
    config_info *config;
    char err_buf[SLAPI_DSE_RETURNTEXT_SIZE];
    
    for (config = ldbm_config; config->config_name != NULL; config++) {
        ldbm_config_set((void *)li, config->config_name, ldbm_config, NULL /* use default */, err_buf, CONFIG_PHASE_INITIALIZATION, 1 /* apply */);
    }
}

void
ldbm_config_read_instance_entries(struct ldbminfo *li, const char *backend_type)
{
    Slapi_PBlock *tmp_pb;
    char basedn[BUFSIZ];
    Slapi_Entry **entries = NULL;

    /* Construct the base dn of the subtree that holds the instance entries. */
    PR_snprintf(basedn, BUFSIZ, "cn=%s, cn=plugins, cn=config", backend_type);

    /* Do a search of the subtree containing the instance entries */
    tmp_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(tmp_pb, basedn, LDAP_SCOPE_SUBTREE, "(objectclass=nsBackendInstance)", NULL, 0, NULL, NULL, li->li_identity, 0);
    slapi_search_internal_pb (tmp_pb);
    slapi_pblock_get(tmp_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if (entries!=NULL) {
        int i;
        for (i=0; entries[i]!=NULL; i++) {
            ldbm_instance_add_instance_entry_callback(NULL, entries[i], NULL, NULL, NULL, li);
        }
    }

    slapi_free_search_results_internal(tmp_pb);
    slapi_pblock_destroy(tmp_pb);
}

/* Reads in any config information held in the dse for the ldbm plugin.  
 * Creates dse entries used to configure the ldbm plugin and dblayer
 * if they don't already exist.  Registers dse callback functions to
 * maintain those dse entries.  Returns 0 on success.
 */
int ldbm_config_load_dse_info(struct ldbminfo *li)
{
    Slapi_PBlock *search_pb;
    Slapi_Entry **entries = NULL;
    int res;
    char dn[BUFSIZ];

    /* We try to read the entry 
     * cn=config, cn=ldbm database, cn=plugins, cn=config.  If the entry is
     * there, then we process the config information it stores.
     */
    PR_snprintf(dn, BUFSIZ, "cn=config, cn=%s, cn=plugins, cn=config", 
				li->li_plugin->plg_name);
    search_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(search_pb, dn, LDAP_SCOPE_BASE, 
        "objectclass=*", NULL, 0, NULL, NULL, li->li_identity, 0);
    slapi_search_internal_pb (search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &res);

    if (LDAP_NO_SUCH_OBJECT == res) {
        /* Add skeleten dse entries for the ldbm plugin */
        ldbm_config_add_dse_entries(li, ldbm_skeleton_entries,
                                    li->li_plugin->plg_name, NULL, NULL, 0);
    } else if (res != LDAP_SUCCESS) {
        LDAPDebug(LDAP_DEBUG_ANY, "Error accessing the ldbm config DSE\n",
                  0, 0, 0);
        return 1;
    } else {
        /* Need to parse the configuration information for the ldbm
         * plugin that is held in the DSE. */
        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                         &entries);
        if (NULL == entries || entries[0] == NULL) {
            LDAPDebug(LDAP_DEBUG_ANY, "Error accessing the ldbm config DSE\n",
                      0, 0, 0);
            return 1;
        }
        parse_ldbm_config_entry(li, entries[0], ldbm_config);	
    }

    if (search_pb) {
        slapi_free_search_results_internal(search_pb);
        slapi_pblock_destroy(search_pb);
    }
	
    /* Find all the instance entries and create a Slapi_Backend and an
     * ldbm_instance for each */
    ldbm_config_read_instance_entries(li, li->li_plugin->plg_name);

    /* setup the dse callback functions for the ldbm backend config entry */
    PR_snprintf(dn, BUFSIZ, "cn=config, cn=%s, cn=plugins, cn=config",
            li->li_plugin->plg_name);
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_BASE, "(objectclass=*)", ldbm_config_search_entry_callback,
        (void *) li);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_BASE, "(objectclass=*)", ldbm_config_modify_entry_callback,
        (void *) li);
    slapi_config_register_callback(DSE_OPERATION_WRITE, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_BASE, "(objectclass=*)", ldbm_config_search_entry_callback,
        (void *) li);

    /* setup the dse callback functions for the ldbm backend monitor entry */
    PR_snprintf(dn, BUFSIZ, "cn=monitor, cn=%s, cn=plugins, cn=config",
            li->li_plugin->plg_name);
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_BASE, "(objectclass=*)", ldbm_back_monitor_search,
        (void *)li);

    /* And the ldbm backend database monitor entry */
    PR_snprintf(dn, BUFSIZ, "cn=database, cn=monitor, cn=%s, cn=plugins, cn=config",
        li->li_plugin->plg_name);
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_BASE, "(objectclass=*)", ldbm_back_dbmonitor_search,
        (void *)li);

    /* setup the dse callback functions for the ldbm backend instance
     * entries */
    PR_snprintf(dn, BUFSIZ, "cn=%s, cn=plugins, cn=config", li->li_plugin->plg_name);
    slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_SUBTREE, "(objectclass=nsBackendInstance)",
        ldbm_instance_add_instance_entry_callback, (void *) li);
    slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_POSTOP, dn,
        LDAP_SCOPE_SUBTREE, "(objectclass=nsBackendInstance)",
        ldbm_instance_postadd_instance_entry_callback, (void *) li);
    slapi_config_register_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_SUBTREE, "(objectclass=nsBackendInstance)",
        ldbm_instance_delete_instance_entry_callback, (void *) li);
    slapi_config_register_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_POSTOP, dn,
        LDAP_SCOPE_SUBTREE, "(objectclass=nsBackendInstance)",
        ldbm_instance_post_delete_instance_entry_callback, (void *) li);

    return 0;
}


/* Utility function used in creating config entries.  Using the
 * config_info, this function gets info and formats in the correct
 * way.
 * buf is char[BUFSIZ]
 */
void ldbm_config_get(void *arg, config_info *config, char *buf)
{
    char *tmp_string;
	size_t val = 0;
    
    if (config == NULL) {
        buf[0] = '\0';
    }
    
    switch(config->config_type) {
	case CONFIG_TYPE_INT:
            sprintf(buf, "%d", (int) config->config_get_fn(arg));
            break;
    case CONFIG_TYPE_INT_OCTAL:
        sprintf(buf, "%o", (int) config->config_get_fn(arg));
        break;
    case CONFIG_TYPE_LONG:
        sprintf(buf, "%ld", (long) config->config_get_fn(arg));
        break;
    case CONFIG_TYPE_SIZE_T:
		val = (size_t) config->config_get_fn(arg);
        sprintf(buf, "%lu", val);
        break;
    case CONFIG_TYPE_STRING:
        /* Remember the get function for strings returns memory
         * that must be freed. */
        tmp_string = (char *) config->config_get_fn(arg);
        PR_snprintf(buf, BUFSIZ, "%s", (char *) tmp_string);
        slapi_ch_free((void **)&tmp_string);
        break;
    case CONFIG_TYPE_ONOFF:
        if ((int) config->config_get_fn(arg)) {
            sprintf(buf, "on");
        } else {
            sprintf(buf, "off");
        }
        break;
    }
}

/*
 * Returns:
 *   SLAPI_DSE_CALLBACK_ERROR on failure
 *   SLAPI_DSE_CALLBACK_OK on success
 */
int ldbm_config_search_entry_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg)
{
    char buf[BUFSIZ];
    struct berval *vals[2];
    struct berval val;
    struct ldbminfo *li= (struct ldbminfo *) arg;
    config_info *config;
    
    vals[0] = &val;
    vals[1] = NULL;  
    
    returntext[0] = '\0';
    
    PR_Lock(li->li_config_mutex);
    
    for(config = ldbm_config; config->config_name != NULL; config++) {
        /* Go through the ldbm_config table and fill in the entry. */

        if (!(config->config_flags & (CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_PREVIOUSLY_SET))) {
            /* This config option shouldn't be shown */
            continue;
        }
        
        ldbm_config_get((void *) li, config, buf);
        
        val.bv_val = buf;
        val.bv_len = strlen(buf);
        slapi_entry_attr_replace(e, config->config_name, vals);
    }
    
    PR_Unlock(li->li_config_mutex);

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}


int ldbm_config_ignored_attr(char *attr_name)
{
    /* These are the names of attributes that are in the
     * config entries but are not config attributes. */
    if (!strcasecmp("objectclass", attr_name) ||
        !strcasecmp("cn", attr_name) ||
        !strcasecmp("creatorsname", attr_name) ||
        !strcasecmp("modifiersname", attr_name) ||
        !strcasecmp("createtimestamp", attr_name) ||
        !strcasecmp("numsubordinates", attr_name) ||
        !strcasecmp("modifytimestamp", attr_name)) {
        return 1;
    } else {
        return 0;
    }
}

/* Returns LDAP_SUCCESS on success */
int ldbm_config_set(void *arg, char *attr_name, config_info *config_array, struct berval *bval, char *err_buf, int phase, int apply_mod)
{
    config_info *config;
    int use_default;
    int int_val;
    long long_val;
    size_t sz_val;
	PRInt64 llval;
	int maxint = (int)(((unsigned int)~0)>>1);
	int minint = ~maxint;
	PRInt64 llmaxint;
	PRInt64 llminint;
	int err = 0;
	char *str_val;
    int retval = 0;

	LL_I2L(llmaxint, maxint);
	LL_I2L(llminint, minint);

    config = get_config_info(config_array, attr_name);
    if (NULL == config) {
        LDAPDebug(LDAP_DEBUG_CONFIG, "Unknown config attribute %s\n", attr_name, 0, 0);
        PR_snprintf(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Unknown config attribute %s\n", attr_name);
        return LDAP_SUCCESS; /* Ignore unknown attributes */
    }

    /* Some config attrs can't be changed while the server is running. */
    if (phase == CONFIG_PHASE_RUNNING && 
        !(config->config_flags & CONFIG_FLAG_ALLOW_RUNNING_CHANGE)) {
        PR_snprintf(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "%s can't be modified while the server is running.\n", attr_name);
        LDAPDebug(LDAP_DEBUG_ANY, "%s", err_buf, 0, 0);
        return LDAP_UNWILLING_TO_PERFORM;
    }
    
    /* If the config phase is initialization or if bval is NULL, we will use
     * the default value for the attribute. */
    if (CONFIG_PHASE_INITIALIZATION == phase || NULL == bval) {
		use_default = 1;
    } else {
        use_default = 0;
        
        /* Since we are setting the value for the config attribute, we
         * need to turn on the CONFIG_FLAG_PREVIOUSLY_SET flag to make
         * sure this attribute is shown. */
        config->config_flags |= CONFIG_FLAG_PREVIOUSLY_SET;
    }
    
    switch(config->config_type) {
    case CONFIG_TYPE_INT:
        if (use_default) {
			str_val = config->config_default_value;
        } else {
			str_val = bval->bv_val;
        }
		/* get the value as a 64 bit value */
		llval = db_atoi(str_val, &err);
		/* check for parsing error (e.g. not a number) */
		if (err) {
			PR_snprintf(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is not a number\n",
					str_val, attr_name);
			LDAPDebug(LDAP_DEBUG_ANY, "%s", err_buf, 0, 0);
			return LDAP_UNWILLING_TO_PERFORM;
		/* check for overflow */
		} else if (LL_CMP(llval, >, llmaxint)) {
			PR_snprintf(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is greater than the maximum %d\n",
					str_val, attr_name, maxint);
			LDAPDebug(LDAP_DEBUG_ANY, "%s", err_buf, 0, 0);
			return LDAP_UNWILLING_TO_PERFORM;
		/* check for underflow */
		} else if (LL_CMP(llval, <, llminint)) {
			PR_snprintf(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is less than the minimum %d\n",
					str_val, attr_name, minint);
			LDAPDebug(LDAP_DEBUG_ANY, "%s", err_buf, 0, 0);
			return LDAP_UNWILLING_TO_PERFORM;
		}
		/* convert 64 bit value to 32 bit value */
		LL_L2I(int_val, llval);
        retval = config->config_set_fn(arg, (void *) int_val, err_buf, phase, apply_mod);
        break;
    case CONFIG_TYPE_INT_OCTAL:
        if (use_default) {
            int_val = (int) strtol(config->config_default_value, NULL, 8);
        } else {
            int_val = (int) strtol((char *)bval->bv_val, NULL, 8);
        }
		retval = config->config_set_fn(arg, (void *) int_val, err_buf, phase, apply_mod);
		break;
    case CONFIG_TYPE_LONG:
        if (use_default) {
			str_val = config->config_default_value;
        } else {
			str_val = bval->bv_val;
        }
		/* get the value as a 64 bit value */
		llval = db_atoi(str_val, &err);
		/* check for parsing error (e.g. not a number) */
		if (err) {
			PR_snprintf(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is not a number\n",
					str_val, attr_name);
			LDAPDebug(LDAP_DEBUG_ANY, "%s", err_buf, 0, 0);
			return LDAP_UNWILLING_TO_PERFORM;
		/* check for overflow */
		} else if (LL_CMP(llval, >, llmaxint)) {
			PR_snprintf(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is greater than the maximum %d\n",
					str_val, attr_name, maxint);
			LDAPDebug(LDAP_DEBUG_ANY, "%s", err_buf, 0, 0);
			return LDAP_UNWILLING_TO_PERFORM;
		/* check for underflow */
		} else if (LL_CMP(llval, <, llminint)) {
			PR_snprintf(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is less than the minimum %d\n",
					str_val, attr_name, minint);
			LDAPDebug(LDAP_DEBUG_ANY, "%s", err_buf, 0, 0);
			return LDAP_UNWILLING_TO_PERFORM;
		}
		/* convert 64 bit value to 32 bit value */
		LL_L2I(long_val, llval);
        retval = config->config_set_fn(arg, (void *) long_val, err_buf, phase, apply_mod);
        break;
    case CONFIG_TYPE_SIZE_T:
        if (use_default) {
            str_val = config->config_default_value;
        } else {
            str_val = bval->bv_val;
        }

        /* get the value as a size_t value */
        sz_val = db_strtoul(str_val, &err);

        /* check for parsing error (e.g. not a number) */
        if (err == EINVAL) {
            PR_snprintf(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is not a number\n",
                    str_val, attr_name);
            LDAPDebug(LDAP_DEBUG_ANY, "%s", err_buf, 0, 0);
            return LDAP_UNWILLING_TO_PERFORM;
            /* check for overflow */
        } else if (err == ERANGE) {
            PR_snprintf(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: value %s for attr %s is outside the range of representable values\n",
                    str_val, attr_name);
            LDAPDebug(LDAP_DEBUG_ANY, "%s", err_buf, 0, 0);
            return LDAP_UNWILLING_TO_PERFORM;
        }
        retval = config->config_set_fn(arg, (void *) sz_val, err_buf, phase, apply_mod);
        break;
    case CONFIG_TYPE_STRING:
        if (use_default) {
            retval = config->config_set_fn(arg, config->config_default_value, err_buf, phase, apply_mod);
        } else {
			retval = config->config_set_fn(arg, bval->bv_val, err_buf, phase, apply_mod);
        }
        break;
    case CONFIG_TYPE_ONOFF:
        if (use_default) {
            int_val = !strcasecmp(config->config_default_value, "on");
        } else {
            int_val = !strcasecmp((char *) bval->bv_val, "on");
        }
        retval = config->config_set_fn(arg, (void *) int_val, err_buf, phase, apply_mod);
        break;
    }
    
    return retval;
}


static int parse_ldbm_config_entry(struct ldbminfo *li, Slapi_Entry *e, config_info *config_array)
{
    Slapi_Attr *attr = NULL;
    
    for (slapi_entry_first_attr(e, &attr); attr; slapi_entry_next_attr(e, attr, &attr)) { 
        char *attr_name = NULL;
        Slapi_Value *sval = NULL;
        struct berval *bval;
        char err_buf[SLAPI_DSE_RETURNTEXT_SIZE];
        
        slapi_attr_get_type(attr, &attr_name);
        
        /* There are some attributes that we don't care about, like objectclass. */
        if (ldbm_config_ignored_attr(attr_name)) {
            continue;
        }
        slapi_attr_first_value(attr, &sval);
        bval = (struct berval *) slapi_value_get_berval(sval);
        
        if (ldbm_config_set(li, attr_name, config_array, bval, err_buf, CONFIG_PHASE_STARTUP, 1 /* apply */) != LDAP_SUCCESS) {
            LDAPDebug(LDAP_DEBUG_ANY, "Error with config attribute %s : %s\n", attr_name, err_buf, 0);
            return 1;
        }
    }
    return 0;
}

/*
 * Returns:
 *   SLAPI_DSE_CALLBACK_ERROR on failure
 *   SLAPI_DSE_CALLBACK_OK on success
 */		
int ldbm_config_modify_entry_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg) 
{ 
    int i; 
    char *attr_name; 
    LDAPMod **mods; 
    int rc = LDAP_SUCCESS; 
    int apply_mod = 0; 
    struct ldbminfo *li= (struct ldbminfo *) arg;
    
    /* This lock is probably way too conservative, but we don't expect much
     * contention for it. */
    PR_Lock(li->li_config_mutex);
        
    slapi_pblock_get( pb, SLAPI_MODIFY_MODS, &mods ); 
	
    returntext[0] = '\0'; 

    /* 
     * First pass: set apply mods to 0 so only input validation will be done; 
     * 2nd pass: set apply mods to 1 to apply changes to internal storage 
     */ 
    for ( apply_mod = 0; apply_mod <= 1 && LDAP_SUCCESS == rc; apply_mod++ ) {
        for (i = 0; mods[i] && LDAP_SUCCESS == rc; i++) {
            attr_name = mods[i]->mod_type;
            
            /* There are some attributes that we don't care about, like modifiersname. */
            if (ldbm_config_ignored_attr(attr_name)) {
                continue;
            }
            
            if ((mods[i]->mod_op & LDAP_MOD_DELETE) || 
                ((mods[i]->mod_op & ~LDAP_MOD_BVALUES) == LDAP_MOD_ADD)) { 
                rc= LDAP_UNWILLING_TO_PERFORM; 
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "%s attributes is not allowed", 
                        (mods[i]->mod_op & LDAP_MOD_DELETE) ? "Deleting" : "Adding"); 
            } else if (mods[i]->mod_op & LDAP_MOD_REPLACE) {
                /* This assumes there is only one bval for this mod. */
                rc = ldbm_config_set((void *) li, attr_name, ldbm_config,
                            ( mods[i]->mod_bvalues == NULL ) ? NULL
                            : mods[i]->mod_bvalues[0], returntext,
                            ((li->li_flags&LI_FORCE_MOD_CONFIG)?
                            CONFIG_PHASE_INTERNAL:CONFIG_PHASE_RUNNING),
                            apply_mod); 
            } 
        } 
    } 
    
    PR_Unlock(li->li_config_mutex);
    
    *returncode= rc; 
    if(LDAP_SUCCESS == rc) { 
        return SLAPI_DSE_CALLBACK_OK; 
    } 
    else { 
        return SLAPI_DSE_CALLBACK_ERROR; 
    } 
} 


/* This function is used to set config attributes. It can be used as a 
 * shortcut to doing an internal modify operation on the config DSE.
 */
void ldbm_config_internal_set(struct ldbminfo *li, char *attrname, char *value)
{
    char err_buf[SLAPI_DSE_RETURNTEXT_SIZE];
    struct berval bval;
    
    bval.bv_val = value;
    bval.bv_len = strlen(value);
    
    if (ldbm_config_set((void *) li, attrname, ldbm_config, &bval, 
                        err_buf, CONFIG_PHASE_INTERNAL, 1 /* apply */) != LDAP_SUCCESS) {
        LDAPDebug(LDAP_DEBUG_ANY, 
                  "Internal Error: Error setting instance config attr %s to %s: %s\n", 
                  attrname, value, err_buf);
        exit(1);
    }
}

/*
 * replace_ldbm_config_value:
 * - update an ldbm database config value
 */
void replace_ldbm_config_value(char *conftype, char *val, struct ldbminfo *li)
{
    Slapi_PBlock pb;
	Slapi_Mods smods;

    pblock_init(&pb);
	slapi_mods_init(&smods, 1);
	slapi_mods_add(&smods, LDAP_MOD_REPLACE, conftype, strlen(val), val);
    slapi_modify_internal_set_pb(&pb, 
        		   "cn=config,cn=ldbm database,cn=plugins,cn=config",
        		   slapi_mods_get_ldapmods_byref(&smods),
        		   NULL, NULL, li->li_identity, 0);
    slapi_modify_internal_pb(&pb);
    pblock_done(&pb);
}

/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 *
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details
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
 * Public License in all respects for all of the Program code and other code
 * used in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish
 * to provide this exception without modification, you must delete this
 * exception statement from your version and license this file solely under the
 * GPL without exception.
 *
 * END COPYRIGHT BLOCK **/
/* 
 $Id: posix-winsync-config.c 42 2011-06-10 08:39:50Z grzemba $
 $HeadURL: file:///storejet/svn/posix-winsync-plugin/trunk/posix-winsync-config.c $
 */

#ifdef WINSYNC_TEST_POSIX
#include <slapi-plugin.h>
#include "winsync-plugin.h"
#else
#include <dirsrv/slapi-plugin.h>
#include <dirsrv/winsync-plugin.h>
#endif
#include "posix-wsp-ident.h"
#include <string.h>
#include "posix-group-func.h"

#define POSIX_WINSYNC_CONFIG_FILTER "(objectclass=*)"
/*
 * static variables
 */
/* for now, there is only one configuration and it is global to the plugin  */
static POSIX_WinSync_Config theConfig;
static int inited = 0;

/* This is called when a new agreement is created or loaded
 at startup.
 */

void *
posix_winsync_agmt_init(const Slapi_DN *ds_subtree, const Slapi_DN *ad_subtree)
{
    void *cbdata = NULL;
    void *node = NULL;
    Slapi_DN *sdn = NULL;

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "--> posix_winsync_agmt_init [%s] [%s] -- begin\n",
                    slapi_sdn_get_dn(ds_subtree), slapi_sdn_get_dn(ad_subtree));

    sdn = slapi_get_first_suffix(&node, 0);
    while (sdn) {
        if (slapi_sdn_isparent(sdn, ds_subtree) == 0) {
            theConfig.rep_suffix = sdn;
            slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME, "Found suffix's '%s'\n",
                            slapi_sdn_get_dn(sdn));
            break;
        }
        sdn = slapi_get_next_suffix(&node, 0);
    }
    if (!sdn) {
        char *pardn = slapi_dn_parent(slapi_sdn_get_dn(ds_subtree));
        slapi_log_error(SLAPI_LOG_FATAL, POSIX_WINSYNC_PLUGIN_NAME, "suffix not found for '%s'\n",
                        pardn);
        slapi_ch_free_string(&pardn);
    }

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "<-- posix_winsync_agmt_init -- end\n");

    return cbdata;
}

static int
posix_winsync_apply_config(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e,
    int *returncode, char *returntext, void *arg);

POSIX_WinSync_Config *
posix_winsync_get_config()
{
    return &theConfig;
}

PRBool
posix_winsync_config_get_mapMemberUid()
{
    return theConfig.mapMemberUID;
}

PRBool
posix_winsync_config_get_lowercase()
{
    return theConfig.lowercase;
}

PRBool
posix_winsync_config_get_createMOFTask()
{
    return theConfig.createMemberOfTask;
}
void
posix_winsync_config_set_MOFTaskCreated()
{
    theConfig.MOFTaskCreated = PR_TRUE;
}
void
posix_winsync_config_reset_MOFTaskCreated()
{
    theConfig.MOFTaskCreated = PR_FALSE;
}
PRBool
posix_winsync_config_get_MOFTaskCreated()
{
    return theConfig.MOFTaskCreated;
}

PRBool
posix_winsync_config_get_msSFUSchema()
{
    return theConfig.mssfuSchema;
}

PRBool
posix_winsync_config_get_mapNestedGrouping()
{
    return theConfig.mapNestedGrouping;
}

Slapi_DN *
posix_winsync_config_get_suffix()
{
    return theConfig.rep_suffix;
}
/*
 * Read configuration and create a configuration data structure.
 * This is called after the server has configured itself so we can check
 * schema and whatnot.
 * Returns an LDAP error code (LDAP_SUCCESS if all goes well).
 */
int
posix_winsync_config(Slapi_Entry *config_e)
{
    int returncode = LDAP_SUCCESS;
    char returntext[SLAPI_DSE_RETURNTEXT_SIZE];

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME, "--> _config %s -- begin\n",
                    slapi_entry_get_dn_const(config_e));
    if (inited) {
        slapi_log_error(SLAPI_LOG_FATAL, POSIX_WINSYNC_PLUGIN_NAME,
                        "Error: POSIX WinSync plug-in already configured.  "
                            "Please remove the plugin config entry [%s]\n",
                        slapi_entry_get_dn_const(config_e));
        return (LDAP_PARAM_ERROR);
    }

    /* initialize fields */
    if ((theConfig.lock = slapi_new_mutex()) == NULL) {
        return (LDAP_LOCAL_ERROR);
    }

    /* init defaults */
    theConfig.config_e = slapi_entry_alloc();
    slapi_entry_init(theConfig.config_e, slapi_ch_strdup(""), NULL);
    theConfig.mssfuSchema = PR_FALSE;
    theConfig.mapMemberUID = PR_TRUE;
    theConfig.lowercase = PR_FALSE;
    theConfig.createMemberOfTask = PR_FALSE;
    theConfig.MOFTaskCreated = PR_FALSE;
    theConfig.mapNestedGrouping = PR_FALSE;

    posix_winsync_apply_config(NULL, NULL, config_e, &returncode, returntext, NULL);
    /* config DSE must be initialized before we get here */
    {
        int rc = 0;
        const char *config_dn = slapi_entry_get_dn_const(config_e);

        if (!memberUidLockInit()) {
            slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                            "posix_winsync_config -- init Monitor failed\n");
        }

        slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_POSTOP, config_dn,
                                       LDAP_SCOPE_BASE, POSIX_WINSYNC_CONFIG_FILTER,
                                       posix_winsync_apply_config, NULL);

        rc = slapi_task_register_handler("memberuid task", posix_group_task_add);
        if (rc) {
            slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                            "posix_winsync_config -- register memberuid task failed\n");
        }
    }

    inited = 1;

    if (returncode != LDAP_SUCCESS) {
        slapi_log_error(SLAPI_LOG_FATAL, POSIX_WINSYNC_PLUGIN_NAME, "Error %d: %s\n", returncode,
                        returntext);
    }

    return returncode;
}

static int
posix_winsync_apply_config(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e,
    int *returncode, char *returntext, void *arg)
{
    PRBool mssfuSchema = PR_FALSE;
    PRBool mapMemberUID = PR_TRUE;
    PRBool createMemberOfTask = PR_FALSE;
    PRBool lowercase = PR_FALSE;
    Slapi_Attr *testattr = NULL;
    PRBool mapNestedGrouping = PR_FALSE;

    *returncode = LDAP_UNWILLING_TO_PERFORM; /* be pessimistic */

    /* get msfuSchema value */
    if (!slapi_entry_attr_find(e, POSIX_WINSYNC_MSSFU_SCHEMA, &testattr) && (NULL != testattr)) {
        mssfuSchema = slapi_entry_attr_get_bool(e, POSIX_WINSYNC_MSSFU_SCHEMA);
        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "_apply_config: Config paramter %s: %d\n", POSIX_WINSYNC_MSSFU_SCHEMA,
                        mssfuSchema);
    }

    /* get memberUid value */
    if (!slapi_entry_attr_find(e, POSIX_WINSYNC_MAP_MEMBERUID, &testattr) && (NULL != testattr)) {
        mapMemberUID = slapi_entry_attr_get_bool(e, POSIX_WINSYNC_MAP_MEMBERUID);
        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "_apply_config: Config paramter %s: %d\n", POSIX_WINSYNC_MAP_MEMBERUID,
                        mapMemberUID);
    }
    /* get create task value */
    if (!slapi_entry_attr_find(e, POSIX_WINSYNC_CREATE_MEMBEROFTASK, &testattr) && (NULL
        != testattr)) {
        createMemberOfTask = slapi_entry_attr_get_bool(e, POSIX_WINSYNC_CREATE_MEMBEROFTASK);
        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "_apply_config: Config paramter %s: %d\n",
                        POSIX_WINSYNC_CREATE_MEMBEROFTASK, createMemberOfTask);
    }
    /* get lower case UID in memberUID */
    if (!slapi_entry_attr_find(e, POSIX_WINSYNC_LOWER_CASE, &testattr) && (NULL != testattr)) {
        lowercase = slapi_entry_attr_get_bool(e, POSIX_WINSYNC_LOWER_CASE);
        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "_apply_config: Config paramter %s: %d\n", POSIX_WINSYNC_LOWER_CASE,
                        lowercase);
    }
    /* propogate memberuids in nested grouping */
    if (!slapi_entry_attr_find(e, POSIX_WINSYNC_MAP_NESTED_GROUPING, &testattr) && (NULL != testattr)) {
        mapNestedGrouping = slapi_entry_attr_get_bool(e, POSIX_WINSYNC_MAP_NESTED_GROUPING);
        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "_apply_config: Config paramter %s: %d\n", POSIX_WINSYNC_MAP_NESTED_GROUPING,
                        mapNestedGrouping);
    }
    /* if we got here, we have valid values for everything
     set the config entry */
    slapi_lock_mutex(theConfig.lock);
    slapi_entry_free(theConfig.config_e);
    theConfig.config_e = slapi_entry_alloc();
    slapi_entry_init(theConfig.config_e, slapi_ch_strdup(""), NULL);

    /* all of the attrs and vals have been set - set the other values */
    theConfig.mssfuSchema = mssfuSchema;
    theConfig.mapMemberUID = mapMemberUID;
    theConfig.createMemberOfTask = createMemberOfTask;
    theConfig.lowercase = lowercase;
    theConfig.mapNestedGrouping = mapNestedGrouping;

    /* success */
    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "<-- _apply_config: config evaluated\n");
    *returncode = LDAP_SUCCESS;

    slapi_unlock_mutex(theConfig.lock);

    if (*returncode != LDAP_SUCCESS) {
        return SLAPI_DSE_CALLBACK_ERROR;
    } else {
        return SLAPI_DSE_CALLBACK_OK;
    }
}


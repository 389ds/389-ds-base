/******************************************************************************
Copyright (C) 2009 Hewlett-Packard Development Company, L.P.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
version 2 as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

Contributors:
Hewlett-Packard Development Company, L.P.
******************************************************************************/

/* Example enabling and config entries
dn: cn=Account Policy Plugin,cn=plugins,cn=config
objectClass: top
objectClass: nsSlapdPlugin
objectClass: extensibleObject
cn: Account Policy Plugin
nsslapd-pluginPath: /path/to/libacctpolicy-plugin.sl
nsslapd-pluginInitfunc: acct_policy_init
nsslapd-pluginType: object
nsslapd-pluginEnabled: on
nsslapd-plugin-depends-on-type: database
nsslapd-pluginId: Account Policy Plugin

dn: cn=config,cn=Account Policy Plugin,cn=plugins,cn=config
objectClass: top
objectClass: extensibleObject
cn: config
alwaysrecordlogin: yes
stateattrname: lastLoginTime
altstateattrname: createTimestamp
specattrname: acctPolicySubentry
limitattrname: accountInactivityLimit
*/

#include <stdio.h>
#include <string.h>
#include "acctpolicy.h"
#include "slapi-plugin.h"

static Slapi_PluginDesc plugin_desc = {PLUGIN_NAME, PLUGIN_VENDOR,
                                       PLUGIN_VERSION, PLUGIN_DESC};
static Slapi_PluginDesc pre_plugin_desc = {PRE_PLUGIN_NAME, PLUGIN_VENDOR,
                                           PLUGIN_VERSION, PLUGIN_DESC};
static Slapi_PluginDesc post_plugin_desc = {PRE_PLUGIN_NAME, PLUGIN_VENDOR,
                                            PLUGIN_VERSION, PLUGIN_DESC};

/* Local function prototypes */
int acct_policy_start(Slapi_PBlock *pb);
int acct_policy_close(Slapi_PBlock *pb);
int acct_policy_init(Slapi_PBlock *pb);
int acct_preop_init(Slapi_PBlock *pb);
int acct_postop_init(Slapi_PBlock *pb);
int acct_bind_preop(Slapi_PBlock *pb);
int acct_bind_postop(Slapi_PBlock *pb);

static void *_PluginID = NULL;
static Slapi_DN *_PluginDN = NULL;
static Slapi_DN *_ConfigAreaDN = NULL;
static Slapi_RWLock *config_rwlock = NULL;

void
acct_policy_set_plugin_id(void *pluginID)
{
    _PluginID = pluginID;
}

void *
acct_policy_get_plugin_id(void)
{
    return _PluginID;
}

void
acct_policy_set_plugin_sdn(Slapi_DN *pluginDN)
{
    _PluginDN = pluginDN;
}

Slapi_DN *
acct_policy_get_plugin_sdn()
{
    return _PluginDN;
}

void
acct_policy_set_config_area(Slapi_DN *sdn)
{
    _ConfigAreaDN = sdn;
}

Slapi_DN *
acct_policy_get_config_area()
{
    return _ConfigAreaDN;
}

/*
  Main init function for the account plugin
*/
int
acct_policy_init(Slapi_PBlock *pb)
{
    void *plugin_id;
    int enabled;

    slapi_pblock_get(pb, SLAPI_PLUGIN_ENABLED, &enabled);

    if (!enabled) {
        /* not enabled */
        return (CALLBACK_OK);
    }


    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *)&plugin_desc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                         (void *)&acct_policy_close) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                         (void *)acct_policy_start) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, PLUGIN_NAME,
                      "acct_policy_init - Registration failed\n");
        return (CALLBACK_ERR);
    }

    if (slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_id) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, PLUGIN_NAME,
                      "acct_policy_init - Failed to get plugin identity\n");
        return (CALLBACK_ERR);
    }

    set_identity(plugin_id);

    /* Register the pre and postop plugins */
    if (slapi_register_plugin("preoperation", 1, "acct_preop_init",
                              acct_preop_init, PRE_PLUGIN_DESC, NULL, plugin_id) != 0 ||
        slapi_register_plugin("postoperation", 1, "acct_postop_init",
                              acct_postop_init, POST_PLUGIN_DESC, NULL, plugin_id) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, PLUGIN_NAME,
                      "acct_policy_init  - Failed to register callbacks\n");
        return (CALLBACK_ERR);
    }

    return (CALLBACK_OK);
}

/*
  Plugin startup function, when this is called any other plugins should
  already be initialized, so it's safe to e.g. perform internal searches,
  which is needed to retrieve the plugin configuration
*/
int
acct_policy_start(Slapi_PBlock *pb)
{
    acctPluginCfg *cfg;
    void *plugin_id = get_identity();
    Slapi_DN *plugindn = NULL;
    char *config_area = NULL;

    if (slapi_plugin_running(pb)) {
        return 0;
    }

    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &plugindn);
    acct_policy_set_plugin_sdn(slapi_sdn_dup(plugindn));

    /* Set the alternate config area if one is defined. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_AREA, &config_area);
    if (config_area) {
        acct_policy_set_config_area(slapi_sdn_new_normdn_byval(config_area));
    }

    if (config_rwlock == NULL) {
        if ((config_rwlock = slapi_new_rwlock()) == NULL) {
            return (CALLBACK_ERR);
        }
    }

    /* Load plugin configuration */
    if (acct_policy_load_config_startup(pb, plugin_id)) {
        slapi_log_err(SLAPI_LOG_ERR, PLUGIN_NAME,
                      "acct_policy_start failed to load configuration\n");
        return (CALLBACK_ERR);
    }

    /* Show the configuration */
    cfg = get_config();
    slapi_log_err(SLAPI_LOG_PLUGIN, PLUGIN_NAME, "acct_policy_start - config: "
                                                 "stateAttrName=%s altStateAttrName=%s specAttrName=%s limitAttrName=%s "
                                                 "alwaysRecordLogin=%d\n",
                  cfg->state_attr_name, cfg->alt_state_attr_name ? cfg->alt_state_attr_name : "not configured", cfg->spec_attr_name,
                  cfg->limit_attr_name, cfg->always_record_login);

    return (CALLBACK_OK);
}

int
acct_policy_close(Slapi_PBlock *pb __attribute__((unused)))
{
    int rc = 0;

    slapi_destroy_rwlock(config_rwlock);
    config_rwlock = NULL;
    slapi_sdn_free(&_PluginDN);
    slapi_sdn_free(&_ConfigAreaDN);
    free_config();

    return rc;
}

int
acct_preop_init(Slapi_PBlock *pb)
{
    /* Which slapi plugin API we're compatible with. */
    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *)&pre_plugin_desc) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, PRE_PLUGIN_NAME,
                      "Failed to set plugin version or description\n");
        return (CALLBACK_ERR);
    }

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_BIND_FN, (void *)acct_bind_preop) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_ADD_FN, (void *)acct_add_pre_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_MODIFY_FN, (void *)acct_mod_pre_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_DELETE_FN, (void *)acct_del_pre_op) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, PRE_PLUGIN_NAME,
                      "acct_preop_init - Failed to set plugin callback function\n");
        return (CALLBACK_ERR);
    }

    return (CALLBACK_OK);
}

int
acct_postop_init(Slapi_PBlock *pb)
{
    void *plugin_id = get_identity();

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *)&post_plugin_desc) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, POST_PLUGIN_NAME,
                      "acct_postop_init - Failed to set plugin version or name\n");
        return (CALLBACK_ERR);
    }


    if (slapi_pblock_set(pb, SLAPI_PLUGIN_POST_BIND_FN, (void *)acct_bind_postop) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_ADD_FN, (void *)acct_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_MODIFY_FN, (void *)acct_post_op) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, POST_PLUGIN_NAME,
                      "acct_postop_init - Failed to set plugin callback function\n");
        return (CALLBACK_ERR);
    }

    if (slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_id) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, POST_PLUGIN_NAME,
                      "acct_postop_init - Failed to get plugin identity\n");
        return (CALLBACK_ERR);
    }

    return (CALLBACK_OK);
}

/*
 * Wrappers for config locking
 */
void
config_rd_lock()
{
    slapi_rwlock_rdlock(config_rwlock);
}

void
config_wr_lock()
{
    slapi_rwlock_wrlock(config_rwlock);
}

void
config_unlock()
{
    slapi_rwlock_unlock(config_rwlock);
}

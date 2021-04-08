/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/* cl5_init.c - implments initialization/cleanup functions for
                4.0 style changelog
 */

#include "slapi-plugin.h"
#include "cl5.h"
#include "repl5.h"

static int _cl5_upgrade_replica(Replica *replica, void *arg);
static int _cl5_upgrade_replica_config(Replica *replica, changelog5Config *config);
static int _cl5_upgrade_removedir(char *path);
static int _cl5_upgrade_removeconfig(void);

/* upgrade changelog*/
/* the changelog5 configuration maintained all changlog files in a separate directory
 * now the changlog is part of the instance database.
 * If this is the first startup with the new version we willi
 * - try to move the changelog files for each replica to the instance database.
 * - create a config entry for trimming and encryption in each backend.
 */
int
changelog5_upgrade(void)
{
    int rc = 0;
    changelog5Config config = {};

    changelog5_read_config(&config);

    if (config.dir == NULL) {
        /* we do not have a valid legacy config, nothing to upgrade */
        return rc;
    }

    replica_enumerate_replicas(_cl5_upgrade_replica, (void *)&config);

    rc = _cl5_upgrade_removedir(config.dir);

    rc = _cl5_upgrade_removeconfig();

    changelog5_config_done(&config);

    return rc;
}

/* initializes changelog*/
int
changelog5_init(void)
{
    int rc;

    rc = cl5Init();
    if (rc != CL5_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "changelog5_init: failed to initialize changelog\n");
        return 1;
    }

    /* setup callbacks for operations on changelog */
    changelog5_config_init();

    /* start changelog */
    rc = cl5Open();
    if (rc != CL5_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                      "changelog5_init: failed to start changelog\n");
        rc = 1;
        goto done;
    }


    rc = 0;

done:
    return rc;
}

/* cleanups changelog data */
void
changelog5_cleanup()
{
    /* close changelog */
    cl5Close();

    /* cleanup config */
    changelog5_config_cleanup();
}

static int
_cl5_upgrade_replica_config(Replica *replica, changelog5Config *config)
{
    int rc = 0;
    Slapi_Backend *be = slapi_be_select(replica_get_root(replica));

    Slapi_Entry *config_entry = slapi_entry_alloc();
    slapi_entry_init(config_entry, slapi_ch_strdup("cn=changelog"), NULL);
    slapi_entry_add_string(config_entry, "objectclass", "top");
    slapi_entry_add_string(config_entry, "objectclass", "extensibleObject");

    /* keep the changelog trimming config */
    if (config->maxEntries) {
        char *maxEnt = slapi_ch_smprintf("%d", config->maxEntries);
        slapi_entry_add_string(config_entry, CONFIG_CHANGELOG_MAXENTRIES_ATTRIBUTE, maxEnt);
    }
    if (strcmp(config->maxAge, CL5_STR_IGNORE)) {
        slapi_entry_add_string(config_entry, CONFIG_CHANGELOG_MAXAGE_ATTRIBUTE, config->maxAge);
    }
    if (config->trimInterval != CHANGELOGDB_TRIM_INTERVAL) {
        slapi_entry_add_string(config_entry, CONFIG_CHANGELOG_TRIM_ATTRIBUTE, gen_duration(config->trimInterval));
    }

    /* if changelog encryption is enabled then in the upgrade mode all backends will have 
     * an encrypted changelog, store the encryption attrs */
    if (config->encryptionAlgorithm) {
        slapi_entry_add_string(config_entry, CONFIG_CHANGELOG_ENCRYPTION_ALGORITHM, config->encryptionAlgorithm);
        slapi_entry_add_string(config_entry, CONFIG_CHANGELOG_SYMMETRIC_KEY, config->symmetricKey);
    }
    rc = slapi_back_ctrl_info(be, BACK_INFO_CLDB_SET_CONFIG, (void *)config_entry);

    return rc;
}

static int
_cl5_upgrade_replica(Replica *replica, void *arg)
{
    changelog5Config *config = (changelog5Config *)arg;
    const char *replName = replica_get_name(replica);
    char *replGen = replica_get_generation(replica);
    char *oldFile = slapi_ch_smprintf("%s/%s_%s.db", config->dir, replName, replGen);
    char *newFile = NULL;
    char *instancedir = NULL;
    int rc = 0;

    if (PR_Access(oldFile, PR_ACCESS_EXISTS) == PR_SUCCESS) {
        Slapi_Backend *be = slapi_be_select(replica_get_root(replica));
        char *cl_filename;

        slapi_back_get_info(be, BACK_INFO_INSTANCE_DIR, (void **)&instancedir);
        slapi_back_get_info(be, BACK_INFO_CLDB_FILENAME, (void **)&cl_filename);
        newFile = slapi_ch_smprintf("%s/%s", instancedir, cl_filename);
        rc = slapi_back_ctrl_info(be, BACK_INFO_DBENV_CLDB_UPGRADE, oldFile);
        slapi_log_err(SLAPI_LOG_INFO, repl_plugin_name_cl,
                      "_cl5_upgrade_replica: moving changelog file (%s) to (%s) %s\n",
                      oldFile, newFile, rc?"failed":"succeeded");
    }

    /* Move changelog config to backend config */
    rc = _cl5_upgrade_replica_config(replica, config);

    /* Cleanup */
    slapi_ch_free_string(&instancedir);
    slapi_ch_free_string(&oldFile);
    slapi_ch_free_string(&newFile);
    slapi_ch_free_string(&replGen);

    return rc;
}

static int
_cl5_upgrade_removedir(char *path)
{
    /* this is duplicated from ldbm_delete_dirs, we unfortunately
     * cannot access the backend functions
     */
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
            PR_Delete(fullpath);
        }
    }
    PR_CloseDir(dirhandle);
    /* remove the directory itself too */
    rval += PR_RmDir(path);
    return rval;
}

static int
_cl5_upgrade_removeconfig(void)
{
    int rc = LDAP_SUCCESS;

    Slapi_PBlock *pb = slapi_pblock_new();
    slapi_delete_internal_set_pb(pb, "cn=changelog5,cn=config", NULL, NULL,
                                 repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION), 0);
    slapi_delete_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    slapi_pblock_destroy(pb);
    return rc;
}

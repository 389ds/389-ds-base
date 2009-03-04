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

/* cl5_config.c - functions to process changelog configuration
 */	

#include <string.h>
#include <prio.h>
#include "repl5.h"
#include "cl5.h"
#include "cl5_clcache.h" /* To configure the Changelog Cache */
#include "intrinsics.h" /* JCMREPL - Is this bad? */
#ifdef TEST_CL5
#include "cl5_test.h"    
#endif
#include "nspr.h"
#include "plstr.h"

#define CONFIG_BASE		"cn=changelog5,cn=config" /*"cn=changelog,cn=supplier,cn=replication5.0,cn=replication,cn=config"*/
#define CONFIG_FILTER	"(objectclass=*)"

static PRRWLock *s_configLock; /* guarantees that only on thread at a time
								modifies changelog configuration */

/* Forward Declartions */
static int changelog5_config_add (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
static int changelog5_config_modify (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
static int changelog5_config_delete (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
static int dont_allow_that(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg);

static void changelog5_extract_config(Slapi_Entry* entry, changelog5Config *config);
static changelog5Config * changelog5_dup_config(changelog5Config *config);
static void replace_bslash (char *dir);
static int notify_replica (Replica *r, void *arg);
static int _is_absolutepath (char *dir);

int changelog5_config_init()
{
    /* The FE DSE *must* be initialised before we get here */

	/* create the configuration lock */
	s_configLock = PR_NewRWLock(PR_RWLOCK_RANK_NONE, "config_lock");
	if (s_configLock == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"changelog5_config_init: failed to create configurationlock; "
                        "NSPR error - %d\n",PR_GetError ());
		return 1;
	}
    
	slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_BASE, 
								   CONFIG_FILTER, changelog5_config_add, NULL); 
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_BASE, 
								   CONFIG_FILTER, changelog5_config_modify, NULL);
    slapi_config_register_callback(SLAPI_OPERATION_MODRDN, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_BASE,
								   CONFIG_FILTER, dont_allow_that, NULL);
    slapi_config_register_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_BASE, 
								   CONFIG_FILTER, changelog5_config_delete, NULL); 

    return 0;
}

void changelog5_config_cleanup()
{
	slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_BASE, 
								   CONFIG_FILTER, changelog5_config_add); 
    slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_BASE, 
								   CONFIG_FILTER, changelog5_config_modify);
    slapi_config_remove_callback(SLAPI_OPERATION_MODRDN, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_BASE,
								   CONFIG_FILTER, dont_allow_that);
    slapi_config_remove_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_BASE, 
								   CONFIG_FILTER, changelog5_config_delete);

	if (s_configLock)
	{
		PR_DestroyRWLock (s_configLock);
		s_configLock = NULL;
	}	
}

int changelog5_read_config (changelog5Config *config)
{
	int rc = LDAP_SUCCESS;
	Slapi_PBlock *pb;

    pb = slapi_pblock_new ();
	slapi_search_internal_set_pb (pb, CONFIG_BASE, LDAP_SCOPE_BASE, CONFIG_FILTER, NULL, 0, NULL,
								  NULL, repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
	slapi_search_internal_pb (pb);
    slapi_pblock_get( pb, SLAPI_PLUGIN_INTOP_RESULT, &rc );
    if ( LDAP_SUCCESS == rc )
	{
        Slapi_Entry **entries = NULL;
        slapi_pblock_get( pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries );
        if ( NULL != entries && NULL != entries[0])
		{
        	/* Extract the config info from the changelog entry */
			changelog5_extract_config(entries[0], config);
        }
    }
	else
	{
		memset (config, 0, sizeof (*config));
        rc = LDAP_SUCCESS;
	}

    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);

	return rc;
}

void changelog5_config_done (changelog5Config *config)
{
  if (config) {
	/* slapi_ch_free_string accepts NULL pointer */
	slapi_ch_free_string (&config->maxAge);
	slapi_ch_free_string (&config->dir);
  }
}

void changelog5_config_free (changelog5Config **config)
{
	changelog5_config_done(*config);
	slapi_ch_free((void **)config);
}

static int 
changelog5_config_add (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, 
					   int *returncode, char *returntext, void *arg)
{
    int rc;	
	changelog5Config config;

	*returncode = LDAP_SUCCESS;

	PR_RWLock_Wlock (s_configLock);

	/* we already have a configured changelog - don't need to do anything
	   since add operation will fail */
	if (cl5GetState () == CL5_STATE_OPEN)
	{	
		*returncode = 1;
		if (returntext)
		{
			strcpy (returntext, "attempt to add changelog when it already exists");
		}

		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"changelog5_config_add: changelog already exist; "
						"request ignored\n");
		goto done;
	}

	changelog5_extract_config(e, &config);
	if (config.dir == NULL)
	{
		*returncode = 1;
		if (returntext)
		{
			PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE, "NULL changelog directory");
		}

		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
				"changelog5_config_add: NULL changelog directory\n");
		goto done;	
	}	

	if (!cl5DbDirIsEmpty(config.dir))
	{
		*returncode = 1;
		if (returntext)
		{
			PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
						"The changelog directory [%s] already exists and is not empty.  "
						"Please choose a directory that does not exist or is empty.\n",
						config.dir);
		}
		
		goto done;
	}

	/* start the changelog */
	rc = cl5Open (config.dir, &config.dbconfig);
	if (rc != CL5_SUCCESS)
	{
		*returncode = 1;
		if (returntext)
		{
			PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE, "failed to start changelog; error - %d", rc);
		}

		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
				"changelog5_config_add: failed to start changelog\n");
		goto done;
	}

	/* set trimming parameters */
	rc = cl5ConfigTrimming (config.maxEntries, config.maxAge);
	if (rc != CL5_SUCCESS)
	{
		*returncode = 1;
		if (returntext)
		{
			PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE, "failed to configure changelog trimming; error - %d", rc);
		}
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"changelog5_config_add: failed to configure changelog trimming\n");
		goto done;
	}

    /* notify all the replicas that the changelog is configured 
       so that the can log dummy changes if necessary. */
    replica_enumerate_replicas (notify_replica, NULL);

#ifdef TEST_CL5
    testChangelog (TEST_ITERATION);
#endif

done:;
	PR_RWLock_Unlock (s_configLock);
    changelog5_config_done (&config);
	if (*returncode == LDAP_SUCCESS)
	{
		if (returntext)
		{
			returntext[0] = '\0';
		}

		return SLAPI_DSE_CALLBACK_OK;
	}
	
	return SLAPI_DSE_CALLBACK_ERROR;
}

static int 
changelog5_config_modify (Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, 
						  int *returncode, char *returntext, void *arg)
{
    int rc= 0;
   	LDAPMod **mods;
	int i;
	changelog5Config config;
	changelog5Config * originalConfig = NULL;
	char *currentDir = NULL;

	*returncode = LDAP_SUCCESS;

	/* changelog must be open before its parameters can be modified */
	if (cl5GetState() != CL5_STATE_OPEN)
	{
		if (returntext)
		{
			strcpy (returntext, "changelog is not configured");
		}

		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
					"changelog5_config_modify: changelog is not configured\n");
		return SLAPI_DSE_CALLBACK_ERROR;
	}

	PR_RWLock_Wlock (s_configLock);

	/* changelog must be open before its parameters can be modified */
	if (cl5GetState() != CL5_STATE_OPEN)
	{
		*returncode = 1;
		if (returntext)
		{
			strcpy (returntext, "changelog is not configured");
		}

		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
					"changelog5_config_modify: changelog is not configured\n");
		goto done;
	}

	/* 
	 * Extract all the original configuration: This is needed to ensure that the configuration
	 * is trully reloaded. This was not needed before 091401 because the changelog configuration
	 * was always hardcoded (NULL was being passed to cl5Open). Now we need to ensure we pass to
	 * cl5Open the proper configuration... 
	 */
	changelog5_extract_config(e, &config);
	originalConfig = changelog5_dup_config(&config);

	/* Reset all the attributes that have been potentially modified by the current MODIFY operation */
	slapi_ch_free_string(&config.dir);
	config.dir = NULL;
	config.maxEntries = CL5_NUM_IGNORE;
	slapi_ch_free_string(&config.maxAge);
	config.maxAge = slapi_ch_strdup(CL5_STR_IGNORE);
	config.dbconfig.maxChCacheEntries = 0;
	config.dbconfig.maxChCacheSize = (PRUint32)CL5_NUM_IGNORE;

	slapi_pblock_get( pb, SLAPI_MODIFY_MODS, &mods );
    for (i = 0; mods[i] != NULL; i++)
	{
        if (mods[i]->mod_op & LDAP_MOD_DELETE)
		{
            /* We don't support deleting changelog attributes */
        }
		else
		{
			int j;
            for (j = 0; ((mods[i]->mod_values[j]) && (LDAP_SUCCESS == rc)); j++)
			{
                char *config_attr, *config_attr_value;
                config_attr = (char *) mods[i]->mod_type; 
                config_attr_value = (char *) mods[i]->mod_bvalues[j]->bv_val;

#define ATTR_MODIFIERSNAME	"modifiersname"
#define ATTR_MODIFYTIMESTAMP	"modifytimestamp"

				if ( strcasecmp ( config_attr, ATTR_MODIFIERSNAME ) == 0 ) {
                    continue;
                }
				if ( strcasecmp ( config_attr, ATTR_MODIFYTIMESTAMP ) == 0 ) {
					continue;
                }
                /* replace existing value */
                if ( strcasecmp (config_attr, CONFIG_CHANGELOG_DIR_ATTRIBUTE ) == 0 )
				{
					if (config_attr_value && config_attr_value[0] != '\0')
					{
						slapi_ch_free_string(&config.dir);
						config.dir = slapi_ch_strdup(config_attr_value);
						replace_bslash (config.dir);
					}
					else
					{
						*returncode = 1;
						if (returntext)
						{
							strcpy (returntext, "null changelog directory");
						}
						goto done;
					}
                }
                else if ( strcasecmp ( config_attr, CONFIG_CHANGELOG_MAXENTRIES_ATTRIBUTE ) == 0 )
				{
					if (config_attr_value && config_attr_value[0] != '\0')
					{
						config.maxEntries = atoi (config_attr_value);
					}
					else
					{
						config.maxEntries = 0;
					}
                }
                else if ( strcasecmp ( config_attr, CONFIG_CHANGELOG_MAXAGE_ATTRIBUTE ) == 0 )
				{
					slapi_ch_free_string(&config.maxAge);
                    config.maxAge = slapi_ch_strdup(config_attr_value);
                }
                else if ( strcasecmp ( config_attr, CONFIG_CHANGELOG_CACHESIZE ) == 0 )
				{ /* The Changelog Cache Size parameters can be modified online without a need for restart */
					if (config_attr_value && config_attr_value[0] != '\0')
					{
						config.dbconfig.maxChCacheEntries = atoi (config_attr_value);
					}
					else
					{
						config.dbconfig.maxChCacheEntries = 0;
					}
                }
                else if ( strcasecmp ( config_attr, CONFIG_CHANGELOG_CACHEMEMSIZE ) == 0 )
				{ /* The Changelog Cache Size parameters can be modified online without a need for restart */
					if (config_attr_value && config_attr_value[0] != '\0')
					{
						config.dbconfig.maxChCacheSize = atoi (config_attr_value);
					}
					else
					{
						config.dbconfig.maxChCacheSize = 0;
					}
				} 
				else 
				{
					*returncode = LDAP_UNWILLING_TO_PERFORM;
					if (returntext)
					{
						PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE,
									 "Unwilling to apply %s mods while the server is running", config_attr);
					}
					goto done;
				}
			}
		}
	}
	/* Undo the reset above for all the modifiable attributes that were not modified 
	 * except config.dir */
	if (config.maxEntries == CL5_NUM_IGNORE)
		config.maxEntries = originalConfig->maxEntries;
	if (strcmp (config.maxAge, CL5_STR_IGNORE) == 0) {
		slapi_ch_free_string(&config.maxAge);
		if (originalConfig->maxAge)
			config.maxAge = slapi_ch_strdup(originalConfig->maxAge);
	}
	if (config.dbconfig.maxChCacheEntries == 0)
		config.dbconfig.maxChCacheEntries = originalConfig->dbconfig.maxChCacheEntries;
	if (config.dbconfig.maxChCacheSize == (PRUint32)CL5_NUM_IGNORE)
		config.dbconfig.maxChCacheSize = originalConfig->dbconfig.maxChCacheSize;

	
	/* attempt to change chagelog dir */
	if (config.dir)
	{
		currentDir = cl5GetDir ();
		if (currentDir == NULL)
		{
			/* something is wrong: we should never be here */
			*returncode = 1;
			if (returntext)
			{
				strcpy (returntext, "internal failure");
			}
			
			goto done;
		}

#ifdef _WIN32
		if (strcasecmp (currentDir, config.dir) != 0)
#else /* On Unix, path are case sensitive */
		if (strcmp (currentDir, config.dir) != 0)
#endif
		{
			if (!cl5DbDirIsEmpty(config.dir))
			{
				*returncode = 1;
				if (returntext)
				{
					PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
								"The changelog directory [%s] already exists and is not empty.  "
								"Please choose a directory that does not exist or is empty.\n",
								config.dir);
				}

				goto done;
			}

			if (!_is_absolutepath(config.dir) || (CL5_SUCCESS != cl5CreateDirIfNeeded(config.dir)))
			{
				*returncode = 1;
				if (returntext)
				{
					PL_strncpyz (returntext, "invalid changelog directory or insufficient access", SLAPI_DSE_RETURNTEXT_SIZE);
				}
				
				goto done;
			}
			
			/* changelog directory changed - need to remove the
			   previous changelog and create new one */
		
			slapi_log_error(SLAPI_LOG_PLUGIN, repl_plugin_name_cl, 
					"changelog5_config_modify: changelog directory changed; "
					"old dir - %s, new dir - %s; recreating changelog.\n",
					currentDir, config.dir);

			rc = cl5Close ();
			if (rc != CL5_SUCCESS)
			{
				*returncode = 1;
				if (returntext)
				{
					PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE, "failed to close changelog; error - %d", rc);
				}

				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
					"changelog5_config_modify: failed to close changelog\n");
				goto done;
			} else {
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
								"changelog5_config_modify: closed the changelog\n");
			}

			rc = cl5Delete (currentDir);
			if (rc != CL5_SUCCESS)
			{
				*returncode = 1;
				if (returntext)
				{
					PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE, "failed to remove changelog; error - %d", rc);
				}

				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
					"changelog5_config_modify: failed to remove changelog\n");
				goto done;
			} else {
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
								"changelog5_config_modify: deleted the changelog at %s\n", currentDir);
			}

			rc = cl5Open (config.dir, &config.dbconfig);
			if (rc != CL5_SUCCESS)
			{
				*returncode = 1;
				if (returntext)
				{
					PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE, "failed to restart changelog; error - %d", rc);
				}

				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
					"changelog5_config_modify: failed to restart changelog\n");
				/* before finishing, let's try to do some error recovery */
				if (CL5_SUCCESS != cl5Open(currentDir, &config.dbconfig)) {
					slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
									"changelog5_config_modify: failed to restore previous changelog\n");
				}
				goto done;
			} else {
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name_cl, 
								"changelog5_config_modify: opened the changelog at %s\n", config.dir);
			}
		}
	}

	/* one of the changelog parameters is modified */
	if (config.maxEntries != CL5_NUM_IGNORE || 
		strcmp (config.maxAge, CL5_STR_IGNORE) != 0)
	{
		rc = cl5ConfigTrimming (config.maxEntries, config.maxAge);
		if (rc != CL5_SUCCESS)
		{
			*returncode = 1;
			if (returntext)
			{
				PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE, "failed to configure changelog trimming; error - %d", rc);
			}

			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
				"changelog5_config_modify: failed to configure changelog trimming\n");
			goto done;
		}
	}

	if (config.dbconfig.maxChCacheEntries != 0 || config.dbconfig.maxChCacheSize != (PRUint32)CL5_NUM_IGNORE)
	  clcache_set_config(&config.dbconfig);

done:;						   
	PR_RWLock_Unlock (s_configLock);

    changelog5_config_done (&config);
    changelog5_config_free (&originalConfig);

	/* slapi_ch_free accepts NULL pointer */
	slapi_ch_free ((void**)&currentDir);

	if (*returncode  == LDAP_SUCCESS)
	{

		if (returntext)
		{
			returntext[0] = '\0';
		}

		return SLAPI_DSE_CALLBACK_OK;
	}
	
	return SLAPI_DSE_CALLBACK_ERROR;
}

static int 
changelog5_config_delete (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, 
						  int *returncode, char *returntext, void *arg)
{
	int rc;	
	char *currentDir = NULL;
	*returncode = LDAP_SUCCESS;

	/* changelog must be open before it can be deleted */
	if (cl5GetState () != CL5_STATE_OPEN)
	{
		*returncode = 1;
		if (returntext)
		{
			PL_strncpyz(returntext, "changelog is not configured", SLAPI_DSE_RETURNTEXT_SIZE);
		}
		
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
					"changelog5_config_delete: chagelog is not configured\n");
		return SLAPI_DSE_CALLBACK_ERROR;
	}

	PR_RWLock_Wlock (s_configLock);	

	/* changelog must be open before it can be deleted */
	if (cl5GetState () != CL5_STATE_OPEN)
	{
		*returncode = 1;
		if (returntext)
		{
			PL_strncpyz(returntext, "changelog is not configured", SLAPI_DSE_RETURNTEXT_SIZE);
		}
		
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
					"changelog5_config_delete: chagelog is not configured\n");
		goto done;
	}

	currentDir = cl5GetDir ();

	if (currentDir == NULL)
	{
		/* something is wrong: we should never be here */
		*returncode = 1;
		if (returntext)
		{
			PL_strncpyz (returntext, "internal failure", SLAPI_DSE_RETURNTEXT_SIZE);
		}
		
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
					"changelog5_config_delete: NULL directory\n");
		goto done;
	}

	/* this call will block until all threads using changelog
	   release changelog by calling cl5RemoveThread () */
	rc = cl5Close ();
	if (rc != CL5_SUCCESS)
	{
		*returncode = 1;
		if (returntext)
		{
			PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE, "failed to close changelog; error - %d", rc);
		}

		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
					"changelog5_config_delete: failed to close changelog\n");
		goto done;
	}

	rc = cl5Delete (currentDir);
	if (rc != CL5_SUCCESS)
	{
		*returncode = 1;
		if (returntext)
		{
			PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE, "failed to remove changelog; error - %d", rc);
		}

		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
					"changelog5_config_delete: failed to remove changelog\n");
		goto done;
	}

done:;
	PR_RWLock_Unlock (s_configLock);

	/* slapi_ch_free accepts NULL pointer */
	slapi_ch_free ((void**)&currentDir);

	if (*returncode == LDAP_SUCCESS)
	{
		if (returntext)
		{
			returntext[0] = '\0';
		}

		return SLAPI_DSE_CALLBACK_OK;
	}
	
	return SLAPI_DSE_CALLBACK_ERROR;
}

static int dont_allow_that(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, 
						   int *returncode, char *returntext, void *arg)
{
	*returncode = LDAP_UNWILLING_TO_PERFORM;
    return SLAPI_DSE_CALLBACK_ERROR;
}

static changelog5Config * changelog5_dup_config(changelog5Config *config)
{
	changelog5Config *dup = (changelog5Config *) slapi_ch_calloc(1, sizeof(changelog5Config));

	if (config->dir)
	  dup->dir = slapi_ch_strdup(config->dir);
	if (config->maxAge)
	  dup->maxAge = slapi_ch_strdup(config->maxAge);

	dup->maxEntries = config->maxEntries;

	/*memcpy((void *) &dup->dbconfig, (const void *) &config->dbconfig, sizeof(CL5DBConfig));*/
	dup->dbconfig.cacheSize = config->dbconfig.cacheSize;
	dup->dbconfig.durableTrans = config->dbconfig.durableTrans;
	dup->dbconfig.checkpointInterval = config->dbconfig.checkpointInterval;
	dup->dbconfig.circularLogging = config->dbconfig.circularLogging;
	dup->dbconfig.pageSize = config->dbconfig.pageSize;
	dup->dbconfig.logfileSize = config->dbconfig.logfileSize;
	dup->dbconfig.maxTxnSize = config->dbconfig.maxTxnSize;
	dup->dbconfig.fileMode = config->dbconfig.fileMode;
	dup->dbconfig.verbose = config->dbconfig.verbose;
	dup->dbconfig.debug = config->dbconfig.debug;
	dup->dbconfig.tricklePercentage = config->dbconfig.tricklePercentage;
	dup->dbconfig.spinCount = config->dbconfig.spinCount;
	dup->dbconfig.maxChCacheEntries = config->dbconfig.maxChCacheEntries;
	dup->dbconfig.maxChCacheSize = config->dbconfig.maxChCacheSize;
	dup->dbconfig.nb_lock_config = config->dbconfig.nb_lock_config;

	return dup;
}


/*
 * Given the changelog configuration entry, extract the configuration directives.
 */
static void changelog5_extract_config(Slapi_Entry* entry, changelog5Config *config)
{
	char *arg;

	memset (config, 0, sizeof (*config));
	config->dir = slapi_entry_attr_get_charptr(entry,CONFIG_CHANGELOG_DIR_ATTRIBUTE);
	replace_bslash (config->dir);
   
	arg= slapi_entry_attr_get_charptr(entry,CONFIG_CHANGELOG_MAXENTRIES_ATTRIBUTE);
	if (arg)
	{
		config->maxEntries = atoi (arg);
		slapi_ch_free_string(&arg);
	}
	
	config->maxAge = slapi_entry_attr_get_charptr(entry,CONFIG_CHANGELOG_MAXAGE_ATTRIBUTE);

	/* 
	 * Read the Changelog Internal Configuration Parameters for the Changelog DB
	 * (db cache size, db settings...)
	 */

	/* Set configuration default values first... */
	config->dbconfig.cacheSize		    = CL5_DEFAULT_CONFIG_DB_DBCACHESIZE;
	config->dbconfig.durableTrans	    = CL5_DEFAULT_CONFIG_DB_DURABLE_TRANSACTIONS;
	config->dbconfig.checkpointInterval = CL5_DEFAULT_CONFIG_DB_CHECKPOINT_INTERVAL;
	config->dbconfig.circularLogging	= CL5_DEFAULT_CONFIG_DB_CIRCULAR_LOGGING;
	config->dbconfig.pageSize		    = CL5_DEFAULT_CONFIG_DB_PAGE_SIZE;
	config->dbconfig.logfileSize		= CL5_DEFAULT_CONFIG_DB_LOGFILE_SIZE;
	config->dbconfig.maxTxnSize		    = CL5_DEFAULT_CONFIG_DB_TXN_MAX;
	config->dbconfig.verbose			= CL5_DEFAULT_CONFIG_DB_VERBOSE;
	config->dbconfig.debug			    = CL5_DEFAULT_CONFIG_DB_DEBUG;
	config->dbconfig.tricklePercentage  = CL5_DEFAULT_CONFIG_DB_TRICKLE_PERCENTAGE;
	config->dbconfig.spinCount		    = CL5_DEFAULT_CONFIG_DB_SPINCOUNT;
	config->dbconfig.nb_lock_config		= CL5_DEFAULT_CONFIG_NB_LOCK;
	
	/* Now read from the entry to override default values if needed */
	arg= slapi_entry_attr_get_charptr(entry, CONFIG_CHANGELOG_DB_DBCACHESIZE);
	if (arg)
	{
		size_t theSize = atoi (arg);
		if (theSize > CL5_MIN_DB_DBCACHESIZE)
			config->dbconfig.cacheSize = theSize;
		else {
			config->dbconfig.cacheSize = CL5_MIN_DB_DBCACHESIZE;
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
				"Warning: Changelog dbcache size too small. "
				"Increasing the Memory Size to %d bytes\n", 
				CL5_MIN_DB_DBCACHESIZE);
		}
		slapi_ch_free_string(&arg);
	}

	arg= slapi_entry_attr_get_charptr(entry, CONFIG_CHANGELOG_DB_DURABLE_TRANSACTIONS);
	if (arg)
	{
		config->dbconfig.durableTrans = atoi (arg);
		slapi_ch_free_string(&arg);
	}
	arg= slapi_entry_attr_get_charptr(entry, CONFIG_CHANGELOG_DB_CHECKPOINT_INTERVAL);
	if (arg)
	{
		config->dbconfig.checkpointInterval = atoi (arg);
		slapi_ch_free_string(&arg);
	}
	arg= slapi_entry_attr_get_charptr(entry, CONFIG_CHANGELOG_DB_CIRCULAR_LOGGING);
	if (arg)
	{
		config->dbconfig.circularLogging = atoi (arg);
		slapi_ch_free_string(&arg);
	}
	arg= slapi_entry_attr_get_charptr(entry, CONFIG_CHANGELOG_DB_PAGE_SIZE);
	if (arg)
	{
		config->dbconfig.pageSize = atoi (arg);
		slapi_ch_free_string(&arg);
	}
	arg= slapi_entry_attr_get_charptr(entry, CONFIG_CHANGELOG_DB_LOGFILE_SIZE);
	if (arg)
	{
		config->dbconfig.logfileSize = atoi (arg);
		slapi_ch_free_string(&arg);
	}
	arg= slapi_entry_attr_get_charptr(entry, CONFIG_CHANGELOG_DB_MAXTXN_SIZE);
	if (arg)
	{
		config->dbconfig.maxTxnSize = atoi (arg);
		slapi_ch_free_string(&arg);
	}
	arg= slapi_entry_attr_get_charptr(entry, CONFIG_CHANGELOG_DB_VERBOSE);
	if (arg)
	{
		config->dbconfig.verbose = atoi (arg);
		slapi_ch_free_string(&arg);
	}
	arg= slapi_entry_attr_get_charptr(entry, CONFIG_CHANGELOG_DB_DEBUG);
	if (arg)
	{
		config->dbconfig.debug = atoi (arg);
		slapi_ch_free_string(&arg);
	}
	arg= slapi_entry_attr_get_charptr(entry, CONFIG_CHANGELOG_DB_TRICKLE_PERCENTAGE);
	if (arg)
	{
		config->dbconfig.tricklePercentage = atoi (arg);
		slapi_ch_free_string(&arg);
	}
	arg= slapi_entry_attr_get_charptr(entry, CONFIG_CHANGELOG_DB_SPINCOUNT);
	if (arg)
	{
		config->dbconfig.spinCount = atoi (arg);
		slapi_ch_free_string(&arg);
	}
	arg= slapi_entry_attr_get_charptr(entry, CONFIG_CHANGELOG_MAX_CONCURRENT_WRITES);
	if (arg)
	{
		config->dbconfig.maxConcurrentWrites = atoi (arg);
		slapi_ch_free_string(&arg);
	}
	if ( config->dbconfig.maxConcurrentWrites <= 0 )
	{
		config->dbconfig.maxConcurrentWrites = CL5_DEFAULT_CONFIG_MAX_CONCURRENT_WRITES;
	}

	/* 
	 * Read the Changelog Internal Configuration Parameters for the Changelog Cache
	 */

	/* Set configuration default values first... */
	config->dbconfig.maxChCacheEntries	= CL5_DEFAULT_CONFIG_CACHESIZE;
	config->dbconfig.maxChCacheSize	= CL5_DEFAULT_CONFIG_CACHEMEMSIZE;

	/* Now read from the entry to override default values if needed */
	arg= slapi_entry_attr_get_charptr(entry, CONFIG_CHANGELOG_CACHESIZE);
	if (arg)
	{
		config->dbconfig.maxChCacheEntries = atoi (arg);
		slapi_ch_free_string(&arg);
	}
	arg= slapi_entry_attr_get_charptr(entry, CONFIG_CHANGELOG_CACHEMEMSIZE);
	if (arg)
	{
		config->dbconfig.maxChCacheSize = atoi (arg);
		slapi_ch_free_string(&arg);
	}
	arg = slapi_entry_attr_get_charptr(entry, CONFIG_CHANGELOG_NB_LOCK);
	if (arg)
	{
		size_t theSize = atoi(arg);
		if (theSize < CL5_MIN_NB_LOCK)
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
				"Warning: Changelog %s value is too low (%ld). Set to minimal value instead (%d)\n",
				CONFIG_CHANGELOG_NB_LOCK, theSize, CL5_MIN_NB_LOCK);
			config->dbconfig.nb_lock_config = CL5_MIN_NB_LOCK;
		} 
		else
		{
			config->dbconfig.nb_lock_config = theSize;
		}
		slapi_ch_free_string(&arg);
	}
	
	clcache_set_config(&config->dbconfig);
}

static void replace_bslash (char *dir)
{
	char *bslash;

	if (dir == NULL)
		return;

	bslash = strchr (dir, '\\');
	while (bslash)
	{
		*bslash = '/';
		bslash = strchr (bslash, '\\');
	}	
}

static int notify_replica (Replica *r, void *arg)
{
    return replica_log_ruv_elements (r);
}

static int _is_absolutepath (char * dir)
{
	if (dir[0] == '/')
		return 1;
#if defined(_WIN32)
	if (dir[2] == '/' && dir[1] == ':') 
		return 1;
#endif
	return 0;
}

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* cl5_test.c - changelog test cases */
#include "cl5_test.h"
#include "slapi-plugin.h"
#include "cl5.h"

#define REPLICA_ROOT        "dc=example,dc=com"   /* replica root */
#define OP_COUNT            4                  /* number of ops generated at a time */
#define MOD_COUNT           5
#define VALUE_COUNT         5
#define ENTRY_COUNT         50
#define CL_DN               "cn=changelog5,cn=config"
#define INSTANCE_ATTR       "nsslapd-instancedir"
#define REPLICA_OC          "nsds5Replica"
#define REPLICA_RDN         "cn=replica"

static void testBasic ();
static void testBackupRestore ();
static void testIteration ();
static void testTrimming ();
static void testPerformance ();
static void testPerformanceMT ();
static void testLDIF ();
static void testAll ();
static int  configureChangelog ();
static int  configureReplica ();
static int  populateChangelogOp ();
static int  populateChangelog (int entryCount, CSN ***csnList);
static int  processEntries (int entryCount, CSN **csnList);
static void clearCSNList (CSN ***csnList, int count);
static void threadMain (void *data);
static char* getBaseDir (const char *dir);
static LDAPMod **buildMods ();

void testChangelog (TestType type)
{
	switch (type)
	{
		case TEST_BASIC:			testBasic ();
									break;
		case TEST_BACKUP_RESTORE:	testBackupRestore ();
									break;
		case TEST_ITERATION:		testIteration ();
									break;
		case TEST_TRIMMING:			testTrimming ();
									break;
		case TEST_PERFORMANCE:		testPerformance ();
									break;
		case TEST_PERFORMANCE_MT:	testPerformanceMT ();
									break;
		case TEST_LDIF:				testLDIF ();
									break;
		case TEST_ALL:				testAll ();
									break;
		default:					printf ("Taste case %d is not supported\n", type);
	}
}

/* tests Open/Close, normal recovery, read/write/remove 
   of an entry */
static void testBasic ()
{
	int rc = 0;

	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Starting basic test ...\n");	

    /* ONREPL - we can't run the tests from the startup code because
       operations can't be issued until all plugins are started. So,
       instead, we do it when changelog is created 
    rc = configureChangelog (); */
    if (rc == 0)
    {
        rc = configureReplica ();
        if (rc == 0)
        {
            rc = populateChangelogOp ();
        }
    }

	if (rc == 0)
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"Basic test completed successfully\n");
	else
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"Basic test failed\n");
}

static void testBackupRestore ()
{
	char *dir;
	int rc = -1;
	char *baseDir;
	char bkDir [MAXPATHLEN];

	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Starting backup and recovery test ...\n");	
	
	dir = cl5GetDir ();

	if (dir)
	{
		baseDir = getBaseDir (dir);
		PR_snprintf (bkDir, sizeof(bkDir), "%s/clbackup", baseDir);
		slapi_ch_free ((void**)&baseDir);
		rc = cl5Backup (bkDir, NULL);

		if (rc == CL5_SUCCESS)
		{
			cl5Close ();
			rc = cl5Restore (dir, bkDir, NULL);
			if (rc == CL5_SUCCESS)
				rc = cl5Open (dir, NULL);

			/* PR_RmDir (bkDir);*/
		}
	}

	if (rc == CL5_SUCCESS)
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"Backup and Restore test completed successfully\n");
	else
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"Backup and Restore test failed\n");
}

static void testIteration ()
{	
    Object *r_obj;
    Slapi_DN *r_root;
    Replica *r;
    char *replGen;
    RUV *ruv;
	CL5ReplayIterator *it = NULL;
	slapi_operation_parameters op;
	int rc;
	int i;

	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Starting iteration test ...\n");	

	/* get replica object */
    r_root = slapi_sdn_new_dn_byval(REPLICA_ROOT);
	r_obj = replica_get_replica_from_dn (r_root);
    slapi_sdn_free (&r_root);
    if (r_obj == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "replica is not configured for (%s)\n",
                        REPLICA_ROOT);	
        return;
    }

    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Starting first iteration pass ...\n");	

    /* configure empty consumer ruv */        
	r = (Replica*)object_get_data (r_obj);
    PR_ASSERT (r);
    replGen = replica_get_generation (r);
    ruv_init_new (replGen, 0, NULL, &ruv);

    /* create replay iterator */
    rc = cl5CreateReplayIterator (r_obj, ruv, &it);
    if (it)
    {							 
		i = 0;
		while ((rc = cl5GetNextOperationToReplay (it, &op)) == CL5_SUCCESS)	
		{
			ruv_set_csns (ruv, op.csn, NULL);
			operation_parameters_done (&op);
			i ++;
		}
	}
			
	if (it)
		cl5DestroyReplayIterator (&it);

    if (rc == CL5_NOTFOUND)
    {
	    if (i == 0) /* success */
		    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "First iteration pass completed "
				            "successfully: no changes to replay\n");
	    else /* incorrect number of entries traversed */
		    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "First iteration pass failed: "
				            "traversed %d entries; expected none\n", i);
    }
	else /* general error */
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "First iteration pass failed\n");

    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Starting second iteration pass ...\n");	

    /* add some entries */
    populateChangelogOp ();
    
    /* create replay iterator */
    rc = cl5CreateReplayIterator (r_obj, ruv, &it);
    if (it)
    {							 
		i = 0;
		while ((rc = cl5GetNextOperationToReplay (it, &op)) == CL5_SUCCESS)	
		{
			ruv_set_csns (ruv, op.csn, NULL);
			operation_parameters_done (&op);
			i ++;
		}
	}
			
	if (it)
		cl5DestroyReplayIterator (&it);

    if (rc == CL5_NOTFOUND)
    {
	    if (i == OP_COUNT) /* success */
		    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Second iteration pass completed "
				            "successfully: %d entries traversed\n", i);
	    else /* incorrect number of entries traversed */
		    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Second iteration pass failed: "
				            "traversed %d entries; expected %d\n", i, OP_COUNT);
    }
	else /* general error */
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Second iteration pass failed\n");

    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Starting third iteration pass ...\n");	
    /* add more entries */
    populateChangelogOp ();
    
    /* create replay iterator */
    rc = cl5CreateReplayIterator (r_obj, ruv, &it);
    if (it)
    {							 
		i = 0;
		while ((rc = cl5GetNextOperationToReplay (it, &op)) == CL5_SUCCESS)	
		{
			ruv_set_csns (ruv, op.csn, NULL);
			operation_parameters_done (&op);
			i ++;
		}
	}
			
	if (it)
		cl5DestroyReplayIterator (&it);

    if (rc == CL5_NOTFOUND)
    {
	    if (i == OP_COUNT) /* success */
		    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Third iteration pass completed "
				            "successfully: %d entries traversed\n", i);
	    else /* incorrect number of entries traversed */
		    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Third iteration pass failed: "
				            "traversed %d entries; expected %d\n", i, OP_COUNT);
    }
	else /* general error */
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Second iteration pass failed\n");

    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Iteration test is complete\n");

    ruv_destroy (&ruv);
    object_release (r_obj);
    slapi_ch_free ((void**)&replGen);
}

static void testTrimming ()
{
	PRIntervalTime    interval;
	int count;
	int rc;

	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Starting trimming test ...\n");	

	rc = populateChangelog (200, NULL);

	if (rc == 0)
	{
		interval = PR_SecondsToInterval(2);
		DS_Sleep (interval);

		rc = populateChangelog (300, NULL);

		if (rc == 0)
			rc = cl5ConfigTrimming (300, "1d");

		interval = PR_SecondsToInterval(300); /* 5 min is default trimming interval */
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
			"Trimming test: sleeping for 5 minutes until trimming kicks in\n");
		DS_Sleep (interval);

		count = cl5GetOperationCount (NULL);
	}

	if (rc == 0 && count == 300)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
			"Trimming test completed successfully: changelog contains 300 entries\n");
	}
	else if (rc == 0)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
			"Trimming test failed: changelog contains %d entries; expected - 300\n", 
			count);
	}
	else /* general failure */
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Trimming test failed\n");
}

static void testPerformance ()
{
	PRTime starttime, endtime, totaltime;
	int entryCount = 5000;
	CSN **csnList = NULL;
	int rc;

	starttime = PR_Now();

	rc = populateChangelog (entryCount, &csnList);

	endtime = PR_Now();

	totaltime = (endtime - starttime) / 1000; /* ms */
	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Write performance:\n"
					"entry count - %d, total time - %ldms\n"
					"latency = %d msec\nthroughput = %d entry/sec\n",
					entryCount, totaltime,
					totaltime / entryCount, entryCount * 1000 / totaltime);


	starttime = endtime;

	rc = processEntries (entryCount, csnList);

	endtime = PR_Now();

	totaltime = (endtime - starttime) / 1000; /* ms */
	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Read performance:\n"
					"entry count - %d, total time - %ld\n"
					"latency = %d msec\nthroughput = %d entry/sec\n",
					entryCount, totaltime,
					totaltime / entryCount, entryCount * 1000 / totaltime);

	clearCSNList (&csnList, entryCount);
}

static int threadsLeft;
static void testPerformanceMT ()
{
	PRTime starttime, endtime, totaltime;
	int entryCount = 200;
	int threadCount = 10;
	int entryTotal;
	int i;
	PRIntervalTime interval;

	interval = PR_MillisecondsToInterval(100);
	threadsLeft = threadCount * 2;
	entryTotal = threadCount * entryCount;
	starttime = PR_Now();
	
	for (i = 0; i < threadCount; i++)
	{
		PR_CreateThread(PR_USER_THREAD, threadMain, (void*)&entryCount,
					 PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, 
					 PR_UNJOINABLE_THREAD, 0);
	}
		
	while (threadsLeft > 5)
		DS_Sleep (interval);
	
	endtime = PR_Now();

	totaltime = (endtime - starttime) / 1000; /* ms */
	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Write performance:\n"
					"entry count - %d, total time - %ld\n"
					"latency = %d msec per entry\nthroughput = %d entry/sec\n",
					entryCount, totaltime,
					totaltime / entryTotal, entryTotal * 1000 / totaltime);


	starttime = endtime;

	while (threadsLeft != 0)
		DS_Sleep (interval);	

	endtime = PR_Now();

	totaltime = (endtime - starttime) / 1000; /* ms */
	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Read performance:\n"
					"entry count - %d, total time - %ld\n"
					"latency = %d msec per entry\nthroughput = %d entry/sec\n",
					entryCount, totaltime,
					totaltime / entryTotal, entryTotal * 1000 / totaltime);
}	

static void testLDIF ()
{
	char *clDir = cl5GetDir ();
	int rc;
	char *baseDir;
	char ldifFile [MAXPATHLEN];

	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "Starting LDIF test ...\n");	

	baseDir = getBaseDir (clDir);
	PR_snprintf (ldifFile, sizeof(ldifFile), "%s/cl5.ldif", baseDir);
	slapi_ch_free ((void**)&baseDir);
	rc = populateChangelog (ENTRY_COUNT, NULL);

	if (rc == CL5_SUCCESS)
	{
		rc = cl5ExportLDIF (ldifFile, NULL);
		if (rc == CL5_SUCCESS)
		{
			cl5Close();
			rc = cl5ImportLDIF (clDir, ldifFile, NULL);
			if (rc == CL5_SUCCESS)
				cl5Open(clDir, NULL);
		}
	}

	PR_Delete (ldifFile);

	if (rc == CL5_SUCCESS)
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
		"LDIF test completed successfully\n");
	else
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "LDIF test failed\n");
}

static void testAll ()
{
	testBasic ();

	testIteration ();

	testBackupRestore ();

	testLDIF ();

	/* testTrimming ();*/

#if 0
	/* xxxPINAKI */
	/* these tests are not working correctly...the call to db->put() */
	/* just hangs forever */
	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
					"Starting single threaded performance measurement ...\n");	
	testPerformance ();	

	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
					"Starting multi threaded performance measurement ...\n");	
	testPerformanceMT ();
#endif

}			 

static int populateChangelog (int entryCount, CSN ***csnList)
{
	CSN *csn;
	int i;
	slapi_operation_parameters op;
	int rc;
	char *uniqueid;

	if (csnList)
	{
		(*csnList) = (CSN**)slapi_ch_calloc (entryCount, sizeof (CSN*));		
	}

	/* generate entries */
	for (i = 0; i < entryCount; i++)
	{		
		/* ONREPL need to get replica object
		rc = csnGetNewCSNForRepl (&csn);
		if (rc != CL5_SUCCESS)									 */
			return -1;

		if (csnList)
			(*csnList) [i] = csn_dup (csn);
		memset (&op, 0, sizeof (op));
		op.csn = csn;
		slapi_uniqueIDGenerateString(&uniqueid);			
		op.target_address.uniqueid = uniqueid;
		op.target_address.dn = slapi_ch_strdup ("cn=entry,dc=example,dc=com");
		if (i % 5 == 0)
		{
			op.operation_type = SLAPI_OPERATION_MODRDN;
			op.p.p_modrdn.modrdn_deloldrdn = 1;
			op.p.p_modrdn.modrdn_newrdn = slapi_ch_strdup("cn=entry2,dc=example,dc=com");
			op.p.p_modrdn.modrdn_newsuperior_address.dn = NULL;
			op.p.p_modrdn.modrdn_newsuperior_address.uniqueid = NULL;			
			op.p.p_modrdn.modrdn_mods = buildMods ();
		}
		else if (i % 4 == 0)
		{
			op.operation_type = SLAPI_OPERATION_DELETE;
		}
		else if (i % 3 == 0)
		{

			op.operation_type = SLAPI_OPERATION_ADD;
			op.p.p_add.target_entry = slapi_entry_alloc ();
			slapi_entry_set_dn (op.p.p_add.target_entry, slapi_ch_strdup(op.target_address.dn));
			slapi_entry_set_uniqueid (op.p.p_add.target_entry, slapi_ch_strdup(op.target_address.uniqueid));
			slapi_entry_attr_set_charptr(op.p.p_add.target_entry, "objectclass", "top");
			slapi_entry_attr_set_charptr(op.p.p_add.target_entry, "cn", "entry");			
		}
		else
		{
			op.operation_type = SLAPI_OPERATION_MODIFY;
			op.p.p_modify.modify_mods = buildMods ();
		}

		/* ONREPL rc = cl5WriteOperation (&op, 1);*/
		operation_parameters_done (&op);

		if (rc != CL5_SUCCESS)
			return -1;				
	}

	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
			"Successfully populated changelog with %d entries\n", entryCount);
	return 0;
}

static int processEntries (int entryCount, CSN **csnList)
{
	int i;
	int rc = 0;
	slapi_operation_parameters op;
	
	for (i = 0; i < entryCount; i++)
	{
		memset (&op, 0, sizeof (op));

		op.csn = csn_dup (csnList [i]);

		/* rc = cl5GetOperation (&op);*/
		if (rc != CL5_SUCCESS)
			return -1;

		operation_parameters_done (&op);
	} 		

	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
			"Successfully read %d entries from the changelog\n", entryCount);
	return 0;
}

void clearCSNList (CSN ***csnList, int count)
{
	int i;

	for (i = 0; i < count; i++)
	{
 	   csn_free (&((*csnList)[i]));
	}

	slapi_ch_free ((void**)csnList);
}

static void threadMain (void *data)
{
	int entryCount = *(int*)data;
	CSN **csnList;

	populateChangelog (entryCount, &csnList);
	PR_AtomicDecrement (&threadsLeft);

	processEntries (entryCount, csnList);
	PR_AtomicDecrement (&threadsLeft);

	clearCSNList (&csnList, entryCount);
}

static char* getBaseDir (const char *dir)
{
	char *baseDir = slapi_ch_strdup (dir);
	char *ch;

	ch = &(baseDir [strlen (dir) - 2]);

	while (ch >= baseDir && *ch != '\\' && *ch != '/')
		ch --;

	if (ch >= baseDir)
	{
		*ch = '\0';
	}	

	return baseDir;
}

static LDAPMod **buildMods ()
{
	Slapi_Mods smods;
	Slapi_Mod  smod; 
	LDAPMod **mods;
	struct berval bv;
	int j, k;

	slapi_mods_init (&smods, MOD_COUNT);

	for (j = 0; j < MOD_COUNT; j++)
	{
		slapi_mod_init (&smod, VALUE_COUNT);
		slapi_mod_set_operation (&smod, LDAP_MOD_ADD | LDAP_MOD_BVALUES);
		slapi_mod_set_type (&smod, "attr");
		
		for (k = 0; k < VALUE_COUNT; k++)
		{
			bv.bv_val = "bvalue";
			bv.bv_len = strlen (bv.bv_val) + 1;
			slapi_mod_add_value (&smod, &bv);
		}									
	
		slapi_mods_add_smod (&smods, &smod);
		/* ONREPL slapi_mod_done (&smod); */
	}

	mods = slapi_mods_get_ldapmods_passout (&smods);
	slapi_mods_done (&smods);
	return mods;
}

/* Format:
    dn: cn=changelog5,cn=config
    objectclass: top
    objectclass: extensibleObject
    cn: changelog5
    nsslapd-changelogDir: d:/netscape/server4/slapd-elf/cl5 */
static int  configureChangelog ()
{
    Slapi_PBlock *pb = slapi_pblock_new ();
    Slapi_Entry  *e = slapi_entry_alloc ();
    int rc;
    char *attrs[] = {INSTANCE_ATTR, NULL};
    Slapi_Entry **entries;
    char cl_dir [256];
	char *str = NULL;

    /* set changelog dn */
    slapi_entry_set_dn (e, slapi_ch_strdup (CL_DN));
    
    /* set object classes */
    slapi_entry_add_string(e, "objectclass", "top");
    slapi_entry_add_string(e, "objectclass", "extensibleObject");

    /* get directory instance dir */
    slapi_search_internal_set_pb (pb, "cn=config", LDAP_SCOPE_BASE, "objectclass=*", 
						          attrs, 0, NULL, NULL, 
                                  repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
    slapi_search_internal_pb (pb);
    
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != LDAP_SUCCESS)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "failed to get server instance "
                        "directory; LDAP error - %d\n", rc);	
        rc = -1;
        goto done;
    }

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
	str = slapi_entry_attr_get_charptr(entries[0], INSTANCE_ATTR);
    PR_snprintf (cl_dir, sizeof(cl_dir), "%s/%s", str, "cl5db");
	slapi_ch_free((void **)&str);
	slapi_entry_add_string (e, CONFIG_CHANGELOG_DIR_ATTRIBUTE, cl_dir);

    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy (pb);

    pb = slapi_pblock_new ();

    slapi_add_entry_internal_set_pb (pb, e, NULL, 
                                     repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
   
    slapi_add_internal_pb (pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != LDAP_SUCCESS)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "failed to add changelog "
                        "configuration entry; LDAP error - %d\n", rc);
        rc = -1;
    }
    else
        rc = 0;

done:
    slapi_pblock_destroy (pb);

    return rc;
}

#define DN_SIZE 1024

/* Format:
    dn: cn=replica,cn="o=NetscapeRoot",cn= mapping tree,cn=config
    objectclass: top
    objectclass: nsds5Replica
    objectclass: extensibleObject
    nsds5ReplicaRoot: o=NetscapeRoot
    nsds5ReplicaId: 2
    nsds5flags: 1
    cn: replica
 */
static int  configureReplica ()
{
    Slapi_PBlock *pb = slapi_pblock_new ();
    Slapi_Entry  *e = slapi_entry_alloc ();
    int rc;
    char dn [DN_SIZE];

    /* set changelog dn */
    PR_snprintf (dn, sizeof(dn), "%s,cn=\"%s\",%s", REPLICA_RDN, REPLICA_ROOT, 
             slapi_get_mapping_tree_config_root ());
    slapi_entry_set_dn (e, slapi_ch_strdup (dn));
    
    /* set object classes */
    slapi_entry_add_string(e, "objectclass", "top");
    slapi_entry_add_string(e, "objectclass", REPLICA_OC);
    slapi_entry_add_string(e, "objectclass", "extensibleObject");

    /* set other attributes */
	slapi_entry_add_string (e, attr_replicaRoot, REPLICA_ROOT);
    slapi_entry_add_string (e, attr_replicaId, "1");
    slapi_entry_add_string (e, attr_flags, "1");

    slapi_add_entry_internal_set_pb (pb, e, NULL, 
                                     repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
   
    slapi_add_internal_pb (pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != LDAP_SUCCESS)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "failed to add replica for (%s) "
                        "configuration entry; LDAP error - %d\n", REPLICA_ROOT, rc);
        rc = -1;
    }
    else
        rc = 0;

    slapi_pblock_destroy (pb);

    return rc;
}

/* generates one of each ldap operations */
static int  populateChangelogOp ()
{
    Slapi_PBlock *pb = slapi_pblock_new ();
    Slapi_Entry  *e = slapi_entry_alloc ();
    int rc;
    char dn [DN_SIZE], newrdn [64];
    LDAPMod *mods[2];
    Slapi_Mod smod;
    struct berval bv;
    time_t cur_time;

    /* add entry */
    cur_time = time(NULL);
    PR_snprintf (dn, sizeof(dn), "cn=%s,%s", ctime(&cur_time), REPLICA_ROOT);
    slapi_entry_set_dn (e, slapi_ch_strdup (dn));
    slapi_entry_add_string(e, "objectclass", "top");
    slapi_entry_add_string(e, "objectclass", "extensibleObject");
    slapi_entry_add_string (e, "mail", "jsmith@netscape.com");

    slapi_add_entry_internal_set_pb (pb, e, NULL, 
                                     repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
    slapi_add_internal_pb (pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    slapi_pblock_destroy (pb);
    if (rc != LDAP_SUCCESS)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "failed to add entry (%s); "
                        "LDAP error - %d\n", dn, rc);
        return -1;
    }

    /* modify entry */
    pb = slapi_pblock_new ();
    slapi_mod_init (&smod, 1);
    slapi_mod_set_type (&smod, "mail");
    slapi_mod_set_operation (&smod, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES);
    bv.bv_val = "jsmith@aol.com";
    bv.bv_len = strlen (bv.bv_val);
    slapi_mod_add_value(&smod, &bv);
    mods[0] = (LDAPMod*)slapi_mod_get_ldapmod_byref(&smod);
    mods[1] = NULL;
    slapi_modify_internal_set_pb (pb, dn, mods, NULL, NULL, 
								  repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
    slapi_modify_internal_pb (pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    slapi_mod_done (&smod);
    slapi_pblock_destroy (pb);
    if (rc != LDAP_SUCCESS)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "failed to modify entry (%s); "
                        "LDAP error - %d\n", dn, rc);
        return -1;
    }

    /* rename entry */
    pb = slapi_pblock_new ();
    cur_time = time (NULL);
    PR_snprintf (newrdn, sizeof(newrdn), "cn=renamed%s", ctime(&cur_time));
    slapi_rename_internal_set_pb (pb, dn, newrdn, NULL, 1, NULL, NULL,
	 					          repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
    slapi_modrdn_internal_pb (pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    slapi_pblock_destroy (pb);
    if (rc != LDAP_SUCCESS)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "failed to rename entry (%s); "
                        "LDAP error - %d\n", dn, rc);
        return -1;
    }

    /* delete the entry */
    pb = slapi_pblock_new ();
    PR_snprintf (dn, sizeof(dn), "%s,%s", newrdn, REPLICA_ROOT);
    slapi_delete_internal_set_pb (pb, dn, NULL,  NULL, 
								  repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
    slapi_delete_internal_pb (pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    slapi_pblock_destroy (pb);
    if (rc != LDAP_SUCCESS)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, "failed to delete entry (%s); "
                        "LDAP error - %d\n", dn, rc);
        return -1;
    }

    return 0;    
}

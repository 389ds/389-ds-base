/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include	<stdio.h>
#include	<netsite.h>
#include	<base/session.h>
#include	<base/daemon.h>
#include	<base/systhr.h>
#include	<libaccess/nserror.h>
#include	<libaccess/acl.h>
#include	"../aclpriv.h"
#include	<libaccess/aclproto.h>
#include	"../aclcache.h"
#include	<libaccess/las.h>


extern	ACLListHandle_t *ACL_ParseFile(NSErr_t *errp, char *filename);

int
TestEvalFunc(NSErr_t *errp, char *attr, CmpOp_t comparator,
             char *attr_pattern, ACLCachable_t *cachable,
             void **las_cookie, PList_t subject, PList_t resource,
             PList_t auth_info, PList_t global_auth)
{
	return 0;
}

void
TestFlushFunc(void **cookie)
{
	return;
}

static int parse_dburl (NSErr_t *errp, ACLDbType_t dbtype,
			const char *dbname, const char *url,
			PList_t plist, void **db)
{
    *db = strdup(url);
    return 0;
}


main()
{
	ACLListHandle_t	*acl_list;
	int		result;
	ACLCachable_t cachable = 0;
	void	*las_cookie=NULL;
	ACLEvalHandle_t	eval;
	char	*rights[3];
	char	filename[20];
	char	newfilename[25];
	int		i;
	char	*map_generic[7];
	LASEvalFunc_t	Eval_funcp;
	LASFlushFunc_t	Flush_funcp;
	char	*bong;
	char	*bong_type;
	char	*acl_tag;
	int	expr_num;
	int	ii;
	char	**name_list;
	ACLMethod_t	method=NULL;
	ACLDbType_t	dbtype=NULL;
	int	rv;
	ACLAttrGetterList_t aglist;
	ACLAttrGetter_t *agptr;
	char **names;
	int cnt;
	
	systhread_init("acl_utest");

        char	*acl_file_list[3] = {"aclfile6", "aclfile7", NULL};
        char	*new_filename = "merge6_7";
        char	*acl_name_list[3] = {"aclfile6", "aclfile7", NULL};
        char	*new_aclname = "merge6_7";
        char	*bad_acl_file_list[3] = {"bad_aclfile6", "bad_aclfile7", NULL};

        if ( ACL_FileMergeFile(NULL, new_filename, bad_acl_file_list, 0) < 0 ) {
            printf("Failed ACL_FileMergeFile() test.\n");
        }

        if ( ACL_FileMergeFile(NULL, new_filename, acl_file_list, 0) < 0 ) {
            printf("Failed ACL_FileMergeFile() test.\n");
        }

        if ( ACL_FileMergeAcl(NULL, new_filename, acl_name_list, new_aclname, 0) < 0 ) {
            printf("Failed ACL_FileMergeAcl() test.\n");
        }

	/*	LAS Registration Unit Tests	*/

	ACL_Init();

	rv = ACL_MethodRegister(NULL, "one", &method);
	printf("Method one is #%d, rv=%d\n", (int)method, rv);

	rv = ACL_MethodRegister(NULL, "two", &method);
	printf("Method two is #%d, rv=%d\n", (int)method, rv);

	rv = ACL_MethodRegister(NULL, "one", &method);
	printf("Method one repeated is #%d, rv=%d\n", (int)method, rv);

	rv = ACL_MethodRegister(NULL, "three", &method);
	printf("Method three is #%d, rv=%d\n", (int)method, rv);

	rv = ACL_MethodNamesGet(NULL, &names, &cnt);

	for(i = 0; i < cnt; i++) {
	    printf("\tMethod[%d] = \"%s\"\n", i, names[i]);
	}

	ACL_MethodNamesFree(NULL, names, cnt);
	
	if (!ACL_MethodIsEqual(NULL, method, method)) {
	    printf("Error comparing methods");
	}

	if (!ACL_MethodNameIsEqual(NULL, method, "three")) {
	    printf("Error comparing method by name");
	}

	/* Since LDAP is already registered by ACL_Init, the first number
	 * we'll get is actually 2.
	 */
	rv = ACL_DbTypeRegister(NULL, "two", parse_dburl, &dbtype);
	printf("DbType two is #%d, rv=%d\n", (int)dbtype, rv);

	rv = ACL_DbTypeRegister(NULL, "three", parse_dburl, &dbtype);
	printf("DbType three is #%d, rv=%d\n", (int)dbtype, rv);

	rv = ACL_DbTypeRegister(NULL, "two", parse_dburl, &dbtype);
	printf("DbType two repeated is #%d, rv=%d\n", (int)dbtype, rv);

	rv = ACL_DbTypeRegister(NULL, "four", parse_dburl, &dbtype);
	printf("DbType four is #%d, rv=%d\n", (int)dbtype, rv);

	if (!ACL_DbTypeIsEqual(NULL, dbtype, dbtype)) {
	    printf("Error comparing dbtypes\n");
	}

	if (!ACL_DbTypeNameIsEqual(NULL, dbtype, "four")) {
	    printf("Error comparing dbtype by name\n");
	}

	rv = ACL_DatabaseRegister(NULL, dbtype, "db1", "url for db1", NULL);
	if (rv < 0) {
	    printf("ACL_DatabaseRegister failed for db1\n");
	}
	
	rv = ACL_DatabaseRegister(NULL, dbtype, "db2", "url for db2", NULL);
	if (rv < 0) {
	    printf("ACL_DatabaseRegister failed for db2\n");
	}
	
	rv = ACL_DatabaseRegister(NULL, dbtype, "db3", "url for db3", NULL);
	if (rv < 0) {
	    printf("ACL_DatabaseRegister failed for db3\n");
	}
	
	rv = ACL_DatabaseNamesGet(NULL, &names, &cnt);

	for(i = 0; i < cnt; i++) {
	    printf("\tDatabase[%d] = \"%s\"\n", i, names[i]);
	}

	if (ACL_AttrGetterRegister(NULL, "attr", (ACLAttrGetterFn_t)2, (ACLMethod_t)10, (ACLDbType_t)20, ACL_AT_FRONT, NULL)) {
	    printf("Error registering attr getter\n");
	}

	if (ACL_AttrGetterRegister(NULL, "attr", (ACLAttrGetterFn_t)3, (ACLMethod_t)10, (ACLDbType_t)20, ACL_AT_END, NULL)) {
	    printf("Error registering attr getter\n");
	}

	if (ACL_AttrGetterRegister(NULL, "attr", (ACLAttrGetterFn_t)1, (ACLMethod_t)10, (ACLDbType_t)20, ACL_AT_FRONT, NULL)) {
	    printf("Error registering attr getter\n");
	}

	if (ACL_AttrGetterRegister(NULL, "attr", (ACLAttrGetterFn_t)4, (ACLMethod_t)10, (ACLDbType_t)20, ACL_AT_END, NULL)) {
	    printf("Error registering attr getter\n");
	}

	if (ACL_AttrGetterFind(NULL, "attr", &aglist)) {
	    printf("Error finding attr getter\n");
	}

	for (i = 0, agptr = ACL_AttrGetterFirst(&aglist);
         i < 4;
         i++, agptr = ACL_AttrGetterNext(&aglist, agptr)) {

        if (agptr) {
            printf("position %d\n", (int)(agptr->fn));
        }
        else {
            printf("***Error: missing getter ***\n");
        }
	}

#ifndef	XP_WIN32
	if (ACL_LasRegister(NULL, "test_attr", TestEvalFunc, TestFlushFunc)) {
		printf("Error registering Test LAS functions\n");
	}
	ACL_LasFindEval(NULL, "test_attr", &Eval_funcp);
	if (Eval_funcp != TestEvalFunc) {
		printf("Error finding Eval function - expecting %x, got %x\n",
		TestEvalFunc, Eval_funcp);
	}
	ACL_LasFindFlush(NULL, "test_attr", &Flush_funcp);
	if (Flush_funcp != TestFlushFunc) {
		printf("Error finding Flush function - expecting %x, got %x\n",
		TestFlushFunc, Flush_funcp);
	}
	ACL_LasFindEval(NULL, "wrong_attr", &Eval_funcp);
	if (Eval_funcp != NULL) {
		printf("Error finding Eval function - expecting NULL, got %x\n",
		Eval_funcp);
	}
	ACL_LasFindFlush(NULL, "wrong_attr", &Flush_funcp);
	if (Flush_funcp != NULL) {
		printf("Error finding Flush function - expecting NULL, got %x\n",
		Flush_funcp);
	}
#endif /* !XP_WIN32 */

	/*	ACL Eval Unit Tests
	 */
	rights[0] = "http_get";
	rights[1] = "http_post";
	rights[2] = NULL;

	eval.subject = NULL;
	eval.resource = NULL;

	for (i=0; i<10; i++) {
		sprintf(filename, "aclfile%d", i);
		eval.acllist	= ACL_ParseFile((NSErr_t *)NULL, filename);
                if ( eval.acllist == NULL ) {
			printf("Couldn't parse.\n");
                        continue;
		}

                sprintf(newfilename, "%s.v30", filename);
                if ( ACL_WriteFile(NULL, newfilename, eval.acllist) < 0) {
			printf("Couldn't write %s.\n", newfilename);
		}
		result	= ACL_EvalTestRights(NULL, &eval, &rights[0], 
		http_generic, &bong, &bong_type, &acl_tag, &expr_num);
		ACL_ListDestroy(NULL, eval.acllist);
		printf("%s = %d\n\n", filename, result);
	}

/********************************************************************

 TEST #1

 TEST ACL_ParseString()
 TEST ACL_WriteFile()
 TEST ACL_ParseFile()
 TEST ACL_ListFind()

*********************************************************************/
        acl_list = ACL_ParseString((NSErr_t *)NULL, 
		"version 3.0; acl > franco;");
        if ( acl_list != NULL ) {
        	ACL_ListDestroy(NULL, acl_list);
		printf("Test #1a fails parsed invalid ACL\n");
                goto skip_test;
        }

        acl_list = ACL_ParseString((NSErr_t *)NULL, 
		"version 3.0; acl franco; \nallow (read) user=franco;");
        if ( acl_list == NULL ) {
		printf("Test #1b fails couldn't parse valid ACL\n");
                goto skip_test;
        } else {
		if ( ACL_WriteFile(NULL, "buffer", acl_list) < 0) {
			printf("Test #1b, couldn't write %s.\n", "buffer");
		}
		ACL_ListDestroy(NULL, acl_list);
        }

        acl_list = ACL_ParseString((NSErr_t *)NULL, 
		"version 3.0; acl franco; \njunk (read) user=franco;");
        
        if ( acl_list != NULL ) {
		printf("Test #1c failed missed syntax error\n");
		ACL_ListDestroy(NULL, acl_list);
                goto skip_test;
        }

        acl_list = ACL_ParseString((NSErr_t *)NULL, 
		"version 3.0; acl franco; \nallow (read) user=franco;");
        
        if ( acl_list == NULL ) {
		printf("Test #1d couldn't parse valid ACL\n");
        } else {
		ACL_ListDestroy(NULL, acl_list);
                goto skip_test;
        }

	acl_list= ACL_ParseFile((NSErr_t *)NULL, "buffer");
        if ( acl_list == NULL ) {
		printf("Test #1e, couldn't perform ACL_ParseFile(buffer)\n");
                goto skip_test;
        } else {
		if ( ACL_ListFind(NULL, acl_list, "franco", ACL_CASE_INSENSITIVE) == NULL ) {
			printf("Test #1e, couldn't find %s in %s.\n", "franco", "buffer");
		}
		ACL_ListDestroy(NULL, acl_list);
        }

/********************************************************************

 TEST #2

 TEST ACL_FileDeleteAcl()
 TEST ACL_ParseFile()
 TEST ACL_ListFind()

*********************************************************************/
	if ( ACL_FileDeleteAcl(NULL, "buffer", "franco", ACL_CASE_INSENSITIVE) < 0) {
		printf("Test #2, couldn't write %s.\n", "buffer");
	}
	acl_list= ACL_ParseFile((NSErr_t *)NULL, "buffer");
        if ( acl_list == NULL ) {
		printf("Test #2, couldn't perform ACL_ParseFile(buffer)\n");
                goto skip_test;
        } else {
		if ( ACL_ListFind(NULL, acl_list, "franco", ACL_CASE_INSENSITIVE) ) {
			printf("Couldn't delete %s from %s.\n", "franco", "buffer");
		}
		ACL_ListDestroy(NULL, acl_list);
        }

/********************************************************************

 TEST #3

 TEST ACL_FileSetAcl()
 TEST ACL_ParseFile()
 TEST ACL_ListFind()

*********************************************************************/
	if ( ACL_FileSetAcl(NULL, "buffer", 
		"version 3.0; acl FileSetAcl; \nallow (read) user=franco;",
                ACL_CASE_INSENSITIVE)< 0) {
		printf("Test #3, couldn't ACL_FileSetACL(%s).\n", "FileSetAcl");
	}
	if ( ACL_FileSetAcl(NULL, "buffer", 
		"version 3.0; acl franco; \nallow (read) user=franco;",
                ACL_CASE_INSENSITIVE)< 0) {
		printf("Test #3, couldn't ACL_FileSetACL(%s).\n", "franco");
	}
	acl_list= ACL_ParseFile((NSErr_t *)NULL, "buffer");
        if ( acl_list == NULL ) {
		printf("Test #3, couldn't perform ACL_ParseFile(buffer)\n");
                goto skip_test;
        } else {
		if ( ACL_ListFind(NULL, acl_list, "franco", ACL_CASE_INSENSITIVE) == NULL) {
			printf("Test #3, couldn't set %s in %s.\n", "franco", "buffer");
		}
		if ( ACL_ListFind(NULL, acl_list, "filesetacl", ACL_CASE_INSENSITIVE) == NULL) {
			printf("Test #3, couldn't set %s in %s.\n", "filesetacl", "buffer");
		}
		ACL_ListDestroy(NULL, acl_list);
	}

/********************************************************************

 TEST #4

 TEST ACL_FileRenameAcl()
 TEST ACL_ParseFile()
 TEST ACL_ListFind()

*********************************************************************/
	if ( ACL_FileRenameAcl(NULL, "buffer", "FileSetAcl", "loser", ACL_CASE_INSENSITIVE)< 0) {
		printf("Test #4, fail ACL_FileRenameACL(filesetacl, loser).\n");
	}
	if ( ACL_FileRenameAcl(NULL, "buffer", "franco", "bigdogs", 
                              ACL_CASE_INSENSITIVE)< 0) {
		printf("Test #4, fail ACL_FileRenameACL(franco, bigdogs).\n");
	}
	acl_list= ACL_ParseFile((NSErr_t *)NULL, "buffer");
        if ( acl_list == NULL ) {
		printf("Test #3, couldn't perform ACL_ParseFile(buffer)\n");
                goto skip_test;
        } else {
		if ( ACL_ListFind(NULL, acl_list, "loser", ACL_CASE_INSENSITIVE) == NULL) {
			printf("Test #4, fail rename %s in %s.\n", "loser", "buffer");
		}
		if ( ACL_ListFind(NULL, acl_list, "bigdogs", ACL_CASE_INSENSITIVE) == NULL) {
			printf("Test #4, fail rename %s in %s.\n", "bigdogs", "buffer");
		}
		if ( ACL_ListGetNameList(NULL, acl_list, &name_list) < 0 ) {
			printf("Test #4, yikes, the GetNameList failed.\n");
                } else {
                	for (ii = 0; name_list[ii]; ii++)
                		printf("ACL %s\n", name_list[ii]);
			ACL_NameListDestroy(NULL, name_list);
		}
		ACL_ListDestroy(NULL, acl_list);
	}

	


skip_test:
/********************************************************************

 END

*********************************************************************/

	rights[0] = "html_read";
	rights[1] = "html_write";

	map_generic[0]	= "html_read";
	map_generic[1]	= "html_write";
	map_generic[2]	= "N/A";
	map_generic[3]	= "html_create";
	map_generic[4]	= "html_delete";
	map_generic[5]	= "N/A";
	map_generic[6]	= NULL;

	for (i=10; i<20; i++) {
		sprintf(filename, "aclfile%d", i);
		eval.acllist	= ACL_ParseFile((NSErr_t *)NULL, filename);
		if ( eval.acllist == NULL ) {
			printf("Parse failed.\n");
			continue;
		}
		result	= ACL_EvalTestRights(NULL, &eval, &rights[0], map_generic, &bong, &bong_type, &acl_tag, &expr_num);
		ACL_ListDestroy(NULL, eval.acllist);
		printf("%s = %d\n\n", filename, result);
	}

	/*
	 *	Program LAS Unit Tests
	 */
	char *groups[32] = {
		"http-foo",
		"http-bar",
		"http-grog",
		NULL
	};
	char *programs[32] = {
		"foo, fubar, frobozz",
		"bar, shoo, fly",
		"grog, beer",
		NULL
	};
	struct program_groups program_groups;
	program_groups.groups = groups;
	program_groups.programs = programs;

	result = LASProgramEval(NULL, "program", CMP_OP_EQ, "http-foo, http-bar,http-grog", &cachable, &las_cookie, (PList_t)"foo", (PList_t)&program_groups, NULL, NULL);
	printf("program = foo %d\n\n", result);


	result = LASProgramEval(NULL, "program", CMP_OP_EQ, "http-foo, http-bar,http-grog", &cachable, &las_cookie, (PList_t)"nomatch", (PList_t)&program_groups, NULL, NULL);
	printf("program = nomatch %d\n\n", result);


	result = LASProgramEval(NULL, "program", CMP_OP_EQ, "http-foo, http-bar,http-grog", &cachable, &las_cookie, (PList_t)"beer", (PList_t)&program_groups, NULL, NULL);
	printf("program = beer %d\n\n", result);


	result = LASProgramEval(NULL, "program", CMP_OP_EQ, "http-foo, http-bar, http-grog", &cachable, &las_cookie, (PList_t)"http-grog", (PList_t)&program_groups, NULL, NULL);
	printf("program = http-grog %d\n\n", result);

	result = LASProgramEval(NULL, "program", CMP_OP_EQ, "http-foo", &cachable, &las_cookie, (PList_t)"ubar", (PList_t)&program_groups, NULL, NULL);
	printf("program = ubar %d\n\n", result);


	/*	
	 *	DNS LAS Unit Tests
	 */

	result	= LASDnsEval(NULL, "dnsalias", CMP_OP_EQ, "*", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("dnsalias = *? %d\n\n", result);

	LASDnsFlush(&las_cookie);

	result	= LASDnsEval(NULL, "dnsalias", CMP_OP_EQ, "aruba.mcom.com brain251.mcom.com", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("dnsalias = aruba.mcom.com brain251.mcom.com? %d\n\n", result);

	LASDnsFlush(&las_cookie);

	result	= LASDnsEval(NULL, "dns", CMP_OP_EQ, "*", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("dns = *? %d\n\n", result);

	result	= LASDnsEval(NULL, "dns", CMP_OP_NE, "*", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("dns != *? %d\n\n", result);

	LASDnsFlush(&las_cookie);

	result	= LASDnsEval(NULL, "dns", CMP_OP_EQ, "aruba.mcom.com", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("dns = aruba.mcom.com? %d\n\n", result);

	LASDnsFlush(&las_cookie);

	result	= LASDnsEval(NULL, "dns", CMP_OP_EQ, "ai.mit.edu", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("dns = ai.mit.edu? %d\n\n", result);

	LASDnsFlush(&las_cookie);

	result	= LASDnsEval(NULL, "dns", CMP_OP_EQ, "*.ai.mit.edu", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("dns = *.ai.mit.edu? %d\n\n", result);

	LASDnsFlush(&las_cookie);

	result	= LASDnsEval(NULL, "dns", CMP_OP_EQ, "*.mit.edu", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("dns = *.mit.edu? %d\n\n", result);

	LASDnsFlush(&las_cookie);

	result	= LASDnsEval(NULL, "dns", CMP_OP_EQ, "*.edu", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("dns = *.edu? %d\n\n", result);

	LASDnsFlush(&las_cookie);

	result	= LASDnsEval(NULL, "dns", CMP_OP_NE, "*.edu", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("dns != *.edu? %d\n\n", result);

	LASDnsFlush(&las_cookie);

	result	= LASDnsEval(NULL, "mistake", CMP_OP_NE, "*.edu", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("mistake != *.edu? %d\n\n", result);

	LASDnsFlush(&las_cookie);

	result	= LASDnsEval(NULL, "dns", CMP_OP_GT, "*.edu", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("dns > *.edu? %d\n\n", result);

	LASDnsFlush(&las_cookie);


	/* 
	 *	IP LAS Unit Tests
	 */
	result	= LASIpEval(NULL, "ip", CMP_OP_EQ, "*", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip = *? %d\n\n", result);

	LASIpFlush(&las_cookie);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_NE, "*", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip != *? %d\n\n", result);

	LASIpFlush(&las_cookie);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_EQ, "*.*.*.*", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip = *.*.*.*? %d\n\n", result);

	LASIpFlush(&las_cookie);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_EQ, "17.*", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip = 17.*? %d\n\n", result);

	LASIpFlush(&las_cookie);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_EQ, "17.*.*.*", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip = 17.*.*.*? %d\n\n", result);

	LASIpFlush(&las_cookie);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_EQ, "17.34.*", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip = 17.34.*? %d\n\n", result);

	LASIpFlush(&las_cookie);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_EQ, "17.34.*.*", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip = 17.34.*.*? %d\n\n", result);

	LASIpFlush(&las_cookie);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_EQ, "17.34.51.*", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip = 17.34.51.*? %d\n\n", result);

	LASIpFlush(&las_cookie);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_EQ, "17.34.51.*+255.255.255.255", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip = 17.34.51.*+255.255.255.255? %d\n\n", result);

	LASIpFlush(&las_cookie);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_EQ, "17.34.51.69+255.255.255.254, 123.45.67.89", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip = 17.34.51.69+255.255.255.254, 123.45.67.89? %d\n\n", result);

	LASIpFlush(&las_cookie);

	result	= LASIpEval(NULL, "ip", CMP_OP_NE, "17.34.51.69+255.255.255.254, 123.45.67.89", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip != 17.34.51.69+255.255.255.254, 123.45.67.89? %d\n\n", result);

	LASIpFlush(&las_cookie);

	result	= LASIpEval(NULL, "ip", CMP_OP_EQ, "17.34.51.68, 17.34.51.69", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip = 17.34.51.68, 17.34.51.69? %d\n\n", result);

	LASIpFlush(&las_cookie);

	result	= LASIpEval(NULL, "ip", CMP_OP_EQ, "17.34.51.68, 17.34.51.69, 123.45.67.89", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip = 17.34.51.68, 17.34.51.69, 123.45.67.89? %d\n\n", result);

	LASIpFlush(&las_cookie);

	result	= LASIpEval(NULL, "ip", CMP_OP_NE, "17.34.51.68, 17.34.51.69, 123.45.67.89", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip != 17.34.51.68, 17.34.51.69, 123.45.67.89? %d\n\n", result);

	LASIpFlush(&las_cookie);

	result	= LASIpEval(NULL, "ip", CMP_OP_EQ, "17.34.51.68", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip = 17.34.51.68? %d\n\n", result);

	LASIpFlush(&las_cookie);

	result	= LASIpEval(NULL, "ip", CMP_OP_EQ, "17.34.51.69", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip = 17.34.51.69? %d\n\n", result);

	LASIpFlush(&las_cookie);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_EQ, "17.34.51.69+255.255.255.254", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip = 17.34.51.69+255.255.255.254? %d\n\n", result);

	LASIpFlush(&las_cookie);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_EQ, "17.34.50.69+255.255.254.0", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip = 17.34.50.69+255.255.254.0? %d\n\n", result);

	LASIpFlush(&las_cookie);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_EQ, "17.35.50.69+255.254.0.0", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip = 17.35.50.69+255.254.0.0? %d\n\n", result);

	LASIpFlush(&las_cookie);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_EQ, "16.35.50.69+254.0.0.0", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip = 16.35.50.69+254.0.0.0? %d\n\n", result);

	LASIpFlush(&las_cookie);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_EQ, "123.45.67.89", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip = 123.45.67.89? %d\n\n", result);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_NE, "123.45.67.89", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip != 123.45.67.89? %d\n\n", result);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_GT, "123.45.67.89", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip > 123.45.67.89? %d\n\n", result);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_LT, "123.45.67.89", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip < 123.45.67.89? %d\n\n", result);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_GE, "123.45.67.89", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip >= 123.45.67.89? %d\n\n", result);
	
	result	= LASIpEval(NULL, "ip", CMP_OP_LE, "123.45.67.89", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("ip <= 123.45.67.89? %d\n\n", result);

	LASIpFlush(&las_cookie);
	
	result	= LASIpEval(NULL, "mistake", CMP_OP_LE, "123.45.67.89", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf ("mistake <= 123.45.67.89? %d\n\n", result);

	LASIpFlush(&las_cookie);
	

	/*
	 *	Time of Day unit tests.
	 */
	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_EQ, "2120", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time = 2120? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_NE, "2120", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time != 2120? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_EQ, "0700", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time = 0700? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_NE, "0700", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time != 0700? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_EQ, "2400", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time = 2400? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_NE, "2400", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time != 2400? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_GT, "2120", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time > 2120? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_LT, "2120", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time < 2120? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_GT, "0700", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time > 0700? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_LT, "0700", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time < 0700? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_GT, "2400", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time > 2400? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_LT, "2400", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time < 2400? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_GE, "2120", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time >= 2120? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_LE, "2120", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time <= 2120? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_GE, "0700", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time >= 0700? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_LE, "0700", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time <= 0700? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_GE, "2400", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time >= 2400? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_LE, "2400", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time <= 2400? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "mistake", CMP_OP_LE, "2400", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("mistake <= 2400? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_EQ, "0800-2200", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time = 0800-2200? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_NE, "0800-2200", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time != 0800-2200? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_EQ, "2200-0800", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time = 2200-0800? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_NE, "2200-0800", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time != 2200-0800? %d\n\n", result);

	result	= LASTimeOfDayEval(NULL, "timeofday", CMP_OP_LE, "2200-0800", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("time <= 2200-0800? %d\n\n", result);


	/*
	 *	Day Of Week Unit Tests
	 */
	result	= LASDayOfWeekEval(NULL, "dayofweek", CMP_OP_EQ, "Mon", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("= mon? %d\n\n", result);

	result	= LASDayOfWeekEval(NULL, "dayofweek", CMP_OP_EQ, "tUe", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("= tUe? %d\n\n", result);

	result	= LASDayOfWeekEval(NULL, "dayofweek", CMP_OP_EQ, "weD", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("= weD? %d\n\n", result);

	result	= LASDayOfWeekEval(NULL, "dayofweek", CMP_OP_EQ, "THu", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("= THu? %d\n\n", result);

	result	= LASDayOfWeekEval(NULL, "dayofweek", CMP_OP_EQ, "FrI", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("= FrI? %d\n\n", result);

	result	= LASDayOfWeekEval(NULL, "dayofweek", CMP_OP_EQ, "sAT", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("= tUe? %d\n\n", result);

	result	= LASDayOfWeekEval(NULL, "dayofweek", CMP_OP_EQ, "Sun", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("= Sun? %d\n\n", result);

	result	= LASDayOfWeekEval(NULL, "dayofweek", CMP_OP_EQ, "mon,tuewed,thu,frisatsun", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("= mon,tuewed,thu,frisatsun? %d\n\n", result);

	result	= LASDayOfWeekEval(NULL, "dayofweek", CMP_OP_NE, "mon,tuewed,thu,frisatsun", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("!= mon,tuewed,thu,frisatsun? %d\n\n", result);

	result	= LASDayOfWeekEval(NULL, "dayofweek", CMP_OP_GT, "Sun", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("> Sun? %d\n\n", result);

	result	= LASDayOfWeekEval(NULL, "dayofweek", CMP_OP_LT, "Sun", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("< Sun? %d\n\n", result);

	result	= LASDayOfWeekEval(NULL, "dayofweek", CMP_OP_GE, "Sun", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf(">= Sun? %d\n\n", result);

	result	= LASDayOfWeekEval(NULL, "dayofweek", CMP_OP_LE, "Sun", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("<= Sun? %d\n\n", result);

	result	= LASDayOfWeekEval(NULL, "mistake", CMP_OP_LE, "Sun", &cachable, &las_cookie, NULL, NULL, NULL, NULL);
	printf("mistake <= Sun? %d\n\n", result);


	ACL_Destroy();

	exit(0);

}

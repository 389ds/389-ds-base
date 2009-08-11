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
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include <prinit.h>		// for PR_Init
#include <prpriv.h>		// for PR_Exit
#include <ldaputil/certmap.h>
#include <ldaputil/init.h>
#include <ldaputil/ldapdb.h>
#include <ldaputil/ldapauth.h>
#include <ldaputil/dbconf.h>
#include <ldaputil/ldaputil.h>
#include <ldap.h>

static const char* dllname = "plugin.so";

char *global_issuer_dn = "o=" VENDOR ", c=US";

#define NSPR_INIT(Program) (PR_Init(PR_USER_THREAD, PR_PRIORITY_NORMAL, 8))

static int ldapu_certinfo_save_test (const char *fname, const char *old_fname)
{
    int rv;

    /* Read the original certmap config file first */
    rv = ldaputil_init(old_fname, dllname, NULL, NULL, NULL);

    if (rv != LDAPU_SUCCESS) {
	fprintf(stderr, "ldapu_certinfo_save_test failed.  Reason: %s\n",
		ldapu_err2string(rv));
	return rv;
    }

    rv = ldapu_certinfo_save(fname, old_fname, "certmap.tmp");

    if (rv != LDAPU_SUCCESS) {
	fprintf(stderr, "ldapu_certinfo_save_test failed.  Reason: %s\n",
		ldapu_err2string(rv));
    }

    return rv;
}

static int ldapu_certinfo_delete_test (const char *fname, const char *old_fname)
{
    int rv;

    /* Read the original certmap config file first */
    rv = ldaputil_init(old_fname, dllname, NULL, NULL, NULL);

    if (rv != LDAPU_SUCCESS) {
	fprintf(stderr, "ldapu_certinfo_delete_test failed.  Reason: %s\n",
		ldapu_err2string(rv));
	return rv;
    }

    /* rv = ldapu_certinfo_delete("o=Ace Industry, c=US"); */
    rv = ldapu_certinfo_delete("o=" VENDOR ", c=US");

    if (rv != LDAPU_SUCCESS) {
	fprintf(stderr, "ldapu_certinfo_delete failed.  Reason: %s\n",
		ldapu_err2string(rv));
	return rv;
    }

    rv = ldapu_certinfo_save(fname, old_fname, "certmap.tmp");

    if (rv != LDAPU_SUCCESS) {
	fprintf(stderr, "ldapu_certinfo_delete_test failed.  Reason: %s\n",
		ldapu_err2string(rv));
    }

    return rv;
}

static int ldapu_certinfo_new_test (const char *fname, const char *old_fname)
{
    int rv;
    LDAPUPropValList_t *propval_list;
    LDAPUPropVal_t *propval;

    /* Read the original certmap config file first */
    rv = ldaputil_init(old_fname, dllname, NULL, NULL, NULL);

    if (rv != LDAPU_SUCCESS) {
	fprintf(stderr, "ldapu_certinfo_new_test failed.  Reason: %s\n",
		ldapu_err2string(rv));
	return rv;
    }

    /* Setup propval_list */
    rv = ldapu_list_alloc(&propval_list);
    if (rv != LDAPU_SUCCESS) return rv;

    rv = ldapu_propval_alloc("prop1", "val1", &propval);
    if (rv != LDAPU_SUCCESS) return rv;

    rv = ldapu_list_add_info(propval_list, propval);
    if (rv != LDAPU_SUCCESS) return rv;

    rv = ldapu_propval_alloc("prop2", "val2", &propval);
    if (rv != LDAPU_SUCCESS) return rv;

    rv = ldapu_list_add_info(propval_list, propval);
    if (rv != LDAPU_SUCCESS) return rv;

    rv = ldapu_propval_alloc("prop3", 0, &propval);
    if (rv != LDAPU_SUCCESS) return rv;

    rv = ldapu_list_add_info(propval_list, propval);
    if (rv != LDAPU_SUCCESS) return rv;

    rv = ldapu_certinfo_modify("newmap", "o=Mcom Communications, c=US",
			       propval_list);

    ldapu_propval_list_free(propval_list);

    if (rv != LDAPU_SUCCESS) {
	fprintf(stderr, "ldapu_certinfo_delete failed.  Reason: %s\n",
		ldapu_err2string(rv));
	return rv;
    }

    rv = ldapu_certinfo_save(fname, old_fname, "certmap.tmp");

    if (rv != LDAPU_SUCCESS) {
	fprintf(stderr, "ldapu_certinfo_new_test failed.  Reason: %s\n",
		ldapu_err2string(rv));
    }

    return rv;
}

static int get_dbnames_test (const char *mapfile)
{
    char **names;
    int cnt;
    int rv;
    int i;

    rv = dbconf_get_dbnames(mapfile, &names, &cnt);

    if (rv != LDAPU_SUCCESS) {
	fprintf(stderr, "get_dbnames_test failed. Reason: %s\n",
		ldapu_err2string(rv));
    }
    else {
	for(i = 0; i < cnt; i++) {
	    fprintf(stderr, "\tdbname[%d] = \"%s\"\n", 
		    i, names[i]);
	}
    }

    dbconf_free_dbnames(names);

    return rv;
}

static int case_ignore_strcmp (const char *s1, const char *s2)
{
    int ls1, ls2;		/* tolower values of chars in s1 & s2 resp. */

    if (!s1) return !s2 ? 0 : 0-tolower(*s2);
    else if (!s2) return tolower(*s1);

    while(*s1 && *s2 && (ls1 = tolower(*s1)) == (ls2 = tolower(*s2))) { s1++; s2++; }

    if (!*s1)
	return *s2 ? 0-tolower(*s2) : 0;
    else if (!*s2)
	return tolower(*s1);
    else
	return ls1 - ls2;
}

#define STRCASECMP3(s1, s2, rv) \
{ \
    int i = case_ignore_strcmp(s1, s2); \
    fprintf(stderr, "strcasecmp(\"%s\", \"%s\")\t=\t%d\t%s\tExpected: %d\n", \
	    s1 ? s1 : "<NULL>", s2 ? s2 : "<NULL>", \
	    i, i == rv ? "SUCCESS" : "FAILED", rv); \
}

#ifndef XP_WIN32
#define STRCASECMP(s1, s2) STRCASECMP3(s1, s2, strcasecmp(s1, s2))
#else
#define STRCASECMP(s1, s2) STRCASECMP3(s1, s2, case_ignore_strcmp(s1, s2))
#endif

static void strcasecmp_test ()
{
    STRCASECMP3(0, "aBcD", 0-tolower('a'));
    STRCASECMP3(0, 0, 0);
    STRCASECMP3("aBcD", 0, tolower('a'));

    STRCASECMP("AbCd", "aBcD");
    STRCASECMP("AbCd", "abcd");
    STRCASECMP("ABCD", "ABCD");
    STRCASECMP("abcd", "abcd");

    STRCASECMP("AbCd", "aBcD3");
    STRCASECMP("AbCd", "abcd3");
    STRCASECMP("ABCD", "ABCD3");
    STRCASECMP("abcd", "abcd3");
			      
    STRCASECMP("AbCd1", "aBcD");
    STRCASECMP("AbCd2", "abcd");
    STRCASECMP("ABCDX", "ABCD");
    STRCASECMP("abcdY", "abcd");

    STRCASECMP("AbCd5", "aBcD1");
    STRCASECMP("AbCd5", "abcd1");
    STRCASECMP("ABCD5", "ABCD1");
    STRCASECMP("abcd5", "abcd1");
			       
    STRCASECMP("AbCd2", "aBcDp");
    STRCASECMP("AbCd2", "abcdQ");
    STRCASECMP("ABCD2", "ABCDr");
    STRCASECMP("abcd2", "abcdS");
}

static int certmap_tests (const char *config_file) { return 0; }

static int read_config_test (const char *config_file, const char *dbname,
			     const char *url,
			     const char *binddn, const char *bindpw)
{
    int rv;
    DBConfDBInfo_t *db_info;
    char *dn;
    char *pw;

    rv = dbconf_read_default_dbinfo(config_file, &db_info);

    if (rv != LDAPU_SUCCESS) {
	fprintf(stderr, "config_test failed: %s\n",
		ldapu_err2string(rv));
	return LDAPU_FAILED;
    }

    if (strcmp(db_info->dbname, dbname) ||
	strcmp(db_info->url, url)) {
	fprintf(stderr, "config_test failed: %s\n",
		"first line in config file is wrong");
	return LDAPU_FAILED;
    }

    if ((ldapu_dbinfo_attrval(db_info, "binddn", &dn) != LDAPU_SUCCESS) ||
	(ldapu_dbinfo_attrval(db_info, "bindpw", &pw) != LDAPU_SUCCESS))
    {
	fprintf(stderr, "config_test failed: %s\n",
		"properties are missing");
	return LDAPU_FAILED;
    }

    if (strcmp(dn, binddn) ||
	strcmp(pw, bindpw)) {
	fprintf(stderr, "config_test failed: %s\n",
		"property values are wrong");
	return LDAPU_FAILED;
    }

    fprintf(stderr, "binddn from config file: \"%s\"\n", dn);
    fprintf(stderr, "bindpw from config file: \"%s\"\n", pw);

    /* cleanup */
    dbconf_free_dbinfo(db_info);
    free(dn);
    free(pw);

    return LDAPU_SUCCESS;
}

static int config_test (const char *binddn, const char *bindpw)
{
    char *config_file = "config_out.conf";
    FILE *fp = fopen(config_file, "w");
    const char *dbname = "default";
    const char *url = "file:/foobar/path";
    int rv;

    if (!fp) return LDAPU_FAILED;

    dbconf_output_db_directive(fp, dbname, url);
    dbconf_output_propval(fp, dbname, "binddn", binddn, 0);
    dbconf_output_propval(fp, dbname, "bindpw", bindpw, 1);

    fclose(fp);

    fprintf(stderr, "Config file written: %s\n", config_file);

    rv = read_config_test(config_file, dbname, url, binddn, bindpw);

    return rv;
}

static int
compare_groupid(const void *arg, const char *group, const int len)
{
    auto const char* groupid = (const char*)arg;
    auto int err = LDAPU_FAILED;
    if (len == strlen (groupid) && !strncasecmp (groupid, group, len)) {
	err = LDAPU_SUCCESS;
    }
    return err;
}

static int
compare_group(LDAP* directory, LDAPMessage* entry, void* set)
{
    auto int err = LDAPU_FAILED;
    auto char** vals = ldap_get_values (directory, entry, "CN");
    if (vals) {
	auto char** val;
	for (val = vals; *val; ++val) {
	    if (!strcasecmp (*val, (char*)set)) {
		err = LDAPU_SUCCESS;
		break;
	    }
	}
	ldap_value_free (vals);
    }
    return err;
}

int perform_test (int argc, char *argv[])
{
    int test_type;
    int retval = LDAPU_SUCCESS;
    DBConfDBInfo_t *db_info;
    LDAPDatabase_t *ldb;
    LDAP *ld;
    char *dbmap_file = "dblist.conf";
    char *binddn = 0;
    char *bindpw = 0;
    char *basedn;
    int retry = 1;
    int rv;

    fprintf(stderr, "\nStart of test: ./auth %s \"%s\" \"%s\"\n",
	    argv[1], argv[2], argv[3]);

    rv = dbconf_read_default_dbinfo(dbmap_file, &db_info);

    if (rv != LDAPU_SUCCESS) {
	fprintf(stderr, "Error reading dbmap file \"%s\".  Reason: %s\n",
		dbmap_file, ldapu_err2string(rv));
	return rv;
    }

    ldapu_dbinfo_attrval (db_info, LDAPU_ATTR_BINDDN, &binddn);
    ldapu_dbinfo_attrval (db_info, LDAPU_ATTR_BINDPW, &bindpw);

    rv = ldapu_url_parse (db_info->url, binddn, bindpw, &ldb);
    free(binddn);
    free(bindpw);

    if (rv != LDAPU_SUCCESS) {
	fprintf(stderr, "Error parsing ldap url \"%s\".  Reason: %s\n",
		db_info->url, ldapu_err2string(rv));
	return rv;
    }

    basedn = ldb->basedn;

    test_type = atoi(argv[1]);

    retry = 1;

    while(retry) {
	retry = 0;

	rv = ldapu_ldap_init_and_bind (ldb);

	if (rv != LDAPU_SUCCESS) {
	    fprintf(stderr, "Error initializing connection to LDAP.  Reason: %s\n",
		    ldapu_err2string(rv));
	    return rv;
	}

	ld = ldb->ld;

	switch(test_type) {
	case 1:
	    fprintf(stderr, "\nuserdn:\t\t\"%s\"\ngroupdn:\t\"%s\"\n",
		    argv[2], argv[3]);
	    retval = ldapu_auth_userdn_groupdn(ld, argv[2], argv[3], basedn);
	    break;

	case 2:
	    fprintf(stderr, "\nuid:\t\t\"%s\"\ngroupdn:\t\"%s\"\n", argv[2], argv[3]);
	    retval = ldapu_auth_uid_groupdn(ld, argv[2], argv[3], basedn);
	    break;

	case 3:
	    fprintf(stderr, "\nuid:\t\t\"%s\"\ngroupid:\t\"%s\"\n", argv[2], argv[3]);
	    retval = ldapu_auth_uid_groupid(ld, argv[2], argv[3], basedn);
	    break;

	case 4:
	    fprintf(stderr, "\nuserdn:\t\t\"%s\"\ngroupid:\t\"%s\"\n", argv[2], argv[3]);
	    retval = ldapu_auth_userdn_groupid(ld, argv[2], argv[3], basedn);
	    break;

	case 5:
	    fprintf(stderr, "\nuserdn:\t\t\"%s\"\nattrFilter:\t\"%s\"\n", argv[2], argv[3]);
	    retval = ldapu_auth_userdn_attrfilter(ld, argv[2], argv[3]);
	    break;

	case 6:
	    fprintf(stderr, "\nuid:\t\t\"%s\"\nattrFilter:\t\"%s\"\n", argv[2], argv[3]);
	    retval = ldapu_auth_uid_attrfilter(ld, argv[2], argv[3], basedn);
	    break;

	case 7:
	    fprintf(stderr, "\nuserdn:\t\t\"%s\"\npassword:\t\"%s\"\n", argv[2], argv[3]);
	    retval = ldapu_auth_userdn_password(ld, argv[2], argv[3]);
	    break;

	case 8:
	    fprintf(stderr, "\nuid:\t\t\"%s\"\npassword:\t\"%s\"\n", argv[2], argv[3]);
	    retval = ldapu_auth_uid_password(ld, argv[2], argv[3], basedn);
	    break;

	case 9: {
	    /* plugin test */
	    LDAPMessage *entry = 0;
	    LDAPMessage *res = 0;

	    fprintf(stderr, "Cert Map issuer DN: \"%s\"\n", argv[2]);
	    fprintf(stderr, "Cert Map subject DN: \"%s\"\n", argv[3]);
	    retval = ldaputil_init("certmap.conf", dllname, NULL, NULL, NULL);

	    if (retval != LDAPU_SUCCESS) {
		fprintf(stderr, "Cert Map info test failed.  Reason: %s\n",
			ldapu_err2string(retval));
		break;
	    }

	    if (*(argv[2]))
		global_issuer_dn = argv[2];
	    else
		global_issuer_dn = 0;

	    retval = ldapu_cert_to_ldap_entry(argv[3], ld, ldb->basedn, &res);

	    if (retval == LDAPU_SUCCESS) {
		char *dn;

		entry = ldap_first_entry(ld, res);
		dn = ldap_get_dn(ld, entry);
		fprintf(stderr, "Matched entry to cert: \"%s\"\n", dn);
		ldap_memfree(dn);
	    }
	    else if (retval == LDAPU_FAILED) {
		/* Not an error but couldn't map the cert */
	    }
	    else {
		fprintf(stderr, "Cert Map info test failed.  Reason: %s\n",
			ldapu_err2string(retval));
		break;
	    }

	    /* TEMPORARY -- when & how to free the entry */
	    if (res) ldap_msgfree(res);

	    break;
	} /* case 9 */

	case 10:
	    if ((retval = config_test(argv[2], argv[3])) == LDAPU_SUCCESS) {
		fprintf(stderr, "Config file test succeeded\n");
	    }
	    else {
		fprintf(stderr, "Config file test failed\n");
	    }
	    break;

	case 11:
	    retval = get_dbnames_test(argv[2]);
	    break;

	case 12:
	    retval = ldapu_certinfo_save_test(argv[2], argv[3]);
	    break;

	case 13:
	    retval = ldapu_certinfo_delete_test(argv[2], argv[3]);
	    break;

	case 14:
	    retval = ldapu_certinfo_new_test(argv[2], argv[3]);
	    break;

	case 15:
	    fprintf(stderr, "\nuserdn:\t\t\"%s\"\ngroupid:\t\"%s\"\n", argv[2], argv[3]);
	    {
		auto LDAPU_DNList_t* userDNs = ldapu_DNList_alloc();
		ldapu_DNList_add(userDNs, argv[2]);
		retval = ldapu_auth_usercert_groups(ld, basedn, userDNs, NULL,
						    argv[3], compare_group, 30, NULL);
		ldapu_DNList_free(userDNs);
	    }
	    break;

	case 16:
	    fprintf(stderr, "\nuserCert:\t\"%s\"\ngroupid:\t\"%s\"\n", argv[2], argv[3]);
	    retval = ldapu_auth_usercert_groupids(ld, NULL/*userDN*/, argv[2], argv[3],
						  compare_groupid, basedn, NULL/*group_out*/);
	    break;

	} /* switch */

	if (retval == LDAP_SERVER_DOWN) {
	    /* retry */
	    retry = 1;
	    ldb->ld = 0;
	}
	else if (retval == LDAPU_SUCCESS) {
	    fprintf(stderr, "Authentication succeeded.\n");
	}
	else {
	    fprintf(stderr, "Authentication failed.\n");
	}
    }

    /* cleanup */
//     ldapu_free_LDAPDatabase_t(ldb);
//     dbconf_free_dbinfo(db_info);
//     ldaputil_exit();
    return retval;
}

int main (int argc, char *argv[])
{
    int rv;

    NSPR_INIT("auth");

    if (argc != 4) {
        fprintf(stderr, "argc = %d\n", argc);
	fprintf(stderr, "usage: %s test_type user_dn group_dn\n", argv[0]);
	fprintf(stderr, "\t%s 1 <userdn> <groupdn>\n", argv[0]);
	fprintf(stderr, "\t%s 2 <uid> <groupdn>\n", argv[0]);
	fprintf(stderr, "\t%s 3 <uid> <groupid>\n", argv[0]);
	fprintf(stderr, "\t%s 4 <userdn> <groupid>\n", argv[0]);
	fprintf(stderr, "\t%s 5 <userdn> <attrFilter>\n", argv[0]);
	fprintf(stderr, "\t%s 6 <uid> <attrFilter>\n", argv[0]);
	fprintf(stderr, "\t%s 7 <userdn> <password>\n", argv[0]);
	fprintf(stderr, "\t%s 8 <uid> <password>\n", argv[0]);
	fprintf(stderr, "\t%s 9 <certmap.conf> <subjectDN>\n", argv[0]);
	fprintf(stderr, "\t%s 10 <binddn> <bindpw>\n", argv[0]);
	fprintf(stderr, "\t%s 11 <dbmap> <ignore>\n", argv[0]);
	fprintf(stderr, "\t%s 12 <newconfig> <oldconfig> ... to test save\n", argv[0]);
	fprintf(stderr, "\t%s 13 <newconfig> <oldconfig> ... to test delete\n", argv[0]);
	fprintf(stderr, "\t%s 14 <newconfig> <oldconfig> ... to test add\n", argv[0]);
	fprintf(stderr, "\t%s 15 <userdn> <groupid>\n", argv[0]);
	fprintf(stderr, "\t%s 16 <userCertDescription> <groupid>\n", argv[0]);
	exit(LDAP_PARAM_ERROR);
    }

    rv = perform_test(argc, argv);
    /*    PR_Exit(); */

    return rv;
}


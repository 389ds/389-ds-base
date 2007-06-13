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
#include <stdarg.h>
#include <stdlib.h>
#include <assert.h>
#include "ldap.h"
#include "dsalib.h"
#include "nspr.h"
#include "plstr.h"
#include <string.h>

#define __CFG_SSPT_C

#include "cfg_sspt.h"

/*#define CGI_DEBUG 1*/

#undef TEST_CONFIG /* for testing cn=config40 dummy entry instead of real one */

char* const NULLSTR = 0;

char* const class_top                    = "top";
char* const class_organization           = "organization";
char* const class_organizationalUnit     = "organizationalunit";
char* const class_person                 = "person";
char* const class_organizationalPerson   = "organizationalperson";
char* const class_inetOrgPerson          = "inetorgperson";
char* const class_groupOfUniqueNames     = "groupofuniquenames";
char* const class_domain                 = "domain";
char* const class_extensibleObject       = "extensibleObject";
char* const class_adminDomain			 = "nsadmindomain";
char* const class_country                = "country";
char* const class_locality               = "locality";

char* const name_objectClass             = "objectclass";
char* const name_cn                      = "cn";
char* const name_sn                      = "sn";
char* const name_givenname               = "givenname";
char* const name_uid                     = "uid";
char* const name_userPassword            = "userpassword";
char* const name_passwordExpirationTime  = "passwordExpirationTime";
char* const name_o                       = "o";
char* const name_ou                      = "ou";
char* const name_dc                      = "dc";
char* const name_member                  = "member";
char* const name_uniqueMember            = "uniquemember";
char* const name_aci                     = "aci";
char* const name_description             = "description";
char* const name_adminDomain			 = "nsadmindomainname";
char* const name_c                       = "c";
char* const name_st                      = "st";
char* const name_l                       = "l";

char* const name_netscaperootDN          = "o=NetscapeRoot";

char* const value_configAdminCN          = "Configuration Administrator";
char* const value_configAdminSN          = "Administrator";
char* const value_configAdminGN          = "Configuration";
char* const value_peopleOU    			 = "People";
char* const value_peopleDesc   			 = "Standard branch for people (uid) entries";
char* const value_groupsOU    			 = "Groups";
char* const value_groupsDesc   			 = "Standard Branch for group entries";
#ifdef TEST_CONFIG
char* const value_config40               = "config40";
char* const value_config40DN             = "cn=config40";
#endif /* TEST_CONFIG */

char* dbg_log_file                       = "ds_sscfg.log";

char* const name_localDAGroup		 	 = "Directory Administrators";
char* const value_localDAGroupDesc		 = "Entities with administrative access to this directory server";

static char* const ACI_self_allow = "(targetattr=\""
			"carLicense ||"
			"description ||"
			"displayName ||"
			"facsimileTelephoneNumber ||"
			"homePhone ||"
			"homePostalAddress ||"
			"initials ||"
			"jpegPhoto ||"
			"labeledURL ||"
			"mail ||"
			"mobile ||"
			"pager ||"
			"photo ||"
			"postOfficeBox ||"
			"postalAddress ||"
			"postalCode ||"
			"preferredDeliveryMethod ||"
			"preferredLanguage ||"
			"registeredAddress ||"
			"roomNumber ||"
			"secretary ||"
			"seeAlso ||"
			"st ||"
			"street ||"
			"telephoneNumber ||"
			"telexNumber ||"
			"title ||"
			"userCertificate ||"
			"userPassword ||"
			"userSMIMECertificate ||"
			"x500UniqueIdentifier\")"
			"(version 3.0; acl \"Enable self write for common attributes\"; allow (write) "
			"userdn=\"ldap:///self\";)";

static char* const ACI_anonymous_allow = "(targetattr!=\"userPassword\")"
			"(version 3.0; "
			"acl \"Enable anonymous access\"; allow (read, search, compare)"
			"userdn=\"ldap:///anyone\";)";

static char* const ACI_anonymous_allow_with_filter =
			"(targetattr=\"*\")(targetfilter=(%s))"
			"(version 3.0; acl \"Default anonymous access\"; "
			"allow (read, search) userdn=\"ldap:///anyone\";)";

static char* const ACI_config_admin_group_allow_all = "(targetattr=\"*\")"
			"(version 3.0; "
			"acl \"Enable Configuration Administrator Group modification\"; "
			"allow (all) groupdn=\"ldap:///%s, %s=%s, %s, %s\";)";

static char* const ACI_config_admin_group_allow = "(targetattr=\"*\")"
			"(version 3.0; "
			"acl \"Configuration Administrators Group\"; allow (%s) "
			"groupdn=\"ldap:///%s\";)";

static char* const ACI_local_DA_allow = "(targetattr = \"*\")(version 3.0; "
			"acl \"Local Directory Administrators Group\"; allow (%s) "
			"groupdn=\"ldap:///%s\";)";

static char* const ACI_group_expansion = "(targetattr=\"*\")"
			"(version 3.0; acl \"Enable Group Expansion\"; "
			"allow (read, search, compare) groupdnattr=\"uniquemember\";)";

static char* const ACI_user_allow_1 = "(targetattr=\"*\")(version 3.0; "
				"acl \"Configuration Administrator\"; allow (%s) "
				"userdn=\"ldap:///uid=%s, %s\";)";

static char* const ACI_user_allow_2 = "(targetattr=\"*\")(version 3.0; "
				"acl \"Configuration Administrator\"; allow (%s) "
				"userdn=\"ldap:///%s\";)";
/*
  This is a list of DSE entries that the Configuration Admin Group has
  access to and the access rights for that entry
*/
static struct _DSEEntriesAndAccess {
	char *entryDN;
	char *access;
} entryAndAccessList[] = {
	{"cn=config", "all"},
	{"cn=schema", "all"}
};

static int entryAndAccessListSize =
	sizeof(entryAndAccessList)/sizeof(entryAndAccessList[0]);

int
getEntryAndAccess(int index, const char **entry, const char **access)
{
	if (!entry || !access)
		return 0;

	*entry = 0;
	*access = 0;

	if (index < 0 || index >= entryAndAccessListSize)
		return 0;

	*entry = entryAndAccessList[index].entryDN;
	*access = entryAndAccessList[index].access;

	return 1;
}

static int
is_root_user(const char *name, QUERY_VARS* query)
{
	if (!name || !query->rootDN) {
		return 0;
	}
	return !PL_strcasecmp(name, query->rootDN);
}

/*
** ---------------------------------------------------------------------------
**
** Utility Routines - Functions for performing string and file operations.
**
*/

#ifdef CGI_DEBUG
#include <stdarg.h>
static void debug_log (const char* file, const char* format, ...)
#ifdef __GNUC__ 
        __attribute__ ((format (printf, 2, 3)));
#else
        ;
#endif

static void
debug_log (const char* file, const char* format, ...)
{
	va_list args;
	FILE* fp = fopen(file, "a+");
	if (fp) {
		va_start(args, format);
		vfprintf(fp, format, args);
		va_end(args);
		fflush(fp);
		fclose(fp);
	}
}

static void
debug_log_array (const char* file, char* name, char** vals)
{
	FILE* fp = fopen(file, "a+");

	if (fp) {
		if (vals != NULL) {
			for (; *vals != NULL; LDAP_UTF8INC(vals)) {
				fprintf (fp, "%s: %s\n", name, *vals);
			}
			fflush(fp);
		}
		fclose(fp);
	}
}

#endif /* CGI_DEBUG */

static char *
extract_name_from_dn(const char *dn)
{
	char **rdnList = 0;
	char *ret = 0;
	if (!dn)
		return ret;

	rdnList = ldap_explode_dn(dn, 1); /* leave out types */
	if (!rdnList || !rdnList[0])
		ret = strdup(dn); /* the given dn is not really a dn */
	else
		ret = strdup(rdnList[0]);

	if (rdnList)
		ldap_value_free(rdnList);

	return ret;
}

int
entry_exists(LDAP* ld, const char* entrydn)
{
	int exists = 0;
	int err;

	struct timeval sto = { 10L, 0L };
	LDAPMessage* pLdapResult;

	err = ldap_search_st(ld, entrydn, LDAP_SCOPE_BASE, 
						 "objectClass=*", NULL, 0, &sto, &pLdapResult);
  
	if (err == LDAP_SUCCESS)
	{
		LDAPMessage* pLdapEntry;
		char* dn;
    
		for (pLdapEntry = ldap_first_entry(ld, pLdapResult);
			 pLdapEntry != NULL;
			 pLdapEntry = ldap_next_entry(ld, pLdapEntry))
		{
			if ((dn = ldap_get_dn(ld, pLdapEntry)) != NULL) 
			{
				exists = 1;
				free(dn);
				/*ldap_memfree(dn);*/
				break;
			}
		}

		ldap_msgfree(pLdapResult);
	}

	return exists;
}

int
add_aci(LDAP* ld, char* DN, char* privilege)
{
	int err;
	int ret = 0;
	LDAPMod mod;
	LDAPMod* mods[2];
	char* aci[2];

#ifdef CGI_DEBUG
	debug_log (dbg_log_file, "add_aci('%s', '%s')\n",
			   DN ? DN : "NULL", 
			   privilege ? privilege : "NULL");
#endif

	if (ld == NULL || DN == NULL || privilege == NULL)
	{
		return -1;
	}

	mods[0] = &mod;
	mods[1] = NULL;
	mod.mod_op = LDAP_MOD_ADD;
	mod.mod_type = name_aci;
	mod.mod_values = aci;
	aci[0] = privilege;
	aci[1] = NULL;
	/* fprintf (stdout, "ldap_modify_s('%s')<br>\n",DN); fflush (stdout); */
	err = ldap_modify_s (ld, DN, mods);
	if (err != LDAP_SUCCESS && err != LDAP_TYPE_OR_VALUE_EXISTS) {
		char* exp = "can't add privilege. ";
		char* explanation = PR_smprintf("%s (%i) returned from ldap_modify_s(%s, %i). Privilege: %s",
										ldap_err2string (err), err, DN, LDAP_MOD_ADD, aci[0]);
		ds_report_warning (DS_INCORRECT_USAGE, exp, explanation);
		PR_smprintf_free (explanation);
		ret = 1;
	}

	return ret;
}

/*
  Same as add_aci, except that the 3rd parameter is a format string
  in printf style format, and the 4th - Nth parameters are a NULL terminated
  list of strings to substitute in the format; basically just constructs
  the correct aci string and passes it to add_aci
*/
int add_aci_v(LDAP* ld, char* DN, char* format, ...)
#ifdef __GNUC__ 
        __attribute__ ((format (printf, 3, 4)));
#else
        ;
#endif
int
add_aci_v(LDAP* ld, char* DN, char* format, ...)
{
	char* acistring = NULL;
	int len = 0;
	int status = 0;
	int fudge = 10; /* a little extra just to make sure */
	char *s = 0;
	va_list ap;

#ifdef CGI_DEBUG
	debug_log (dbg_log_file, "add_aci_v('%s', '%s')\n",
			   DN ? DN : "NULL", 
			   format ? format : "NULL");
#endif

	if (ld == NULL || DN == NULL || format == NULL)
	{
		return -1;
	}

	/* determine the length of the string to allocate to hold
	   the aci string
	   */
	len += strlen(format) + fudge;
	va_start(ap, format);
	s = va_arg(ap, char*);
	while (s)
	{
		len += strlen(s) + 1;
		s = va_arg(ap, char*);
	}
	va_end(ap);
  
	va_start(ap, format);
	acistring = (char *)malloc(len);
	vsprintf(acistring, format, ap);
	va_end(ap);
	status = add_aci(ld, DN, acistring);

	free(acistring);

	return status;
}

/*
  Make a dn from lists of dn components.  The format argument is in the
  standard printf format.  The varargs list contains the various dn
  components.  The string returned is malloc()'d and must be free()'d by
  the caller after use.  example:
  make_dn("cn=%s, ou=%s, %s", "Admins", "TopologyManagement", "o=NetscapeRoot", NULL)
  returns
  "cn=Admins, ou=TopologyManagement, o=NetscapeRoot"
*/
char *
make_dn(const char* format, ...)
{
	char *s;
	int len = 0;
	int fudge = 3;
	va_list ap;
	char *dnstring;

#ifdef CGI_DEBUG
	debug_log (dbg_log_file, "make_dn('%s', ...)\n",
			   format ? format : "NULL");
#endif

	if (format == NULL)
	{
		return NULL;
	}

	/* determine the length of the string to allocate to hold
	   the dn string
	   */
	len += strlen(format) + fudge;
	va_start(ap, format);
	s = va_arg(ap, char*);
	while (s)
	{
		len += strlen(s) + 3;
		s = va_arg(ap, char*);
	}
	va_end(ap);
  
	va_start(ap, format);
	dnstring = (char *)malloc(len);
	vsprintf(dnstring, format, ap);
	va_end(ap);

	return dnstring;
}

char *
admin_user_exists(LDAP* ld, char* base, char *userID)
{
	int exists = 0;
	int err;
	char	search_str[MAX_STRING_LEN];

	struct timeval sto = { 10L, 0L };
	LDAPMessage* pLdapResult;
	PR_snprintf (search_str, sizeof(search_str), "uid=%s*", userID ? userID : "admin");

	err = ldap_search_st(ld, base, LDAP_SCOPE_SUBTREE, 
						 search_str, NULL, 0, &sto, &pLdapResult);
  
	if (err == LDAP_SUCCESS)
	{
		LDAPMessage* pLdapEntry;
		char* dn = NULL;
    
		for (pLdapEntry = ldap_first_entry(ld, pLdapResult);
			 pLdapEntry != NULL;
			 pLdapEntry = ldap_next_entry(ld, pLdapEntry))
		{
			if ((dn = ldap_get_dn(ld, pLdapEntry)) != NULL) 
			{
				exists = 1;
				/*ldap_memfree(dn);*/
				break;
			}
		}

		ldap_msgfree(pLdapResult);
		return dn;
	}

	return NULL;
}

static void
getUIDFromDN(const char *userID, char *uid)
{
	char **rdnListTypes = 0;
	char **rdnListNoTypes = 0;
	int ii = 0;
	int uidindex = -1;
	uid[0] = 0;

	rdnListTypes = ldap_explode_dn(userID, 0);
	if (!rdnListTypes)
		return; /* userID is not a DN */

	/* find the first rdn in the given userID DN which begins with
	   "uid=" */
	for (ii = 0; uidindex < 0 && rdnListTypes[ii]; ++ii)
	{
		if (!PL_strncasecmp(rdnListTypes[ii], "uid=", 4))
			uidindex = ii;
	}
	ldap_value_free(rdnListTypes);

	if (uidindex < 0) /* did not find an rdn beginning with "uid=" */
		return;

	rdnListNoTypes = ldap_explode_dn(userID, 1);
	PL_strncpyz(uid, rdnListNoTypes[uidindex], 1024);
	ldap_value_free(rdnListNoTypes);

	return;
}

static char *
create_ssadmin_user(LDAP* ld, char *base, char* userID, char* password)
{
	int err;
	char *ret = 0;
	char entrydn[1024] = {0};
	char realuid[1024] = {0};
	char *admin_dn = NULL;

#ifdef CGI_DEBUG
	debug_log (dbg_log_file, "create_ssadmin_user('%s','%s','%s')\n",
			   base ? base : "NULL", userID ? userID : "NULL", 
			   password ? password : "NULL");
#endif

	if (ld == NULL || base == NULL || userID == NULL || *userID == '\0' ||
		password == NULL || *password == '\0')
	{
		return NULL;
	}

	getUIDFromDN(userID, realuid);
	if (realuid[0])
	{
		PL_strncpyz(entrydn, userID, sizeof(entrydn));
		if (entry_exists(ld, entrydn))
			admin_dn = entrydn;
	}
	else
	{
		PR_snprintf(entrydn, sizeof(entrydn), "%s=%s, %s", name_uid, userID, base);
		admin_dn = admin_user_exists(ld, base, userID);
		PL_strncpyz(realuid, userID, sizeof(realuid));
	}

	if (admin_dn) 
	{
		char error[BIG_LINE];
		PR_snprintf(error, sizeof(error), "A user with uid=%s \"%s\" already exists in the directory"
				" and will not be overwritten.", realuid[0] ? realuid : "admin", admin_dn);
		ds_send_error(error, 0);
		return admin_dn;
	}
	else
	{
		LDAPMod* attrs[8];
		LDAPMod attr[7];
		char* objectClasses[5];
		char* cn[2];
		char* sn[2];
		char* givenname[2];
		char* uid[2];
		char* userPassword[2];
		char* passwordExpirationTime[2];

		attrs[0] = &attr[0];
		attrs[1] = &attr[1];
		attrs[2] = &attr[2];
		attrs[3] = &attr[3];
		attrs[4] = &attr[4];
		attrs[5] = &attr[5];
		attrs[6] = &attr[6];
		attrs[7] = NULL;
		attr[0].mod_op = LDAP_MOD_ADD;
		attr[0].mod_type = name_objectClass;
		attr[0].mod_values = objectClasses;
		objectClasses[0] = class_top;
		objectClasses[1] = class_person;
		objectClasses[2] = class_organizationalPerson;
		objectClasses[3] = class_inetOrgPerson;
		objectClasses[4] = NULL;
		attr[1].mod_op = LDAP_MOD_ADD;
		attr[1].mod_type = name_cn;
		attr[1].mod_values = cn;
		cn[0] = value_configAdminCN;
		cn[1] = NULL;
		attr[2].mod_op = LDAP_MOD_ADD;
		attr[2].mod_type = name_sn;
		attr[2].mod_values = sn;
		sn[0] = value_configAdminSN;
		sn[1] = NULL;
		attr[3].mod_op = LDAP_MOD_ADD;
		attr[3].mod_type = name_givenname;
		attr[3].mod_values = givenname;
		givenname[0] = value_configAdminGN;
		givenname[1] = NULL;
		attr[4].mod_op = LDAP_MOD_ADD;
		attr[4].mod_type = name_uid;
		attr[4].mod_values = uid;
		uid[0] = realuid;
		uid[1] = NULL;
		attr[5].mod_op = LDAP_MOD_ADD;
		attr[5].mod_type = name_userPassword;
		attr[5].mod_values = userPassword;
		userPassword[0] = password;
		userPassword[1] = NULL;
		attr[6].mod_op = LDAP_MOD_ADD;
		attr[6].mod_type = name_passwordExpirationTime;
		attr[6].mod_values = passwordExpirationTime;
		passwordExpirationTime[0] = "20380119031407Z";
		passwordExpirationTime[1] = NULL;

		/*	fprintf (stdout, "ldap_add_s(%s)<br>\n", entrydn); fflush (stdout); */

		err = ldap_add_s (ld, entrydn, attrs);

		if (err != LDAP_SUCCESS) 
		{
			char *explanation = PR_smprintf("Unable to create administrative user."
											" (%s (%i) returned from ldap_add_s(%s))",
											ldap_err2string (err), err, entrydn);
			ds_report_warning (DS_NETWORK_ERROR, " can't create user", explanation);
			PR_smprintf_free (explanation);
			ret = NULL;
		}
	}

	return NULL;
}

static int
create_base_entry(
	LDAP* ld,
	char* basedn,
	char *naming_attr_type,
	char *naming_attr_value,
	char *objectclassname
)
{
	int err;
	int ret = 0;

#ifdef CGI_DEBUG
	debug_log (dbg_log_file, "create_base_entry('%s','%s')\n",
			   basedn ? basedn : "NULL", naming_attr_value: "NULL");
#endif

	if (ld == NULL || basedn == NULL || *basedn == '\0')
	{
		return -1;
	}

	if (!entry_exists(ld, basedn))
	{
		LDAPMod* attrs[3];
		LDAPMod attr[2];
		char* objectClasses[3];
		char* names[2];

		attrs[0] = &attr[0];
		attrs[2] = NULL;
		attr[0].mod_op = LDAP_MOD_ADD;
		attr[0].mod_type = name_objectClass;
		attr[0].mod_values = objectClasses;
		objectClasses[0] = class_top;
		objectClasses[1] = objectclassname;
		objectClasses[2] = NULL;
		attrs[1] = &attr[1];
		attr[1].mod_op = LDAP_MOD_ADD;
		attr[1].mod_type = naming_attr_type;
		attr[1].mod_values = names;
		names[0] = naming_attr_value;
		names[1] = NULL;

		/*	fprintf (stdout, "ldap_add_s(%s)<br>\n", basedn); fflush (stdout); */

		err = ldap_add_s (ld, basedn, attrs);

		if (err != LDAP_SUCCESS) 
		{
			char* explanation = PR_smprintf("Unable to create base entry."
											" (%s (%i) returned from ldap_add_s(%s))",
											ldap_err2string (err), err, basedn);
			ds_report_warning (DS_NETWORK_ERROR, " can't create base entry",
							   explanation);
			PR_smprintf_free (explanation);
			ret = 1;
		}
	}

	return ret;
}

static int
create_organization(LDAP* ld, char* base, char* org)
{
	return create_base_entry(ld, base, name_o, org, class_organization);
}

static int
create_organizational_unit(LDAP* ld, char* base, char* unit, char *description,
						   char *extra_objectclassName,
						   char *extra_attrName,
						   char *extra_attrValue)
{
	int err;
	int ret = 0;
	char *entrydn = NULL;

#ifdef CGI_DEBUG
	debug_log (dbg_log_file, "create_organizational_unit('%s','%s')\n",
			   base ? base : "NULL", unit ? unit : "NULL");
#endif

	if (ld == NULL || unit == NULL || *unit == '\0')
	{
		return -1;
	}

	/*
	  if base is null, assume the unit is the full DN of the entry
	  to create; this assumes the caller knows what he/she is doing
	  and has already created the parent entry(ies)
	  */
	if (!base)
		entrydn = strdup(unit);
	else
		entrydn = make_dn("%s=%s, %s", name_ou, unit, base, NULLSTR);

	if (!entry_exists(ld, entrydn))
	{
		LDAPMod* attrs[5];
		LDAPMod attr[4];
		char* objectClasses[4];
		char* names[2];
		char* desc[2];
		char* extra[2];
		char *baseName = unit;
		int attrnum = 0;
		if (base)
		{
			baseName = strdup(unit);
		}
		else
		{
			/* since the unit is in DN form, we need to extract something to
			   use for the ou: attribute */
			baseName = extract_name_from_dn(unit);
		}
		attrs[0] = &attr[0];
		attrs[1] = &attr[1];
		attrs[2] = NULL;
		attr[0].mod_op = LDAP_MOD_ADD;
		attr[0].mod_type = name_objectClass;
		attr[0].mod_values = objectClasses;
		objectClasses[0] = class_top;
		objectClasses[1] = class_organizationalUnit;
		objectClasses[2] = extra_objectclassName; /* may be null */
		objectClasses[3] = NULL;
		attr[1].mod_op = LDAP_MOD_ADD;
		attr[1].mod_type = name_ou;
		attr[1].mod_values = names;
		names[0] = baseName;
		names[1] = NULL;
		attrnum = 2;
		if (description && *description)
		{
			attr[attrnum].mod_op = LDAP_MOD_ADD;
			attr[attrnum].mod_type = name_description;
			attr[attrnum].mod_values = desc;
			desc[0] = description;
			desc[1] = NULL;
			attrs[attrnum] = &attr[attrnum];
			attrs[++attrnum] = NULL;
		}
		if (extra_attrName && extra_attrValue &&
			*extra_attrName && *extra_attrValue)
		{
			attr[attrnum].mod_op = LDAP_MOD_ADD;
			attr[attrnum].mod_type = extra_attrName;
			attr[attrnum].mod_values = extra;
			extra[0] = extra_attrValue;
			extra[1] = NULL;
			attrs[attrnum] = &attr[attrnum];
			attrs[++attrnum] = NULL;
		}

		/*	fprintf (stdout, "ldap_add_s(%s)<br>\n", DN); fflush (stdout); */

		err = ldap_add_s (ld, entrydn, attrs);
		if (baseName)
			free(baseName);

		if (err != LDAP_SUCCESS) 
		{
			char* explanation = PR_smprintf("Unable to create organizational unit."
											" (%s (%i) returned from ldap_add_s(%s))",
											ldap_err2string (err), err, entrydn);
			ds_report_warning (DS_NETWORK_ERROR, " can't create organizational unit",
							   explanation);
			PR_smprintf_free (explanation);
			ret = 1;
		}
	}

	if (entrydn)
		free(entrydn);

	return ret;
}

static int
create_domain_component(LDAP* ld, char* base, char* domcomp)
{
	return create_base_entry(ld, base, name_dc, domcomp, class_domain);
}

static int
create_country(LDAP* ld, char* base, char* country)
{
	return create_base_entry(ld, base, name_c, country, class_country);
}

static int
create_state(LDAP* ld, char* base, char* state)
{
	return create_base_entry(ld, base, name_st, state, class_locality);
}

static int
create_locality(LDAP* ld, char* base, char* locality)
{
	return create_base_entry(ld, base, name_l, locality, class_locality);
}

static int
create_base(LDAP* ld, char* base)
{
	int ret = 0;
	char* attr;
	char **rdnList = 0;
	char **rdnListNoTypes = 0;
	enum BASETYPE { unknown, org, orgunit, domcomp, country, state, locality } base_type = unknown;

#ifdef CGI_DEBUG
	debug_log (dbg_log_file, "create_base('%s')\n", base ? base : "NULL");
#endif

	if (ld == NULL || base == NULL || *base == '\0')
	{
		return -1;
	}

	rdnList = ldap_explode_dn(base, 0);
	if (!rdnList)
	{
		char error[BIG_LINE];
		PR_snprintf(error, sizeof(error), "The given base suffix [%s] is not a valid DN", base);
		ds_send_error(error, 0);
		return -1;
	}

	if (PL_strncasecmp(rdnList[0], "o=", 2) == 0)
	{
		base_type = org;
	}
	else if (PL_strncasecmp(rdnList[0], "ou=", 3) == 0)
	{
		base_type = orgunit;
	}
	else if (PL_strncasecmp(rdnList[0], "dc=", 3) == 0)
	{
		base_type = domcomp;
	}
	else if (PL_strncasecmp(rdnList[0], "c=", 2) == 0)
	{
		base_type = country;
	}
	else if (PL_strncasecmp(rdnList[0], "st=", 3) == 0)
	{
		base_type = state;
	}
	else if (PL_strncasecmp(rdnList[0], "l=", 2) == 0)
	{
		base_type = locality;
	}
	else
	{
		ds_report_warning (DS_INCORRECT_USAGE, " Unable to create the root suffix.",
		   "In order to create the root suffix in the directory, you must "
		   "specify a distinguished name beginning with o=, ou=, dc=, c=, st=, or l=.  "
		   "If you wish to use something else for your root suffix, you "
		   "should first create the directory with one of these suffixes, then you can "
		   "create additional suffixes in any form you choose."
		);
		return -1;
	}

	ldap_value_free(rdnList);
	/*
	  We need to extract from the base the value to use for the attribute
	  name_attr e.g. ou: foo or o: org.
	  */
	rdnListNoTypes = ldap_explode_dn(base, 1);
	attr = rdnListNoTypes[0];

	if (!entry_exists(ld, base)) 
	{
		if (base_type == org)
		{
			ret = create_organization(ld, base, attr);
		}
		else if (base_type == orgunit)
		{
			/* this function is smart enough to extract the name from the DN */
			ret = create_organizational_unit(ld, 0, base, 0, 0, 0, 0);
		}
		else if (base_type == domcomp)
		{
			ret = create_domain_component(ld, base, attr);
		}
		else if (base_type == country)
		{
			ret = create_country(ld, base, attr);
		}
		else if (base_type == state)
		{
			ret = create_state(ld, base, attr);
		}
		else if (base_type == locality)
		{
			ret = create_locality(ld, base, attr);
		}
	}

	ldap_value_free(rdnListNoTypes);

	/* now add the anon search and self mod acis */
	if (!ret)
	{
		ret = add_aci(ld, base, ACI_anonymous_allow);
		if (!ret)
			ret = add_aci(ld, base, ACI_self_allow);
	}

	return ret;
}


#ifdef TEST_CONFIG
static int
create_configEntry(LDAP* ld)
{
/*
  dn: cn=config40
  objectclass: top
  objectclass: extensibleObject
  cn: config40
  */
	char *entrydn = NULL;
	int err;
	int ret = 0;

#ifdef CGI_DEBUG
	debug_log (dbg_log_file, "create_configEntry()\n");
#endif

	if (ld == NULL)
	{
		return -1;
	}

	entrydn = make_dn("%s=%s", name_cn, value_config40, NULLSTR);
	if (!entry_exists(ld, entrydn))
	{
		LDAPMod* attrs[3];
		LDAPMod attr[2];
		char* objectClasses[3];
		char* names[2];

		attrs[0] = &attr[0];
		attrs[2] = NULL;
		attr[0].mod_op = LDAP_MOD_ADD;
		attr[0].mod_type = name_objectClass;
		attr[0].mod_values = objectClasses;
		objectClasses[0] = class_top;
		objectClasses[1] = class_extensibleObject;
		objectClasses[2] = NULL;
		attrs[1] = &attr[1];
		attr[1].mod_op = LDAP_MOD_ADD;
		attr[1].mod_type = name_cn;
		attr[1].mod_values = names;
		names[0] = value_config40;
		names[1] = NULL;
	
		/*	fprintf (stdout, "ldap_add_s(%s)<br>\n", DN); fflush (stdout); */

		err = ldap_add_s (ld, entrydn, attrs);

		if (err != LDAP_SUCCESS) 
		{
			char* explanation = PR_smprintf("Unable to create %s."
											" (%s (%i) returned from ldap_add_s(%s))",
											value_config40, ldap_err2string (err), err, entrydn);
			ds_report_warning (DS_NETWORK_ERROR, " can't create config40",
							   explanation);
			PR_smprintf_free (explanation);
			ret = 1;
		}

	}

	if (entrydn)
		free(entrydn);

	return ret;
}
#endif

int
create_group(LDAP* ld, char* base, char* group)
{
	int err;
	int ret = 0;
	LDAPMod* attrs[3];
	LDAPMod attr[2];
	char* objectClasses[3];
	char* names[2];
	char *entrydn = 0;

#ifdef CGI_DEBUG
	debug_log (dbg_log_file, "create_group('%s','%s')\n",
			   base ? base : "NULL", group ? group : "NULL");
#endif

	if (ld == NULL || base == NULL || *base == '\0' ||
		group == NULL || *group == '\0')
	{
		return -1;
	}

	entrydn = make_dn("%s=%s, %s", name_cn, group, base, NULLSTR);

	if (!entry_exists(ld, entrydn)) 
	{
		attrs[0] = &attr[0];
		attrs[1] = &attr[1];
		attrs[2] = NULL;
		attr[0].mod_op = LDAP_MOD_ADD;
		attr[0].mod_type = name_objectClass;
		attr[0].mod_values = objectClasses;
		objectClasses[0] = class_top;
		objectClasses[1] = class_groupOfUniqueNames;
		objectClasses[2] = NULL;
		attr[1].mod_op = LDAP_MOD_ADD;
		attr[1].mod_type = name_cn;
		attr[1].mod_values = names;
		names[0] = group;
		names[1] = NULL;
		/*	fprintf (stdout, "ldap_add_s(%s)<br>\n", entrydn); fflush (stdout); */

		err = ldap_add_s (ld, entrydn, attrs);

		if (err != LDAP_SUCCESS) 
		{
			char* explanation = PR_smprintf("Unable to create group."
											" (%s (%i) returned from ldap_add_s(%s))",
											ldap_err2string (err), err, entrydn);
			ds_report_warning (DS_NETWORK_ERROR, " can't create group", explanation);
			PR_smprintf_free (explanation);
			ret = 1;
		}
	}

	if (entrydn)
		free(entrydn);

	return ret;
}

int
create_consumer_dn(LDAP* ld, char* dn, char* hashedpw)
{
	int err;
	int ret = 0;
	LDAPMod* attrs[7];
	LDAPMod attr[6];
	char* objectClasses[3];
	char* names[2];
	char* snames[2];
	char* desc[2];
	char* pwd[2];
	char* passwordExpirationTime[2];

#ifdef CGI_DEBUG
	debug_log (dbg_log_file, "create_consumer_dn('%s','%s')\n",
			   dn ? dn : "NULL", hashedpw ? hashedpw : "NULL");
#endif

	if (ld == NULL || dn == NULL || hashedpw == NULL)
	{
		return -1;
	}

	if (!entry_exists(ld, dn)) 
	{
		attrs[0] = &attr[0];
		attrs[1] = &attr[1];
		attrs[2] = &attr[2];
		attrs[3] = &attr[3];
		attrs[4] = &attr[4];
		attrs[5] = &attr[5];
		attrs[6] = NULL;

		attr[0].mod_op = LDAP_MOD_ADD;
		attr[0].mod_type = name_objectClass;
		attr[0].mod_values = objectClasses;
		objectClasses[0] = class_top;
		objectClasses[1] = class_person;
		objectClasses[2] = NULL;

		attr[1].mod_op = LDAP_MOD_ADD;
		attr[1].mod_type = name_cn;
		attr[1].mod_values = names;
		names[0] = "Replication Consumer";
		names[1] = NULL;

		attr[2].mod_op = LDAP_MOD_ADD;
		attr[2].mod_type = name_sn;
		attr[2].mod_values = snames;
		snames[0] = "Consumer";
		snames[1] = NULL;

		attr[3].mod_op = LDAP_MOD_ADD;
		attr[3].mod_type = name_description;
		attr[3].mod_values = desc;
		desc[0] = "Replication Consumer bind entity";
		desc[1] = NULL;

		attr[4].mod_op = LDAP_MOD_ADD;
		attr[4].mod_type = name_userPassword;
		attr[4].mod_values = pwd;
		pwd[0] = hashedpw;
		pwd[1] = NULL;

		attr[5].mod_op = LDAP_MOD_ADD;
		attr[5].mod_type = name_passwordExpirationTime;
		attr[5].mod_values = passwordExpirationTime;
		passwordExpirationTime[0] = "20380119031407Z";
		passwordExpirationTime[1] = NULL;

		/*	fprintf (stdout, "ldap_add_s(%s)<br>\n", DN); fflush (stdout); */

		err = ldap_add_s (ld, dn, attrs);

		if (err != LDAP_SUCCESS) 
		{
			char* explanation = PR_smprintf("Unable to create consumer dn."
											" (%s (%i) returned from ldap_add_s(%s))",
											ldap_err2string (err), err, dn);
			ds_report_warning (DS_NETWORK_ERROR, " can't create consumer dn", explanation);
			PR_smprintf_free (explanation);
			ret = 1;
		}
	}

	return ret;
}

static int
add_group_member(LDAP* ld, char* DN, char* attr, char* member)
{
	int err;
	int ret = 0;
	LDAPMod mod;
	LDAPMod* mods[2];
	char* members[2];

#ifdef CGI_DEBUG
	debug_log (dbg_log_file, "add_group_member('%s', '%s', '%s')\n",
			   DN ? DN : "NULL", 
			   attr ? attr : "NULL",
			   member ? member : "NULL");
#endif

	if (ld == NULL || DN == NULL || attr == NULL || member == NULL)
	{
		return -1;
	}

	mods[0] = &mod;
	mods[1] = NULL;
	mod.mod_op = LDAP_MOD_ADD;
	mod.mod_type = attr;
	mod.mod_values = members;
	members[0] = member;
	members[1] = NULL;
	/* fprintf (stdout, "ldap_modify_s('%s')<br>\n",DN); fflush (stdout); */
	err = ldap_modify_s (ld, DN, mods);
	if (err != LDAP_SUCCESS && err != LDAP_TYPE_OR_VALUE_EXISTS) {
		char* exp = "can't add member. ";
		char* explanation = PR_smprintf("%s (%i) returned from ldap_modify_s(%s, %i).",
										ldap_err2string (err), err, DN, LDAP_MOD_ADD);
		ds_report_warning (DS_INCORRECT_USAGE, exp, explanation);
		PR_smprintf_free (explanation);
		ret = 1;
	}

	return ret;
}

static LDAP*
do_bind(SLAPD_CONFIG* slapd, char* rootdn, char* rootpw)
{
	LDAP* connection = NULL;
	int retrymax = 1800;	/* wait up to 30 min; init dbcache could be slow. */
	int err = LDAP_SUCCESS;

	/* added error retry to work around the slow start introduced
	   by blackflag 624053 */
	while ( retrymax-- )
	{
		if (connection == NULL) {
			connection = ldap_open ("127.0.0.1", slapd->port);
		}

		if (connection) {
			err = ldap_simple_bind_s (connection, rootdn, rootpw ? rootpw : "");
			if (LDAP_SUCCESS == err)
				break;
		}

		PR_Sleep(PR_SecondsToInterval(1));
	}

	if (connection == NULL) {
		char* format = " Cannot connect to server.";
		ds_report_warning (DS_NETWORK_ERROR, format, "");
	} else if (err != LDAP_SUCCESS) {
		char* explanation = PR_smprintf("Unable to bind to server."
										" (%s (%i) returned from ldap_simple_bind_s(%s))",
										ldap_err2string (err), err, rootdn);
		ds_report_warning (DS_NETWORK_ERROR, " can't bind to server",
							   explanation);
		PR_smprintf_free (explanation);
		ldap_unbind (connection);
		connection = NULL;
	}
	fflush (stdout);
	return connection;
}

#ifdef TEST_CONFIG
int
config_configEntry(LDAP* connection, QUERY_VARS* query)
{
	/* initial ACIs for o=NetscapeRoot */

	int ret = add_aci_v (connection, value_config40DN, ACI_self_allow, NULLSTR);
	return ret;
}
#endif

int
config_suitespot(SLAPD_CONFIG* slapd, QUERY_VARS* query)
{
	LDAP* connection;
	const char* DN_formatUID = "uid=%s,%s";
	char* usageShortMsg = " Required field missing.";
	char* usageErrorMsg = NULL;
	int status = 0;
	char *admin_domainDN = 0;
	int ii = 0;
	char *configAdminDN = 0;
	char *adminGroupDN = 0;
	char *parentDN = 0;
	char *localDAGroupDN = 0;
	char realuid[1024] = {0};

	if (!query->rootDN || *query->rootDN == '\0') {
		usageErrorMsg = "You must enter the distinguished name of a user with "
		    "unrestricted access to the directory.";
	} else if (!query->rootPW || *query->rootPW == '\0') {
		usageErrorMsg = "You must enter the password of the user with "
		    "unrestricted access to the directory.";
	}

	if (usageErrorMsg) {
		ds_report_warning (DS_INCORRECT_USAGE, usageShortMsg, usageErrorMsg);
		return -1;
	}

	if (!(connection = do_bind (slapd, query->rootDN, query->rootPW)))
		return 1;

	if (query->suffix)
	{
		status = create_base(connection, query->suffix);
		if (!status)
		{
			if (configAdminDN && !is_root_user(configAdminDN, query)) {
				add_aci_v(connection, query->suffix, ACI_user_allow_2,
						  "all", configAdminDN, NULLSTR);
			}

			status = create_group(connection, query->suffix, name_localDAGroup);
		}
	}

	if (!status && query->consumerDN && query->consumerPW &&
		PL_strcasecmp(query->consumerDN, query->rootDN))
		status = create_consumer_dn(connection,
									query->consumerDN, query->consumerPW);

	if (!status)
	{
		if (query->suffix)
		{
			localDAGroupDN = make_dn("cn=%s, %s", name_localDAGroup,
 							   query->suffix, NULLSTR);
		}
		else 
		{
			localDAGroupDN = NULL;
		}	
        for (ii = 0; ii < entryAndAccessListSize; ++ii)
		{
			if (query->cfg_sspt && adminGroupDN) {
				add_aci_v(connection, entryAndAccessList[ii].entryDN,
						  ACI_config_admin_group_allow,
						  entryAndAccessList[ii].access,
						  adminGroupDN, NULLSTR);
			}
			if (configAdminDN && !is_root_user(configAdminDN, query)) {
				add_aci_v(connection, entryAndAccessList[ii].entryDN,
						  ACI_user_allow_2,
						  entryAndAccessList[ii].access,
						  configAdminDN, NULLSTR);
			}
			if (localDAGroupDN)
			{
				add_aci_v(connection, entryAndAccessList[ii].entryDN,
						  ACI_local_DA_allow,
						  entryAndAccessList[ii].access,
						  localDAGroupDN, NULLSTR);
			}
		}
	}

#ifdef TEST_CONFIG
	if (!status && query->testconfig)
		status = create_configEntry(connection);

	if (!status && query->testconfig)
		status = config_configEntry(connection, query);
#endif

	if (connection)
		ldap_unbind (connection);
	if (adminGroupDN)
		free(adminGroupDN);
	if (configAdminDN)
		free(configAdminDN);
	if (parentDN)
		free(parentDN);
	if (localDAGroupDN)
		free(localDAGroupDN);

	return status;
}

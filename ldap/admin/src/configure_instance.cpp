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
/*********************************************************************
**
**
** NAME:
**   configure_instance.cpp
**
** DESCRIPTION:
**   Fedora Directory Server Configuration Program
**
** NOTES:
** Derived from the original ux-config.cc
**
**
*********************************************************************/

#include <iostream.h>
#include <fstream.h>
#include <stdio.h>		/* printf, file I/O */
#include <string.h>		/* strlen */
#include <ctype.h>
#include <sys/stat.h>
#ifdef XP_UNIX
#include <strings.h>
#include <pwd.h>
#include <grp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#else
#include <io.h>
#endif
#include <stdlib.h>		/* memset, rand stuff */
#include <sys/types.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>

extern "C" {
#include "ldap.h"
#include "dsalib.h"
}

#include "nspr.h"
#include "plstr.h"

#include "setupapi.h"
#ifdef XP_UNIX
#include "ux-util.h"
#endif
#include "ldapu.h"
#include "install_keywords.h"
#include "create_instance.h"
#include "cfg_sspt.h"
#include "configure_instance.h"
#include "dirver.h"

#undef FILE_PATHSEP
#ifdef XP_WIN32
#define FILE_PATHSEP "\\"
#else
#define FILE_PATHSEP "/"
#endif

#ifdef XP_WIN32
#define DEFAULT_TASKCONF "bin\\slapd\\install\\ldif\\tasks.ldif"
#define ROLEDIT_EXTENSION "bin\\slapd\\install\\ldif\\roledit.ldif"
#define COMMON_TASKS "bin\\slapd\\install\\ldif\\commonTasks.ldif"
#define SAMPLE_LDIF "bin\\slapd\\install\\ldif\\Example.ldif"
#define TEMPLATE_LDIF "bin\\slapd\\install\\ldif\\template.ldif"
#else
#define DEFAULT_TASKCONF "bin/slapd/install/ldif/tasks.ldif"
#define ROLEDIT_EXTENSION "bin/slapd/install/ldif/roledit.ldif"
#define COMMON_TASKS "bin/slapd/install/ldif/commonTasks.ldif"
#define SAMPLE_LDIF "bin/slapd/install/ldif/Example.ldif"
#define TEMPLATE_LDIF "bin/slapd/install/ldif/template.ldif"
#endif

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

// location of java runtime relative to server root
#ifdef XP_WIN32
#define JAVA_RUNTIME "bin\\base\\jre\\bin\\jre"
#else
#define JAVA_RUNTIME "bin/base/jre/bin/jre"
#endif

// location of class files for java
#define JAVA_DIR "java"
// location of jar files relative to java dir
#define JARS_DIR "jars"
// full name of class with main() for running admin console
#define CONSOLE_CLASS_NAME "com.netscape.management.client.console.Console"
// name of script file to generate relative to slapd instance directory
#define SCRIPT_FILE_NAME "start-console"

#define DS_JAR_FILE_NAME "ds71.jar"
#define DS_CONSOLE_CLASS_NAME "com.netscape.admin.dirserv.DSAdmin"

#ifdef XP_WIN32
#define strtok_r(x,y,z) strtok(x,y)
#include "proto-ntutil.h"
#endif

#define SERVER_MIGRATION_CLASS "com.netscape.admin.dirserv.task.MigrateCreate"
#define SERVER_CREATION_CLASS "com.netscape.admin.dirserv.task.MigrateCreate"

static InstallMode installMode = Interactive;
static InstallInfo *installInfo = NULL;
static InstallInfo *slapdInfo = NULL;
static InstallInfo *slapdINFFileInfo = NULL;
static InstallInfo *adminInfo = NULL;
static const char *infoFile = NULL;
static const char *logFile = NULL;

static InstallLog    *installLog = NULL;
static int reconfig = 0; // set to 1 if we are reconfiguring
/*
 * iDSISolaris is set to 1 for Solaris 9+ specific installation.
 * This can be done by passing -S as the command line argument.
 */
int iDSISolaris = 0;

/*
 * There is currently a bug in LdapEntry->printEntry - it will crash if given a NULL argument
 * This is a workaround
 */
static void
my_printEntry(LdapEntry *ent, const char *filename, int which)
{
	ostream *os = NULL;
	if (filename && ent)
	{
		// just use LdapEntry, which should work given a good filename
		ent->printEntry(filename);
		return;
	}
	else if (which)
	{
		os = &cerr;
	}
	else
	{
		os = &cout;
	}

	if (!ent || !ent->entryDN() || ent->isEmpty())
	{
		*os << "Error: entry to print is empty" << endl;
	}
	else
	{
		*os << "dn: " << ent->entryDN() << endl;
		char **attrs = ent->getAttributeNames();
		for (int ii = 0; attrs && attrs[ii]; ++ii)
		{
			char **values = ent->getAttributes(attrs[ii]);
			for (int jj = 0; values && values[jj]; jj++)
			{
				*os << attrs[ii] << ": " << values[jj] << endl;
			}

			if (values)
			{
				ent->freeAttributes(values);
			}			
		}
		if (attrs)
		{
			ent->freeAttributeNames(attrs);
		}
	}
}

// changes empty strings ("") to NULLs (0)
static char *
my_strdup(const char *s)
{
	char *n = 0;
	if (s && *s)
	{
		n = new char[strlen(s) + 1];
		strcpy(n, s);
	}

	return n;
}

// changes empty strings ("") to NULLs (0)
static char *
my_c_strdup(const char *s)
{
	char *n = 0;
	if (s && *s)
	{
		n = (char *)malloc(strlen(s) + 1);
		strcpy(n, s);
	}

	return n;
}

static int
isAValidDN(const char *dn_to_test)
{
	int ret = 1;

	if (!dn_to_test || !*dn_to_test)
	{
		ret = 0;
	}
	else
	{
		char **rdnList = ldap_explode_dn(dn_to_test, 0);
		char **rdnNoTypes = ldap_explode_dn(dn_to_test, 1);
		if (!rdnList || !rdnList[0] || !rdnNoTypes || !rdnNoTypes[0] ||
			!strcasecmp(rdnList[0], rdnNoTypes[0]))
		{
			ret = 0;
		}
		if (rdnList)
			ldap_value_free(rdnList);
		if (rdnNoTypes)
			ldap_value_free(rdnNoTypes);
	}

	return ret;
}

static void
initMessageLog(const char *filename)
{
	if (filename && !installLog)
	{
		logFile = my_c_strdup(filename);
#ifdef XP_UNIX
		if (!logFile && installMode != Silent)
		{
			logFile = "/dev/tty";
		}
#endif
		installLog = new InstallLog(logFile);
	}
}

static void
dsLogMessage(const char *level, const char *which,
			 const char *format, ...)
{
	char bigbuf[BIG_BUF*4];
	va_list ap;
	va_start(ap, format);
	PR_vsnprintf(bigbuf, BIG_BUF*4, format, ap);
	va_end(ap);
#ifdef _WIN32 // always output to stdout (for CGIs), and always log
	// if a log is available
	fprintf(stdout, "%s %s %s\n", level, which, bigbuf);
	fflush(stdout);
	if (installLog)
		installLog->logMessage(level, which, bigbuf);
#else // not Windows
	if (installMode == Interactive)
	{
		fprintf(stdout, "%s %s %s\n", level, which, bigbuf);
		fflush(stdout);
	}
	else
	{
		if (installLog)
			installLog->logMessage(level, which, bigbuf);
		else
			fprintf(stdout, "%s %s %s\n", level, which, bigbuf);
		fflush(stdout);
	}
#endif

	return;
}

static char *
getGMT()
{
	static char buf[20];
	time_t curtime;
	struct tm ltm;
 
	curtime = time( (time_t *)0 );
#ifdef _WIN32
	ltm = *gmtime( &curtime );
#else
	gmtime_r( &curtime, &ltm );
#endif
	strftime( buf, sizeof(buf), "%Y%m%d%H%M%SZ", &ltm );
	return buf;
}

static void
normalizeDNs()
{
	static const char *DN_VALUED_ATTRS[] = {
		SLAPD_KEY_SUFFIX,
		SLAPD_KEY_ROOTDN,
		SLAPD_KEY_REPLICATIONDN,
		SLAPD_KEY_CONSUMERDN,
		SLAPD_KEY_SIR_SUFFIX,
		SLAPD_KEY_SIR_BINDDN
	};
	static const int N = sizeof(DN_VALUED_ATTRS)/sizeof(DN_VALUED_ATTRS[0]);
	static const char *URL_ATTRS[] = {
		SLAPD_KEY_K_LDAP_URL,
		SLAPD_KEY_USER_GROUP_LDAP_URL
	};
	static const int NURLS = sizeof(URL_ATTRS)/sizeof(URL_ATTRS[0]);

	int ii;
	for (ii = 0; slapdInfo && (ii < N); ++ii)
	{
		const char *attr = DN_VALUED_ATTRS[ii];
		char *dn = my_strdup(slapdInfo->get(attr));
		if (dn)
		{
			slapdInfo->set(attr, dn_normalize_convert(dn));
			delete [] dn;
		}
	}

	for (ii = 0; installInfo && (ii < NURLS); ++ii)
	{
		const char *attr = URL_ATTRS[ii];
		const char *url = installInfo->get(attr);
		LDAPURLDesc *desc = 0;
		if (url && !ldap_url_parse((char *)url, &desc) && desc)
		{
			char *dn = dn_normalize_convert(my_strdup(desc->lud_dn));
			int isSSL = !strncmp(url, "ldaps:", strlen("ldaps:"));
			if (dn)
			{
				char port[6];
				PR_snprintf(port, sizeof(port), "%d", desc->lud_port);
				NSString newurl = NSString("ldap") +
					(isSSL ? "s" : "") +
					"://" + desc->lud_host +
					":" + port + "/" + dn;
				installInfo->set(attr, newurl);
				delete [] dn;
			}
		}
		if (desc)
			ldap_free_urldesc(desc);
	}
}
		

static int
featureIsEnabled(const char *s)
{
	if (!s || !*s || !strncasecmp(s, "no", strlen(s)))
		return 0; // feature is disabled

	return 1; // feature is enabled
}

static LdapErrorCode
add_sample_entries(const char *sroot, LdapEntry *ldapEntry)
{
   char tmp[MED_BUF];

   if (sroot)
	   PR_snprintf(tmp, MED_BUF, "%s%s%s", sroot, FILE_PATHSEP, SAMPLE_LDIF);
   else
	   strcpy(tmp, "test.ldif");
   
   return insertLdifEntries(ldapEntry->ldap(), NULL, tmp, NULL);

}


// in the given string s, replace all occurrances of token with replace
// the string return is allocated with new char []
static char *
replace_token(const char *s, const char *token, int tokenlen,
			  const char *replace, int replacelen)
{
	char *ptr = (char*)strstr(s, token);
	char *n = 0;
	if (!ptr)
	{
		n = my_strdup(s);
		return n;
	}

	// count the number of occurances of the token
	int ntokens = 1;
	while (ptr && *ptr)
	{
		ptr = (char*)strstr(ptr+1, token);
		++ntokens;
	}

	n = new char [strlen(s) + (ntokens * replacelen)];
	char *d = n;
	const char *begin = s;
	for (ptr = (char*)strstr(s, token); ptr && *ptr;)
	{
		int len = int(ptr - begin);
		strncpy(d, begin, len);
		d += len;
		begin = ptr + tokenlen;
		len = replacelen;
		strncpy(d, replace, len);
		d += len;
		ptr = strstr(ptr+1, token);
	}
	// no more occurances of token in string; copy the rest
	for (ptr = (char *)begin; ptr && *ptr; LDAP_UTF8INC(ptr))
	{
		*d = *ptr;
		LDAP_UTF8INC(d);
	}
	*d = 0;

	return n;
}

static void
add_org_entries(const char *sroot, LdapEntry *ldapEntry,
				const char *initialLdifFile, const char *org_size,
				NSString sieDN)
{
   org_size = org_size;

   char tmp[MED_BUF];
   char *dn;

   LdapError ldapError;
   char **vals;
   static const char *TOKEN[] = {
   	   "%%%SUFFIX%%%",
	   "%%%ORG%%%",
	   "%%%CONFIG_ADMIN_DN%%%"
   };
   static const int TOKENLEN[] = { 12, 9, 21 };
   static const int NTOKENS = 3;
   static const char *REPLACE[] = { 0, 0, 0 };
   static int REPLACELEN[] = { 0, 0, 0 };

   REPLACE[0] = slapdInfo->get(SLAPD_KEY_SUFFIX);
   const char *org = strchr(REPLACE[0], '=');
   if (org)
	   REPLACE[1] = org+1;

   REPLACE[2] = slapdInfo->get(SLAPD_KEY_CONFIG_ADMIN_DN);
   for (int ii = 0; ii < NTOKENS; ++ii)
   {
	   if (REPLACE[ii])
		   REPLACELEN[ii] = strlen(REPLACE[ii]);
   }

   if (sroot)
   {
	   if (!initialLdifFile || !*initialLdifFile ||
		   !strncasecmp(initialLdifFile, "suggest", strlen(initialLdifFile)))
		   PR_snprintf(tmp, sizeof(tmp), "%s%s%s", sroot, FILE_PATHSEP, TEMPLATE_LDIF);
	   else
		   PL_strncpyz(tmp, initialLdifFile, sizeof(tmp));
   }
   else
	   PL_strncpyz(tmp, "test.ldif", sizeof(tmp));
   
   LdifEntry ldif(tmp);
   
   if (!ldif.isValid() || ldif.nextEntry() == -1)
   {
	   dsLogMessage(SETUP_LOG_WARN, "Slapd", "File %s\ndoes not"
					" appear to be a valid LDIF file.", tmp);
	   return;
   }

   int entry_num = 0;

   do
   {
	  entry_num++;
	  if (ldapEntry)
		  ldapEntry->clear();

      for (int i = 0; i < ldif.numList(); i++)
      {
         const char *name = ldif.list(i);
		 if (!name || !*name)
			 continue;

         vals = ldif.getListItems(name);
		 if (!vals || !*vals)
			 continue;

		 int n = ldif.numListItems(name);
		 if (!n)
			 continue;

		 char **newvals = new char* [n+1];
		 newvals[n] = 0; // null terminated
		 // go through the values replacing the token string with the value
		 for (int iii = 0; iii < n; ++iii)
		 {
			 newvals[iii] = my_strdup(vals[iii]);
			 for (int jj = 0; jj < NTOKENS; ++jj)
			 {
				 char *oldnewvals = newvals[iii];
				 newvals[iii] = replace_token(newvals[iii], TOKEN[jj], TOKENLEN[jj],
											  REPLACE[jj], REPLACELEN[jj]);
				 delete [] oldnewvals;
			 }
		 }

		 if (!strcasecmp(name, "dn"))
		 {
			 dn = my_strdup(newvals[0]);
		 }
		 else if (ldapEntry)
		 {
			 ldapEntry->addAttributes(name, (const char **) newvals);
		 }
		 else /* this is for debugging only */
		 {
			 cerr << "name = " << name << " dn = " << dn << endl;
			 for (int jj = 0; jj < n; ++jj)
			 {
				 cerr << "old entry[" << jj << "] = " << vals[jj] << endl;
				 cerr << "new entry[" << jj << "] = " << newvals[jj] << endl;
			 }
			 cerr << "####" << endl;
		 }
         ldif.freeListItems(vals);
		 for (int jj = 0; jj < n; ++jj)
			 delete [] newvals[jj];
		 delete [] newvals;
      }

	  if (!ldapEntry)
		  continue;

	  if (!dn || !*dn)
	  {
		  dsLogMessage(SETUP_LOG_WARN, "Slapd", "Entry number %d in file %s\ndoes not"
					   " contain a valid dn: attribute.\nThe file may be"
					   " corrupted or not in valid LDIF format.",
					   entry_num, tmp);
		  continue;
	  }

	  if (entry_num == 1)
	  {
		  NSString aci = NSString(
			  "(targetattr = \"*\")(version 3.0; "
			  "acl \"SIE Group\"; allow (all)"
			  "groupdn = \"ldap:///") + sieDN + "\";)";
		  // add the aci for the SIE group
		  ldapEntry->addAttribute("aci", aci);
	  }

      if (ldapEntry->exists(dn) == False)
      {
         ldapError = ldapEntry->insert(dn);
      }
      else
      {
         ldapError = ldapEntry->update(dn);
      }

      if (ldapError != OKAY)
      {
         PR_snprintf(tmp, sizeof(tmp), "%d", ldapError.errorCode());
		 dsLogMessage(SETUP_LOG_WARN, "Slapd", "Could not write entry %s (%s:%s)", dn, tmp, ldapError.msg());
      }
	  delete [] dn;
   } while (ldif.nextEntry() != -1);
}

// dsSIEDN will be something like:
// cn=slapd-foo, cn=NDS, cn=SS4.0, cn=FQDN, ou=admindomain, o=netscaperoot
static void
getAdminSIEDN(const char *dsSIEDN, const char *hostname, NSString& adminSIEDN)
{
	char *editablehostname = my_strdup(hostname);
	char *eptr = strchr(editablehostname, '.');
	if (eptr)
		*eptr = 0;

	char **rdnList = ldap_explode_dn(dsSIEDN, 0);
	char *baseDN = 0;
	if (rdnList && rdnList[0] && rdnList[1] && rdnList[2]) // dsSIEDN is a valid DN
	{
		int len = 0;
		int ii;
		for (ii = 2; rdnList[ii]; ++ii)
			len += strlen(rdnList[ii]) + 3;

		baseDN = (char *)malloc(len+1);
		baseDN[0] = 0;
		for (ii = 2; rdnList[ii]; ++ii)
		{
			if (ii > 2)
				strcat(baseDN, ", ");
			strcat(baseDN, rdnList[ii]);
		}
	}
	else
	{
		baseDN = my_c_strdup(dsSIEDN);
	}

	if (rdnList)
		ldap_value_free(rdnList);

	adminSIEDN = NSString("cn=admin-serv-") + editablehostname +
		", cn=Fedora Administration Server, " + baseDN;

	delete [] editablehostname;
	free(baseDN);

	return;
}

static void
setAppEntryInformation(LdapEntry *appEntry)
{
	// required attributes
	if (!appEntry->getAttribute("objectclass"))
		appEntry->addAttribute("objectclass", "nsApplication");
	appEntry->setAttribute("cn", slapdINFFileInfo->get("Name"));
	appEntry->setAttribute("nsProductname", slapdINFFileInfo->get("Name"));
	appEntry->setAttribute("nsProductversion", PRODUCTTEXT);
	// optional attributes
/*
	NSString temp = slapdINFFileInfo->get("Description");
	if ((NSString)NULL != temp)
		appEntry->setAttribute("description", temp);
*/
	NSString temp = slapdINFFileInfo->get("NickName");
	if ((NSString)NULL == temp)
		temp = "slapd";
	appEntry->setAttribute("nsNickName", temp);
	temp = slapdINFFileInfo->get("BuildNumber");
	if ((NSString)NULL != temp)
		appEntry->setAttribute("nsBuildNumber", temp);
	temp = slapdINFFileInfo->get("Revision");
	if ((NSString)NULL != temp)
		appEntry->setAttribute("nsRevisionNumber", temp);
	temp = slapdINFFileInfo->get("SerialNumber");
	if ((NSString)NULL != temp)
		appEntry->setAttribute("nsSerialNumber", temp);
	temp = slapdINFFileInfo->get("Vendor");
	if ((NSString)NULL != temp)
		appEntry->setAttribute("nsVendor", temp);

	if (!appEntry->getAttribute("nsInstalledLocation"))
		appEntry->addAttribute("nsInstalledLocation",
							   installInfo->get(SLAPD_KEY_SERVER_ROOT));
	appEntry->setAttribute("installationTimeStamp", getGMT());
	temp = slapdINFFileInfo->get("Expires");
	if ((NSString)NULL != temp)
		appEntry->setAttribute("nsExpirationDate", temp);
	temp = slapdINFFileInfo->get("Security");
	if ((NSString)NULL != temp)
		appEntry->setAttribute("nsBuildSecurity", temp);

	return;
}

static LdapError
create_sie_and_isie(LdapEntry *sieEntry, LdapEntry *appEntry, NSString& sieDN)
{
	LdapError ldapError; // return value

	// Prepare sieEntry
	sieEntry->clear();

	sieEntry->addAttribute("objectclass", "netscapeServer");
	sieEntry->addAttribute("objectclass", "nsDirectoryServer");
	sieEntry->addAttribute("objectclass", "nsResourceRef");
	sieEntry->addAttribute("objectclass", "nsConfig");
	sieEntry->addAttribute("nsServerSecurity", "off");
	NSString serverID = NSString("slapd-") + slapdInfo->get(SLAPD_KEY_SERVER_IDENTIFIER);
	sieEntry->addAttribute("nsServerID", serverID);
	sieEntry->addAttribute("nsBindDN", slapdInfo->get(SLAPD_KEY_ROOTDN));
	sieEntry->addAttribute("nsBaseDN", slapdInfo->get(SLAPD_KEY_SUFFIX));
	char *hashedPwd = (char *)ds_salted_sha1_pw_enc (
		(char *)slapdInfo->get(SLAPD_KEY_ROOTDNPWD));
	if (hashedPwd)
		sieEntry->addAttribute("userPassword", hashedPwd);
//	sieEntry->addAttribute("AuthenticationPassword", slapdInfo->get(SLAPD_KEY_ROOTDNPWD));
	sieEntry->addAttribute("serverHostName", installInfo->get(SLAPD_KEY_FULL_MACHINE_NAME));
	sieEntry->addAttribute("serverRoot", installInfo->get(SLAPD_KEY_SERVER_ROOT));
	sieEntry->addAttribute("nsServerPort", slapdInfo->get(SLAPD_KEY_SERVER_PORT));
	sieEntry->addAttribute("nsSecureServerPort", "636");
/*
	NSString temp = slapdINFFileInfo->get("Description");
	if ((NSString)NULL != temp)
		sieEntry->addAttribute("description", temp);
*/
	NSString name = NSString(slapdINFFileInfo->get("InstanceNamePrefix")) + " (" +
		slapdInfo->get(SLAPD_KEY_SERVER_IDENTIFIER) + ")";
	sieEntry->addAttribute("serverProductName", name);
	sieEntry->addAttribute("serverVersionNumber", slapdINFFileInfo->get("Version"));
	sieEntry->addAttribute("installationTimeStamp", getGMT());
	NSString temp = installInfo->get(SLAPD_KEY_SUITESPOT_USERID);
	if ((NSString)NULL != temp) // may not be present on NT . . .
		sieEntry->addAttribute("nsSuiteSpotUser", temp);

	// Prepare appEntry
	appEntry->clear();
	setAppEntryInformation(appEntry);

	NSString ssDN = installInfo->get(SLAPD_KEY_ADMIN_DOMAIN);

	// to make a disposable copy
	char *fqdn = my_strdup(installInfo->get(SLAPD_KEY_FULL_MACHINE_NAME));

	LdapErrorCode code = createSIE(sieEntry, appEntry, fqdn,
								   installInfo->get(SLAPD_KEY_SERVER_ROOT),
								   ssDN);
	delete [] fqdn;

	if (code != OKAY)
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd",
					 "createSIE returned error code %d for ssDN=%s machinename=%s "
					 "server root=%s", (int)code, (const char *)ssDN,
					 installInfo->get(SLAPD_KEY_FULL_MACHINE_NAME),
					 installInfo->get(SLAPD_KEY_SERVER_ROOT));
#ifdef XP_UNIX
		cerr << "Here is the sieEntry:" << endl;
		my_printEntry(sieEntry, 0, 1); // output to cerr
#else
		dsLogMessage(SETUP_LOG_FATAL, "Slapd", "SIE entry printed to c:/temp/SIE.out");
		sieEntry->printEntry("c:/temp/SIE.out");
#endif
#ifdef XP_UNIX
		cerr << "Here is the appEntry:" << endl;
		my_printEntry(appEntry, 0, 1); // output to cerr
#else
		dsLogMessage(SETUP_LOG_FATAL, "Slapd", "APP entry printed to c:/temp/APP.out");
		appEntry->printEntry("c:/temp/APP.out");
#endif
		return code;
	}

//	dsLogMessage("Info", "Slapd", "Created configuration entry for server %s",
//				 (const char *)serverID);

	sieDN = sieEntry->entryDN();

	NSString configDN, configTaskDN, opTaskDN, adminSIEDN;
	getAdminSIEDN(sieDN, installInfo->get(SLAPD_KEY_FULL_MACHINE_NAME),
				  adminSIEDN);

	// append the adminSIE to the create and migrate class names
	appEntry->clear();
	NSString classname = NSString(SERVER_MIGRATION_CLASS"@"DS_JAR_FILE_NAME"@") +
		adminSIEDN;
	appEntry->addAttribute("nsServerMigrationClassname", classname);
	classname = NSString(SERVER_CREATION_CLASS"@"DS_JAR_FILE_NAME"@") +
		adminSIEDN;
	appEntry->addAttribute("nsServerCreationClassname", classname);
	if ((ldapError = appEntry->update(appEntry->entryDN())) != OKAY)
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd", "Error: Could not modify nsServerMigrationClassname "
					 "and/or nsServerCreationClassname in entry %s: error code %d\n",
					appEntry->entryDN(), (int)ldapError);
		return (int)ldapError;
	}

	// Write configuration parameters (see ns-admin.conf)
	sieEntry->clear();

	sieEntry->addAttribute("objectclass", "nsResourceRef");
	sieEntry->addAttribute("objectclass", "nsAdminObject");
	sieEntry->addAttribute("objectclass", "nsDirectoryInfo");

	/*
	 * Mandatory fields here
	 */
	NSString description = NSString("Configuration information for directory server ") +
		serverID;
	sieEntry->addAttribute ("cn", "configuration");
	NSString nsclassname = NSString(DS_CONSOLE_CLASS_NAME) + "@" +
		DS_JAR_FILE_NAME + "@" + adminSIEDN;
	sieEntry->addAttribute ("nsclassname", nsclassname);
	sieEntry->addAttribute ("nsjarfilename",
							DS_JAR_FILE_NAME);
	char** rdnList = ldap_explode_dn(appEntry->entryDN(), 0);
	if (rdnList)
	{
		int ii = 0;
		int len = 0;
		for (ii = 1; rdnList[ii]; ++ii) // skip first rdn
			len += (strlen(rdnList[ii]) + 3);
		char *adminGroupDN = (char *)calloc(1, len);
		for (ii = 1; rdnList[ii]; ++ii) {
			if (ii > 1)
				strcat(adminGroupDN, ", ");
			strcat(adminGroupDN, rdnList[ii]);
		}
		ldap_value_free(rdnList);
		sieEntry->addAttribute("nsDirectoryInfoRef", adminGroupDN);
		free(adminGroupDN);
	}

	configDN = NSString("cn=configuration") + "," + sieDN;

	// allow modification by kingpin topology
	createACIForConfigEntry(sieEntry, sieDN);

	if (sieEntry->exists(configDN) == False)
	{
		ldapError = sieEntry->insert(configDN);
	}
	else
	{
		ldapError = sieEntry->update(configDN);
	}

	if (ldapError != OKAY)
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd",
					 "Could not update the configuration entry %s: error code %d",
					 (const char *)configDN, ldapError.errorCode());
		return (int)ldapError;
	} else {
//		dsLogMessage("Info", "Slapd", "Updated configuration entry for server %s",
//					 (const char *)serverID);
	}

	// Write Tasks nodes
	installInfo->toLocal(SLAPD_KEY_SERVER_ROOT); // path needs local encoding
	NSString filename = NSString(installInfo->get(SLAPD_KEY_SERVER_ROOT)) +
		FILE_PATHSEP + DEFAULT_TASKCONF;
	ldapError = insertLdifEntries(sieEntry->ldap(), sieDN, filename,
								  adminSIEDN);
	installInfo->toUTF8(SLAPD_KEY_SERVER_ROOT); // back to utf8
	
	if (ldapError != OKAY)
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd",
					 "Could not update the instance specific tasks entry %s: error code %d",
					 (const char *)sieDN, ldapError.errorCode());
		return ldapError;
	} else {
//		dsLogMessage("Info", "Slapd", "Added task information for server %s",
//					 (const char *)serverID);
	}

	installInfo->toLocal(SLAPD_KEY_SERVER_ROOT); // path needs local encoding
	filename = NSString(installInfo->get(SLAPD_KEY_SERVER_ROOT)) +
		FILE_PATHSEP + COMMON_TASKS;
	ldapError = insertLdifEntries(sieEntry->ldap(), appEntry->entryDN(),
								  filename, adminSIEDN);
	installInfo->toUTF8(SLAPD_KEY_SERVER_ROOT); // back to utf8

	if (ldapError != OKAY)
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd",
					 "Could not update the general tasks entry %s: error code %d",
					 (const char *)appEntry->entryDN(), ldapError.errorCode());
	} else {
//		dsLogMessage("Info", "Slapd", "Updated common task information for server %s",
//					 (const char *)serverID);
	}

	return ldapError;
}


static LdapErrorCode
create_roledit_extension(Ldap* ldap)
{
	//
	// If needed, we create "ou=Global Preferences,ou=<domain>,o=NetscapeRoot".
	// The following code has been duplicated from setupGlobalPreferences() in
	// setupldap.cpp
	//
	LdapEntry ldapEntry(ldap);
	LdapError err;
	NSString globalPref = DEFAULT_GLOBAL_PREFS_RDN;
	NSString adminDomain = installInfo->get(SLAPD_KEY_ADMIN_DOMAIN);
	char * domain = setupFormAdminDomainDN(adminDomain);

	globalPref = globalPref + LDAP_PATHSEP + domain;

//	dsLogMessage("Info", "Slapd", "Beginning update console role editor extensions");
	if (ldapEntry.retrieve(globalPref) != OKAY)
	{
		ldapEntry.setAttribute("objectclass", DEFAULT_GLOBAL_PREFS_OBJECT);
		ldapEntry.setAttribute("ou", DEFAULT_GLOBAL_PREFS);
		ldapEntry.setAttribute("aci", DEFAULT_GLOBAL_PREFS_ACI);
		ldapEntry.setAttribute("description", "Default branch for Fedora Server Products Global Preferences");
//		dsLogMessage("Info", "Slapd", "Updating global preferences for console role editor extensions");
		err = ldapEntry.insert(globalPref);
	}
	else
	{
		ldapEntry.setAttribute("aci", DEFAULT_GLOBAL_PREFS_ACI);
		ldapEntry.setAttribute("description", "Default branch for Fedora Server Products Global Preferences");
//		dsLogMessage("Info", "Slapd", "Updating global preferences for console role editor extensions");
		err = ldapEntry.replace(globalPref);
	}

	if (err == OKAY) {
//		dsLogMessage("Info", "Slapd", "Updated global console preferences for role editor extensions");
	}

	//
	// Now let try to add the AdminResourceExtension entries.
	// They are defined in the LDIF file named ROLEDIT_EXTENSION.
	//
	if (err == OKAY)
	{
//		dsLogMessage("Info", "Slapd", "Updating console role editor extensions");

		installInfo->toLocal(SLAPD_KEY_SERVER_ROOT); // path needs local encoding
		NSString filename = NSString(installInfo->get(SLAPD_KEY_SERVER_ROOT)) +
			FILE_PATHSEP + ROLEDIT_EXTENSION;
		err = insertLdifEntries(ldap, domain, filename, NULL);
		installInfo->toUTF8(SLAPD_KEY_SERVER_ROOT); // back to utf8
	}

	if(domain) free(domain);

	if (err.errorCode() == OKAY) {
//		dsLogMessage("Info", "Slapd", "Updated console role editor extensions");
	}

	return err.errorCode();
}


static int
create_ss_dir_tree(const char *hostname, NSString &sieDN)
{
	int status = 0;

	LdapError ldapError = OKAY;
	NSString adminID = installInfo->get(SLAPD_KEY_SERVER_ADMIN_ID);
	NSString adminPwd = installInfo->get(SLAPD_KEY_SERVER_ADMIN_PWD);
	Ldap ldap (ldapError, installInfo->get(SLAPD_KEY_K_LDAP_URL),
			   adminID, adminPwd, 0, 0);

	if (ldapError != OKAY)
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd",
					 "ERROR: Ldap authentication failed for url %s user id %s (%d:%s)" ,
					installInfo->get(SLAPD_KEY_K_LDAP_URL), adminID.data(),
					ldapError.errorCode(), ldapError.msg()); 
		return ldapError.errorCode();
	}

	LdapEntry *sieEntry = new LdapEntry(&ldap);
	LdapEntry *appEntry = new LdapEntry(&ldap);

	LdapErrorCode code = create_sie_and_isie(sieEntry, appEntry, sieDN);

	if (code != OKAY)
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd",
					 "ERROR: failed to register Directory server as a Fedora server (%d)",
					code); 
		return code;
	}

	code = create_roledit_extension(&ldap);

	if (code != OKAY)
	{
		dsLogMessage(SETUP_LOG_WARN, "Slapd",
					 "WARNING: failed to add extensions for role edition (%d)",
					 code); 
		code = OKAY; // We can continue anyway
	}

	const char *user_ldap_url = installInfo->get(SLAPD_KEY_USER_GROUP_LDAP_URL);
	if (!user_ldap_url)
		user_ldap_url = installInfo->get(SLAPD_KEY_K_LDAP_URL);

	code = addGlobalUserDirectory(&ldap,
								  installInfo->get(SLAPD_KEY_ADMIN_DOMAIN),
								  user_ldap_url, 0, 0);

	if (code != OKAY)
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd",
					 "ERROR: failed to add Global User Directory (%d)",
					 code); 
		return code;
	}

	// we need to add some ACIs which will allow the SIE group to have
	// admin access to the newly created directory
	Ldap *new_ldap = 0;
	int port = atoi(slapdInfo->get(SLAPD_KEY_SERVER_PORT));
	if (strcasecmp(ldap.host(), hostname) || ldap.port() != port)
	{
		const char *suffix = 0;
		if (featureIsEnabled(slapdInfo->get(SLAPD_KEY_USE_EXISTING_UG)))
			suffix = DEFAULT_ROOT_DN;
		else
			suffix = slapdInfo->get(SLAPD_KEY_SUFFIX);
		NSString new_url = NSString("ldap://") +
			hostname + ":" + slapdInfo->get(SLAPD_KEY_SERVER_PORT) +
			"/" + suffix;
		const char *userDN;
		const char *userPwd;
		if (!(userDN = slapdInfo->get(SLAPD_KEY_ROOTDN)))
			userDN = ldap.userDN();
		if (!(userPwd = slapdInfo->get(SLAPD_KEY_ROOTDNPWD)))
			userPwd = ldap.userPassword();
		new_ldap = new Ldap(ldapError, new_url, userDN, userPwd,
							userDN, userPwd);
		if (ldapError != OKAY)
		{
			dsLogMessage(SETUP_LOG_WARN, "Slapd",
						 "Could not open the new directory server [%s:%s] to add an aci [%d].",
						 (const char *)new_url, userDN, ldapError.errorCode());
			delete new_ldap;
			new_ldap = 0;
		}
	}
	else
		new_ldap = &ldap;

	if (new_ldap)
	{
		const char *entry = 0;
		const char *access = 0;
		LdapEntry ent(new_ldap);
		int ii = 0;
		while (getEntryAndAccess(ii, &entry, &access))
		{
			++ii;
			NSString aci = NSString(
				"(targetattr = \"*\")(version 3.0; "
				"acl \"SIE Group\"; allow (") + access + ")"
				"groupdn = \"ldap:///" + sieDN + "\";)";
			ent.clear();
			ent.addAttribute("aci", aci);
			ldapError = ent.update(entry);
			if (ldapError != OKAY)
				dsLogMessage(SETUP_LOG_WARN, "Slapd",
							 "Could not add aci %s to entry %s [%d].",
							 (const char *)aci, entry, ldapError.errorCode());
		}
	}

	if (new_ldap && new_ldap != &ldap)
		delete new_ldap;

	destroyLdapEntry(sieEntry);
	destroyLdapEntry(appEntry);

	if (status == OKAY) {
//		dsLogMessage("Info", "Slapd", "Updated console administration access controls");
	}

	return status;
}

static void
create_console_script()
{
#if 0 // does not work right now
#ifdef XP_UNIX
	const char *sroot = installInfo->get(SLAPD_KEY_SERVER_ROOT);
	const char *sid = slapdInfo->get(SLAPD_KEY_SERVER_IDENTIFIER);
	const char *hn = installInfo->get(SLAPD_KEY_FULL_MACHINE_NAME);
	const char *port = slapdInfo->get(SLAPD_KEY_SERVER_PORT);
	const char *suf = slapdInfo->get(SLAPD_KEY_SUFFIX);
	const char *classpathSeparator = ":";

	NSString scriptFilename = NSString(sroot) + FILE_PATHSEP + "slapd-" +
		sid + FILE_PATHSEP + SCRIPT_FILE_NAME;
	ofstream ofs(scriptFilename);
	if (!ofs)
		return;

	ofs << "#!/bin/sh" << endl;
	ofs << "#" << endl;
	ofs << "# This script will invoke the Fedora Management Console" << endl;
	ofs << "#" << endl;
	// see if there are any other .jar or .zip files in the java directory
	// and add them to our class path too
	ofs << "for file in " << sroot << FILE_PATHSEP << JAVA_DIR << FILE_PATHSEP
		<< JARS_DIR << FILE_PATHSEP << "*.jar ; do" << endl;
	ofs << "\tCLASSPATH=${CLASSPATH}" << classpathSeparator << "$file" << endl;
	ofs << "done" << endl;

	ofs << "for file in " << sroot << FILE_PATHSEP << JAVA_DIR << FILE_PATHSEP
		<< "*.jar ; do" << endl;
	ofs << "\tCLASSPATH=${CLASSPATH}" << classpathSeparator << "$file" << endl;
	ofs << "done" << endl;

	ofs << "for file in " << sroot << FILE_PATHSEP << JAVA_DIR << FILE_PATHSEP
		<< "*.zip ; do" << endl;
	ofs << "\tCLASSPATH=${CLASSPATH}" << classpathSeparator << "$file" << endl;
	ofs << "done" << endl;

	ofs << "export CLASSPATH" << endl;

	// go to the java dir
	ofs << "cd " << sroot << FILE_PATHSEP << JAVA_DIR << endl;
	// now, invoke the java runtime environment
	ofs << sroot << FILE_PATHSEP << JAVA_RUNTIME
		<< " -classpath \"$CLASSPATH\" "
		<< CONSOLE_CLASS_NAME << " -d " << hn << " -p " << port << " -b "
		<< "\"" << suf << "\"" << endl;

	ofs.flush();
	ofs.close();

	chmod(scriptFilename, 0755);
#endif
#endif // if 0

	return;
}

// check the install info read in to see if we have valid data
static int
info_is_valid()
{
	static const char *requiredFields[] = {
		SLAPD_KEY_FULL_MACHINE_NAME,
		SLAPD_KEY_SERVER_ROOT,
		SLAPD_KEY_SERVER_IDENTIFIER,
		SLAPD_KEY_SERVER_PORT,
		SLAPD_KEY_ROOTDN,
		SLAPD_KEY_ROOTDNPWD,
		SLAPD_KEY_K_LDAP_URL,
		SLAPD_KEY_SUFFIX,
		SLAPD_KEY_SERVER_ADMIN_ID,
		SLAPD_KEY_SERVER_ADMIN_PWD,
		SLAPD_KEY_ADMIN_DOMAIN
	};
	static int numRequiredFields = sizeof(requiredFields) / sizeof(requiredFields[0]);

	if (!installInfo || !slapdInfo)
		return 0;

	for (int ii = 0; ii < numRequiredFields; ++ii)
	{
		const char *val = installInfo->get(requiredFields[ii]);
		if (val && *val)
			continue;
		val = slapdInfo->get(requiredFields[ii]);
		if (val && *val)
			continue;

		// if we got here, the value was not found in either the install info or
		// the slapd info
		dsLogMessage(SETUP_LOG_FATAL, "Slapd",
					 "The required field %s is not present in the install info file.",
					 requiredFields[ii]);
		return 0;
	}

	return 1;
}

static int
parse_commandline(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "rsSl:f:")) != EOF)
	{
		switch (opt)
		{
	    case 'r':
			reconfig = 1;
			break;
	    case 's':
			installMode = Silent;
			break;
	    case 'S':
			/*
			 * Solaris 9+ specific installation
			 */
                        iDSISolaris = 1;
                        break;
	    case 'l':
			initMessageLog(optarg);	/* Log file to use */
			break;
	    case 'f':
			infoFile = strdup(optarg);	/* Install script */
			installInfo = new InstallInfo(infoFile);
			installInfo->toUTF8();
			break;
	    default:
			break;
		}
	}

	return 0;
}

static const char *
changeYesNo2_0_1(const char *old)
{
	if (old && !strncasecmp(old, "yes", strlen(old)))
		return "1";

	return "0";
}

static void
init_from_config(server_config_s *cf)
{
	if (!cf)
		return;

	if (!installInfo)
		installInfo = new InstallInfo;

	if (!slapdInfo)
		slapdInfo = new InstallInfo;

#ifdef XP_WIN32
	ds_unixtodospath( cf->sroot);
#endif

	installInfo->set(SLAPD_KEY_SERVER_ROOT, cf->sroot);
    installInfo->set(SLAPD_KEY_FULL_MACHINE_NAME, cf->servname);

    slapdInfo->set(SLAPD_KEY_SERVER_PORT, cf->servport);
    installInfo->set(SLAPD_KEY_SERVER_ADMIN_ID, cf->cfg_sspt_uid);
	installInfo->set(SLAPD_KEY_SERVER_ADMIN_PWD, cf->cfg_sspt_uidpw);
    slapdInfo->set(SLAPD_KEY_SERVER_IDENTIFIER, cf->servid);

#ifdef XP_UNIX
    installInfo->set(SLAPD_KEY_SUITESPOT_USERID, cf->servuser);
#endif

	slapdInfo->set(SLAPD_KEY_SUFFIX, cf->suffix);
    slapdInfo->set(SLAPD_KEY_ROOTDN, cf->rootdn);
	slapdInfo->set(SLAPD_KEY_ROOTDNPWD, cf->rootpw);

    installInfo->set(SLAPD_KEY_ADMIN_DOMAIN, cf->admin_domain);
	LDAPURLDesc *desc = 0;
	if (cf->config_ldap_url &&
		!ldap_url_parse(cf->config_ldap_url, &desc) && desc)
	{
		const char *suffix = DEFAULT_ROOT_DN;
		int isSSL = !strncmp(cf->config_ldap_url, "ldaps:", strlen("ldaps:"));
		char port[6];
		PR_snprintf(port, sizeof(port), "%d", desc->lud_port);
		NSString url = NSString("ldap") +
			(isSSL ? "s" : "") +
			"://" + desc->lud_host +
			":" + port + "/" + suffix;
		installInfo->set(SLAPD_KEY_K_LDAP_URL, url);
		ldap_free_urldesc(desc);
	}

	if (cf->suitespot3x_uid)
		slapdInfo->set(SLAPD_KEY_CONFIG_ADMIN_DN, cf->suitespot3x_uid);
	else
		slapdInfo->set(SLAPD_KEY_CONFIG_ADMIN_DN, cf->cfg_sspt_uid);

	/*
	  If we are here, that means we have been called as a CGI, which
	  means that there must already be an MC host, which means that
	  we are not creating an MC host, which means we must be creating
	  a UG host
	*/
	NSString UGLDAPURL = NSString("ldap://") + cf->servname +
		":" + cf->servport + "/" + cf->suffix;
	installInfo->set(SLAPD_KEY_USER_GROUP_LDAP_URL, UGLDAPURL);
	installInfo->set(SLAPD_KEY_USER_GROUP_ADMIN_ID, cf->rootdn);
	installInfo->set(SLAPD_KEY_USER_GROUP_ADMIN_PWD, cf->rootpw);

	installInfo->addSection("slapd", slapdInfo);

	return;
}

/* -----------------------         main            ------------------------ */

/*
  Initialize the cf structure based on data in the inf file, and also initialize
  our static objects.  Return 0 if everything was OK, and non-zero if there
  were errors, like parsing a bogus inf file
*/
extern "C" int create_config_from_inf(
	server_config_s *cf,
	int argc,
	char *argv[]
)
{
        InstallInfo *admInfo;
	if (parse_commandline(argc, argv))
		return 1;

	admInfo = installInfo->getSection("admin");
	slapdInfo = installInfo->getSection("slapd");
	if (!slapdInfo->get(SLAPD_KEY_SUFFIX))
		slapdInfo->set(SLAPD_KEY_SUFFIX, DEFAULT_ROOT_DN);

	if (installInfo->get(SLAPD_KEY_SERVER_ADMIN_ID))
		slapdInfo->set(SLAPD_KEY_CONFIG_ADMIN_DN,
					   installInfo->get(SLAPD_KEY_SERVER_ADMIN_ID));

	if (!info_is_valid())
		return 1;

	normalizeDNs();

	installInfo->toLocal(SLAPD_KEY_SERVER_ROOT);
    cf->sroot = my_c_strdup(installInfo->get(SLAPD_KEY_SERVER_ROOT));
	installInfo->toUTF8(SLAPD_KEY_SERVER_ROOT);
    cf->servname = my_c_strdup(installInfo->get(SLAPD_KEY_FULL_MACHINE_NAME));

    cf->servport = my_c_strdup(slapdInfo->get(SLAPD_KEY_SERVER_PORT));

    if (admInfo && admInfo->get(SLAPD_KEY_ADMIN_SERVER_PORT)) {
      cf->adminport = my_c_strdup(admInfo->get(SLAPD_KEY_ADMIN_SERVER_PORT));
    } else {
      cf->adminport = my_c_strdup("80"); 
    }

    cf->cfg_sspt = my_c_strdup(
		changeYesNo2_0_1(slapdInfo->get(SLAPD_KEY_SLAPD_CONFIG_FOR_MC)));
    cf->suitespot3x_uid = my_c_strdup(installInfo->get(SLAPD_KEY_SERVER_ADMIN_ID));
    cf->cfg_sspt_uid = my_c_strdup(installInfo->get(SLAPD_KEY_SERVER_ADMIN_ID));
	cf->cfg_sspt_uidpw = my_c_strdup(installInfo->get(SLAPD_KEY_SERVER_ADMIN_PWD));
    cf->servid = my_c_strdup(slapdInfo->get(SLAPD_KEY_SERVER_IDENTIFIER));

#ifdef XP_UNIX
    cf->servuser = my_c_strdup(installInfo->get(SLAPD_KEY_SUITESPOT_USERID));
#endif

    cf->suffix = my_c_strdup(slapdInfo->get(SLAPD_KEY_SUFFIX));
    cf->rootdn = my_c_strdup(slapdInfo->get(SLAPD_KEY_ROOTDN));
	/* Encode the password in SSHA by default */
	cf->rootpw = my_c_strdup(slapdInfo->get(SLAPD_KEY_ROOTDNPWD));
	cf->roothashedpw = (char *)ds_salted_sha1_pw_enc (cf->rootpw);

	const char *test = slapdInfo->get(SLAPD_KEY_REPLICATIONDN);
	const char *testpw = slapdInfo->get(SLAPD_KEY_REPLICATIONPWD);
	if (test && *test && testpw && *testpw)
	{
		cf->replicationdn = my_c_strdup(slapdInfo->get(SLAPD_KEY_REPLICATIONDN));
		cf->replicationpw = my_c_strdup(slapdInfo->get(SLAPD_KEY_REPLICATIONPWD));
		cf->replicationhashedpw = (char *)ds_salted_sha1_pw_enc (cf->replicationpw);
	}

	test = slapdInfo->get(SLAPD_KEY_CONSUMERDN);
	testpw = slapdInfo->get(SLAPD_KEY_CONSUMERPWD);
	if (test && *test && testpw && *testpw)
	{
		cf->consumerdn = my_c_strdup(test);
		cf->consumerpw = my_c_strdup(testpw);
		cf->consumerhashedpw = (char *)ds_salted_sha1_pw_enc (cf->consumerpw);
    }

    cf->changelogdir = my_c_strdup(slapdInfo->get(SLAPD_KEY_CHANGELOGDIR));
    cf->changelogsuffix = my_c_strdup(slapdInfo->get(SLAPD_KEY_CHANGELOGSUFFIX));
    cf->admin_domain = my_c_strdup(installInfo->get(SLAPD_KEY_ADMIN_DOMAIN));
    cf->disable_schema_checking =
		my_c_strdup(
			changeYesNo2_0_1(slapdInfo->get(SLAPD_KEY_DISABLE_SCHEMA_CHECKING)));

	/*
	  Don't create dc=example,dc=com if the user did not select to add the
	  sample entries
	*/
	if (!featureIsEnabled(slapdInfo->get(SLAPD_KEY_ADD_SAMPLE_ENTRIES)))
	{
		cf->samplesuffix = NULL;
	}

	cf->config_ldap_url = (char *)installInfo->get(SLAPD_KEY_K_LDAP_URL);
	LDAPURLDesc *desc = 0;
	if (cf->config_ldap_url &&
		!ldap_url_parse(cf->config_ldap_url, &desc) && desc)
	{
		const char *suffix = DEFAULT_ROOT_DN;
		int isSSL = !strncmp(cf->config_ldap_url, "ldaps:", strlen("ldaps:"));
		char port[6];
		PR_snprintf(port, sizeof(port), "%d", desc->lud_port);
		NSString url = NSString("ldap") +
			(isSSL ? "s" : "") +
			"://" + desc->lud_host +
			":" + port + "/" + suffix;
		installInfo->set(SLAPD_KEY_K_LDAP_URL, url);
		cf->config_ldap_url = my_c_strdup(url);
		ldap_free_urldesc(desc);
	}

	if ((test = installInfo->get(SLAPD_KEY_USER_GROUP_LDAP_URL)))
		cf->user_ldap_url = my_c_strdup(test);
	else
		cf->user_ldap_url = my_c_strdup(cf->config_ldap_url);

	cf->use_existing_config_ds =
		featureIsEnabled(slapdInfo->get(SLAPD_KEY_USE_EXISTING_MC));

	cf->use_existing_user_ds =
		featureIsEnabled(slapdInfo->get(SLAPD_KEY_USE_EXISTING_UG));

	if ((test = slapdInfo->get(SLAPD_KEY_INSTALL_LDIF_FILE)) &&
		!access(test, 0))
	{
		cf->install_ldif_file = my_c_strdup(test);
		// remove the fields from the slapdInfo so we don't try
		// to handle this case later
		slapdInfo->remove(SLAPD_KEY_ADD_ORG_ENTRIES);
		slapdInfo->remove(SLAPD_KEY_INSTALL_LDIF_FILE);
	}

	/* we also have to setup the environment to mimic a CGI */
	static char netsiteRoot[PATH_MAX+32];
	PR_snprintf(netsiteRoot, sizeof(netsiteRoot), "NETSITE_ROOT=%s", cf->sroot);
	putenv(netsiteRoot);

	/* set the admin SERVER_NAMES = slapd-slapdIdentifier */
	static char serverNames[PATH_MAX+32];
	PR_snprintf(serverNames, sizeof(serverNames), "SERVER_NAMES=slapd-%s", cf->servid);
	putenv(serverNames);

	/* get and set the log file */
	/* use the one given on the command line by default, otherwise, use
	   the one from the inf file */
	if (logFile || (test = slapdInfo->get(SLAPD_INSTALL_LOG_FILE_NAME)))
	{
		static char s_logfile[PATH_MAX+32];
		if (logFile)
		{
			PR_snprintf(s_logfile, sizeof(s_logfile), "DEBUG_LOGFILE=%s", logFile);
		}
		else
		{
			PR_snprintf(s_logfile, sizeof(s_logfile), "DEBUG_LOGFILE=%s", test);
			/* also init the C++ api message log */
			initMessageLog(test);
		}
		putenv(s_logfile);
	}

	return 0;
}

extern "C" int
configure_instance_with_config(
	server_config_s *cf,
	int verbose, // if false, silent; if true, verbose
	const char *lfile
)
{
	if (!cf)
		return 1;

	infoFile = 0;
	initMessageLog(lfile);

	if (!verbose)
		installMode = Silent;

	init_from_config(cf);

	return configure_instance();
}

extern "C" int
configure_instance()
{
	char hn[BUFSIZ];
	int status = 0;

	dsLogMessage(SETUP_LOG_START, "Slapd", "Starting Slapd server configuration.");

	if (!info_is_valid())
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd", "Missing Configuration Parameters.");
		return 1;
	}

	if (installInfo == NULL || slapdInfo == NULL)
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd", "Answer cache not found or invalid");
		return 1;
	}

	adminInfo = installInfo->getSection("admin");

	// next, find the slapd.inf file; it is in the dir <server root>/setup/slapd
	installInfo->toLocal(SLAPD_KEY_SERVER_ROOT); // path needs local encoding
	NSString slapdinffile = NSString(installInfo->get(SLAPD_KEY_SERVER_ROOT))
		+ FILE_PATHSEP + "setup" + FILE_PATHSEP + "slapd" + FILE_PATHSEP +
		"slapd.inf";
	InstallInfo temp(slapdinffile);
	if (!(slapdINFFileInfo = temp.getSection("slapd")) ||
		slapdINFFileInfo->isEmpty())
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd", "Missing configuration file %s",
				   (const char *)slapdinffile);
		return 1;
	}
	installInfo->toUTF8(SLAPD_KEY_SERVER_ROOT); // back to utf8

	hn[0] = '\0';

	/*
	 * Get the full hostname.
	 */
      
	if (!installInfo->get(SLAPD_KEY_FULL_MACHINE_NAME))
	{
		NSString h;
		/* Force automatic detection of host name */
#ifdef XP_UNIX
		h = InstUtil::guessHostname();
#else
		/* stevross: figure out NT equivalent */
#endif
		PL_strncpyz(hn, h, BUFSIZ);
		installInfo->set(SLAPD_KEY_FULL_MACHINE_NAME, hn);
	}
	else
	{
		PL_strncpyz(hn,installInfo->get(SLAPD_KEY_FULL_MACHINE_NAME), BUFSIZ);
	}

	NSString sieDN;
	if ((status = create_ss_dir_tree(hn, sieDN)))
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd",
			   "Did not add Directory Server information to Configuration Server.");
		return status;
	}
	else
		dsLogMessage(SETUP_LOG_SUCCESS, "Slapd",
			   "Added Directory Server information to Configuration Server.");

	// at this point we should be finished talking to the Mission Control LDAP
	// server; we may need to establish a connection to the new instance
	// we just created in order to write some of this optional stuff to it

	Ldap *ldap = 0;
	LdapEntry *entry = 0;
	LdapError ldapError = 0;
	NSString newURL = NSString("ldap://") + hn + ":" +
		slapdInfo->get(SLAPD_KEY_SERVER_PORT) + "/" +
		slapdInfo->get(SLAPD_KEY_SUFFIX);
	const char *bindDN = slapdInfo->get(SLAPD_KEY_ROOTDN);
	const char *bindPwd = slapdInfo->get(SLAPD_KEY_ROOTDNPWD);
	// install a sample tree
	if (featureIsEnabled(slapdInfo->get(SLAPD_KEY_ADD_SAMPLE_ENTRIES)))
	{
		if (!ldap)
		{
			ldapError = 0;
			ldap = new Ldap (ldapError, newURL, bindDN, bindPwd, 0, 0);
			if (ldapError.errorCode())
			{
				delete ldap;
				ldap = 0;
				dsLogMessage(SETUP_LOG_WARN, "Slapd",
						   "Could not add sample entries, ldap error code %d",
						   ldapError.errorCode());
			}
			else
			{
				entry = new LdapEntry(ldap);
			}
		}

		if (entry)
		{
			installInfo->toLocal(SLAPD_KEY_SERVER_ROOT); // path needs local
			ldapError = add_sample_entries(installInfo->get(SLAPD_KEY_SERVER_ROOT),
										   entry);
			installInfo->toUTF8(SLAPD_KEY_SERVER_ROOT); // back to utf8
			if (ldapError.errorCode())
			{
				delete ldap;
				ldap = 0;
				dsLogMessage(SETUP_LOG_WARN, "Slapd",
							 "Could not add sample entries, ldap error code %d",
							 ldapError.errorCode());
				destroyLdapEntry(entry);
				entry = 0;
			}
		}
	}

	// create some default organizational entries based on org size, but only
	// if we're creating the User Directory
	if (!featureIsEnabled(slapdInfo->get(SLAPD_KEY_USE_EXISTING_UG)) &&
		featureIsEnabled(slapdInfo->get(SLAPD_KEY_ADD_ORG_ENTRIES)))
	{
		if (!ldap)
		{
			ldapError = 0;
			ldap = new Ldap (ldapError, newURL, bindDN, bindPwd, 0, 0);
			if (ldapError.errorCode())
			{
				delete ldap;
				ldap = 0;
				dsLogMessage(SETUP_LOG_WARN, "Slapd",
						   "Could not populate with ldif file %s error code %d",
						   slapdInfo->get(SLAPD_KEY_ADD_ORG_ENTRIES),
						   ldapError.errorCode());
			}
			else
			{
				entry = new LdapEntry(ldap);
			}
		}

		if (!isAValidDN(slapdInfo->get(SLAPD_KEY_CONFIG_ADMIN_DN)))
		{
			// its a uid
			NSString adminDN = NSString("uid=") +
				slapdInfo->get(SLAPD_KEY_CONFIG_ADMIN_DN) +
				", ou=Administrators, ou=TopologyManagement, " +
				DEFAULT_ROOT_DN;
			slapdInfo->set(SLAPD_KEY_CONFIG_ADMIN_DN, adminDN);
		}

		if (entry)
		{
			installInfo->toLocal(SLAPD_KEY_SERVER_ROOT); // path needs local
			add_org_entries(installInfo->get(SLAPD_KEY_SERVER_ROOT), entry,
							slapdInfo->get(SLAPD_KEY_INSTALL_LDIF_FILE),
							slapdInfo->get(SLAPD_KEY_ORG_SIZE), sieDN);
			installInfo->toUTF8(SLAPD_KEY_SERVER_ROOT); // back to utf8
		}
	}

	if (ldap)
		delete ldap;
	if (entry)
		destroyLdapEntry(entry);

	// create executable shell script to run the console
	create_console_script();
	
	return status;
}

extern "C" int
reconfigure_instance(int argc, char *argv[])
{
	char hn[BUFSIZ];

	dsLogMessage(SETUP_LOG_START, "Slapd", "Starting Slapd server reconfiguration.");

	if (parse_commandline(argc, argv))
		return 1;

	if (installInfo == NULL)
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd", "Answer cache not found or invalid");
		return 1;
	}

	// next, find the slapd.inf file; it is in the dir <server root>/setup/slapd
	installInfo->toLocal(SLAPD_KEY_SERVER_ROOT); // path needs local
	NSString slapdinffile = NSString(installInfo->get(SLAPD_KEY_SERVER_ROOT))
		+ FILE_PATHSEP + "setup" + FILE_PATHSEP + "slapd" + FILE_PATHSEP +
		"slapd.inf";
	InstallInfo temp(slapdinffile);
	if (!(slapdINFFileInfo = temp.getSection("slapd")) ||
		slapdINFFileInfo->isEmpty())
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd", "Missing configuration file %s",
				   (const char *)slapdinffile);
		return 1;
	}
	installInfo->toUTF8(SLAPD_KEY_SERVER_ROOT); // path needs local

	hn[0] = '\0';

	/*
	 * Get the full hostname.
	 */
      
	if (!installInfo->get(SLAPD_KEY_FULL_MACHINE_NAME))
	{
		NSString h;
		/* Force automatic detection of host name */
#ifdef XP_UNIX
		h = InstUtil::guessHostname();
#else
		/* stevross: figure out NT equivalent */
#endif
		PL_strncpyz(hn, h, BUFSIZ);
		installInfo->set(SLAPD_KEY_FULL_MACHINE_NAME, hn);
	}
	else
	{
		PL_strncpyz(hn,installInfo->get(SLAPD_KEY_FULL_MACHINE_NAME), BUFSIZ);
	}

	// search for the app entry for the DS installation we just replaced
	// open an LDAP connection to the Config Directory
	LdapError le;
	Ldap ldap(le,
			  installInfo->get(SLAPD_KEY_K_LDAP_URL),
			  installInfo->get(SLAPD_KEY_SERVER_ADMIN_ID),
			  installInfo->get(SLAPD_KEY_SERVER_ADMIN_PWD));
	if (le != OKAY)
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd",
					 "ERROR: Ldap authentication failed for url %s user id %s (%d:%s)",
					 installInfo->get(SLAPD_KEY_K_LDAP_URL),
					 installInfo->get(SLAPD_KEY_SERVER_ADMIN_ID),
					 le.errorCode(), le.msg());
		return le.errorCode();
	}

	// construct the base of the search
	NSString baseDN = NSString("cn=") + hn + ", ou=" +
		installInfo->get(SLAPD_KEY_ADMIN_DOMAIN) + ", " +
		DEFAULT_ROOT_DN;

	// find the nsApplication entry corresponding to the slapd installation
	// in the given server root
#ifdef XP_WIN32

	char *pszServerRoot = my_strdup(installInfo->get(SLAPD_KEY_SERVER_ROOT));
	char *pszEscapedServerRoot = (char *)malloc(2*strlen(installInfo->get(SLAPD_KEY_SERVER_ROOT)) );
	char *p,*q;
	
	for(p=pszServerRoot,q=pszEscapedServerRoot; p && *p; p++)
	{
		*q = *p;
		if(*p == '\\')
		{
			q++;
			*q='\\';
		}
		q++;
	}
	/* null terminate it */
	*q= *p;


	NSString filter =
	NSString("(&(objectclass=nsApplication)") +
	"(nsnickname=slapd)(nsinstalledlocation=" +
	pszEscapedServerRoot + "))";

	if(pszServerRoot)
	{
		free(pszServerRoot);
	}

	if(pszEscapedServerRoot)
	{
		free(pszEscapedServerRoot);
	}
#else
	NSString filter =
		NSString("(&(objectclass=nsApplication)") +
		"(nsnickname=slapd)(nsinstalledlocation=" +
		installInfo->get(SLAPD_KEY_SERVER_ROOT) + "))";
#endif

	int scope = LDAP_SCOPE_SUBTREE;

	LdapEntry ldapent(&ldap);
	le = ldapent.retrieve(filter, scope, baseDN);
	if (le != OKAY)
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd",
					 "ERROR: Could not find Directory Server Configuration\n"
					 "URL %s user id %s DN %s (%d:%s)" ,
					 installInfo->get(SLAPD_KEY_K_LDAP_URL),
					 installInfo->get(SLAPD_KEY_SERVER_ADMIN_ID),
					 (const char *)baseDN,
					 le.errorCode(), le.msg()); 
		return le.errorCode();
	}

	setAppEntryInformation(&ldapent);

	le = ldapent.replace(ldapent.entryDN());
	if (le != OKAY)
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd",
					 "ERROR: Could not update Directory Server Configuration\n"
					 "URL %s user id %s DN %s (%d:%s)" ,
					 installInfo->get(SLAPD_KEY_K_LDAP_URL),
					 installInfo->get(SLAPD_KEY_SERVER_ADMIN_ID),
					 (const char *)baseDN,
					 le.errorCode(), le.msg()); 
		return le.errorCode();
	}

	// now update the values in the SIEs under the ISIE
	filter = NSString("(objectclass=nsDirectoryServer)");
	scope = LDAP_SCOPE_ONELEVEL;
	baseDN = NSString(ldapent.entryDN());

	ldapent.clear();
	le = ldapent.retrieve(filter, scope, baseDN);
	if (le != OKAY)
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd",
					 "ERROR: Could not find Directory Server Instances\n"
					 "URL %s user id %s DN %s (%d:%s)",
					 installInfo->get(SLAPD_KEY_K_LDAP_URL),
					 installInfo->get(SLAPD_KEY_SERVER_ADMIN_ID),
					 (const char *)baseDN,
					 le.errorCode(), le.msg()); 
		return le.errorCode();
	}

	// ldapent holds the search results, but ldapent.replace will wipe out that
	// information; so, create a new entry to actually do the replace operation
	// while we use the original ldapent to iterate the search results

	do
	{
		LdapEntry repEntry(ldapent.ldap());
		repEntry.retrieve(ldapent.entryDN());
		repEntry.setAttribute("serverVersionNumber", slapdINFFileInfo->get("Version"));
		repEntry.setAttribute("installationTimeStamp", getGMT());
		
		le = repEntry.replace(repEntry.entryDN());
		if (le != OKAY)
		{
			dsLogMessage(SETUP_LOG_FATAL, "Slapd",
						 "ERROR: Could not update Directory Server Instance\n"
						 "URL %s user id %s DN %s (%d:%s)" ,
						 installInfo->get(SLAPD_KEY_K_LDAP_URL),
						 installInfo->get(SLAPD_KEY_SERVER_ADMIN_ID),
						 (const char *)repEntry.entryDN(),
						 le.errorCode(), le.msg()); 
			return le.errorCode();
		}
	}
	while (ldapent.next() == OKAY);

	// we have a new jar file dsXX.jar so we need to update all
	// references to the old jar file name
	filter = NSString("(|(nsclassname=*)(nsjarfilename=*)"
					  "(nsservermigrationclassname=*)"
					  "(nsservercreationclassname=*))");
	scope = LDAP_SCOPE_SUBTREE;

	ldapent.clear();
	le = ldapent.retrieve(filter, scope, baseDN);
	if (le != OKAY)
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd",
					 "ERROR: Could not find Directory Server Instances\n"
					 "URL %s user id %s DN %s (%d:%s)",
					 installInfo->get(SLAPD_KEY_K_LDAP_URL),
					 installInfo->get(SLAPD_KEY_SERVER_ADMIN_ID),
					 (const char *)baseDN,
					 le.errorCode(), le.msg()); 
		return le.errorCode();
	}

	do
	{
		LdapEntry repEntry(ldapent.ldap());
		repEntry.retrieve(ldapent.entryDN());

		const char *replace[] = {
			"nsclassname",
			"nsservermigrationclassname",
			"nsservercreationclassname"
		};
		const int replaceSize = sizeof(replace)/sizeof(replace[0]);

		if (repEntry.getAttribute("nsjarfilename"))
		{
			repEntry.setAttribute("nsjarfilename", DS_JAR_FILE_NAME);
		}

		for (int ii = 0; ii < replaceSize; ++ii)
		{
			char *val = repEntry.getAttribute(replace[ii]);
			// the class name is of the form
			// full class path and name[@jar file[@admin SIE]]
			// so here's what we'll do:
			// search for the first @ in the string; if there's not one, just
			// skip it
			// save the full class path and name to a temp var
			// create the new classname by appending @new jar file to the full class
			// name
			// if there is a second @ in the original string, grab the rest of the
			// original string after the second @ and append @string to the new
			// classname

			const char *ptr = 0;
			if (val && *val && (ptr = strstr(val, "@")))
			{
				int len = int(ptr - val);
				NSString newClass = NSString(val, len) + "@" +
					DS_JAR_FILE_NAME;
				++ptr;
				if (*ptr && (ptr = strstr(ptr, "@"))) {
					newClass = NSString(val, len) + "@" +
						DS_JAR_FILE_NAME + ptr;
				}
				repEntry.setAttribute(replace[ii], newClass);
			}
		}

		le = repEntry.replace(repEntry.entryDN());
		if (le != OKAY)
		{
			dsLogMessage(SETUP_LOG_FATAL, "Slapd",
						 "ERROR: Could not update Directory Server Instance\n"
						 "URL %s user id %s DN %s (%d:%s)" ,
						 installInfo->get(SLAPD_KEY_K_LDAP_URL),
						 installInfo->get(SLAPD_KEY_SERVER_ADMIN_ID),
						 (const char *)repEntry.entryDN(),
						 le.errorCode(), le.msg()); 
			return le.errorCode();
		}
	}
	while (ldapent.next() == OKAY);				

	// we no longer use nsperl - any CGIs which we used to invoke via perl?perlscript
	// are now invoked directly by making the perl script executable - we need to
	// search for all nsexecref: perl?perlscript and replace them with
	// nsexecref: perlscript
	filter = NSString("(nsexecref=perl*)");
	scope = LDAP_SCOPE_SUBTREE;
	baseDN = name_netscaperootDN;

	ldapent.clear();
	le = ldapent.retrieve(filter, scope, baseDN);
	if (le != OKAY)
	{
		if (le == NOT_FOUND) {
			dsLogMessage(SETUP_LOG_INFO, "Slapd",
						 "No old nsperl references found");
		} else {
			dsLogMessage(SETUP_LOG_FATAL, "Slapd",
						 "ERROR: Could not find old nsperl references\n"
						 "URL %s user id %s DN %s (%d:%s)",
						 installInfo->get(SLAPD_KEY_K_LDAP_URL),
						 installInfo->get(SLAPD_KEY_SERVER_ADMIN_ID),
						 (const char *)baseDN,
						 le.errorCode(), le.msg()); 
			return le.errorCode();
		}
	} else {
		do
		{
			LdapEntry repEntry(ldapent.ldap());
			repEntry.retrieve(ldapent.entryDN());
			char *val = repEntry.getAttribute("nsexecref");
			const char *ptr = 0;
			if (val && *val && (ptr = strstr(val, "perl?"))) {
				ptr = strchr(ptr, '?');
				ptr++;
				NSString newscript = NSString(ptr);
				repEntry.setAttribute("nsexecref", newscript);
			}
			
			le = repEntry.replace(repEntry.entryDN());
			if (le != OKAY)
			{
				dsLogMessage(SETUP_LOG_FATAL, "Slapd",
							 "ERROR: Could not fix old nsperl reference\n"
							 "URL %s user id %s DN %s (%d:%s)" ,
							 installInfo->get(SLAPD_KEY_K_LDAP_URL),
							 installInfo->get(SLAPD_KEY_SERVER_ADMIN_ID),
							 (const char *)repEntry.entryDN(),
							 le.errorCode(), le.msg()); 
				return le.errorCode();
			}
		}
		while (ldapent.next() == OKAY);
	}

	return 0;
}

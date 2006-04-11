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
// ds_remove_uninst.cpp
//
// ds_remove routines that use c++ calls in adminutil
//
#include <iostream.h>
#include <fstream.h>
#include <stdio.h>		/* printf, file I/O */
#include <string.h>		/* strlen */
#include <ctype.h>
#ifdef XP_UNIX
#include <strings.h>
#include <pwd.h>
#include <grp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif
#include <stdlib.h>		/* memset, rand stuff */
#include <sys/types.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>

#include "ds_remove_uninst.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "dsalib.h"
#ifdef __cplusplus
}

#include "prprf.h"

#endif
#ifdef XP_UNIX
#include "ux-util.h"
#endif
#include "ldapu.h"
#include "install_keywords.h"
#include "global.h"
#include "setupapi.h"

#define MAX_STR_SIZE 512
static void dsLogMessage(const char *level, const char *which,
			 const char *format, ...)
#ifdef __GNUC__ 
        __attribute__ ((format (printf, 3, 4)));
#else
        ;
#endif

static InstallLog *installLog = NULL;

static void
dsLogMessage(const char *level, const char *which,
			 const char *format, ...)
{
	char bigbuf[BIG_BUF*4];
	va_list ap;
	va_start(ap, format);
	PR_vsnprintf(bigbuf, sizeof(bigbuf), format, ap);
	va_end(ap);
#ifdef _WIN32 // always output to stdout (for CGIs), and always log
	// if a log is available
	fprintf(stdout, "%s %s %s\n", level, which, bigbuf);
	fflush(stdout);
	if (installLog)
		installLog->logMessage(level, which, bigbuf);
#else // not Windows
	if (installLog)
		installLog->logMessage(level, which, bigbuf);
	else
		fprintf(stdout, "%s %s %s\n", level, which, bigbuf);
	fflush(stdout);
#endif

	return;
}

// replace \ in path with \\ for LDAP search filters
static char *
escapePath(const char *path)
{
	char *s = 0;
	if (path) {
		s = new char [(strlen(path)+1)*2]; // worst case
		char *p = s;
		const char *pp = path;
		for (; *pp; ++pp, ++p) {
			if (*pp == '\\') {
				*p++ = *pp;
			}
			*p = *pp;
		}
		*p = 0;
	}

	return s;
}

static LdapErrorCode
localRemoveISIE(LdapEntry &isieEntry)
{
   /* stevross: for now explicitly delete ISIE because it's not getting
	  removed by removeSIE for some reason */
   LdapError err = isieEntry.dropAll(isieEntry.entryDN());
   if (err.errorCode())
   {
	   dsLogMessage(SETUP_LOG_FATAL, "Slapd",
					"Error: could not remove ISIE entry %s: error = %d",
					(const char *)isieEntry.entryDN(), (int)err.errorCode());
   }

   // OK to remove, recursively go up the tree and remove all
   char *dn = new char [strlen(isieEntry.entryDN()) + 10];
   char **explodedDN = ldap_explode_dn(isieEntry.entryDN(), 0);
   int i = 0;

   while (1)
   {
      dn[0] = 0;
      char **s = &explodedDN[i];
      while (*s != NULL)
      {
         strcat(dn, *s);
         strcat(dn, LDAP_PATHSEP);
         s++;
      }

      if (*s == NULL)
      {
         dn[strlen(dn)-strlen(LDAP_PATHSEP)] = 0;
      }

      if (strcasecmp(dn, DEFAULT_ROOT_DN) == 0)
      {
         break;
      }

      err = isieEntry.retrieve(OBJECT_CLASS_FILTER, LDAP_SCOPE_ONELEVEL, dn);

      if (err == NOT_FOUND)
      {
         isieEntry.drop(dn);
         ++i;
      }
      else
      {
         break;
      }
   }

   delete [] dn;
   ldap_value_free(explodedDN);

   return OKAY;
}

//////////////////////////////////////////////////////////////////////////////
// removeInstanceLDAPEntries
//
// 
// remove sie, isie of this instance
// 
// 
// 
//

int removeInstanceLDAPEntries(const char *pszLdapHost,
							   const char *pszPort,
							   const char *pszLdapSuffix,
							   const char *pszUser, 
							   const char *pszPw,
							   const char *pszInstanceName, 
							   const char *pszInstanceHost,
							   const char *pszServerRoot)
{
	char szSearchBase[] = "o=NetscapeRoot";

	/* open LDAP connection */
	LdapError ldapError = 0;
	NSString newURL = NSString("ldap://") + pszLdapHost + ":" +
		pszPort + "/" + pszLdapSuffix;
	Ldap ldap(ldapError, newURL, pszUser, pszPw, 0, 0);
	if (ldapError.errorCode())
	{
		return 1;
	}

	/* get SIE entry */
	char *sroot = escapePath(pszServerRoot);
	LdapEntry sieEntry(&ldap);
	NSString sieFilter = NSString("(&(serverhostname=") + pszInstanceHost +
		")(cn=" + pszInstanceName + ")(serverroot=" +
		sroot + "))";
	ldapError = sieEntry.retrieve(sieFilter, LDAP_SCOPE_SUBTREE, szSearchBase);
	if (ldapError.errorCode())
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd",
					 "Error: could not find the SIE entry using filter %s: error = %d",
					 (const char *)sieFilter, (int)ldapError.errorCode());
		delete [] sroot;
		return 1;
	}

	/* get ISIE entry */
	LdapEntry isieEntry(&ldap);
	NSString isieFilter =
		NSString("(&(objectclass=nsApplication)(uniquemember=") +
		sieEntry.entryDN() + ")(nsinstalledlocation=" +
		sroot + "))";
	ldapError = isieEntry.retrieve(isieFilter, LDAP_SCOPE_SUBTREE, szSearchBase);
	if (ldapError.errorCode())
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd",
					 "Error: could not find the ISIE entry using filter %s: error = %d",
					 (const char *)isieFilter, (int)ldapError.errorCode());
		delete [] sroot;
		return 1;
	}

	/* delete the SIE and ISIE entry */
	LdapErrorCode code = removeSIE(&ldap, sieEntry.entryDN(), False);
	if (code)
	{
		dsLogMessage(SETUP_LOG_FATAL, "Slapd",
					 "Error: could not remove SIE entry %s: error = %d",
					 (const char *)sieEntry.entryDN(), (int)code);
		return code;
	}

	code = localRemoveISIE(isieEntry);

	delete [] sroot;
	return code;
}


int ds_uninst_set_cgi_env(char *pszInfoFileName)
{
	InstallInfo *uninstallInfo = NULL;
	InstallInfo *instanceInfo = NULL;
	static char szQueryString[512] = {0};
	static char szScriptName[512] = {0};
	static char szNetsiteRoot[512] = {0};
	const char *serverID = 0;
	const char *tmp;

	uninstallInfo = new InstallInfo(pszInfoFileName);

	if (!uninstallInfo)
		return 1;

	instanceInfo = uninstallInfo->getSection("uninstall");
	if (!instanceInfo)
		instanceInfo = uninstallInfo;

	putenv("REQUEST_METHOD=GET");
	if (instanceInfo->get(SLAPD_KEY_SERVER_IDENTIFIER))
		serverID = instanceInfo->get(SLAPD_KEY_SERVER_IDENTIFIER);
	else if (ds_get_server_name())
		serverID = ds_get_server_name();

	if (serverID)
		PR_snprintf(szQueryString, sizeof(szQueryString), "QUERY_STRING=InstanceName=%s",
					serverID);

	putenv(szQueryString);

	if (instanceInfo->get(SLAPD_KEY_SERVER_ROOT))
		PR_snprintf(szNetsiteRoot, sizeof(szNetsiteRoot), "NETSITE_ROOT=%s",
					instanceInfo->get(SLAPD_KEY_SERVER_ROOT));
	putenv(szNetsiteRoot);

	if (serverID)
		PR_snprintf(szScriptName, sizeof(szScriptName), "SCRIPT_NAME=/%s/Tasks/Operation/Remove",
					serverID);
	putenv(szScriptName);

	// remove SIE entry
	const char *host = instanceInfo->get(SLAPD_KEY_K_LDAP_HOST);
	char port[20] = {0};
	if (instanceInfo->get(SLAPD_KEY_K_LDAP_PORT))
		strncpy(port, instanceInfo->get(SLAPD_KEY_K_LDAP_PORT), sizeof(port)-1);
	const char *suffix = instanceInfo->get(SLAPD_KEY_SUFFIX);
	const char *ldapurl = instanceInfo->get(SLAPD_KEY_K_LDAP_URL);
	LDAPURLDesc *desc = 0;
	if (ldapurl && !ldap_url_parse((char *)ldapurl, &desc) && desc) {
		if (!host)
			host = desc->lud_host;
		if (port[0] == 0)
			PR_snprintf(port, sizeof(port), "%d", desc->lud_port);
		if (!suffix)
			suffix = desc->lud_dn;
	}

	// get and set the log file
	if ((tmp = instanceInfo->get(SLAPD_INSTALL_LOG_FILE_NAME)))
	{
		static char s_logfile[PATH_MAX+32];
		PR_snprintf(s_logfile, sizeof(s_logfile), "DEBUG_LOGFILE=%s", tmp);
		putenv(s_logfile);
		installLog = new InstallLog(tmp);
	}
		
	removeInstanceLDAPEntries(host, port, suffix,
							  instanceInfo->get(SLAPD_KEY_SERVER_ADMIN_ID),
							  instanceInfo->get(SLAPD_KEY_SERVER_ADMIN_PWD),
							  serverID, 
							  instanceInfo->get(SLAPD_KEY_FULL_MACHINE_NAME),
							  instanceInfo->get(SLAPD_KEY_SERVER_ROOT));

	if (desc)
		ldap_free_urldesc(desc);
	return 0;
}

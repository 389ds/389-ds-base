/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
///////////////////////////////////////////////////////////////////////////////
// dsinst.c - Brandx Directory Server Installation Plug-In 
//
#include <windows.h>
#include <commctrl.h>
#include <nssetup.h>
#include <ldapu.h>

#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <process.h>
#include <regstr.h>
#include <ldap.h>
#include <wingdi.h>
#include "resource.h"
#include "dsinst.h"
#include "install_keywords.h"
#include "libinst.h"

#ifdef TARGETDIR
#undef TARGETDIR
#endif

#define NUM_PROP_PAGES 16

// this is the path to perl, relative to the server root directory
#define PERL_EXE "bin\\slapd\\admin\\bin\\perl.exe"
// this is the keyword to lookup in slapd.inf
#define NSPERL_POST_INSTALL_PROG "NSPerlPostInstall"

#define INDEX_FIRST_PAGE 0
#define INDEX_LAST_PAGE  14

#define NUM_CIR_ATTR 12
#define NUM_ORC_ATTR 1
#define NUM_SIR_ATTR 10
static MODULEINFO mi = {
  NULL,           // m_hModule
  NULL,           // m_hwndParent
  NS_WIZERROR,    // m_nResult
  0,			  // m_nReInstall
  NULL,			  // m_szMCCBindAs
};

static INFDATA cd;

static void normalizeDNs();

extern int ds_dn_uses_LDAPv2_quoting(const char *utf8dn);
extern char *dn_normalize_convert(char *dn);
static void fixDN(char *dn);
static char *dialogMessage; /* used by shutdownDialogProc */
#define OLD_VERSION_SIZE 32
static char oldVersion[OLD_VERSION_SIZE]; /* used by reinstall */

static void
storeUserDirectoryInfo()
{
	char *utf8UserGroupAdmin = NULL;
	char *utf8UserGroupAdminPW = NULL;
	char *utf8UserGroupURL = NULL;

	if (mi.m_nReInstall == 1)
		return; // do nothing if reinstall

	if(mi.m_nExistingUG == 0)
	{
		/* the user is creating a new UG with this instance */
		if(mi.m_nExistingMCC == 0)
		{
			/* the user is also creating a new MCC so set UG admin to MCC admin */
			lstrcpy(mi.m_szUserGroupAdmin, mi.m_szMCCBindAs);
			lstrcpy(mi.m_szUserGroupAdminPW, mi.m_szMCCPw);

		}else{
			/* user is using an existing MCC so only creating UG, make UG user same as
			   Root DN */
			lstrcpy(mi.m_szUserGroupAdmin, mi.m_szInstanceUnrestrictedUser);
			lstrcpy(mi.m_szUserGroupAdminPW, mi.m_szInstancePassword);

		}
		sprintf(mi.m_szUserGroupURL, "ldap://%s:%d/%s", mi.m_szInstanceHostName,
				mi.m_nInstanceServerPort, mi.m_szInstanceSuffix);	
	}

	SetLdapUserDirInit(TRUE);

	utf8UserGroupAdmin = localToUTF8(mi.m_szUserGroupAdmin);
	utf8UserGroupAdminPW = localToUTF8(mi.m_szUserGroupAdminPW);
	utf8UserGroupURL = localToUTF8(mi.m_szUserGroupURL);
	SetLdapUserDirID(utf8UserGroupAdmin);
	SetLdapUserDirPWD(utf8UserGroupAdminPW);
	SetLdapUserDirURL(utf8UserGroupURL);
	nsSetupFree(utf8UserGroupAdmin);
	nsSetupFree(utf8UserGroupAdminPW);
	nsSetupFree(utf8UserGroupURL);
}

/* converts server ID of the form slapd-foo to foo, and NULL to "" */
static const char *
getShortName(const char *serverID)
{
	const char *retval = serverID;
	const char *prefix = "slapd-";
	int preflen = strlen(prefix);

	if (serverID && !strncmp(serverID, prefix, preflen))
		retval = serverID + preflen;

	if (!retval)
		retval = "";

	return retval;
}

static FILE*
getLogFileP()
{
	static FILE* logfp = 0;

	if (!getenv("USE_LOGFILE"))
		return logfp;

	if (!logfp)
		logfp = fopen("c:\\debug.out", "w");

	return logfp;
}

static void
myLogData(const char *s, ...)
{
	FILE* logfp = getLogFileP();
	va_list ap;

	if (!logfp)
		return;

	va_start(ap, s);
	vfprintf(logfp, s, ap);
	va_end(ap);
	fprintf(logfp, "\n");
	fflush(logfp);
}

/* Will return a malloc'd "" if no error - so caller should always use LocalFree() with
   the returned value
*/
static LPVOID
getLastErrorMessage()
{
	LPVOID lpMsgBuf = NULL;

	FormatMessage( 
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL,
		GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
		(LPTSTR) &lpMsgBuf,
		0,
		NULL);

	/* always return something . . . */
	if (lpMsgBuf == NULL) {
		lpMsgBuf = strdup("");
	}

	return lpMsgBuf;
}	

static void
myLogError(const char *s, ...)
{
	va_list ap;
	LPVOID lpMsgBuf;
	FILE* logfp = getLogFileP();

	if (!logfp)
		return;

	if (lpMsgBuf = getLastErrorMessage()) {
		fprintf(logfp, "Error: %d (%s): at ", GetLastError(), lpMsgBuf);
		// Free the buffer.
		LocalFree( lpMsgBuf );
	}

	va_start(ap, s);
	vfprintf(logfp, s, ap);
	va_end(ap);
}

/*
  prints a message if the given dn uses LDAPv2 style quoting
*/
void
checkForLDAPv2Quoting(const char *dn_to_test)
{
	char *utf8dn = localToUTF8(dn_to_test);
    if (ds_dn_uses_LDAPv2_quoting(utf8dn))
    {
		char *newdn = strdup(dn_to_test);
		fixDN(newdn);
		DSMessageBoxOK(WARN_USING_LDAPV2_QUOTES_TITLE,
					   WARN_USING_LDAPV2_QUOTES,
					   dn_to_test, dn_to_test, newdn);
		free(newdn);
    }
	if (utf8dn)
		nsSetupFree(utf8dn);

    return;
}

int
IsValidAdminDomain(
	const char *host,
	int port,
	const char *suffix,
	const char *admin_domain,
	const char *binddn,
	const char *binddnpwd
)
{
	char ldapurl[4096];
	int status = FALSE;
	Ldap *ldap = NULL;

	sprintf(ldapurl, "ldap://%s:%d/%s", host, port, suffix);
	if (createLdap(&ldap, ldapurl, binddn, binddnpwd, 0, 0) == OKAY)
	{
		LdapEntry *le = createLdapEntry(ldap);
		char *dn = formAdminDomainDN(admin_domain);
		if (le && dn && entryExists(le, dn))
			status = TRUE;
		if (dn)
			nsSetupFree(dn);
		if (le)
			destroyLdapEntry(le);
	}

	if (ldap)
		destroyLdap(ldap);

	return status;
}

void ControlSlapdInstance(char *pszServiceName, BOOL bOn);
static void ConvertPasswordToPin(char *pszServerRoot, char *pszServiceName);
static void ReinstallUpgradeServer(char *pszServerRoot, char *pszServiceName);

char *getGMT()
{
	static char		buf[20];
	time_t		curtime;
	struct tm	ltm;

	curtime = time( (time_t *)0 );
#ifdef _WIN32
	ltm = *gmtime( &curtime );
#else
	gmtime_r( &curtime, &ltm );
#endif
	strftime( buf, sizeof(buf), "%Y%m%d%H%M%SZ", &ltm );
	return buf;
}

char *onezero2yesno(int value)
{

	if (1 == value)
	{ 
		return "yes";
	}else{
		return "no";
	}

}

int yesno2onezero(char *value)
{

	if(!lstrcmpi("yes", value) )
	{
		return 1;
	}else{
		return 0;
	}

}

////////////////
///
//	determine whether a string contains 8 bit characters 
//
static int
contains8BitChars(const char *s)
{
    int ret = 0;

    if (s && *s)
    {
                for (; !ret && *s; ++s)
                {
                        ret = (*s & 0x80);
                }
    }

    return ret;
}

////////////////
///
//	determine whether a dn is valid or not 
//
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
		char *utf8dn = localToUTF8(dn_to_test);
		char **rdnList = ldap_explode_dn(utf8dn, 0);
		char **rdnNoTypes = ldap_explode_dn(utf8dn, 1);
		if (!rdnList || !rdnList[0] || !rdnNoTypes || !rdnNoTypes[0] ||
			!*rdnNoTypes[0] || !stricmp(rdnList[0], rdnNoTypes[0]))
		{
			ret = 0;
		}
		if (rdnList)
			ldap_value_free(rdnList);
		if (rdnNoTypes)
			ldap_value_free(rdnNoTypes);
		if (utf8dn)
			nsSetupFree(utf8dn);
	}

	if ((ret == 1) && dn_to_test)
		checkForLDAPv2Quoting(dn_to_test);

	return ret;
}


////////////////
///
//	check if DN is valid, display error if not 
//  returns 1 if dn is valid
//          0 if dn is invalid
int isValidDN(char *dn)
{
	int nReturn;

	if( 0 == (nReturn = isAValidDN(dn)) )
		DSMessageBoxOK(ERR_INVALID_DN_TITLE, ERR_INVALID_DN, dn, dn);

	return nReturn;
}

////////////////
///
//	check if port is valid, display error if not 
//  returns 1 if port is valid
//          0 if port is invalid
int isValidPort(int port)
{
	int nReturn = 1;
	if (port <= 0 || port > MAXPORT)
	{
		DSMessageBoxOK(ERR_INVALID_PORT_TITLE, ERR_INVALID_PORT, 0,
					   port);
		nReturn = 0;
	}

	return nReturn;
}

////////////////
///
//	get the components out of an ldapurl 
//  
//         

int GetURLComponents(char *szURL, char *szHost, int *nPort, char *szBase)
{

	LDAPURLDesc *ludpp;

	int res;

	if ( ( res = ldap_url_parse( szURL, &ludpp ) ) != 0 )
	{ 
		return res;
	}

	if( NULL != ludpp->lud_host)
	{
	   strcpy(szHost, ludpp->lud_host);
	}else{
	   strcpy(szHost, "\0");
	}

	*nPort = ludpp->lud_port;

	if( NULL != ludpp->lud_dn)
	{
	   strcpy(szBase, ludpp->lud_dn);
	}else{
	   strcpy(szBase, "\0");
	}


	ldap_free_urldesc( ludpp );

	return 0;

}

////////////////
///
//
//
void StartWSA()
{
WORD wVersionRequested;
WSADATA wsaData;
int err;
 
wVersionRequested = MAKEWORD( 2, 0 );
 
err = WSAStartup( wVersionRequested, &wsaData );
if ( err != 0 ) {
    /* Tell the user that we couldn't find a usable */
    /* WinSock DLL.                                  */
	DSMessageBoxOK(ERR_NO_WINSOCK_TITLE, ERR_NO_WINSOCK, 0);
    return;
}
 
/* Confirm that the WinSock DLL supports 2.0.*/
/* Note that if the DLL supports versions greater    */
/* than 2.0 in addition to 2.0, it will still return */
/* 2.0 in wVersion since that is the version we      */
/* requested.                                        */
 
if ( LOBYTE( wsaData.wVersion ) != 2 ||
        HIBYTE( wsaData.wVersion ) != 0 ) {
    /* Tell the user that we couldn't find a usable */
    /* WinSock DLL.                                  */
	DSMessageBoxOK(ERR_NO_WINSOCK_VER_TITLE, ERR_NO_WINSOCK_VER, 0);
    WSACleanup( );
    return; 
}
 
/* The WinSock DLL is acceptable. Proceed. */

}

////////////////
//
//
//

BOOL FullyQualifyHostName(char * HostName) 
{ 
    static char * domain = 0; 
    struct hostent * hptr; 
    BOOL bRC = TRUE;

 hptr = (struct hostent*)gethostbyname(HostName); 
 if (hptr) { 
  /* See if h_name is fully-qualified */ 
  if (hptr->h_name) { 
   domain = strchr(hptr->h_name, '.'); 
   sprintf(HostName,"%s",hptr->h_name); 
   return bRC;
  } 

  /* Otherwise look for a fully qualified alias */ 
  if ((domain == 0) && 
   (hptr->h_aliases && hptr->h_aliases[0])) { 
   char **p; 
   for (p = hptr->h_aliases; *p; ++p) { 
    domain = strchr(*p, '.'); 
    if (domain) break; 
   } 
  } 
 } 

 if (domain != 0) 
 { 
     if (domain[0] == '.') 
     { 
  ++domain; 
     } 
     sprintf(HostName,"%s.%s", HostName, domain); 
 } else 
 { 
     bRC = FALSE;
 } 

 return bRC;
} 

/////////////////
//
//  UTF8IsValidLdapUser
//
//	converts necessary things to UTF8 before calling server
//

BOOL UTF8IsValidLdapUser(char *szHost, int nPort, char *szSuffix, char *szBindAs, char *szPw, BOOL bParam)
{

	char *utf8Host=NULL;
	char *utf8Suffix=NULL;
	char *utf8BindAs=NULL;
	char *utf8Pw=NULL;
	BOOL bReturn;
	
	/* convert to UTF8 first incase international data */
	utf8Host = localToUTF8(szHost);
	utf8Suffix = localToUTF8(szSuffix);
	utf8BindAs = localToUTF8(szBindAs);
	utf8Pw     = localToUTF8(szPw);

	bReturn = IsValidLdapUser(utf8Host, nPort, utf8Suffix, &utf8BindAs, utf8Pw, bParam);

	if( utf8Host)     nsSetupFree(utf8Host);
	if( utf8Suffix)   nsSetupFree(utf8Suffix);
	if( utf8BindAs)   nsSetupFree(utf8BindAs);
	if( utf8Pw)       nsSetupFree(utf8Pw);

	return bReturn;

}
					 
/////////////////
//
//  UTF8IsValidAdminDomain
//
//	converts necessary things to UTF8 before calling server
//

BOOL UTF8IsValidAdminDomain(char *szHost, int nPort, char *szSuffix, char *szAdminDomain, char *szBindAs, char *szPw)
{

	char *utf8Host=NULL;
	char *utf8Suffix=NULL;
	char *utf8BindAs=NULL;
	char *utf8Pw=NULL;
	char *utf8AdminDomain=NULL;
	BOOL bReturn;
	
	/* convert to UTF8 first incase international data */
	utf8Host = localToUTF8(szHost);
	utf8Suffix = localToUTF8(szSuffix);
	utf8BindAs = localToUTF8(szBindAs);
	utf8Pw     = localToUTF8(szPw);
	utf8AdminDomain     = localToUTF8(szAdminDomain);

	bReturn = IsValidAdminDomain(utf8Host, nPort, utf8Suffix, utf8AdminDomain, utf8BindAs, utf8Pw);

	if( utf8Host)     nsSetupFree(utf8Host);
	if( utf8Suffix)   nsSetupFree(utf8Suffix);
	if( utf8BindAs)   nsSetupFree(utf8BindAs);
	if( utf8Pw)       nsSetupFree(utf8Pw);
	if( utf8AdminDomain)       nsSetupFree(utf8AdminDomain);

	return bReturn;

}
					 

void getAdminServInfo() 
{
  char *pszAdminSection="admin";
  char szTempDir[MAX_PATH];
  char szCacheFile[MAX_PATH];

  GetEnvironmentVariable("TEMP", szTempDir, sizeof(szTempDir));

  sprintf(szCacheFile, "%s\\install.inf", szTempDir);

  mi.m_nAdminServerPort = GetPrivateProfileInt(pszAdminSection, SLAPD_KEY_ADMIN_SERVER_PORT, 
					       -1, szCacheFile);
  if (mi.m_nAdminServerPort == -1) {
      myLogData("Warning: Could not determine admin server port for Directory Server Gateway and Orgchart configuration files. Please update them manually.");
      mi.m_nAdminServerPort = DEFAULT_ADMIN_PORT;
  }
  
}

BOOL writeINFfile(const char *filename)
{
   FILE *fp = fopen(filename, "wb");

   
   if (NULL == fp)
	   return FALSE;


   if(0 == lstrcmp("\0", mi.m_szInstallDN) )
   {
      char * szAdminDN = NULL;
      szAdminDN = formAdminDomainDN(mi.m_szAdminDomain);
      if (szAdminDN)
      {
        sprintf(mi.m_szInstallDN, szAdminDN);
        nsSetupFree(szAdminDN);
      }
      else
      {
        //note probably should fail. 
        LogData(NULL, "Warning: Slapd unable to Form Admin Domain, guessing");
        sprintf(mi.m_szInstallDN, "ou=%s, o=NetscapeRoot", mi.m_szAdminDomain);
      }
   }
 
   // write global section header
   fprintf(fp, "[General]\n");
   fprintf(fp, "%s= %s\n", GLOBAL_INF_LDAP_USED, "TRUE");
   fprintf(fp, "%s= %s\n", SLAPD_KEY_SERVER_ADMIN_ID, mi.m_szMCCBindAs);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_SERVER_ADMIN_PWD, mi.m_szMCCPw);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_FULL_MACHINE_NAME, mi.m_szInstanceHostName);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_SERVER_ROOT, TARGETDIR);
  
   fprintf(fp, "%s= %s\n", SLAPD_KEY_K_LDAP_URL, mi.m_szLdapURL);


   fprintf(fp, "%s= %s\n", SLAPD_KEY_ADMIN_DOMAIN, mi.m_szAdminDomain);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_USER_GROUP_LDAP_URL, mi.m_szUserGroupURL);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_USER_GROUP_ADMIN_ID, mi.m_szUserGroupAdmin);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_USER_GROUP_ADMIN_PWD, mi.m_szUserGroupAdminPW);

   // write Admin section header.
   getAdminServInfo(); /* Right now this only gets the admin port. If you want more,
			  you'll have to change getAdminServInfo.*/
   fprintf(fp, "\n[admin]\n");
   fprintf(fp, "%s= %d\n", SLAPD_KEY_ADMIN_SERVER_PORT, mi.m_nAdminServerPort);

 
   // write DS section header
   fprintf(fp, "\n[slapd]\n");
   fprintf(fp, "%s= %d\n", SLAPD_KEY_SERVER_PORT, mi.m_nInstanceServerPort);
   if(0 == mi.m_nExistingUG)
   {
		/* don't write this key when config only directory */
	    /* config only directory when using existing data store */
		fprintf(fp, "%s= %s\n", SLAPD_KEY_SUFFIX, mi.m_szInstanceSuffix);
   }

   fprintf(fp, "%s= %s\n", SLAPD_KEY_USE_EXISTING_MC,
		   onezero2yesno(mi.m_nExistingMCC));
   fprintf(fp, "%s= %s\n", SLAPD_KEY_USE_EXISTING_UG,
		   onezero2yesno(mi.m_nExistingUG));

   fprintf(fp, "%s= %s\n", SLAPD_KEY_ROOTDN, mi.m_szInstanceUnrestrictedUser);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_ROOTDNPWD, mi.m_szInstancePassword);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_SERVER_IDENTIFIER, mi.m_szServerIdentifier);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_SLAPD_CONFIG_FOR_MC, onezero2yesno(mi.m_nCfgSspt) );	      
   fprintf(fp, "%s= %s\n", SLAPD_KEY_ADD_SAMPLE_ENTRIES, onezero2yesno(mi.m_nPopulateSampleEntries));
   fprintf(fp, "%s= %s\n", SLAPD_KEY_ADD_ORG_ENTRIES, onezero2yesno(mi.m_nPopulateSampleOrg));


   fprintf(fp, "%s= %s\n", SLAPD_KEY_USE_REPLICATION, onezero2yesno(
		   (mi.m_nSetupConsumerReplication || mi.m_nSetupSupplierReplication) ));
  
   /* consumer replication settings */
   fprintf(fp, "%s= %d\n", SLAPD_KEY_SETUP_CONSUMER, mi.m_nSetupConsumerReplication);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_CIR_HOST, mi.m_szConsumerHost);
   fprintf(fp, "%s= %d\n", SLAPD_KEY_CIR_PORT, mi.m_nConsumerPort);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_CIR_SUFFIX, mi.m_szConsumerRoot);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_CIR_BINDDN, mi.m_szConsumerBindAs);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_CIR_BINDDNPWD, mi.m_szConsumerPw);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_CIR_SECURITY_ON, onezero2yesno(mi.m_nConsumerSSL));
   fprintf(fp, "%s= %d\n", SLAPD_KEY_CIR_INTERVAL, mi.m_nCIRInterval);

   if(!strcmp(DEFAULT_CIR_DAYS, mi.m_szCIRDays) )
   {
	   /* if default of all days write null to inf file as that is what cgi wants */
		fprintf(fp, "%s=\n", SLAPD_KEY_CIR_DAYS);
   }else{
		fprintf(fp, "%s= %s\n", SLAPD_KEY_CIR_DAYS, mi.m_szCIRDays);
   }

   if(!strcmp(DEFAULT_CIR_TIMES, mi.m_szCIRTimes) )
   {
   	   /* if default of all times write null to inf file as that is what cgi wants */
	   fprintf(fp, "%s=\n", SLAPD_KEY_CIR_TIMES);
   }else{
	   fprintf(fp, "%s= %s\n", SLAPD_KEY_CIR_TIMES, mi.m_szCIRTimes);
   }
   
   fprintf(fp, "%s= %s\n", SLAPD_KEY_REPLICATIONDN, mi.m_szSupplierDN);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_REPLICATIONPWD, mi.m_szSupplierPW);

 
   /* Supplier replication settings */
   fprintf(fp, "%s= %d\n", SLAPD_KEY_SETUP_SUPPLIER, mi.m_nSetupSupplierReplication);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_CHANGELOGDIR,	mi.m_szChangeLogDbDir);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_CHANGELOGSUFFIX, mi.m_szChangeLogSuffix);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_SIR_HOST, mi.m_szSupplierHost);
   fprintf(fp, "%s= %d\n", SLAPD_KEY_SIR_PORT, mi.m_nSupplierPort);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_SIR_SUFFIX, mi.m_szSupplierRoot);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_SIR_BINDDN, mi.m_szSupplierBindAs);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_SIR_BINDDNPWD, mi.m_szSupplierPw);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_SIR_SECURITY_ON, onezero2yesno(mi.m_nSupplierSSL));

   if(!strcmp(DEFAULT_SIR_DAYS, mi.m_szSIRDays) )
   {
	   /* if default of all days write null to inf file as that is what cgi wants */
		fprintf(fp, "%s=\n", SLAPD_KEY_SIR_DAYS);
   }else{
		fprintf(fp, "%s= %s\n", SLAPD_KEY_SIR_DAYS, mi.m_szSIRDays);
   }

   if(!strcmp(DEFAULT_SIR_TIMES, mi.m_szSIRTimes) )
   {
   	   /* if default of all times write null to inf file as that is what cgi wants */
	   fprintf(fp, "%s=\n", SLAPD_KEY_SIR_TIMES);
   }else{
	   fprintf(fp, "%s= %s\n", SLAPD_KEY_SIR_TIMES, mi.m_szSIRTimes);
   }

   fprintf(fp, "%s= %s\n", LOCAL_INF_CONFIG_CONSUMER_DN, onezero2yesno(mi.m_nConfigConsumerDN));
   fprintf(fp, "%s= %s\n", SLAPD_KEY_CONSUMERDN, mi.m_szConsumerDN);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_CONSUMERPWD, mi.m_szConsumerPW);
  
   
   fprintf(fp, "%s= %s\n", SLAPD_KEY_INSTALL_LDIF_FILE, mi.m_szPopLdifFile);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_DISABLE_SCHEMA_CHECKING, onezero2yesno(mi.m_nDisableSchemaChecking));

   fprintf(fp, "%s= %d\n", LOCAL_INF_SNMP_ON, mi.m_nSNMPOn);
   fprintf(fp, "%s= %s\n", SLAPD_INSTALL_LOG_FILE_NAME,	LOGFILE );

   fclose(fp);

   return TRUE;
}

//////////////////////////////////////////////////////////////////////////////
// isValidServerID(char *pszServerIdentifier)
//
// check if valid serverid, for n
//

BOOL isValidServerID(char *pszServerIdentifier)
{

	char *fullId;
	char  line[MAX_PATH];
	DWORD Result;
    HKEY  hServerKey;
	BOOL  bRC = TRUE;

	/* first check that it only contains 7 bit characters */
	if( contains8BitChars(pszServerIdentifier) )
	{
	    DSMessageBoxOK(ERR_8BIT_SERVID_TITLE, ERR_8BIT_SERVID, 0);	
		bRC = FALSE;
	}else{
		/* looks ok, now check if it already exists */

		/* for now just check registry to see if this server ID exists,
			in future add might want to add more sanity checks */

		fullId = (char *)malloc(lstrlen(DS_ID_SERVICE) + lstrlen(pszServerIdentifier) + 6);
		sprintf(fullId, "%s-%s", DS_ID_SERVICE, pszServerIdentifier);

		sprintf(line, "%s\\%s", KEY_SERVICES, fullId); 
   
		Result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, 
		                      line,
			                  0,
				              KEY_ALL_ACCESS,
					          &hServerKey);


		if (Result == ERROR_SUCCESS) 
		{
			/* it already exists */
			DSMessageBoxOK(ERR_SERVER_ID_EXISTS_TITLE, ERR_SERVER_ID_EXISTS,
						   getShortName(pszServerIdentifier),
						   getShortName(pszServerIdentifier));	
			bRC = FALSE;

		}

		free(fullId);
	}

	return bRC;
}


//////////////////////////////////////////////////////////////////////////////
// 
// set_default_ldap_settings()
//
// hostname = getHostName
// serverid = hostname up to first period  
// suffix   = o=(rest of hostname) ie o=mcom.com
// port     = 389
// rootDn = DirectoryManager


int set_default_ldap_settings()
{

    int i, j = 0;

    DSGetHostName(mi.m_szInstanceHostName, MAX_STR_SIZE);
    /* assumption: hostname up to first period for serverid */
    for( i = 0; !(   (mi.m_szInstanceHostName[i] == '\0') 
                  || (mi.m_szInstanceHostName[i] == '.' ) ); 
         i++)
    {
        mi.m_szServerIdentifier[i] = mi.m_szInstanceHostName[i];
    }
    /* null terminate it */
    mi.m_szServerIdentifier[i]='\0';
	if (mi.m_szInstanceHostName[0] && strchr(mi.m_szInstanceHostName, '.'))
	{
		DSGetDefaultSuffix(mi.m_szInstanceSuffix, mi.m_szInstanceHostName);
	}
	else
	{
		strcpy(mi.m_szInstanceSuffix, "dc=example, dc=com");
	}

	/* default admin domain is also derived from the FQDN */
	++i;
	sprintf(mi.m_szAdminDomain, "%s", mi.m_szInstanceHostName+i);
	
    mi.m_nInstanceServerPort=DEFAULT_SERVER_PORT;
                                    
    sprintf(mi.m_szInstanceUnrestrictedUser, DEFAULT_UNRESTRICTED_USER);

    mi.m_nCfgSspt = DEFAULT_CONFIG_SSPT;

	sprintf(mi.m_szSsptUid, DEFAULT_SSPT_USER);

    /* stevross: don't want default for these in silent mode, user must specify them */
    if( SILENTMODE != MODE)
    {
        sprintf(mi.m_szSupplierDN, DEFAULT_SUPPLIER_DN);
        sprintf(mi.m_szChangeLogSuffix, DEFAULT_CHANGELOGSUFFIX);

    }



    /* don't want to use these unless they were asked in dialog, then this flag will be
       changed */
	mi.m_nSetupSupplierReplication = NO_REPLICATION;
	mi.m_nSetupConsumerReplication = NO_REPLICATION;

    mi.m_nUseSupplierSettings  = 0;
    mi.m_nUseChangeLogSettings = 0;

	
	lstrcpy(mi.m_szSIRDays, DEFAULT_SIR_DAYS);
	lstrcpy(mi.m_szSIRTimes, DEFAULT_SIR_TIMES);
	mi.m_nSupplierPort = DEFAULT_SERVER_PORT;

    lstrcpy(mi.m_szCIRDays, DEFAULT_CIR_DAYS);
    lstrcpy(mi.m_szCIRTimes, DEFAULT_CIR_TIMES);
	mi.m_nConsumerPort = DEFAULT_SERVER_PORT;

	mi.m_nCIRInterval = DEFAULT_CIR_INTERVAL;

	
	/* default MCC settings */
	lstrcpy(mi.m_szMCCSuffix, NS_DOMAIN_ROOT);
	mi.m_nMCCPort=DEFAULT_SERVER_PORT;

	mi.m_szMCCBindAs = malloc(MAX_STR_SIZE);
	sprintf(mi.m_szMCCBindAs, "%s", DEFAULT_SSPT_USER);

	lstrcpy(mi.m_szUGSuffix, mi.m_szInstanceSuffix);
	mi.m_nUGPort=DEFAULT_SERVER_PORT;
	lstrcpy(mi.m_szUserGroupAdmin, DEFAULT_UNRESTRICTED_USER);

	mi.m_nPopulateSampleEntries = DEFAULT_POPULATE_SAMPLE_ENTRIES;
	mi.m_nDisableSchemaChecking = DEFAULT_DISABLE_SCHEMA_CHECKING;
 
    return 0;
}

//////////////////////////////////////////////////////////////////////////////
// 
// verify_ldap_settings()
// 
// verifys that ldap settings are valid before installing instance  
// 
//
int verify_ldap_settings()
{
    /* XXX stevross: may want to add checks for other things later */

    /* for now just make sure port is valid */
    if( IsValidNetworkPort( mi.m_nInstanceServerPort ) )
    {
        return 0;
    }else{
		DSMessageBoxOK(ERR_SERV_RUN_ON_PORT_TITLE, ERR_SERV_RUN_ON_PORT,
					   0, mi.m_nInstanceServerPort);
        return -1;
    }


}


//////////////////////////////////////////////////////////////////////////////
// 
// set_ldap_settings()
// 
//  registers ldap settings with framework for use by other installers
// 
//

void set_ldap_settings()
{
	
	char *utf8MCCHost=NULL;
	char *utf8MCCSuffix=NULL;
	char *utf8MCCBindAs=NULL;
	char *utf8MCCPw=NULL;
	char *utf8AdminDomain=NULL;
    char szFullAdminDN[MAX_STR_SIZE];

	if(1 != mi.m_nExistingMCC && SILENTMODE != MODE)
    {
		/* this new instance will be MCC, but only copy over things
			if not silent mode, in silent mode it will read correct
			mcc stuff from the cache */
		lstrcpy(mi.m_szMCCHost, mi.m_szInstanceHostName);
		mi.m_nMCCPort = mi.m_nInstanceServerPort;
		lstrcpy(mi.m_szMCCSuffix, NS_DOMAIN_ROOT);
		sprintf(mi.m_szMCCBindAs, "%s", mi.m_szSsptUid);
		lstrcpy(mi.m_szMCCPw, mi.m_szSsptUidPw);

	}

	/* use existing MCC stuff we read in */

	/* convert to UTF8 first for international stuff */
	utf8MCCHost = localToUTF8(mi.m_szMCCHost);
	utf8MCCSuffix = localToUTF8(mi.m_szMCCSuffix);
	utf8MCCBindAs = localToUTF8(mi.m_szMCCBindAs);
	utf8MCCPw     = localToUTF8(mi.m_szMCCPw);
  
    wsprintf(szFullAdminDN, NS_ADMIN_DOMAIN, mi.m_szAdminDomain);
	utf8AdminDomain = localToUTF8(szFullAdminDN);
	SetLdapHost(utf8MCCHost);
	SetLdapPort(mi.m_nMCCPort);
	SetLdapSuffix(utf8MCCSuffix);
	SetLdapUser(utf8MCCBindAs);
	SetLdapPassword(utf8MCCPw);
	SetLdapInstallDN(utf8AdminDomain);

	if( utf8MCCHost)     nsSetupFree(utf8MCCHost);
	if( utf8MCCSuffix)   nsSetupFree(utf8MCCSuffix);
	if( utf8MCCBindAs)   nsSetupFree(utf8MCCBindAs);
	if( utf8MCCPw)       nsSetupFree(utf8MCCPw);
	if( utf8AdminDomain) nsSetupFree(utf8AdminDomain);

}



//////////////////////////////////////////////////////////////////////////////
// _DialogProcs
//
// The dialog procedure for a single property page.  You will need to create
// one of these for each property page used in the property sheet.  This
// procedure processes dialog messages sent to your property page by Windows.
// See the Windows SDK documentation for more information about this function.
//


/////////////////////////////////////////////////////////////////////////////
// Setup8bitInputDisplay
//
// sets up dialog components to handle 8bit entry/display for I18n requirement
//  
// 
//

void Setup8bitInputDisplay(HWND hwndDlg, INT hControls[])
{
	INT i;

	for(i=0; hControls[i] != -1; i++)
	{
		SendDlgItemMessage (hwndDlg, hControls[i], WM_SETFONT, 
							(WPARAM) GetStockObject(DEFAULT_GUI_FONT) , MAKELPARAM(TRUE, 0)); 

	}

}



/////////////////////////////////////////////////////////////////////////////
// EnableControls
//
// toggles editable state of Fields
//  
// 
//

BOOL EnableControls(INT Controls[], HWND hwndDlg, BOOL bEnable)
{
	INT i;
	HWND hControl;

	for(i=0; Controls[i] != -1; i++)
	{

		hControl = GetDlgItem(hwndDlg,  Controls[i]);
		EnableWindow(hControl, bEnable);
	}

	return TRUE;
}

//////////////////////////////////////////////////////////////////////////////
// EnableLDAPURLSettingsFields
//
// toggles editable state of MCC Settings Fields
//  
// 
//

BOOL EnableLDAPURLSettingsFields(HWND hwndDlg, BOOL bEnable)
{
	INT LDAPURL_Controls[]={IDC_EDIT_HOST,
						IDC_EDIT_PORT,
						IDC_EDIT_SUFFIX,
						IDC_EDIT_BIND_AS,
						IDC_EDIT_PW, -1};

	EnableControls(LDAPURL_Controls, hwndDlg, bEnable);

	return TRUE;
}

//////////////////////////////////////////////////////////////////////////////
// EnableConsumerDNFields
//
// toggles editable state of Consumer DN Fields
//  
// 
//

BOOL EnableConsumerDNFields(HWND hwndDlg, BOOL bEnable)
{
	INT Consumer_DN_Controls[]={IDC_EDIT_CONSUMER_DN,
								IDC_EDIT_PASSWORD,
								IDC_EDIT_PASSWORD_AGAIN,
								-1};

	EnableControls(Consumer_DN_Controls, hwndDlg, bEnable);

	return TRUE;
}




//////////////////////////////////////////////////////////////////////////////
// 
// SaveDlgServerInfo
// 
// Gets host, port, suffix, bind as, pw from dialog
//  used for MCC Settings, UG Settings, Replication Agreement Dialogs
//  

void SaveDlgServerInfo(HWND hwndDlg, 
					  PSZ pszHost, 
					  INT *pnPort,
					  PSZ pszSuffix,
					  PSZ pszBindAs,
					  PSZ pszPw)
{
	BOOL bTrans;

	GetDlgItemText(hwndDlg, 
				   IDC_EDIT_HOST,
				   pszHost,
				   MAX_STR_SIZE);

	*pnPort = GetDlgItemInt(hwndDlg, 
							IDC_EDIT_PORT,
							&bTrans, 
							FALSE);

	GetDlgItemText(hwndDlg, 
				   IDC_EDIT_SUFFIX,
				   pszSuffix,
				   MAX_STR_SIZE);

	GetDlgItemText(hwndDlg, 
				   IDC_EDIT_BIND_AS,
				   pszBindAs,
				   MAX_STR_SIZE);

	GetDlgItemText(hwndDlg, 
				   IDC_EDIT_PW,
				   pszPw,
				   MAX_STR_SIZE);

}

//////////////////////////////////////////////////////////////////////////////
// 
// LoadDlgServerInfo
// 
// Sets host, port, suffix, bind as, pw from dialog
//  used for MCC Settings, UG Settings, Replication Agreement Dialogs
//  

void LoadDlgServerInfo(HWND hwndDlg, 
					  PSZ pszHost, 
					  INT nPort,
					  PSZ pszSuffix,
					  PSZ pszBindAs,
					  PSZ pszPw)
{
	BOOL bResult;

		  bResult = SetDlgItemText(hwndDlg, 
			                       IDC_EDIT_HOST,
				                   pszHost);
		  if(FALSE == bResult)
		  {
			DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }
	  
		  bResult = SetDlgItemInt(hwndDlg,
			                     IDC_EDIT_PORT,
								 nPort,
					             TRUE);
		  if(FALSE == bResult)
		  {
			DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }


		  bResult = SetDlgItemText(hwndDlg, 
			                       IDC_EDIT_SUFFIX,
				                   pszSuffix);
		  if(FALSE == bResult)
		  {
			DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }

		  bResult = SetDlgItemText(hwndDlg, 
			                       IDC_EDIT_BIND_AS,
				                   pszBindAs);
		  if(FALSE == bResult)
		  {
			DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }

		  bResult = SetDlgItemText(hwndDlg, 
			                       IDC_EDIT_PW,
				                   pszPw);
		  if(FALSE == bResult)
		  {
			DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }
}


//////////////////////////////////////////////////////////////////////////////
// VerifyServerInfo
//
// verifies Server settings are ok
//  
// Returns TRUE if there is an invalid setting
// Returns FALSE if all settings are valid
//
// SideEffect: displays error (or writes to log in silent mode)
 
BOOL VerifyServerInfo(   PSZ pszHost, 
						 INT *pnPort,
						 PSZ pszSuffix,
						 PSZ pszBindAs,
						 PSZ pszPw,
						 BOOL bVerifyBindAs)
{
	BOOL bValueReturned = TRUE;

	if ( 0 == strlen( pszHost) )
	{
		DSMessageBoxOK(ERR_NO_HOST_TITLE, ERR_NO_HOST, 0);
	} else if (  0 == *pnPort )
	{
		DSMessageBoxOK(ERR_NO_PORT_TITLE, ERR_NO_PORT, 0);
	} else if ( 0 == strlen( pszSuffix) )
	{
		DSMessageBoxOK(ERR_NO_SUFFIX_TITLE, ERR_NO_SUFFIX, 0);
	} else if ( !isValidDN(pszSuffix) )
	{
		/* error message displayed by isvalidDN */
	} else if ( 0 == strlen( pszBindAs) )
	{
		DSMessageBoxOK(ERR_NO_BIND_DN_TITLE, ERR_NO_BIND_DN, 0);
    }else if ( 0 == strlen( pszPw) )
	{
		DSMessageBoxOK(ERR_NO_PW_TITLE, ERR_NO_PW, 0);
	} else if (contains8BitChars(pszPw) )
	{

		DSMessageBoxOK(ERR_8BIT_PW_TITLE, ERR_8BIT_PW, 0);  
	} else
	{
		/* all settings look good */
		bValueReturned = FALSE;
	}

	/* try to verify valid dn if need to and no 
	   previous invalid fields */

	if( !bValueReturned && bVerifyBindAs )
	{
		/* error message displayed by isvalidDN */
		if ( !isValidDN(pszBindAs) )
		{
		    /* dn is invalid return true from here */
			bValueReturned = TRUE;
		}
	} 

	return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// SaveDialogInput_MCC_Settings
//
// save MCC settings entered on Dialog on back and next
//  
// 
//

void SaveDialogInput_MCC_Settings(HWND hwndDlg)
{
   
	if( 1 == mi.m_nExistingMCC )
	{
	   SaveDlgServerInfo(hwndDlg,
					     mi.m_szMCCHost, 
						 &mi.m_nMCCPort,
					     mi.m_szMCCSuffix,
						 mi.m_szMCCBindAs,
					     mi.m_szMCCPw);
	}
}

//////////////////////////////////////////////////////////////////////////////
// VerifyDialogInput_MCC_Settings
//
// verify MCC settings entered on Dialog on back and next
//  
// 
// Returns TRUE if there is an invalid setting
// Returns FALSE if all settings are valid
//
// SideEffect: displays error (or writes to log in silent mode)


BOOL Verify_MCC_Settings()
{
   BOOL bValueReturned = FALSE;

    /* only check if configuring to use existing MCC */
	if (1 == mi.m_nExistingMCC)
	{
		if( FALSE == ( bValueReturned = VerifyServerInfo(mi.m_szMCCHost, 
								   &mi.m_nMCCPort,
								   mi.m_szMCCSuffix,
								   mi.m_szMCCBindAs,
								   mi.m_szMCCPw,
								   FALSE) ) )
		{
		   /* server info ok, now check rest of the settigns */
			if (FALSE == FullyQualifyHostName(mi.m_szMCCHost) )
			{
			    /* can't qualify host name, must be invalid */
             	DSMessageBoxOK(ERR_INVALID_HOST_TITLE, ERR_INVALID_HOST,
							   mi.m_szMCCHost, mi.m_szMCCHost);
				bValueReturned = TRUE;               
			} else
			{
				/* now that all settings entered, check to see if valid user */
				if (FALSE == (UTF8IsValidLdapUser( mi.m_szMCCHost, 
												   mi.m_nMCCPort, 
												   mi.m_szMCCSuffix, 
												   mi.m_szMCCBindAs, 
												   mi.m_szMCCPw, 
												   FALSE) ) )
				{
					DSMessageBoxOK(ERR_CANT_FIND_DS_TITLE,
								   ERR_CANT_FIND_DS, 0,
								   mi.m_szMCCHost,
								   mi.m_nMCCPort,
								   mi.m_szMCCBindAs);
					bValueReturned = TRUE;
				}
				/* now that all settings entered, find admin domain */
				else if ((CUSTOMMODE != MODE) &&
						 (FALSE == (UTF8IsValidAdminDomain( mi.m_szMCCHost, 
												   mi.m_nMCCPort, 
												   mi.m_szMCCSuffix, 
												   mi.m_szAdminDomain, 
												   mi.m_szMCCBindAs, 
												   mi.m_szMCCPw) ) ) )
				{
					DSMessageBoxOK(ERR_CANT_FIND_ADMIN_DOMAIN_TITLE,
								   ERR_CANT_FIND_ADMIN_DOMAIN, mi.m_szAdminDomain,
								   mi.m_szAdminDomain,
								   mi.m_szMCCHost,
								   mi.m_nMCCPort,
								   mi.m_szMCCBindAs);
					bValueReturned = TRUE;
				}

				/* all settings good */

				/* don't want to cfg sspt if already have mcc user */
				mi.m_nCfgSspt = 0;
			}

		}

	}

	return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// MCC_Settings_DialogProc
//
// dialog proc to choose MCC server settings
//  
// 
//

static BOOL CALLBACK
MCC_Settings_DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  BOOL bValueReturned = FALSE;
  UINT uResult        = 0;
  BOOL bResult        = FALSE;

  /* list of controls to be setup for 8bit input/display */
  INT h8bitControls[]={IDC_EDIT_HOST,
						IDC_EDIT_SUFFIX,
						IDC_EDIT_BIND_AS,
						IDC_EDIT_PW, -1};
  switch (uMsg) 
  {
    case WM_INITDIALOG:
      // This message is sent when the property page is first created.  Here
      // you can perform any one time initialization that you require. 

		 Setup8bitInputDisplay(hwndDlg, h8bitControls);

		 ShowWindow(GetDlgItem(hwndDlg, IDC_EDIT_SUFFIX), FALSE);
		 ShowWindow(GetDlgItem(hwndDlg, IDC_STATIC_MCC_SUFFIX), FALSE);

         break;


    case WM_COMMAND:
      // Windows sends WM_COMMAND messages whenever the user clicks on
      // a control in your property page.  If you need to perform some
      // special action, such as validating data or responding to a
      // button click, do it here.
	
		if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_RADIO_USE_EXISTING_SERVER) )
		{
			EnableLDAPURLSettingsFields(hwndDlg, TRUE);
			mi.m_nExistingMCC = 1;
		}else if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_RADIO_USE_THIS_SERVER) ){
			EnableLDAPURLSettingsFields(hwndDlg, FALSE);
			mi.m_nExistingMCC = 0;
		}

      break;

    case WM_NOTIFY:
      // Windows sends WM_NOTIFY messages to your property page whenever
      // something interesting happens to the page.  This could be page
      // activation/deactivation, a button click, etc.  The wParam parameter
      // contains a pointer to the property page.  The lParam parameter
      // contains a pointer to an NMHDR structure.  The code field of this
      // structure contains the notification message code being sent.  The
      // property sheet API allows you to alter the behavior of these
      // messages by returning a value for each message.  To return a value,
      // use the SetWindowLong Windows SDK function.

      switch (((NMHDR*)lParam)->code) 
      {

        case PSN_SETACTIVE:
          // This notification is sent upon activation of the property page.
          // The property sheet should be centered each time it is activated
          // in case the user has moved the stupid thing (this duplicates
          // InstallShield functionality). You should also set the state of
          // the wizard buttons here.
          //
          // NOTE: If you do not wish this page to become active, return -1.
		  LoadDlgServerInfo(hwndDlg,
							mi.m_szMCCHost,
							mi.m_nMCCPort,
							mi.m_szMCCSuffix,
							mi.m_szMCCBindAs,
							mi.m_szMCCPw);
		  
		  CheckRadioButton(hwndDlg, IDC_RADIO_USE_THIS_SERVER, 
									IDC_RADIO_USE_EXISTING_SERVER, 
							( (1 == mi.m_nExistingMCC) ? IDC_RADIO_USE_EXISTING_SERVER : 
									IDC_RADIO_USE_THIS_SERVER));

	      if(1 == mi.m_nExistingMCC)
		  {
			EnableLDAPURLSettingsFields(hwndDlg, TRUE);
		  }else{
			EnableLDAPURLSettingsFields(hwndDlg, FALSE);
		  }

          CenterWindow(GetParent(hwndDlg));
          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_KILLACTIVE:
          // This notification is sent upon deactivation of the property page.
          // Here you can do whatever might be necessary for this action, such
          // as saving the state of the controls.  You should also reset the
          // the state of the wizard buttons here, as both the Back and Next
          // buttons should be active when you leave the AskOptions function.
          //
          // NOTE: If you do not want the page deactivated, return -1.

          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_WIZBACK:
          // The user clicked the back button from the first property page.
          // Set the result code NS_WIZBACK to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the first property sheet, you should ignore this
          // notification and simply let windows go to the previous page.
          //
          // NOTE: To prevent the wizard from stepping back, return -1.

		   SaveDialogInput_MCC_Settings(hwndDlg);
  
		   /* first dialog, so send wizback to previous module */
		   mi.m_nResult = NS_WIZBACK;
           SendMessage(GetParent(hwndDlg), WM_CLOSE, 0, 0);

          break;

        case PSN_WIZNEXT:
          // The user clicked the next button from the last property page.
          // Set the result code NS_WIZNEXT to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the last property sheet, you should ignore this
          // notification and simply let windows go to the next page.
          //
          // NOTE: To prevent the wizard from stepping ahead, return -1.

  	      SaveDialogInput_MCC_Settings(hwndDlg);
          if( TRUE == (bValueReturned = Verify_MCC_Settings() ) )
		  {
			  // one of the settings was invalid so stay on this page
			  SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
		  }

	      break;

        case PSN_QUERYCANCEL:
          // This notification is sent when the user clicks the Cancel button.
          // It is also sent in response to the WM_CLOSE messages issued
          // by PSN_WIZBACK and PSN_WIZNEXT.  Make sure that we only process
          // this message if the result is not back or next so that we don't
          // nuke the return value assigned by PSN_WIZBACK or PSN_WIZNEXT.
          //
          // NOTE: To prevent the cancel from occuring, return -1.

          if (mi.m_nResult != NS_WIZBACK && mi.m_nResult != NS_WIZNEXT)
          {
            if (QueryExit(hwndDlg)) 
            {
              mi.m_nResult = NS_WIZCANCEL;
            }
            else
            {
              SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
              bValueReturned = TRUE;
            }
          }
          break;
      }
      break;
  }

  return bValueReturned;
}


//////////////////////////////////////////////////////////////////////////////
// 
// SaveDialogInput_ReInstall
// 
// saves settings entered in ReInstall Dlg
//  

void SaveDialogInput_ReInstall(HWND hwndDlg)
{

	GetDlgItemText(hwndDlg, 
				   IDC_EDIT_BIND_AS,
				   mi.m_szMCCBindAs,
				   MAX_STR_SIZE);

	GetDlgItemText(hwndDlg, 
				   IDC_EDIT_PW,
				   mi.m_szMCCPw,
				   MAX_STR_SIZE);

}

//////////////////////////////////////////////////////////////////////////////
// Verify_ReInstall
//
// verify ReInstall settings entered on Dialog on back and next
//  
// 
// Returns TRUE if there is an invalid setting
// Returns FALSE if all settings are valid
//
// SideEffect: displays error (or writes to log in silent mode)


BOOL Verify_ReInstall()
{
	BOOL bValueReturned = FALSE;

	/* Get URL components so we can verify all MCC settings */
	if( GetURLComponents(mi.m_szLdapURL, mi.m_szMCCHost, 
						 &mi.m_nMCCPort, mi.m_szMCCSuffix) != 0)
	{
		/* error Getting URL Components*/
		DSMessageBoxOK(ERR_NO_CONFIG_URL_TITLE, ERR_NO_CONFIG_URL, 0);
		bValueReturned = TRUE;
	}else{

	    /* since we have all MCC info, 
		   pass it thorugh Verify_MCC_Settings 
		   to do all the same verifications.*/

		/* set this to one so MCC Settings get checked */
	    mi.m_nExistingMCC = 1;

		bValueReturned = Verify_MCC_Settings();
	}

	return bValueReturned;
}


//////////////////////////////////////////////////////////////////////////////
// ReInstall Dialog Proc
//
// ask configuration information needed on reinstall
// 
// 
// 
//

static BOOL CALLBACK
ReInstall_DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  BOOL bValueReturned = FALSE;
  UINT uResult        = 0;
  BOOL bResult        = FALSE;
    
   /* list of controls to be setup for 8bit input/display */
  INT h8bitControls[]={IDC_EDIT_BIND_AS,
					   -1}; 
  
  switch (uMsg) 
  {
    case WM_INITDIALOG:
      // This message is sent when the property page is first created.  Here
      // you can perform any one time initialization that you require. 
	  Setup8bitInputDisplay(hwndDlg, h8bitControls);

      break;


    case WM_COMMAND:
      // Windows sends WM_COMMAND messages whenever the user clicks on
      // a control in your property page.  If you need to perform some
      // special action, such as validating data or responding to a
      // button click, do it here.
      
      break;

    case WM_NOTIFY:
      // Windows sends WM_NOTIFY messages to your property page whenever
      // something interesting happens to the page.  This could be page
      // activation/deactivation, a button click, etc.  The wParam parameter
      // contains a pointer to the property page.  The lParam parameter
      // contains a pointer to an NMHDR structure.  The code field of this
      // structure contains the notification message code being sent.  The
      // property sheet API allows you to alter the behavior of these
      // messages by returning a value for each message.  To return a value,
      // use the SetWindowLong Windows SDK function.

      switch (((NMHDR*)lParam)->code) 
      {
          char * szLdapUrl;

        case PSN_SETACTIVE:
          
          // This notification is sent upon activation of the property page.
          // The property sheet should be centered each time it is activated
          // in case the user has moved the stupid thing (this duplicates
          // InstallShield functionality). You should also set the state of
          // the wizard buttons here.
          //
          // NOTE: If you do not wish this page to become active, return -1.

			/* This dialog is displayed in the following ways */
			/*		Creating MCC Custom & normal mode ( after MCC admin page) */
			/*	or															  */
			/*		Using existing MCC Custom Mode Only  (after MCC Settings Page ) */
      
      szLdapUrl = stripConfigLdapURL(mi.m_szLdapURL);
			bResult = SetDlgItemText(hwndDlg, 
			                       IDC_CONFIG_URL_VAL,
		                           szLdapUrl);
		    if(FALSE == bResult)
			{
		        DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
			}

			bResult = SetDlgItemText(hwndDlg, 
			                       IDC_EDIT_BIND_AS,
		                           mi.m_szMCCBindAs);
		    if(FALSE == bResult)
			{
				DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
			}

			bResult = SetDlgItemText(hwndDlg, 
			                       IDC_EDIT_PW,
		                           mi.m_szMCCPw);
		    if(FALSE == bResult)
			{
				DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
			}
		
			CenterWindow(GetParent(hwndDlg));
            PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_KILLACTIVE:
          // This notification is sent upon deactivation of the property page.
          // Here you can do whatever might be necessary for this action, such
          // as saving the state of the controls.  You should also reset the
          // the state of the wizard buttons here, as both the Back and Next
          // buttons should be active when you leave the AskOptions function.
          //
          // NOTE: If you do not want the page deactivated, return -1.

          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_WIZBACK:
          // The user clicked the back button from the first property page.
          // Set the result code NS_WIZBACK to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the first property sheet, you should ignore this
          // notification and simply let windows go to the previous page.
          //
          // NOTE: To prevent the wizard from stepping back, return -1.
		  
		  SaveDialogInput_ReInstall(hwndDlg);
			
		  // this is only dialog, so set back to go to other module */

		  mi.m_nResult = NS_WIZBACK;
		  SendMessage(GetParent(hwndDlg), WM_CLOSE, 0, 0);
			
		  break;

        case PSN_WIZNEXT:
          // The user clicked the next button from the last property page.
          // Set the result code NS_WIZNEXT to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the last property sheet, you should ignore this
          // notification and simply let windows go to the next page.
          //
          // NOTE: To prevent the wizard from stepping ahead, return -1.
		
			SaveDialogInput_ReInstall(hwndDlg);
		    if (TRUE == (bValueReturned = Verify_ReInstall() ) )
			{  
			    /* setting is invalid stay on this page */
				SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
			}else{

				/* everything looks ok */
 		   
				// this is only dialog, so set back to go to other module */
				mi.m_nResult = NS_WIZNEXT;
				SendMessage(GetParent(hwndDlg), WM_CLOSE, 0, 0);
			}

		  break;

        case PSN_QUERYCANCEL:
          // This notification is sent when the user clicks the Cancel button.
          // It is also sent in response to the WM_CLOSE messages issued
          // by PSN_WIZBACK and PSN_WIZNEXT.  Make sure that we only process
          // this message if the result is not back or next so that we don't
          // nuke the return value assigned by PSN_WIZBACK or PSN_WIZNEXT.
          //
          // NOTE: To prevent the cancel from occuring, return -1.

          if (mi.m_nResult != NS_WIZBACK && mi.m_nResult != NS_WIZNEXT)
          {
            if (QueryExit(hwndDlg)) 
            {
              mi.m_nResult = NS_WIZCANCEL;
            }
            else
            {
              SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
              bValueReturned = TRUE;
            }
          }
          break;
      }
      break;
  }

  return bValueReturned;
}


//////////////////////////////////////////////////////////////////////////////
// 
//	SaveDialogInput_AdminDomain
// 
// 
//  save settings entered in admin domain dialog proc
// 
//

void SaveDialogInput_AdminDomain(HWND hwndDlg)
{
	GetDlgItemText(hwndDlg, 
				   IDC_EDIT_ADMIN_DOMAIN,
				   mi.m_szAdminDomain,
				   MAX_STR_SIZE);

}

//////////////////////////////////////////////////////////////////////////////
// 
//	Verify_AdminDomain
// 
// 
// verify and save settings entered in admin domain dialog proc
// 
// Returns TRUE if there is an invalid setting
// Returns False if all settings are valid
//
// SideEffect: displays error (or writes to log in silent mode)

BOOL VerifyAdminDomain()
{
	BOOL bValueReturned = TRUE;

	if(0 == strlen(mi.m_szAdminDomain) ) 
	{
		DSMessageBoxOK(ERR_NO_ADMIN_DOMAIN_TITLE, ERR_NO_ADMIN_DOMAIN, 0);
	}else if(isAValidDN(mi.m_szAdminDomain) ){
		/* admin domain is not allowed to be a DN, so if it is
			prompt user and return error */
		DSMessageBoxOK(ERR_ADMIN_DOMAIN_DN_TITLE, ERR_ADMIN_DOMAIN_DN,
					   mi.m_szAdminDomain, mi.m_szAdminDomain);
	}else if (0 == mi.m_nExistingMCC){
		/* we are creating the Config Directory, so we don't need to check if
		   the admin domain is present */
		bValueReturned = FALSE;
	/* now that all settings entered, find admin domain */
	}else if (FALSE == (UTF8IsValidAdminDomain( mi.m_szMCCHost, 
												mi.m_nMCCPort, 
												mi.m_szMCCSuffix, 
												mi.m_szAdminDomain, 
												mi.m_szMCCBindAs, 
												mi.m_szMCCPw) ) )
	{
		DSMessageBoxOK(ERR_CANT_FIND_ADMIN_DOMAIN_TITLE,
					   ERR_CANT_FIND_ADMIN_DOMAIN, mi.m_szAdminDomain,
					   mi.m_szAdminDomain,
					   mi.m_szMCCHost,
					   mi.m_nMCCPort,
					   mi.m_szMCCBindAs);
		bValueReturned = TRUE;
	}else{
	   /* all settings ok, return false */
	   bValueReturned = FALSE;
	}

	return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// Admin Domain Dialog Proc
//
// dialog proc to ask for admin domain
// 
// 
// 
//

static BOOL CALLBACK
AdminDomain_DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  BOOL bValueReturned = FALSE;
  UINT uResult        = 0;
  BOOL bResult        = FALSE;
    
   /* list of controls to be setup for 8bit input/display */
  INT h8bitControls[]={IDC_EDIT_ADMIN_DOMAIN
					   -1}; 
  switch (uMsg) 
  {
    case WM_INITDIALOG:
      // This message is sent when the property page is first created.  Here
      // you can perform any one time initialization that you require. 
	  Setup8bitInputDisplay(hwndDlg, h8bitControls);

      break;


    case WM_COMMAND:
      // Windows sends WM_COMMAND messages whenever the user clicks on
      // a control in your property page.  If you need to perform some
      // special action, such as validating data or responding to a
      // button click, do it here.
      
      break;

    case WM_NOTIFY:
      // Windows sends WM_NOTIFY messages to your property page whenever
      // something interesting happens to the page.  This could be page
      // activation/deactivation, a button click, etc.  The wParam parameter
      // contains a pointer to the property page.  The lParam parameter
      // contains a pointer to an NMHDR structure.  The code field of this
      // structure contains the notification message code being sent.  The
      // property sheet API allows you to alter the behavior of these
      // messages by returning a value for each message.  To return a value,
      // use the SetWindowLong Windows SDK function.

      switch (((NMHDR*)lParam)->code) 
      {

	  case PSN_SETACTIVE:
		  // This notification is sent upon activation of the property page.
		  // The property sheet should be centered each time it is activated
		  // in case the user has moved the stupid thing (this duplicates
          // InstallShield functionality). You should also set the state of
          // the wizard buttons here.
          //
          // NOTE: If you do not wish this page to become active, return -1.

		  /* This dialog is displayed in the following ways */
		  /*		Creating MCC Custom & normal mode ( after MCC admin page) */
		  /*	or															  */
		  /*		Using existing MCC Custom Mode Only  (after MCC Settings Page ) */

		
		  if(1 == mi.m_nExistingMCC )
		  {
			  /* not creating an MCC so don't ask this page */
			  SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
			  return TRUE;
		  }
		
		  bResult = SetDlgItemText(hwndDlg, 
			                       IDC_EDIT_ADMIN_DOMAIN,
		                           mi.m_szAdminDomain);
		  if(FALSE == bResult)
		  {
			  DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }


		  CenterWindow(GetParent(hwndDlg));
		  PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

	  case PSN_KILLACTIVE:
          // This notification is sent upon deactivation of the property page.
          // Here you can do whatever might be necessary for this action, such
          // as saving the state of the controls.  You should also reset the
          // the state of the wizard buttons here, as both the Back and Next
          // buttons should be active when you leave the AskOptions function.
          //
          // NOTE: If you do not want the page deactivated, return -1.

          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

	  case PSN_WIZBACK:
          // The user clicked the back button from the first property page.
          // Set the result code NS_WIZBACK to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the first property sheet, you should ignore this
          // notification and simply let windows go to the previous page.
          //
          // NOTE: To prevent the wizard from stepping back, return -1.
		  SaveDialogInput_AdminDomain(hwndDlg);

		  break;

	  case PSN_WIZNEXT:
          // The user clicked the next button from the last property page.
          // Set the result code NS_WIZNEXT to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the last property sheet, you should ignore this
          // notification and simply let windows go to the next page.
          //
          // NOTE: To prevent the wizard from stepping ahead, return -1.
		  SaveDialogInput_AdminDomain(hwndDlg);
		  if (TRUE == (bValueReturned = VerifyAdminDomain() ) )
		  {  
			  /* setting is invalid stay on this page */
			  SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
		  }

		  break;

	  case PSN_QUERYCANCEL:
          // This notification is sent when the user clicks the Cancel button.
          // It is also sent in response to the WM_CLOSE messages issued
          // by PSN_WIZBACK and PSN_WIZNEXT.  Make sure that we only process
          // this message if the result is not back or next so that we don't
          // nuke the return value assigned by PSN_WIZBACK or PSN_WIZNEXT.
          //
          // NOTE: To prevent the cancel from occuring, return -1.

          if (mi.m_nResult != NS_WIZBACK && mi.m_nResult != NS_WIZNEXT)
          {
			  if (QueryExit(hwndDlg)) 
			  {
				  mi.m_nResult = NS_WIZCANCEL;
			  }
			  else
			  {
				  SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
				  bValueReturned = TRUE;
			  }
          }
          break;
      }
      break;
  }

  return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// Admin Domain Custom Dialog Proc
//
// dialog proc to ask for admin domain in custom mode when we are installing
// into existing MCC
// 
// basically the same as Admin Domain with some added code to lookup domain
// kind of lame to duplicate code, but this was the best way to get flow to match unix
//

static BOOL CALLBACK
AdminDomainCustom_DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  BOOL bValueReturned = FALSE;
  UINT uResult        = 0;
  BOOL bResult        = FALSE;
    
   /* list of controls to be setup for 8bit input/display */
  INT h8bitControls[]={IDC_EDIT_ADMIN_DOMAIN
					   -1}; 
  switch (uMsg) 
  {
    case WM_INITDIALOG:
      // This message is sent when the property page is first created.  Here
      // you can perform any one time initialization that you require. 
	  Setup8bitInputDisplay(hwndDlg, h8bitControls);

      break;


    case WM_COMMAND:
      // Windows sends WM_COMMAND messages whenever the user clicks on
      // a control in your property page.  If you need to perform some
      // special action, such as validating data or responding to a
      // button click, do it here.
      
      break;

    case WM_NOTIFY:
      // Windows sends WM_NOTIFY messages to your property page whenever
      // something interesting happens to the page.  This could be page
      // activation/deactivation, a button click, etc.  The wParam parameter
      // contains a pointer to the property page.  The lParam parameter
      // contains a pointer to an NMHDR structure.  The code field of this
      // structure contains the notification message code being sent.  The
      // property sheet API allows you to alter the behavior of these
      // messages by returning a value for each message.  To return a value,
      // use the SetWindowLong Windows SDK function.

      switch (((NMHDR*)lParam)->code) 
      {

	  case PSN_SETACTIVE:
          // This notification is sent upon activation of the property page.
          // The property sheet should be centered each time it is activated
          // in case the user has moved the stupid thing (this duplicates
          // InstallShield functionality). You should also set the state of
          // the wizard buttons here.
          //
          // NOTE: If you do not wish this page to become active, return -1.

		  /* This dialog is displayed in the following ways */
		  /*		Creating MCC Custom & normal mode ( after MCC admin page) */
		  /*	or															  */
		  /*		Using existing MCC Custom Mode Only  (after MCC Settings Page ) */

		
		  /* only display this page in Custom Mode if installing into existing MCC */
		  if( (0 == mi.m_nExistingMCC) || CUSTOMMODE != MODE)
		  {
			  /* dont display this page */
			  SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
			  return TRUE;
		  }
	
		  /* since we are installing into existing mcc we can search it for admin domains */
			
		  /* stevross: add code to search for admin domains here */

		
		  bResult = SetDlgItemText(hwndDlg, 
			                       IDC_EDIT_ADMIN_DOMAIN,
		                           mi.m_szAdminDomain);
		  if(FALSE == bResult)
		  {
			  DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }


		  CenterWindow(GetParent(hwndDlg));
		  PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

	  case PSN_KILLACTIVE:
          // This notification is sent upon deactivation of the property page.
          // Here you can do whatever might be necessary for this action, such
          // as saving the state of the controls.  You should also reset the
          // the state of the wizard buttons here, as both the Back and Next
          // buttons should be active when you leave the AskOptions function.
          //
          // NOTE: If you do not want the page deactivated, return -1.

          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

	  case PSN_WIZBACK:
          // The user clicked the back button from the first property page.
          // Set the result code NS_WIZBACK to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the first property sheet, you should ignore this
          // notification and simply let windows go to the previous page.
          //
          // NOTE: To prevent the wizard from stepping back, return -1.
   		  SaveDialogInput_AdminDomain(hwndDlg);
		  break;

	  case PSN_WIZNEXT:
          // The user clicked the next button from the last property page.
          // Set the result code NS_WIZNEXT to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the last property sheet, you should ignore this
          // notification and simply let windows go to the next page.
          //
          // NOTE: To prevent the wizard from stepping ahead, return -1.
   		  SaveDialogInput_AdminDomain(hwndDlg);
  		  if (TRUE == (bValueReturned = VerifyAdminDomain() ) )
		  {
			  /* setting is invalid stay on this page */
			  SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
	      }
		  break;

	  case PSN_QUERYCANCEL:
          // This notification is sent when the user clicks the Cancel button.
          // It is also sent in response to the WM_CLOSE messages issued
          // by PSN_WIZBACK and PSN_WIZNEXT.  Make sure that we only process
          // this message if the result is not back or next so that we don't
          // nuke the return value assigned by PSN_WIZBACK or PSN_WIZNEXT.
          //
          // NOTE: To prevent the cancel from occuring, return -1.

          if (mi.m_nResult != NS_WIZBACK && mi.m_nResult != NS_WIZNEXT)
          {
			  if (QueryExit(hwndDlg)) 
			  {
				  mi.m_nResult = NS_WIZCANCEL;
			  }
			  else
			  {
				  SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
				  bValueReturned = TRUE;
			  }
          }
          break;
      }
      break;
  }

  return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// 
//  SaveDialogInput_UG_Settings(HWND hwndDlg)
// 
//  Save values entered in UG dialog on back and next
// 
//

void SaveDialogInput_UG_Settings(HWND hwndDlg)
{

	if (1 == mi.m_nExistingUG)
	{
		SaveDlgServerInfo(hwndDlg,
						 mi.m_szUGHost, 
						 &mi.m_nUGPort,
						 mi.m_szUGSuffix,
						 mi.m_szUserGroupAdmin,
						 mi.m_szUserGroupAdminPW);
	}

}

//////////////////////////////////////////////////////////////////////////////
// 
// Verify_UG_Settings()
// 
//  verify values entered in UG dialog on back and next
// 
//

BOOL Verify_UG_Settings()
{
	BOOL bValueReturned = FALSE;

	/* only verify if installing into existing UG */
	if (1 == mi.m_nExistingUG)
	{
		bValueReturned = VerifyServerInfo(mi.m_szUGHost, 
										  &mi.m_nUGPort,
										  mi.m_szUGSuffix,
										  mi.m_szUserGroupAdmin,
										  mi.m_szUserGroupAdminPW,
										  FALSE);

		if (FALSE == bValueReturned)
		{
			/* server info looks ok, now check the rest of the stuff */

			if (FALSE == FullyQualifyHostName(mi.m_szUGHost) )
			{
				/* failed to fully qualify host name */
				DSMessageBoxOK(ERR_INVALID_HOST_TITLE,
							   ERR_INVALID_HOST,
							   mi.m_szUGHost, mi.m_szUGHost);
				bValueReturned = TRUE;                   
			} else
			{
				/* now that all settings entered, check to see if valid user */
				if (FALSE == UTF8IsValidLdapUser(mi.m_szUGHost, mi.m_nUGPort, 
												 mi.m_szUGSuffix,
												 mi.m_szUserGroupAdmin,
												 mi.m_szUserGroupAdminPW,
												 FALSE) )
				{

					DSMessageBoxOK(ERR_CANT_FIND_DS_TITLE,
								   ERR_CANT_FIND_DS, 0, mi.m_szUGHost,
								   mi.m_nUGPort, mi.m_szUserGroupAdmin);
					bValueReturned = TRUE;
				} else
				{
					/* all settings good */
					/* set UG LDAP URL */
					sprintf(mi.m_szUserGroupURL, "ldap://%s:%d/%s",
							mi.m_szUGHost, mi.m_nUGPort, mi.m_szUGSuffix);
				}
			}
		}
	}
	return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// UG_Settings_DialogProc
//
// dialog proc to choose User Group server settings
//  
// 
//

static BOOL CALLBACK
UG_Settings_DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  BOOL bValueReturned = FALSE;
  UINT uResult        = 0;
  BOOL bResult        = FALSE;
  CHAR szTemp[MAX_STR_SIZE]	= {0};

  /* list of controls to be setup for 8bit input/display */
  INT h8bitControls[]={IDC_EDIT_HOST,
						IDC_EDIT_SUFFIX,
						IDC_EDIT_BIND_AS,
						IDC_EDIT_PW, -1};

  
  switch (uMsg) 
  {
    case WM_INITDIALOG:
      // This message is sent when the property page is first created.  Here
      // you can perform any one time initialization that you require. 

		 Setup8bitInputDisplay(hwndDlg, h8bitControls);

         break;


    case WM_COMMAND:
      // Windows sends WM_COMMAND messages whenever the user clicks on
      // a control in your property page.  If you need to perform some
      // special action, such as validating data or responding to a
      // button click, do it here.
	
		if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_RADIO_USE_EXISTING_SERVER) )
		{
			EnableLDAPURLSettingsFields(hwndDlg, TRUE);
			mi.m_nExistingUG = 1;
		}else if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_RADIO_USE_THIS_SERVER) ){
			EnableLDAPURLSettingsFields(hwndDlg, FALSE);
			mi.m_nExistingUG = 0;
		}


      break;

    case WM_NOTIFY:
      // Windows sends WM_NOTIFY messages to your property page whenever
      // something interesting happens to the page.  This could be page
      // activation/deactivation, a button click, etc.  The wParam parameter
      // contains a pointer to the property page.  The lParam parameter
      // contains a pointer to an NMHDR structure.  The code field of this
      // structure contains the notification message code being sent.  The
      // property sheet API allows you to alter the behavior of these
      // messages by returning a value for each message.  To return a value,
      // use the SetWindowLong Windows SDK function.

      switch (((NMHDR*)lParam)->code) 
      {

        case PSN_SETACTIVE:
          // This notification is sent upon activation of the property page.
          // The property sheet should be centered each time it is activated
          // in case the user has moved the stupid thing (this duplicates
          // InstallShield functionality). You should also set the state of
          // the wizard buttons here.
          //
          // NOTE: If you do not wish this page to become active, return -1.
	
		  /* setup static text with user and group strings */		

		  /* don't want to show this page if not installing into an existing MCC */
		  if( 1 == mi.m_nExistingMCC)
		  {
		      /* dont display this page */
			  SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
			  return TRUE;
		  }

	      LoadString( mi.m_hModule, IDS_UG_DESC, szTemp, MAX_STR_SIZE);
		 		  
		  bResult = SetDlgItemText(hwndDlg, 
		                           IDC_STATIC_DESC,
		                           szTemp);
		  if(FALSE == bResult)
		  {
	          DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }

	      LoadString( mi.m_hModule, IDS_UG_GB_DESC, szTemp, MAX_STR_SIZE);
		 		  
		  bResult = SetDlgItemText(hwndDlg, 
		                           IDC_STATIC_SETTINGS,
		                           szTemp);
		  if(FALSE == bResult)
		  {
	          DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }

		  

	      LoadString( mi.m_hModule, IDS_UG_RADIO_CREATE, szTemp, MAX_STR_SIZE);
		 		  
		  bResult = SetDlgItemText(hwndDlg, 
		                           IDC_RADIO_USE_THIS_SERVER,
		                           szTemp);
		  if(FALSE == bResult)
		  {
	          DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }

	      LoadString( mi.m_hModule, IDS_UG_RADIO_EXIST, szTemp, MAX_STR_SIZE);
		 		  
		  bResult = SetDlgItemText(hwndDlg, 
		                           IDC_RADIO_USE_EXISTING_SERVER,
		                           szTemp);
		  if(FALSE == bResult)
		  {
	          DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }

		  /* set defaults for text edit fields in this dialog */
		  LoadDlgServerInfo(hwndDlg,
							mi.m_szUGHost,
							mi.m_nUGPort,
							mi.m_szUGSuffix,
							mi.m_szUserGroupAdmin,
							mi.m_szUserGroupAdminPW);
	
				
		  CheckRadioButton(hwndDlg, IDC_RADIO_USE_THIS_SERVER, 
									IDC_RADIO_USE_EXISTING_SERVER, 
							( (1 == mi.m_nExistingUG) ? IDC_RADIO_USE_EXISTING_SERVER : 
									IDC_RADIO_USE_THIS_SERVER));
		  
	      if(1 == mi.m_nExistingUG)
		  {
			EnableLDAPURLSettingsFields(hwndDlg, TRUE);
		  }else{
			EnableLDAPURLSettingsFields(hwndDlg, FALSE);
		  }


          CenterWindow(GetParent(hwndDlg));
          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_KILLACTIVE:
          // This notification is sent upon deactivation of the property page.
          // Here you can do whatever might be necessary for this action, such
          // as saving the state of the controls.  You should also reset the
          // the state of the wizard buttons here, as both the Back and Next
          // buttons should be active when you leave the AskOptions function.
          //
          // NOTE: If you do not want the page deactivated, return -1.

          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_WIZBACK:
          // The user clicked the back button from the first property page.
          // Set the result code NS_WIZBACK to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the first property sheet, you should ignore this
          // notification and simply let windows go to the previous page.
          //
          // NOTE: To prevent the wizard from stepping back, return -1.
          SaveDialogInput_UG_Settings(hwndDlg);
          break;

        case PSN_WIZNEXT:
          // The user clicked the next button from the last property page.
          // Set the result code NS_WIZNEXT to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the last property sheet, you should ignore this
          // notification and simply let windows go to the next page.
          //
          // NOTE: To prevent the wizard from stepping ahead, return -1.
          SaveDialogInput_UG_Settings(hwndDlg);
		  if(TRUE == (bValueReturned = Verify_UG_Settings() ) )
		  {
			 /* one of the settings was invalid */
             SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
  	      }
	      break;

        case PSN_QUERYCANCEL:
          // This notification is sent when the user clicks the Cancel button.
          // It is also sent in response to the WM_CLOSE messages issued
          // by PSN_WIZBACK and PSN_WIZNEXT.  Make sure that we only process
          // this message if the result is not back or next so that we don't
          // nuke the return value assigned by PSN_WIZBACK or PSN_WIZNEXT.
          //
          // NOTE: To prevent the cancel from occuring, return -1.

          if (mi.m_nResult != NS_WIZBACK && mi.m_nResult != NS_WIZNEXT)
          {
            if (QueryExit(hwndDlg)) 
            {
              mi.m_nResult = NS_WIZCANCEL;
            }
            else
            {
              SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
              bValueReturned = TRUE;
            }
          }
          break;
      }
      break;
  }

  return bValueReturned;
}


//////////////////////////////////////////////////////////////////////////////
// SaveDialogInput_Server_Settings
//
// save settings entered in Server_Setting_DialogProc 
//  called on back and next
// 
// 
//
void SaveDialogInput_Server_Settings(HWND hwndDlg)
{
   	BOOL bResult        = FALSE;

	GetDlgItemText(hwndDlg,
				   IDC_EDIT_SERVER_IDENTIFIER,
				   mi.m_szServerIdentifier,
				   MAX_STR_SIZE);

	/* get the suffix */
	GetDlgItemText(hwndDlg,
				   IDC_EDIT_SUFFIX,
				   mi.m_szInstanceSuffix,
				   MAX_STR_SIZE);

	mi.m_nInstanceServerPort = (int ) GetDlgItemInt(hwndDlg,
													IDC_EDIT_SERVER_PORT,
													&bResult,
													TRUE);
}

//////////////////////////////////////////////////////////////////////////////
// Verify_Server_Settings
//
// verify settings entered in Server_Setting_DialogProc
// 
// Returns TRUE if there is an invalid setting
// Returns FALSE if all settings are valid
//
// SideEffect: displays error (or writes to log in silent mode)

BOOL Verify_Server_Settings()
{
	BOOL bValueReturned = TRUE;

	if ( 0 == strlen(mi.m_szServerIdentifier) )
	{
		/* no value entered for server id */
		DSMessageBoxOK(ERR_NO_SERVER_ID_TITLE, ERR_NO_SERVER_ID, 0);
	} else if (FALSE == isValidServerID(mi.m_szServerIdentifier) )
	{
		/* server id is invalid */
		/* error reported by isValidServerID */   
	} else if (0 == strlen(mi.m_szInstanceSuffix) && !mi.m_nExistingUG )
	{
		/* ok not to specify suffix when using existing UG hence above
		   otherwise it must be specified */
	    /* no value entered for suffix */
		DSMessageBoxOK(ERR_NO_SUFFIX_TITLE, ERR_NO_SUFFIX, 0);
    }else if (!mi.m_nExistingUG && !isValidDN(mi.m_szInstanceSuffix) )
	{
		/* don't check dn if ExistingUG since suffix is null */
		/* error message displayed by isValidDN */
	} else if ( !isValidPort(mi.m_nInstanceServerPort))
	{
		/* error displayed by isValidPort */
	}else{
       	/* all items in this dialogue look good */
		bValueReturned = FALSE;
	}

	return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// Server_Settings_DialogProc
//
// dialog proc to choose server settings
// 
// used by typical mode 
// 
//

static BOOL CALLBACK
Server_Settings_DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  BOOL bValueReturned = FALSE;
  UINT uResult        = 0;
  BOOL bResult        = FALSE;
  HWND hCtrl; 

  /* list of controls to be setup for 8bit input/display */
  INT h8bitControls[]={IDC_EDIT_SERVER_IDENTIFIER, 
					   IDC_EDIT_SUFFIX, 
					   -1}; 
  INT nCmdShow = SW_SHOW;
  static CHAR szSavedSuffix[MAX_STR_SIZE]="\0";

  switch (uMsg) 
  {
    case WM_INITDIALOG:
      // This message is sent when the property page is first created.  Here
      // you can perform any one time initialization that you require. 
	  Setup8bitInputDisplay(hwndDlg, h8bitControls);


	  SendDlgItemMessage(hwndDlg, IDC_SPIN_SERVER_PORT, UDM_SETBUDDY, (WPARAM)GetDlgItem(hwndDlg, IDC_EDIT_SERVER_PORT), 0);
	  SendDlgItemMessage(hwndDlg, IDC_SPIN_SERVER_PORT, UDM_SETRANGE, 0, MAKELONG((short)UD_MAXVAL, (short)1));

	  lstrcpy(szSavedSuffix, mi.m_szInstanceSuffix);
      break;


    case WM_COMMAND:
      // Windows sends WM_COMMAND messages whenever the user clicks on
      // a control in your property page.  If you need to perform some
      // special action, such as validating data or responding to a
      // button click, do it here.
      
      break;

    case WM_NOTIFY:
      // Windows sends WM_NOTIFY messages to your property page whenever
      // something interesting happens to the page.  This could be page
      // activation/deactivation, a button click, etc.  The wParam parameter
      // contains a pointer to the property page.  The lParam parameter
      // contains a pointer to an NMHDR structure.  The code field of this
      // structure contains the notification message code being sent.  The
      // property sheet API allows you to alter the behavior of these
      // messages by returning a value for each message.  To return a value,
      // use the SetWindowLong Windows SDK function.

      switch (((NMHDR*)lParam)->code) 
      {

        case PSN_SETACTIVE:
          // This notification is sent upon activation of the property page.
          // The property sheet should be centered each time it is activated
          // in case the user has moved the stupid thing (this duplicates
          // InstallShield functionality). You should also set the state of
          // the wizard buttons here.
          //
          // NOTE: If you do not wish this page to become active, return -1.

		  if(mi.m_nExistingUG)
		  {
  			/* hide suffix when creating config only directory */
			if( 0 != strlen(mi.m_szInstanceSuffix) )
			{
				lstrcpy(szSavedSuffix, mi.m_szInstanceSuffix);
				memset(mi.m_szInstanceSuffix, '\0', MAX_STR_SIZE);
			}
			nCmdShow = SW_HIDE;
		  }else{
			lstrcpy(mi.m_szInstanceSuffix, szSavedSuffix);
			nCmdShow = SW_SHOW;
		  }

		  hCtrl = GetDlgItem(hwndDlg, IDC_EDIT_SUFFIX);
		  ShowWindow(hCtrl,nCmdShow);

		  hCtrl = GetDlgItem(hwndDlg, IDC_STATIC_SUFFIX);
		  ShowWindow(hCtrl,nCmdShow);


	      bResult = SetDlgItemText(hwndDlg, 
			                       IDC_EDIT_SERVER_IDENTIFIER,
			                       mi.m_szServerIdentifier);
		  if(FALSE == bResult)
		  {
		      DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }

  		  bResult = SetDlgItemText(hwndDlg, 
			                       IDC_EDIT_SUFFIX,
				                   mi.m_szInstanceSuffix);
		  if(FALSE == bResult)
		  {
		  	  DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }

		  bResult = SetDlgItemInt(hwndDlg,
			                      IDC_EDIT_SERVER_PORT,
			                      mi.m_nInstanceServerPort,
					              TRUE);
		  if(FALSE == bResult)
		  {
		  	  DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }
		  
          CenterWindow(GetParent(hwndDlg));
          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_KILLACTIVE:
          // This notification is sent upon deactivation of the property page.
          // Here you can do whatever might be necessary for this action, such
          // as saving the state of the controls.  You should also reset the
          // the state of the wizard buttons here, as both the Back and Next
          // buttons should be active when you leave the AskOptions function.
          //
          // NOTE: If you do not want the page deactivated, return -1.

          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_WIZBACK:
          // The user clicked the back button from the first property page.
          // Set the result code NS_WIZBACK to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the first property sheet, you should ignore this
          // notification and simply let windows go to the previous page.
          //
          // NOTE: To prevent the wizard from stepping back, return -1.
		  SaveDialogInput_Server_Settings(hwndDlg);
		  // save the suffix typed in by the user
		  lstrcpy(szSavedSuffix, mi.m_szInstanceSuffix);
          break;

        case PSN_WIZNEXT:
          // The user clicked the next button from the last property page.
          // Set the result code NS_WIZNEXT to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the last property sheet, you should ignore this
          // notification and simply let windows go to the next page.
          //
          // NOTE: To prevent the wizard from stepping ahead, return -1.
   		  SaveDialogInput_Server_Settings(hwndDlg);
		  // save the suffix typed in by the user
		  lstrcpy(szSavedSuffix, mi.m_szInstanceSuffix);
		  if( TRUE == (bValueReturned =  Verify_Server_Settings()) )
		  {
			  /* one of the settings was invalid stay on this page */
              SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
		  }
		  break;

        case PSN_QUERYCANCEL:
          // This notification is sent when the user clicks the Cancel button.
          // It is also sent in response to the WM_CLOSE messages issued
          // by PSN_WIZBACK and PSN_WIZNEXT.  Make sure that we only process
          // this message if the result is not back or next so that we don't
          // nuke the return value assigned by PSN_WIZBACK or PSN_WIZNEXT.
          //
          // NOTE: To prevent the cancel from occuring, return -1.

          if (mi.m_nResult != NS_WIZBACK && mi.m_nResult != NS_WIZNEXT)
          {
            if (QueryExit(hwndDlg)) 
            {
              mi.m_nResult = NS_WIZCANCEL;
            }
            else
            {
              SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
              bValueReturned = TRUE;
            }
          }
          break;
      }
      break;
  }

  return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// BOOL SaveDialogPasswords
//
// helper for ROOTDN and SUITESPOTDN Dialogs
// 
// 
// saves passwords entered into dialog
//
// 

void SaveDialogPasswords(HWND hwndDlg, PSZ pszPassword, PSZ pszPasswordAgain)
{

	GetDlgItemText(hwndDlg,
				   IDC_EDIT_PASSWORD,
				   pszPassword,
				   MAX_STR_SIZE);


	GetDlgItemText(hwndDlg,
				   IDC_EDIT_PASSWORD_AGAIN,
				   pszPasswordAgain,
				   MAX_STR_SIZE);
}

//////////////////////////////////////////////////////////////////////////////
// BOOL LoadDialogPasswords
//
// helper for ROOTDN and SUITESPOTDN Dialogs
// 
// 
// loads passwords entered into dialog
//
// 
void LoadDialogPasswords(HWND hwndDlg, PSZ pszPassword, PSZ pszPasswordAgain)
{

   BOOL bResult;

   bResult = SetDlgItemText(hwndDlg, 
			                       IDC_EDIT_PASSWORD,
			                       pszPassword);
   if(FALSE == bResult)
   {
	  DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
   }

   if (pszPasswordAgain)
   {
	   bResult = SetDlgItemText(hwndDlg, 
								IDC_EDIT_PASSWORD_AGAIN,
								pszPasswordAgain);
	   if(FALSE == bResult)
	   {
		   DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
	   }
   }

}

//////////////////////////////////////////////////////////////////////////////
// BOOL VerifyPasswords
//
// helper for ROOTDN and SUITESPOTDN Dialogs
// 
// 
// check that passwords are valid
//
// Returns TRUE if there is an invalid setting
// Returns False if all settings are valid
//
// SideEffect: displays error (or writes to log in silent mode)
 

BOOL VerifyPasswords(PSZ pszPassword, PSZ pszPasswordAgain, UINT min_pw_len)
{
	BOOL bValueReturned = TRUE;

	if (min_pw_len > strlen(pszPassword))
	{
		/* password failed minimum length check */
		DSMessageBoxOK(ERR_PW_TOO_SHORT_TITLE, ERR_PW_TOO_SHORT, 0,
					   min_pw_len);
		
	} else if ( contains8BitChars(pszPassword) )
	{
		/* check to make sure pw doesn't contain any 8bit chars */
		DSMessageBoxOK(ERR_8BIT_PW_TITLE, ERR_8BIT_PW, 0);  

	} else if ( 0 == strlen(pszPasswordAgain) )
	{
		/* second password to verify missing */

		DSMessageBoxOK(ERR_NO_PW_AGAIN_TITLE, ERR_NO_PW_AGAIN, 0);
	} else if ( 0 != lstrcmp(pszPassword, pszPasswordAgain) )
	{
		/* passwords don't match */
		DSMessageBoxOK(ERR_PW_DIFFER_TITLE, ERR_PW_DIFFER, 0);
	}else{
	    /* passwords satisfied all checks so return false */
		bValueReturned = FALSE;
	}

	return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// SaveDialogInput_RootDN
//
// saves input in root dn DialogProc
// 
// 
// 
//

void SaveDialogInput_ROOTDN(HWND hwndDlg)
{
	BOOL bValueReturned = FALSE;
	UINT uResult        = 0;
	BOOL bResult        = FALSE;

	GetDlgItemText(hwndDlg, 
				   IDC_EDIT_UNRESTRICTED_USER,
				   mi.m_szInstanceUnrestrictedUser,
				   MAX_STR_SIZE);


	SaveDialogPasswords(hwndDlg, 
						mi.m_szInstancePassword,
						mi.m_szInstancePasswordAgain);
			

}

//////////////////////////////////////////////////////////////////////////////
// Verify_RootDN
//
// verifies input in root dn DialogProc
// 

//
// Returns TRUE if there is an invalid setting
// Returns FALSE if all settings are valid
//
// SideEffect: displays error (or writes to log in silent mode)

BOOL Verify_ROOTDN()
{
	BOOL bValueReturned = FALSE;

	if (  0 == strlen(mi.m_szInstanceUnrestrictedUser) )
	{
		/* no value entered */
		DSMessageBoxOK(ERR_NO_ROOT_DN_TITLE, ERR_NO_ROOT_DN, 0);
		bValueReturned = TRUE;
	}else if ( !isValidDN(mi.m_szInstanceUnrestrictedUser) )
	{
	    /* error message displayed by isvalidDN */
		bValueReturned = TRUE;
	} else
	{
		/* only bother to check passwords if username is valid */
		bValueReturned = VerifyPasswords(mi.m_szInstancePassword,
									     mi.m_szInstancePasswordAgain,
										 SLAPD_MIN_PW_LEN);
    }
	return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// RootDN_DialogProc
//
// dialog proc for the RootDN install page
// 
// 
// 
//

static BOOL CALLBACK
RootDN_DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  BOOL bValueReturned = FALSE;
  UINT uResult        = 0;
  BOOL bResult        = FALSE;
  
   /* list of controls to be setup for 8bit input/display */
  INT h8bitControls[]={IDC_EDIT_UNRESTRICTED_USER, 
					   IDC_EDIT_PASSWORD, 
					   IDC_EDIT_PASSWORD_AGAIN,
					   -1}; 
  switch (uMsg) 
  {
    case WM_INITDIALOG:
      // This message is sent when the property page is first created.  Here
      // you can perform any one time initialization that you require. 
	  Setup8bitInputDisplay(hwndDlg, h8bitControls);

      break;


    case WM_COMMAND:
      // Windows sends WM_COMMAND messages whenever the user clicks on
      // a control in your property page.  If you need to perform some
      // special action, such as validating data or responding to a
      // button click, do it here.
      
      break;

    case WM_NOTIFY:
      // Windows sends WM_NOTIFY messages to your property page whenever
      // something interesting happens to the page.  This could be page
      // activation/deactivation, a button click, etc.  The wParam parameter
      // contains a pointer to the property page.  The lParam parameter
      // contains a pointer to an NMHDR structure.  The code field of this
      // structure contains the notification message code being sent.  The
      // property sheet API allows you to alter the behavior of these
      // messages by returning a value for each message.  To return a value,
      // use the SetWindowLong Windows SDK function.

      switch (((NMHDR*)lParam)->code) 
      {

        case PSN_SETACTIVE:
          // This notification is sent upon activation of the property page.
          // The property sheet should be centered each time it is activated
          // in case the user has moved the stupid thing (this duplicates
          // InstallShield functionality). You should also set the state of
          // the wizard buttons here.
          //
          // NOTE: If you do not wish this page to become active, return -1.

	      bResult = SetDlgItemText(hwndDlg, 
			                       IDC_EDIT_UNRESTRICTED_USER,
			                       mi.m_szInstanceUnrestrictedUser);
		  if(FALSE == bResult)
		  {
			  DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }

		  LoadDialogPasswords(hwndDlg, 
							  mi.m_szInstancePassword,
							  mi.m_szInstancePasswordAgain);

          CenterWindow(GetParent(hwndDlg));
          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_KILLACTIVE:
          // This notification is sent upon deactivation of the property page.
          // Here you can do whatever might be necessary for this action, such
          // as saving the state of the controls.  You should also reset the
          // the state of the wizard buttons here, as both the Back and Next
          // buttons should be active when you leave the AskOptions function.
          //
          // NOTE: If you do not want the page deactivated, return -1.

          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_WIZBACK:
          // The user clicked the back button from the first property page.
          // Set the result code NS_WIZBACK to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the first property sheet, you should ignore this
          // notification and simply let windows go to the previous page.
          //
          // NOTE: To prevent the wizard from stepping back, return -1.
		  SaveDialogInput_ROOTDN(hwndDlg);
          break;

        case PSN_WIZNEXT:
          // The user clicked the next button from the last property page.
          // Set the result code NS_WIZNEXT to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the last property sheet, you should ignore this
          // notification and simply let windows go to the next page.
          //
          // NOTE: To prevent the wizard from stepping ahead, return -1.
			SaveDialogInput_ROOTDN(hwndDlg);
			if ( FALSE == (bValueReturned = Verify_ROOTDN() ) )
			{

				/* all settings on this dialogue look good */

				/* verify ldap settings */
				if (0 != verify_ldap_settings() )
				{
					/* dont allow next until settings corrected */
					SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
					bValueReturned = TRUE;

				} else
				{
					/* set ldap settings for other installs */
					set_ldap_settings();

				}
                
				/* if not advanced mode move on to other installs */
				if (CUSTOMMODE != MODE)
				{
					mi.m_nResult = NS_WIZNEXT;
					SendMessage(GetParent(hwndDlg), WM_CLOSE, 0, 0);                
				}
			} else
			{
				/* one of the settings was invalid, stay on this page */
				SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
			}

		  break;

        case PSN_QUERYCANCEL:
          // This notification is sent when the user clicks the Cancel button.
          // It is also sent in response to the WM_CLOSE messages issued
          // by PSN_WIZBACK and PSN_WIZNEXT.  Make sure that we only process
          // this message if the result is not back or next so that we don't
          // nuke the return value assigned by PSN_WIZBACK or PSN_WIZNEXT.
          //
          // NOTE: To prevent the cancel from occuring, return -1.

          if (mi.m_nResult != NS_WIZBACK && mi.m_nResult != NS_WIZNEXT)
          {
            if (QueryExit(hwndDlg)) 
            {
              mi.m_nResult = NS_WIZCANCEL;
            }
            else
            {
              SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
              bValueReturned = TRUE;
            }
          }
          break;
      }
      break;
  }

  return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
//
//  SaveDialogInput_SuitespotId
//
// 

//
void SaveDialogInput_SuitespotId(HWND hwndDlg)
{



	GetDlgItemText(hwndDlg, 
				   IDC_EDIT_SUITESPOT_USER,
				   mi.m_szSsptUid,
				   MAX_STR_SIZE);


	SaveDialogPasswords(hwndDlg, 
						mi.m_szSsptUidPw,
						mi.m_szSsptUidPwAgain);


}

//////////////////////////////////////////////////////////////////////////////
//
//  Verify_SuitespotId
//
// Returns TRUE if there is an invalid setting
// Returns FALSE if all settings are valid
//
// SideEffect: displays error (or writes to log in silent mode)

BOOL Verify_SuitespotId()
{

	BOOL bValueReturned = FALSE;

	if ( 0 == strlen(mi.m_szSsptUid) )
	{
		/* no value entered for sspt user */
		DSMessageBoxOK(ERR_NO_SS_ADMIN_TITLE, ERR_NO_SS_ADMIN, 0);
		bValueReturned = TRUE;
	} else if (!isAValidDN(mi.m_szSsptUid) &&
			   contains8BitChars(mi.m_szSsptUid))
	{
		/* admin uid value not 7 bit */
		DSMessageBoxOK(ERR_8BIT_UID_TITLE, ERR_8BIT_UID, 0);
		bValueReturned = TRUE;		
	} else
	{
		/* only bother to check passwords if username is valid */
		bValueReturned = VerifyPasswords(mi.m_szSsptUidPw,
									     mi.m_szSsptUidPwAgain,
										 SSPT_MIN_PW_LEN);
	}

	return bValueReturned;
}


//////////////////////////////////////////////////////////////////////////////
// SuitespotID_DialogProc
//
// dialog proc for the SUITESPOTID install page
// 
// 
// 
//

static BOOL CALLBACK
SuitespotID_DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  BOOL bValueReturned = FALSE;
  UINT uResult        = 0;
  BOOL bResult        = FALSE;
    
   /* list of controls to be setup for 8bit input/display */
  INT h8bitControls[]={IDC_EDIT_SUITESPOT_USER, 
					   IDC_EDIT_PASSWORD, 
					   IDC_EDIT_PASSWORD_AGAIN,
					   -1}; 
  switch (uMsg) 
  {
    case WM_INITDIALOG:
      // This message is sent when the property page is first created.  Here
      // you can perform any one time initialization that you require. 
	  Setup8bitInputDisplay(hwndDlg, h8bitControls);

      break;


    case WM_COMMAND:
      // Windows sends WM_COMMAND messages whenever the user clicks on
      // a control in your property page.  If you need to perform some
      // special action, such as validating data or responding to a
      // button click, do it here.
      
      break;

    case WM_NOTIFY:
      // Windows sends WM_NOTIFY messages to your property page whenever
      // something interesting happens to the page.  This could be page
      // activation/deactivation, a button click, etc.  The wParam parameter
      // contains a pointer to the property page.  The lParam parameter
      // contains a pointer to an NMHDR structure.  The code field of this
      // structure contains the notification message code being sent.  The
      // property sheet API allows you to alter the behavior of these
      // messages by returning a value for each message.  To return a value,
      // use the SetWindowLong Windows SDK function.

      switch (((NMHDR*)lParam)->code) 
      {

        case PSN_SETACTIVE:
          // This notification is sent upon activation of the property page.
          // The property sheet should be centered each time it is activated
          // in case the user has moved the stupid thing (this duplicates
          // InstallShield functionality). You should also set the state of
          // the wizard buttons here.
          //
          // NOTE: If you do not wish this page to become active, return -1.

		  if(1 == mi.m_nExistingMCC)
		  {
		      /* don't display this dialog if using existing MCC since asked for it 
			     there */
	          SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
              return TRUE;
		  }

          bResult = SetDlgItemText(hwndDlg, 
			                       IDC_EDIT_SUITESPOT_USER,
		                           mi.m_szSsptUid);
		  if(FALSE == bResult)
		  {
		      DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }

		  LoadDialogPasswords(hwndDlg, 
							  mi.m_szSsptUidPw,
							  mi.m_szSsptUidPwAgain);

          CenterWindow(GetParent(hwndDlg));
          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_KILLACTIVE:
          // This notification is sent upon deactivation of the property page.
          // Here you can do whatever might be necessary for this action, such
          // as saving the state of the controls.  You should also reset the
          // the state of the wizard buttons here, as both the Back and Next
          // buttons should be active when you leave the AskOptions function.
          //
          // NOTE: If you do not want the page deactivated, return -1.

          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_WIZBACK:
          // The user clicked the back button from the first property page.
			// Set the result code NS_WIZBACK to indicate this action and close
			// the property sheet using brute force.  If this procedure is not
			// being used by the first property sheet, you should ignore this
			// notification and simply let windows go to the previous page.
			//
			// NOTE: To prevent the wizard from stepping back, return -1.


			SaveDialogInput_SuitespotId(hwndDlg);

			// in express mode this is the first dialog, so set back to go to other module */

			if (EXPRESSMODE == MODE)
			{
				mi.m_nResult = NS_WIZBACK;
				SendMessage(GetParent(hwndDlg), WM_CLOSE, 0, 0);
			}
			

		  break;

        case PSN_WIZNEXT:
          // The user clicked the next button from the last property page.
          // Set the result code NS_WIZNEXT to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the last property sheet, you should ignore this
          // notification and simply let windows go to the next page.
          //
          // NOTE: To prevent the wizard from stepping ahead, return -1.
		  SaveDialogInput_SuitespotId(hwndDlg);
		  if(TRUE == (bValueReturned = Verify_SuitespotId() ) )
		  {
			/* one of the settings was invalid stay on this page */
			 SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
		  }
		   
		  break;

        case PSN_QUERYCANCEL:
          // This notification is sent when the user clicks the Cancel button.
          // It is also sent in response to the WM_CLOSE messages issued
          // by PSN_WIZBACK and PSN_WIZNEXT.  Make sure that we only process
          // this message if the result is not back or next so that we don't
          // nuke the return value assigned by PSN_WIZBACK or PSN_WIZNEXT.
          //
          // NOTE: To prevent the cancel from occuring, return -1.

          if (mi.m_nResult != NS_WIZBACK && mi.m_nResult != NS_WIZNEXT)
          {
            if (QueryExit(hwndDlg)) 
            {
              mi.m_nResult = NS_WIZCANCEL;
            }
            else
            {
              SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
              bValueReturned = TRUE;
            }
          }
          break;
      }
      break;
  }

  return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// Admin_ID_Only_DialogProc
//
// dialog proc for the ADMIN_ID_ONLY install page
// 
// 
// 
//

static BOOL CALLBACK
Admin_ID_Only_DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  BOOL bValueReturned = FALSE;
  UINT uResult        = 0;
  BOOL bResult        = FALSE;
    
   /* list of controls to be setup for 8bit input/display */
  INT h8bitControls[]={IDC_EDIT_SUITESPOT_USER, 
					   IDC_EDIT_PASSWORD, 
					   -1}; 
  switch (uMsg) 
  {
    case WM_INITDIALOG:
      // This message is sent when the property page is first created.  Here
      // you can perform any one time initialization that you require. 
	  Setup8bitInputDisplay(hwndDlg, h8bitControls);

      break;


    case WM_COMMAND:
      // Windows sends WM_COMMAND messages whenever the user clicks on
      // a control in your property page.  If you need to perform some
      // special action, such as validating data or responding to a
      // button click, do it here.
      
      break;

    case WM_NOTIFY:
      // Windows sends WM_NOTIFY messages to your property page whenever
      // something interesting happens to the page.  This could be page
      // activation/deactivation, a button click, etc.  The wParam parameter
      // contains a pointer to the property page.  The lParam parameter
      // contains a pointer to an NMHDR structure.  The code field of this
      // structure contains the notification message code being sent.  The
      // property sheet API allows you to alter the behavior of these
      // messages by returning a value for each message.  To return a value,
      // use the SetWindowLong Windows SDK function.

      switch (((NMHDR*)lParam)->code) 
      {

        case PSN_SETACTIVE:
          // This notification is sent upon activation of the property page.
          // The property sheet should be centered each time it is activated
          // in case the user has moved the stupid thing (this duplicates
          // InstallShield functionality). You should also set the state of
          // the wizard buttons here.
          //
          // NOTE: If you do not wish this page to become active, return -1.

          bResult = SetDlgItemText(hwndDlg, 
			                       IDC_EDIT_SUITESPOT_USER,
		                           mi.m_szMCCBindAs);
		  if(FALSE == bResult)
		  {
		      DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }

		  LoadDialogPasswords(hwndDlg, 
							  mi.m_szMCCPw,
							  0);

          CenterWindow(GetParent(hwndDlg));
          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_KILLACTIVE:
          // This notification is sent upon deactivation of the property page.
          // Here you can do whatever might be necessary for this action, such
          // as saving the state of the controls.  You should also reset the
          // the state of the wizard buttons here, as both the Back and Next
          // buttons should be active when you leave the AskOptions function.
          //
          // NOTE: If you do not want the page deactivated, return -1.

          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_WIZBACK:
			// The user clicked the back button from the first property page.
			// Set the result code NS_WIZBACK to indicate this action and close
			// the property sheet using brute force.  If this procedure is not
			// being used by the first property sheet, you should ignore this
			// notification and simply let windows go to the previous page.
			//
			// NOTE: To prevent the wizard from stepping back, return -1.

			GetDlgItemText(hwndDlg, 
						   IDC_EDIT_SUITESPOT_USER,
						   mi.m_szMCCBindAs,
						   MAX_STR_SIZE);
			GetDlgItemText(hwndDlg, 
						   IDC_EDIT_PASSWORD,
						   mi.m_szMCCPw,
						   MAX_STR_SIZE);

			// in express mode this is the first dialog, so set back to go to other module */

			if (EXPRESSMODE == MODE)
			{
				mi.m_nResult = NS_WIZBACK;
				SendMessage(GetParent(hwndDlg), WM_CLOSE, 0, 0);
			}
			

			break;

        case PSN_WIZNEXT:
			// The user clicked the next button from the last property page.
			// Set the result code NS_WIZNEXT to indicate this action and close
			// the property sheet using brute force.  If this procedure is not
			// being used by the last property sheet, you should ignore this
			// notification and simply let windows go to the next page.
			//
			// NOTE: To prevent the wizard from stepping ahead, return -1.
			GetDlgItemText(hwndDlg, 
						   IDC_EDIT_SUITESPOT_USER,
						   mi.m_szMCCBindAs,
						   MAX_STR_SIZE);
			GetDlgItemText(hwndDlg, 
						   IDC_EDIT_PASSWORD,
						   mi.m_szMCCPw,
						   MAX_STR_SIZE);
			if(TRUE == (bValueReturned = Verify_MCC_Settings() ) )
			{
				/* one of the settings was invalid stay on this page */
				SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
			}
		   
			break;

        case PSN_QUERYCANCEL:
          // This notification is sent when the user clicks the Cancel button.
          // It is also sent in response to the WM_CLOSE messages issued
          // by PSN_WIZBACK and PSN_WIZNEXT.  Make sure that we only process
          // this message if the result is not back or next so that we don't
          // nuke the return value assigned by PSN_WIZBACK or PSN_WIZNEXT.
          //
          // NOTE: To prevent the cancel from occuring, return -1.

          if (mi.m_nResult != NS_WIZBACK && mi.m_nResult != NS_WIZNEXT)
          {
            if (QueryExit(hwndDlg)) 
            {
              mi.m_nResult = NS_WIZCANCEL;
            }
            else
            {
              SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
              bValueReturned = TRUE;
            }
          }
          break;
      }
      break;
  }

  return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// Choose_Replication_DialogProc
//
// choose the type of replicatin to do, determine which pages to ask next
// 
// 
// 
//

static BOOL CALLBACK
Choose_Replication_DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  BOOL bValueReturned = FALSE;
  UINT uResult        = 0;
  BOOL bResult        = FALSE;
  
  switch (uMsg) 
  {
    case WM_INITDIALOG:
      // This message is sent when the property page is first created.  Here
      // you can perform any one time initialization that you require. 
 
      break;


    case WM_COMMAND:
      // Windows sends WM_COMMAND messages whenever the user clicks on
      // a control in your property page.  If you need to perform some
      // special action, such as validating data or responding to a
      // button click, do it here.

        /* consumer replication */
        if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_RADIO_NO_CONSUMER_REPLICATION ) )
        {
            mi.m_nSetupConsumerReplication = NO_REPLICATION;
        }else if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_RADIO_CONSUMER_CIR ) ){
            mi.m_nSetupConsumerReplication = CONSUMER_CIR_REPLICATION;
        }else if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_RADIO_CONSUMER_SIR ) ){
            mi.m_nSetupConsumerReplication = CONSUMER_SIR_REPLICATION;
        }

        /* supplier replication */
        if( BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_RADIO_NO_SUPPLIER_REPLICATION ) )
        {
            mi.m_nSetupSupplierReplication = NO_REPLICATION;
        }else if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_RADIO_SUPPLIER_CIR ) ){
            mi.m_nSetupSupplierReplication = SUPPLIER_CIR_REPLICATION;
        }else if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_RADIO_SUPPLIER_SIR ) ){
            mi.m_nSetupSupplierReplication = SUPPLIER_SIR_REPLICATION;
        }
     
      break;

    case WM_NOTIFY:
      // Windows sends WM_NOTIFY messages to your property page whenever
      // something interesting happens to the page.  This could be page
      // activation/deactivation, a button click, etc.  The wParam parameter
      // contains a pointer to the property page.  The lParam parameter
      // contains a pointer to an NMHDR structure.  The code field of this
      // structure contains the notification message code being sent.  The
      // property sheet API allows you to alter the behavior of these
      // messages by returning a value for each message.  To return a value,
      // use the SetWindowLong Windows SDK function.

      switch (((NMHDR*)lParam)->code) 
      {

        case PSN_SETACTIVE:
          // This notification is sent upon activation of the property page.
          // The property sheet should be centered each time it is activated
          // in case the user has moved the stupid thing (this duplicates
          // InstallShield functionality). You should also set the state of
          // the wizard buttons here.
          //
          // NOTE: If you do not wish this page to become active, return -1.

	       /* set buttons for appropriate replication type */
			if(CONSUMER_CIR_REPLICATION == mi.m_nSetupConsumerReplication)
			{
				CheckDlgButton(hwndDlg, IDC_RADIO_CONSUMER_CIR, BST_CHECKED); 
			}else if(CONSUMER_SIR_REPLICATION == mi.m_nSetupConsumerReplication){ 
				CheckDlgButton(hwndDlg, IDC_RADIO_CONSUMER_SIR, BST_CHECKED); 
			}else{
				CheckDlgButton(hwndDlg, IDC_RADIO_NO_CONSUMER_REPLICATION, BST_CHECKED); 
			}

			if(SUPPLIER_CIR_REPLICATION == mi.m_nSetupSupplierReplication)
			{
				CheckDlgButton(hwndDlg, IDC_RADIO_SUPPLIER_CIR, BST_CHECKED); 
			}else if(SUPPLIER_SIR_REPLICATION == mi.m_nSetupSupplierReplication){ 
				CheckDlgButton(hwndDlg, IDC_RADIO_SUPPLIER_SIR, BST_CHECKED); 
			}else{
				CheckDlgButton(hwndDlg, IDC_RADIO_NO_SUPPLIER_REPLICATION, BST_CHECKED); 
			}

	        CenterWindow(GetParent(hwndDlg));
		    PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_KILLACTIVE:
          // This notification is sent upon deactivation of the property page.
          // Here you can do whatever might be necessary for this action, such
          // as saving the state of the controls.  You should also reset the
          // the state of the wizard buttons here, as both the Back and Next
          // buttons should be active when you leave the AskOptions function.
          //
          // NOTE: If you do not want the page deactivated, return -1.

          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_WIZBACK:
          // The user clicked the back button from the first property page.
          // Set the result code NS_WIZBACK to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the first property sheet, you should ignore this
          // notification and simply let windows go to the previous page.
          //
          // NOTE: To prevent the wizard from stepping back, return -1.
		   
		  // simple dialog, all button state gets saved in WM_COMMAND proccessing
  		  break;

        case PSN_WIZNEXT:
          // The user clicked the next button from the last property page.
          // Set the result code NS_WIZNEXT to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the last property sheet, you should ignore this
          // notification and simply let windows go to the next page.
          //
          // NOTE: To prevent the wizard from stepping ahead, return -1.
		  
		  // simple dialog, all button state gets saved in WM_COMMAND proccessing
		  break;

        case PSN_QUERYCANCEL:
          // This notification is sent when the user clicks the Cancel button.
          // It is also sent in response to the WM_CLOSE messages issued
          // by PSN_WIZBACK and PSN_WIZNEXT.  Make sure that we only process
          // this message if the result is not back or next so that we don't
          // nuke the return value assigned by PSN_WIZBACK or PSN_WIZNEXT.
          //
          // NOTE: To prevent the cancel from occuring, return -1.

          if (mi.m_nResult != NS_WIZBACK && mi.m_nResult != NS_WIZNEXT)
          {
            if (QueryExit(hwndDlg)) 
            {
              mi.m_nResult = NS_WIZCANCEL;
            }
            else
            {
              SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
              bValueReturned = TRUE;
            }
          }
          break;
      }
      break;
  }

  return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// SaveDialogInput_Consumer_Replication
//
// save settings from ConsumerReplication Dialog
// 
// 
// 
//

void SaveDialogInput_Consumer_Replication(HWND hwndDlg)
{


	if ( CONSUMER_SIR_REPLICATION  == mi.m_nSetupConsumerReplication  )
	{

		GetDlgItemText(hwndDlg, 
					   IDC_EDIT_SUPPLIER_DN,
					   mi.m_szSupplierDN,
					   MAX_STR_SIZE);



		SaveDialogPasswords(hwndDlg, 
							mi.m_szSupplierPW,
							mi.m_szSupplierPWAgain);

	}
}

//////////////////////////////////////////////////////////////////////////////
// Verify_Consumer_Replication
//
// save settings from ConsumerReplication Dialog
// 
// 
// Returns TRUE if there is an invalid setting
// Returns FALSE if all settings are valid
//
// SideEffect: displays error (or writes to log in silent mode)


BOOL Verify_Consumer_Replication()
{
    BOOL bValueReturned = FALSE;

	/* only check if configuring consumer replication */
	if ( CONSUMER_SIR_REPLICATION  == mi.m_nSetupConsumerReplication  )
	{

		if ( 0 == strlen(mi.m_szSupplierDN) )
		{
			/* no value entered for Supplier DN */
			DSMessageBoxOK(ERR_NO_SUPPLIER_DN_TITLE,
						   ERR_NO_SUPPLIER_DN, 0);
			bValueReturned = TRUE;
		}else if ( !isValidDN(mi.m_szSupplierDN) )
		{
			/* error message displayed by isvalidDN */
			bValueReturned = TRUE;
		} else
		{

			bValueReturned = VerifyPasswords(mi.m_szSupplierPW,
										     mi.m_szSupplierPWAgain,
											 SLAPD_MIN_PW_LEN);
	    }
	}
	return bValueReturned;
}


//////////////////////////////////////////////////////////////////////////////
// Consumer_Replication_DialogProc
//
// ask common settings needed for this server to be a consumer
// 
// 
// 
//

static BOOL CALLBACK
Consumer_Replication_DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  BOOL bValueReturned = FALSE;
  UINT uResult        = 0;
  BOOL bResult        = FALSE;
  
   /* list of controls to be setup for 8bit input/display */
  INT h8bitControls[]={IDC_EDIT_SUPPLIER_DN, 
					   IDC_EDIT_PASSWORD, 
					   IDC_EDIT_PASSWORD_AGAIN,
					   -1}; 
  
  switch (uMsg) 
  {
    case WM_INITDIALOG:
      // This message is sent when the property page is first created.  Here
      // you can perform any one time initialization that you require. 
	  Setup8bitInputDisplay(hwndDlg, h8bitControls);
     
      break;


    case WM_COMMAND:
      // Windows sends WM_COMMAND messages whenever the user clicks on
      // a control in your property page.  If you need to perform some
      // special action, such as validating data or responding to a
      // button click, do it here.
      
      break;

    case WM_NOTIFY:
      // Windows sends WM_NOTIFY messages to your property page whenever
      // something interesting happens to the page.  This could be page
      // activation/deactivation, a button click, etc.  The wParam parameter
      // contains a pointer to the property page.  The lParam parameter
      // contains a pointer to an NMHDR structure.  The code field of this
      // structure contains the notification message code being sent.  The
      // property sheet API allows you to alter the behavior of these
      // messages by returning a value for each message.  To return a value,
      // use the SetWindowLong Windows SDK function.

      switch (((NMHDR*)lParam)->code) 
      {

        case PSN_SETACTIVE:
          // This notification is sent upon activation of the property page.
          // The property sheet should be centered each time it is activated
          // in case the user has moved the stupid thing (this duplicates
          // InstallShield functionality). You should also set the state of
          // the wizard buttons here.
          //
          // NOTE: If you do not wish this page to become active, return -1.
          if( CONSUMER_SIR_REPLICATION  != mi.m_nSetupConsumerReplication )
          {
            /* we only want this dialog for Consumer SIR replication */
            SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
            return TRUE;

          }

          bResult = SetDlgItemText(hwndDlg, 
		                           IDC_EDIT_SUPPLIER_DN,
		                           mi.m_szSupplierDN);
		  if(FALSE == bResult)
		  {
	          DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }

		  
		  LoadDialogPasswords(hwndDlg, 
							mi.m_szSupplierPW,
							mi.m_szSupplierPWAgain);

          CenterWindow(GetParent(hwndDlg));
          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_KILLACTIVE:
          // This notification is sent upon deactivation of the property page.
          // Here you can do whatever might be necessary for this action, such
          // as saving the state of the controls.  You should also reset the
          // the state of the wizard buttons here, as both the Back and Next
          // buttons should be active when you leave the AskOptions function.
          //
          // NOTE: If you do not want the page deactivated, return -1.

          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_WIZBACK:
          // The user clicked the back button from the first property page.
          // Set the result code NS_WIZBACK to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the first property sheet, you should ignore this
          // notification and simply let windows go to the previous page.
          //
          // NOTE: To prevent the wizard from stepping back, return -1.
		  SaveDialogInput_Consumer_Replication(hwndDlg);

  		  break;

        case PSN_WIZNEXT:
			// The user clicked the next button from the last property page.
			// Set the result code NS_WIZNEXT to indicate this action and close
			// the property sheet using brute force.  If this procedure is not
			// being used by the last property sheet, you should ignore this
			// notification and simply let windows go to the next page.
			//
			// NOTE: To prevent the wizard from stepping ahead, return -1.
			SaveDialogInput_Consumer_Replication(hwndDlg);
			if( TRUE == (bValueReturned = Verify_Consumer_Replication() ) )
			{
			   /* a setting was invalid stay on this page*/
               SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
			}else{
                /* all settings on this dialogue look good */
		
				/* these settings were specified, ok to use them */
				mi.m_nUseSupplierSettings = 1;
			}

			break;

        case PSN_QUERYCANCEL:
          // This notification is sent when the user clicks the Cancel button.
          // It is also sent in response to the WM_CLOSE messages issued
          // by PSN_WIZBACK and PSN_WIZNEXT.  Make sure that we only process
          // this message if the result is not back or next so that we don't
          // nuke the return value assigned by PSN_WIZBACK or PSN_WIZNEXT.
          //
          // NOTE: To prevent the cancel from occuring, return -1.

          if (mi.m_nResult != NS_WIZBACK && mi.m_nResult != NS_WIZNEXT)
          {
            if (QueryExit(hwndDlg)) 
            {
              mi.m_nResult = NS_WIZCANCEL;
            }
            else
            {
              SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
              bValueReturned = TRUE;
            }
          }
          break;
      }
      break;
  }

  return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// SaveDialogInput_Supplier_Replication
//
// save supplier settings
// 
// 
// 
//

void SaveDialogInput_Supplier_Replication(HWND hwndDlg)
{
	GetDlgItemText(hwndDlg, 
				   IDC_EDIT_CHANGELOG_DB_DIR,
				   mi.m_szChangeLogDbDir,
				   MAX_STR_SIZE);


	GetDlgItemText(hwndDlg, 
				   IDC_EDIT_CHANGELOG_DB_SUFFIX,
				   mi.m_szChangeLogSuffix,
				   MAX_STR_SIZE);
}

//////////////////////////////////////////////////////////////////////////////
// Verify_Supplier_Replication
//
// save supplier settings
// 
// 
// Returns TRUE if there is an invalid setting
// Returns FALSE if all settings are valid
//
// SideEffect: displays error (or writes to log in silent mode)

BOOL Verify_Supplier_Replication()
{
	BOOL bValueReturned = TRUE;

	if ( 0 == strlen(mi.m_szChangeLogDbDir) )
	{
		DSMessageBoxOK(ERR_NO_CHANGELOG_DB_TITLE,
					   ERR_NO_CHANGELOG_DB, 0);
	} else if ( contains8BitChars(mi.m_szChangeLogDbDir) )
	{
		/* make sure the path doesnt contain international characters */
		DSMessageBoxOK(ERR_8BIT_PATH_TITLE, ERR_8BIT_PATH, 0);    
	} else if ( 0 == strlen(mi.m_szChangeLogSuffix) )
	{
		DSMessageBoxOK(ERR_NO_CHANGELOG_SUFFIX_TITLE,
					   ERR_NO_CHANGELOG_SUFFIX, 0);
    } else if ( !isValidDN(mi.m_szChangeLogSuffix) )
	{
		/* error message displayed by isValidDN */
	}else{
	    /* all settings lookg good */
	    bValueReturned = FALSE;
	}
	return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// Supplier_Replication_DialogProc
//
// ask common settings for this server to be a suppplier
// 
// 
// 
//

static BOOL CALLBACK
Supplier_Replication_DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  BOOL bValueReturned = FALSE;
  UINT uResult        = 0;
  BOOL bResult        = FALSE;
  static INT nInitialized = 0;
  
   /* list of controls to be setup for 8bit input/display */
  INT h8bitControls[]={IDC_EDIT_CHANGELOG_DB_DIR, 
					   IDC_EDIT_CHANGELOG_DB_SUFFIX, 
					   -1}; 
  switch (uMsg) 
  {
    case WM_INITDIALOG:
      // This message is sent when the property page is first created.  Here
      // you can perform any one time initialization that you require. 
		Setup8bitInputDisplay(hwndDlg, h8bitControls);

	        // need to initialize the changelog dir the first time
		// this dialog is created. This is because 
		// the default values are set on dllMain and it wouldn't
		// pick up the users change of target dir to calculate it
		// reason for nInitialized is so it doesn't blow away 
		// users changes after leaving module and window is recreated
		// and wm_init is called again
		if( !nInitialized )
		{
			sprintf(mi.m_szChangeLogDbDir,"%s\\%s-%s\\%s", TARGETDIR,  
				      DS_ID_SERVICE, mi.m_szServerIdentifier, DEFAULT_CHANGELOGDIR);
			nInitialized = 1;
		}

      break;


    case WM_COMMAND:
      // Windows sends WM_COMMAND messages whenever the user clicks on
      // a control in your property page.  If you need to perform some
      // special action, such as validating data or responding to a
      // button click, do it here.
      
      break;

    case WM_NOTIFY:
      // Windows sends WM_NOTIFY messages to your property page whenever
      // something interesting happens to the page.  This could be page
      // activation/deactivation, a button click, etc.  The wParam parameter
      // contains a pointer to the property page.  The lParam parameter
      // contains a pointer to an NMHDR structure.  The code field of this
      // structure contains the notification message code being sent.  The
      // property sheet API allows you to alter the behavior of these
      // messages by returning a value for each message.  To return a value,
      // use the SetWindowLong Windows SDK function.

      switch (((NMHDR*)lParam)->code) 
      {

        case PSN_SETACTIVE:
          // This notification is sent upon activation of the property page.
          // The property sheet should be centered each time it is activated
          // in case the user has moved the stupid thing (this duplicates
          // InstallShield functionality). You should also set the state of
          // the wizard buttons here.
          //
          // NOTE: If you do not wish this page to become active, return -1.
          if( NO_REPLICATION == mi.m_nSetupSupplierReplication)
          {
            /* we dont want to display this page unless this server is a supplier*/
            SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
            return TRUE;

          }

		                                
	      bResult = SetDlgItemText(hwndDlg, 
			                       IDC_EDIT_CHANGELOG_DB_SUFFIX,
				                     mi.m_szChangeLogSuffix);
		  if(FALSE == bResult)
		  {
			  DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }

 		  bResult = SetDlgItemText(hwndDlg, 
				                     IDC_EDIT_CHANGELOG_DB_DIR,
					                 mi.m_szChangeLogDbDir);
		  if(FALSE == bResult)
		  {
		      DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }


          CenterWindow(GetParent(hwndDlg));
          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_KILLACTIVE:
          // This notification is sent upon deactivation of the property page.
          // Here you can do whatever might be necessary for this action, such
          // as saving the state of the controls.  You should also reset the
          // the state of the wizard buttons here, as both the Back and Next
          // buttons should be active when you leave the AskOptions function.
          //
          // NOTE: If you do not want the page deactivated, return -1.

          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_WIZBACK:
          // The user clicked the back button from the first property page.
          // Set the result code NS_WIZBACK to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the first property sheet, you should ignore this
          // notification and simply let windows go to the previous page.
          //
          // NOTE: To prevent the wizard from stepping back, return -1.
		  SaveDialogInput_Supplier_Replication(hwndDlg); 
  		  break;

        case PSN_WIZNEXT:
          // The user clicked the next button from the last property page.
          // Set the result code NS_WIZNEXT to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the last property sheet, you should ignore this
          // notification and simply let windows go to the next page.
          //
          // NOTE: To prevent the wizard from stepping ahead, return -1.
			SaveDialogInput_Supplier_Replication(hwndDlg); 

			if ( FALSE == (bValueReturned = Verify_Supplier_Replication() ) )
			{
				/* all settings look good */
				/* user chose these settings through dialog so use them */
				mi.m_nUseChangeLogSettings = 1;   
			}else{
				/* one of the settings is invalid, stay on this page */
                SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
			}
			break;

        case PSN_QUERYCANCEL:
          // This notification is sent when the user clicks the Cancel button.
          // It is also sent in response to the WM_CLOSE messages issued
          // by PSN_WIZBACK and PSN_WIZNEXT.  Make sure that we only process
          // this message if the result is not back or next so that we don't
          // nuke the return value assigned by PSN_WIZBACK or PSN_WIZNEXT.
          //
          // NOTE: To prevent the cancel from occuring, return -1.

          if (mi.m_nResult != NS_WIZBACK && mi.m_nResult != NS_WIZNEXT)
          {
            if (QueryExit(hwndDlg)) 
            {
              mi.m_nResult = NS_WIZCANCEL;
            }
            else
            {
              SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
              bValueReturned = TRUE;
            }
          }
          break;
      }
      break;
  }

  return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// SaveDialogInput_Consumer_DN
//
// 
// save input for Consumer DN dialog
// 
//

void SaveDialogInput_Consumer_DN(HWND hwndDlg)
{

	if (1 == mi.m_nConfigConsumerDN)
	{
		GetDlgItemText(hwndDlg, 
					   IDC_EDIT_CONSUMER_DN,
					   mi.m_szConsumerDN,
					   MAX_STR_SIZE);

		SaveDialogPasswords(hwndDlg, 
							mi.m_szConsumerPW,
							mi.m_szConsumerPWAgain);


	}
}

//////////////////////////////////////////////////////////////////////////////
// Verify_Consumer_DN
//
// 
// verify input for Consumer DN dialog
// 
// Returns TRUE if there is an invalid setting
// Returns FALSE if all settings are valid
//
// SideEffect: displays error (or writes to log in silent mode)

BOOL Verify_Consumer_DN()
{

   BOOL bValueReturned = FALSE;

    /* only verify if trying to use these settings */
	if (1 == mi.m_nConfigConsumerDN)
	{

		if ( 0 == strlen(mi.m_szConsumerDN) )
		{
			/* no value entered for consumer dn */
			DSMessageBoxOK(ERR_NO_CONSUMER_DN_TITLE, ERR_NO_CONSUMER_DN, 0);
			bValueReturned = TRUE;
	    }else if ( !isValidDN(mi.m_szConsumerDN) )
		{
		/* error message displayed by isvalidDN */
			bValueReturned = TRUE;
		} else
		{
			/* only bother to check passwords if username is valid */
			bValueReturned = VerifyPasswords(mi.m_szConsumerPW,
											 mi.m_szConsumerPWAgain,
											 SLAPD_MIN_PW_LEN);
		}

	}
	return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// Consumer_DN_DialogProc
//
// dialog proc for the Consumer DN page 
// displayed only under Supplier CIR replication
// 
//

static BOOL CALLBACK
Consumer_DN_DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  BOOL bValueReturned = FALSE;
  UINT uResult        = 0;
  BOOL bResult        = FALSE;
    
   /* list of controls to be setup for 8bit input/display */
  INT h8bitControls[]={IDC_EDIT_CONSUMER_DN, 
					   IDC_EDIT_PASSWORD,
					   IDC_EDIT_PASSWORD_AGAIN,
					   -1};
  switch (uMsg) 
  {
    case WM_INITDIALOG:

      // This message is sent when the property page is first created.  Here
      // you can perform any one time initialization that you require. 
	  Setup8bitInputDisplay(hwndDlg, h8bitControls);

	  sprintf(mi.m_szConsumerDN, "%s,%s", DEFAULT_CONSUMER_DN, mi.m_szInstanceSuffix);

      break;


    case WM_COMMAND:
      // Windows sends WM_COMMAND messages whenever the user clicks on
      // a control in your property page.  If you need to perform some
      // special action, such as validating data or responding to a
      // button click, do it here.

		if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_RADIO_CONFIG_CONSUMER_DN_YES) )
		{
			EnableConsumerDNFields(hwndDlg, TRUE);
			mi.m_nConfigConsumerDN = 1;
		}else if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_RADIO_CONFIG_CONSUMER_DN_NO) ){
			EnableConsumerDNFields(hwndDlg, FALSE);
			mi.m_nConfigConsumerDN = 0;
		}
      
      break;

    case WM_NOTIFY:
      // Windows sends WM_NOTIFY messages to your property page whenever
      // something interesting happens to the page.  This could be page
      // activation/deactivation, a button click, etc.  The wParam parameter
      // contains a pointer to the property page.  The lParam parameter
      // contains a pointer to an NMHDR structure.  The code field of this
      // structure contains the notification message code being sent.  The
      // property sheet API allows you to alter the behavior of these
      // messages by returning a value for each message.  To return a value,
      // use the SetWindowLong Windows SDK function.

      switch (((NMHDR*)lParam)->code) 
      {

        case PSN_SETACTIVE:
          // This notification is sent upon activation of the property page.
          // The property sheet should be centered each time it is activated
          // in case the user has moved the stupid thing (this duplicates
          // InstallShield functionality). You should also set the state of
          // the wizard buttons here.
          //
          // NOTE: If you do not wish this page to become active, return -1.

          if( SUPPLIER_CIR_REPLICATION  != mi.m_nSetupSupplierReplication  )
          {
            /* we only ask consumer dn for supplier cir*/
            SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
            return TRUE;

          }
		
	      bResult = SetDlgItemText(hwndDlg, 
			                       IDC_EDIT_CONSUMER_DN,
				                   mi.m_szConsumerDN);
		  if(FALSE == bResult)
		  {
		      DSMessageBoxOK(ERR_INIT_DIALOG_TITLE, ERR_INIT_DIALOG, 0);
		  }

	  	  LoadDialogPasswords(hwndDlg, 
			   				  mi.m_szConsumerPW,
							  mi.m_szConsumerPWAgain);


  		  CheckRadioButton(hwndDlg, IDC_RADIO_CONFIG_CONSUMER_DN_YES, IDC_RADIO_CONFIG_CONSUMER_DN_NO, 
							( (1 == mi.m_nConfigConsumerDN) ? IDC_RADIO_CONFIG_CONSUMER_DN_YES : 
									IDC_RADIO_CONFIG_CONSUMER_DN_NO));


		  if(1 == mi.m_nConfigConsumerDN)
		  {
			EnableConsumerDNFields(hwndDlg, TRUE);
		  }else{
			EnableConsumerDNFields(hwndDlg, FALSE);
		  }

          CenterWindow(GetParent(hwndDlg));
          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_KILLACTIVE:
          // This notification is sent upon deactivation of the property page.
          // Here you can do whatever might be necessary for this action, such
          // as saving the state of the controls.  You should also reset the
          // the state of the wizard buttons here, as both the Back and Next
          // buttons should be active when you leave the AskOptions function.
          //
          // NOTE: If you do not want the page deactivated, return -1.

          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_WIZBACK:
          // The user clicked the back button from the first property page.
          // Set the result code NS_WIZBACK to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the first property sheet, you should ignore this
          // notification and simply let windows go to the previous page.
          //
          // NOTE: To prevent the wizard from stepping back, return -1.
		  SaveDialogInput_Consumer_DN(hwndDlg);
          break;

        case PSN_WIZNEXT:
          // The user clicked the next button from the last property page.
          // Set the result code NS_WIZNEXT to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the last property sheet, you should ignore this
          // notification and simply let windows go to the next page.
          //
          // NOTE: To prevent the wizard from stepping ahead, return -1.
  		  SaveDialogInput_Consumer_DN(hwndDlg);
		  if( TRUE == (bValueReturned = Verify_Consumer_DN(hwndDlg) ) )
		  {
			  /* one of the settings is invalid, stay on this page */
    		  SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
		  }

		  
		  break;

        case PSN_QUERYCANCEL:
          // This notification is sent when the user clicks the Cancel button.
          // It is also sent in response to the WM_CLOSE messages issued
          // by PSN_WIZBACK and PSN_WIZNEXT.  Make sure that we only process
          // this message if the result is not back or next so that we don't
          // nuke the return value assigned by PSN_WIZBACK or PSN_WIZNEXT.
          //
          // NOTE: To prevent the cancel from occuring, return -1.

          if (mi.m_nResult != NS_WIZBACK && mi.m_nResult != NS_WIZNEXT)
          {
            if (QueryExit(hwndDlg)) 
            {
              mi.m_nResult = NS_WIZCANCEL;
            }
            else
            {
              SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
              bValueReturned = TRUE;
            }
          }
          break;
      }
      break;
  }

  return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// SetDlgReplDays
//
// helper function to convert string to checked days in repl agreement dialog proc
// 
// 
// 
//                           

   
BOOL SetDlgReplDays(HWND hwndDlg, 
                            PSZ szReplDays)
{
	INT Days[7] = {0};
	INT i, tmp=0;
	INT DayControls[] ={IDC_CHECK_SUN, IDC_CHECK_MON, IDC_CHECK_TUE,
						IDC_CHECK_WED, IDC_CHECK_THUR, IDC_CHECK_FRI,
						IDC_CHECK_SAT, -1};

	for(i=0; szReplDays != NULL && szReplDays[i] != '\0'; i++)
	{
		tmp = ( (INT) szReplDays[i] ) - ASCII_ZERO;
		if( tmp >= 0 && tmp < 7)
		{
			CheckDlgButton(hwndDlg, DayControls[tmp], BST_CHECKED); 
		}

	}
	
	return TRUE;

}

//////////////////////////////////////////////////////////////////////////////
// GetDlgReplDays
//
// helper function to convert checked days to string for repl agreement dialog procs
// 
// 
// 
//                           
	

BOOL GetDlgReplDays(HWND hwndDlg, 
                            PSZ szReplDays)
{
	INT Days[7] = {0};
	INT i, pos;
	INT DayControls[] ={IDC_CHECK_SUN, IDC_CHECK_MON, IDC_CHECK_TUE,
						IDC_CHECK_WED, IDC_CHECK_THUR, IDC_CHECK_FRI,
						IDC_CHECK_SAT, -1};
	pos=0;
	for(i=0; DayControls[i] != -1; i++)
	{
		
		if( IsDlgButtonChecked(hwndDlg, DayControls[i]) )
		{
			szReplDays[pos]=(char) (i + ASCII_ZERO);
			pos++;
		}

	}
	szReplDays[pos]='\0';
	
	return TRUE;

}						

//////////////////////////////////////////////////////////////////////////////
// SetDlgReplTimes
//  Initialize Time Controls/Spinners in ReplAgreementDlgProc
//	
// 
// 
//

BOOL SetDlgReplTimes(HWND hwndDlg, PSZ szTimes)
{
	  CHAR s_hh[3]="\0", s_mm[3]="\0";
	  CHAR e_hh[3]="\0", e_mm[3]="\0";
	  INT i;
	  	   
	  for(i=0; i <2; i++)
	  {
		s_hh[i] = szTimes[i];
		s_mm[i] = szTimes[i+2];
		e_hh[i] = szTimes[i+5];
		e_mm[i] = szTimes[i+7];
	  }

	  SetDlgItemText(hwndDlg, IDC_EDIT_REPL_START_TIME_HH, s_hh);
	  SetDlgItemText(hwndDlg, IDC_EDIT_REPL_START_TIME_MM, s_mm);

	  SetDlgItemText(hwndDlg, IDC_EDIT_REPL_END_TIME_HH, e_hh);
	  SetDlgItemText(hwndDlg, IDC_EDIT_REPL_END_TIME_MM, e_mm);

      SendDlgItemMessage(hwndDlg, IDC_SPIN_REPL_START_TIME_HH, UDM_SETBUDDY, (WPARAM)GetDlgItem(hwndDlg, IDC_EDIT_REPL_START_TIME_HH), 0);
      SendDlgItemMessage(hwndDlg, IDC_SPIN_REPL_START_TIME_HH, UDM_SETRANGE, 0, MAKELONG((short)23, (short)0));

      SendDlgItemMessage(hwndDlg, IDC_SPIN_REPL_START_TIME_MM, UDM_SETBUDDY, (WPARAM)GetDlgItem(hwndDlg, IDC_EDIT_REPL_START_TIME_MM), 0);
      SendDlgItemMessage(hwndDlg, IDC_SPIN_REPL_START_TIME_MM, UDM_SETRANGE, 0, MAKELONG((short)59, (short)0));

      SendDlgItemMessage(hwndDlg, IDC_SPIN_REPL_END_TIME_HH, UDM_SETBUDDY, (WPARAM)GetDlgItem(hwndDlg, IDC_EDIT_REPL_END_TIME_HH), 0);
      SendDlgItemMessage(hwndDlg, IDC_SPIN_REPL_END_TIME_HH, UDM_SETRANGE, 0, MAKELONG((short)23, (short)0));

      SendDlgItemMessage(hwndDlg, IDC_SPIN_REPL_END_TIME_MM, UDM_SETBUDDY, (WPARAM)GetDlgItem(hwndDlg, IDC_EDIT_REPL_END_TIME_MM), 0);
      SendDlgItemMessage(hwndDlg, IDC_SPIN_REPL_END_TIME_MM, UDM_SETRANGE, 0, MAKELONG((short)59, (short)0));
	 

	  return TRUE;
}

//////////////////////////////////////////////////////////////////////////////
// writeTime
//  make Time Component always be 2 digits
//	
// 
// 
//
BOOL writeTime(int nn, char *szTime)
{
	  if( nn > 9)
	  {
		sprintf(szTime, "%d", nn);
	  }else{
		sprintf(szTime, "0%d", nn);
	  }

  return TRUE;
}

//////////////////////////////////////////////////////////////////////////////
// GetDlgReplTimes
//  Get Repl Times From the Dialog
//	
// 
// 
//

BOOL GetDlgReplTimes(HWND hwndDlg, PSZ szTimes)
{
	  CHAR s_hh[3]="\0", s_mm[3]="\0";
	  CHAR e_hh[3]="\0", e_mm[3]="\0";
	  INT  ns_hh, ns_mm;
	  INT  ne_hh, ne_mm;
	  BOOL bTrans;

	  ns_hh = GetDlgItemInt(hwndDlg, IDC_EDIT_REPL_START_TIME_HH, &bTrans, FALSE);
	  ns_mm = GetDlgItemInt(hwndDlg, IDC_EDIT_REPL_START_TIME_MM, &bTrans, FALSE);
	  ne_hh = GetDlgItemInt(hwndDlg, IDC_EDIT_REPL_END_TIME_HH, &bTrans, FALSE);
      ne_mm = GetDlgItemInt(hwndDlg, IDC_EDIT_REPL_END_TIME_MM, &bTrans, FALSE);

	  	   
	  writeTime(ns_hh, s_hh);
	  writeTime(ns_mm, s_mm);
	  writeTime(ne_hh, e_hh);
	  writeTime(ne_mm, e_mm);

	  sprintf(szTimes, "%s%s-%s%s", s_hh, s_mm, e_hh, e_mm);
	 
	  return TRUE;
}

//////////////////////////////////////////////////////////////////////////////
// SaveDlgReplAgreement
//
// save information for replication agreements from dialog
// 
// 
// 

void SaveDlgReplAgreement(HWND hwndDlg, 
						  PSZ pszReplHost, 
						  INT *pnReplPort,
						  PSZ pszReplRoot,
						  PSZ pszReplBindAs,
						  PSZ pszReplPw,
						  PSZ pszReplDays,
						  PSZ pszReplTimes)
{

	SaveDlgServerInfo(hwndDlg,
					  pszReplHost, 
					  pnReplPort,
					  pszReplRoot,
					  pszReplBindAs,
					  pszReplPw);

	/* get replication days */
	GetDlgReplDays(hwndDlg, pszReplDays);

	/* get replication times */
	GetDlgReplTimes(hwndDlg, pszReplTimes);

}

//////////////////////////////////////////////////////////////////////////////
// LoadDlgReplAgreement
//
//load information for replication agreements into dialog
// 
// 
// 

void LoadDlgReplAgreement(HWND hwndDlg, 
						  PSZ pszReplHost, 
						  INT nReplPort,
						  PSZ pszReplRoot,
						  PSZ pszReplBindAs,
						  PSZ pszReplPw,
						  PSZ pszReplDays,
						  PSZ pszReplTimes)
{

	LoadDlgServerInfo(hwndDlg,
					  pszReplHost, 
					  nReplPort,
					  pszReplRoot,
					  pszReplBindAs,
					  pszReplPw);

	SetDlgReplDays(hwndDlg, pszReplDays);
	
	/* get replication times */
	SetDlgReplTimes(hwndDlg, pszReplTimes);

}


//////////////////////////////////////////////////////////////////////////////
// VerifyReplAgreement
//
// Verify replication agreement
// 
// Returns TRUE if there is an invalid setting
// Returns False if all settings are valid
//
// SideEffect: displays error (or writes to log in silent mode)
 

BOOL VerifyReplAgreement(   PSZ pszReplHost, 
                            INT *pnReplPort,
                            PSZ pszReplRoot,
                            PSZ pszReplBindAs,
                            PSZ pszReplPw,
							PSZ pszReplDays,
							PSZ pszReplTimes)
{
    BOOL bValueReturned = FALSE;

	
	if( TRUE == (bValueReturned = VerifyServerInfo(pszReplHost, 
								pnReplPort,
								pszReplRoot,
								pszReplBindAs,
								pszReplPw,
								TRUE) ) )
	{
		/* problem with the server info, just return true for Error */
    
		
	} else if (FALSE == FullyQualifyHostName(pszReplHost) )
	{
		/* can't qualify host name, must be invalid */
        DSMessageBoxOK(ERR_INVALID_HOST_TITLE, ERR_INVALID_HOST,
					   pszReplHost, pszReplHost);

		bValueReturned = TRUE;               

	} else if (FALSE == UTF8IsValidLdapUser( pszReplHost, 
											 *pnReplPort, 
											 pszReplRoot, 
											 pszReplBindAs, 
											 pszReplPw, 
											 FALSE) )
	{
		/* can't bind to host with info entered */
		if( IDNO == DSMessageBox(MB_YESNO, ERR_CANT_FIND_DS_REPL_TITLE,
								 ERR_CANT_FIND_DS_REPL, 0, pszReplHost,
								 *pnReplPort, pszReplBindAs) )
		{
			/* user wants to stay on this page and fix value, otherwise,
				allows them to continue even though cant find host
				for replication */

			bValueReturned = TRUE;
		}
	}
	
	/* may want to add more verification here */

	return bValueReturned;
}



//////////////////////////////////////////////////////////////////////////////
// Supplier_Replication_Agreement_DialogProc
//
// get information for replication agreements
// 
// 
// 
//

static BOOL CALLBACK
Supplier_Replication_Agreement_DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  BOOL bValueReturned = FALSE;
  UINT uResult        = 0;
  BOOL bResult        = FALSE;
  HWND hCtrl;

   /* list of controls to be setup for 8bit input/display */
  INT h8bitControls[]={IDC_EDIT_HOST, 
					   IDC_EDIT_SUFFIX,
					   IDC_EDIT_BIND_AS,
					   IDC_EDIT_PW,
					   -1};

  switch (uMsg) 
  {
    case WM_INITDIALOG:
      // This message is sent when the property page is first created.  Here
      // you can perform any one time initialization that you require. 
		Setup8bitInputDisplay(hwndDlg, h8bitControls);

     break;


    case WM_COMMAND:
      // Windows sends WM_COMMAND messages whenever the user clicks on
      // a control in your property page.  If you need to perform some
      // special action, such as validating data or responding to a
      // button click, do it here.
      
      break;

    case WM_NOTIFY:
      // Windows sends WM_NOTIFY messages to your property page whenever
      // something interesting happens to the page.  This could be page
      // activation/deactivation, a button click, etc.  The wParam parameter
      // contains a pointer to the property page.  The lParam parameter
      // contains a pointer to an NMHDR structure.  The code field of this
      // structure contains the notification message code being sent.  The
      // property sheet API allows you to alter the behavior of these
      // messages by returning a value for each message.  To return a value,
      // use the SetWindowLong Windows SDK function.

      switch (((NMHDR*)lParam)->code) 
      {

        case PSN_SETACTIVE:
          // This notification is sent upon activation of the property page.
          // The property sheet should be centered each time it is activated
          // in case the user has moved the stupid thing (this duplicates
          // InstallShield functionality). You should also set the state of
          // the wizard buttons here.
          //
          // NOTE: If you do not wish this page to become active, return -1.

          /* stevross: will have to come back to display this twice for middle case */
          if( SUPPLIER_SIR_REPLICATION  != mi.m_nSetupSupplierReplication  )
          {
            /* we only setup replication agreements for SUPPLIER SIR */
            SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
            return TRUE;
          }

		  /* set text to say Supplier Repl Agreement */
		  SetDlgItemText(hwndDlg, IDC_STATIC_REPLICATION_AGREEMENT, SUPPLIER_REPL_AGREE);

		  /* display values */
		  LoadDlgReplAgreement(hwndDlg,
							   mi.m_szSupplierHost, 
							   mi.m_nSupplierPort,
							   mi.m_szSupplierRoot,
							   mi.m_szSupplierBindAs,
							   mi.m_szSupplierPw,
							   mi.m_szSIRDays,
							   mi.m_szSIRTimes);
		  
		  /* hide the repl sync interval stuff because it only makes since for CIR */
		  hCtrl = GetDlgItem(hwndDlg, IDC_EDIT_REPL_SYNC_INTERVAL);
		  ShowWindow(hCtrl,SW_HIDE);

 		  hCtrl = GetDlgItem(hwndDlg, IDC_STATIC_REPL_SYNC);
		  ShowWindow(hCtrl,SW_HIDE);

		  hCtrl = GetDlgItem(hwndDlg, IDC_SPIN_REPL_SYNC_INTERVAL);
		  ShowWindow(hCtrl,SW_HIDE);


		  /* center window n stuff */
          CenterWindow(GetParent(hwndDlg));
          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_KILLACTIVE:
          // This notification is sent upon deactivation of the property page.
          // Here you can do whatever might be necessary for this action, such
          // as saving the state of the controls.  You should also reset the
          // the state of the wizard buttons here, as both the Back and Next
          // buttons should be active when you leave the AskOptions function.
          //
          // NOTE: If you do not want the page deactivated, return -1.

          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

		case PSN_WIZBACK:
			// The user clicked the back button from the first property page.
			// Set the result code NS_WIZBACK to indicate this action and close
			// the property sheet using brute force.  If this procedure is not
			// being used by the first property sheet, you should ignore this
			// notification and simply let windows go to the previous page.
			//
			// NOTE: To prevent the wizard from stepping back, return -1.
			SaveDlgReplAgreement(hwndDlg, 
								 mi.m_szSupplierHost, 
								 &mi.m_nSupplierPort,
								 mi.m_szSupplierRoot,
								 mi.m_szSupplierBindAs,
								 mi.m_szSupplierPw,
								 mi.m_szSIRDays,
								 mi.m_szSIRTimes);

			break;

		case PSN_WIZNEXT:
			// The user clicked the next button from the last property page.
			// Set the result code NS_WIZNEXT to indicate this action and close
			// the property sheet using brute force.  If this procedure is not
			// being used by the last property sheet, you should ignore this
			// notification and simply let windows go to the next page.
			//
			// NOTE: To prevent the wizard from stepping ahead, return -1.

			/* only bother to check passwords if username is valid */

			SaveDlgReplAgreement(hwndDlg, 
								 mi.m_szSupplierHost, 
								 &mi.m_nSupplierPort,
								 mi.m_szSupplierRoot,
								 mi.m_szSupplierBindAs,
								 mi.m_szSupplierPw,
								 mi.m_szSIRDays,
								 mi.m_szSIRTimes);

			if( TRUE == (bValueReturned = VerifyReplAgreement(mi.m_szSupplierHost, 
												 &mi.m_nSupplierPort,
												 mi.m_szSupplierRoot,
												 mi.m_szSupplierBindAs,
												 mi.m_szSupplierPw,
												 mi.m_szSIRDays,
												 mi.m_szSIRTimes) ) )
			{
				/* one of the settings is invalid, stay on this page */
				SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
			}

    
		  break;

        case PSN_QUERYCANCEL:
          // This notification is sent when the user clicks the Cancel button.
          // It is also sent in response to the WM_CLOSE messages issued
          // by PSN_WIZBACK and PSN_WIZNEXT.  Make sure that we only process
          // this message if the result is not back or next so that we don't
          // nuke the return value assigned by PSN_WIZBACK or PSN_WIZNEXT.
          //
          // NOTE: To prevent the cancel from occuring, return -1.

          if (mi.m_nResult != NS_WIZBACK && mi.m_nResult != NS_WIZNEXT)
          {
            if (QueryExit(hwndDlg)) 
            {
              mi.m_nResult = NS_WIZCANCEL;
            }
            else
            {
              SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
              bValueReturned = TRUE;
            }
          }
          break;
      }
      break;
  }

  return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// SaveDlgInput_Consumer_Replication_Agreement_DialogProc
//
// get info for consumer replication agreements
// 
// 
// 
//
void SaveDlgInput_Consumer_Replication_Agreement(HWND hwndDlg)
{

    BOOL bTrans;

	SaveDlgReplAgreement(hwndDlg, 
						 mi.m_szConsumerHost, 
						 &mi.m_nConsumerPort,
						 mi.m_szConsumerRoot,
						 mi.m_szConsumerBindAs,
						 mi.m_szConsumerPw,
						 mi.m_szCIRDays,
						 mi.m_szCIRTimes);


	/* get the CIR Interval */
	mi.m_nCIRInterval = GetDlgItemInt(hwndDlg, IDC_EDIT_REPL_SYNC_INTERVAL, &bTrans , FALSE);
}

//////////////////////////////////////////////////////////////////////////////
// Consumer_Replication_Agreement_DialogProc
//
// get information for replication agreements
// 
// 
// 
//

static BOOL CALLBACK
Consumer_Replication_Agreement_DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  BOOL bValueReturned = FALSE;
  UINT uResult        = 0;
  BOOL bResult        = FALSE;
  
   /* list of controls to be setup for 8bit input/display */
  INT h8bitControls[]={IDC_EDIT_HOST, 
					   IDC_EDIT_SUFFIX,
					   IDC_EDIT_BIND_AS,
					   IDC_EDIT_PW,
					   -1};
  switch (uMsg) 
  {
    case WM_INITDIALOG:
      // This message is sent when the property page is first created.  Here
      // you can perform any one time initialization that you require. 
	  Setup8bitInputDisplay(hwndDlg, h8bitControls);

      SendDlgItemMessage(hwndDlg, IDC_SPIN_REPL_SYNC_INTERVAL, UDM_SETBUDDY, (WPARAM)GetDlgItem(hwndDlg, IDC_EDIT_REPL_SYNC_INTERVAL), 0);
      SendDlgItemMessage(hwndDlg, IDC_SPIN_REPL_SYNC_INTERVAL, UDM_SETRANGE, 0, MAKELONG((short)59, (short)0));

      break;


    case WM_COMMAND:
      // Windows sends WM_COMMAND messages whenever the user clicks on
      // a control in your property page.  If you need to perform some
      // special action, such as validating data or responding to a
      // button click, do it here.
      
      break;

    case WM_NOTIFY:
      // Windows sends WM_NOTIFY messages to your property page whenever
      // something interesting happens to the page.  This could be page
      // activation/deactivation, a button click, etc.  The wParam parameter
      // contains a pointer to the property page.  The lParam parameter
      // contains a pointer to an NMHDR structure.  The code field of this
      // structure contains the notification message code being sent.  The
      // property sheet API allows you to alter the behavior of these
      // messages by returning a value for each message.  To return a value,
      // use the SetWindowLong Windows SDK function.

      switch (((NMHDR*)lParam)->code) 
      {

        case PSN_SETACTIVE:
          // This notification is sent upon activation of the property page.
          // The property sheet should be centered each time it is activated
          // in case the user has moved the stupid thing (this duplicates
          // InstallShield functionality). You should also set the state of
          // the wizard buttons here.
          //
          // NOTE: If you do not wish this page to become active, return -1.

          /* stevross: will have to come back to display this twice for middle case */
          if( CONSUMER_CIR_REPLICATION  != mi.m_nSetupConsumerReplication )
          {
            /* we only setup replication agreements for CONSUMER CIR */
            SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
            return TRUE;

          }


		  /* make title consumer replication agreement */
		  SetDlgItemText(hwndDlg, IDC_STATIC_REPLICATION_AGREEMENT, CONSUMER_REPL_AGREE);

  		  /* display values */
		  LoadDlgReplAgreement(hwndDlg,
							   mi.m_szConsumerHost, 
							   mi.m_nConsumerPort,
							   mi.m_szConsumerRoot,
							   mi.m_szConsumerBindAs,
							   mi.m_szConsumerPw,
							   mi.m_szCIRDays,
							   mi.m_szCIRTimes);

		  SetDlgItemInt(hwndDlg, IDC_EDIT_REPL_SYNC_INTERVAL, mi.m_nCIRInterval, FALSE);

          CenterWindow(GetParent(hwndDlg));
          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_KILLACTIVE:
          // This notification is sent upon deactivation of the property page.
          // Here you can do whatever might be necessary for this action, such
          // as saving the state of the controls.  You should also reset the
          // the state of the wizard buttons here, as both the Back and Next
          // buttons should be active when you leave the AskOptions function.
          //
          // NOTE: If you do not want the page deactivated, return -1.

          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_WIZBACK:
          // The user clicked the back button from the first property page.
          // Set the result code NS_WIZBACK to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the first property sheet, you should ignore this
          // notification and simply let windows go to the previous page.
          //
          // NOTE: To prevent the wizard from stepping back, return -1.
		  SaveDlgInput_Consumer_Replication_Agreement(hwndDlg);
  		  break;

		case PSN_WIZNEXT:
			// The user clicked the next button from the last property page.
			// Set the result code NS_WIZNEXT to indicate this action and close
			// the property sheet using brute force.  If this procedure is not
			// being used by the last property sheet, you should ignore this
			// notification and simply let windows go to the next page.
			//
			// NOTE: To prevent the wizard from stepping ahead, return -1.

			/* only bother to check passwords if username is valid */

			SaveDlgInput_Consumer_Replication_Agreement(hwndDlg);
			if( TRUE == (bValueReturned = VerifyReplAgreement(mi.m_szConsumerHost, 
												 &mi.m_nConsumerPort,
												 mi.m_szConsumerRoot,
												 mi.m_szConsumerBindAs,
												 mi.m_szConsumerPw,
												 mi.m_szCIRDays,
												 mi.m_szCIRTimes) ) )
			{
				/* one of the settings invalid, stay on this page */
				SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
			}		     


		  break;

        case PSN_QUERYCANCEL:
          // This notification is sent when the user clicks the Cancel button.
          // It is also sent in response to the WM_CLOSE messages issued
          // by PSN_WIZBACK and PSN_WIZNEXT.  Make sure that we only process
          // this message if the result is not back or next so that we don't
          // nuke the return value assigned by PSN_WIZBACK or PSN_WIZNEXT.
          //
          // NOTE: To prevent the cancel from occuring, return -1.

          if (mi.m_nResult != NS_WIZBACK && mi.m_nResult != NS_WIZNEXT)
          {
            if (QueryExit(hwndDlg)) 
            {
              mi.m_nResult = NS_WIZCANCEL;
            }
            else
            {
              SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
              bValueReturned = TRUE;
            }
          }
          break;
      }
      break;
  }

  return bValueReturned;
}

//////////////////////////////////////////////////////////////////////////////
// Sample_Entries_Org_DialogProc
//
// ask user if they want to populate with sample entries and sample organization
// 
// 
// 
//

static BOOL CALLBACK
Sample_Entries_Org_DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  BOOL bValueReturned = FALSE;
  UINT uResult        = 0;
  BOOL bResult        = FALSE;
  CHAR szMustHaveBase[MAX_STR_SIZE]="\0";
  static CHAR szCustomFileName[MAX_STR_SIZE]="\0";
  static CHAR szSampleFileName[MAX_STR_SIZE]="\0";
  
  /* list of controls to be setup for 8bit input/display */
  INT h8bitControls[]={IDC_STATIC_MUST_HAVE_BASE, 
					   IDC_STATIC_LDIF_FILE_NAME,
					   -1};


  switch (uMsg) 
  {
    case WM_INITDIALOG:
      // This message is sent when the property page is first created.  Here
      // you can perform any one time initialization that you require. 
		  /* default is to populate with sample entries */

	    Setup8bitInputDisplay(hwndDlg, h8bitControls);

     break;


    case WM_COMMAND:
      // Windows sends WM_COMMAND messages whenever the user clicks on
      // a control in your property page.  If you need to perform some
      // special action, such as validating data or responding to a
      // button click, do it here.
	
		switch (LOWORD(wParam))
		{
	
			case IDC_BUTTON_CHOOSE_LDIF_FILE:
				GetFileName(hwndDlg, "Choose ldif file to import", 
					        "Ldif Files|*.ldif|All Files|*.*|",
							NULL, szCustomFileName, MAX_PATH );
				
				/* assume by browsing user will want this file so check custom radio button for them
				   and set file to be displayed */
				sprintf(mi.m_szPopLdifFile, "%s", szCustomFileName);
				CheckRadioButton(hwndDlg, IDC_RADIO_DONT_POPULATE, IDC_RADIO_POPULATE_CUSTOM, IDC_RADIO_POPULATE_CUSTOM);

			default:

			  mi.m_nPopulateSampleOrg = (int )(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_CHECK_POPULATE_ORG_ENTRIES ) );
			  
			  if( BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_RADIO_POPULATE_SAMPLE ) )
			  {
					mi.m_nPopulateSampleEntries = 1;
					sprintf(mi.m_szPopLdifFile, "%s", szSampleFileName);
			  }else if( BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_RADIO_POPULATE_CUSTOM ) ){
					mi.m_nPopulateSampleEntries = 0;
					mi.m_nPopulateSampleOrg = 1;
//		     		sprintf(mi.m_szPopLdifFile, "%s", szCustomFileName);
			  }else{
					mi.m_nPopulateSampleEntries = 0;
					sprintf(mi.m_szPopLdifFile, "\0");
			  }

			  
			break;

		}
		/* update ldif file name if changed */
		SetDlgItemText(hwndDlg, IDC_STATIC_LDIF_FILE_NAME, mi.m_szPopLdifFile);

      break;

    case WM_NOTIFY:
      // Windows sends WM_NOTIFY messages to your property page whenever
      // something interesting happens to the page.  This could be page
      // activation/deactivation, a button click, etc.  The wParam parameter
      // contains a pointer to the property page.  The lParam parameter
      // contains a pointer to an NMHDR structure.  The code field of this
      // structure contains the notification message code being sent.  The
      // property sheet API allows you to alter the behavior of these
      // messages by returning a value for each message.  To return a value,
      // use the SetWindowLong Windows SDK function.

      switch (((NMHDR*)lParam)->code) 
      {

	
        case PSN_SETACTIVE:
          // This notification is sent upon activation of the property page.
          // The property sheet should be centered each time it is activated
          // in case the user has moved the stupid thing (this duplicates
          // InstallShield functionality). You should also set the state of
          // the wizard buttons here.
          //
          // NOTE: If you do not wish this page to become active, return -1.
          if(   (CONSUMER_CIR_REPLICATION  == mi.m_nSetupConsumerReplication)
             || (CONSUMER_SIR_REPLICATION  == mi.m_nSetupConsumerReplication ) )
          {
            /* we only want to populate if they are not doing CIR */
            SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
            return TRUE;

          }
		  
		  /* warn user about suffix and database import */
		  sprintf(szMustHaveBase, "(note: must have base %s)", mi.m_szInstanceSuffix );
		  SetDlgItemText(hwndDlg, IDC_STATIC_MUST_HAVE_BASE, szMustHaveBase);
    	  sprintf(szSampleFileName, "%s\\%s", TARGETDIR, SAMPLE_LDIF);	
 
		  if(mi.m_nExistingUG == 0)
		  {
			/* the user is creating a new UG with this instance */
		
			/* create ou=People/ou=Groups */
//			mi.m_nPopulateSampleOrg = 1;
//			lstrcpy(mi.m_szPopLdifFile, SUGGEST_LDIF);
		  }

		  if(mi.m_nPopulateSampleEntries)
		  {
			  CheckRadioButton(hwndDlg, IDC_RADIO_DONT_POPULATE,
							   IDC_RADIO_POPULATE_CUSTOM,
							   IDC_RADIO_POPULATE_SAMPLE);
		  } else if (mi.m_szPopLdifFile[0]) {
			  CheckRadioButton(hwndDlg, IDC_RADIO_DONT_POPULATE,
							   IDC_RADIO_POPULATE_CUSTOM,
							   IDC_RADIO_POPULATE_CUSTOM);
		  } else {
			  CheckRadioButton(hwndDlg, IDC_RADIO_DONT_POPULATE,
							   IDC_RADIO_POPULATE_CUSTOM,
							   IDC_RADIO_DONT_POPULATE);
		  }

		  if(mi.m_nPopulateSampleOrg)
		  {
		      CheckDlgButton(hwndDlg, IDC_CHECK_POPULATE_ORG_ENTRIES, BST_CHECKED); 
		  }

		  SetDlgItemText(hwndDlg, IDC_STATIC_LDIF_FILE_NAME, mi.m_szPopLdifFile);
    
		  CenterWindow(GetParent(hwndDlg));
          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_KILLACTIVE:
          // This notification is sent upon deactivation of the property page.
          // Here you can do whatever might be necessary for this action, such
          // as saving the state of the controls.  You should also reset the
          // the state of the wizard buttons here, as both the Back and Next
          // buttons should be active when you leave the AskOptions function.
          //
          // NOTE: If you do not want the page deactivated, return -1.

          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_WIZBACK:
          // The user clicked the back button from the first property page.
          // Set the result code NS_WIZBACK to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the first property sheet, you should ignore this
          // notification and simply let windows go to the previous page.
          //
          // NOTE: To prevent the wizard from stepping back, return -1.
		   
		  /* all settings for this dailog are saved in WM_COMMAND processing */
  		  break;

        case PSN_WIZNEXT:
          // The user clicked the next button from the last property page.
          // Set the result code NS_WIZNEXT to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the last property sheet, you should ignore this
          // notification and simply let windows go to the next page.
          //
          // NOTE: To prevent the wizard from stepping ahead, return -1.
		
		  /* all settings for this dailog are saved in WM_COMMAND processing */
		  break;

        case PSN_QUERYCANCEL:
          // This notification is sent when the user clicks the Cancel button.
          // It is also sent in response to the WM_CLOSE messages issued
          // by PSN_WIZBACK and PSN_WIZNEXT.  Make sure that we only process
          // this message if the result is not back or next so that we don't
          // nuke the return value assigned by PSN_WIZBACK or PSN_WIZNEXT.
          //
          // NOTE: To prevent the cancel from occuring, return -1.

          if (mi.m_nResult != NS_WIZBACK && mi.m_nResult != NS_WIZNEXT)
          {
            if (QueryExit(hwndDlg)) 
            {
              mi.m_nResult = NS_WIZCANCEL;
            }
            else
            {
              SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
              bValueReturned = TRUE;
            }
          }
          break;
      }
      break;
  }

  return bValueReturned;
}



//////////////////////////////////////////////////////////////////////////////
// Disable_Schema_Checking_DialogProc
//
// ask user if they want to disable schema checking
// 
// 
// 
//

static BOOL CALLBACK
Disable_Schema_Checking_DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  BOOL bValueReturned = FALSE;
  UINT uResult        = 0;
  BOOL bResult        = FALSE;
  

  switch (uMsg) 
  {
    case WM_INITDIALOG:
      // This message is sent when the property page is first created.  Here
      // you can perform any one time initialization that you require. 
		  /* default is to populate with sample entries */
  		
      break;


    case WM_COMMAND:
      // Windows sends WM_COMMAND messages whenever the user clicks on
      // a control in your property page.  If you need to perform some
      // special action, such as validating data or responding to a
      // button click, do it here.
	
		switch (LOWORD(wParam))
		{
	
			default:

				mi.m_nDisableSchemaChecking = (int )(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_CHECK_DISABLE_SCHEMA_CHECKING ) );
			break;

		}

      break;

    case WM_NOTIFY:
      // Windows sends WM_NOTIFY messages to your property page whenever
      // something interesting happens to the page.  This could be page
      // activation/deactivation, a button click, etc.  The wParam parameter
      // contains a pointer to the property page.  The lParam parameter
      // contains a pointer to an NMHDR structure.  The code field of this
      // structure contains the notification message code being sent.  The
      // property sheet API allows you to alter the behavior of these
      // messages by returning a value for each message.  To return a value,
      // use the SetWindowLong Windows SDK function.

      switch (((NMHDR*)lParam)->code) 
      {

	
        case PSN_SETACTIVE:
          // This notification is sent upon activation of the property page.
          // The property sheet should be centered each time it is activated
          // in case the user has moved the stupid thing (this duplicates
          // InstallShield functionality). You should also set the state of
          // the wizard buttons here.
          //
          // NOTE: If you do not wish this page to become active, return -1.
            
		  if(mi.m_nDisableSchemaChecking)
		  {
		      CheckDlgButton(hwndDlg, IDC_CHECK_DISABLE_SCHEMA_CHECKING, BST_CHECKED); 
		  }

		  CenterWindow(GetParent(hwndDlg));
          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_KILLACTIVE:
          // This notification is sent upon deactivation of the property page.
          // Here you can do whatever might be necessary for this action, such
          // as saving the state of the controls.  You should also reset the
          // the state of the wizard buttons here, as both the Back and Next
          // buttons should be active when you leave the AskOptions function.
          //
          // NOTE: If you do not want the page deactivated, return -1.

          PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_BACK | PSWIZB_NEXT);
          break;

        case PSN_WIZBACK:
          // The user clicked the back button from the first property page.
          // Set the result code NS_WIZBACK to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the first property sheet, you should ignore this
          // notification and simply let windows go to the previous page.
          //
          // NOTE: To prevent the wizard from stepping back, return -1.
		  
		  /* all settings for this dailog are saved in WM_COMMAND processing */
  		  break;

        case PSN_WIZNEXT:
          // The user clicked the next button from the last property page.
          // Set the result code NS_WIZNEXT to indicate this action and close
          // the property sheet using brute force.  If this procedure is not
          // being used by the last property sheet, you should ignore this
          // notification and simply let windows go to the next page.
          //
          // NOTE: To prevent the wizard from stepping ahead, return -1.

     
          /* this is the last property page of advanced mode whenever we display it*/
          mi.m_nResult = NS_WIZNEXT;
          SendMessage(GetParent(hwndDlg), WM_CLOSE, 0, 0);     
  		  
		  /* all settings for this dailog are saved in WM_COMMAND processing */
		  break;

        case PSN_QUERYCANCEL:
          // This notification is sent when the user clicks the Cancel button.
          // It is also sent in response to the WM_CLOSE messages issued
          // by PSN_WIZBACK and PSN_WIZNEXT.  Make sure that we only process
          // this message if the result is not back or next so that we don't
          // nuke the return value assigned by PSN_WIZBACK or PSN_WIZNEXT.
          //
          // NOTE: To prevent the cancel from occuring, return -1.

          if (mi.m_nResult != NS_WIZBACK && mi.m_nResult != NS_WIZNEXT)
          {
            if (QueryExit(hwndDlg)) 
            {
              mi.m_nResult = NS_WIZCANCEL;
            }
            else
            {
              SetWindowLong(hwndDlg, DWL_MSGRESULT, -1);
              bValueReturned = TRUE;
            }
          }
          break;
      }
      break;
  }

  return bValueReturned;
}

void initialize_module()
{

	mi.m_nResult                       = 0;
	mi.m_szMCCBindAs                   = NULL;
	mi.m_nInstanceServerPort           = 0;
	mi.m_nAdminServerPort              = 0;
	mi.m_nCfgSspt                      = 0;
	mi.m_nPopulateSampleEntries        = 0;
	mi.m_nPopulateSampleOrg            = 1;
	mi.m_nSetupConsumerReplication     = 0;
	mi.m_nSetupSupplierReplication     = 0;
	mi.m_nMaxChangeLogRecords          = 0;
	mi.m_nMaxChangeLogAge              = 0;
	mi.m_nChangeLogAgeMagnitude        = 0;
	mi.m_nConsumerSSL                  = 0;
	mi.m_nSupplierSSL                  = 0;
	mi.m_nUseSupplierSettings          = 0;
	mi.m_nUseChangeLogSettings         = 0;
	mi.m_nCIRInterval                  = 0;
	mi.m_nConsumerPort                 = 0;
	mi.m_nSupplierPort                 = 0;
	mi.m_nMCCPort                      = 0;
	mi.m_nExistingMCC                  = 0;
	mi.m_nUGPort                       = 0;
	mi.m_nExistingUG                   = 0;
	mi.m_nDisableSchemaChecking        = 0;
	mi.m_nSNMPOn                       = 0;
	mi.m_nConfigConsumerDN             = 0;

	memset(mi.m_szMCCPw,  			  			'\0', MAX_STR_SIZE);
	memset(mi.m_szMCCHost,        				'\0', MAX_STR_SIZE);
	memset(mi.m_szMCCSuffix,        			'\0', MAX_STR_SIZE);
	memset(mi.m_szUGPw,        					'\0', MAX_STR_SIZE);
	memset(mi.m_szUGHost,        				'\0', MAX_STR_SIZE);
	memset(mi.m_szUGSuffix,        				'\0', MAX_STR_SIZE);
	memset(mi.m_szAdminDomain,        			'\0', MAX_STR_SIZE);
	memset(mi.m_szLdapURL,        				'\0', MAX_STR_SIZE);
	memset(mi.m_szUserGroupURL, 			    '\0', MAX_STR_SIZE);
	memset(mi.m_szUserGroupAdmin,   		    '\0', MAX_STR_SIZE);
	memset(mi.m_szUserGroupAdminPW,	   		    '\0', MAX_STR_SIZE);
	memset(mi.m_szInstallDN,    			    '\0', MAX_STR_SIZE);
	memset(mi.m_szSsptUid,        				'\0', MAX_STR_SIZE);
	memset(mi.m_szSsptUidPw,      				'\0', MAX_STR_SIZE);
	memset(mi.m_szSsptUidPwAgain, 		        '\0', MAX_STR_SIZE);
	memset(mi.m_szSsptUser,        				'\0', MAX_STR_SIZE);
	memset(mi.m_szServerIdentifier,        		'\0', MAX_STR_SIZE);
	memset(mi.m_szInstanceSuffix,				'\0', MAX_STR_SIZE);
	memset(mi.m_szInstanceUnrestrictedUser,		'\0', MAX_STR_SIZE);
	memset(mi.m_szInstancePassword,        		'\0', MAX_STR_SIZE);
	memset(mi.m_szInstancePasswordAgain,       	'\0', MAX_STR_SIZE);
	memset(mi.m_szInstanceHostName,        		'\0', MAX_STR_SIZE);
	memset(mi.m_szSupplierDN,        			'\0', MAX_STR_SIZE);
	memset(mi.m_szSupplierPW,        			'\0', MAX_STR_SIZE);
	memset(mi.m_szSupplierPWAgain,       		'\0', MAX_STR_SIZE);
	memset(mi.m_szSSLClients,       			'\0', MAX_STR_SIZE);
	memset(mi.m_szChangeLogDbDir,        		'\0', MAX_STR_SIZE);
	memset(mi.m_szChangeLogSuffix,        		'\0', MAX_STR_SIZE);
	memset(mi.m_szConsumerDN,        			'\0', MAX_STR_SIZE);
	memset(mi.m_szConsumerPW,        			'\0', MAX_STR_SIZE);
	memset(mi.m_szConsumerPWAgain,        		'\0', MAX_STR_SIZE);
	memset(mi.m_szConsumerHost,        			'\0', MAX_STR_SIZE);
	memset(mi.m_szConsumerRoot,        			'\0', MAX_STR_SIZE);
	memset(mi.m_szConsumerBindAs,        		'\0', MAX_STR_SIZE);
	memset(mi.m_szConsumerPw,        			'\0', MAX_STR_SIZE);
	memset(mi.m_szSupplierHost,        			'\0', MAX_STR_SIZE);
	memset(mi.m_szSupplierRoot,        			'\0', MAX_STR_SIZE);
	memset(mi.m_szSupplierBindAs,        		'\0', MAX_STR_SIZE);
	memset(mi.m_szSupplierPw,        			'\0', MAX_STR_SIZE);
	memset(mi.m_szPopLdifFile,        			'\0', MAX_STR_SIZE);
	memset(mi.m_szCIRDays,             			'\0', N_DAYS);
	memset(mi.m_szCIRTimes,          			'\0', N_TIMES);
	memset(mi.m_szSIRDays,          			'\0', N_DAYS);
	memset(mi.m_szSIRTimes,          			'\0', N_TIMES);
}

//////////////////////////////////////////////////////////////////////////////
//getSNMPStatus() 
//
// sets module info for state of SNMPService
// if its on asks user if ok to turn it off
// if user says no or there is an error it returns false
//

BOOL getSNMPStatus()
{

	BOOL bReturn = TRUE;

	if ( TRUE == isServiceRunning(SNMP_SERVICE) )
	{
		// its running
		if (SILENTMODE == MODE)
		{
			/* don't prompt user, jsut turn it off */
			if ( 0 == ControlServer(SNMP_SERVICE, FALSE) )
			{
				DSMessageBoxOK(ERR_SNMP_BAD_SHUTDOWN_TITLE,
							   ERR_SNMP_BAD_SHUTDOWN, 0);
				bReturn = FALSE;
			}
			mi.m_nSNMPOn = 1;
		} else
		{

			/* ask the user what they want to do */
			if ( IDOK == DSMessageBox(MB_OKCANCEL, ERR_SNMP_IS_RUNNING_TITLE,
									  ERR_SNMP_IS_RUNNING, 0) )
			{
				/* save state for later use by cache so we know what to do in post install */
				mi.m_nSNMPOn = 1;
				if ( 0 == ControlServer(SNMP_SERVICE, FALSE) )
				{
					DSMessageBoxOK(ERR_SNMP_BAD_SHUTDOWN_TITLE,
								   ERR_SNMP_BAD_SHUTDOWN, 0);
					bReturn = FALSE;
				}

			} else
			{
				UINT uExitCode = 1;
				/* stevross: use ExitProcess until admin server provides us with
							 better way to exit framework and cleanup */
				ExitProcess(uExitCode);
			}

		}

	} else
	{
		mi.m_nSNMPOn = 0;
	}

	return bReturn;

}


/////////////////////////////////////////////////////////////////
//
//  somehow determine if slapd is installed under this server root
//
//

BOOL slapdExists(char *pszServerRoot)
{
	BOOL bReturn = FALSE;
    WIN32_FIND_DATA fileData;
    HANDLE hFileHandle;
	CHAR szCurrentDir[MAX_STR_SIZE]="\0";

	/* not sure what the right way to check is, try this for now */
    /* check if any slapd instances exist in server root */
    /* later look in directory for anything slapd- */

	/* get current dir so we have it for later */
	GetCurrentDirectory(MAX_STR_SIZE, szCurrentDir);
	
	/* change current dir to server root */
	SetCurrentDirectory(pszServerRoot);

	hFileHandle = FindFirstFile("slapd-*", &fileData);

	if( INVALID_HANDLE_VALUE != hFileHandle)
	{
		/* found slapd- something */
		bReturn = TRUE;
	}

	/* set back to previous current directory */
	SetCurrentDirectory(szCurrentDir);

    return bReturn;
}


/////////////////////////////////////////////////////////////////
//
//  turn slapd instances in this server root on or off
//
//

BOOL ControlSlapdServers(char *pszServerRoot, BOOL bOn, BOOL fixPwd)
{
	BOOL bReturn = FALSE;
    WIN32_FIND_DATA fileData;
    HANDLE hFileHandle;
	CHAR szCurrentDir[MAX_STR_SIZE]="\0";

	/* not sure what the right way to check is, try this for now */
    /* check if any slapd instances exist in server root */
    /* later look in directory for anything slapd- */

	/* get current dir so we have it for later */
	GetCurrentDirectory(MAX_STR_SIZE, szCurrentDir);
	
	/* change current dir to server root */
	SetCurrentDirectory(pszServerRoot);

	hFileHandle = FindFirstFile("slapd-*", &fileData);
	if( INVALID_HANDLE_VALUE != hFileHandle)
	{
		if (fixPwd)
		{
			/* convert password file to new pin format */
			ConvertPasswordToPin(pszServerRoot, fileData.cFileName);
			/* do any server upgrade stuff */
			ReinstallUpgradeServer(pszServerRoot, fileData.cFileName);
		}
		
		/* turn on server */
		ControlSlapdInstance(fileData.cFileName, bOn);
		
		while(TRUE == FindNextFile(hFileHandle, &fileData) )
		{
			if (fixPwd)
			{
				/* convert password file to new pin format */
				ConvertPasswordToPin(pszServerRoot, fileData.cFileName);
				/* do any server upgrade stuff */
				ReinstallUpgradeServer(pszServerRoot, fileData.cFileName);
			}

			/* turn on server */
			ControlSlapdInstance(fileData.cFileName, bOn);
		}	
	}

	/* wait to make sure give server enough time startup/shutdown*/
	/* this time should be long enough for all instances we just */
	/* tried to shutdown/startup */
	Sleep(SLAPD_SHUTDOWN_TIME_MILLISECONDS);


	/* set back to previous current directory */
	SetCurrentDirectory(szCurrentDir);

    return bReturn;
}


//////////////////////////////////////////////////////////////////////////////
// TMPL_PreInstall
//
// This function is called by the installation framework before asking the
// user any questions.  Here you should determine if all of the requisites
// for installing this component are being met.  If this operation succeeds
// return TRUE, otherwise display an error message and return FALSE to abort
// installation.
//

BOOL __declspec(dllexport)
DSINST_PreInstall(LPCSTR lpszInstallPath)
{
	BOOL bReturn = FALSE;
	// TODO: Add code to check for pre-installation requirements.

	if( TRUE == getSNMPStatus() )
	{
		char * szLdapURL = NULL;
		char * szLdapUser = NULL;
		char * szAdminDomain = NULL;

		getDefaultLdapInfo(TARGETDIR, &szLdapURL, &szLdapUser,
						   &szAdminDomain);
		if (szLdapURL && szAdminDomain)
		{
			lstrcpy(mi.m_szLdapURL, szLdapURL);
			GetURLComponents(mi.m_szLdapURL, mi.m_szMCCHost, 
							 &mi.m_nMCCPort, mi.m_szMCCSuffix);
			if (mi.m_szMCCSuffix[0] == 0)
				lstrcpy(mi.m_szMCCSuffix, NS_DOMAIN_ROOT);
			if (szLdapUser && mi.m_szMCCBindAs)
				lstrcpy(mi.m_szMCCBindAs, szLdapUser);
			lstrcpy(mi.m_szAdminDomain, szAdminDomain);
			// since this server root is already configured to use
			// an existing configuration directory server, we will
			// not allow the user to install another one here, so
			// the directory server created here will be a user
			// directory; we will still need to ask for the admin
			// user password
			mi.m_nExistingMCC = 1;
			mi.m_nExistingUG = 0;
			mi.m_nCfgSspt = 0;

			// it's only a reinstall if there is already a slapd
			// installed in this server root
			if( slapdExists(TARGETDIR) )
				mi.m_nReInstall = 1;
			else
				mi.m_nReInstall = 0;
		}
		bReturn = TRUE;
	}

	if (mi.m_nReInstall) {
		char infFile[MAX_PATH] = {0};
		sprintf(infFile, "%s\\setup\\slapd\\slapd.inf", TARGETDIR);
		GetProductInfoStringWithTok(SETUP_INF_VERSION, "=", oldVersion,
								OLD_VERSION_SIZE, infFile);
		myLogData("file %s old version is %s", infFile, oldVersion);
	}

	return bReturn;
}

//////////////////////////////////////////////////////////////////////////////
// DSINST_AskOptions
//
// This function is called by the installation framework to query the user for
// information about your component.  Here you should ask all of the questions
// required to install your component as a series of wizard property sheets.
//

INT __declspec(dllexport)
DSINST_AskOptions(HWND hwndParent, INT nDirection)
{
	PROPSHEETPAGE psp[NUM_PROP_PAGES];
	UINT uStartPage;
	INT nNumPages = 0;
	static INT wasExistingMCC = -1;

	// TODO: Initialize a property page for each dialog template/resource
	// required to query the user for options related to your server
	// installation.  Don't forget to increment the count of pages contained
	// in NUM_PROP_PAGES at the top of this file.

	/* Keep the value of mi.m_nExistingMCC at the first invocation */
	if (wasExistingMCC == -1)
	{
		wasExistingMCC = mi.m_nExistingMCC;
	}

	/* if in silent mode or reinstalling, don't display any property pages */
	if ( SILENTMODE == MODE )
	{
		mi.m_nResult = nDirection; // keep moving in same direction...
	}else
	{
		if(1 == mi.m_nReInstall)
		{
			uStartPage = ((nDirection == NS_WIZNEXT) ? 0 : 0);
			AddWizardPage(mi.m_hModule, &psp[0], IDD_REINSTALL_CONFIG, ReInstall_DialogProc);
			nNumPages = 1;
		}else{

			if (EXPRESSMODE == MODE)
			{
				/* just ask for Suitespot ID and Unrestricted User */
				uStartPage = ((nDirection == NS_WIZNEXT) ? 0 : 1);
				if (mi.m_nExistingMCC) // just need admin id and pwd
					AddWizardPage(mi.m_hModule, &psp[0], IDD_ADMIN_ID_ONLY,
								  Admin_ID_Only_DialogProc);
				else
					AddWizardPage(mi.m_hModule, &psp[0], IDD_SUITESPOTID,
								  SuitespotID_DialogProc); 
				AddWizardPage(mi.m_hModule, &psp[1], IDD_ROOTDN, RootDN_DialogProc); 

				/* make sure to set numprop pages to actual number */
				nNumPages = 2;
			} else if ( (NORMALMODE == MODE) || (CUSTOMMODE == MODE) )
			{
				/* ask for server settings, SuitespotID and Unrestricted User */

				if ((NORMALMODE == MODE) && wasExistingMCC)
				{
					AddWizardPage(mi.m_hModule, &psp[nNumPages++],
								  IDD_SERVER_SETTINGS, Server_Settings_DialogProc);
					AddWizardPage(mi.m_hModule, &psp[nNumPages++],
								  IDD_ADMIN_ID_ONLY, Admin_ID_Only_DialogProc);
					AddWizardPage(mi.m_hModule, &psp[nNumPages++],
								  IDD_ROOTDN, RootDN_DialogProc);
				}
				else
				{
					AddWizardPage(mi.m_hModule, &psp[nNumPages++],
								  IDD_MCC_SETTINGS, MCC_Settings_DialogProc);
					AddWizardPage(mi.m_hModule, &psp[nNumPages++],
								  IDD_ADMIN_DOMAIN, AdminDomainCustom_DialogProc);
					AddWizardPage(mi.m_hModule, &psp[nNumPages++],
								  IDD_MCC_SETTINGS, UG_Settings_DialogProc);
					AddWizardPage(mi.m_hModule, &psp[nNumPages++],
								  IDD_SERVER_SETTINGS, Server_Settings_DialogProc);
					AddWizardPage(mi.m_hModule, &psp[nNumPages++],
								  IDD_SUITESPOTID, SuitespotID_DialogProc);
					AddWizardPage(mi.m_hModule, &psp[nNumPages++],
								  IDD_ADMIN_DOMAIN, AdminDomain_DialogProc);
					AddWizardPage(mi.m_hModule, &psp[nNumPages++],
								  IDD_ROOTDN, RootDN_DialogProc);
				}

				/* add additional pages for custom mode */
				if ( (CUSTOMMODE == MODE) )
				{
#ifdef CUSTOM_REPL_FOR_4X
					AddWizardPage(mi.m_hModule, &psp[nNumPages++],
								  IDD_CHOOSE_REPLICATION_SETUP,
								  Choose_Replication_DialogProc);
					AddWizardPage(mi.m_hModule, &psp[nNumPages++],
								  IDD_SUPPLIER_REPLICATION_SETTINGS,
								  Supplier_Replication_DialogProc);
					AddWizardPage(mi.m_hModule, &psp[nNumPages++],
								  IDD_CONSUMER_DN, Consumer_DN_DialogProc);
					AddWizardPage(mi.m_hModule, &psp[nNumPages++],
								  IDD_REPLICATION_AGREEMENT,
								  Supplier_Replication_Agreement_DialogProc);
					AddWizardPage(mi.m_hModule, &psp[nNumPages++],
								  IDD_CONSUMER_REPLICATION_SETTINGS,
								  Consumer_Replication_DialogProc);
					AddWizardPage(mi.m_hModule, &psp[nNumPages++],
								  IDD_REPLICATION_AGREEMENT,
								  Consumer_Replication_Agreement_DialogProc);  
#endif
					AddWizardPage(mi.m_hModule, &psp[nNumPages++],
								  IDD_SAMPLE_ENTRIES_ORG,
								  Sample_Entries_Org_DialogProc);
					AddWizardPage(mi.m_hModule, &psp[nNumPages++],
								  IDD_DISABLE_SCHEMA_CHECKING,
								  Disable_Schema_Checking_DialogProc);
				}
				uStartPage = ((nDirection == NS_WIZNEXT) ? 0 : (nNumPages-1));
			}

		}

		// Must initialize the result to an error code before calling WizardDialog
		mi.m_nResult = NS_WIZERROR;

		// Set the first page to display based on the direction we are travelling


		// Call WizardDialog to display the set of property pages
		if (WizardDialog(mi.m_hModule, hwndParent, psp, nNumPages, uStartPage) < 0)
		{
			mi.m_nResult = NS_WIZERROR;
		}

	}

	// convert all DN valued attributes to LDAPv3 quoting
	normalizeDNs();

	// store the User directory information
	storeUserDirectoryInfo();

	if (1 == mi.m_nReInstall)
	{
		set_ldap_settings();
	}

	return mi.m_nResult;
}

//////////////////////////////////////////////////////////////////////////////
// DSINST_GetSummary
//
// This function is called by the installation framework after all questions,
// for all components, have been asked.  Here you should provide a detailed
// summary explaining all of the choices selected by the user.
//
// IMPORTANT NOTE: Each line MUST end in a carriage return/line feed
// combination ("\r\n") as this string is placed in an edit control.  Edit
// controls do not properly handle single "\n" end-of-line characters.
//

VOID __declspec(dllexport)
DSINST_GetSummary(LPSTR lpszSummary)
{

	// TODO: Add code to fill in the summary information entered by the user
	char *psz = lpszSummary;

	/* only use replication settings written to slapd.conf if dialogs were 
		seen by user... otherwise set them to null so posted as null */

	if ( 1 != mi.m_nUseSupplierSettings)
	{
		memset(mi.m_szSupplierDN, '\0', MAX_STR_SIZE);
	}
	if ( 1 != mi.m_nUseChangeLogSettings)
	{
		memset(mi.m_szChangeLogDbDir,  '\0', MAX_STR_SIZE);
		memset(mi.m_szChangeLogSuffix, '\0', MAX_STR_SIZE);
	}

	if ( 1 != mi.m_nConfigConsumerDN)
	{
		memset(mi.m_szConsumerDN, '\0', MAX_STR_SIZE);
	}

	/* display in order of dialogs */
	if ( 1 == mi.m_nReInstall )
	{
		psz += WriteSummaryStringRC(psz, "  %s\r\n",         mi.m_hModule, SUM_REINSTALL, NULL);
	} else
	{


		/* if installing into existing configuration directory display settings entered */
		if ( 1 == mi.m_nExistingMCC)
		{
			psz += WriteSummaryStringRC(psz, "  %s\r\n",         mi.m_hModule, SUM_CONFIG_DS_TITLE, NULL);
			psz += WriteSummaryStringRC(psz, "      %s: %s\r\n", mi.m_hModule, SUM_HOST, mi.m_szMCCHost);
			psz += WriteSummaryIntRC(psz,    "      %s: %d\r\n", mi.m_hModule, SUM_PORT, mi.m_nMCCPort);
			psz += WriteSummaryStringRC(psz, "      %s: %s\r\n", mi.m_hModule, SUM_SUFFIX, mi.m_szMCCSuffix);
			psz += WriteSummaryStringRC(psz, "      %s: %s\r\n", mi.m_hModule, SUM_BIND_AS, mi.m_szMCCBindAs);
		}

		/* if storing data in existing directory display options entered */
		if ( 1 == mi.m_nExistingUG)
		{
			psz += WriteSummaryStringRC(psz, "  %s\r\n",         mi.m_hModule, SUM_DATA_DS_TITLE, NULL);
			psz += WriteSummaryStringRC(psz, "      %s: %s\r\n", mi.m_hModule, SUM_HOST, mi.m_szUGHost);
			psz += WriteSummaryIntRC(psz,    "      %s: %d\r\n", mi.m_hModule, SUM_PORT, mi.m_nUGPort);
			psz += WriteSummaryStringRC(psz, "      %s: %s\r\n", mi.m_hModule, SUM_SUFFIX, mi.m_szUGSuffix);
			psz += WriteSummaryStringRC(psz, "      %s: %s\r\n", mi.m_hModule, SUM_BIND_AS, mi.m_szUserGroupAdmin);
		}

		/* display instance settings */
		psz += WriteSummaryStringRC(psz, "  %s\r\n",         mi.m_hModule, SUM_DS_SET_TITLE, NULL);
		psz += WriteSummaryStringRC(psz, "      %s: %s\r\n", mi.m_hModule, SUM_SERVER_IDENTIFIER, mi.m_szServerIdentifier);
		psz += WriteSummaryStringRC(psz, "      %s: %s\r\n", mi.m_hModule, SUM_SUFFIX, mi.m_szInstanceSuffix);
		psz += WriteSummaryIntRC(psz,    "      %s: %d\r\n", mi.m_hModule, SUM_PORT,    mi.m_nInstanceServerPort);

		if ( 1 != mi.m_nExistingMCC)
		{
			/* if not using Existing MCC display configuration Admin id*/
			/* otherwise it is displayed as the Bind As under Configuration Directory */
			psz += WriteSummaryStringRC(psz, "      %s: %s\r\n", mi.m_hModule, SUM_CFG_ADM_ID, mi.m_szSsptUid);
		}
		psz += WriteSummaryStringRC(psz, "      %s: %s\r\n", mi.m_hModule, SUM_ADMIN_DOMAIN, mi.m_szAdminDomain);
		psz += WriteSummaryStringRC(psz, "      %s: %s\r\n", mi.m_hModule, SUM_DIRECTORY_MANAGER, mi.m_szInstanceUnrestrictedUser);

		/* replication settings */
		if (mi.m_nSetupSupplierReplication != NO_REPLICATION)
		{
			psz += WriteSummaryStringRC(psz, "      %s\r\n", mi.m_hModule, SUM_SUPPLIER_REPL_TITLE, NULL);

			/* display changelog DB dir and suffix for both supplier replication modes */
			psz += WriteSummaryStringRC(psz, "          %s: %s\r\n", mi.m_hModule, SUM_CHANGELOG_DB_DIR, mi.m_szChangeLogDbDir);
			psz += WriteSummaryStringRC(psz, "          %s: %s\r\n", mi.m_hModule, SUM_CHANGELOG_SUFFIX, mi.m_szChangeLogSuffix);
		}

		if (SUPPLIER_SIR_REPLICATION == mi.m_nSetupSupplierReplication)
		{
			/* display replication agreement */
			psz += WriteSummaryStringRC(psz, "          %s\r\n", mi.m_hModule, SUM_REPL_AGR_TITLE, NULL);
			psz += WriteSummaryStringRC(psz, "              %s: %s\r\n", mi.m_hModule, SUM_HOST, mi.m_szSupplierHost);
			psz += WriteSummaryIntRC(psz,    "              %s: %d\r\n", mi.m_hModule, SUM_PORT, mi.m_nSupplierPort);
			psz += WriteSummaryStringRC(psz, "              %s: %s\r\n", mi.m_hModule, SUM_REPL_ROOT, mi.m_szSupplierRoot);
			psz += WriteSummaryStringRC(psz, "              %s: %s\r\n", mi.m_hModule, SUM_BIND_AS, mi.m_szSupplierBindAs);
			psz += WriteSummaryStringRC(psz, "              %s: %s\r\n", mi.m_hModule, SUM_REPL_DAYS, mi.m_szSIRDays);
			psz += WriteSummaryStringRC(psz, "              %s: %s\r\n", mi.m_hModule, SUM_REPL_TIMES, mi.m_szSIRTimes);
		}

		if (SUPPLIER_CIR_REPLICATION == mi.m_nSetupSupplierReplication )
		{
			/* if configuring consumer BIND DN display what user entered */
			if (1 == mi.m_nConfigConsumerDN)
			{
				psz += WriteSummaryStringRC(psz, "          %s: %s\r\n", mi.m_hModule, SUM_CONSUMER_BIND_DN, mi.m_szConsumerDN);
			}
		}

		if (CONSUMER_SIR_REPLICATION == mi.m_nSetupConsumerReplication)
		{
			psz += WriteSummaryStringRC(psz, "      %s\r\n", mi.m_hModule, SUM_CONSUMER_REPL_TITLE, NULL);
			/* display supplier bind dn */
			psz += WriteSummaryStringRC(psz, "          %s: %s\r\n", mi.m_hModule, SUM_SUPPLIER_DN, mi.m_szSupplierDN);
		}

		if (CONSUMER_CIR_REPLICATION == mi.m_nSetupConsumerReplication)
		{
			psz += WriteSummaryStringRC(psz, "      %s\r\n", mi.m_hModule, SUM_CONSUMER_REPL_TITLE, NULL);
			/* display replication agreement */
			/* display replication agreement */
			psz += WriteSummaryStringRC(psz, "          %s\r\n", mi.m_hModule, SUM_REPL_AGR_TITLE, NULL);
			psz += WriteSummaryStringRC(psz, "              %s: %s\r\n", mi.m_hModule, SUM_HOST, mi.m_szConsumerHost);
			psz += WriteSummaryIntRC(psz,    "              %s: %d\r\n", mi.m_hModule, SUM_PORT, mi.m_nConsumerPort);
			psz += WriteSummaryStringRC(psz, "              %s: %s\r\n", mi.m_hModule, SUM_REPL_ROOT, mi.m_szConsumerRoot);
			psz += WriteSummaryStringRC(psz, "              %s: %s\r\n", mi.m_hModule, SUM_BIND_AS, mi.m_szConsumerBindAs);
			psz += WriteSummaryStringRC(psz, "              %s: %s\r\n", mi.m_hModule, SUM_REPL_DAYS, mi.m_szCIRDays);
			psz += WriteSummaryStringRC(psz, "              %s: %s\r\n", mi.m_hModule, SUM_REPL_TIMES, mi.m_szCIRTimes);
			psz += WriteSummaryIntRC(psz,    "              %s: %d\r\n", mi.m_hModule, SUM_REPL_SYNC_INTERVAL, mi.m_nCIRInterval);
		}

		if ( CUSTOMMODE == MODE)
		{
			/* display org & ldif files */

			psz += WriteSummaryStringRC(psz, "      %s: %s\r\n", mi.m_hModule, SUM_POP_ORG_STRUCT, onezero2yesno(mi.m_nPopulateSampleOrg)   );
			psz += WriteSummaryStringRC(psz, "      %s: %s\r\n", mi.m_hModule, SUM_POP_DB_FILE, mi.m_szPopLdifFile);
			psz += WriteSummaryStringRC(psz, "      %s: %s\r\n", mi.m_hModule, SUM_DISABLE_SCHEMA_CHECKING, onezero2yesno(mi.m_nDisableSchemaChecking)  );
		}

	}

	*psz = '\0';
}

//////////////////////////////////////////////////////////////////////////////
// DSINST_WriteGlobalCache
//
// This function is called by the installation framework when the user clicks
// Next at the summary screen.  Here you should write all information entered
// by the user into the installation cache for use during silent installation.
// Data written to this section of the file may be interpreted by the
// framework.  If this operation succeeds return TRUE, otherwise display an
// error message and return FALSE to indicate an error.
//

BOOL __declspec(dllexport)
DSINST_WriteGlobalCache(LPCSTR lpszCacheFileName, LPCSTR lpszSectionName)
{

   if(1 == mi.m_nReInstall)
  {

	  /* write configuration directory info, thats the only thing we know about */
	  /* during reinstall */
	  WritePrivateProfileString(lpszSectionName, GLOBAL_INF_LDAP_USER, mi.m_szMCCBindAs, 
		                    lpszCacheFileName);

	  WritePrivateProfileString(lpszSectionName, GLOBAL_INF_LDAP_PASSWD, mi.m_szMCCPw, 
		                    lpszCacheFileName);

	  WritePrivateProfileString(lpszSectionName, SLAPD_KEY_K_LDAP_URL, mi.m_szLdapURL,
	                        lpszCacheFileName);


	  /* where do we get admin domain from on ReInstall ??? is default ok? */
	  WritePrivateProfileString(lpszSectionName, SLAPD_KEY_ADMIN_DOMAIN, mi.m_szAdminDomain,
                                   lpszCacheFileName);


	  /* shut down all slapd servers so no file conflicts*/
	  ControlSlapdServers(TARGETDIR, FALSE, FALSE);

      return TRUE;
  }	

   	/* this is the first thing called after last dialog, setup UG stuff here if creating UG directory */
	/* stevross: is there a better place to put this ? possibly in GetSummary, */
    /* but what happens in silent mode ?*/

	/* construct the LDAPURL */
    /* suffix must always be o=netscape root */
	sprintf(mi.m_szLdapURL, "ldap://%s:%d/%s", mi.m_szMCCHost, mi.m_nMCCPort, NS_DOMAIN_ROOT);

	if(mi.m_nExistingUG == 0)
	{
		/* the user is creating a new UG with this instance */
		
		/* create ou=People/ou=Groups */
//		mi.m_nPopulateSampleOrg = 1;
//		lstrcpy(mi.m_szPopLdifFile, SUGGEST_LDIF);

		if(mi.m_nExistingMCC == 0)
		{
			/* the user is also creating a new MCC so set UG admin to MCC admin */
			lstrcpy(mi.m_szUserGroupAdmin, mi.m_szMCCBindAs);
			lstrcpy(mi.m_szUserGroupAdminPW, mi.m_szMCCPw);

		}else{
			/* user is using an existing MCC so only creating UG, make UG user same as
				Root DN */
			lstrcpy(mi.m_szUserGroupAdmin, mi.m_szInstanceUnrestrictedUser);
			lstrcpy(mi.m_szUserGroupAdminPW, mi.m_szInstancePassword);

		}
		sprintf(mi.m_szUserGroupURL, "ldap://%s:%d/%s", mi.m_szInstanceHostName,
								 mi.m_nInstanceServerPort, mi.m_szInstanceSuffix);	
	}
 
   WritePrivateProfileString(lpszSectionName, GLOBAL_INF_LDAP_USER, mi.m_szMCCBindAs, 
		                    lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, GLOBAL_INF_LDAP_PASSWD, mi.m_szMCCPw, 
		                    lpszCacheFileName);

	WritePrivateProfileString(lpszSectionName, SLAPD_KEY_ADMIN_DOMAIN, mi.m_szAdminDomain,
                                   lpszCacheFileName);

	WritePrivateProfileString(lpszSectionName, SLAPD_KEY_K_LDAP_URL, mi.m_szLdapURL,
                                   lpszCacheFileName);


	WritePrivateProfileString(lpszSectionName, SLAPD_KEY_USER_GROUP_LDAP_URL, mi.m_szUserGroupURL,
                                   lpszCacheFileName);

	WritePrivateProfileString(lpszSectionName, SLAPD_KEY_USER_GROUP_ADMIN_ID, mi.m_szUserGroupAdmin,
                                   lpszCacheFileName);

	WritePrivateProfileString(lpszSectionName, SLAPD_KEY_USER_GROUP_ADMIN_PWD, mi.m_szUserGroupAdminPW,
                                   lpszCacheFileName);
  
  return TRUE;
}

//////////////////////////////////////////////////////////////////////////////
// DSINST_WriteLocalCache
//
// This function is called by the installation framework when the user clicks
// Next at the summary screen.  Here you should write all information entered
// by the user into the installation cache for use during silent installation.
// Data written to this file is not interpreted by the framework, and may
// consist of any values that you will need to perform the installation (not
// just values entered by the user). If this operation succeeds return TRUE,
// otherwise display an error message and return FALSE to indicate an error.
//

BOOL __declspec(dllexport)
DSINST_WriteLocalCache(LPCSTR lpszCacheFileName, LPCSTR lpszSectionName)
{
  // TODO: Add code to write data to the cache file (INI format) under the
  // specified section name.

   CHAR szInt[BUFSIZ];

	/* don't want to over write with bogus default values on ReInstall*/
	  if(1 == mi.m_nReInstall)
	  {
		  /* just write snmp status cause thats the only thing we really know*/
		  /* will allow to control it on reinstall also */
		   WritePrivateProfileString(lpszSectionName, LOCAL_INF_SNMP_ON, onezero2yesno(mi.m_nSNMPOn), 
				                    lpszCacheFileName);   

        return TRUE;
	  }	

   
   /* general settings */
   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_USE_EXISTING_MC, onezero2yesno(mi.m_nExistingMCC), 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_USE_EXISTING_UG, onezero2yesno(mi.m_nExistingUG), 
	                        lpszCacheFileName);

   sprintf(szInt, "%d",  mi.m_nInstanceServerPort);
   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_SERVER_PORT, szInt, 
	                        lpszCacheFileName);
     
   if(!mi.m_nExistingUG)
   {
		/* don't write this key when config only directory */
	    /* config only directory when using existing data store */
		WritePrivateProfileString(lpszSectionName, SLAPD_KEY_SUFFIX, mi.m_szInstanceSuffix, 
				                    lpszCacheFileName);
   }

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_ROOTDN, mi.m_szInstanceUnrestrictedUser, 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_ROOTDNPWD, mi.m_szInstancePassword, 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_SERVER_IDENTIFIER, mi.m_szServerIdentifier, 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_SLAPD_CONFIG_FOR_MC, onezero2yesno(mi.m_nCfgSspt), 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_ADD_SAMPLE_ENTRIES, 
								onezero2yesno(mi.m_nPopulateSampleEntries), 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_ADD_ORG_ENTRIES, onezero2yesno(mi.m_nPopulateSampleOrg), 
	                        lpszCacheFileName);

   sprintf(szInt, "%s",  onezero2yesno( ( (NO_REPLICATION != mi.m_nSetupConsumerReplication) || (NO_REPLICATION != mi.m_nSetupSupplierReplication) ) ) );
   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_USE_REPLICATION, szInt, 
	                        lpszCacheFileName);
   
   /* consumer replication settings */

   /* write no instead of number for no replication to be like unix installer */
   if(NO_REPLICATION != mi.m_nSetupConsumerReplication)
   {
		sprintf(szInt, "%d", mi.m_nSetupConsumerReplication);
   }else{
	    sprintf(szInt, "no");
   }

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_SETUP_CONSUMER, szInt, 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_CIR_HOST, mi.m_szConsumerHost, 
	                        lpszCacheFileName);

   sprintf(szInt, "%d", mi.m_nConsumerPort );
   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_CIR_PORT, szInt, 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_CIR_SUFFIX, mi.m_szConsumerRoot, 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_CIR_BINDDN, mi.m_szConsumerBindAs, 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_CIR_BINDDNPWD, mi.m_szConsumerPw, 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_CIR_SECURITY_ON, onezero2yesno(mi.m_nConsumerSSL),
	                        lpszCacheFileName);
   
   sprintf(szInt, "%d", mi.m_nCIRInterval );
   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_CIR_INTERVAL, szInt, 
	                        lpszCacheFileName);

   if(!strcmp(DEFAULT_CIR_DAYS, mi.m_szCIRDays) )
   {
	   /* if default of all days write null to inf file as that is what cgi wants */
	   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_CIR_DAYS, "\0", 
		                        lpszCacheFileName);
   }else{
	   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_CIR_DAYS, mi.m_szCIRDays, 
		                        lpszCacheFileName);
   }

   if(!strcmp(DEFAULT_CIR_TIMES, mi.m_szCIRTimes) )
   {
   	   /* if default of all times write null to inf file as that is what cgi wants */
	  WritePrivateProfileString(lpszSectionName, SLAPD_KEY_CIR_TIMES, "\0", 
	                        lpszCacheFileName);
   }else{
	  WritePrivateProfileString(lpszSectionName, SLAPD_KEY_CIR_TIMES, mi.m_szCIRTimes, 
	                        lpszCacheFileName);
   }

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_REPLICATIONDN, mi.m_szSupplierDN, 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_REPLICATIONPWD, mi.m_szSupplierPW, 
	                        lpszCacheFileName);

   /* Supplier replication settings */
 
   /* write no instead of number for no replication to be like unix installer */
   if(NO_REPLICATION != mi.m_nSetupSupplierReplication)
   {
		sprintf(szInt, "%d", mi.m_nSetupSupplierReplication);
   }else{
	    sprintf(szInt, "no");
   }
   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_SETUP_SUPPLIER, szInt, 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_CHANGELOGDIR, mi.m_szChangeLogDbDir, 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_CHANGELOGSUFFIX, mi.m_szChangeLogSuffix, 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_SIR_HOST, mi.m_szSupplierHost, 
	                        lpszCacheFileName);

   sprintf(szInt, "%d", mi.m_nSupplierPort );
   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_SIR_PORT, szInt, 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_SIR_SUFFIX, mi.m_szSupplierRoot, 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_SIR_BINDDN, mi.m_szSupplierBindAs, 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_SIR_BINDDNPWD, mi.m_szSupplierPw, 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_SIR_SECURITY_ON, onezero2yesno( mi.m_nSupplierSSL), 
	                        lpszCacheFileName);
 
   if(!strcmp(DEFAULT_SIR_DAYS, mi.m_szSIRDays) )
   {
	   /* if default of all days write null to inf file as that is what cgi wants */
	   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_SIR_DAYS, "\0", 
		                        lpszCacheFileName);
   }else{
	   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_SIR_DAYS, mi.m_szSIRDays, 
		                        lpszCacheFileName);
   }

   if(!strcmp(DEFAULT_SIR_TIMES, mi.m_szSIRTimes) )
   {
   	   /* if default of all times write null to inf file as that is what cgi wants */
	  WritePrivateProfileString(lpszSectionName, SLAPD_KEY_SIR_TIMES, "\0", 
	                        lpszCacheFileName);
   }else{
	  WritePrivateProfileString(lpszSectionName, SLAPD_KEY_SIR_TIMES, mi.m_szSIRTimes, 
	                        lpszCacheFileName);
   }

   WritePrivateProfileString(lpszSectionName, LOCAL_INF_CONFIG_CONSUMER_DN, onezero2yesno(mi.m_nConfigConsumerDN), 
	                        lpszCacheFileName);
   
   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_CONSUMERDN, mi.m_szConsumerDN, 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_CONSUMERPWD, mi.m_szConsumerPW, 
	                        lpszCacheFileName);


   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_INSTALL_LDIF_FILE, mi.m_szPopLdifFile, 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, SLAPD_KEY_DISABLE_SCHEMA_CHECKING, onezero2yesno(mi.m_nDisableSchemaChecking), 
	                        lpszCacheFileName);

   WritePrivateProfileString(lpszSectionName, LOCAL_INF_SNMP_ON, onezero2yesno(mi.m_nSNMPOn), 
	                        lpszCacheFileName);   

  return TRUE;
}

//////////////////////////////////////////////////////////////////////////////
// DSINST_ReadGlobalCache
//
// This function is called by the installation framework during silent install
// to initialize your data from the cache file you created above. Here you
// should read any information stored in the installation cache's global
// section that you need. If this operation succeeds return TRUE, otherwise
// display an error message and return FALSE to indicate an error.
//

BOOL __declspec(dllexport)
DSINST_ReadGlobalCache(LPCSTR lpszCacheFileName, LPCSTR lpszSectionName)
{
  // TODO: Add code to read data from the cache file (INI format) under the
  // specified section name.

	/* stevross: this may be null when reading cache for post install or something, so
		make sure to allocate it again */
	CHAR szFormat[MAX_STR_SIZE];

	if(1 == mi.m_nReInstall)
	{
		/* just read config directory stuff */
		/* this is the only stuff we are need and are guaranteed to have */
	LoadString( mi.m_hModule, ERR_READ_GLOBAL_CACHE, szFormat, MAX_STR_SIZE);
					
    GetPrivateProfileString(lpszSectionName, SLAPD_KEY_FULL_MACHINE_NAME, "\0", 
		                     mi.m_szInstanceHostName, MAX_STR_SIZE, 
							 lpszCacheFileName);

	if (mi.m_szInstanceHostName[0] == 0)
		DSGetHostName(mi.m_szInstanceHostName, MAX_STR_SIZE);

	if(NULL == mi.m_szMCCBindAs )
	{
		mi.m_szMCCBindAs = malloc(MAX_STR_SIZE);
	}

    GetPrivateProfileString(lpszSectionName, GLOBAL_INF_LDAP_USER, "\0", 
		                     mi.m_szMCCBindAs, MAX_STR_SIZE, 
							 lpszCacheFileName);

    if ( 0 == lstrcmp(mi.m_szMCCBindAs, "\0") )
	{
		DSMessageBoxOK(ERR_NO_SS_ADMIN_TITLE, ERR_NO_SS_ADMIN, 0);
	    return FALSE;
	}else{

		/* stevross: now that cgi can handle full DN 
			Sspt UID is same user as MCC BindAs no matter what
			look into removing later once get instance creatin working */
			lstrcpy(mi.m_szSsptUid, mi.m_szMCCBindAs);
	}

    GetPrivateProfileString(lpszSectionName, GLOBAL_INF_LDAP_PASSWD, "\0", 
		                     mi.m_szMCCPw, MAX_STR_SIZE, 
							 lpszCacheFileName);

    if ( 0 == lstrcmp(mi.m_szMCCPw, "\0") )
	{
		DSMessageBoxOK(ERR_NO_PW_TITLE, ERR_NO_PW, 0);
	    return FALSE;
	}else{
		/* use password for sspt user since this is the ssptuser */
		lstrcpy(mi.m_szSsptUidPw, mi.m_szMCCPw);
	    lstrcpy(mi.m_szSsptUidPwAgain, mi.m_szSsptUidPw);
	}

    GetPrivateProfileString(lpszSectionName, SLAPD_KEY_ADMIN_DOMAIN, "\0", 
		                     mi.m_szAdminDomain, MAX_STR_SIZE, 
							 lpszCacheFileName);

    GetPrivateProfileString(lpszSectionName, SLAPD_KEY_K_LDAP_URL, "\0", 
		                     mi.m_szLdapURL, MAX_STR_SIZE, 
							 lpszCacheFileName);

	if( GetURLComponents(mi.m_szLdapURL, mi.m_szMCCHost, 
			&mi.m_nMCCPort, mi.m_szMCCSuffix) != 0)
	{
		DSMessageBoxOK(ERR_NO_CONFIG_URL_TITLE, ERR_NO_CONFIG_URL, 0);
		return FALSE;
	}


        return TRUE;
	}	


	LoadString( mi.m_hModule, ERR_READ_GLOBAL_CACHE, szFormat, MAX_STR_SIZE);
					
    GetPrivateProfileString(lpszSectionName, SLAPD_KEY_FULL_MACHINE_NAME, "\0", 
		                     mi.m_szInstanceHostName, MAX_STR_SIZE, 
							 lpszCacheFileName);

	if (mi.m_szInstanceHostName[0] == 0)
		DSGetHostName(mi.m_szInstanceHostName, MAX_STR_SIZE);

	if(NULL == mi.m_szMCCBindAs )
	{
		mi.m_szMCCBindAs = malloc(MAX_STR_SIZE);
	}

    GetPrivateProfileString(lpszSectionName, GLOBAL_INF_LDAP_USER, "\0", 
		                     mi.m_szMCCBindAs, MAX_STR_SIZE, 
							 lpszCacheFileName);

    if ( 0 == lstrcmp(mi.m_szMCCBindAs, "\0") )
	{
		DSMessageBoxOK(ERR_NO_SS_ADMIN_TITLE, ERR_NO_SS_ADMIN, 0);
	    return FALSE;
	}else{

		/* stevross: now that cgi can handle full DN 
			Sspt UID is same user as MCC BindAs no matter what
			look into removing later once get instance creatin working */
			lstrcpy(mi.m_szSsptUid, mi.m_szMCCBindAs);
	}

    GetPrivateProfileString(lpszSectionName, GLOBAL_INF_LDAP_PASSWD, "\0", 
		                     mi.m_szMCCPw, MAX_STR_SIZE, 
							 lpszCacheFileName);

    if ( 0 == lstrcmp(mi.m_szMCCPw, "\0") )
	{
		DSMessageBoxOK(ERR_NO_PW_TITLE, ERR_NO_PW, 0);
	    return FALSE;
	}else{
		/* use password for sspt user since this is the ssptuser */
		lstrcpy(mi.m_szSsptUidPw, mi.m_szMCCPw);
	    lstrcpy(mi.m_szSsptUidPwAgain, mi.m_szSsptUidPw);
	}

    GetPrivateProfileString(lpszSectionName, SLAPD_KEY_ADMIN_DOMAIN, "\0", 
		                     mi.m_szAdminDomain, MAX_STR_SIZE, 
							 lpszCacheFileName);

    GetPrivateProfileString(lpszSectionName, SLAPD_KEY_K_LDAP_URL, "\0", 
		                     mi.m_szLdapURL, MAX_STR_SIZE, 
							 lpszCacheFileName);

	if( GetURLComponents(mi.m_szLdapURL, mi.m_szMCCHost, 
			&mi.m_nMCCPort, mi.m_szMCCSuffix) != 0)
	{
		DSMessageBoxOK(ERR_NO_CONFIG_URL_TITLE, ERR_NO_CONFIG_URL, 0);
		return FALSE;
	}

	GetPrivateProfileString(lpszSectionName, SLAPD_KEY_USER_GROUP_LDAP_URL, "\0", 
		                     mi.m_szUserGroupURL, MAX_STR_SIZE, 
							 lpszCacheFileName);

	if( GetURLComponents(mi.m_szUserGroupURL, mi.m_szUGHost, 
		&mi.m_nUGPort, mi.m_szUGSuffix) != 0)
	{
		DSMessageBoxOK(ERR_NO_USER_URL_TITLE, ERR_NO_USER_URL, 0);
		return FALSE;
	}

	GetPrivateProfileString(lpszSectionName, SLAPD_KEY_USER_GROUP_ADMIN_ID, "\0", 
		                      mi.m_szUserGroupAdmin, MAX_STR_SIZE, 
							 lpszCacheFileName);

	GetPrivateProfileString(lpszSectionName, SLAPD_KEY_USER_GROUP_ADMIN_PWD, "\0", 
		                      mi.m_szUserGroupAdminPW, MAX_STR_SIZE, 
							 lpszCacheFileName);

  return TRUE;
}

//////////////////////////////////////////////////////////////////////////////
// DSINST_ReadLocalCache
//
// This function is called by the installation framework during silent install
// to intialize your data from the local section of the cache created above.
// Here you should read any information stored in the installation cache's
// local section that you need.  If this operation succeeds return TRUE,
// otherwise display an error message and return FALSE to indicate an error.
//

BOOL __declspec(dllexport)
DSINST_ReadLocalCache(LPCSTR lpszCacheFileName, LPCSTR lpszSectionName)
{
  // TODO: Add code to read data from the cache file (INI format) under the
  // specified section name.
	char	szTemp[BUFSIZ];
	CHAR szFormat[MAX_STR_SIZE];

	/* only ting we know on this section during reinstall is SNMP value */
	if(1 == mi.m_nReInstall)
	{
		
	   GetPrivateProfileString(lpszSectionName, LOCAL_INF_SNMP_ON, "\0", 
		                     szTemp, BUFSIZ, 
							 lpszCacheFileName);

		mi.m_nSNMPOn  = yesno2onezero(szTemp);

	    return TRUE;
	}	


	LoadString( mi.m_hModule, ERR_READ_LOCAL_CACHE, szFormat, MAX_STR_SIZE);

	GetPrivateProfileString(lpszSectionName, SLAPD_KEY_USE_EXISTING_MC, "\0", 
		                     szTemp, BUFSIZ, 
							 lpszCacheFileName);

	mi.m_nExistingMCC = yesno2onezero(szTemp);

	GetPrivateProfileString(lpszSectionName, SLAPD_KEY_USE_EXISTING_UG, "\0", 
		                     szTemp, BUFSIZ, 
							 lpszCacheFileName);

	mi.m_nExistingUG = yesno2onezero(szTemp);

    mi.m_nInstanceServerPort = GetPrivateProfileInt(lpszSectionName, 
		SLAPD_KEY_SERVER_PORT, DEFAULT_SERVER_PORT, lpszCacheFileName);

    GetPrivateProfileString(lpszSectionName, SLAPD_KEY_SUFFIX, "\0", 
		                     mi.m_szInstanceSuffix, MAX_STR_SIZE, 
							 lpszCacheFileName);
 
	GetPrivateProfileString(lpszSectionName, SLAPD_KEY_ROOTDN, "\0", 
		                     mi.m_szInstanceUnrestrictedUser, MAX_STR_SIZE, 
							 lpszCacheFileName);
   
    GetPrivateProfileString(lpszSectionName, SLAPD_KEY_ROOTDNPWD, "\0", 
		                     mi.m_szInstancePassword, MAX_STR_SIZE, 
							 lpszCacheFileName);

    lstrcpy(mi.m_szInstancePasswordAgain, mi.m_szInstancePassword);


    GetPrivateProfileString(lpszSectionName, SLAPD_KEY_SERVER_IDENTIFIER, "\0", 
		                     mi.m_szServerIdentifier, MAX_STR_SIZE, 
							 lpszCacheFileName);

     GetPrivateProfileString(lpszSectionName, SLAPD_KEY_SLAPD_CONFIG_FOR_MC, "\0", 
		                     szTemp, BUFSIZ, 
							 lpszCacheFileName);

	mi.m_nCfgSspt = yesno2onezero(szTemp);

    /*stevross: should I add more error checking below? These are only required in certain cases*/

    GetPrivateProfileString(lpszSectionName, SLAPD_KEY_ADD_SAMPLE_ENTRIES, "\0", 
		                     szTemp, BUFSIZ, 
							 lpszCacheFileName);

	mi.m_nPopulateSampleEntries = yesno2onezero(szTemp);


    GetPrivateProfileString(lpszSectionName, SLAPD_KEY_ADD_ORG_ENTRIES, "\0", 
		                     szTemp, BUFSIZ, 
							 lpszCacheFileName);

	mi.m_nPopulateSampleOrg = yesno2onezero(szTemp);


   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_SETUP_CONSUMER, "\0", 
		                     szTemp, BUFSIZ, 
							 lpszCacheFileName);

   /* value should be no, 1 or 2 to be like unix installer*/
   if(!strcmpi(szTemp, "no") )
   {
		mi.m_nSetupConsumerReplication = NO_REPLICATION;
   }else{
	    mi.m_nSetupConsumerReplication = GetPrivateProfileInt(lpszSectionName, SLAPD_KEY_SETUP_CONSUMER, 
	                                                      DEFAULT_SETUP_CONSUMER, lpszCacheFileName);
   }

   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_CIR_HOST, "\0", 
                           mi.m_szConsumerHost, MAX_STR_SIZE, 
	    	 			   lpszCacheFileName);

   mi.m_nConsumerPort = GetPrivateProfileInt(lpszSectionName, SLAPD_KEY_CIR_PORT, 
                                                        DEFAULT_CIR_PORT, lpszCacheFileName);

   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_CIR_SUFFIX, "\0", 
                           mi.m_szConsumerRoot, MAX_STR_SIZE, 
	    	 			   lpszCacheFileName);
  
   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_CIR_BINDDN, "\0", 
                           mi.m_szConsumerBindAs, MAX_STR_SIZE, 
	    	 			   lpszCacheFileName);

 
   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_CIR_BINDDNPWD, "\0", 
                           mi.m_szConsumerPw, MAX_STR_SIZE, 
	    	 			   lpszCacheFileName);
   
   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_CIR_SECURITY_ON, "\0", 
		                     szTemp, BUFSIZ, 
							 lpszCacheFileName);

   mi.m_nConsumerSSL = yesno2onezero(szTemp);


   mi.m_nCIRInterval = GetPrivateProfileInt(lpszSectionName, SLAPD_KEY_CIR_INTERVAL, 
														DEFAULT_CIR_INTERVAL, lpszCacheFileName);

   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_CIR_DAYS, DEFAULT_CIR_DAYS, 
                           mi.m_szCIRDays, N_DAYS, 
	    	 			   lpszCacheFileName);

   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_CIR_TIMES, DEFAULT_CIR_TIMES, 
                           mi.m_szCIRTimes, N_TIMES, 
	    	 			   lpszCacheFileName);
  
  
   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_REPLICATIONDN, "\0", 
                           mi.m_szSupplierDN, MAX_STR_SIZE, 
	    	 			   lpszCacheFileName);
 
   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_REPLICATIONPWD, "\0", 
                           mi.m_szSupplierPW, MAX_STR_SIZE, 
	    	 			   lpszCacheFileName);

   /* read from cache, so copy it to mi.m_szSupplierPWAgain); */
   lstrcpy(mi.m_szSupplierPWAgain, mi.m_szSupplierPW);
   
   /* Supplier replication settings */

   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_SETUP_SUPPLIER, "\0", 
		                     szTemp, BUFSIZ, 
							 lpszCacheFileName);

   /* value should be no, 1 or 2 to be like unix installer*/
   if(!strcmpi(szTemp, "no"))
   {
		mi.m_nSetupSupplierReplication = NO_REPLICATION;
   }else{
	    mi.m_nSetupSupplierReplication = GetPrivateProfileInt(lpszSectionName, SLAPD_KEY_SETUP_SUPPLIER, 
	                                                      DEFAULT_SETUP_CONSUMER, lpszCacheFileName);
   }

   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_CHANGELOGDIR, "\0", 
                           mi.m_szChangeLogDbDir, MAX_STR_SIZE, 
	    	 			   lpszCacheFileName);
  
   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_CHANGELOGSUFFIX, "\0", 
                           mi.m_szChangeLogSuffix, MAX_STR_SIZE, 
	    	 			   lpszCacheFileName);


   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_SIR_HOST, "\0", 
                           mi.m_szSupplierHost, MAX_STR_SIZE, 
	    	 			   lpszCacheFileName);

   mi.m_nSupplierPort = GetPrivateProfileInt(lpszSectionName, SLAPD_KEY_SIR_PORT, 
                                                        DEFAULT_SIR_PORT, lpszCacheFileName);

   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_SIR_SUFFIX, "\0", 
                           mi.m_szSupplierRoot, MAX_STR_SIZE, 
	    	 			   lpszCacheFileName);
  
   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_SIR_BINDDN, "\0", 
                           mi.m_szSupplierBindAs, MAX_STR_SIZE, 
	    	 			   lpszCacheFileName);

 
   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_SIR_BINDDNPWD, "\0", 
                           mi.m_szSupplierPw, MAX_STR_SIZE, 
	    	 			   lpszCacheFileName);
   
   
   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_SIR_SECURITY_ON, "\0", 
		                     szTemp, BUFSIZ, 
							 lpszCacheFileName);

   mi.m_nSupplierSSL  = yesno2onezero(szTemp);

   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_SIR_DAYS,DEFAULT_SIR_DAYS, 
                           mi.m_szSIRDays, N_DAYS, 
	    	 			   lpszCacheFileName);

   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_SIR_TIMES, DEFAULT_SIR_TIMES, 
                           mi.m_szSIRTimes, N_TIMES, 
	    	 			   lpszCacheFileName);   
  
   GetPrivateProfileString(lpszSectionName, LOCAL_INF_CONFIG_CONSUMER_DN, "\0", 
		                     szTemp, BUFSIZ, 
							 lpszCacheFileName);

   mi.m_nConfigConsumerDN  = yesno2onezero(szTemp);

   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_CONSUMERDN, "\0", 
                           mi.m_szConsumerDN, MAX_STR_SIZE, 
	    	 			   lpszCacheFileName);
 
   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_CONSUMERPWD, "\0", 
                           mi.m_szConsumerPW, MAX_STR_SIZE, 
	    	 			   lpszCacheFileName);

   lstrcpy(mi.m_szConsumerPWAgain, mi.m_szConsumerPW);
   
   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_INSTALL_LDIF_FILE, DEFAULT_INF_POP_LDIF_FILE, 
                           mi.m_szPopLdifFile, MAX_STR_SIZE, 
	    	 			   lpszCacheFileName);
   
   
   GetPrivateProfileString(lpszSectionName, SLAPD_KEY_DISABLE_SCHEMA_CHECKING, "\0", 
		                     szTemp, BUFSIZ, 
							 lpszCacheFileName);

   mi.m_nDisableSchemaChecking  = yesno2onezero(szTemp);



   GetPrivateProfileString(lpszSectionName, LOCAL_INF_SNMP_ON, "\0", 
		                     szTemp, BUFSIZ, 
							 lpszCacheFileName);

	mi.m_nSNMPOn  = yesno2onezero(szTemp);

	if ( SILENTMODE == MODE)
	{
		/* verify settings here as they didn't get verified in dialog */
		/* stop install if find something bad */

		/* things that always need to be checked */
		if (    Verify_Server_Settings() )
		{
			/* error in server settings */
			return FALSE;
		}

		if ( Verify_ROOTDN() )
		{
			return FALSE;
		}

#ifdef CUSTOM_REPL_FOR_4X
		if (SUPPLIER_SIR_REPLICATION == mi.m_nSetupSupplierReplication)
		{
			/* always need to check changelogdb and suffix */
			if ( Verify_Supplier_Replication() )
			{
				return FALSE;

			}
			
			/* check replication agreement */
			if (  VerifyReplAgreement(mi.m_szSupplierHost, 
									  &mi.m_nSupplierPort,
									  mi.m_szSupplierRoot,
									  mi.m_szSupplierBindAs,
									  mi.m_szSupplierPw,
									  mi.m_szSIRDays,
									  mi.m_szSIRTimes) )
			{
				return FALSE;
			}


		}

		if (SUPPLIER_CIR_REPLICATION == mi.m_nSetupSupplierReplication)
		{
			/* always need to check changelogdb and suffix */
			if ( Verify_Supplier_Replication() )
			{
				return FALSE;


			}

			if ( Verify_Consumer_DN() )
			{
				return FALSE;
			}
		}

		if (CONSUMER_SIR_REPLICATION == mi.m_nSetupConsumerReplication)
		{
			if ( Verify_Consumer_Replication() )
			{
				return FALSE;
			}
		}

		if (CONSUMER_CIR_REPLICATION == mi.m_nSetupConsumerReplication)
		{
			if ( VerifyReplAgreement(mi.m_szConsumerHost, 
									 &mi.m_nConsumerPort,
									 mi.m_szConsumerRoot,
									 mi.m_szConsumerBindAs,
									 mi.m_szConsumerPw,
									 mi.m_szCIRDays,
									 mi.m_szCIRTimes) )
			{
				return FALSE;
			}

		}
#endif
	}

	/* set ldap settings for silent install mode */
        if(1 != mi.m_nReInstall)
	{
             set_ldap_settings();
        }

	return TRUE;
}


//////////////////////////////////////////////////////////////////////////////
// DSINST_ReadComponentInf
//
// 

BOOL __declspec(dllexport) DSINST_ReadComponentInf(LPCSTR pszCacheFile, LPCSTR pszSection) 
{ 

  char szValue[MAX_PATH]; 

  myLogData("In DSINST_ReadComponentInf: file [%s] section [%s]",
			pszCacheFile, pszSection);
  GetPrivateProfileString(pszSection, SETUP_INF_COM_VENDOR, "", szValue, sizeof(szValue), pszCacheFile); 
  cd.szVendor = _strdup(szValue); 
  GetPrivateProfileString(pszSection, SETUP_INF_COM_DESC, "", szValue, sizeof(szValue), pszCacheFile); 
  cd.szDescription = _strdup(szValue); 
  GetPrivateProfileString(pszSection, SETUP_INF_COM_NAME, "", szValue, sizeof(szValue), pszCacheFile); 
  cd.szName = _strdup(szValue); 
  GetPrivateProfileString(pszSection, SETUP_INF_COM_NICKNAME, "", szValue, sizeof(szValue), pszCacheFile); 
  cd.szNickname = _strdup(szValue); 
  GetPrivateProfileString(pszSection, SETUP_INF_COM_VERSION, "", szValue, sizeof(szValue), pszCacheFile); 
  cd.szVersion = _strdup(szValue); 
  GetPrivateProfileString(pszSection, SETUP_INF_COM_BUILDNUMBER, "", szValue, sizeof(szValue), pszCacheFile); 
  cd.szBuildNumber = _strdup(szValue); 
  GetPrivateProfileString(pszSection, SETUP_INF_COM_REVISION, "", szValue, sizeof(szValue), pszCacheFile); 
  cd.szRevision = _strdup(szValue); 
  GetPrivateProfileString(pszSection, SETUP_INF_COM_EXPIRY, "", szValue, sizeof(szValue), pszCacheFile); 
  cd.szExpireDate = _strdup(szValue); 
  GetPrivateProfileString(pszSection, SETUP_INF_COM_SECURITY, "", szValue, sizeof(szValue), pszCacheFile); 
  cd.szSecurity = _strdup(szValue); 
  cd.szTimeStamp = _strdup(getGMT()); 

  myLogData("In DSINST_ReadComponentInf: name=%s nick=%s version=%s build=%s "
			"rev=%s time=%s",
			cd.szName, cd.szNickname, cd.szVersion, cd.szBuildNumber, cd.szRevision,
			cd.szTimeStamp);
  return TRUE; 
} 

//////////////////////////////////////////////////////////////////////////////
// run_cgi
//
// 
// runs a cgi
// 
// 
// 
// 
//

static int
run_cgi(const char *serverroot, const char *cgipath, const char *args)
{
	int status = 0;
    DWORD procResult;
	DWORD dwLastError = 0;
    char prog[MAX_STR_SIZE] = {0};
	char cmdLine[MAX_STR_SIZE] = {0};
	char netsiteRootEnvVar[MAX_STR_SIZE] = {0};
    LPVOID lpMsgBuf;

    sprintf(netsiteRootEnvVar, "NETSITE_ROOT=%s", serverroot);
    _putenv(netsiteRootEnvVar);
    if ( getenv("DEBUG_DSINST") )
		DebugBreak();
    /* everything is set, start the program */
	sprintf(prog, "%s\\%s", serverroot, cgipath);
	if (!FileExists(prog))
	{
		lpMsgBuf = getLastErrorMessage();

		DSMessageBoxOK(ERR_NO_FIND_INST_PROG_TITLE,
					   ERR_NO_FIND_INST_PROG, 0, prog, lpMsgBuf, LOGFILE);


        status = -1;
		myLogData("Error: could not find program %s: %d (%s)", prog, GetLastError(), lpMsgBuf);
        LocalFree( lpMsgBuf );
	}
	else
	{
		sprintf(cmdLine, "\"%s\" %s", prog, args);
        
		myLogData("run_cgi: before execution of %s", cmdLine);
		if ( (procResult = _LaunchAndWait(cmdLine, INFINITE)) != 0)
		{
			dwLastError = GetLastError();
			lpMsgBuf = getLastErrorMessage();

			myLogData("Error: could not run %s: %d (%s)", cmdLine, dwLastError, lpMsgBuf);
			if(0 == dwLastError)
			{
				DSMessageBoxOK(ERR_EXEC_INST_PROG_TITLE, ERR_UNK_INST_CREATE, 0,
							   prog, LOGFILE);
			}else {
				DSMessageBoxOK(ERR_EXEC_INST_PROG_TITLE, ERR_EXEC_INST_PROG, 0,
							   prog, lpMsgBuf, LOGFILE);
			}
            
            LocalFree( lpMsgBuf );
            status = -1;
		}
		myLogData("run_cgi: after execution of %s", cmdLine);
	}

	return status;
}

//////////////////////////////////////////////////////////////////////////////
// create_slapd_instance
//
// 
// creates a instance of slapd 
// 
// 
// 
// 
//


static int
create_slapd_instance(const char *hostname, const char *serverroot)
{
    int status = 0;
    char INFfile[MAX_STR_SIZE] = {0};
    char debugFile[MAX_STR_SIZE] = {0};
    struct _stat statbuf;	
    static char contentLength[100] = {0};	
    static char admservRoot[MAX_STR_SIZE] = {0};
    static char serverUrl[MAX_STR_SIZE] = {0};
    static char scriptName[MAX_STR_SIZE] = {0};
	char szCGIArgs[MAX_STR_SIZE]= {0};
    LPVOID lpMsgBuf;

	/* create an .inf file to pass to index */
	/* write the data to a temp file */
	sprintf(INFfile, "%s\\temp%d.inf", TEMPDIR, _getpid());
	myLogData("create_slapd_instance: inf file is %s", INFfile);

	if (TRUE == (status = writeINFfile(INFfile)) )
	{
		if (status = _stat(INFfile, &statbuf))
		{
			lpMsgBuf = getLastErrorMessage();
			DSMessageBoxOK(ERR_NO_STAT_TMP_FILE_TITLE,
						   ERR_NO_STAT_TMP_FILE, 0, INFfile, lpMsgBuf);

            LocalFree(lpMsgBuf);
		}
		else
		{
            /* set temp file for admin output */
            sprintf(debugFile, "DEBUG_FILE=%s\\debug.%d", TEMPDIR, _getpid());
            _putenv(debugFile);
			sprintf(szCGIArgs, "\"%s\\bin\\slapd\\admin\\bin\\Install.pl\"",
					serverroot);
		    if (mi.m_nReInstall) 
			{ 
				strcat(szCGIArgs, " -r -f ");
				/* add the -r flag if reinstalling */ 
			} else 
			{ 
				strcat(szCGIArgs, " -f ");
			}
			strcat(szCGIArgs, "\"");
			strcat(szCGIArgs, INFfile);
			strcat(szCGIArgs, "\"");
			myLogData("create_slapd_instance: executing %s %s",
				  PERL_EXE, szCGIArgs);
			status = run_cgi(serverroot, PERL_EXE, szCGIArgs); 
		}		
	}

	if (!getenv("USE_LOGFILE"))
	  _unlink(INFfile); 

	return status;
}


//////////////////////////////////////////////////////////////////////////////
// 
// generate_mcc_bat
// 
// make bat file with correct classpath, and host/port to start console
// 
//

int generate_mcc_bat()
{

    FILE *fp;
    CHAR szFilename[MAX_STR_SIZE];
    CHAR szJavaDir[MAX_STR_SIZE];
    INT rc = 0;

    // don't generate the file unless asked
    if (!getenv("GENERATE_MCC_BAT")) {
	return rc;
    }

    sprintf(szFilename, "%s\\%s-%s\\mcc.bat", TARGETDIR, DS_ID_SERVICE,
			mi.m_szServerIdentifier);
    fp = fopen(szFilename, "wb");
    if (!fp)
    {
		DSMessageBoxOK(ERR_NO_CREATE_FILE_TITLE,
					   ERR_NO_CREATE_FILE, 0, szFilename);
    	rc = -1;
    }else{
        sprintf(szJavaDir, "%s\\java", TARGETDIR);
                              
        fprintf(fp, "pushd \"%s\"\n", szJavaDir);

        /* use jre */
        fprintf(fp, "%s\\jre\\bin\\jre.exe -cp ", TARGETDIR);
        
        /* classes for classpath -cp option on jre */
        fprintf(fp, "%s\\ds50.jar;", szJavaDir);
        fprintf(fp, "%s\\ds50_en.jar;", szJavaDir);
	fprintf(fp, "%s\\admserv45.jar;", szJavaDir);
	fprintf(fp, "%s\\admserv45_en.jar;", szJavaDir);
        fprintf(fp, "%s\\mcc45.jar;", szJavaDir);
        fprintf(fp, "%s\\mcc45_en.jar;", szJavaDir);
        fprintf(fp, "%s\\ldapjdk.jar;", szJavaDir);
        fprintf(fp, "%s\\nmclf45.jar;", szJavaDir);
        fprintf(fp, "%s\\nmclf45_en.jar;", szJavaDir);
        fprintf(fp, "%s\\ssl.zip;", szJavaDir);
        fprintf(fp, "%s\\base.jar;", szJavaDir);
        fprintf(fp, "%s ", szJavaDir);
       
        /* command and arguments to execute the console for this server */
        fprintf(fp, "com.netscape.management.client.console.Console -d %s -p %d -b \"%s\"\n",
                     mi.m_szMCCHost, mi.m_nMCCPort, mi.m_szMCCSuffix);
        fprintf(fp, "popd\n");

        fclose(fp);

        rc = 0;
    }
 
 return rc;

}


//
//  Generates bat file to install ldap ctrs, since have to be in the same directory to get .h file
//


int generate_install_ldapctrs_bat()
{

    FILE *fp;
    CHAR szFilename[MAX_STR_SIZE];
    INT rc = 0;

    sprintf(szFilename, "%s\\%s", TARGETDIR, INSTALL_CTRS_BAT);
    fp = fopen(szFilename, "wb");
    if (!fp)
    {
		DSMessageBoxOK(ERR_NO_CREATE_FILE_TITLE,
					   ERR_NO_CREATE_FILE, 0, szFilename);
    	rc = -1;
    }else{
        fprintf(fp, "copy  %s\\%s\\nsldapctr*.* %s\n", 
                    TARGETDIR, BIN_SLAPD_INSTALL_BIN, WINSYSDIR);

        fprintf(fp, "%s\\lodctr nsldapctrs.ini\n", WINSYSDIR);

        fprintf(fp, "del %s\\nsldapctr*.*\n", WINSYSDIR);

        fclose(fp);
         rc = 0;
    }
 
 return rc;

}


//--------------------------------------------------------------------------//
// Install perfmon                                                          //
//     Creates Registry keys and loads counters for permon etc              //
//--------------------------------------------------------------------------//
BOOL _InstallPerfmon(char *szServerRoot)
{
    BOOL bReturn = FALSE;
    HKEY hKey;
    DWORD dwDisposition;
    char szKey[MAX_PATH];
    char szTemp[MAX_PATH];
	int maxpath = MAX_PATH;
	char *reg = REGSTR_PATH_SERVICES;
	char *id = SVR_ID_SERVICE;
	char *ver = SVR_VERSION;
	char *key = KEY_PERFORMANCE;

    wsprintf(szKey, "%s\\%s%s\\%s", REGSTR_PATH_SERVICES, SVR_ID_SERVICE, SVR_VERSION, KEY_PERFORMANCE);
    if(RegCreateKeyEx(HKEY_LOCAL_MACHINE, szKey, 0, "", REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, &dwDisposition) == ERROR_SUCCESS)
    {
        RegSetValueEx(hKey, "Open", 0, REG_SZ, PERF_OPEN_FUNCTION, lstrlen(PERF_OPEN_FUNCTION)+2);
        RegSetValueEx(hKey, "Collect", 0, REG_SZ, PERF_COLLECT_FUNCTION, lstrlen(PERF_COLLECT_FUNCTION)+2);
        RegSetValueEx(hKey, "Close", 0, REG_SZ, PERF_CLOSE_FUNCTION, lstrlen(PERF_CLOSE_FUNCTION)+2);
        wsprintf(szTemp, "%s\\bin\\slapd\\server\\nsldapctr.dll", szServerRoot);
        RegSetValueEx(hKey, "Library", 0, REG_SZ, szTemp, lstrlen(szTemp)+2);
        RegCloseKey(hKey);

        wsprintf(szTemp, "unlodctr %s%s", SVR_ID_SERVICE, SVR_VERSION);
        _LaunchAndWait(szTemp, INFINITE);
        
        generate_install_ldapctrs_bat();
            
        wsprintf(szTemp, "%s\\%s",  szServerRoot, INSTALL_CTRS_BAT);
        _LaunchAndWait(szTemp, INFINITE);

        DeleteFile(szTemp);
     
    }



    return(bReturn);
}

static void
CopyAndDeleteKey(
	HKEY srcBase, const char *srcName,
	HKEY destBase, const char *destName
)
{
	DWORD index = 0;
	LONG retval = 0;
	HKEY srcHKEY;
	HKEY destHKEY;
	char className[MAX_PATH+1] = {0};
	DWORD classLen = MAX_PATH+1;
	DWORD nKeys = 0, maxKeyLen = 0, maxClassLen = 0, nValues = 0,
		maxValueNameLen = 0, maxValueDataLen = 0;
	DWORD disposition = 0;

	// open the source key
	retval = RegOpenKey(srcBase, srcName, &srcHKEY);
	if (retval != ERROR_SUCCESS) {
		myLogError("CopyAndDeleteKey: could not open src key %s: ret = %d\n",
				   srcName, retval);
		return;
	}

	// get the info from the old key
	retval = RegQueryInfoKey(srcHKEY, className, &classLen, 0, &nKeys, &maxKeyLen,
							 &maxClassLen, &nValues, &maxValueNameLen,
							 &maxValueDataLen, 0, 0);
	if (retval != ERROR_SUCCESS) {
		myLogError("CopyAndDeleteKey: could not read src key info %s: ret = %d\n",
				   srcName, retval);
		return;
	}

	// create the new key based on the info in the old key
	retval = RegCreateKeyEx(destBase, destName, 0, className,
							REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, 0,
							&destHKEY, &disposition);
	if (retval != ERROR_SUCCESS) {
		myLogError("CopyAndDeleteKey: creating new key %s: ret = %d\n",
				   destName, retval);
		return;
	}

	// copy all of the values from the old key to the new key
	for (index = 0; index < nValues; ++index) {
		DWORD valueNameLen = maxValueNameLen+1;
		DWORD valueType = 0;
		DWORD valueDataLen = maxValueDataLen+1;
		char *valueName = calloc(1, valueNameLen);
		char *valueData = calloc(1, valueDataLen);
		retval = RegEnumValue(srcHKEY, index, valueName, &valueNameLen, 0,
							  &valueType, valueData, &valueDataLen);
		if (retval == ERROR_SUCCESS) {
			retval = RegSetValueEx(destHKEY, valueName, 0, valueType,
								   valueData, valueDataLen);
			if (retval != ERROR_SUCCESS) {
				myLogError("CopyAndDeleteKey: could not write value %s to key %s\n",
						   valueName, destName);
			}
		} else {
			myLogError("CopyAndDeleteKey: could not read value %d:%s from key %s\n",
					   index, (valueName ? valueName : "null"), destName);
		}
		free(valueName);
		free(valueData);
	}

	// copy all of the sub keys as well; since we're deleting keys as we go along,
	// the actual nKeys will change
	if (nKeys > 0) {
		for (index = nKeys; index; --index) {
			DWORD keyNameLen = maxKeyLen+1;
			char *keyName = calloc(1, keyNameLen);
			retval = RegEnumKey(srcHKEY, index-1, keyName, keyNameLen);
			if (retval == ERROR_SUCCESS) {
				CopyAndDeleteKey(srcHKEY, keyName, destHKEY, keyName);
			} else {
				myLogError("CopyAndDeleteKey: could not get key %d:%s of nKeys %d:"
						   "error %d\n", index-1, (keyName ? keyName : "null"),
						   nKeys, retval);
			}
			free(keyName);
		}
	}

	// close the destination key
	retval = RegCloseKey(destHKEY);
	if (retval != ERROR_SUCCESS) {
		myLogError("CopyAndDeleteKey: could not close dest key %s\n",
				   destName);
	}

	// close the source key
	retval = RegCloseKey(srcHKEY);
	if (retval != ERROR_SUCCESS) {
		myLogError("CopyAndDeleteKey: could not close source key %s\n",
				   srcName);
	}

	// delete the source key
	retval = RegDeleteKey(srcBase, srcName);
	if (retval != ERROR_SUCCESS) {
		myLogError("CopyAndDeleteKey: could not delete source key %s\n",
				   srcName);
	}

	return;
}

// This function will rename the registry keys from the old version to
// the new version
static void
updateRegistryKeys(const char *oldVersion, const char *newVersion)
{
	char oldKey[MAX_PATH] = {0};
	char newKey[MAX_PATH] = {0};
	int retval = 0;
	DWORD index = 0;
	HKEY svrHKEY;
	DWORD nKeys = 0;
	DWORD maxKeyLen = 0;
	char *ptr = 0;

	// There are three places we need to change
	// the first place is under
	// HKEY_LOCAL_MACHINE\SOFTWARE\Netscape\Directory\oldVersion
	// we need to change oldVersion to newVersion
	sprintf(newKey, "%s\\%s", KEY_SOFTWARE_NETSCAPE, SVR_KEY_ROOT);
	strcpy(oldKey, newKey);
	if (ptr = strstr(oldKey, SVR_VERSION)) {
		strncpy(ptr, oldVersion, strlen(oldVersion));
	}

	myLogData("updateRegistryKeys: copying %s to %s\n",
			  oldKey, newKey);
	CopyAndDeleteKey(HKEY_LOCAL_MACHINE, oldKey, HKEY_LOCAL_MACHINE,
					 newKey);

	// the second place is under
	// HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\slapdoldVersoin
	// we need to change oldVersion to newVersion
	sprintf(oldKey, "%s\\%s%s", KEY_SERVICES, PRODUCT_NAME,
			oldVersion);
	sprintf(newKey, "%s\\%s%s", KEY_SERVICES, PRODUCT_NAME,
			SVR_VERSION);

	CopyAndDeleteKey(HKEY_LOCAL_MACHINE, oldKey, HKEY_LOCAL_MACHINE,
					 newKey);

	// the third place is under
	// HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\slapd-instance
	// for each instance, we need to replace the DisplayName value with
	// the new display name

	// open the services key
	retval = RegOpenKey(HKEY_LOCAL_MACHINE, KEY_SERVICES, &svrHKEY);
	
	// get the info from the key
	retval = RegQueryInfoKey(svrHKEY, 0, 0, 0, &nKeys, &maxKeyLen,
							 0, 0, 0,
							 0, 0, 0);
	if (retval != ERROR_SUCCESS) {
		myLogError("updateRegistryKeys: could not read info %s: ret = %d\n",
				   KEY_SERVICES, retval);
		return;
	}

	// iterate the keys under Services
	for (index = 0; index < nKeys; ++index) {
		DWORD keyNameLen = maxKeyLen+1;
		char *keyName = calloc(1, keyNameLen);
		retval = RegEnumKey(svrHKEY, index, keyName, keyNameLen);
		if (retval == ERROR_SUCCESS && keyName &&
			!strncmp(keyName, PRODUCT_NAME, strlen(PRODUCT_NAME))) {
			// read the DisplayName value from the key
			HKEY key;
			retval = RegOpenKey(svrHKEY, keyName, &key);
			if (retval == ERROR_SUCCESS) {
				DWORD type = REG_SZ;
				char oldValue[MAX_PATH+1] = {0};
				DWORD oldValueLen = MAX_PATH+1;
				char *ptr = 0;

				retval = RegQueryValueEx(key, "DisplayName", 0, &type,
										 oldValue, &oldValueLen);
				// if the DisplayName contains the old version number . . .
				if ((retval == ERROR_SUCCESS) &&
					(ptr = strstr(oldValue, oldVersion))) {
					// . . . replace it
					strncpy(ptr, SVR_VERSION, strlen(SVR_VERSION));
					retval = RegSetValueEx(key, "DisplayName", 0, type,
										   oldValue, oldValueLen);
					if (retval != ERROR_SUCCESS) {
						myLogError("updateRegistryKeys: could not set value %s "
								   "for key %s\n",
								   oldValue, keyName);
					}
				} else {
					myLogError("updateRegistryKeys: could not read DisplayName"
							   "from key %s:%s\n", keyName, oldValue);
				}
				RegCloseKey(key);
			} else {
				myLogError("updateRegistryKeys: could not open service key %s\n",
						   keyName);
			}
		} else {
			myLogError("updateRegistryKeys: could not get key %d:%s of nKeys %d:"
					   "error %d\n", index, (keyName ? keyName : "null"),
					   nKeys, retval);
		}
		free(keyName);
	}

	RegCloseKey(svrHKEY);

	// finally, remove the old Uninstall string
#define REG_UNINST "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Netscape Server Family 4.0"
	myLogData("Deleting key %s", REG_UNINST);
	DeleteServerRegistryKey(HKEY_LOCAL_MACHINE, REG_UNINST);
}

// This function makes sure nsperl is installed and running before the main slapd
// post install runs, which needs nsperl to run
static BOOL
NSPERLINST_PostInstall(VOID)
{
  BOOL bRC = TRUE;
  char *p = 0;
  char instDir[BUFSIZ] = {0};
  char nsPerlPostInstall[MAX_PATH] = {0};
  char infFile[MAX_PATH] = {0};
  char srcPath[MAX_PATH] = {0};
  char destPath[MAX_PATH] = {0};
  char szCurrentDir[MAX_STR_SIZE] = {0};

  if (GetCurrentDirectory(MAX_STR_SIZE, szCurrentDir) == 0) {
    myLogError("NSPERLINST_PostInstall could not determine the current directory");
    return FALSE;
  }

  // hack to work around potential bug in setupsdk . . .
  SetCurrentDirectory("../slapd");
  sprintf(infFile, "slapd.inf");
  GetProductInfoStringWithTok(NSPERL_POST_INSTALL_PROG, "=", nsPerlPostInstall,
			      BUFSIZ, infFile);

  p = strrchr(nsPerlPostInstall, '/');
  if (!p)
	p = strrchr(nsPerlPostInstall, '\\');
  if (!p) {
	// punt
    myLogError("NSPERLINST_PostInstall: could not get the post install program %s"
	       " from the info file %s", nsPerlPostInstall, infFile);
    return FALSE;
  }

  // get the RunPostInstall attribute from the inf; this is the name
  // of the post install program
  *p = 0; // p points at last dir sep in the path, so null it
  sprintf(instDir, "%s\\%s", TARGETDIR, nsPerlPostInstall);
  p++;

  // change directory to the directory of the post install program and
  // execute it
  if (SetCurrentDirectory(instDir) == 0) {
    myLogError("NSPERLINST_PostInstall: could not change directory to %s",
	       instDir);
    return FALSE;
  }

  if (_LaunchAndWait(p, INFINITE) != 0) {
    myLogError("NSPERLINST_PostInstall: could not run the nsperl post install"
	       " program %s from directory %s", p, instDir);

    SetCurrentDirectory(szCurrentDir);
    return FALSE;
  }

  SetCurrentDirectory(szCurrentDir);

  sprintf(srcPath, "%s\\nsperl.exe", instDir);
  sprintf(destPath, "%s\\%s", TARGETDIR, PERL_EXE);

  if (FALSE == CopyFile(srcPath, destPath, FALSE)) { // FALSE to overwrite file if exists
    myLogError("NSPERLINST_PostInstall: could not copy file %s to %s",
	       srcPath, destPath);
    bRC = FALSE;
  }

  myLogData("Successfully installed nsPerl");
  return bRC;
}


//////////////////////////////////////////////////////////////////////////////
// DSINST_PostInstall
//
// The framework calls this function to perform post-installation
// configuration.  Here you should set values in any product configuration
// files, install services, add registry keys, start servers, and anything
// else that can only be done once the binaries are layed down on the disk.
// If the function succeeds return TRUE, otherwise return FALSE to indicate
// an error.
//

BOOL __declspec(dllexport)
DSINST_PostInstall(VOID)
{
	// TODO: Add code to perform configuration.
	BOOL rc;

	myLogData("DSINST_PostInstall: BEGIN");

	rc = NSPERLINST_PostInstall();
	
	/* install perfmon*/
	_InstallPerfmon(TARGETDIR);

	if (1 == mi.m_nReInstall )
	{
		myLogData("DSINST_PostInstall: doing a reinstall");
		/* if the old version is not equal to the new version, we need to
		   update the various registry keys */
		if (strcmp(oldVersion, SVR_VERSION)) {
			updateRegistryKeys(oldVersion, SVR_VERSION);
		}

		/* turn servers back on */
		ControlSlapdServers(TARGETDIR, TRUE, TRUE);
	
		/* do any other ReInstall things here */
	}

	/*create slapd instance detects reinstall and calls index with -r */
	myLogData("DSINST_PostInstall: before create_slapd_instance %s", mi.m_szInstanceHostName);
	if (0 == create_slapd_instance(mi.m_szInstanceHostName, TARGETDIR) )
	{
		if ( 0 == generate_mcc_bat() )
		{
			rc = TRUE;
		} else
		{
			DSMessageBoxOK(ERR_CREATE_MCC_BAT_TITLE, ERR_CREATE_MCC_BAT, 0);
			rc = FALSE;
		}
	} else
	{
		DSMessageBoxOK(ERR_CREATE_DS_INSTANCE_TITLE, ERR_CREATE_DS_INSTANCE, 0);
		rc = FALSE;
	}

	/* turn SNMP service back on if it was running */
	if ( 1 == mi.m_nSNMPOn)
	{
		if ( 0 == ControlServer(SNMP_SERVICE, TRUE) )
		{
			/* complain but continue with install */
			DSMessageBoxOK(ERR_SNMP_BAD_STARTUP_TITLE, ERR_SNMP_BAD_STARTUP, 0);
		}
	}
	
	return rc;
}

//--------------------------------------------------------------------------//
//                                                                          //
//--------------------------------------------------------------------------//

static BOOL RemoveSNMPValue(void)
{

    char  line[MAX_PATH];
    char  NumValuesBuf[3];
    DWORD Result;
    HKEY  hServerKey;
    DWORD NumValues;
    DWORD iterator;
    int   value_already_exists = 0;
    DWORD type_buffer;
    char  value_data_buffer[MAX_PATH];
    DWORD sizeof_value_data_buffer;

    /* open registry key for Microsoft SNMP service  */
    sprintf(line, "%s\\%s", KEY_SERVICES, KEY_SNMP_SERVICE); 
    Result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, 
                          line,
                          0,
                          KEY_ALL_ACCESS,
                          &hServerKey);

    /* if Microsoft SNMP Service is installed look 
       for slapd snmp value to remove  */
    if (Result == ERROR_SUCCESS) 
    {
        sprintf(line,
                "%s\\%s\\%s",
                KEY_SOFTWARE_NETSCAPE, 
                SVR_KEY_ROOT, 
                KEY_SNMP_CURRENTVERSION);

        Result = RegQueryInfoKey(hServerKey, 
                                 NULL, NULL, 
                                 NULL, NULL, 
                                 NULL, NULL, 
                                 &NumValues, 
                                 NULL, NULL, 
                                 NULL, NULL);
        
        if (Result == ERROR_SUCCESS)
        {
            for(iterator = 0; iterator <= NumValues; iterator++)
            {
                sizeof_value_data_buffer=MAX_PATH;
                sprintf(NumValuesBuf, "%d", iterator);
                Result = RegQueryValueEx(hServerKey,
                                         NumValuesBuf,
                                         NULL,
                                         &type_buffer,
                                         value_data_buffer,
                                         &sizeof_value_data_buffer);

                if(!lstrcmp(value_data_buffer, line))
                {
                    /* remove the value */
                    Result = RegDeleteValue(hServerKey, NumValuesBuf); 
                    break;
                }
            }				
        }
    }
    RegCloseKey(hServerKey);

    return (Result == ERROR_SUCCESS);
}

BOOL RemoveSNMPKeys(void)
{
  
    char  line[MAX_PATH];
    BOOL  bRC = TRUE;

    /* open registry key for Directory SNMP s */
     memset(line, '\0', MAX_PATH);
     sprintf(line, "%s\\%s\\%s", KEY_SOFTWARE_NETSCAPE, SVR_KEY_ROOT, 
                             	 KEY_SNMP_CURRENTVERSION);
    
     RegDeleteKey(HKEY_LOCAL_MACHINE, line);

     memset(line, '\0', MAX_PATH);
     sprintf(line, "%s\\%s\\%s", KEY_SOFTWARE_NETSCAPE, SVR_KEY_ROOT, 
                             	 SNMP_SERVICE_NAME);

     RegDeleteKey(HKEY_LOCAL_MACHINE, line);

     return bRC;
}


BOOL RemovePerfMon(void)
{
  
    char  szTemp[MAX_PATH];
    BOOL  bRC = TRUE;
   
     // uninstall perfmon counters and keys
     wsprintf(szTemp, "unlodctr %s%s", SVR_ID_SERVICE, SVR_VERSION);
     _LaunchAndWait(szTemp, 10000);

     wsprintf(szTemp, "%s\\%s%s\\%s", REGSTR_PATH_SERVICES,
			  SVR_ID_SERVICE, SVR_VERSION, KEY_PERFORMANCE);
     RegDeleteKey(HKEY_LOCAL_MACHINE, szTemp );

     wsprintf(szTemp, "%s\\%s%s", REGSTR_PATH_SERVICES,
			  SVR_ID_SERVICE, SVR_VERSION);
     RegDeleteKey(HKEY_LOCAL_MACHINE, szTemp );
    
     return bRC;
}

BOOL RemoveDirectoryRootKey()
{     
    char  line[MAX_PATH];
    BOOL  bRC = TRUE;

    memset(line, '\0', MAX_PATH);
    sprintf(line, "%s\\%s", KEY_SOFTWARE_NETSCAPE, DS_NAME_SHORT);

    RegDeleteKey(HKEY_LOCAL_MACHINE, line);
  
    return bRC;
}

static void
_PumpMessage(HWND hwndMsgDlg)
{
  MSG msg;

  while (GetMessage(&msg, NULL, 0, 0))
  {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}

//////////////////////////////////////////////////////////////////////////////
// ShutdownDialogProc
//
//	winproc for status window users sees when trying to shutdown instance so
//		install doesn't appear to be hung
//
//


BOOL CALLBACK ShutdownDialogProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	int retval;

	myLogData("ShutdownDialog Proc iMsg=%d wParam=%d lParam=%d", iMsg, wParam, lParam);

	switch (iMsg)
	{
	
	case WM_INITDIALOG:
		retval = SetWindowText(hwnd, dialogMessage);
		myLogData("19 SetWindowText returns %d", retval);
		if (!retval) myLogError("19 SetWindowText");
		retval = SetDlgItemText(hwnd, IDC_STOPPING_SERVER_MESSAGE,
								dialogMessage);
		myLogData("20 SetDlgItemText returns %d", retval);
		if (!retval) myLogError("20 SetDlgItemText");
		myLogData("iMsg=%d WM_INITDIALOG=%d WM_CREATE=%d msg=%s",
				iMsg, WM_INITDIALOG, WM_CREATE, dialogMessage); 
		return TRUE;
	
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	}

	return FALSE;
}

//////////////////////////////////////////////////////////////////////////////
// shutdownDialog
//
//	thread proc for creating shutdown dialog acts as win main translating and
//	dispatching events
//
//


void shutdownDialog(ShutdownArg *shutdownargs)
{
	CHAR dbg[MAX_STR_SIZE]="\0";
	int retval;

	dialogMessage = shutdownargs->pszServiceName;
	myLogData("Before createDialog");
	shutdownargs->hwnd = CreateDialog(mi.m_hModule,
									  MAKEINTRESOURCE(IDD_UNINSTALL_STATUS),
									  NULL,
									  ShutdownDialogProc);
	myLogData("After createDialog");
    if (shutdownargs->hwnd == NULL)
    {
		return;
    }

	CenterWindow(shutdownargs->hwnd);
	retval = SetWindowText(shutdownargs->hwnd, dialogMessage);
	myLogData("1 SetWindowText returns %d", retval);
	retval = GetWindowText(shutdownargs->hwnd, dbg, MAX_STR_SIZE);
	myLogData("GetWindowText string [%s] retval %d", dbg, retval);
	retval = SetDlgItemText(shutdownargs->hwnd, IDC_STOPPING_SERVER_MESSAGE,
							dialogMessage);
	myLogData("3 SetDlgItemText returns %d", retval);
	retval = UpdateWindow(shutdownargs->hwnd);
	myLogData("5 UpdateWindow returns %d", retval);
	retval = ShowWindow(shutdownargs->hwnd, SW_SHOWNORMAL);
	myLogData("6 ShowWindow returns %d", retval);
	retval = UpdateWindow(shutdownargs->hwnd);
	myLogData("7 UpdateWindow returns %d", retval);
	retval = SetWindowText(shutdownargs->hwnd, dialogMessage);
	myLogData("8 SetWindowText returns %d", retval);
	retval = GetWindowText(shutdownargs->hwnd, dbg, MAX_STR_SIZE);
	myLogData("9 GetWindowText string [%s] retval %d", dbg, retval);
	retval = UpdateWindow(shutdownargs->hwnd);
	myLogData("10 retval=%d msg=%s\n", retval, dialogMessage); 

	_PumpMessage(shutdownargs->hwnd);
}

BOOL writeUninstINFfile(const char *filename, 
						const char *pszServerRoot,
						const char *pszServiceName)
{
   FILE *fp = fopen(filename, "wb");
   CHAR szHostName[MAX_STR_SIZE]="\0";

   DSGetHostName(szHostName, MAX_STR_SIZE);
   
   if (NULL == fp)
	   return FALSE;

   // write section header
   fprintf(fp, "[uninstall]\n");
   fprintf(fp, "%s= %s\n", SLAPD_KEY_FULL_MACHINE_NAME, szHostName);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_SERVER_ROOT, pszServerRoot);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_SERVER_IDENTIFIER, pszServiceName);
   fprintf(fp, "%s= %s\n", GLOBAL_INF_LDAP_HOST,	GetLdapHost() );
   fprintf(fp, "%s= %d\n", GLOBAL_INF_LDAP_PORT,	GetLdapPort() );
   fprintf(fp, "%s= %s\n", SLAPD_KEY_SUFFIX,	GetLdapSuffix()	);
   fprintf(fp, "%s= %s\n", SLAPD_KEY_SERVER_ADMIN_ID,	GetLdapUser() );
   fprintf(fp, "%s= %s\n", SLAPD_KEY_SERVER_ADMIN_PWD,	GetLdapPassword() );
   fprintf(fp, "%s= %s\n", SLAPD_INSTALL_LOG_FILE_NAME,	LOGFILE );
   
   fclose(fp);

   return TRUE;
}

//////////////////////////////
//
//  try to turn a server instance on or off
//  displays dialog while doing so
//
//

void ControlSlapdInstance(char *pszServiceName, BOOL bOn)
{
   INT shutdown_tries=0;
   BOOL bServerRunning=0;
   CHAR szLog[MAX_STR_SIZE]="\0";
   CHAR szFormat[MAX_STR_SIZE]="\0";
   ShutdownArg  shutdownargs;
   CHAR szMessage[MAX_STR_SIZE]="\0";
   const CHAR *shortName = getShortName(pszServiceName);

   myLogData("Begin ControlSlapdInstance");
   if(bOn)
   {
   		LoadString( mi.m_hModule, IDS_STARTING_SERVICE, szFormat, MAX_STR_SIZE);
   }else{
   		LoadString( mi.m_hModule, IDS_STOPPING_SERVICE, szFormat, MAX_STR_SIZE);
   }

   sprintf(szMessage, szFormat, shortName);

   myLogData(szMessage);
	ZeroMemory(&shutdownargs, sizeof(shutdownargs));
	/* strategy here is to try and turn on/off the server,
		sometimes it may take more than the first try
		if can't do it after N tries then give up
		and warn user */

	/* check for opposite of bOn (ie starting server bOn = true, check if its down = false) */
	while ( shutdown_tries < MAX_SLAPD_SHUTDOWN_TRIES
			&& (bOn != (bServerRunning = isServiceRunning( pszServiceName ) ) ) )
	{
		/* try to turn of the server */
		sprintf(szLog, szMessage);
		LogData(NULL, szLog);
		myLogData(szLog);

		/* setup and launch thread to display window to user
			so it doesn't think install is hung */
		shutdownargs.pszServiceName = szMessage;
		_beginthread(shutdownDialog, 0, &shutdownargs);

		ControlServer( pszServiceName, bOn );

		/* give it some time to shutdown */
		/* unneeded? */
   	
		   if(bOn)
		   {
   				LoadString( mi.m_hModule, IDS_WAIT_SERVICE_START, szFormat, MAX_STR_SIZE);
		   }else{
   				LoadString( mi.m_hModule, IDS_WAIT_SERVICE_STOP, szFormat, MAX_STR_SIZE);
		   }

	  	   sprintf(szLog, szFormat, shortName);
		   LogData(NULL, szLog);
		   myLogData(szLog);

		Sleep(SLAPD_SHUTDOWN_TIME_MILLISECONDS);

		shutdown_tries++;
	}

	if ((shutdown_tries > 0) && (shutdownargs.hwnd > 0))
	{
		SendMessage(shutdownargs.hwnd, WM_DESTROY, 0, 0);
	} 

	if ( MAX_SLAPD_SHUTDOWN_TRIES == shutdown_tries)
	{
		/* check if it got it the last time */

		/* it should be whatever user wanted in bOn at this point*/
		if (bOn == (bServerRunning = isServiceRunning( pszServiceName ) ) )
		{
			/* warn user, ask if they want to continue */

			if ( IDOK == DSMessageBox(MB_OKCANCEL, ERR_SLAPD_SHUTDOWN_TITLE,
									  ERR_SLAPD_SHUTDOWN, shortName, shortName) )
			{

			} else
			{
				UINT uExitCode = 1;
				/* stevross: use ExitProcess until admin server provides us with
					   better way to exit framework and cleanup */
				ExitProcess(uExitCode);
			}

		}

	}
   myLogData("End ControlSlapdInstance");
}

static void ConvertPasswordToPin(char *pszServerRoot, char *pszServiceName)
{
	CHAR szFormat[MAX_STR_SIZE*4]="\0";
	CHAR szCurrentDir[MAX_STR_SIZE]="\0";
	CHAR szNewDir[MAX_STR_SIZE]="\0";

	myLogData("Begin ConvertPasswordToPin");

	/* get current dir so we have it for later */
	if (GetCurrentDirectory(MAX_STR_SIZE, szCurrentDir) == 0)
	{
		myLogData("ConvertPasswordToPin: could not get current directory: %d",
				  GetLastError());
		return;
	}
	/* have to be in the alias directory to run this */
	sprintf(szNewDir, "%s\\alias", pszServerRoot);
	/* change current dir to the alias directory */
	if (SetCurrentDirectory(szNewDir) == 0)
	{
		myLogData("ConvertPasswordToPin: could not set current directory to %s: %d",
				  szNewDir, GetLastError());
		return;
	}

	/* spawn the perl script which does the conversion */
	sprintf(szFormat, "\"%s\\bin\\slapd\\admin\\bin\\migratePwdFile\" \"%s\" %s",
		pszServerRoot, pszServerRoot, pszServiceName);
	run_cgi(pszServerRoot, PERL_EXE, szFormat);

	if (SetCurrentDirectory(szCurrentDir) == 0)
	{
		myLogData("ConvertPasswordToPin: could not set current directory back to %s: %d",
				  szCurrentDir, GetLastError());
		return;
	}
	
	myLogData("End ConvertPasswordToPin");
}

static void ReinstallUpgradeServer(char *pszServerRoot, char *pszServiceName)
{
	CHAR szFormat[MAX_STR_SIZE*4]="\0";
	CHAR szCurrentDir[MAX_STR_SIZE]="\0";

	myLogData("Begin ReinstallUpgradeServer");

	/* get current dir so we have it for later */
	if (GetCurrentDirectory(MAX_STR_SIZE, szCurrentDir) == 0)
	{
		myLogData("ReinstallUpgradeServer: could not get current directory: %d",
				  GetLastError());
		return;
	}
	/* have to be in the server root directory to run this */
	if (SetCurrentDirectory(pszServerRoot) == 0)
	{
		myLogData("ReinstallUpgradeServer: could not set current directory to %s: %d",
				  pszServerRoot, GetLastError());
		return;
	}

	/* spawn the perl script which does the conversion */
	sprintf(szFormat, "\"%s\\bin\\slapd\\admin\\bin\\upgradeServer\" \"%s\" %s",
		pszServerRoot, pszServerRoot, pszServiceName);
	run_cgi(pszServerRoot, PERL_EXE, szFormat);

	if (SetCurrentDirectory(szCurrentDir) == 0)
	{
		myLogData("ReinstallUpgradeServer: could not set current directory back to %s: %d",
				  szCurrentDir, GetLastError());
		return;
	}
	
	myLogData("End ReinstallUpgradeServer");
}

BOOL RemoveSlapdInstance(LPCSTR pszServerRoot, char *pszServiceName)
{
	int status = 0;
	char szINFfile[MAX_STR_SIZE] = "\0";
	CHAR szCGIArgs[MAX_STR_SIZE]="\0";


    /* try to turn of service */
	ControlSlapdInstance(pszServiceName, FALSE);

    /* now try to remove the instance */

	/* call remove cgi with inf */
	sprintf(szINFfile, "%s/unin%d.inf", TEMPDIR, _getpid());	
	writeUninstINFfile(	szINFfile, pszServerRoot, pszServiceName);
	sprintf(szCGIArgs, " -f \"%s\"", szINFfile);

	/* remove this instance */
	status = run_cgi(pszServerRoot, "bin\\slapd\\admin\\bin\\ds_remove.exe", szCGIArgs);
	
	/* remove temp inffile */
	_unlink(szINFfile); 

    return (status == 0); /* return true if run_cgi succeeded */
}


BOOL RemoveMiscRegistryEntries(void)
{
    BOOL bRC = TRUE;


    RemoveSNMPKeys();
    RemoveSNMPValue();
 
    return bRC;
}

BOOL RemoveMiscSlapdFiles(pszServerRoot)
{

    char *miscFilesList[] = 
    {
     "dsgw", 
     "plugins\\slapd", 
     "plugins\\snmp\\netscape-ldap.mib",
     "bin\\slapd",
     "manual\\slapd",
     "relnotes.gif",
     "relnotes.html",
     "slapd.txt",
     "unsynch.exe",
     "mcc.bat",
     "authdb",
     "setup\\slapd",
     "ldap.info",
     NULL
    };

    int i;
    
    CHAR szFileName[MAX_STR_SIZE];

    for(i=0; miscFilesList[i] != NULL; i++)
    {
        memset(szFileName, '\0', MAX_STR_SIZE);
        sprintf(szFileName, "%s\\%s", pszServerRoot, miscFilesList[i] );
        DeleteRecursively(szFileName);
    }

    return TRUE;
 }

//////////////////////////////////////////////////////////////////////////////
// PreUninst
//
// 
// Do things before uninstalling like turn off the server
// 
// 
// 
//

BOOL __declspec(dllexport)
DSINST_PreUnInstall(LPCSTR pszServerRoot)
{
    BOOL rc = TRUE;
	BOOL snmpstatus;
    WIN32_FIND_DATA fileData;
    HANDLE hFileHandle;
	CHAR szCurrentDir[MAX_STR_SIZE]="\0";

    /* for now just turn of the one instance we install */
    /* later look in directory for anything slapd- and turn that off */
    
    /* stevross: do this here until decide what to do with 
                 DeleteServerRegistryKeys in Remove Instance */

	/* get current dir so we have it for later */
	GetCurrentDirectory(MAX_STR_SIZE, szCurrentDir);
	
	/* change current dir to server root */
	SetCurrentDirectory(pszServerRoot);

	/* Turn off SNMP Service if Running */
	snmpstatus = getSNMPStatus();

	/* remove SNMP keys and any other Misc stuff */
	RemoveMiscRegistryEntries();

	hFileHandle = FindFirstFile("slapd-*", &fileData);

	if( INVALID_HANDLE_VALUE != hFileHandle)
	{
		rc = RemoveSlapdInstance(pszServerRoot, fileData.cFileName);
		while(TRUE == FindNextFile(hFileHandle, &fileData) )
		{
			BOOL status = RemoveSlapdInstance(pszServerRoot, fileData.cFileName);
			/* we want to report failure even if only 1 instance removal fails */
			if (rc)
			{
				rc = status;
			}
		}
		FindClose(hFileHandle);
	}
       		
	/* turn SNMP service back on if it was running */
	if (snmpstatus)
	{
		if( 1 == mi.m_nSNMPOn)
		{
			if( 0 == ControlServer(SNMP_SERVICE, TRUE) )
			{
				/* complain but continue with install */
				DSMessageBoxOK(ERR_SNMP_BAD_STARTUP_TITLE,
							   ERR_SNMP_BAD_STARTUP, 0);
			}
		}
	}

	/* set back to previous current directory */
	SetCurrentDirectory(szCurrentDir);

	/* unfortunately, if we just return FALSE here, uninstall will continue
	   happily along, and ultimately remove the uninst.exe program which we
	   need to run again after we figure out what went wrong
	   So, we must exit here
	*/
	if (!rc) {
		DSMessageBoxOK(ERR_UNINSTALL_DS_TITLE,
					   ERR_UNINSTALL_DS, 0, LOGFILE);
		ExitProcess(1);
	}

    return rc;
}

//////////////////////////////////////////////////////////////////////////////
// PostUninst
//
// 
// Clean up registry keys etc
// 
//
//
//
BOOL __declspec(dllexport)
DSINST_PostUnInstall(LPCSTR pszServerRoot)
{

    BOOL bRC = TRUE;

   /* remove misc files */

    
    RemovePerfMon();
    RemoveMiscSlapdFiles(pszServerRoot);
    RemoveDirectoryRootKey();
  
    return bRC;
}

//////////////////////////////////////////////////////////////////////////////
// DllMain
//
// The Windows DLL main entry point.  Called upon loading the DLL into memory.
// Perform all initialization in the DLL_PROCESS_ATTACH reason handler, and
// release any resources that you have allocated in the DLL_PROCESS_DETACH
// message handler.  See the Windows SDK documentation for more information
// on this function.
//

BOOL WINAPI
DllMain(HANDLE hModule, ULONG ulReasonForCall, LPVOID lpReserved)
{
  switch (ulReasonForCall)
  {
    case DLL_PROCESS_ATTACH:
      mi.m_hModule = hModule;
	  StartWSA();
	  initialize_module();

	  /* set default server settings */
	  set_default_ldap_settings();
      break;
    case DLL_PROCESS_DETACH:
      break;
    case DLL_THREAD_ATTACH:
      break;
    case DLL_THREAD_DETACH:
      break;
  }
  return TRUE;
}

static void
fixDN(char *dn)
{
	if (dn && *dn)
	{
		char *utf8dn = localToUTF8(dn);
		char *localdn = NULL;
		dn_normalize_convert(utf8dn);
		localdn = UTF8ToLocal(utf8dn);
		strcpy(dn, localdn);
		nsSetupFree(utf8dn);
		nsSetupFree(localdn);
	}
}

static void
fixURL(char *url)
{
	if (url && *url)
	{
		char host[MAX_STR_SIZE];
		int port;
		char base[MAX_STR_SIZE];
		GetURLComponents(url, host, &port, base);
		fixDN(base);
		sprintf(url, "ldap://%s:%d/%s", host, port, base);
	}
}
		

static void
normalizeDNs()
{
	fixDN(mi.m_szMCCSuffix);
	fixDN(mi.m_szUGSuffix);
	fixDN(mi.m_szInstallDN);
	fixDN(mi.m_szInstanceSuffix);
	fixDN(mi.m_szInstanceUnrestrictedUser);
	fixDN(mi.m_szSupplierDN);
	fixDN(mi.m_szChangeLogSuffix);
	fixDN(mi.m_szConsumerDN);
	fixDN(mi.m_szConsumerBindAs);
	fixDN(mi.m_szSupplierBindAs);
	fixDN(mi.m_szConsumerRoot);
	fixDN(mi.m_szSupplierRoot);
	fixURL(mi.m_szLdapURL);
	fixURL(mi.m_szUserGroupURL);
}

/*
  Usage:
  	DSMessageBox(type, titleKey, msgKey, titlearg, msgarg1, ..., msgargN);
*/
int
DSMessageBox(UINT type, UINT titleKey, UINT msgKey, const char *titlearg, ...)
{
	int retval = 0;
	va_list ap;
	CHAR msgFormat[MAX_STR_SIZE] = {0};
	CHAR msg[MAX_STR_SIZE*2] = {0};
	CHAR titleFormat[MAX_STR_SIZE] = {0};
	CHAR title[MAX_STR_SIZE] = {0};

	LoadString(mi.m_hModule, msgKey, msgFormat, MAX_STR_SIZE);
	if (!msgFormat[0])
		return retval;

	if (titleKey >= 0)
		LoadString(mi.m_hModule, titleKey, titleFormat, MAX_STR_SIZE);

	va_start(ap, titlearg);
	vsprintf(msg, msgFormat, ap);
	va_end(ap);

	LogData(NULL, msg);
	myLogData(msg);
	if (SILENTMODE != MODE)
	{
		if (titleFormat[0])
		{
			sprintf(title, titleFormat, titlearg);
			retval = NsSetupMessageBox(NULL, msg, title, type);
		}
		else
			retval = NsSetupMessageBox(NULL, msg, NULL, type);
	}
	else
	{
		retval = IDOK; /* force OK for silent mode */
	}

	return retval;
}

/*
  Usage:
  	DSMessageBoxOK(titleKey, msgKey, titlearg, msgarg1, ..., msgargN);
*/
int
DSMessageBoxOK(UINT titleKey, UINT msgKey, const char *titlearg, ...)
{
	int retval = 0;
	va_list ap;
	CHAR msgFormat[MAX_STR_SIZE] = {0};
	CHAR msg[MAX_STR_SIZE*2] = {0};
	CHAR titleFormat[MAX_STR_SIZE] = {0};
	CHAR title[MAX_STR_SIZE] = {0};

	LoadString(mi.m_hModule, msgKey, msgFormat, MAX_STR_SIZE);
	if (!msgFormat[0])
		return retval;

	if (titleKey >= 0)
		LoadString(mi.m_hModule, titleKey, titleFormat, MAX_STR_SIZE);

	va_start(ap, titlearg);
	vsprintf(msg, msgFormat, ap);
	va_end(ap);

	LogData(NULL, msg);
	if (MODE != SILENTMODE)
	{
		if (titleFormat[0])
		{
			sprintf(title, titleFormat, titlearg);
			retval = NsSetupMessageBox(NULL, msg, title, MB_OK);
		}
		else
			retval = NsSetupMessageBox(NULL, msg, NULL, MB_OK);
	}
	else
	{
		myLogData(msg); /* log the message */
		retval = IDOK; /* force true return if silent mode */
	}

	return retval;
}

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*********************************************************************
**
** NAME:
**   ux-config.cc
**
** DESCRIPTION:
**   Fedora Directory Server Pre-installation Program
**
** NOTES:
**   This program is intended for UNIX only and is NOT thread-safe.
**   Based on the original ux-config.c.
**
*********************************************************************/

extern "C" {
#include <stdio.h>
#include <string.h>
#ifdef AIX
#include <strings.h>
#endif
#include "nspr.h"
#include "plstr.h"
}
/* Newer g++ wants the new std header forms */
#if defined( Linux )
#include <strstream>
using std::ostrstream;
/* But some platforms won't accept those (specifically HP-UX aCC */
#else
#include <strstream.h>
#endif
#include "dialog.h"
#include "ux-config.h"
#include "ux-dialog.h"
#include "install_keywords.h"
#include "utf8.h"
extern "C" {
#include <dsalib.h>

#if defined(__sun) || defined(__hppa) || defined(__osf__) || defined(__linux__) || defined(linux)
#include <netdb.h>
#endif
}

extern const char *DEFAULT_SYSUSER = "root";
extern const char *DEFAULT_OLDROOT = "/usr/ns-home";

const int RECONFIG_EXIT_CODE = 7;

/*
 * iDSISolaris is set to 1 for Solaris 9+ specific installation.
 * This can be done by passing -S as the command line argument.
 */
int iDSISolaris = 0;

static char *
my_strdup(const char *s)
{
	char *ret = 0;
	if (s)
	{
		ret = new char[strlen(s) + 1];
		strcpy(ret, s);
	}

	return ret;
}

/*********************************************************************
**
** METHOD:
**   main
** DESCRIPTION:
**   This is the ns-config program. This program functions as
**     - The Pre-installation program used during the Installation
**       of the Directory Server. In this case, the program
**       is supposed to be executed by the common installer (ns-setup)
**       and can be executed from anywhere.
**
**     - The stand-alone configuration program used to re-configure
**       the directory server. In this case, the program has
**       to be executed from the serverroot.
**
** SIDE EFFECTS:
**   None
** RESTRICTIONS:
**
** ALGORITHM:
**
**********************************************************************/
int
main(int argc, char **argv)
{
   int err = 0;

   SlapdPreInstall program(argc, argv);

   err = program.init();
   if (!err)
   {
      err = program.start();
   }

   return err;
} 

SlapdPreInstall::SlapdPreInstall(int argc, char **argv) : _reconfig(False)
{
   setInstallMode(Interactive);
   setInstallType(Typical);
   _configured = False;

   getOptions(argc, argv);

}

SlapdPreInstall::~SlapdPreInstall()
{
}

void
SlapdPreInstall::getOptions(int argc, char **argv)
{
   int opt;

   while ((opt = getopt(argc,argv, "l:f:m:rsS")) != EOF)
   {
      switch (opt)
      {
         case 'l':
           _logFile = strdup(optarg);
           break;
         case 'f':
           _infoFile = strdup(optarg);
           break;
         case 's':
           setInstallMode(Silent);
           break;
         case 'm':
           setInstallType((InstallType)atoi(optarg));
           break;
         case 'r':
           _reconfig = True;
           break;
         case 'S':
	   /*
	    * Solaris 9+ specific installation
	    */	 
           iDSISolaris = 1;
           break;
         default:
		   fprintf(stderr, "SlapdPreInstall::getOptions(): "
				   "invalid option [%s]\n", argv[optind-1]);
           break;
       }
   }
}


int
SlapdPreInstall::init()
{
   char errMsg[256];
   struct stat fi;
   Bool shell = True;

   _installInfo = NULL;
   _slapdInfo = new InstallInfo;

   if (installMode() != Silent)
   {
/* richm 20011005 - we can't do this until we get setupsdk46 - if ever
      if (iDSISolaris)
         Dialog::initDisplay("Directory", (const char *) NULL, "Configuration");
      else
*/
	 Dialog::initDisplay("Directory");
   }

   if ((installMode() == Silent && _infoFile == (char *) NULL) || 
       (_infoFile != (char *) NULL && InstUtil::fileExists(_infoFile) == False))
   {
      PR_snprintf(errMsg, sizeof(errMsg), "ERROR: answer cache not found\n");
      if (installMode() == Silent)
      {
         printf(errMsg);
      }
      else
      {
         DialogAlert alert(errMsg);
         alert.execute();
      }
      return -1;
   }

   _serverRoot = InstUtil::getCurrentDir();
   if (installMode() != Silent)
   {
      if (_infoFile == (char *) NULL)
      {
         // Not executing from the Shell, check if this is the server
         if (stat ("admin-serv", &fi) != 0)
         {
            PR_snprintf(errMsg, sizeof(errMsg), "ERROR: %s is not a server root\n",_serverRoot.data());
            DialogAlert alert(errMsg);
            alert.execute();
            return -1;
         }
         shell = False;
		 // if we are here, we are being run to reconfigure
		 _reconfig = True;
      }
   }

   if (installMode() == Silent)
   {
      if (_logFile == (char *) NULL)
      {
         // Should have a logfile
         _logFile = _serverRoot + "/setup/install.log";
      }
      _installLog = new InstallLog (_logFile);
   }

   if (shell)
   {
      _installInfo = new InstallInfo(_infoFile);
      _serverRoot = _installInfo->get(SLAPD_KEY_SERVER_ROOT);
	  if (!(_adminInfo = _installInfo->getSection("admin")))
	  {
		  _adminInfo = new InstallInfo;
	  }
   }
   else
   {
      // Retrieve configuration data into installInfo
      _infoFile = _serverRoot + "/" + "setup/install.inf";
      _installInfo = new InstallInfo();
      if (initDefaultConfig() == -1) {
	    const char *guess_host = InstUtil::guessHostname();
	    if (guess_host) {
			PR_snprintf(errMsg, sizeof(errMsg), "ERROR: %s is not an addressable hostname\n",
						guess_host);
	    } else {
			PR_snprintf(errMsg, sizeof(errMsg), "ERROR: cannot determine an addressable hostname\n");
	    }
            DialogAlert alert(errMsg);
            alert.execute();
	    return -1;
      }
      if (getDNSDomain() == NULL) {
	    const char *guess_domain = InstUtil::guessDomain();	

	    if (guess_domain == NULL) {
			PR_snprintf(errMsg, sizeof(errMsg), "ERROR: cannot determine domainname\n");
	    } else {
			PR_snprintf(errMsg, sizeof(errMsg), "ERROR: domainname is not valid for DNS\n");
	    }
            DialogAlert alert(errMsg);
            alert.execute();
	    return -1;
      }
   }

   setDefaultScript(_slapdInfo);

   char *url = 0;
   char *adminid = 0;
   char *admin_domain = 0;
   getDefaultLdapInfo(_serverRoot, &url, &adminid, &admin_domain);
   if (url && admin_domain) // in some cases adminid is NULL
   {
	   if (!adminid)
	   {
		   // look up the admin ID in the config ds
	   }
	   // use these values as our default values
	   _installInfo->set(SLAPD_KEY_K_LDAP_URL, url);
	   if (adminid)
	   {
		   _installInfo->set(SLAPD_KEY_SERVER_ADMIN_ID, adminid);
	   }
	   _installInfo->set(SLAPD_KEY_ADMIN_DOMAIN, admin_domain);
	   // since this server root is already configured to use
	   // an existing configuration directory server, we will
	   // not allow the user to install another one here, so
	   // the directory server created here will be a user
	   // directory; we will still need to ask for the admin
	   // user password
	   _slapdInfo->set(SLAPD_KEY_USE_EXISTING_MC, "Yes");
	   _slapdInfo->set(SLAPD_KEY_USE_EXISTING_UG, "No");
	   _slapdInfo->set(SLAPD_KEY_SLAPD_CONFIG_FOR_MC, "No");
   }
   else
   {
	   _slapdInfo->set(SLAPD_KEY_SLAPD_CONFIG_FOR_MC, "Yes");
   }

   return 0;
}
/*
 * PVO
 */

int
SlapdPreInstall::initDefaultConfig()
{
   // PVO - should read from DS instead
   if (_adminInfo->isEmpty())
   {
      const char *guess_host = InstUtil::guessHostname();

      if (guess_host) {
#if defined(__sun) || defined(__hppa) || defined(__osf__) || defined(__linux__) || defined(linux)
	static char test_host[BIG_BUF] = {0};
	struct hostent *hp;
	
	PL_strncpyz(test_host,guess_host,sizeof(test_host));
	hp = gethostbyname(test_host);
	if (hp == NULL) {
	  return -1;
	}
#endif
      }
      _installInfo->set(SLAPD_KEY_SERVER_ROOT, _serverRoot);
      _installInfo->set(SLAPD_KEY_FULL_MACHINE_NAME, guess_host);
      _installInfo->set(SLAPD_KEY_K_LDAP_URL, NSString("ldap://")
						+ guess_host
						+ "/"
						+ DEFAULT_LDAP_SUFFIX);
      _installInfo->set(SLAPD_KEY_SUITESPOT_USERID, DEFAULT_SSUSER);
      _installInfo->set(SS_GROUP, DEFAULT_SSGROUP);
   }
   else
   {
      _configured = True;
   }
   return 0;
}

inline void
changeIndex(int &ii, int incr, int min, int max)
{
	ii += incr;
	if (ii < min)
		ii = min;
	if (ii > max)
		ii = max;
}
	
int
SlapdPreInstall::start()
{
	// if we're in silent install mode, don't execute any of the dialogs, just
	// assume the user knows what he/she is doing . . .
	if (installMode() == Silent)
	{
		if (_reconfig)
			shutdownServers();
		return 0;
	}

   // only enable win mode if we are not doing a silent install because
   // it messes up terminal settings
   enableWinMode();

   DialogAction action = DIALOG_NEXT;
   int err = 0;
   Dialog *advancedDialogList[] = {
	   &askUseExistingMC,
	   &askMCHost,
	   &askMCPort,
	   &askMCDN,
	   &askMCAdminDomain,
	   &askUseExistingUG,
	   &askUGHost,
	   &askUGPort,
	   &askUGSuffix,
	   &askUGDN,
	   &askSlapdPort,
	   &askSlapdServerID,
	   &askMCAdminID,
	   &askSlapdSuffix,
	   &askSlapdRootDN,
	   &askAdminDomain,
	   /*
	   &askReplication,
	   &askSIR,
	   &askChangeLogSuffix,
	   &askChangeLogDir,
	   &askConsumerDN,
	   &askSIRHost,
	   &askSIRPort,
	   &askSIRDN,
	   &askSIRSuffix,
	   &askSIRDays,
	   &askSIRTimes,
	   &askCIR,
	   &askCIRHost,
	   &askCIRPort,
	   &askCIRDN,
	   &askCIRSuffix,
	   &askCIRInterval,
	   &askCIRDays,
	   &askCIRTimes,
	   &askReplicationDN,
	   */
	   &askSample,
	   &askPopulate,
	   &askDisableSchemaChecking
   };
   Dialog *advancediDSISolarisForceUGDialogList[] = {
	   &askSlapdPort,
	   &askSlapdServerID,
	   &askMCHost,
	   &askMCPort,
	   &askMCDN,
	   &askSlapdSuffix,
	   &askSlapdRootDN,
	   &askSample,
	   &askPopulate,
	   &askDisableSchemaChecking
   };
   Dialog *normalDialogList[] = {
	   &askUseExistingMC,
	   &askMCHost,
	   &askMCPort,
	   &askMCDN,
	   &askUseExistingUG,
	   &askUGHost,
	   &askUGPort,
	   &askUGSuffix,
	   &askUGDN,
	   &askSlapdPort,
	   &askSlapdServerID,
	   &askMCAdminID,
	   &askSlapdSuffix,
	   &askSlapdRootDN,
	   &askAdminDomain
   };
   Dialog *normalForceUGDialogList[] = {
	   &askSlapdPort,
	   &askSlapdServerID,
	   &askMCDN,
	   &askSlapdSuffix,
	   &askSlapdRootDN
   };
   Dialog *normaliDSISolarisForceUGDialogList[] = {
	   &askSlapdPort,
	   &askSlapdServerID,
	   &askMCHost,
	   &askMCPort,
	   &askMCDN,
	   &askSlapdSuffix,
	   &askSlapdRootDN
   };
   Dialog *expressDialogList[] = {
	   &askMCAdminID,
	   &askSlapdRootDN
   };
   Dialog *expressForceUGDialogList[] = {
	   &askMCDN,
	   &askSlapdRootDN
   };
   Dialog *expressiDSISolarisForceUGDialogList[] = {
	   &askMCHost,
	   &askMCPort,
	   &askMCDN,
	   &askSlapdRootDN
   };
   Dialog *reconfigDialogList[] = {
	   &askReconfigMCAdminPwd
   };
   const int nNormalDialogs = sizeof(normalDialogList) / sizeof(normalDialogList[0]);
   const int nExpressDialogs = sizeof(expressDialogList) / sizeof(expressDialogList[0]);
   const int nExpressForceUGDialogs = sizeof(expressForceUGDialogList) / sizeof(expressForceUGDialogList[0]);
   const int nExpressiDSISolarisForceUGDialogs = sizeof(expressiDSISolarisForceUGDialogList) / sizeof(expressiDSISolarisForceUGDialogList[0]);
   const int nAdvancedDialogs = sizeof(advancedDialogList) / sizeof(advancedDialogList[0]);
   const int nAdvancediDSISolarisForceUGDialogs = sizeof(advancediDSISolarisForceUGDialogList) / sizeof(advancediDSISolarisForceUGDialogList[0]);
   const int nReconfigDialogs = sizeof(reconfigDialogList) / sizeof(reconfigDialogList[0]);
   const int nNormalForceUGDialogs = sizeof(normalForceUGDialogList) / sizeof(normalForceUGDialogList[0]);
   const int nNormaliDSISolarisForceUGDialogs = sizeof(normaliDSISolarisForceUGDialogList) / sizeof(normaliDSISolarisForceUGDialogList[0]);

   int nDialogs = nNormalDialogs;
   Dialog** dialogList = normalDialogList;
   if (_reconfig)
   {
	   nDialogs = nReconfigDialogs;
	   dialogList = reconfigDialogList;
   }
   else if (installType() == Express)
   {
	   nDialogs = nExpressDialogs;
	   dialogList = expressDialogList;
   }
   else if (installType() == Custom)
   {
	   nDialogs = nAdvancedDialogs;
	   dialogList = advancedDialogList;
   }
   else if (!iDSISolaris && featureIsEnabled(SLAPD_KEY_USE_EXISTING_MC))
   {
	   if (installType() == Typical)
	   {
		   nDialogs = nNormalForceUGDialogs;
		   dialogList = normalForceUGDialogList;
	   }
	   else if (installType() == Express)
	   {
		   nDialogs = nExpressForceUGDialogs;
		   dialogList = expressForceUGDialogList;
	   }
   }

   if (iDSISolaris && featureIsEnabled(SLAPD_KEY_USE_EXISTING_MC))
   {
	   if (installType() == Typical)
	   {
		   nDialogs = nNormaliDSISolarisForceUGDialogs;
		   dialogList = normaliDSISolarisForceUGDialogList;
	   }
	   else if (installType() == Express)
	   {
		   nDialogs = nExpressiDSISolarisForceUGDialogs;
		   dialogList = expressiDSISolarisForceUGDialogList;
	   }
	   else if (installType() == Custom)
	   {
	  	   nDialogs = nAdvancediDSISolarisForceUGDialogs;
		   dialogList = advancediDSISolarisForceUGDialogList;
	   }

   }

   getDefaultScript()->set(SLAPD_KEY_SECURITY_ON, "No");

   int ii = 0;

   // initialize all dialogs

   if (!_reconfig)
   {
	   for (ii = 0; ii < nAdvancedDialogs; ++ii)
	   {
		   advancedDialogList[ii]->registerDialogNext(this);
		   advancedDialogList[ii]->enable8BitInput();
		   // this next bit of hackery allows us to use the dialog->setup()
		   // method of each dialog to setup the default values for the
		   // .inf file; if the SETUP_ONLY flag is set, each setup() method
		   // will just return DIALOG_NEXT after setting up the default
		   // values; pretty sneaky, huh?
		   advancedDialogList[ii]->setUserData(SETUP_DEFAULTS, SETUP_ONLY);
		   advancedDialogList[ii]->setUserData(ACTION, DIALOG_NEXT);
		   advancedDialogList[ii]->execute();
		   advancedDialogList[ii]->setUserData(SETUP_DEFAULTS, (long)0);
	   }
	   advancedDialogList[nAdvancedDialogs-1]->registerDialogLast(this);
   }
   else
   {
	   for (ii = 0; ii < nReconfigDialogs; ++ii)
	   {
		   reconfigDialogList[ii]->registerDialogNext(this);
		   reconfigDialogList[ii]->enable8BitInput();
	   }
	   reconfigDialogList[nReconfigDialogs-1]->registerDialogLast(this);
   }

   ii = 0;
   int min = 0;
   // keep looping until we hit the end
   while (ii < nDialogs)
   {
	   int incr = 1; // go to next by default
	   Dialog *d = dialogList[ii];

	   // tell the dialog what the action was that brought it here so that
	   // the dialog knows if it was called as the result of a next or
	   // a prev or whatever
	   d->setUserData(ACTION, (long)action);
//	   cerr << "set action in dialog " << ii << " to " << action << endl;
//	   cerr << "DIALOG_PREV, SAME, NEXT = " << DIALOG_PREV << "," << DIALOG_SAME << "," << DIALOG_NEXT << endl;

	   // execute the dialog
//	   cerr << "executing dialog number " << ii << endl;
	   action = d->execute();
	   if (action == DIALOG_PREV)
	   {
		   incr = -1; // go to prev
//		   cerr << "prev" << endl;
	   }
	   else if (action == DIALOG_SAME)
	   {
		   incr = 0; // repeat this state
//		   cerr << "same" << endl;
	   }
	   else if (action != DIALOG_NEXT)
	   {
		   incr = nDialogs;
		   err = -1; // could just break here, I suppose . . .
	   }
	   else
	   {
//		   cerr << "next" << endl;
	   }

	   changeIndex(ii, incr, min, nDialogs);
   }

   if (err == 0)
   {
	   if (!_reconfig)
	   {
		   _installInfo->addSection("slapd", _slapdInfo);
		   if (!_installInfo->getSection("admin") && _adminInfo &&
			   !_adminInfo->isEmpty())
		   {
			   _installInfo->addSection("admin", _adminInfo);
			   delete _adminInfo;
			   _adminInfo = 0;
		   }

		   if (!_installInfo->get(SLAPD_KEY_K_LDAP_HOST))
		   {
			   _installInfo->set(SLAPD_KEY_K_LDAP_HOST,
								 _installInfo->get(SLAPD_KEY_FULL_MACHINE_NAME));
		   }
		   if (!_installInfo->get(SLAPD_KEY_K_LDAP_PORT))
		   {
			   _installInfo->set(SLAPD_KEY_K_LDAP_PORT,
								 _slapdInfo->get(SLAPD_KEY_SERVER_PORT));
		   }
		   const char *test = 0;
		   if (!(test = _installInfo->get(SLAPD_KEY_BASE_SUFFIX)) || !*test)
		   {
			   // if there's no config directory suffix we must use
			   // o=NetscapeRoot
			   _installInfo->set(SLAPD_KEY_BASE_SUFFIX, DEFAULT_ROOT_DN);
		   }

		   // only UG directories have a user base suffix . . .
		   if (featureIsEnabled(SLAPD_KEY_USE_EXISTING_UG))
			   _slapdInfo->remove(SLAPD_KEY_SUFFIX);

		   // if there is no LdapURL and other ldap info in the installInfo, write
		   // it
		   if (!_installInfo->get(SLAPD_KEY_K_LDAP_URL))
		   {
			   // construct a new LdapURL based on host, port, and suffix
			   const char *suffix = _installInfo->get(SLAPD_KEY_BASE_SUFFIX);
			   if (!suffix || !*suffix)
				   suffix = DEFAULT_ROOT_DN;
			   NSString ldapURL = NSString("ldap://") +
				   _installInfo->get(SLAPD_KEY_K_LDAP_HOST) + ":" +
				   _installInfo->get(SLAPD_KEY_K_LDAP_PORT) + "/" +
				   suffix;
			   _installInfo->set(SLAPD_KEY_K_LDAP_URL, ldapURL);
		   }

		   if (!featureIsEnabled(SLAPD_KEY_USE_EXISTING_MC))
		   {
			   // if this is to be both the MC and the UG host . . .
			   if (!featureIsEnabled(SLAPD_KEY_USE_EXISTING_UG))
			   {
				   // use the MC admin ID for the UG admin ID
				   if (!_installInfo->get(SLAPD_KEY_USER_GROUP_ADMIN_ID))
					   _installInfo->set(SLAPD_KEY_USER_GROUP_ADMIN_ID,
										 _installInfo->get(SLAPD_KEY_SERVER_ADMIN_ID));

				   if (!_installInfo->get(SLAPD_KEY_USER_GROUP_ADMIN_PWD))
					   _installInfo->set(SLAPD_KEY_USER_GROUP_ADMIN_PWD,
										 _installInfo->get(SLAPD_KEY_SERVER_ADMIN_PWD));
			   }
		   }

		   // set the ug ldap url if we need one
		   if (!_installInfo->get(SLAPD_KEY_USER_GROUP_LDAP_URL))
		   {
			   if (featureIsEnabled(SLAPD_KEY_USE_EXISTING_UG))
			   {
				   NSString url = NSString("ldap://") +
					   _installInfo->get(SLAPD_KEY_UG_HOST) + ":" +
					   _installInfo->get(SLAPD_KEY_UG_PORT) + "/" +
					   _installInfo->get(SLAPD_KEY_UG_SUFFIX);
				   _installInfo->set(SLAPD_KEY_USER_GROUP_LDAP_URL, url);
			   }
			   else // the directory we're creating is the UG
			   {
				   NSString url = NSString("ldap://") +
					   _installInfo->get(SLAPD_KEY_FULL_MACHINE_NAME) + ":" +
					   _slapdInfo->get(SLAPD_KEY_SERVER_PORT) + "/" +
					   _slapdInfo->get(SLAPD_KEY_SUFFIX);
				   _installInfo->set(SLAPD_KEY_USER_GROUP_LDAP_URL, url);
			   }
		   }

		   if (!_installInfo->get(SLAPD_KEY_USER_GROUP_ADMIN_ID))
			   _installInfo->set(SLAPD_KEY_USER_GROUP_ADMIN_ID,
								 _slapdInfo->get(SLAPD_KEY_ROOTDN));

		   if (!_installInfo->get(SLAPD_KEY_USER_GROUP_ADMIN_PWD))
			   _installInfo->set(SLAPD_KEY_USER_GROUP_ADMIN_PWD,
								 _slapdInfo->get(SLAPD_KEY_ROOTDNPWD));
	   } else {
		   // for reconfigure, just shutdown the servers
		   shutdownServers();
	   }

	   // remove the fields we don't need
	   _installInfo->remove(SLAPD_KEY_K_LDAP_HOST);
	   _installInfo->remove(SLAPD_KEY_K_LDAP_PORT);
	   _installInfo->remove(SLAPD_KEY_BASE_SUFFIX);
	   _installInfo->remove(SLAPD_KEY_UG_HOST);
	   _installInfo->remove(SLAPD_KEY_UG_PORT);
	   _installInfo->remove(SLAPD_KEY_UG_SUFFIX);

	   // normalize and convert the DN valued attributes to LDAPv3 style
	   normalizeDNs();

	   // format for .inf file
	   _installInfo->setFormat(1);

	   // convert internally stored UTF8 to local
	   _installInfo->toLocal();
	   _installInfo->write(_infoFile);
   }

   disableWinMode();

   return err;
}

int
SlapdPreInstall::cont()
{
   return 0;
}

void
SlapdPreInstall::clear()
{
}

void
SlapdPreInstall::add(Dialog *p)
{
	p = p;
}
void
SlapdPreInstall::resetLast()
{
}

void
SlapdPreInstall::addLast(Dialog *p)
{
	p = p;
}
void
SlapdPreInstall::setParent(void *parent)
{
	parent = parent;
	return;
}
void *
SlapdPreInstall::parent() const
{
   return (void *) this;
}

void
SlapdPreInstall::setAdminScript(InstallInfo *script)
{
	_adminInfo = script;
}

InstallInfo *
SlapdPreInstall::getAdminScript() const
{
	return _adminInfo;
}

InstallInfo *
SlapdPreInstall::getBaseScript() const
{
	return _installInfo;
}

void
SlapdPreInstall::showAlert(const char *msg)
{
	char *localMsg = UTF8ToLocal(msg);
	DialogAlert alert(localMsg);
	alert.execute();
	nsSetupFree(localMsg);

	return;
}

int
SlapdPreInstall::verifyRemoteLdap(
	const char *host,
	const char *port,
	const char *suffix,
	const char *binddn,
	const char *binddnpwd
) const
{
	const char *myhost = getDefaultScript()->get(host);
	if (!myhost)
		myhost = getBaseScript()->get(host);
	const char *myport = getDefaultScript()->get(port);
	if (!myport)
		myport = getBaseScript()->get(port);
	const char *mysuffix = getDefaultScript()->get(suffix);
	if (!mysuffix)
		mysuffix = getBaseScript()->get(suffix);
	if (!mysuffix)
		mysuffix = DEFAULT_ROOT_DN;
	const char *mydn = getDefaultScript()->get(binddn);
	if (!mydn)
		mydn = getBaseScript()->get(binddn);
	const char *mypwd = getDefaultScript()->get(binddnpwd);
	if (!mypwd)
		mypwd = getBaseScript()->get(binddnpwd);

	char *s = PR_smprintf("ldap://%s:%s/%s", myhost, myport, (suffix && mysuffix) ? mysuffix : "");
	int status = authLdapUser(s, mydn, mypwd, NULL, NULL);
	PR_smprintf_free(s);
	return status;
}

int
SlapdPreInstall::verifyAdminDomain(
	const char *host,
	const char *port,
	const char *suffix,
	const char *admin_domain,
	const char *binddn,
	const char *binddnpwd
) const
{
	const char *myhost = getDefaultScript()->get(host);
	if (!myhost)
		myhost = getBaseScript()->get(host);
	const char *myport = getDefaultScript()->get(port);
	if (!myport)
		myport = getBaseScript()->get(port);
	const char *mysuffix = getDefaultScript()->get(suffix);
	if (!mysuffix)
		mysuffix = getBaseScript()->get(suffix);
	if (!mysuffix)
		mysuffix = DEFAULT_ROOT_DN;
	const char *mydn = getDefaultScript()->get(binddn);
	if (!mydn)
		mydn = getBaseScript()->get(binddn);
	const char *mypwd = getDefaultScript()->get(binddnpwd);
	if (!mypwd)
		mypwd = getBaseScript()->get(binddnpwd);
	const char *myadmin_domain = getDefaultScript()->get(admin_domain);
	if (!myadmin_domain)
		myadmin_domain = getBaseScript()->get(admin_domain);

	char *s = PR_smprintf("ldap://%s:%s/%s", myhost, myport, (suffix && mysuffix) ? mysuffix : "");
	LdapError ldapErr;
	Ldap ldap(ldapErr, s, mydn, mypwd);
	int status = ldapErr;
	if (!status && admin_domain && myadmin_domain && mysuffix)
	{
		LdapEntry ad(&ldap);
		NSString dn = NSString("ou=") + myadmin_domain + ", " + mysuffix;
		status = ad.retrieve(dn);
	}
		
	PR_smprintf_free(s);
	return status;
}

const char *
SlapdPreInstall::getDNSDomain() const
{
	static char domain[BIG_BUF] = {0};

	if (domain[0])
		return domain;

	const char *FQDN =
		getBaseScript()->get(SLAPD_KEY_FULL_MACHINE_NAME);
	if (!FQDN) {
		FQDN = InstUtil::guessHostname();
	}

	const char *ptr = NULL;
	if (FQDN != NULL) {
	// copy the domain name part (not the hostname) into the suffix
	// find the last '.' in the FQDN
	  	ptr = strchr(FQDN, '.');
	}

	if (FQDN == NULL || ptr == NULL) {
	  	const char *guess_domain = InstUtil::guessDomain();
		
		if (guess_domain) {
		  	/* ensure domain is of at least 2 components */
		  	const char *dptr = strchr(guess_domain, '.');
			if (dptr == NULL) {
			  	return NULL;
			}
			
		  	PL_strncpyz(domain, guess_domain, sizeof(domain));
			return domain;
		} else {
		  	return NULL;
		}
	}

	++ptr;
	PL_strncpyz(domain, ptr, sizeof(domain));

	return domain;
}

const char *
SlapdPreInstall::getDefaultSuffix() const
{
	const char *SUF = "dc=";
	const int SUF_LEN = 3;
	static char suffix[BIG_BUF] = {0};

	if (suffix[0])
		return suffix;

	char *sptr = suffix;
	PL_strcatn(sptr, sizeof(suffix), SUF);
	sptr += SUF_LEN;
	for (const char *ptr = getDNSDomain(); ptr && *ptr; *ptr++) {
		if (*ptr == '.') {
			PL_strcatn(sptr, sizeof(suffix), ", ");
			sptr += 2;
			PL_strcatn(sptr, sizeof(suffix), SUF);
			sptr += SUF_LEN;
		} else {
			*sptr++ = *ptr;
		}
	}
	*sptr = 0;
	if (!*suffix)
		PR_snprintf(suffix, sizeof(suffix), "%s%s", SUF, "unknown-domain");

	return suffix;
}

const char *
SlapdPreInstall::getConsumerDN() const
{
	static char dn[BIG_BUF];

	dn[0] = 0;
	const char *suffix = 
		getDefaultScript()->get(SLAPD_KEY_SUFFIX);
	if (suffix)
		PR_snprintf(dn, sizeof(dn), "cn=Replication Consumer, %s", suffix);
	else
		PR_snprintf(dn, sizeof(dn), "cn=Replication Consumer");

	return dn;
}

int
SlapdPreInstall::featureIsEnabled(const char *which) const
{
	const char *val = getDefaultScript()->get(which);
	if (!val)
		val = getBaseScript()->get(which);
	if (!val || !*val || !strncasecmp(val, "no", strlen(val)))
		return 0; // feature is disabled

	return 1; // feature is enabled
}

void
SlapdPreInstall::shutdownServers()
{
	const char *nick = "slapd";
	const char *script = "stop-slapd";
	int len = strlen(nick);
	const char *sroot = getBaseScript()->get(SLAPD_KEY_SERVER_ROOT);
	if (!sroot)
		return;

	DIR* srootdir = opendir(sroot);
	if (!srootdir)
		return;

	struct dirent* entry = 0;
	while ((entry = readdir(srootdir)))
	{
		// look for instance directories
		if (!strncasecmp(entry->d_name, nick, len))
		{
			NSString instanceDir = NSString(sroot) + "/" + entry->d_name;
			if (InstUtil::dirExists(instanceDir))
			{
				NSString prog = instanceDir + "/" + script;
				// call the stop-slapd script
				if (InstUtil::fileExists(prog))
				{
					cout << "Shutting down server " << entry->d_name
						 << " . . . " << flush;
					int status = InstUtil::execProgram(prog);
					if (status)
						// attempt to determine cause of failure
						cout << "Could not shutdown server: status=" << status
							 << " error=" << errno << endl;
					else
						cout << "Done." << endl;
				}
			}
		}
	}
	closedir(srootdir);

	return;
}

void
SlapdPreInstall::normalizeDNs()
{
	static const char *DN_VALUED_ATTRS[] = {
		SLAPD_KEY_SUFFIX,
		SLAPD_KEY_ROOTDN,
		SLAPD_KEY_CIR_SUFFIX,
		SLAPD_KEY_CIR_BINDDN,
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
	for (ii = 0; _slapdInfo && (ii < N); ++ii)
	{
		const char *attr = DN_VALUED_ATTRS[ii];
		char *dn = my_strdup(_slapdInfo->get(attr));
		if (dn)
		{
			_slapdInfo->remove(attr);
			_slapdInfo->set(attr, dn_normalize_convert(dn));
			fflush(stdout);
			delete [] dn;
		}
	}

	for (ii = 0; _installInfo && (ii < NURLS); ++ii)
	{
		const char *attr = URL_ATTRS[ii];
		const char *url = _installInfo->get(attr);
		LDAPURLDesc *desc = 0;
		if (url && !ldap_url_parse((char *)url, &desc) && desc)
		{
			char *dn = dn_normalize_convert(my_strdup(desc->lud_dn));
			if (dn)
			{
				char port[6];
				PR_snprintf(port, sizeof(port), "%d", desc->lud_port);
				NSString newurl = NSString("ldap://") + desc->lud_host +
					":" + port + "/" + dn;
				_installInfo->set(attr, newurl);
				delete [] dn;
			}
		}
		if (desc)
			ldap_free_urldesc(desc);
	}
}
